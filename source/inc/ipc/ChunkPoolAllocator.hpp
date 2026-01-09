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
#include "CResult.hpp"
#include "COptional.hpp"

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
     * [ControlBlock][ChunkHeader[0]][Payload[0]][ChunkHeader[1]][Payload[1]]...
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
            : base_addr_(base_addr)
            , control_(control)
            , chunk_pool_start_(nullptr)
        {
            // ChunkPool starts at fixed offset 2MB (after ControlBlock + Queue regions)
            if (base_addr_ && control_)
            {
                UInt8* addr = reinterpret_cast<UInt8*>(base_addr_);
                addr += kChunkPoolOffset;  // Fixed 2MB offset
                chunk_pool_start_ = reinterpret_cast<ChunkHeader*>(addr);
            }
        }
        
        /**
         * @brief Initialize chunk pool
         * @details Must be called once by the creator
         * @return Result
         */
        Result<void> Initialize() noexcept;
        
        /**
         * @brief Allocate a chunk (lock-free)
         * @return Optional chunk index (empty if pool exhausted)
         */
        Optional<UInt32> Allocate() noexcept;
        
        /**
         * @brief Deallocate a chunk (lock-free)
         * @param chunk_index Chunk index to free
         */
        void Deallocate(UInt32 chunk_index) noexcept;
        
        /**
         * @brief Get chunk header by index
         * @param chunk_index Chunk index
         * @return Chunk header pointer
         */
        ChunkHeader* GetChunkHeader(UInt32 chunk_index) const noexcept;
        
        /**
         * @brief Get chunk payload by index
         * @param chunk_index Chunk index
         * @return Payload pointer
         */
        void* GetChunkPayload(UInt32 chunk_index) const noexcept;
        
        /**
         * @brief Check if pool is exhausted
         * @return true if no chunks available
         */
        bool IsExhausted() const noexcept
        {
            return control_->free_list_head.load(std::memory_order_acquire) == kInvalidChunkIndex;
        }
        
        /**
         * @brief Get allocated chunk count
         * @return Number of allocated chunks
         */
        UInt32 GetAllocatedCount() const noexcept
        {
            return control_->allocated_count.load(std::memory_order_acquire);
        }
        
        /**
         * @brief Get maximum chunks
         * @return Maximum number of chunks
         */
        UInt32 GetMaxChunks() const noexcept
        {
            return control_->max_chunks;
        }
        
    private:
        /**
         * @brief Calculate chunk start address
         * @param index Chunk index
         * @return Chunk header address
         */
        ChunkHeader* GetChunkAt(UInt32 index) const noexcept;
        
        void* base_addr_;              ///< Shared memory base address
        ControlBlock* control_;        ///< Control block
        ChunkHeader* chunk_pool_start_; ///< First chunk header address
    };
    
}  // namespace ipc
}  // namespace core
}  // namespace lap

#endif  // LAP_CORE_IPC_CHUNK_POOL_ALLOCATOR_HPP
