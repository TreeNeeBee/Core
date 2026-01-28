/**
 * @file        ControlBlock.hpp
 * @author      LightAP Core Team
 * @brief       Shared memory control block structure
 * @date        2026-01-13
 * @details     Control block with configurable memory footprint (SHRINK/NORMAL/EXTEND)
 * @copyright   Copyright (c) 2026
 * @note        Based on iceoryx2 design
 */
#ifndef LAP_CORE_IPC_CONTROL_BLOCK_HPP
#define LAP_CORE_IPC_CONTROL_BLOCK_HPP

#include "CTypedef.hpp"
#include "IPCTypes.hpp"

namespace lap
{
namespace core
{
namespace ipc
{
    /**
     * @brief Optimized control block with configurable memory footprint
     * @details Size varies by mode:
     * 
     * Memory Layout:
     * - Cache Line 0: Header metadata (64B)
     */ 
    struct ControlBlock // alignas(kCacheLineSize)
    {
        //=====================================================================
        // Cache Line 0: Header Metadata (64B)
        //=====================================================================
        struct alignas( kSystemWordSize ) DEF_LAP_SYS_ALIGN
        {
            Atomic<UInt32> magic;        ///< 0xICE02525 for validation
            IPCType type;                ///< IPC type
            UInt8 reserved;              ///< Padding
            UInt16 max_chunks;           ///< Maximum chunks in pool
            UInt32 chunk_size;           ///< Fixed chunk size (bytes)
            Atomic<UInt32> version;      ///< IPC version
            Atomic<UInt8> ref_count;     ///< Reference count
            UInt8 max_channels;          ///< Maximum channels
            UInt16 channel_capacity;     ///< Capacity per channel subscriber queue (64/256/1024)
        } header;
        DEF_LAP_STATIC_ASSERT( sizeof( header ) <= 32, "Header must be less than 32 bytes" );
        
        //=====================================================================
        // Cache Line 0: ChunkPool State (64B)
        //=====================================================================
        struct alignas( kSystemWordSize ) DEF_LAP_SYS_ALIGN
        {
            Atomic<UInt16> free_list_head;    ///< Free list head index
            Atomic<UInt16> remain_count;      ///< Current free chunks
        } pool_state;
        DEF_LAP_STATIC_ASSERT( sizeof( pool_state ) <= 8, "pool_state must be less than 8 bytes" );
        
        //=====================================================================
        // Cache Line 0 ～ 1: Registry Control (64B)
        //=====================================================================
        // /**
        // * @brief Subscriber snapshot structure (size varies by mode)
        // * @details Used for double-buffered lock-free iteration by Publisher
        // */
        // struct ChannelSnapshot
        // {
        //     UInt8 count;                            ///< Number of active subscribers
        //     UInt8 channel_list[kMaxChannels];       ///< Subscriber queue indices ( 3 in SHRINK, 31 in NORMAL )
        // };

        struct alignas( kSystemWordSize ) DEF_LAP_SYS_ALIGN
        {
            Atomic<UInt64>      read_mask;          ///< Bitmask of active publishers (64 bits)
            Atomic<UInt64>      write_mask;         ///< Bitmask of active subscribers (64 bits)
            Atomic<UInt32>      read_seq;           ///< Sequence for read snapshot updates
            Atomic<UInt32>      write_seq;          /// Sequence for write snapshot updates
        } registry;

        // attached snapshots follow in shared memory, size: kMaxChannels * 2 * sizeof(UInt8)

        //=====================================================================
        // Methods
        //=====================================================================
        
        /**
         * @brief Initialize control block
         * @param max_chunks Maximum chunks in pool
         * @param max_channels Maximum subscribers (≤kMaxSubscribers)
         * @param chunk_size Size of each chunk
         * @param channel_capacity Capacity per queue (≤kQueueCapacity)
         */
        void Initialize(UInt16 max_chunks, 
                       UInt8 max_channels,
                       UInt32 chunk_size,
                       UInt32 channel_capacity = kMaxChannelCapacity) noexcept
        {
            // Initialize header
            header.magic.store(kIPCMagicNumber, std::memory_order_release);
            header.type = IPCType::kSPMC;
            header.version.store(kIPCVersion, std::memory_order_release);
            header.max_chunks = max_chunks;
            header.max_channels = max_channels > kMaxChannels ? kMaxChannels : max_channels;
            header.chunk_size = chunk_size;
            header.ref_count.store( 0, std::memory_order_release );
            header.channel_capacity = DEF_LAP_MIN( channel_capacity, kMaxChannelCapacity );
            
            // Initialize pool state in chunkpool
            pool_state.free_list_head.store( kInvalidChunkIndex, std::memory_order_release );
            pool_state.remain_count.store( max_chunks, std::memory_order_release );
            
            // Initialize registry control
            registry.read_mask.store(0, std::memory_order_release);
            registry.read_seq.store(0, std::memory_order_release);
            registry.write_mask.store(0, std::memory_order_release);
            registry.write_seq.store(0, std::memory_order_release);
        }
        
        /**
         * @brief Validate magic number and version
         * @return true if valid
         */
        Bool Validate() const noexcept
        {
            return header.magic.load(std::memory_order_acquire) == kIPCMagicNumber &&
                   header.version.load(std::memory_order_acquire) == kIPCVersion;
        }

        inline IPCType GetIPCType() const noexcept
        {
            return header.type;
        }
    };
    
    /**
     * @brief Subscriber Queue in shared memory (SPSC queue)
     * @details Lives in shared memory after ControlBlock
     * 
     * Memory Layout:
     * - Each subscriber gets one queue slot
     * - Publisher writes chunk_index (producer)
     * - Subscriber reads chunk_index (consumer)
     */
    struct __attribute__( ( packed ) ) ChannelQueueValue
    {
        alignas(32) UInt16 sequence;        ///< Sequence number
        UInt16 chunk_index;                 ///< Index of chunk in pool
    };

    // cache-line aligned subscriber queue
    struct alignas(kCacheLineSize) ChannelQueue
    {
        Atomic<UInt16>  STmin;              ///< microseconds for min interval
        Atomic<Bool>    active;             ///< Is this queue active
        UInt8           reserved{0};        ///< Padding
        UInt16          capacity;           ///< Queue capacity (power of 2)
        Atomic<UInt8>   in;                 ///< Producer index
        Atomic<UInt8>   out;                ///< Consumer index
        Atomic<UInt16>  head;               ///< Consumer index
        Atomic<UInt16>  tail;               ///< Producer index

        // WaitSet for blocking receive
        Atomic<UInt32> queue_waitset;     ///< Event flags for queue operations
    
        // Ring buffer storage (chunk indices)
        // This array follows this struct in memory
        // UInt32 buffer[capacity];  // Allocated separately

        //=====================================================================
        /**
         * @brief Initialize queue
         * @param cap Capacity (must be power of 2)
         * @param stmin Minimum send interval in microseconds
         */
        void Initialize( UInt16 cap, UInt16 stmin = 0 ) noexcept
        {
            STmin.store( stmin, std::memory_order_release );
            active.store( false, std::memory_order_release );
            capacity = cap;
            in.store(kInvalidChannelID, std::memory_order_release);
            out.store(kInvalidChannelID, std::memory_order_release);
            head.store( 0, std::memory_order_release );
            tail.store( 0, std::memory_order_release );

            queue_waitset.store( EventFlag::kNone, std::memory_order_release );
        }
        
        /**
         * @brief Get buffer start address
         * @return Pointer to buffer array
         */
        inline ChannelQueueValue* GetBuffer() noexcept
        {
            // Buffer follows immediately after this struct
            return reinterpret_cast< ChannelQueueValue* >( this + 1 );
        }
        
        inline const ChannelQueueValue* GetBuffer() const noexcept
        {
            return reinterpret_cast< const ChannelQueueValue* >( this + 1 );
        }

        inline Bool IsActive() const noexcept
        {
            return active.load( std::memory_order_acquire );
        }
    };
}  // namespace ipc
}  // namespace core
}  // namespace lap

#endif  // LAP_CORE_IPC_CONTROL_BLOCK_HPP
