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
#include <iostream>

namespace lap
{
namespace core
{
namespace ipc
{
    Result<void> ChunkPoolAllocator::Initialize() noexcept
    {
        if (!control_)
        {
            return Result<void>(MakeErrorCode(CoreErrc::kInvalidArgument));
        }
        
        // ChunkPool starts at 1MB offset (after ControlBlock + Queue + Reserved regions)
        // chunk_pool_start_ is set in constructor, verify it's at correct offset
        INNER_CORE_LOG("[DEBUG] ChunkPoolAllocator::Initialize\n");
        INNER_CORE_LOG("  - Pool starts at offset: 0x%lX (1MB)\n", 
                       static_cast<unsigned long>(kChunkPoolOffset));
        INNER_CORE_LOG("  - Chunk size: %lu bytes\n", static_cast<unsigned long>(control_->chunk_size));
        INNER_CORE_LOG("  - Max chunks: %lu\n", static_cast<unsigned long>(control_->max_chunks));
        
        // Initialize each chunk and build free list
        for (UInt32 i = 0; i < control_->max_chunks; ++i)
        {
            auto* header = GetChunkAt(i);
            header->Initialize(i, control_->chunk_size);
            
            // Link to next chunk in free list
            if (i + 1 < control_->max_chunks)
            {
                header->next_free_index.store(i + 1, std::memory_order_relaxed);
            }
            else
            {
                header->next_free_index.store(kInvalidChunkIndex, std::memory_order_relaxed);
            }
        }
        
        // Set free list head to first chunk
        control_->free_list_head.store(0, std::memory_order_release);
        control_->allocated_count.store(0, std::memory_order_release);
        
        // Mark as initialized
        control_->MarkInitialized();
        
        return {};
    }
    
    Optional<UInt32> ChunkPoolAllocator::Allocate() noexcept
    {
        // Lock-free allocation using CAS loop
        while (true)
        {
            UInt32 head = control_->free_list_head.load(std::memory_order_acquire);
            
            // Pool exhausted
            if (head == kInvalidChunkIndex)
            {
                static std::atomic<int> log_count{0};
                if (log_count.fetch_add(1) < 3) {
                    INNER_CORE_LOG("[DEBUG] ChunkPool EXHAUSTED: allocated=%u, max=%u, total_allocs=%lu\n",
                                   control_->allocated_count.load(),
                                   control_->max_chunks,
                                   control_->total_allocations.load());
                }
                return {};
            }
            
            auto* header = GetChunkAt(head);
            UInt32 next = header->next_free_index.load(std::memory_order_acquire);
            
            // Try to update head to next
            if (control_->free_list_head.compare_exchange_weak(
                    head, next,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
                // Successfully removed from free list
                
                // Transition state to Loaned
                header->state.store(static_cast<UInt32>(ChunkState::kLoaned), 
                                   std::memory_order_release);
                
                // Increment allocated count
                control_->allocated_count.fetch_add(1, std::memory_order_relaxed);
                control_->total_allocations.fetch_add(1, std::memory_order_relaxed);
                
                // Clear next_free_index
                header->next_free_index.store(kInvalidChunkIndex, std::memory_order_release);
                
                return head;
            }
            
            // CAS failed, retry
        }
    }
    
    void ChunkPoolAllocator::Deallocate(UInt32 chunk_index) noexcept
    {
        if (chunk_index >= control_->max_chunks)
        {
            return;  // Invalid index
        }
        
        auto* header = GetChunkAt(chunk_index);
        
        // Transition state to Free
        header->state.store(static_cast<UInt32>(ChunkState::kFree), 
                           std::memory_order_release);
        
        // Reset reference count
        header->ref_count.store(0, std::memory_order_release);
        
        // Lock-free insertion to free list
        while (true)
        {
            UInt32 head = control_->free_list_head.load(std::memory_order_acquire);
            
            // Set next to current head
            header->next_free_index.store(head, std::memory_order_release);
            
            // Try to update head to this chunk
            if (control_->free_list_head.compare_exchange_weak(
                    head, chunk_index,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
                // Successfully inserted into free list
                
                // Decrement allocated count
                control_->allocated_count.fetch_sub(1, std::memory_order_relaxed);
                control_->total_deallocations.fetch_add(1, std::memory_order_relaxed);
                
                // Signal waiters that a chunk is available
                (void)control_->loan_waitset.fetch_or(
                    EventFlag::kHasFreeChunk,
                    std::memory_order_release
                );
                
                // If flag was not set before, we need to wake waiters
                // (WaitSet implementation will handle futex wake)
                
                return;
            }
            
            // CAS failed, retry
        }
    }
    
    ChunkHeader* ChunkPoolAllocator::GetChunkHeader(UInt32 chunk_index) const noexcept
    {
        if (chunk_index >= control_->max_chunks)
        {
            return nullptr;
        }
        
        return GetChunkAt(chunk_index);
    }
    
    void* ChunkPoolAllocator::GetChunkPayload(UInt32 chunk_index) const noexcept
    {
        auto* header = GetChunkHeader(chunk_index);
        if (!header)
        {
            return nullptr;
        }
        
        return header->GetPayload();
    }
    
    ChunkHeader* ChunkPoolAllocator::GetChunkAt(UInt32 index) const noexcept
    {
        UInt64 chunk_stride = sizeof(ChunkHeader) + control_->chunk_size;
        UInt8* addr = reinterpret_cast<UInt8*>(chunk_pool_start_);
        addr += index * chunk_stride;
        
        return reinterpret_cast<ChunkHeader*>(addr);
    }
    
}  // namespace ipc
}  // namespace core
}  // namespace lap
