/**
 * @file        ChunkPoolAllocator.cpp
 * @author      LightAP Core Team
 * @brief       Implementation of ChunkPoolAllocator
 * @date        2026-01-07
 * @copyright   Copyright (c) 2026
 */

#include "ipc/ChunkPoolAllocator.hpp"
#include "CCoreErrorDomain.hpp"
#include "CMacroDefine.hpp"
#include <cstring>

namespace lap
{
namespace core
{
namespace ipc
{
    Result<void> ChunkPoolAllocator::Initialize() noexcept
    {
        if ( !m_pControl ) {
            return Result<void>::FromError(
                MakeErrorCode(CoreErrc::kInvalidArgument)
            );
        }

        // Check if already initialized (idempotent)
        if ( ( m_pControl->pool_state.free_list_head.load(std::memory_order_acquire) == kInvalidChunkIndex ) && 
                ( m_pControl->pool_state.remain_count.load(std::memory_order_acquire) != 0 ) ) {
            // Calculate and cache aligned chunk stride (once during initialization)
            // Chunk stride must be aligned to kCacheLineSize to avoid false sharing
            // and ensure proper alignment for ChunkHeader (which has alignas(kCacheLineSize))
            m_iChunkStride = DEF_LAP_ALIGN_FORMAT( sizeof( ChunkHeader ) + m_pControl->header.chunk_size, kCacheLineSize );
            
            // ChunkPool starts at 1MB offset (after ControlBlock + Queue + Reserved regions)
            // m_pChunkPoolStart is set in constructor, verify it's at correct offset
            INNER_CORE_LOG("[DEBUG] ChunkPoolAllocator::Initialize\n");
            INNER_CORE_LOG("  - Pool starts at offset: 0x%lX\n", static_cast<unsigned long>(kChunkPoolOffset));
            INNER_CORE_LOG("  - Chunk size: %lu bytes\n", static_cast<unsigned long>(m_pControl->header.chunk_size));
            INNER_CORE_LOG("  - Chunk stride: %lu bytes (aligned)\n", static_cast<unsigned long>(m_iChunkStride));
            INNER_CORE_LOG("  - Max chunks: %lu\n", static_cast<unsigned long>(m_pControl->header.max_chunks));
            
            // Initialize each chunk and build free list
            for ( UInt16 i = 0; i < m_pControl->header.max_chunks; ++i ) {
                auto* header = getChunkAt( i );
                header->Initialize(i, m_pControl->header.chunk_size);
                
                // Link to next chunk in free list
                if ( DEF_LAP_IF_LIKELY ( ( i + 1 ) < m_pControl->header.max_chunks) ) {
                    header->next_free_index.store( i + 1, std::memory_order_relaxed );
                } else {
                    header->next_free_index.store(kInvalidChunkIndex, std::memory_order_relaxed);
                }
            }
            
            // Set free list head to first chunk
            m_pControl->pool_state.free_list_head.store( 0, std::memory_order_release );
            m_pControl->pool_state.remain_count.store( m_pControl->header.max_chunks, std::memory_order_release );
        } else {
            // Already initialized by another process, recalculate stride
            m_iChunkStride = DEF_LAP_ALIGN_FORMAT( sizeof( ChunkHeader ) + m_pControl->header.chunk_size, kCacheLineSize );
        }

        return {};
    }
    
    UInt16 ChunkPoolAllocator::Allocate() noexcept
    {
        // Lock-free allocation using CAS loop
        while ( true ) {
            UInt16 head = m_pControl->pool_state.free_list_head.load(std::memory_order_acquire);
            
            // Pool exhausted
            if ( head == kInvalidChunkIndex ) {
                // static std::atomic<int> log_count{0};
                // if ( log_count.fetch_add(1) < 3 ) {
                //     INNER_CORE_LOG("[DEBUG] ChunkPool EXHAUSTED: remain=%u, max=%u\n",
                //                    m_pControl->pool_state.remain_count.load(),
                //                    m_pControl->header.max_chunks);
                // }
                return kInvalidChunkIndex;
            } else {
                auto* header = getChunkAt( head );
                UInt16 next = header->next_free_index.load(std::memory_order_acquire);
                
                // Try to update head to next
                if ( m_pControl->pool_state.free_list_head.compare_exchange_weak(
                        head, next,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire) ) {
                    // Successfully removed from free list
                    
                    // Transition state to Loaned
                    DEF_LAP_ASSERT( header->state.load(std::memory_order_acquire) == ChunkState::kFree, 
                                    "Allocated chunk must be in Free state" );
                    header->state.store( static_cast< UInt8 >(ChunkState::kLoaned), std::memory_order_release );
                    
                    // Increment allocated count
                    m_pControl->pool_state.remain_count.fetch_sub( 1, std::memory_order_relaxed );
                    
                    // Clear next_free_index
                    header->next_free_index.store( kInvalidChunkIndex, std::memory_order_release );
                    
                    return head;
                }
            }
        }
    }
    
    void ChunkPoolAllocator::Deallocate(UInt16 chunk_index) noexcept
    {
        DEF_LAP_ASSERT( chunk_index < m_pControl->header.max_chunks, "Chunk index out of range" );

        auto* header = getChunkAt(chunk_index);
        
        // Transition state to Free
        header-> state.store( static_cast< UInt8 >( ChunkState::kFree ), std::memory_order_release );

        // Reset reference count
        header->ref_count.store(0, std::memory_order_release);
        
        // Lock-free insertion to free list
        while ( true ) {
            UInt16 head = m_pControl->pool_state.free_list_head.load(std::memory_order_acquire);
            
            // Set next to current head
            header->next_free_index.store(head, std::memory_order_release);
  
            // Try to update head to this chunk
            if ( m_pControl->pool_state.free_list_head.compare_exchange_weak(
                    head, chunk_index,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire) ) {
                // Successfully inserted into free list
                
                // Decrement allocated count
                m_pControl->pool_state.remain_count.fetch_add(1, std::memory_order_relaxed);
                
                break;
            }
        }
    }
    
    ChunkHeader* ChunkPoolAllocator::GetChunkHeader(UInt16 chunk_index) const noexcept
    {
        DEF_LAP_ASSERT( chunk_index < m_pControl->header.max_chunks, "Chunk index out of range" );

        return getChunkAt( chunk_index );
    }
    
    void* ChunkPoolAllocator::GetChunkPayload(UInt16 chunk_index) const noexcept
    {
        auto* header = GetChunkHeader(chunk_index);
        if ( DEF_LAP_IF_LIKELY( header ) ) {
            return header->GetPayload();
        } else {
            return nullptr;
        }
    }
    
    ChunkHeader* ChunkPoolAllocator::getChunkAt(UInt16 index) const noexcept
    {
        // Use pre-calculated aligned chunk stride (calculated once in Initialize)
        UInt8* addr = reinterpret_cast<UInt8*>(m_pChunkPoolStart);
        addr += index * m_iChunkStride;
        
        return reinterpret_cast<ChunkHeader*>(addr);
    }
    
}  // namespace ipc
}  // namespace core
}  // namespace lap
