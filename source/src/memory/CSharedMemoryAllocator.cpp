/**
 * @file CSharedMemoryAllocator.cpp
 * @brief Lock-free Zero-Copy Shared Memory Allocator Implementation (iceoryx2-style)
 * @author LightAP Team
 * @date 2025-12-26
 * 
 * Implementation Notes:
 * - All operations are lock-free using atomic CAS
 * - Free list is a Treiber stack (lock-free stack)
 * - Memory ordering optimized for performance:
 *   - relaxed: For statistics (no synchronization needed)
 *   - acquire/release: For state transitions (synchronize data)
 *   - seq_cst: For critical CAS operations (total ordering)
 * - Cache-line alignment prevents false sharing
 * - Compatible with jemalloc as system allocator
 */

#include "CSharedMemoryAllocator.hpp"
#include "CSharedMemoryWaitSet.hpp"
#include "memory/CSegmentState.hpp"  // ✅ NEW: Dual-counter architecture
#include "memory/CMessageQueueBlock.hpp"  // ✅ NEW: Ring buffer for message queues
#include "memory/CMemoryUtils.hpp"  // Unified memory allocation macros
#include <memory>  // For std::unique_ptr, std::make_unique
#include "CCoreErrorDomain.hpp"
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <thread>
#include <condition_variable>
#include <sys/mman.h>  // mmap for 2MB segment allocation
#include <unistd.h>    // sysconf for page size

// ============================================================================
// Memory Allocation Strategy
// ============================================================================
// 1. SHM Segments: mmap (anonymous, 2MB blocks) - Zero-copy, kernel-managed
// 2. Metadata (chunk_headers_, publishers_, etc.): jemalloc - Fast allocation
// 3. Overflow allocations (>max_chunk_size): jemalloc - Fallback path
//
// Jemalloc Integration:
// - When LAP_USE_JEMALLOC is defined and jemalloc is linked (-ljemalloc),
//   std::malloc/free automatically resolve to je_malloc/je_free via SYS_MALLOC/SYS_FREE
// - This provides arena-based allocation for metadata with low fragmentation
// - SHM segments use mmap directly to avoid jemalloc overhead
// ============================================================================
#if defined(LAP_USE_JEMALLOC)
    #include <jemalloc/jemalloc.h>
#endif

// Aligned allocation macro
#define SYS_ALIGNED_ALLOC(align, size) std::aligned_alloc(align, size)

// ============================================================================
// SHM segment allocation macros
// NOTE: These are used for segment allocation. For IPC support, use allocateNewSegmentAt()
// ============================================================================
#define SHM_SEGMENT_MMAP(size) ::mmap(nullptr, size, PROT_READ | PROT_WRITE, \
                                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
#define SHM_SEGMENT_MUNMAP(ptr, size) ::munmap(ptr, size)

namespace lap::core {

// ============================================================================
// FreeList Implementation (Treiber Stack - iceoryx2 style)
// ============================================================================
// ============================================================================
// MessageQueue Implementation (using independent MessageQueueBlock ringbuffer)
// ============================================================================

bool SharedMemoryAllocator::MessageQueue::initialize(void* memory, Size memory_size, 
                                                      UInt32 capacity, bool /*use_shm*/) noexcept {
    if (!memory || memory_size == 0 || capacity == 0) {
        return false;
    }
    
    block_memory = memory;
    block_memory_size = memory_size;
    
    // Create MessageQueueBlock with dedicated memory
    const Size block_size = sizeof(void*);  // Each slot stores ChunkHeader*
    block = new MessageQueueBlock(memory, memory_size, block_size);
    
    if (!block || block->getCapacity() == 0) {
        INNER_CORE_LOG("[ERROR] MessageQueue::initialize: Failed to create MessageQueueBlock\n");
        block = nullptr;
        return false;
    }
    
    return true;
}

void SharedMemoryAllocator::MessageQueue::cleanup() noexcept {
    if (block) {
        delete block;
        block = nullptr;
    }
    // Note: block_memory is freed by caller
    block_memory = nullptr;
    block_memory_size = 0;
}

bool SharedMemoryAllocator::MessageQueue::enqueue(ChunkHeader* chunk) noexcept {
    if (!block) {
        INNER_CORE_LOG("[ERROR] MessageQueue::enqueue: block not initialized\n");
        return false;
    }
    
    // Directly enqueue pointer to ringbuffer
    return block->enqueue(static_cast<void*>(chunk));
}

ChunkHeader* SharedMemoryAllocator::MessageQueue::dequeue() noexcept {
    if (!block) {
        INNER_CORE_LOG("[ERROR] MessageQueue::dequeue: block not initialized\n");
        return nullptr;
    }
    
    void* ptr = nullptr;
    if (block->dequeue(ptr)) {
        return static_cast<ChunkHeader*>(ptr);
    }
    
    return nullptr;  // Queue empty
}

// ============================================================================
// Default Configuration
// ============================================================================

SharedMemoryAllocatorConfig GetDefaultSharedMemoryConfig() noexcept {
    SharedMemoryAllocatorConfig config;
    
    // SHM IPC settings (anonymous by default)
    config.shm_name = "";              // Empty = anonymous memory
    config.shm_base_address = nullptr; // Let OS choose address
    config.is_creator = true;          // Default is creator mode
    
    // Segment settings (2MB aligned for huge pages)
    config.segment_size = 2 * 1024 * 1024;        // 2MB initial segment
    config.max_segment_size = 16 * 1024 * 1024;   // 16MB max per segment
    config.segment_growth_size = 2 * 1024 * 1024; // Grow by 2MB
    config.initial_segments = 1;                   // Start with 1 segment
    config.max_segments = 0;                       // Unlimited segments
    
    // Chunk pool settings
    config.max_chunk_size = 64 * 1024;  // 64KB max chunk
    config.chunk_count = 256;            // 256 chunks initially
    
    // Publisher/Subscriber settings
    config.max_publishers = 4;
    config.max_subscribers = 8;
    config.subscriber_queue_capacity = 16;         // 16 messages per subscriber
    config.publisher_max_loaned_samples = 16;      // Publisher can loan 16 samples max
    config.subscriber_max_borrowed_samples = 8;    // Subscriber can hold 8 samples max
    
    // Queue node pool settings
    config.use_shm_for_queue_nodes = false;  // Use malloc for queue nodes (faster for single-process)
    config.queue_node_pool_capacity = 0;     // Auto-calculate based on subscribers
    
    // Policies
    config.allocation_policy = AllocationPolicy::USE_OVERFLOW;
    config.queue_overflow_policy = QueueOverflowPolicy::DISCARD_OLDEST;
    config.enable_safe_overflow = true;  // Allow fallback to malloc
    
    return config;
}

// ============================================================================
// Constructor/Destructor
// ============================================================================

SharedMemoryAllocator::SharedMemoryAllocator() noexcept
    : initialized_(false)
    , chunk_headers_(nullptr)
    , free_head_(nullptr)
    , free_count_(0)
    , segment_state_(nullptr)
    , publishers_(nullptr)
    , subscribers_(nullptr)
    , next_publisher_id_(1)
    , next_subscriber_id_(1)
    , active_publishers_(0)
    , active_subscribers_(0)
    , total_loans_(0)
    , total_sends_(0)
    , total_receives_(0)
    , total_releases_(0)
    , loan_failures_(0)
    , receive_failures_(0)
    , overflow_allocations_(0)
    , peak_memory_usage_(0)
    , cas_retries_(0)
    , enqueue_failures_(0)
{
    // Set default policies (iceoryx2-style)
    config_.allocation_policy = AllocationPolicy::WAIT_SYNC;  // Spin-wait for high performance
    config_.queue_overflow_policy = QueueOverflowPolicy::DISCARD_NEWEST;
    config_.subscriber_queue_capacity = 0;  // unlimited
}

SharedMemoryAllocator::~SharedMemoryAllocator() noexcept {
    uninitialize();
    
    // Cleanup publishers/subscribers
    if (publishers_) {
        delete[] publishers_;
        publishers_ = nullptr;
    }
    if (subscribers_) {
        delete[] subscribers_;
        subscribers_ = nullptr;
    }
}

// ============================================================================
// Initialization (iceoryx2-inspired memory layout with 2MB segments)
// ============================================================================

Bool SharedMemoryAllocator::initialize(const SharedMemoryAllocatorConfig& config) noexcept {
    // Check if already initialized
    Bool expected = false;
    if (!initialized_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return true;  // Already initialized
    }

    config_ = config;
    
    // ===== Validate 2MB alignment for all segment size parameters =====
    constexpr Size MB_2 = 2 * 1024 * 1024;  // 2MB alignment requirement
    if (config.segment_size == 0 || config.segment_size % MB_2 != 0) {
        INNER_CORE_LOG("[ERROR] segment_size must be 2MB aligned (got %zu)\n", config.segment_size);
        initialized_.store(false, std::memory_order_release);
        return false;
    }
    if (config.max_segment_size > 0 && config.max_segment_size % MB_2 != 0) {
        INNER_CORE_LOG("[ERROR] max_segment_size must be 2MB aligned (got %zu)\n", config.max_segment_size);
        initialized_.store(false, std::memory_order_release);
        return false;
    }
    if (config.segment_growth_size % MB_2 != 0) {
        INNER_CORE_LOG("[ERROR] segment_growth_size must be 2MB aligned (got %zu)\n", config.segment_growth_size);
        initialized_.store(false, std::memory_order_release);
        return false;
    }
    if (config.max_segment_size > 0 && config.segment_size > config.max_segment_size) {
        INNER_CORE_LOG("[ERROR] segment_size cannot exceed max_segment_size\n");
        initialized_.store(false, std::memory_order_release);
        return false;
    }
    
    // ===== Segment-based allocation mode (iceoryx2-inspired) =====
    const Size segment_size = config_.segment_size;
    const UInt32 initial_segments = config_.initial_segments;
    
    INNER_CORE_LOG("[INFO] SharedMemoryAllocator: Initializing segment-based pool\n");
    INNER_CORE_LOG("       Segment size: %.2f MB\n", segment_size / (1024.0 * 1024.0));
    INNER_CORE_LOG("       Initial segments: %u (total: %.2f MB)\n", 
                   initial_segments, (initial_segments * segment_size) / (1024.0 * 1024.0));
    INNER_CORE_LOG("       Dynamic growth: segment_growth=%zuMB, max_size=%zuMB, batch=%u\n",
                   config_.segment_growth_size / (1024*1024),
                   config_.max_segment_size / (1024*1024),
                   1);
    INNER_CORE_LOG("       Dynamic growth: %s (max segments: %u)\n",
                   (config_.segment_growth_size > 0) ? "ENABLED" : "DISABLED",
                   config_.max_segments == 0 ? 9999 : config_.max_segments);

    // Calculate chunk layout
    const Size chunk_header_size = sizeof(ChunkHeader);
    const Size payload_size = config_.max_chunk_size;
    const Size chunk_total_size = chunk_header_size + payload_size;
    
    // Calculate how many chunks fit in one segment
    const UInt32 chunks_per_segment = static_cast<UInt32>(segment_size / chunk_total_size);
    const UInt32 initial_chunk_count = chunks_per_segment * initial_segments;
    
    INNER_CORE_LOG("       Chunk size: %zu bytes (header=%zu, payload=%zu)\n",
                   chunk_total_size, chunk_header_size, payload_size);
    INNER_CORE_LOG("       Chunks per segment: %u\n", chunks_per_segment);
    INNER_CORE_LOG("       Initial chunks: %u\n", initial_chunk_count);

    // Allocate chunk header array (calculate max based on largest possible segment)
    // Use max_segment_size (or fallback to reasonable upper bound) to calculate max chunks
    Size largest_segment = config_.max_segment_size > 0 ? config_.max_segment_size : (segment_size * 8); // 8x growth if unlimited
    UInt32 max_chunks_per_segment = static_cast<UInt32>(largest_segment / chunk_total_size);
    const UInt32 max_possible_chunks = max_chunks_per_segment * 
        (config_.max_segments == 0 ? 64 : config_.max_segments); // Reasonable upper bound
    
    INNER_CORE_LOG("       Max capacity: %u chunks (largest_segment=%.2fMB, max_chunks_per_seg=%u)\n",
                   max_possible_chunks, largest_segment / (1024.0*1024.0), max_chunks_per_segment);
    
    chunk_headers_ = static_cast<ChunkHeader*>(
        SYS_ALIGNED_ALLOC(CACHE_LINE_SIZE, max_possible_chunks * sizeof(ChunkHeader))
    );
    if (!chunk_headers_) {
        INNER_CORE_LOG("[ERROR] Failed to allocate chunk headers\n");
        initialized_.store(false, std::memory_order_release);
        return false;
    }
    
    // Initialize free list
    free_head_.store(nullptr, std::memory_order_relaxed);
    free_count_.store(0, std::memory_order_relaxed);
    
    // Initialize segment tracking
    total_segments_.store(0, std::memory_order_relaxed);
    total_chunks_.store(0, std::memory_order_relaxed);
    next_segment_size_.store(segment_size, std::memory_order_relaxed);
    total_pool_size_ = 0;
    
    INNER_CORE_LOG("       Dynamic growth: segment_size=%zuMB, growth=%zuMB, max=%zuMB\n",
                   segment_size / (1024*1024),
                   config_.segment_growth_size / (1024*1024),
                   config_.max_segment_size / (1024*1024));
    
    // Allocate publisher/subscriber arrays (max 64 of each - iceoryx2 style)
    constexpr UInt32 MAX_PUBLISHERS = 64;
    constexpr UInt32 MAX_SUBSCRIBERS = 64;
    
    publishers_ = new PublisherState[MAX_PUBLISHERS];
    subscribers_ = new SubscriberState[MAX_SUBSCRIBERS];
    if (!publishers_ || !subscribers_) {
        INNER_CORE_LOG("[ERROR] Failed to allocate publisher/subscriber arrays\n");
        SYS_FREE(chunk_headers_);
        if (publishers_) delete[] publishers_;
        if (subscribers_) delete[] subscribers_;
        chunk_headers_ = nullptr;
        initialized_.store(false, std::memory_order_release);
        return false;
    }
    
    // ✅ Initialize SegmentState with MAX capacity (chunk_headers_ size)
    // Critical: chunk indices range from 0 to max_possible_chunks-1, so segment_counters must have that capacity
    segment_state_ = new CSegmentState(max_possible_chunks);  // Allocate for max possible chunks
    if (!segment_state_) {
        INNER_CORE_LOG("[ERROR] Failed to allocate SegmentState\n");
        SYS_FREE(chunk_headers_);
        delete[] publishers_;
        delete[] subscribers_;
        chunk_headers_ = nullptr;
        initialized_.store(false, std::memory_order_release);
        return false;
    }
    segment_state_->setPayloadSize(config_.max_chunk_size);
    INNER_CORE_LOG("[INFO] SegmentState initialized (dual-counter mode, max capacity=%u chunks)\n", max_possible_chunks);
    
    // Note: Queue memory is allocated per-subscriber in createSubscriber()
    // This avoids shared ringbuffer issues and provides isolation
    
    // Allocate initial segments (all use same segment_size, no growth yet)
    for (UInt32 seg = 0; seg < initial_segments; ++seg) {
        if (!allocateNewSegment()) {
            INNER_CORE_LOG("[ERROR] Failed to allocate initial segment %u\n", seg);
            // Cleanup allocated segments
            for (auto& segment : segments_) {
                if (segment->base_address) {
                    SYS_FREE(segment->base_address);
                }
            }
            SYS_FREE(chunk_headers_);
            delete[] publishers_;
            delete[] subscribers_;
            delete segment_state_;
            chunk_headers_ = nullptr;
            initialized_.store(false, std::memory_order_release);
            return false;
        }
    }
    
    // ===== NOW enable dynamic growth: set next_segment_size to include growth_size =====
    // Initial segments are all config_.segment_size. Future segments will grow.
    Size first_dynamic_size = segment_size + config_.segment_growth_size;
    if (config_.max_segment_size > 0 && first_dynamic_size > config_.max_segment_size) {
        first_dynamic_size = config_.max_segment_size;
    }
    next_segment_size_.store(first_dynamic_size, std::memory_order_relaxed);
    
    // ✅ Initialize Publisher/Subscriber quota limits
    for (UInt32 i = 0; i < MAX_PUBLISHERS; ++i) {
        publishers_[i].max_loaned_samples = config_.publisher_max_loaned_samples;
    }
    for (UInt32 i = 0; i < MAX_SUBSCRIBERS; ++i) {
        subscribers_[i].max_borrowed_samples = config_.subscriber_max_borrowed_samples;
    }
    INNER_CORE_LOG("[INFO] Publisher max_loaned_samples=%u, Subscriber max_borrowed_samples=%u\n",
                   config_.publisher_max_loaned_samples, config_.subscriber_max_borrowed_samples);

    INNER_CORE_LOG("[INFO] SharedMemoryAllocator: Initialization complete (segment-based mode)\n");
    INNER_CORE_LOG("       Total segments: %u, Total chunks: %u, Total memory: %.2f MB\n",
                   total_segments_.load(std::memory_order_relaxed),
                   total_chunks_.load(std::memory_order_relaxed),
                   total_pool_size_ / (1024.0 * 1024.0));
#if defined(LAP_USE_JEMALLOC)
    INNER_CORE_LOG("       Using jemalloc for overflow allocations\n");
#else
    INNER_CORE_LOG("       Using std::malloc for overflow allocations\n");
#endif

    return true;
}

// ============================================================================
// Segment allocation (iceoryx2-inspired dynamic block management with 2MB alignment)
// ============================================================================

bool SharedMemoryAllocator::allocateNewSegment() noexcept {
    std::lock_guard<std::mutex> lock(segment_mutex_);
    
    // Check max segments limit
    UInt32 current_segments = total_segments_.load(std::memory_order_relaxed);
    if (config_.max_segments > 0 && current_segments >= config_.max_segments) {
        INNER_CORE_LOG("[WARN] Reached max segments limit: %u\n", config_.max_segments);
        return false;
    }
    
    // ===== Dynamic growth: use next_segment_size (grows over time) =====
    Size segment_size = next_segment_size_.load(std::memory_order_relaxed);
    
    // Apply max_segment_size limit if configured
    if (config_.max_segment_size > 0 && segment_size > config_.max_segment_size) {
        segment_size = config_.max_segment_size;
    }
    
    // Calculate chunk layout
    const Size chunk_header_size = sizeof(ChunkHeader);
    const Size payload_size = config_.max_chunk_size;
    const Size chunk_total_size = chunk_header_size + payload_size;
    const UInt32 chunks_per_segment = static_cast<UInt32>(segment_size / chunk_total_size);
    
    // ✅ Allocate segment using mmap
    // For IPC: If shm_base_address is set, use MAP_FIXED to map at specific address
    // For anonymous: Use MAP_ANONYMOUS with nullptr address
    void* segment_memory = nullptr;
    
    if (!config_.shm_name.empty() && config_.shm_base_address != nullptr) {
        // IPC mode: Named shared memory at fixed address
        // Calculate offset for this segment
        Size offset = current_segments * segment_size;
        void* target_addr = static_cast<char*>(config_.shm_base_address) + offset;
        
        // Map at fixed address for cross-process consistency
        segment_memory = ::mmap(target_addr, segment_size,
                               PROT_READ | PROT_WRITE,
                               MAP_SHARED | MAP_FIXED | MAP_ANONYMOUS,
                               -1, 0);
        
        if (segment_memory == MAP_FAILED || segment_memory != target_addr) {
            INNER_CORE_LOG("[ERROR] Failed to mmap segment at fixed address %p (got %p, errno=%d)\n",
                          target_addr, segment_memory, errno);
            return false;
        }
        
        INNER_CORE_LOG("[INFO] Mapped segment at fixed address %p for IPC (%s)\n",
                      segment_memory, config_.shm_name.c_str());
    } else {
        // Anonymous mode: Let OS choose address
        segment_memory = SHM_SEGMENT_MMAP(segment_size);
        
        if (segment_memory == MAP_FAILED || !segment_memory) {
            INNER_CORE_LOG("[ERROR] Failed to mmap segment (errno=%d)\n", errno);
            return false;
        }
    }
    
    // Create segment descriptor
    MemorySegment segment;
    segment.base_address = segment_memory;
    segment.segment_size = segment_size;
    segment.used_bytes = chunks_per_segment * chunk_total_size;
    segment.chunk_start_index = total_chunks_.load(std::memory_order_relaxed);
    segment.chunk_count = chunks_per_segment;
    segment.active.store(true, std::memory_order_release);
    segment.marked_for_release.store(false, std::memory_order_release);
    
    // Initialize chunks from this segment
    const UInt32 chunk_start_idx = segment.chunk_start_index;
    for (UInt32 i = 0; i < chunks_per_segment; ++i) {
        const UInt32 chunk_idx = chunk_start_idx + i;
        ChunkHeader* chunk = &chunk_headers_[chunk_idx];
        
        // Placement new for atomic members
        new (chunk) ChunkHeader();
        
        // Calculate payload pointer (offset into this segment)
        chunk->user_payload = static_cast<char*>(segment_memory) + (i * chunk_total_size) + chunk_header_size;
        chunk->chunk_id = chunk_idx + 1;  // 1-based chunk IDs
        chunk->payload_size = 0;  // Will be set on loan
        chunk->state.store(ChunkState::FREE, std::memory_order_relaxed);
        chunk->sequence.store(0, std::memory_order_relaxed);
        
        // Push to free list
        pushFreeChunk(chunk);
    }
    
    // Update tracking - create unique_ptr to avoid atomic copy issues
    segments_.emplace_back(std::make_unique<MemorySegment>(std::move(segment)));
    total_segments_.fetch_add(1, std::memory_order_relaxed);
    total_chunks_.fetch_add(chunks_per_segment, std::memory_order_relaxed);
    total_pool_size_ += segment_size;
    
    // No need to expand SegmentState - it was pre-allocated with max capacity
    
    // ===== Dynamic growth: increase next_segment_size for next allocation =====
    // Only grow if we've allocated more than initial_segments (dynamic expansion phase)
    Size new_next_size = segment_size;  // Default: keep same size
    if (current_segments >= config_.initial_segments) {
        new_next_size = segment_size + config_.segment_growth_size;
        if (config_.max_segment_size > 0 && new_next_size > config_.max_segment_size) {
            new_next_size = config_.max_segment_size;  // Cap at max
        }
        next_segment_size_.store(new_next_size, std::memory_order_relaxed);
    }
    
    INNER_CORE_LOG("[INFO] Allocated new segment: #%u, size=%.2f MB, chunks=%u (total chunks=%u, next_size=%.2fMB)\n",
                   current_segments + 1,
                   segment_size / (1024.0 * 1024.0),
                   chunks_per_segment,
                   total_chunks_.load(std::memory_order_relaxed),
                   new_next_size / (1024.0 * 1024.0));
    
    return true;
}

// ============================================================================
// Dynamic Shrink: Release idle 2MB segments back to kernel
// ============================================================================

UInt32 SharedMemoryAllocator::shrinkIdleSegments(UInt32 keep_minimum) noexcept {
    if (!initialized_.load(std::memory_order_acquire)) {
        return 0;
    }
    
    std::lock_guard<std::mutex> lock(segment_mutex_);
    
    // Default: keep initial_segments as minimum
    if (keep_minimum == 0) {
        keep_minimum = config_.initial_segments;
    }
    
    UInt32 current_segments = static_cast<UInt32>(segments_.size());
    if (current_segments <= keep_minimum) {
        return 0;  // Already at minimum
    }
    
    UInt32 released_count = 0;
    
    // Scan segments from back to front (LIFO - release newest first)
    for (int i = static_cast<int>(segments_.size()) - 1; i >= 0 && segments_.size() > keep_minimum; --i) {
        auto& segment = segments_[i];  // segment is unique_ptr<MemorySegment>
        if (!segment->active.load(std::memory_order_acquire) || !segment->base_address) {
            continue;
        }
        
        // Step 1: Check if all chunks in this segment are FREE
        bool all_free = true;
        UInt32 start_idx = segment->chunk_start_index;
        UInt32 end_idx = start_idx + segment->chunk_count;
        
        for (UInt32 chunk_idx = start_idx; chunk_idx < end_idx; ++chunk_idx) {
            ChunkState state = chunk_headers_[chunk_idx].state.load(std::memory_order_acquire);
            if (state != ChunkState::FREE) {
                all_free = false;
                break;
            }
        }
        
        if (!all_free) {
            continue;  // Segment has active chunks, skip
        }
        
        // Step 2: Mark segment for release (prevents new loans)
        segment->marked_for_release.store(true, std::memory_order_release);
        
        // Step 3: Atomically transition all chunks FREE -> LOANED with double-check
        // This prevents TOCTOU: if any chunk was loaned between check and transition, abort
        bool transition_failed = false;
        for (UInt32 chunk_idx = start_idx; chunk_idx < end_idx; ++chunk_idx) {
            ChunkHeader* chunk = &chunk_headers_[chunk_idx];
            ChunkState expected = ChunkState::FREE;
            if (!chunk->state.compare_exchange_strong(
                    expected, ChunkState::LOANED,
                    std::memory_order_acq_rel, std::memory_order_acquire)) {
                // Chunk was modified (loaned by another thread), abort this segment
                transition_failed = true;
                break;
            }
        }
        
        if (transition_failed) {
            // Rollback: restore chunks to FREE
            for (UInt32 chunk_idx = start_idx; chunk_idx < end_idx; ++chunk_idx) {
                ChunkHeader* chunk = &chunk_headers_[chunk_idx];
                ChunkState expected = ChunkState::LOANED;
                chunk->state.compare_exchange_strong(
                    expected, ChunkState::FREE,
                    std::memory_order_release, std::memory_order_relaxed
                );
            }
            segment->marked_for_release.store(false, std::memory_order_release);
            continue;
        }
        
        // Step 4: Safe to release - all chunks locked, no concurrent access possible
        INNER_CORE_LOG("[INFO] Shrinking: releasing segment #%d, size=%.2f MB, chunks=%u\n",
                       i + 1, segment->segment_size / (1024.0 * 1024.0), segment->chunk_count);
        
        SHM_SEGMENT_MUNMAP(segment->base_address, segment->segment_size);
        segment->base_address = nullptr;
        segment->active.store(false, std::memory_order_release);
        
        // Mark all chunks in this segment as INVALID (prevent future use)
        for (UInt32 chunk_idx = start_idx; chunk_idx < end_idx; ++chunk_idx) {
            ChunkHeader* chunk = &chunk_headers_[chunk_idx];
            chunk->state.store(ChunkState::INVALID, std::memory_order_release);
            chunk->user_payload = nullptr;  // Clear dangling pointer
        }
        
        // Update counters
        total_segments_.fetch_sub(1, std::memory_order_relaxed);
        total_chunks_.fetch_sub(segment->chunk_count, std::memory_order_relaxed);
        total_pool_size_ -= segment->segment_size;
        
        // NOTE: Do NOT erase from segments_ vector to preserve chunk_start_index mapping
        // The segment object remains but is marked inactive with base_address = nullptr
        
        released_count++;
    }
    
    if (released_count > 0) {
        INNER_CORE_LOG("[INFO] Shrink complete: released %u segments (%.2f MB returned to kernel)\n",
                       released_count, (released_count * config_.segment_size) / (1024.0 * 1024.0));
    }
    
    return released_count;
}

void SharedMemoryAllocator::uninitialize() noexcept {
    Bool expected = true;
    if (!initialized_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        return;  // Not initialized
    }

    INNER_CORE_LOG("[INFO] SharedMemoryAllocator: Uninitializing\n");

    // Cleanup subscriber queues (each has independent memory)
    for (UInt32 i = 0; i < 64; ++i) {  // MAX_SUBSCRIBERS = 64
        SubscriberState* sub = &subscribers_[i];
        if (sub->active.load(std::memory_order_relaxed)) {
            sub->rx_queue.cleanup();
            sub->completion_queue.cleanup();
            
            // Free queue memory
            if (sub->rx_queue.block_memory) {
                if (config_.use_shm_for_queue_nodes) {
                    SHM_SEGMENT_MUNMAP(sub->rx_queue.block_memory, sub->rx_queue.block_memory_size);
                } else {
                    SYS_FREE(sub->rx_queue.block_memory);
                }
            }
            if (sub->completion_queue.block_memory) {
                if (config_.use_shm_for_queue_nodes) {
                    SHM_SEGMENT_MUNMAP(sub->completion_queue.block_memory, sub->completion_queue.block_memory_size);
                } else {
                    SYS_FREE(sub->completion_queue.block_memory);
                }
            }
        }
    }

    // ✅ NEW: Free SegmentState
    if (segment_state_) {
        delete segment_state_;
        segment_state_ = nullptr;
    }

    // ✅ Free all allocated segments (2MB blocks) using munmap
    for (auto& segment : segments_) {
        if (segment->base_address) {
            SHM_SEGMENT_MUNMAP(segment->base_address, segment->segment_size);
            segment->base_address = nullptr;
            segment->active.store(false, std::memory_order_release);
        }
    }
    segments_.clear();
    
    // Free chunk headers
    if (chunk_headers_) {
        // Call destructors for atomic members (for all chunks including from expanded segments)
        UInt32 total_chunks = total_chunks_.load(std::memory_order_relaxed);
        for (UInt32 i = 0; i < total_chunks; ++i) {
            chunk_headers_[i].~ChunkHeader();
        }
        SYS_FREE(chunk_headers_);
        chunk_headers_ = nullptr;
        
        // ✅ CRITICAL: Reset free_list to prevent dangling pointers
        free_head_.store(nullptr, std::memory_order_release);
    }

    // ✅ Free publisher/subscriber arrays
    if (publishers_) {
        delete[] publishers_;
        publishers_ = nullptr;
    }
    if (subscribers_) {
        delete[] subscribers_;
        subscribers_ = nullptr;
    }

    total_pool_size_ = 0;
    total_segments_.store(0, std::memory_order_relaxed);
    total_chunks_.store(0, std::memory_order_relaxed);
    next_segment_size_.store(0, std::memory_order_relaxed);  // Reset dynamic growth state
}

// ============================================================================
// Publisher/Subscriber Management (iceoryx2-style)
// ============================================================================

Result<void> SharedMemoryAllocator::createPublisher(PublisherHandle& handle) noexcept {
    if (!initialized_.load(std::memory_order_acquire)) {
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kNotInitialized));
    }
    
    // Allocate publisher ID (atomic increment)
    UInt32 pub_id = next_publisher_id_.fetch_add(1, std::memory_order_relaxed);
    
    if (pub_id >= 64) {  // Max 64 publishers
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kResourceExhausted));
    }
    
    PublisherState* pub = &publishers_[pub_id - 1];
    pub->id = pub_id;
    pub->active.store(true, std::memory_order_release);
    
    active_publishers_.fetch_add(1, std::memory_order_relaxed);
    
    handle.publisher_id = pub_id;
    // iceoryx2-style: No internal pointer, use ID to resolve
    
    INNER_CORE_LOG("[INFO] Created publisher ID=%u\n", pub_id);
    return Result<void>::FromValue();
}

Result<void> SharedMemoryAllocator::destroyPublisher(PublisherHandle& handle) noexcept {
    if (handle.publisher_id == 0 || handle.publisher_id > 64) {
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    }
    
    PublisherState* pub = &publishers_[handle.publisher_id - 1];
    
    if (!pub->active.load(std::memory_order_acquire)) {
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    }
    
    // TODO: Drain message queue and return chunks to free list
    
    pub->active.store(false, std::memory_order_release);
    active_publishers_.fetch_sub(1, std::memory_order_relaxed);
    
    handle.publisher_id = 0;
    // iceoryx2-style: No internal pointer to clear
    
    return Result<void>::FromValue();
}

Result<void> SharedMemoryAllocator::createSubscriber(SubscriberHandle& handle) noexcept {
    if (!initialized_.load(std::memory_order_acquire)) {
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kNotInitialized));
    }
    
    // Allocate subscriber ID (atomic increment)
    UInt32 sub_id = next_subscriber_id_.fetch_add(1, std::memory_order_relaxed);
    
    if (sub_id >= 64) {  // Max 64 subscribers
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kResourceExhausted));
    }
    
    SubscriberState* sub = &subscribers_[sub_id - 1];
    sub->id = sub_id;
    sub->active.store(true, std::memory_order_release);
    
    // Allocate independent memory for rx_queue and completion_queue
    UInt32 queue_capacity = config_.subscriber_queue_capacity > 0 ? config_.subscriber_queue_capacity : 256;
    Size queue_memory_size = sizeof(void*) * queue_capacity;
    
    // Allocate rx_queue memory
    void* rx_memory = nullptr;
    if (config_.use_shm_for_queue_nodes) {
        rx_memory = SHM_SEGMENT_MMAP(queue_memory_size);
        if (rx_memory == MAP_FAILED || !rx_memory) {
            INNER_CORE_LOG("[ERROR] Failed to allocate rx_queue memory for subscriber %u\n", sub_id);
            sub->active.store(false, std::memory_order_release);
            return Result<void>::FromError(MakeErrorCode(CoreErrc::kOutOfMemory));
        }
    } else {
        rx_memory = SYS_ALIGNED_ALLOC(CACHE_LINE_SIZE, queue_memory_size);
        if (!rx_memory) {
            INNER_CORE_LOG("[ERROR] Failed to allocate rx_queue memory for subscriber %u\n", sub_id);
            sub->active.store(false, std::memory_order_release);
            return Result<void>::FromError(MakeErrorCode(CoreErrc::kOutOfMemory));
        }
    }
    
    // Allocate completion_queue memory
    void* completion_memory = nullptr;
    if (config_.use_shm_for_queue_nodes) {
        completion_memory = SHM_SEGMENT_MMAP(queue_memory_size);
        if (completion_memory == MAP_FAILED || !completion_memory) {
            INNER_CORE_LOG("[ERROR] Failed to allocate completion_queue memory for subscriber %u\n", sub_id);
            if (config_.use_shm_for_queue_nodes) {
                SHM_SEGMENT_MUNMAP(rx_memory, queue_memory_size);
            } else {
                SYS_FREE(rx_memory);
            }
            sub->active.store(false, std::memory_order_release);
            return Result<void>::FromError(MakeErrorCode(CoreErrc::kOutOfMemory));
        }
    } else {
        completion_memory = SYS_ALIGNED_ALLOC(CACHE_LINE_SIZE, queue_memory_size);
        if (!completion_memory) {
            INNER_CORE_LOG("[ERROR] Failed to allocate completion_queue memory for subscriber %u\n", sub_id);
            SYS_FREE(rx_memory);
            sub->active.store(false, std::memory_order_release);
            return Result<void>::FromError(MakeErrorCode(CoreErrc::kOutOfMemory));
        }
    }
    
    // Initialize queues with independent memory
    if (!sub->rx_queue.initialize(rx_memory, queue_memory_size, queue_capacity, config_.use_shm_for_queue_nodes)) {
        INNER_CORE_LOG("[ERROR] Failed to initialize rx_queue for subscriber %u\n", sub_id);
        if (config_.use_shm_for_queue_nodes) {
            SHM_SEGMENT_MUNMAP(rx_memory, queue_memory_size);
            SHM_SEGMENT_MUNMAP(completion_memory, queue_memory_size);
        } else {
            SYS_FREE(rx_memory);
            SYS_FREE(completion_memory);
        }
        sub->active.store(false, std::memory_order_release);
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kInternalError));
    }
    
    if (!sub->completion_queue.initialize(completion_memory, queue_memory_size, queue_capacity, config_.use_shm_for_queue_nodes)) {
        INNER_CORE_LOG("[ERROR] Failed to initialize completion_queue for subscriber %u\n", sub_id);
        sub->rx_queue.cleanup();
        if (config_.use_shm_for_queue_nodes) {
            SHM_SEGMENT_MUNMAP(rx_memory, queue_memory_size);
            SHM_SEGMENT_MUNMAP(completion_memory, queue_memory_size);
        } else {
            SYS_FREE(rx_memory);
            SYS_FREE(completion_memory);
        }
        sub->active.store(false, std::memory_order_release);
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kInternalError));
    }
    
    active_subscribers_.fetch_add(1, std::memory_order_relaxed);
    
    handle.subscriber_id = sub_id;
    // iceoryx2-style: No internal pointer, always resolve via subscribers_[id-1]
    
    INNER_CORE_LOG("[INFO] Created subscriber ID=%u\n", sub_id);
    return Result<void>::FromValue();
}

Result<void> SharedMemoryAllocator::destroySubscriber(SubscriberHandle& handle) noexcept {
    if (handle.subscriber_id == 0 || handle.subscriber_id > 64) {
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    }
    
    SubscriberState* sub = &subscribers_[handle.subscriber_id - 1];
    
    if (!sub->active.load(std::memory_order_acquire)) {
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    }
    
    sub->active.store(false, std::memory_order_release);
    active_subscribers_.fetch_sub(1, std::memory_order_relaxed);
    
    handle.subscriber_id = 0;
    // iceoryx2-style: No internal pointer to clear
    
    return Result<void>::FromValue();
}

// Publisher: loan() - Lock-free allocation (iceoryx2-style with ownership)
// ============================================================================
Result<void> SharedMemoryAllocator::loan(const PublisherHandle& publisher, Size size, SharedMemoryMemoryBlock& block) noexcept {
    // Validate publisher
    if (publisher.publisher_id == 0 || publisher.publisher_id > 64) {
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    }
    
    if (!initialized_.load(std::memory_order_acquire)) {
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kNotInitialized));
    }
    
    PublisherState* pub = &publishers_[publisher.publisher_id - 1];
    if (!pub->active.load(std::memory_order_acquire)) {
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    }
    
    // ✅ PHASE 2.2: Check quota limit (iceoryx2-style)
    UInt32 current_loaned = pub->loan_counter.load(std::memory_order_relaxed);
    if (current_loaned >= pub->max_loaned_samples) {
        if (false) {
            INNER_CORE_LOG("[ERROR] loan: Publisher %u exceeds max_loaned_samples (%u/%u)\\n",
                          publisher.publisher_id, current_loaned, pub->max_loaned_samples);
        }
        loan_failures_.fetch_add(1, std::memory_order_relaxed);
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kResourceExhausted));
    }
    
    // Validate size
    constexpr Size MAX_OVERFLOW_SIZE = 1024 * 1024 * 1024;  // 1GB limit
    if (size > MAX_OVERFLOW_SIZE) {
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    }
    
    // Try to pop chunk from free list (lock-free) if size fits
    ChunkHeader* chunk = nullptr;
    if (size <= config_.max_chunk_size) {
        chunk = popFreeChunk();
        
        // Skip chunks from inactive/freed segments (marked as INVALID after shrink)
        while (chunk && chunk->state.load(std::memory_order_acquire) == ChunkState::INVALID) {
            chunk = popFreeChunk();  // Get next chunk
        }
        
        // ⚡ OPTIMIZATION: Only retrieve returned samples when pool is exhausted
        // This avoids O(64) overhead on every loan() call
        if (!chunk) {
            retrieveReturnedSamples(pub);
            chunk = popFreeChunk();  // Retry after reclaim
            
            // Skip invalid chunks again
            while (chunk && chunk->state.load(std::memory_order_acquire) == ChunkState::INVALID) {
                chunk = popFreeChunk();
            }
        }
    }
    
    if (!chunk) {
        // Pool exhausted or size too large - apply allocation policy
        
        // ✅ NEW: Try to allocate new segment if dynamic growth enabled
        if ((config_.segment_growth_size > 0) && size <= config_.max_chunk_size) {
            if (allocateNewSegment()) {
                chunk = popFreeChunk();  // Retry after new segment allocated
                if (chunk) {
                    INNER_CORE_LOG("[INFO] loan: allocated chunk from new segment\n");
                }
            }
        }
        
        if (!chunk) {
            // Still no chunk - apply fallback policy
            switch (config_.allocation_policy) {
            case AllocationPolicy::WAIT_SYNC: {
                // Adaptive spin-wait with exponential backoff (reduces CPU usage)
                size_t retry_count = 0;
                const size_t MAX_RETRIES = 100000000;  // 100M retries (~10 seconds)
                size_t backoff = 1;
                const size_t MAX_BACKOFF = 1024;  // Max backoff iterations
                
                while (!chunk && retry_count < MAX_RETRIES) {
                    chunk = popFreeChunk();
                    if (chunk) break;
                    
                    retry_count++;
                    
                    // Adaptive exponential backoff strategy:
                    // Phase 1 (0-1K): exponential backoff spin (low latency, exponential growth)
                    // Phase 2 (1K-100K): yield with exponential backoff (balanced)
                    // Phase 3 (100K+): sleep for short periods (reduce CPU)
                    if (retry_count < 1000) {
                        // Tight spin with exponential growth for low latency
                        for (volatile size_t i = 0; i < backoff; ++i);
                        backoff = (backoff * 2 > MAX_BACKOFF) ? MAX_BACKOFF : backoff * 2;
                    } else if (retry_count < 100000) {
                        // Yield CPU to other threads with exponential backoff
                        for (volatile size_t i = 0; i < backoff; ++i);
                        std::this_thread::yield();
                        backoff = (backoff * 2 > MAX_BACKOFF) ? MAX_BACKOFF : backoff * 2;
                    } else {
                        // Long wait: sleep briefly to reduce CPU load
                        std::this_thread::sleep_for(std::chrono::microseconds(10));
                    }
                    
                    if (retry_count % 10000000 == 0 && false) {
                        INNER_CORE_LOG("loan: WAIT_SYNC retry %zu M (backoff=%zu)\\n", 
                                      retry_count / 1000000, backoff);
                    }
                }
                
                if (!chunk) {
                    loan_failures_.fetch_add(1, std::memory_order_relaxed);
                    INNER_CORE_LOG("[ERROR] loan: WAIT_SYNC timeout after %zu retries\\n", retry_count);
                    return Result<void>::FromError(MakeErrorCode(CoreErrc::kResourceExhausted));
                }
                break;
            }
            
            case AllocationPolicy::WAIT_ASYNC: {
                std::unique_lock<std::mutex> lock(free_chunk_mutex_);
                auto timeout = std::chrono::seconds(10);
                auto deadline = std::chrono::steady_clock::now() + timeout;
                
                while (!chunk) {
                    chunk = popFreeChunk();
                    if (chunk) break;
                    
                    if (free_chunk_available_.wait_until(lock, deadline) == std::cv_status::timeout) {
                        loan_failures_.fetch_add(1, std::memory_order_relaxed);
                        INNER_CORE_LOG("[ERROR] loan: WAIT_ASYNC timeout after 10 seconds\\n");
                        return Result<void>::FromError(MakeErrorCode(CoreErrc::kResourceExhausted));
                    }
                }
                break;
            }
            
            case AllocationPolicy::ABORT_ON_FULL: {
                loan_failures_.fetch_add(1, std::memory_order_relaxed);
                if (false) {
                    INNER_CORE_LOG("loan: ABORT_ON_FULL policy, pool exhausted\\n");
                }
                return Result<void>::FromError(MakeErrorCode(CoreErrc::kResourceExhausted));
            }
            
            case AllocationPolicy::USE_OVERFLOW: {
                if (!config_.enable_safe_overflow) {
                    loan_failures_.fetch_add(1, std::memory_order_relaxed);
                    return Result<void>::FromError(MakeErrorCode(CoreErrc::kResourceExhausted));
                }
                
                if (false) {
                    INNER_CORE_LOG("loan: USE_OVERFLOW policy, size=%zu\\n", size);
                }
                
                void* overflow_ptr = SYS_MALLOC(size);
                if (!overflow_ptr) {
                    loan_failures_.fetch_add(1, std::memory_order_relaxed);
                    return Result<void>::FromError(MakeErrorCode(CoreErrc::kOutOfMemory));
                }
                
                overflow_allocations_.fetch_add(1, std::memory_order_relaxed);
                
                // Return overflow block with ownership
                block.ptr = overflow_ptr;
                block.size = size;
                block.chunk_id = 0;
                block.chunk_header = nullptr;
                block.is_loaned = true;
                block.owner_id = publisher.publisher_id;
                
                pub->loan_counter.fetch_add(1, std::memory_order_relaxed);
                
                return Result<void>::FromValue();
            }
            }  // End switch
        }  // End if (!chunk) after segment allocation attempt
    }  // End if (!chunk) initial
    
    // Successfully got chunk from pool - transition FREE → LOANED
    ChunkState expected_state = ChunkState::FREE;
    if (!chunk->state.compare_exchange_strong(
            expected_state, ChunkState::LOANED,
            std::memory_order_acq_rel, std::memory_order_relaxed)) {
        INNER_CORE_LOG("[ERROR] loan: Chunk state mismatch (expected FREE)\\n");
        loan_failures_.fetch_add(1, std::memory_order_relaxed);
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kNotInitialized));
    }
    
    // Update chunk metadata
    chunk->payload_size = size;
    chunk->publisher_id = publisher.publisher_id;
    
    // Fill in block descriptor
    block.ptr = chunk->user_payload;
    block.size = size;
    block.chunk_id = chunk->chunk_id;
    block.chunk_header = chunk;
    block.is_loaned = true;
    block.owner_id = publisher.publisher_id;
    
    // Update counters
    // Release: ensure chunk initialization is visible to send()
    pub->loan_counter.fetch_add(1, std::memory_order_release);
    
    // Update peak memory usage
    Size current_usage = size;
    Size peak = peak_memory_usage_.load(std::memory_order_relaxed);
    while (current_usage > peak) {
        if (peak_memory_usage_.compare_exchange_weak(
                peak, current_usage,
                std::memory_order_relaxed, std::memory_order_relaxed)) {
            break;
        }
    }
    
    if (false) {
        INNER_CORE_LOG("loan: Publisher %u loaned chunk_id=%lu, size=%zu (loan_counter=%u/%u)\\n",
                      publisher.publisher_id,
                      static_cast<unsigned long>(chunk->chunk_id), size,
                      pub->loan_counter.load(std::memory_order_relaxed),
                      pub->max_loaned_samples);
    }
    
    return Result<void>::FromValue();
}

// ============================================================================
// Publisher: send() - BROADCAST to all subscribers (iceoryx2-style)
// ============================================================================
Result<void> SharedMemoryAllocator::send(const PublisherHandle& publisher, SharedMemoryMemoryBlock& block) noexcept {
    // Validate publisher
    if (publisher.publisher_id == 0 || publisher.publisher_id > 64) {
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    }
    
    PublisherState* pub = &publishers_[publisher.publisher_id - 1];
    if (!pub->active.load(std::memory_order_acquire)) {
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    }
    
    // ✅ PHASE 2.3: Publisher releases ownership (loan_counter--)
    // 对标 iceoryx2: sender.rs:send_sample() calls loan_counter.fetch_sub(1)
    // Acquire: ensure we see chunk initialization from loan()
    pub->loan_counter.fetch_sub(1, std::memory_order_acquire);
    
    // Validate ownership
    if (block.chunk_header && block.owner_id != publisher.publisher_id) {
        INNER_CORE_LOG("[ERROR] send: Ownership violation (block owned by %u, publisher is %u)\n",
                       block.owner_id, publisher.publisher_id);
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    }
    
    // Handle overflow allocation (bypass pool, just free it)
    if (block.chunk_header == nullptr) {
        if (block.ptr) {
            SYS_FREE(block.ptr);
            block.ptr = nullptr;
        }
        block.is_loaned = false;
        total_sends_.fetch_add(1, std::memory_order_relaxed);
        if (false) {
            INNER_CORE_LOG("send: overflow block freed (chunk_id=0, size=%zu)\n", block.size);
        }
        return Result<void>::FromValue();
    }
    
    ChunkHeader* chunk = static_cast<ChunkHeader*>(block.chunk_header);
    
    // Get active subscriber count BEFORE state transition
    UInt32 active_subs = active_subscribers_.load(std::memory_order_acquire);
    
    if (active_subs == 0) {
        // No active subscribers - directly return chunk to pool
        if (false) {
            INNER_CORE_LOG("send: No active subscribers, chunk_id=%lu returned to pool\n",
                          static_cast<unsigned long>(chunk->chunk_id));
        }
        
        // Transition LOANED -> FREE
        ChunkState expected_state = ChunkState::LOANED;
        if (chunk->state.compare_exchange_strong(expected_state, ChunkState::FREE,
                                                  std::memory_order_release,
                                                  std::memory_order_relaxed)) {
            pushFreeChunk(chunk);
        }
        
        block.ptr = nullptr;
        block.chunk_header = nullptr;
        block.is_loaned = false;
        
        return Result<void>::FromValue();
    }
    
    // ❌ REMOVED: chunk->ref_count.store(0, ...) 
    // 在双计数器架构中，sample_reference_counter默认就是0
    // 对标 iceoryx2: sample_ref 初始值为0，send时才递增
    
    // Transition LOANED → SENT (atomic CAS with memory_order_release)
    ChunkState expected_state = ChunkState::LOANED;
    if (!chunk->state.compare_exchange_strong(
            expected_state, ChunkState::SENT,
            std::memory_order_release,  // Ensure data writes visible to all receivers
            std::memory_order_relaxed)) {
        INNER_CORE_LOG("[ERROR] send: Invalid state (expected LOANED, got %u)\n",
                       static_cast<uint32_t>(expected_state));
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    }
    
    // Increment sequence number (ABA problem prevention)
    chunk->sequence.fetch_add(1, std::memory_order_relaxed);
    
    // ⚡ OPTIMIZATION: Calculate distance once (avoid repeated multiplication)
    UInt32 distance = getDistanceToChunk(chunk);
    
    // BROADCAST: Enqueue to ALL active subscriber queues
    UInt32 broadcast_count = 0;
    UInt32 remaining_active = active_subs;  // Track remaining to process
    
    for (UInt32 i = 0; i < 64 && remaining_active > 0; ++i) {
        SubscriberState* sub = &subscribers_[i];
        // Must use acquire to see subscriber initialization
        bool is_active = sub->active.load(std::memory_order_acquire);
        
        if (is_active) {
            remaining_active--;
            // Check queue capacity (iceoryx2-style overflow policy)
            bool should_enqueue = true;
            if (config_.subscriber_queue_capacity > 0) {
                uint32_t current_size = sub->rx_queue.size();
                if (current_size >= config_.subscriber_queue_capacity) {
                    // Queue is full - apply overflow policy
                    if (config_.queue_overflow_policy == QueueOverflowPolicy::DISCARD_OLDEST) {
                        // Drop oldest message to make room
                        ChunkHeader* oldest = sub->rx_queue.dequeue();
                        if (oldest) {
                            // ✅ PHASE 2.3: Decrement sample_reference_counter
                            // 对标 iceoryx2: segment_state.release_sample()
                            UInt32 oldest_distance = getDistanceToChunk(oldest);
                            uint64_t old_ref = segment_state_->releaseSample(oldest_distance);
                            
                            if (old_ref == 1) {
                                // Last reference - return to pool
                                oldest->state.store(ChunkState::FREE, std::memory_order_release);
                                pushFreeChunk(oldest);
                            }
                        }
                    } else if (config_.queue_overflow_policy == QueueOverflowPolicy::DISCARD_NEWEST) {
                        // Skip this enqueue (drop new message)
                        should_enqueue = false;
                        if (false) {
                            INNER_CORE_LOG("send: Queue full for subscriber %u, discarding newest\n", i + 1);
                        }
                    } else if (config_.queue_overflow_policy == QueueOverflowPolicy::BLOCK_PUBLISHER) {
                        // Block until queue has space (iceoryx2-style)
                        if (false) {
                            INNER_CORE_LOG("send: Queue full (size=%u, capacity=%u), blocking for subscriber %u\n",
                                          sub->rx_queue.size(), config_.subscriber_queue_capacity, i + 1);
                        }
                        std::unique_lock<std::mutex> lock(sub->queue_mutex);
                        const size_t MAX_WAIT_MS = 5000;  // 5 second timeout
                        auto start = std::chrono::steady_clock::now();
                        
                        while (sub->rx_queue.size() >= config_.subscriber_queue_capacity) {
                            auto elapsed = static_cast<size_t>(
                                std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - start).count());
                            
                            if (elapsed >= MAX_WAIT_MS) {
                                // Timeout - fail the send
                                should_enqueue = false;
                                INNER_CORE_LOG("send: BLOCK_PUBLISHER timeout for subscriber %u (waited %zu ms, queue still at %u)\n",
                                              i + 1, elapsed, sub->rx_queue.size());
                                break;
                            }
                            
                            // Wait for space (100ms timeout per iteration)
                            sub->queue_space_available.wait_for(lock, std::chrono::milliseconds(100));
                        }
                        if (should_enqueue && false) {
                            INNER_CORE_LOG("send: Queue space available for subscriber %u (size=%u)\n",
                                          i + 1, sub->rx_queue.size());
                        }
                    }
                }
            }
            
            if (should_enqueue) {
                // ✅ PHASE 2.3: Increment sample_reference_counter BEFORE enqueue (iceoryx2-style)
                // 对标 iceoryx2: segment_state.borrow_sample(distance)
                // This prevents race: subscriber dequeues before ref is set
                // ⚡ OPTIMIZATION: Use pre-calculated distance
                segment_state_->borrowSample(distance);
                
                bool enqueue_success = sub->rx_queue.enqueue(chunk);
                
                if (enqueue_success) {
                    broadcast_count++;
                    
                    // Notify waiting subscribers (async mode)
                    // Note: notify_one() doesn't require holding mutex (C++11 standard)
                    if (false) {
                        sub->data_available.notify_one();
                    }
                } else {
                    // enqueue failed due to malloc failure - rollback sample_ref
                    // 对标 iceoryx2: segment_state.release_sample(distance)
                    segment_state_->releaseSample(distance);
                    enqueue_failures_.fetch_add(1, std::memory_order_relaxed);
                    INNER_CORE_LOG("[ERROR] send: enqueue failed for subscriber %u (malloc failure)\n", i + 1);
                }
            } else {
                // Message discarded due to queue full
                enqueue_failures_.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    
    // sample_reference_counter was incremented atomically for each successful enqueue
    // Final sample_ref = broadcast_count (number of subscribers holding the chunk)
    
    if (broadcast_count == 0) {
        // All enqueues failed - return chunk to pool immediately
        INNER_CORE_LOG("[ERROR] send: All enqueues failed, returning chunk to pool\n");
        chunk->state.store(ChunkState::FREE, std::memory_order_release);
        pushFreeChunk(chunk);
        block.is_loaned = false;
        block.ptr = nullptr;
        block.chunk_header = nullptr;
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kOutOfMemory));
    }
    
    if (false) {
        INNER_CORE_LOG("send: chunk_id=%lu broadcast to %u subscribers\n",
                      static_cast<unsigned long>(chunk->chunk_id), 
                      broadcast_count);
    }
    
    // Update publisher statistics
    pub->total_sent.fetch_add(1, std::memory_order_relaxed);
    
    // Clear block descriptor (publisher loses ownership)
    block.is_loaned = false;
    block.ptr = nullptr;
    block.chunk_header = nullptr;

    total_sends_.fetch_add(1, std::memory_order_relaxed);

    return Result<void>::FromValue();
}

// ============================================================================
// Subscriber: receive() - Read from own queue (iceoryx2-style BROADCAST)
// ============================================================================
Result<void> SharedMemoryAllocator::receive(const SubscriberHandle& subscriber, SharedMemoryMemoryBlock& block) noexcept {
    // Validate subscriber
    if (subscriber.subscriber_id == 0 || subscriber.subscriber_id > 64) {
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    }
    
    // iceoryx2-style: Resolve SubscriberState via ID index (base address + offset)
    SubscriberState* sub = &subscribers_[subscriber.subscriber_id - 1];
    if (!sub || !sub->active.load(std::memory_order_acquire)) {
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    }
    
    // ✅ PHASE 2.4: Check borrow quota (iceoryx2-style)
    // 对标 iceoryx2: if borrow_counter >= max_borrowed_samples
    UInt32 current_borrow = sub->borrow_counter.load(std::memory_order_acquire);
    if (current_borrow >= sub->max_borrowed_samples) {
        if (false) {
            INNER_CORE_LOG("[ERROR] receive: Subscriber %u exceeds max_borrowed_samples (%u/%u)\n",
                          subscriber.subscriber_id, current_borrow, sub->max_borrowed_samples);
        }
        receive_failures_.fetch_add(1, std::memory_order_relaxed);
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kResourceExhausted));
    }
    
    // Dequeue from subscriber's own receive queue
    ChunkHeader* chunk = sub->rx_queue.dequeue();
    
    if (!chunk) {
        // Queue empty
        receive_failures_.fetch_add(1, std::memory_order_relaxed);
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kWouldBlock));
    }
    
    // Notify blocked publisher that queue has space (BLOCK_PUBLISHER policy)
    // Note: notify_one() doesn't require holding mutex (C++11 standard)
    if (config_.queue_overflow_policy == QueueOverflowPolicy::BLOCK_PUBLISHER &&
        config_.subscriber_queue_capacity > 0) {
        sub->queue_space_available.notify_one();
    }
    
    // BROADCAST MODEL: First subscriber transitions SENT → IN_USE
    // Subsequent subscribers see IN_USE state
    ChunkState current_state = chunk->state.load(std::memory_order_relaxed);
    if (current_state == ChunkState::SENT) {
        // First subscriber: try to transition SENT → IN_USE
        ChunkState expected = ChunkState::SENT;
        chunk->state.compare_exchange_strong(
            expected, ChunkState::IN_USE,
            std::memory_order_release,
            std::memory_order_relaxed);
        // Note: CAS may fail if another subscriber races, that's ok
    } else if (current_state != ChunkState::IN_USE) {
        // Chunk was already freed - rare race condition
        cas_retries_.fetch_add(1, std::memory_order_relaxed);
        receive_failures_.fetch_add(1, std::memory_order_relaxed);
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kWouldBlock));
    }
    
    // Acquire memory ordering ensures we see the publisher's writes
    std::atomic_thread_fence(std::memory_order_acquire);
    
    // Success - fill in block descriptor
    block.ptr = chunk->user_payload;
    block.size = chunk->payload_size;
    block.chunk_id = chunk->chunk_id;
    block.chunk_header = chunk;
    block.is_loaned = false;
    block.owner_id = subscriber.subscriber_id;
    
    // ✅ PHASE 2.4: Increment borrow_counter (iceoryx2-style)
    // 对标 iceoryx2: borrow_counter++
    sub->borrow_counter.fetch_add(1, std::memory_order_release);  // Release: ensure receive visible
    
    total_receives_.fetch_add(1, std::memory_order_relaxed);
    sub->total_received.fetch_add(1, std::memory_order_relaxed);
    
    if (false) {
        INNER_CORE_LOG("receive: chunk_id=%lu by subscriber %u (borrow_counter=%u/%u)\n",
                      static_cast<unsigned long>(chunk->chunk_id),
                      subscriber.subscriber_id,
                      sub->borrow_counter.load(std::memory_order_relaxed), sub->max_borrowed_samples);
    }
    
    return Result<void>::FromValue();
}

// ============================================================================
// Subscriber: release() - Delayed reclaim (iceoryx2-style BROADCAST)
// ============================================================================
Result<void> SharedMemoryAllocator::release(const SubscriberHandle& subscriber, SharedMemoryMemoryBlock& block) noexcept {
    // Validate subscriber
    if (subscriber.subscriber_id == 0 || subscriber.subscriber_id > 64) {
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    }
    
    // iceoryx2-style: Resolve SubscriberState via ID index (base address + offset)
    SubscriberState* sub = &subscribers_[subscriber.subscriber_id - 1];
    if (!sub || !sub->active.load(std::memory_order_acquire)) {
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    }
    
    // Validate ownership (iceoryx2 safety check)
    if (block.chunk_header && block.owner_id != subscriber.subscriber_id) {
        INNER_CORE_LOG("[ERROR] release: Ownership violation (block owned by %u, subscriber is %u)\n",
                       block.owner_id, subscriber.subscriber_id);
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    }
    
    if (!block.chunk_header) {
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    }
    
    ChunkHeader* chunk = static_cast<ChunkHeader*>(block.chunk_header);
    
    // ✅ PHASE 2.5: Decrement borrow_counter (iceoryx2-style)
    // 对标 iceoryx2: borrow_counter--
    // Release: ensure all operations on chunk data are visible before release
    sub->borrow_counter.fetch_sub(1, std::memory_order_release);
    
    // ✅ PHASE 2.5: Push to completion_queue (iceoryx2-style delayed reclaim)
    // 对标 iceoryx2: completion_queue.push(offset)
    // 不立即减少sample_reference_counter，由retrieveReturnedSamples()批量处理
    // MessageQueue is lock-free (Michael-Scott algorithm), no mutex needed
    if (!sub->completion_queue.enqueue(chunk)) {
        // 如果enqueue失败（malloc失败），降级到立即回收
        INNER_CORE_LOG("[WARN] release: completion_queue enqueue failed, fallback to immediate reclaim\n");
        releaseSampleToPool(chunk);
    }
    
    // Clear block descriptor
    block.ptr = nullptr;
    block.chunk_header = nullptr;
    
    total_releases_.fetch_add(1, std::memory_order_relaxed);
    sub->total_released.fetch_add(1, std::memory_order_relaxed);
    
    if (false) {
        INNER_CORE_LOG("release: chunk_id=%lu by subscriber %u (borrow_counter=%u)\n",
                      static_cast<unsigned long>(chunk->chunk_id),
                      subscriber.subscriber_id, sub->borrow_counter.load(std::memory_order_relaxed));
    }
    
    return Result<void>::FromValue();
}

// ============================================================================
// Convenience: Auto-detect release type
// ============================================================================

Result<void> SharedMemoryAllocator::release(SharedMemoryMemoryBlock& block) noexcept {
    if (!block.chunk_header) {
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    }
    
    ChunkHeader* chunk = block.chunk_header;
    ChunkState state = chunk->state.load(std::memory_order_acquire);
    
    // Check if this is a LOANED block (publisher loan without send)
    if (state == ChunkState::LOANED && block.is_loaned) {
        // Return loaned sample to pool
        if (!transitionState(chunk, ChunkState::LOANED, ChunkState::FREE)) {
            return Result<void>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
        }
        
        // Clear publisher ownership
        UInt32 pub_id = chunk->publisher_id;
        chunk->publisher_id = 0;
        
        // Decrement publisher's loan counter
        if (pub_id > 0 && pub_id <= 64) {
            PublisherState* pub = &publishers_[pub_id - 1];
            pub->loan_counter.fetch_sub(1, std::memory_order_release);
        }
        
        // Return to free list
        pushFreeChunk(chunk);
        
        // Clear block descriptor
        block.ptr = nullptr;
        block.chunk_header = nullptr;
        block.is_loaned = false;
        
        if (false) {
            INNER_CORE_LOG("release: Returned loaned chunk_id=%lu from publisher %u (loan-without-send)\n",
                          static_cast<unsigned long>(chunk->chunk_id), pub_id);
        }
        
        return Result<void>::FromValue();
    }
    
    // Check if this is a RECEIVED block (subscriber received sample)
    if (state == ChunkState::IN_USE) {
        // Use owner_id to determine subscriber
        if (block.owner_id == 0 || block.owner_id > 64) {
            return Result<void>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
        }
        
        SubscriberHandle sub;
        sub.subscriber_id = block.owner_id;
        
        return release(sub, block);
    }
    
    // Invalid state
    return Result<void>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
}

// ============================================================================
// Statistics
// ============================================================================

void SharedMemoryAllocator::getStats(SharedMemoryAllocatorStats& stats) const noexcept {
    // Read all atomic counters (relaxed ordering is fine for statistics)
    stats.total_loans = total_loans_.load(std::memory_order_relaxed);
    stats.total_sends = total_sends_.load(std::memory_order_relaxed);
    stats.total_receives = total_receives_.load(std::memory_order_relaxed);
    stats.total_releases = total_releases_.load(std::memory_order_relaxed);
    stats.loan_failures = loan_failures_.load(std::memory_order_relaxed);
    stats.receive_failures = receive_failures_.load(std::memory_order_relaxed);
    stats.overflow_allocations = overflow_allocations_.load(std::memory_order_relaxed);
    stats.peak_memory_usage = peak_memory_usage_.load(std::memory_order_relaxed);
    stats.cas_retries = cas_retries_.load(std::memory_order_relaxed);
    stats.enqueue_failures = enqueue_failures_.load(std::memory_order_relaxed);

    // Count chunks in each state (lock-free scan)
    stats.free_chunks = 0;
    stats.loaned_chunks = 0;
    stats.sent_chunks = 0;
    stats.in_use_chunks = 0;

    if (initialized_.load(std::memory_order_acquire) && chunk_headers_) {
        // Use total_chunks_ instead of config_.chunk_count for segment-based mode
        UInt32 total = total_chunks_.load(std::memory_order_relaxed);
        for (UInt32 i = 0; i < total; ++i) {
            ChunkState state = chunk_headers_[i].state.load(std::memory_order_relaxed);
            switch (state) {
                case ChunkState::FREE:   stats.free_chunks++;   break;
                case ChunkState::LOANED: stats.loaned_chunks++; break;
                case ChunkState::SENT:   stats.sent_chunks++;   break;
                case ChunkState::IN_USE: stats.in_use_chunks++; break;
                case ChunkState::INVALID: /* skip - from freed segment */ break;
            }
        }
    }
}

void SharedMemoryAllocator::resetStats() noexcept {
    // Reset all counters (relaxed ordering is fine)
    total_loans_.store(0, std::memory_order_relaxed);
    total_sends_.store(0, std::memory_order_relaxed);
    total_receives_.store(0, std::memory_order_relaxed);
    total_releases_.store(0, std::memory_order_relaxed);
    loan_failures_.store(0, std::memory_order_relaxed);
    receive_failures_.store(0, std::memory_order_relaxed);
    overflow_allocations_.store(0, std::memory_order_relaxed);
    peak_memory_usage_.store(0, std::memory_order_relaxed);
    cas_retries_.store(0, std::memory_order_relaxed);
}

// ============================================================================
// Helper Functions
// ============================================================================

bool SharedMemoryAllocator::transitionState(
    ChunkHeader* chunk, ChunkState expected, ChunkState desired) noexcept {
    return chunk->state.compare_exchange_strong(
        expected, desired,
        std::memory_order_acq_rel,
        std::memory_order_relaxed
    );
}

uint32_t SharedMemoryAllocator::countChunksInState(ChunkState state) const noexcept {
    if (!initialized_.load(std::memory_order_acquire) || !chunk_headers_) {
        return 0;
    }

    uint32_t count = 0;
    for (UInt32 i = 0; i < config_.chunk_count; ++i) {
        if (chunk_headers_[i].state.load(std::memory_order_relaxed) == state) {
            ++count;
        }
    }
    return count;
}

// ============================================================================
// Dual-Counter Helper Methods (iceoryx2-style)
// ============================================================================

UInt32 SharedMemoryAllocator::getDistanceToChunk(const ChunkHeader* chunk) const noexcept {
    // Calculate chunk index in the global chunk_headers_ array
    // This is the "sample index" in iceoryx2 terminology
    UInt32 chunk_index = static_cast<UInt32>(chunk - chunk_headers_);
    
    // iceoryx2 uses sample index directly for reference counting
    // The distance is just the index (not byte offset)
    // segment_state_ uses this index to look up the reference counter
    return chunk_index;
}

void SharedMemoryAllocator::retrieveReturnedSamples(PublisherState* publisher) noexcept {
    // 对标 iceoryx2: sender.rs:retrieve_returned_samples()
    // Batch reclaim released samples from all subscribers' completion_queues
    (void)publisher;  // Reserved for future optimization
    
    if (!segment_state_) return;
    
    // Iterate all active subscribers
    for (UInt32 i = 0; i < 64; ++i) {
        SubscriberState* sub = &subscribers_[i];
        if (!sub->active.load(std::memory_order_acquire)) {
            continue;
        }
        
        // Batch reclaim from completion_queue (lock-free dequeue)
        // MessageQueue is lock-free (Michael-Scott algorithm), no mutex needed
        ChunkHeader* chunk;
        while ((chunk = sub->completion_queue.dequeue()) != nullptr) {
            // Decrement sample_reference_counter (对标 iceoryx2: release_sample)
            UInt32 distance = getDistanceToChunk(chunk);
            UInt64 old_ref = segment_state_->releaseSample(distance);
            
            // If last reference (old_ref == 1), return to pool
            if (old_ref == 1) {
                ChunkState expected = ChunkState::IN_USE;
                if (chunk->state.compare_exchange_strong(
                        expected, ChunkState::FREE,
                        std::memory_order_release,
                        std::memory_order_relaxed)) {
                    pushFreeChunk(chunk);
                    
                    // Notify waiters (WAIT_ASYNC policy)
                    // Note: notify_one() doesn't require holding mutex (C++11 standard)
                    if (config_.allocation_policy == AllocationPolicy::WAIT_ASYNC) {
                        free_chunk_available_.notify_one();
                    }
                }
            }
        }
    }
}

bool SharedMemoryAllocator::releaseSampleToPool(ChunkHeader* chunk) noexcept {
    // 对标 iceoryx2: segment_state.rs:release_sample + check last reference
    
    if (!segment_state_) return false;
    
    UInt32 distance = getDistanceToChunk(chunk);
    UInt64 old_ref = segment_state_->releaseSample(distance);
    
    // Check if last reference
    if (old_ref == 1) {
        // Last reference - return to pool
        ChunkState expected = ChunkState::IN_USE;
        if (chunk->state.compare_exchange_strong(
                expected, ChunkState::FREE,
                std::memory_order_release,
                std::memory_order_relaxed)) {
            pushFreeChunk(chunk);
            
            // Notify waiters
            // Note: notify_one() doesn't require holding mutex (C++11 standard)
            if (config_.allocation_policy == AllocationPolicy::WAIT_ASYNC) {
                free_chunk_available_.notify_one();
            }
            
            return true;
        }
    }
    
    return false;
}

// ============================================================================
// Async/Blocking APIs
// ============================================================================

bool SharedMemoryAllocator::hasData(const SubscriberHandle& subscriber) const noexcept {
    if (!initialized_.load(std::memory_order_acquire)) {
        return false;
    }
    
    if (subscriber.subscriber_id == 0 || subscriber.subscriber_id > 64) {
        return false;
    }
    
    // iceoryx2-style: Resolve SubscriberState via ID index
    const SubscriberState* sub = &subscribers_[subscriber.subscriber_id - 1];
    if (!sub || !sub->active.load(std::memory_order_acquire)) {
        return false;
    }
    
    return sub->rx_queue.size() > 0;
}

bool SharedMemoryAllocator::waitForData(const SubscriberHandle& subscriber, int64_t timeout_us) noexcept {
    if (!initialized_.load(std::memory_order_acquire)) {
        return false;
    }
    
    // Validate subscriber
    if (subscriber.subscriber_id == 0 || subscriber.subscriber_id > 64) {
        return false;
    }
    
    // iceoryx2-style: Resolve SubscriberState via ID index
    SubscriberState* sub = &subscribers_[subscriber.subscriber_id - 1];
    if (!sub || !sub->active.load(std::memory_order_acquire)) {
        return false;
    }
    
    // Poll mode: just check queue
    if (timeout_us == 0) {
        return sub->rx_queue.size() > 0;
    }
    
    // Blocking wait with condition variable
    std::unique_lock<std::mutex> lock(sub->wait_mutex);
    
    auto has_data = [&]() {
        return sub->rx_queue.size() > 0;
    };
    
    if (timeout_us < 0) {
        // Infinite wait
        sub->data_available.wait(lock, has_data);
        return true;
    } else {
        // Timed wait
        auto timeout = std::chrono::microseconds(timeout_us);
        return sub->data_available.wait_for(lock, timeout, has_data);
    }
}

std::unique_ptr<WaitSet> SharedMemoryAllocator::createWaitSet() noexcept {
    if (!initialized_.load(std::memory_order_acquire)) {
        return nullptr;
    }
    
    auto waitset = std::make_unique<WaitSet>();
    waitset->allocator_ = this;  // Set allocator pointer (friend access)
    return waitset;
}

} // namespace lap::core
