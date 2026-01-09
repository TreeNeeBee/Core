/**
 * @file        ChunkHeader.hpp
 * @author      LightAP Core Team
 * @brief       Chunk header structure for zero-copy IPC
 * @date        2026-01-07
 * @details     Fixed-size header at the beginning of each chunk
 * @copyright   Copyright (c) 2026
 * @note        Based on iceoryx2 design
 */
#ifndef LAP_CORE_IPC_CHUNK_HEADER_HPP
#define LAP_CORE_IPC_CHUNK_HEADER_HPP

#include "IPCTypes.hpp"
#include <atomic>

namespace lap
{
namespace core
{
namespace ipc
{
    /**
     * @brief Header for each chunk in the pool
     * @details Aligned to cache line (64 bytes) to avoid false sharing
     * 
     * State Machine:
     * kFree -> kLoaned (Publisher::Loan)
     * kLoaned -> kSent (Publisher::Send)
     * kSent -> kReceived (Subscriber::Receive)
     * kReceived -> kFree (Sample destructor)
     * kLoaned -> kFree (Sample released without send)
     */
    struct alignas(kCacheLineSize) ChunkHeader
    {
        // ====================================================================
        // Metadata (immutable after pool initialization)
        // ====================================================================
        
        UInt64 chunk_size;           ///< Total chunk size (header + payload)
        UInt32 chunk_index;          ///< Index in the pool
        UInt32 publisher_id;         ///< Publisher ID (for debugging)
        
        // ====================================================================
        // State Machine (atomic)
        // ====================================================================
        
        std::atomic<UInt32> state;   ///< ChunkState enum
        
        // ====================================================================
        // Reference Counting (atomic)
        // ====================================================================
        
        /// Reference count: number of Subscribers holding this chunk
        /// When ref_count reaches 0, chunk returns to free list
        std::atomic<UInt64> ref_count;
        
        // ====================================================================
        // Free-List Linkage (atomic)
        // ====================================================================
        
        /// Next chunk index in free list (kInvalidChunkIndex if end)
        std::atomic<UInt32> next_free_index;
        
        // ====================================================================
        // Sequencing and Timing
        // ====================================================================
        
        UInt64 sequence_number;      ///< Message sequence number
        UInt64 timestamp;            ///< Timestamp (nanoseconds since epoch)
        
        // ====================================================================
        // E2E Protection (optional, for future use)
        // ====================================================================
        
        UInt32 e2e_counter;          ///< E2E routine counter
        UInt32 e2e_crc;              ///< CRC32 checksum
        
        // ====================================================================
        // Methods
        // ====================================================================
        
        /**
         * @brief Initialize chunk header
         * @param index Chunk index in pool
         * @param size Total chunk size
         */
        void Initialize(UInt32 index, UInt64 size) noexcept
        {
            chunk_size = size;
            chunk_index = index;
            publisher_id = 0;
            
            state.store(static_cast<UInt32>(ChunkState::kFree), std::memory_order_release);
            ref_count.store(0, std::memory_order_release);
            next_free_index.store(kInvalidChunkIndex, std::memory_order_release);
            
            sequence_number = 0;
            timestamp = 0;
            
            e2e_counter = 0;
            e2e_crc = 0;
        }
        
        /**
         * @brief Get current state
         * @return Current chunk state
         */
        ChunkState GetState() const noexcept
        {
            return static_cast<ChunkState>(state.load(std::memory_order_acquire));
        }
        
        /**
         * @brief Transition state atomically
         * @param expected Expected current state
         * @param desired Desired new state
         * @return true if transition succeeded
         */
        bool TransitionState(ChunkState expected, ChunkState desired) noexcept
        {
            UInt32 expected_val = static_cast<UInt32>(expected);
            UInt32 desired_val = static_cast<UInt32>(desired);
            
            return state.compare_exchange_strong(
                expected_val,
                desired_val,
                std::memory_order_acq_rel,
                std::memory_order_acquire
            );
        }
        
        /**
         * @brief Increment reference count
         * @return New reference count
         */
        UInt64 IncrementRef() noexcept
        {
            return ref_count.fetch_add(1, std::memory_order_acq_rel) + 1;
        }
        
        /**
         * @brief Decrement reference count
         * @return New reference count
         */
        UInt64 DecrementRef() noexcept
        {
            return ref_count.fetch_sub(1, std::memory_order_acq_rel) - 1;
        }
        
        /**
         * @brief Get payload pointer (after header)
         * @return Payload address
         */
        void* GetPayload() noexcept
        {
            return reinterpret_cast<UInt8*>(this) + sizeof(ChunkHeader);
        }
        
        /**
         * @brief Get payload pointer (const)
         * @return Payload address
         */
        const void* GetPayload() const noexcept
        {
            return reinterpret_cast<const UInt8*>(this) + sizeof(ChunkHeader);
        }
        
        /**
         * @brief Get chunk header from payload pointer
         * @param payload Payload pointer
         * @return Chunk header pointer
         */
        static ChunkHeader* FromPayload(void* payload) noexcept
        {
            return reinterpret_cast<ChunkHeader*>(
                reinterpret_cast<UInt8*>(payload) - sizeof(ChunkHeader)
            );
        }
    };
    
    static_assert(sizeof(ChunkHeader) == kCacheLineSize, 
                  "ChunkHeader must be exactly one cache line");
    
}  // namespace ipc
}  // namespace core
}  // namespace lap

#endif  // LAP_CORE_IPC_CHUNK_HEADER_HPP
