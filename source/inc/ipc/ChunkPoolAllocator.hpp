/**
 * @file        ChunkPoolAllocator.hpp
 * @author      LightAP Core Team
 * @brief       Lock-free chunk pool allocator
 * @date        2026-01-07
 * @details     Fixed-size memory pool with lock-free allocation
 * @copyright   Copyright (c) 2026
 * @note        Based on iceoryx2 design - zero-copy with offset-based addressing
 */
#ifndef LAP_CORE_IPC_CHUNK_POOL_ALLOCATOR_HPP
#define LAP_CORE_IPC_CHUNK_POOL_ALLOCATOR_HPP

#include "IPCTypes.hpp"
#include "ChunkHeader.hpp"
#include "ControlBlock.hpp"
#include "SharedMemoryManager.hpp"
#include "CResult.hpp"

namespace lap
{
namespace core
{
namespace ipc
{
    /**
     * @brief Lock-free chunk pool allocator
     * @details 
     * - Fixed-size pool initialized at startup
     * - Lock-free allocation/deallocation using CAS
     * - Free-list implemented as index-based linked list
     * - O(1) allocation and deallocation
     * 
     * Memory Layout in Shared Memory:
     * [ControlBlock][sub queue[0]]...[sub queue[100]][ChunkHeader[0]][Payload[0]][ChunkHeader[1]][Payload[1]]...
     */
    class ChunkPoolAllocator
    {
    public:
        /**
         * @brief Constructor
         * @param base_addr Base address of shared memory
         * @param control Control block pointer
         */
        ChunkPoolAllocator(void* base_addr, ControlBlock* control) noexcept
            : m_pBaseAddr(base_addr)
            , m_pControl(control)
            , m_pChunkPoolStart(nullptr)
            , m_iChunkStride(0)
        {
            if ( m_pBaseAddr && m_pControl ) {
                Byte* addr = reinterpret_cast< Byte* >( m_pBaseAddr );
                addr += kChunkPoolOffset;
                m_pChunkPoolStart = reinterpret_cast< ChunkHeader* >(addr);
            }
        }
        
        /**
         * @brief Initialize chunk pool
         * @details Must be called once by the creator
         * @return Result
         */
        Result< void > Initialize() noexcept;
        
        /**
         * @brief Allocate a chunk (lock-free)
         * @return Optional chunk index (empty if pool exhausted)
         */
        UInt16 Allocate() noexcept;
        
        /**
         * @brief Deallocate a chunk (lock-free)
         * @param chunk_index Chunk index to free
         */
        void Deallocate( UInt16 chunk_index ) noexcept;
        
        /**
         * @brief Get chunk header by index
         * @param chunk_index Chunk index
         * @return Chunk header pointer
         */
        ChunkHeader* GetChunkHeader( UInt16 chunk_index ) const noexcept;
        
        /**
         * @brief Get chunk payload by index
         * @param chunk_index Chunk index
         * @return Payload pointer
         */
        void* GetChunkPayload( UInt16 chunk_index ) const noexcept;
        
        /**
         * @brief Check if pool is exhausted
         * @return true if no chunks available
         */
        inline Bool IsExhausted() const noexcept
        {
            return m_pControl->pool_state.free_list_head.load(std::memory_order_acquire) == kInvalidChunkIndex;
        }
        
        /**
         * @brief Get allocated chunk count
         * @return Number of allocated chunks
         */
        inline UInt16 GetAllocatedCount() const noexcept
        {
            return m_pControl->header.max_chunks - m_pControl->pool_state.remain_count.load(std::memory_order_acquire);
        }
        
        /**
         * @brief Get maximum chunks
         * @return Maximum number of chunks
         */
        inline UInt16 GetMaxChunks() const noexcept
        {
            return m_pControl->header.max_chunks;
        }
        
    private:
        /**
         * @brief Calculate chunk start address
         * @param index Chunk index
         * @return Chunk header address
         */
        inline ChunkHeader* getChunkAt(UInt16 index) const noexcept;
        
        void* m_pBaseAddr;              ///< Shared memory base address
        ControlBlock* m_pControl;        ///< Control block
        ChunkHeader* m_pChunkPoolStart; ///< First chunk header address
        UInt64 m_iChunkStride;          ///< Cached aligned stride between chunks
    };
    
}  // namespace ipc
}  // namespace core
}  // namespace lap

#endif  // LAP_CORE_IPC_CHUNK_POOL_ALLOCATOR_HPP
