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
        DEF_LAP_STATIC_ASSERT( sizeof( header ) <= 32, "Header must be exactly 20 bytes" );
        
        //=====================================================================
        // Cache Line 0: ChunkPool State (64B)
        //=====================================================================
        struct alignas( kSystemWordSize ) DEF_LAP_SYS_ALIGN
        {
            Atomic<UInt32> free_list_head;    ///< Free list head index
            Atomic<UInt32> remain_count;      ///< Current free chunks
        } pool_state;
        DEF_LAP_STATIC_ASSERT( sizeof( pool_state ) <= 8, "Header must be exactly 8 bytes" );
        
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
            Atomic<UInt32>      ready_mask;          ///< Bitmask of active subscribers (32 bits)
            Atomic<UInt8>       active_index;        ///< Active snapshot (0 or 1)
            UInt8               active_count[2];     ///< Number of active subscribers in each snapshot
            UInt8               reserved;            ///< Padding
            UInt16              snapshot_offset[2];  ///< Offsets of snapshots in shared memory

            inline UInt8* GetSnapshot( UInt8 index ) noexcept
            {
                DEF_LAP_ASSERT( index < 2, "Snapshot index out of range" );
                return reinterpret_cast< UInt8* >( this + 1 ) + snapshot_offset[index];
            }

            inline const UInt8* GetSnapshot() const noexcept
            {
                DEF_LAP_ASSERT( active_index.load(std::memory_order_acquire) < 2, "Snapshot index out of range" );
                return reinterpret_cast< const UInt8* >( this + 1 ) + snapshot_offset[active_index.load(std::memory_order_acquire)];
            }   

            inline const UInt8* GetSnapshot() noexcept
            {
                DEF_LAP_ASSERT( active_index.load(std::memory_order_acquire) < 2, "Snapshot index out of range" );
                return reinterpret_cast< const UInt8* >( this + 1 ) + snapshot_offset[active_index.load(std::memory_order_acquire)];
            }
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
            registry.ready_mask.store(0, std::memory_order_release);
            registry.active_index.store(0, std::memory_order_release);   
            // Initialize both snapshots
            for ( UInt8 i = 0; i < 2; ++i ) {
                registry.active_count[i] = 0;
                registry.snapshot_offset[i] = i * header.max_channels * sizeof(UInt8);
                UInt8* base = registry.GetSnapshot(i);
                for ( UInt8 j = 0; j < header.max_channels; ++j ) {
                    base[j] = 0xFF;     // Use UInt8 max as invalid
                }
            }
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
        
        /**
         * @brief Get current active snapshot (for Publisher iteration)
         * @return Snapshot copy
         */
        const UInt8* GetSnapshot() const noexcept
        {
            return registry.GetSnapshot();
        }

        UInt8 GetSnapshotCount() const noexcept
        {
            return registry.active_count[ registry.active_index.load(std::memory_order_acquire) ];
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
        UInt8           in;                 ///< Producer index
        UInt8           out;                ///< Consumer index
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
            in = 0xFF;
            out = 0xFF;
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
