/**
 * @file CSharedMemoryAllocator.hpp
 * @brief Lock-free Zero-Copy Shared Memory Allocator (iceoryx2-style)
 * @author LightAP Team
 * @date 2025-12-26
 * 
 * @details
 * 完全无锁的零拷贝共享内存分配器，基于 iceoryx2 设计理念：
 * - Lock-free MPMC (Multi-Producer Multi-Consumer) chunk pool
 * - Atomic CAS operations for all state transitions
 * - Memory-order optimizations for minimal overhead
 * - Cache-aligned data structures to avoid false sharing
 * - Safe overflow to jemalloc/malloc when pool exhausted
 * 
 * Architecture (iceoryx2-inspired):
 * - ChunkHeader: Atomic reference counting + state machine
 * - Free List: Lock-free stack using CAS on head pointer
 * - Sample Pool: Pre-allocated aligned memory chunks
 * - Publisher: loan() -> write data -> send() (release ownership)
 * - Subscriber: receive() -> read data -> release() (decrement refcount)
 * 
 * Performance Characteristics:
 * - Zero-copy: Direct memory access, no memcpy
 * - Wait-free loan: O(1) pop from free list
 * - Lock-free send/receive: Atomic state transitions
 * - Bounded latency: No locks = no priority inversion
 * 
 * @note Compatible with jemalloc as system allocator fallback
 */

#ifndef LAP_CORE_SHARED_MEMORY_ALLOCATOR_HPP
#define LAP_CORE_SHARED_MEMORY_ALLOCATOR_HPP

#include "CTypedef.hpp"
#include "CResult.hpp"
#include "memory/CMessageQueueBlock.hpp"  // iceoryx2-style ringbuffer queue
#include <atomic>
#include <cstddef>
#include <mutex>
#include <condition_variable>
#include <memory>

namespace lap::core {

// Forward declarations
class WaitSet;
class CSegmentState;  // ✅ NEW: Segment state for dual-counter

// Cache line size for alignment (avoid false sharing)
static constexpr size_t CACHE_LINE_SIZE = 64;

/**
 * @brief Chunk state machine (iceoryx2-style)
 */
enum class ChunkState : uint32_t {
    FREE = 0,       ///< In free list, available for loan
    LOANED = 1,     ///< Loaned to publisher, being written
    SENT = 2,       ///< Sent by publisher, ready for receive
    IN_USE = 3,     ///< Received by subscriber, being read
    INVALID = 4     ///< From freed segment, cannot be used (after shrink/munmap)
};

/**
 * @brief Chunk header with atomic state (cache-aligned to avoid false sharing)
 * 
 * Layout inspired by iceoryx2's ChunkHeader:
 * - Atomic state for lock-free transitions
 * - Atomic reference count for multi-subscriber support
 * - Sequence number + Publisher ID for ABA problem prevention
 * - Next pointer for message queue (iceoryx2 FIFO)
 */
struct alignas(CACHE_LINE_SIZE) ChunkHeader {
    std::atomic<ChunkState> state;        ///< Current chunk state
    // ❌ REMOVED: std::atomic<uint32_t> ref_count; (moved to SegmentState::sample_reference_counter_)
    std::atomic<uint64_t>   sequence;     ///< Sequence number (ABA prevention)
    Size                    payload_size; ///< User data size
    UInt64                  chunk_id;     ///< Unique chunk identifier
    UInt32                  publisher_id; ///< Publisher ID (for message queue ownership)
    void*                   user_payload; ///< Pointer to user data area
    ChunkHeader*            next_free;    ///< Next in free list (valid only when FREE)
    ChunkHeader*            next_msg;     ///< Next in message queue (valid when SENT)
    
    ChunkHeader() noexcept 
        : state(ChunkState::FREE)
        , sequence(0)
        , payload_size(0)
        , chunk_id(0)
        , publisher_id(0)
        , user_payload(nullptr)
        , next_free(nullptr)
        , next_msg(nullptr)
    {}
};

/**
 * @brief Publisher handle (iceoryx2-style ownership model)
 */
struct PublisherHandle {
    UInt32  publisher_id;    ///< Unique publisher ID (1-based index)
    // iceoryx2-style: No internal pointers, use ID to index into publishers_ array
};

/**
 * @brief Subscriber handle (iceoryx2-style)
 * Uses pure ID mechanism: no internal pointers to avoid shared memory invalidation
 */
struct SubscriberHandle {
    UInt32  subscriber_id;   ///< Unique subscriber ID (1-based index into subscribers_ array)
    // iceoryx2-style: No internal pointers, always resolve via subscribers_[id-1]
};

/**
 * @brief Memory block descriptor (user-facing handle with ownership)
 * 
 * iceoryx2 ownership model:
 * - Loaned blocks are owned by publisher until send()
 * - After send(), ownership transfers to allocator
 * - Received blocks are owned by subscriber until release()
 */
struct SharedMemoryMemoryBlock {
    void*         ptr;          ///< User data pointer
    Size          size;         ///< Data size
    UInt64        chunk_id;     ///< Chunk identifier
    ChunkHeader*  chunk_header; ///< Internal chunk header (opaque)
    Bool          is_loaned;    ///< Loaned flag (for safety checks)
    UInt32        owner_id;     ///< Publisher/Subscriber ID (ownership tracking)
};

// Forward declaration
class SharedMemoryAllocator;

/**
 * @brief Allocation policy when pool is exhausted (iceoryx2-style)
 */
enum class AllocationPolicy {
    WAIT_SYNC,       ///< Block with spin-wait (loop + yield), high performance, high CPU
    WAIT_ASYNC,      ///< Block with condition variable, low CPU, slight overhead
    ABORT_ON_FULL,   ///< Return error immediately if pool exhausted
    USE_OVERFLOW     ///< Fallback to jemalloc/malloc (if enabled)
};

/**
 * @brief Subscriber queue overflow policy (iceoryx2-style)
 */
enum class QueueOverflowPolicy {
    DISCARD_OLDEST,  ///< Drop oldest message when queue full (ring buffer)
    DISCARD_NEWEST,  ///< Drop new message when queue full (default)
    BLOCK_PUBLISHER  ///< Block publisher until queue has space (not implemented)
};

/**
 * @brief Allocator configuration (iceoryx2-inspired)
 */
struct SharedMemoryAllocatorConfig {
    // ===== Shared Memory IPC Settings =====
    std::string shm_name;          ///< Shared memory name (for IPC, empty=anonymous)
    void*   shm_base_address;      ///< Fixed base address for SHM (nullptr=let OS choose)
    Bool    is_creator;            ///< true=create new SHM, false=attach to existing
    
    // ===== Segment-based allocation (mmap blocks, 2MB aligned) =====
    Size    segment_size;          ///< Initial segment size (default: 2MB, MUST be 2MB aligned)
    Size    max_segment_size;      ///< Max single segment size (default: 16MB, MUST be 2MB aligned, 0=no limit)
    Size    segment_growth_size;   ///< Increment size per expansion (default: 2MB, MUST be 2MB aligned, 0=no growth)
    UInt32  initial_segments;      ///< Initial segment count (default: 1)
    UInt32  max_segments;          ///< Max segments allowed (0=unlimited)
    
    // ===== Chunk pool settings =====
    Size    max_chunk_size;       ///< Max chunk payload size (default: 64KB)
    UInt32  chunk_count;          ///< Total chunks in pool (default: 256)
    
    // ===== Publisher/Subscriber settings =====
    UInt32  max_publishers;       ///< Max concurrent publishers (default: 4)
    UInt32  max_subscribers;      ///< Max concurrent subscribers (default: 8)
    UInt32  subscriber_queue_capacity; ///< Max messages per subscriber queue (0=unlimited, default: 16)
    UInt32  publisher_max_loaned_samples;   ///< Max samples a publisher can loan simultaneously (default: 16)
    UInt32  subscriber_max_borrowed_samples; ///< Max samples a subscriber can hold simultaneously (default: 8)
    
    // ===== MessageQueue node pool settings =====
    Bool    use_shm_for_queue_nodes; ///< true=use mmap for queue nodes, false=use malloc (default: false)
    UInt32  queue_node_pool_capacity; ///< Total queue node capacity (0=auto-calculate, default: 0)
    
    // ===== Policies =====
    AllocationPolicy allocation_policy;     ///< Policy when pool exhausted (default: USE_OVERFLOW)
    QueueOverflowPolicy queue_overflow_policy; ///< Policy when subscriber queue full (default: DISCARD_OLDEST)
    Bool    enable_safe_overflow; ///< Fallback to malloc on pool exhausted (default: true)
};

/**
 * @brief Get default allocator configuration
 * @return Default configuration for typical use cases
 */
SharedMemoryAllocatorConfig GetDefaultSharedMemoryConfig() noexcept;

/**
 * @brief Allocator statistics (atomic counters)
 */
struct SharedMemoryAllocatorStats {
    UInt64  total_loans;          ///< Total loan() calls
    UInt64  total_sends;          ///< Total send() calls
    UInt64  total_receives;       ///< Total receive() calls
    UInt64  total_releases;       ///< Total release() calls
    UInt32  free_chunks;          ///< Current free chunks in pool
    UInt32  loaned_chunks;        ///< Chunks in LOANED state
    UInt32  sent_chunks;          ///< Chunks in SENT state (ready for receive)
    UInt32  in_use_chunks;        ///< Chunks in IN_USE state
    UInt64  loan_failures;        ///< loan() failures (pool empty)
    UInt64  receive_failures;     ///< receive() failures (no data available)
    UInt64  overflow_allocations; ///< Allocations from jemalloc/malloc
    Size    peak_memory_usage;    ///< Peak memory usage (bytes)
    UInt64  cas_retries;          ///< Total CAS retry count (contention metric)
    UInt64  enqueue_failures;     ///< Messages dropped due to malloc failure
};

/**
 * @class SharedMemoryAllocator
 * @brief Lock-free shared memory allocator (iceoryx2-style)
 * 
 * Lock-Free Design:
 * - Free list: Lock-free stack using atomic CAS on head pointer
 * - State transitions: Atomic CAS with memory ordering
 * - Reference counting: Atomic increment/decrement for subscribers
 * - No mutexes: All operations are wait-free or lock-free
 * 
 * Usage Pattern:
 * Publisher:
 *   1. loan(size, block) -> Atomically pop from free list
 *   2. Write data to block.ptr
 *   3. send(block) -> Atomically transition FREE->LOANED->SENT
 * 
 * Subscriber:
 *   1. receive(block) -> Atomically find SENT chunk, transition to IN_USE
 *   2. Read data from block.ptr
 *   3. release(block) -> Atomically decrement refcount, return to FREE if zero
 * 
 * @note Thread-safe, lock-free, wait-free for loan/send operations
 */
class SharedMemoryAllocator {
public:
    SharedMemoryAllocator() noexcept;
    ~SharedMemoryAllocator() noexcept;

    // Non-copyable, non-movable
    SharedMemoryAllocator(const SharedMemoryAllocator&) = delete;
    SharedMemoryAllocator& operator=(const SharedMemoryAllocator&) = delete;
    SharedMemoryAllocator(SharedMemoryAllocator&&) = delete;
    SharedMemoryAllocator& operator=(SharedMemoryAllocator&&) = delete;

    /**
     * @brief Initialize allocator with configuration
     * @param config Configuration parameters
     * @return true on success, false on failure
     */
    Bool initialize(const SharedMemoryAllocatorConfig& config) noexcept;

    /**
     * @brief Uninitialize and free all resources
     */
    void uninitialize() noexcept;

    /**
     * @brief Create a publisher (iceoryx2-style)
     * 
     * Creates an independent message queue for this publisher.
     * Each publisher has its own FIFO queue to minimize contention.
     * 
     * @param[out] handle Output publisher handle
     * @return Success or error
     */
    Result<void> createPublisher(PublisherHandle& handle) noexcept;

    /**
     * @brief Destroy a publisher
     * @param handle Publisher handle from createPublisher()
     * @return Success or error
     */
    Result<void> destroyPublisher(PublisherHandle& handle) noexcept;

    /**
     * @brief Create a subscriber (iceoryx2-style)
     * @param[out] handle Output subscriber handle
     * @return Success or error
     */
    Result<void> createSubscriber(SubscriberHandle& handle) noexcept;

    /**
     * @brief Destroy a subscriber
     * @param handle Subscriber handle from createSubscriber()
     * @return Success or error
     */
    Result<void> destroySubscriber(SubscriberHandle& handle) noexcept;

    // =========================================================================
    // RAII-style API (Recommended - iceoryx2-style C++ interface)
    // =========================================================================

    // ========================================================================
    // Core Pub/Sub APIs (Zero-overhead, direct access)
    // ========================================================================

    /**
     * @brief Publisher: Loan a memory block for writing
     * @param publisher Publisher handle
     * @param size Requested payload size
     * @param[out] block Output memory block (contains ptr, size, chunk_id)
     * @return Result (success or error code)
     * 
     * @warning Must call send() after writing data, or call releaseLoanedSample() to return unused block.
     *          Failing to do so will exhaust the memory pool.
     * 
     * @example
     * SharedMemoryMemoryBlock block;
     * if (allocator.loan(pub, 256, block).HasValue()) {
     *     memcpy(block.ptr, data, 256);
     *     allocator.send(pub, block);
     * }
     */
    Result<void> loan(const PublisherHandle& publisher, Size size, SharedMemoryMemoryBlock& block) noexcept;

    /**
     * @brief Publisher: Send a loaned sample to all subscribers
     * @param publisher Publisher handle
     * @param block Memory block obtained from loan()
     * @return Result (success or error code)
     * 
     * @note Broadcasts the sample to all active subscribers.
     *       Automatically manages reference counting for broadcast semantics.
     */
    Result<void> send(const PublisherHandle& publisher, SharedMemoryMemoryBlock& block) noexcept;

    /**
     * @brief Subscriber: Receive a sample from queue
     * @param subscriber Subscriber handle
     * @param[out] block Output memory block (read-only access recommended)
     * @return Result (kNoData if queue empty, kSuccess otherwise)
     * 
     * @warning Must call release() after processing data to prevent memory leaks.
     * 
     * @example
     * SharedMemoryMemoryBlock block;
     * if (allocator.receive(sub, block).HasValue()) {
     *     processData(block.ptr, block.size);
     *     allocator.release(sub, block);
     * }
     */
    Result<void> receive(const SubscriberHandle& subscriber, SharedMemoryMemoryBlock& block) noexcept;

    /**
     * @brief Subscriber: Release a received sample
     * @param subscriber Subscriber handle
     * @param block Memory block obtained from receive()
     * @return Result (success or error code)
     * 
     * @note Decrements reference counter. When all subscribers release,
     *       the chunk is returned to the free pool.
     */
    Result<void> release(const SubscriberHandle& subscriber, SharedMemoryMemoryBlock& block) noexcept;

    /**
     * @brief Convenience: Release a block (auto-detects loaned vs received)
     * @param block Memory block to release
     * @return Result (success or error code)
     * 
     * @note This method infers whether the block is:
     *       - LOANED (from publisher): Returns to pool without sending
     *       - RECEIVED (from subscriber): Decrements reference counter
     *       
     *       Use this for cleanup in error paths or tests.
     *       For production code, prefer explicit release(subscriber, block).
     */
    Result<void> release(SharedMemoryMemoryBlock& block) noexcept;

    /**
     * @brief Get statistics snapshot (lock-free read)
     * @param[out] stats Output statistics
     */
    void getStats(SharedMemoryAllocatorStats& stats) const noexcept;

    /**
     * @brief Reset statistics counters
     */
    void resetStats() noexcept;

    /**
     * @brief Check if allocator is initialized
     * @return true if initialized
     */
    Bool isInitialized() const noexcept { 
        return initialized_.load(std::memory_order_acquire); 
    }

    /**
     * @brief Shrink memory by releasing idle 2MB segments (dynamic shrink)
     * @details Scans segments from back to front, releases segments where all chunks are FREE.
     *          This implements mmap-based memory return to kernel via munmap.
     * @param keep_minimum Minimum segments to keep (default: initial_segments)
     * @return Number of segments released
     */
    UInt32 shrinkIdleSegments(UInt32 keep_minimum = 0) noexcept;

    // ========================================================================
    // Async/Blocking APIs (iceoryx2-style event notification)
    // ========================================================================

    /**
     * @brief Create a WaitSet for multiplexing multiple subscribers
     * 
     * WaitSet allows waiting on multiple subscribers simultaneously,
     * similar to epoll/select but for shared memory IPC.
     * 
     * Use case: Event-driven architecture with multiple data sources.
     * 
     * @return Unique pointer to WaitSet instance
     */
    std::unique_ptr<WaitSet> createWaitSet() noexcept;

    // ========================================================================
    // Async/Blocking Wait APIs
    // ========================================================================
    
    bool waitForData(const SubscriberHandle& subscriber, int64_t timeout_us = -1) noexcept;
    bool hasData(const SubscriberHandle& subscriber) const noexcept;
    
    // ========================================================================
    // Shared Memory IPC APIs
    // ========================================================================
    
    /**
     * @brief Get shared memory base address (for IPC)
     * @return Base address of shared memory region, or nullptr if not using SHM
     */
    void* getShmBaseAddress() const noexcept { return config_.shm_base_address; }
    
    /**
     * @brief Get shared memory name (for IPC)
     * @return SHM name, empty if anonymous memory
     */
    const std::string& getShmName() const noexcept { return config_.shm_name; }
    
    /**
     * @brief Check if this allocator is SHM creator
     * @return true if creator, false if attached to existing SHM
     */
    bool isShmCreator() const noexcept { return config_.is_creator; }

private:
    // ========================================================================
    // Internal Data Structures
    // ========================================================================
    
    friend class WaitSet;  // Allow WaitSet to access internal structures
    
    /**
     * @brief Push chunk to free list (lock-free Treiber stack)
     */
    inline void pushFreeChunk(ChunkHeader* chunk) noexcept {
        ChunkHeader* old_head = free_head_.load(std::memory_order_relaxed);
        do {
            chunk->next_free = old_head;
        } while (!free_head_.compare_exchange_weak(
            old_head, chunk,
            std::memory_order_release,
            std::memory_order_relaxed
        ));
        free_count_.fetch_add(1, std::memory_order_relaxed);
    }
    
    /**
     * @brief Pop chunk from free list (lock-free Treiber stack)
     */
    inline ChunkHeader* popFreeChunk() noexcept {
        ChunkHeader* old_head = free_head_.load(std::memory_order_acquire);
        ChunkHeader* new_head;
        do {
            if (old_head == nullptr) return nullptr;
            new_head = old_head->next_free;
        } while (!free_head_.compare_exchange_weak(
            old_head, new_head,
            std::memory_order_acquire,
            std::memory_order_acquire
        ));
        old_head->next_free = nullptr;
        free_count_.fetch_sub(1, std::memory_order_relaxed);
        return old_head;
    }

    // Lock-free message queue (iceoryx2-style FIFO per publisher)
    // Updated to use offset-based nodes from MessageQueueBlock
    struct alignas(CACHE_LINE_SIZE) MessageQueue {
        MessageQueueBlock* block;  ///< Ring buffer for message pointers (独立实例)
        void* block_memory;            ///< Memory for this queue's ringbuffer
        Size block_memory_size;        ///< Memory size

        MessageQueue() noexcept : block(nullptr), block_memory(nullptr), block_memory_size(0) {}
        
        /**
         * @brief Initialize queue with dedicated ringbuffer memory
         * @param memory Pre-allocated memory for this queue
         * @param memory_size Size of memory
         * @param capacity Queue capacity (number of elements)
         * @param use_shm Whether to use shared memory
         */
        bool initialize(void* memory, Size memory_size, UInt32 capacity, bool use_shm) noexcept;
        
        /**
         * @brief Cleanup queue resources
         */
        void cleanup() noexcept;

        // Enqueue message (lock-free using ringbuffer)
        bool enqueue(ChunkHeader* chunk) noexcept;

        // Dequeue message (lock-free using ringbuffer)
        ChunkHeader* dequeue() noexcept;

        // Peek count (from ringbuffer)
        uint32_t size() const noexcept { 
            return block ? block->size() : 0; 
        }
    };

    // Publisher state (iceoryx2-style broadcast model)
    struct alignas(CACHE_LINE_SIZE) PublisherState {
        UInt32           id;
        std::atomic<bool> active;
        std::atomic<UInt64> total_sent;  ///< Statistics
        
        // ✅ NEW: Dual-counter architecture (对标 iceoryx2)
        std::atomic<UInt32> loan_counter;      ///< Current number of loaned (unsent) samples
        UInt32              max_loaned_samples; ///< Quota limit (from config)
        
        PublisherState() noexcept 
            : id(0), active(false), total_sent(0), loan_counter(0), max_loaned_samples(16) {}
    };

    // Subscriber state (iceoryx2-style broadcast model)
    struct alignas(CACHE_LINE_SIZE) SubscriberState {
        UInt32           id;
        MessageQueue     rx_queue;  ///< Receive queue (all publishers broadcast here)
        std::atomic<bool> active;
        std::atomic<UInt64> total_received;  ///< Statistics
        std::atomic<UInt64> total_released;  ///< Statistics
        
        // ✅ NEW: Dual-counter architecture (对标 iceoryx2)
        std::atomic<UInt32> borrow_counter;        ///< Current number of borrowed (unreleased) samples (MUST be atomic!)
        UInt32              max_borrowed_samples;  ///< Quota limit (from config)
        MessageQueue        completion_queue;      ///< Delayed reclaim queue (iceoryx2-style, lock-free)
        
        // Async notification support (for waitForData + WaitSet)
        std::mutex wait_mutex;
        std::condition_variable data_available;
        
        // BLOCK_PUBLISHER support: notify when queue has space
        std::mutex queue_mutex;
        std::condition_variable queue_space_available;
        
        SubscriberState() noexcept 
            : id(0), active(false), total_received(0), total_released(0)
            , borrow_counter(0), max_borrowed_samples(8) {}
    };

    // ===== Memory Segment (iceoryx2-inspired 2MB blocks) =====
    struct MemorySegment {
        void*  base_address;      ///< Segment base address (2MB aligned)
        Size   segment_size;      ///< Segment size (typically 2MB)
        Size   used_bytes;        ///< Bytes allocated from this segment
        UInt32 chunk_start_index; ///< First chunk index in this segment
        UInt32 chunk_count;       ///< Number of chunks in this segment
        std::atomic<bool> active; ///< Is this segment active (atomic for shrink safety)
        std::atomic<bool> marked_for_release; ///< Marked for release in shrink
        
        MemorySegment() : base_address(nullptr), segment_size(0), used_bytes(0),
                         chunk_start_index(0), chunk_count(0), active(false), marked_for_release(false) {}
        
        // Move constructor
        MemorySegment(MemorySegment&& other) noexcept
            : base_address(other.base_address),
              segment_size(other.segment_size),
              used_bytes(other.used_bytes),
              chunk_start_index(other.chunk_start_index),
              chunk_count(other.chunk_count),
              active(other.active.load(std::memory_order_relaxed)),
              marked_for_release(other.marked_for_release.load(std::memory_order_relaxed)) {
            other.base_address = nullptr;
        }
        
        // Move assignment
        MemorySegment& operator=(MemorySegment&& other) noexcept {
            if (this != &other) {
                base_address = other.base_address;
                segment_size = other.segment_size;
                used_bytes = other.used_bytes;
                chunk_start_index = other.chunk_start_index;
                chunk_count = other.chunk_count;
                active.store(other.active.load(std::memory_order_relaxed), std::memory_order_relaxed);
                marked_for_release.store(other.marked_for_release.load(std::memory_order_relaxed), std::memory_order_relaxed);
                other.base_address = nullptr;
            }
            return *this;
        }
        
        // Delete copy constructor and copy assignment
        MemorySegment(const MemorySegment&) = delete;
        MemorySegment& operator=(const MemorySegment&) = delete;
    };
    
    // Internal state
    alignas(CACHE_LINE_SIZE) std::atomic<Bool> initialized_;
    alignas(CACHE_LINE_SIZE) SharedMemoryAllocatorConfig config_;
    
    // ===== Segment-based memory management (NEW) =====
    std::vector<std::unique_ptr<MemorySegment>> segments_;  ///< Allocated segments (pointer-based to avoid atomic copy issues)
    std::mutex segment_mutex_;             ///< Protects segment allocation (optimized with batch allocation)
    std::atomic<UInt32> total_segments_;   ///< Current segment count
    std::atomic<UInt32> total_chunks_;     ///< Total chunks across all segments
    std::atomic<Size> next_segment_size_;  ///< Next segment size to allocate (grows dynamically, 2MB aligned)
    Size total_pool_size_;  ///< Total allocated memory (across all segments)
    
    // ===== Chunk pool (iceoryx2-style with state machine) =====
    ChunkHeader*  chunk_headers_;     ///< Array of chunk headers (grows with segments)
    std::atomic<ChunkHeader*> free_head_;  ///< Free list head (lock-free Treiber stack)
    std::atomic<UInt32> free_count_;  ///< Free chunk count
    
    // ✅ NEW: Segment state for sample reference counting (iceoryx2-style)
    class CSegmentState* segment_state_;  ///< Sample reference counter manager
    
    // Publisher/Subscriber management (iceoryx2-style)
    PublisherState*  publishers_;       ///< Array of publisher states
    SubscriberState* subscribers_;      ///< Array of subscriber states
    std::atomic<UInt32> next_publisher_id_;  ///< ID allocator
    std::atomic<UInt32> next_subscriber_id_; ///< ID allocator
    std::atomic<UInt32> active_publishers_;  ///< Active count
    std::atomic<UInt32> active_subscribers_; ///< Active count
    
    // Atomic statistics (cache-aligned to avoid false sharing)
    alignas(CACHE_LINE_SIZE) std::atomic<UInt64> total_loans_;
    alignas(CACHE_LINE_SIZE) std::atomic<UInt64> total_sends_;
    alignas(CACHE_LINE_SIZE) std::atomic<UInt64> total_receives_;
    alignas(CACHE_LINE_SIZE) std::atomic<UInt64> total_releases_;
    alignas(CACHE_LINE_SIZE) std::atomic<UInt64> loan_failures_;
    alignas(CACHE_LINE_SIZE) std::atomic<UInt64> receive_failures_;
    alignas(CACHE_LINE_SIZE) std::atomic<UInt64> overflow_allocations_;
    alignas(CACHE_LINE_SIZE) std::atomic<Size>   peak_memory_usage_;
    alignas(CACHE_LINE_SIZE) std::atomic<UInt64> cas_retries_;
    alignas(CACHE_LINE_SIZE) std::atomic<UInt64> enqueue_failures_;  ///< Messages dropped due to malloc failure
    
    // Async wait support for WAIT_ASYNC policy
    std::mutex free_chunk_mutex_;                ///< Mutex for free chunk condition
    std::condition_variable free_chunk_available_; ///< Signaled when chunk freed
    
    // ===== Helper functions =====
    
    /**
     * @brief Allocate a new 2MB segment using mmap (iceoryx2-style dynamic growth)
     * @return true if successful, false if max segments reached or allocation failed
     */
    bool allocateNewSegment() noexcept;
    
    // Helper: Transition chunk state with CAS (returns true on success)
    bool transitionState(ChunkHeader* chunk, ChunkState expected, ChunkState desired) noexcept;
    
    // Helper: Count chunks in specific state (for stats)
    uint32_t countChunksInState(ChunkState state) const noexcept;
    
    // ✅ NEW: Dual-counter helper methods (iceoryx2-style)
    
    /**
     * @brief Calculate chunk offset from pool start
     * @param chunk Chunk header pointer
     * @return Distance in bytes from chunk_pool_ to chunk
     */
    UInt32 getDistanceToChunk(const ChunkHeader* chunk) const noexcept;
    
    /**
     * @brief Batch reclaim released samples from all subscribers (iceoryx2-style)
     * 
     * Iterates all subscriber completion_queues and:
     * 1. Dequeue released chunks
     * 2. Decrement sample_reference_counter
     * 3. If ref==1 (last reference), return to pool
     * 
     * This implements iceoryx2's delayed batch reclaim mechanism.
     * 
     * @param publisher Publisher triggering the reclaim (for optimization)
     */
    void retrieveReturnedSamples(PublisherState* publisher) noexcept;
    
    /**
     * @brief Release a single sample to pool (decrements sample_ref, checks last reference)
     * @param chunk Chunk to release
     * @return true if returned to pool (last reference), false otherwise
     */
    bool releaseSampleToPool(ChunkHeader* chunk) noexcept;
};


} // namespace lap::core

#endif // LAP_CORE_SHARED_MEMORY_ALLOCATOR_HPP
