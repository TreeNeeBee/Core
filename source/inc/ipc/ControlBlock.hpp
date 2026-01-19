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

#include "IPCTypes.hpp"
#include "IPCConfig.hpp"
#include <atomic>

namespace lap
{
namespace core
{
namespace ipc
{
    /**
     * @brief Optimized control block with configurable memory footprint
     * @details Size varies by mode:
     * - SHRINK: 256B (4 cache lines, no snapshots)
     * - NORMAL: 576B (9 cache lines, double-buffered snapshots)
     * - EXTEND: 576B (9 cache lines, double-buffered snapshots)
     * 
     * Memory Layout:
     * - Cache Line 0: Header metadata (64B)
     * - Cache Line 1: ChunkPool state (64B)
     * - Cache Line 2: Registry control (64B)
     * - Cache Line 3+: Snapshots (SHRINK: 64B, NORMAL/EXTEND: 384B for 2×192B)
     */
    struct alignas(kCacheLineSize) ControlBlock
    {
        //=====================================================================
        // Cache Line 0: Header Metadata (64B)
        //=====================================================================
        struct DEF_LAP_SYS_ALIGN
        {
            std::atomic<UInt32> magic;        ///< 0xICE02525 for validation
            UInt32 max_chunks;                ///< Maximum chunks in pool
            UInt32 chunk_size;                ///< Fixed chunk size (bytes)
            std::atomic<UInt32> version;      ///< IPC version
            std::atomic<UInt8> ref_count;     ///< Reference count
            UInt8 max_subscribers;            ///< Maximum subscribers
            UInt16 queue_capacity;            ///< Capacity per subscriber queue (64/256/1024)
        } header;
        DEF_LAP_STATIC_ASSERT( sizeof( header ) <= 32, "Header must be exactly 20 bytes" );
        
        //=====================================================================
        // Cache Line 0: ChunkPool State (64B)
        //=====================================================================
        struct DEF_LAP_SYS_ALIGN
        {
            std::atomic<UInt32> free_list_head;  ///< Free list head index
            std::atomic<UInt32> remain_count;      ///< Current free chunks
        } pool_state;
        DEF_LAP_STATIC_ASSERT( sizeof( pool_state ) == 8, "Header must be exactly 8 bytes" );
        
        //=====================================================================
        // Cache Line 1: Registry Control (64B)
        //=====================================================================
        /**
        * @brief Subscriber snapshot structure (size varies by mode)
        * @details Used for double-buffered lock-free iteration by Publisher
        */
        struct DEF_LAP_SYS_ALIGN SubscriberSnapshot
        {
            UInt8 count;                            ///< Number of active subscribers
            UInt8 version;                          ///< Snapshot version (incremented on update)
            UInt8 queue_indices[kMaxSubscribers];   ///< Subscriber queue indices (2 in SHRINK, 30 in NORMAL, 62 in EXTEND)
        };

        struct DEF_LAP_SYS_ALIGN
        {
        #ifdef LIGHTAP_IPC_MODE_SHRINK
            struct DEF_LAP_SYS_ALIGN
            {
                std::atomic<UInt8> ready_mask;           ///< Bitmask of active subscribers (2 bits)
                std::atomic<UInt8> active_snapshot_index;///< Active snapshot (0 or 1)
                std::atomic<UInt8> subscriber_count;     ///< Active subscriber count
                UInt8 reserved;
            } registry_control;
            DEF_LAP_STATIC_ASSERT( sizeof( registry_control ) <= sizeof(void*), "Header must be exactly 4 bytes" );

            SubscriberSnapshot snapshots[2];
        #elif defined(LIGHTAP_IPC_MODE_NORMAL)
            struct DEF_LAP_SYS_ALIGN
            {
                std::atomic<UInt32> ready_mask;           ///< Bitmask of active subscribers (32 bits)
                std::atomic<UInt8> active_snapshot_index;///< Active snapshot (0 or 1)
                std::atomic<UInt8> subscriber_count;     ///< Active subscriber count
                UInt8 reserved[2];                       ///< Align to 64 bytes
            } registry_control;
            DEF_LAP_STATIC_ASSERT( sizeof( registry_control ) == 8, "Header must be exactly 8 bytes" );

            SubscriberSnapshot snapshots[2];
        #else // EXTEND
            struct DEF_LAP_SYS_ALIGN
            {
                std::atomic<UInt64> ready_mask;           ///< Bitmask of active subscribers (32 bits)
                std::atomic<UInt8> active_snapshot_index;///< Active snapshot (0 or 1)
                std::atomic<UInt8> subscriber_count;     ///< Active subscriber count
                UInt8 reserved[6];                       ///< Align to 64 bytes
            } registry_control;
            DEF_LAP_STATIC_ASSERT( sizeof( registry_control ) == 16, "Header must be exactly 16 bytes" );

            SubscriberSnapshot snapshots[2];
        #endif  
        } registry;

        #if defined(LIGHTAP_IPC_MODE_SHRINK)
            static_assert( sizeof(SubscriberSnapshot) <= 8, "SubscriberSnapshot must fit in 8 bytes as SHRINK mode" );
        #elif defined(LIGHTAP_IPC_MODE_NORMAL)
            static_assert(sizeof(SubscriberSnapshot) <= 64, "SubscriberSnapshot must fit in 64 bytes as NORMAL mode");
        #else // EXTEND
            static_assert(sizeof(SubscriberSnapshot) <= 128, "SubscriberSnapshot must fit in 128 bytes as EXTEND mode");
        #endif

        //=====================================================================
        // Methods
        //=====================================================================
        
        /**
         * @brief Initialize control block
         * @param max_chunks Maximum chunks in pool
         * @param max_subscribers Maximum subscribers (≤kMaxSubscribers)
         * @param chunk_size Size of each chunk
         * @param queue_capacity Capacity per queue (≤kQueueCapacity)
         */
        void Initialize(UInt32 max_chunks, 
                       UInt8 max_subscribers,
                       UInt32 chunk_size,
                       UInt32 queue_capacity = kQueueCapacity) noexcept
        {
            // Initialize header
            header.magic.store(kIPCMagicNumber, std::memory_order_release);
            header.version.store(kIPCVersion, std::memory_order_release);
            header.max_chunks = max_chunks;
            header.max_subscribers = max_subscribers > kMaxSubscribers ? kMaxSubscribers : max_subscribers;
            header.chunk_size = chunk_size;
            header.ref_count.store( 0, std::memory_order_release );
            header.queue_capacity = queue_capacity > kQueueCapacity ? kQueueCapacity : queue_capacity;
            
            // Initialize pool state in chunkpool
            pool_state.free_list_head.store( kInvalidChunkIndex, std::memory_order_release );
            pool_state.remain_count.store( max_chunks, std::memory_order_release );
            
            // Initialize registry control
            registry.registry_control.ready_mask.store(0, std::memory_order_release);
            registry.registry_control.active_snapshot_index.store(0, std::memory_order_release);
            registry.registry_control.subscriber_count.store(0, std::memory_order_release);
            
            // Initialize both snapshots
            for ( UInt8 i = 0; i < 2; ++i ) {
                registry.snapshots[i].count = 0;
                registry.snapshots[i].version = 0;
                for ( UInt8 j = 0; j < kMaxSubscribers; ++j ) {
                    registry.snapshots[i].queue_indices[j] = 0xFF;  // Use UInt8 max as invalid
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
        SubscriberSnapshot GetSnapshot() const noexcept
        {
            UInt32 active_idx = registry.registry_control.active_snapshot_index.load(std::memory_order_acquire);
            SubscriberSnapshot result = registry.snapshots[active_idx];
            std::atomic_thread_fence(std::memory_order_acquire);
            return result;
        }
        
        /**
         * @brief Get subscriber count
         * @return Number of active subscribers
         */
        UInt32 GetSubscriberCount() const noexcept
        {
            return registry.registry_control.subscriber_count.load(std::memory_order_acquire);
        }
    };

    #if defined(LIGHTAP_IPC_MODE_SHRINK)
        DEF_LAP_STATIC_ASSERT( sizeof(ControlBlock) <= 64, "ControlBlock must fit in 64 bytes as SHRINK mode" );
    #endif
    
    /**
     * @brief Subscriber Queue in shared memory (SPSC queue)
     * @details Lives in shared memory after ControlBlock
     * 
     * Memory Layout:
     * - Each subscriber gets one queue slot
     * - Publisher writes chunk_index (producer)
     * - Subscriber reads chunk_index (consumer)
     */
    struct alignas(kCacheLineSize) SubscriberQueue
    {
        std::atomic<UInt8> STmin;             ///< ms for min interval
        
    #if defined(LIGHTAP_IPC_MODE_SHRINK)
        UInt8 capacity;                       ///< Fixed capacity (power of 2)
        std::atomic<UInt8> head;              ///< Consumer index
        std::atomic<UInt8> tail;              ///< Producer index
    #elif defined(LIGHTAP_IPC_MODE_NORMAL)
        UInt16 capacity;                       ///< Fixed capacity (power of 2)
        std::atomic<UInt16> head;              ///< Consumer index
        std::atomic<UInt16> tail;              ///< Producer index
    #else // EXTEND
        UInt32 capacity;                       ///< Fixed capacity (power of 2)
        std::atomic<UInt32> head;              ///< Consumer index
        std::atomic<UInt32> tail;              ///< Producer index      
    #endif

        // WaitSet for blocking receive
        std::atomic<UInt32> queue_waitset;     ///< Event flags for queue operations
    
        // Ring buffer storage (chunk indices)
        // This array follows this struct in memory
        // UInt32 buffer[capacity];  // Allocated separately

        //=====================================================================
        /**
         * @brief Initialize queue
         * @param cap Capacity (must be power of 2)
         */
        void Initialize( UInt32 cap, UInt8 stmin = 0 ) noexcept
        {
            capacity = cap;

            STmin.store( stmin, std::memory_order_release );
            head.store( 0, std::memory_order_release );
            tail.store( 0, std::memory_order_release );

            queue_waitset.store( EventFlag::kNone, std::memory_order_release );
        }
        
        /**
         * @brief Get buffer start address
         * @return Pointer to buffer array
         */
        inline UInt32* GetBuffer() noexcept
        {
            // Buffer follows immediately after this struct
            return reinterpret_cast< UInt32* >( this + 1 );
        }
        
        inline const UInt32* GetBuffer() const noexcept
        {
            return reinterpret_cast< const UInt32* >( this + 1 );
        }
    };

    constexpr static UInt32 kControlBlockSize = sizeof(ControlBlock);
    constexpr static UInt32 kSubscriberQueueSize = sizeof( SubscriberQueue ) + kQueueCapacity * sizeof( UInt32 ); 
    
    constexpr static UInt32 kQueueRegionOffset = kControlBlockSize;
    constexpr static UInt32 kChunkPoolOffset = kControlBlockSize + kSubscriberQueueSize * kMaxSubscribers;

}  // namespace ipc
}  // namespace core
}  // namespace lap

#endif  // LAP_CORE_IPC_CONTROL_BLOCK_HPP
