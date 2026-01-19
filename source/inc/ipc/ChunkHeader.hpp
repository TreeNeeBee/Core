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
     * @brief Header for each chunk in the pool (optimized 16B)
     * @details Minimal header aligned to 8 bytes
     * 
     * State Machine:
     * kFree -> kLoaned (Publisher::Loan)
     * kLoaned -> kSent (Publisher::Send)
     * kSent -> kReceived (Subscriber::Receive)
     * kReceived -> kFree (Sample destructor)
     * kLoaned -> kFree (Sample released without send)
     * 
     * Design Notes:
     * - Removed: chunk_size (global constant in ControlBlock)
     * - Removed: sequence_number, timestamp, publisher_id (moved to MessageHeader in payload)
     * - Removed: e2e_counter, e2e_crc (application-layer concern)
     * - Size: 128B → 64B → 16B (87.5% reduction, SHRINK mode optimized)
     * 
     * Memory Layout (16 bytes):
     *   [0-1]  ref_count + state (2B atomic)
     *   [2-3]  payload_offset (2B)
     *   [4-7]  payload_size (4B)
     *   [8-11] next_free_index (4B atomic)
     *   [12-15] chunk_index (4B)
     */
    struct DEF_LAP_SYS_ALIGN ChunkHeader
    {
        //=====================================================================
        // Reference Counting (8B)
        //=====================================================================
        
        /// Reference count: number of Subscribers holding this chunk
        /// When ref_count reaches 0, chunk returns to free list
        std::atomic<UInt8>  ref_count;  
        std::atomic<UInt8>  state;              ///< ChunkState enum
        UInt16              crc_;               ///< CRC for data integrity
        UInt32              payload_size_;      ///< Size of user payload
        
        //=====================================================================
        // Free-List Linkage (4B)
        //=====================================================================
        
        /// Next chunk index in free list (kInvalidChunkIndex if end)
        std::atomic<UInt32> next_free_index;
        
        //=====================================================================
        UInt32 chunk_index;          ///< Index in the pool  

        // Methods
        //=====================================================================
        
        /**
         * @brief Initialize chunk header
         * @param index Chunk index in pool
         * @param payload_offset Offset to user payload
         */
        void Initialize( UInt32 index, UInt32 payload_size ) noexcept
        {
            chunk_index = index;
            payload_size_ = payload_size;

            #if defined( LIGHTAP_IPC_MODE_SHRINK )
                // SHRINK mode: validate size, make sure header + payload fits in cache line
                DEF_LAP_STATIC_ASSERT_CACHELINE_MATCH( sizeof( ChunkHeader ) + payload_size_, kCacheLineSize );
            #endif

            state.store( static_cast< UInt8 >( ChunkState::kFree ), std::memory_order_release );
            ref_count.store( 0, std::memory_order_release) ;
            next_free_index.store( kInvalidChunkIndex, std::memory_order_release );        
        }

        /**
         * @brief Get user payload pointer (header + offset)
         * @return User payload address
         */
        inline void* GetPayload() noexcept
        {
            return reinterpret_cast<UInt8*>(this) + sizeof( ChunkHeader );
        }
        
        /**
         * @brief Get user payload pointer (const)
         * @return User payload address
         */
        inline const void* GetPayload() const noexcept
        {
            return reinterpret_cast<const UInt8*>(this) + sizeof( ChunkHeader );
        }
        
        /**
         * @brief Get chunk header from payload pointer
         * @param payload User payload pointer
         * @param payload_offset Offset used during initialization
         * @return Chunk header pointer
         */
        static inline ChunkHeader* FromPayload( void* payload, UInt16 payload_offset ) noexcept
        {
            return reinterpret_cast< ChunkHeader* >(
                reinterpret_cast<UInt8*>( payload ) - sizeof( ChunkHeader ) - payload_offset
            );
        }
    };
    
    static_assert( sizeof( ChunkHeader ) == 16, 
                  "ChunkHeader must be exactly 16 bytes");
    
}  // namespace ipc
}  // namespace core
}  // namespace lap

#endif  // LAP_CORE_IPC_CHUNK_HEADER_HPP
