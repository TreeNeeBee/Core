/**
 * @file        ControlBlock.hpp
 * @author      LightAP Core Team
 * @brief       Shared memory control block structure
 * @date        2026-01-07
 * @details     Control block resides at the beginning of shared memory segment
 * @copyright   Copyright (c) 2026
 * @note        Based on iceoryx2 design
 */
#ifndef LAP_CORE_IPC_CONTROL_BLOCK_HPP
#define LAP_CORE_IPC_CONTROL_BLOCK_HPP

#include "IPCTypes.hpp"
#include <atomic>

namespace lap
{
namespace core
{
namespace ipc
{
    /**
     * @brief Control block at the start of shared memory
     * @details Contains metadata and management structures for the IPC segment
     * 
     * Memory Layout:
     * - Control block metadata
     * - ChunkPool management (free-list, counters)
     * - WaitSet for loan failures
     */
    struct alignas(kCacheLineSize) ControlBlock
    {
        // ====================================================================
        // Magic and Version
        // ====================================================================
        
        std::atomic<UInt32> magic_number;     ///< 0xICE02525 for validation
        std::atomic<UInt32> version;          ///< IPC version
        std::atomic<UInt32> state;            ///< Initialization state
        UInt32 padding0;                      ///< Padding for alignment
        
        // ====================================================================
        // Service Configuration (immutable after initialization)
        // ====================================================================
        
        UInt32 max_chunks;                    ///< Maximum number of chunks
        UInt32 max_subscriber_queues;         ///< Maximum subscriber queues
        UInt32 queue_capacity;                ///< Capacity per queue
        UInt32 padding1;                      ///< Padding
        
        UInt64 chunk_size;                    ///< Size of each chunk (including header)
        UInt64 chunk_alignment;               ///< Chunk alignment requirement
        
        // ====================================================================
        // ChunkPool Management (lock-free)
        // ====================================================================
        
        std::atomic<UInt32> free_list_head;   ///< Head of free list (chunk index)
        std::atomic<UInt32> allocated_count;  ///< Number of allocated chunks
        std::atomic<bool>   is_initialized;   ///< Initialization complete flag
        UInt8 padding2[3];                    ///< Padding
        
        // ====================================================================
        // WaitSet for Loan Failures
        // ====================================================================
        
        /// Event flags for loan wait mechanism
        /// bit 0: HAS_FREE_CHUNK - ChunkPool has available chunks
        std::atomic<UInt32> loan_waitset;
        
        // ====================================================================
        // Statistics (atomic counters)
        // ====================================================================
        
        std::atomic<UInt32> publisher_count;   ///< Active publisher count
        std::atomic<UInt32> subscriber_count;  ///< Active subscriber count
        
        std::atomic<UInt64> total_allocations; ///< Total allocations
        std::atomic<UInt64> total_deallocations; ///< Total deallocations
        
        // ====================================================================
        // SubscriberRegistry (lock-free double-buffered snapshot)
        // ====================================================================
        
        /// Snapshot structure for subscriber queue indices
        struct SubscriberSnapshot
        {
            UInt32 count;                          ///< Number of active subscribers
            UInt32 queue_indices[kMaxSubscribers]; ///< Array of queue indices
            UInt64 version;                        ///< Version number for change detection
            UInt32 padding[3];                     ///< Padding to cache line boundary
            
            SubscriberSnapshot() noexcept
                : count(0), version(0)
            {
                for (UInt32 i = 0; i < kMaxSubscribers; ++i)
                {
                    queue_indices[i] = kInvalidChunkIndex;
                }
                padding[0] = padding[1] = padding[2] = 0;
            }
        };
        
        /// Double-buffered snapshots for lock-free read
        std::atomic<UInt32> active_snapshot_index; ///< Current active snapshot (0 or 1)
        SubscriberSnapshot  snapshots[2];          ///< Double buffer
        std::atomic<UInt32> write_index;           ///< Current write buffer index
        std::atomic<UInt32> next_queue_index;      ///< Allocator for unique queue indices
        
        // ====================================================================
        // Methods
        // ====================================================================
        
        /**
         * @brief Initialize control block
         * @param max_chunks Maximum chunks in pool
         * @param chunk_size Size of each chunk
         * @param max_queues Maximum subscriber queues
         * @param queue_cap Queue capacity
         */
        void Initialize(UInt32 max_chunks, 
                       UInt64 chunk_size,
                       UInt32 max_queues = kMaxSubscribers,
                       UInt32 queue_cap = kDefaultQueueCapacity) noexcept
        {
            magic_number.store(kIPCMagicNumber, std::memory_order_release);
            version.store(kIPCVersion, std::memory_order_release);
            state.store(0, std::memory_order_release);
            
            this->max_chunks = max_chunks;
            this->chunk_size = chunk_size;
            this->max_subscriber_queues = max_queues;
            this->queue_capacity = queue_cap;
            this->chunk_alignment = kCacheLineSize;
            
            free_list_head.store(0, std::memory_order_release);
            allocated_count.store(0, std::memory_order_release);
            is_initialized.store(false, std::memory_order_release);
            
            loan_waitset.store(EventFlag::kHasFreeChunk, std::memory_order_release);
            
            publisher_count.store(0, std::memory_order_release);
            subscriber_count.store(0, std::memory_order_release);
            
            total_allocations.store(0, std::memory_order_release);
            total_deallocations.store(0, std::memory_order_release);
            
            // Initialize SubscriberRegistry
            active_snapshot_index.store(0, std::memory_order_release);
            write_index.store(0, std::memory_order_release);
            next_queue_index.store(0, std::memory_order_release);
            snapshots[0] = SubscriberSnapshot();
            snapshots[1] = SubscriberSnapshot();
        }
        
        /**
         * @brief Validate magic number and version
         * @return true if valid
         */
        bool Validate() const noexcept
        {
            return magic_number.load(std::memory_order_acquire) == kIPCMagicNumber &&
                   version.load(std::memory_order_acquire) == kIPCVersion;
        }
        
        /**
         * @brief Mark initialization as complete
         */
        void MarkInitialized() noexcept
        {
            is_initialized.store(true, std::memory_order_release);
        }
        
        /**
         * @brief Check if initialized
         * @return true if initialized
         */
        bool IsInitialized() const noexcept
        {
            return is_initialized.load(std::memory_order_acquire);
        }
    };
    
    static_assert(sizeof(ControlBlock) % kCacheLineSize == 0, 
                  "ControlBlock must be cache-line aligned");
    
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
        std::atomic<bool> active;              ///< Queue is allocated
        UInt32 subscriber_id;                  ///< Subscriber ID
        std::atomic<UInt32> queue_full_policy; ///< Policy when full
        UInt32 padding0;                       ///< Padding
        
        // SPSC Ring Buffer fields
        std::atomic<UInt32> head;              ///< Consumer index
        std::atomic<UInt32> tail;              ///< Producer index
        UInt32 capacity;                       ///< Fixed capacity (power of 2)
        UInt32 padding1;                       ///< Padding
        
        // Statistics
        std::atomic<UInt64> overrun_count;     ///< Messages dropped due to full
        std::atomic<UInt64> last_receive_time; ///< Last receive timestamp
        
        // Ring buffer storage (chunk indices)
        // This array follows this struct in memory
        // UInt32 buffer[capacity];  // Allocated separately
        
        /**
         * @brief Initialize queue
         * @param cap Capacity (must be power of 2)
         */
        void Initialize(UInt32 cap) noexcept
        {
            active.store(false, std::memory_order_release);
            subscriber_id = 0;
            queue_full_policy.store(static_cast<UInt32>(QueueFullPolicy::kOverwrite), 
                                   std::memory_order_release);
            
            head.store(0, std::memory_order_release);
            tail.store(0, std::memory_order_release);
            capacity = cap;
            
            overrun_count.store(0, std::memory_order_release);
            last_receive_time.store(0, std::memory_order_release);
        }
        
        /**
         * @brief Get buffer start address
         * @return Pointer to buffer array
         */
        UInt32* GetBuffer() noexcept
        {
            // Buffer follows immediately after this struct
            return reinterpret_cast<UInt32*>(this + 1);
        }
        
        const UInt32* GetBuffer() const noexcept
        {
            return reinterpret_cast<const UInt32*>(this + 1);
        }
    };
    
}  // namespace ipc
}  // namespace core
}  // namespace lap

#endif  // LAP_CORE_IPC_CONTROL_BLOCK_HPP
