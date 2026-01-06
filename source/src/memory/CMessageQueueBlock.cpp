/**
 * @file CMessageQueueBlock.cpp
 * @brief Implementation of MessageQueueBlock
 * @author LightAP Team
 * @date 2026-01-02
 */

#include "memory/CMessageQueueBlock.hpp"
#include "memory/CSharedMemoryAllocator.hpp"  // For ChunkHeader
#include "memory/CMemoryUtils.hpp"  // Unified memory allocation macros
#include <sys/mman.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>

// Jemalloc integration (if enabled)
#if defined(LAP_USE_JEMALLOC)
    #include <jemalloc/jemalloc.h>
#endif

// Aligned allocation macro
#define SYS_ALIGNED_ALLOC(align, size) std::aligned_alloc(align, size)

// SHM segment allocation using mmap
#define SHM_SEGMENT_MMAP(size) ::mmap(nullptr, size, PROT_READ | PROT_WRITE, \
                                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
#define SHM_SEGMENT_MUNMAP(ptr, size) ::munmap(ptr, size)

// Cache line size for alignment
static constexpr size_t CACHE_LINE_SIZE = 64;

namespace lap::core {

MessageQueueBlock::MessageQueueBlock(void* base_addr, Size memory_size, Size block_size) noexcept
    : base_(base_addr)
    , block_size_(block_size)
    , capacity_(0)
    , total_memory_size_(memory_size)
    , head_(0)
    , tail_(0) {
    
    // Validate parameters
    if (!base_addr) {
        INNER_CORE_LOG("[ERROR] MessageQueueBlock: base_addr cannot be nullptr\n");
        base_ = nullptr;
        return;
    }
    
    if (memory_size == 0) {
        INNER_CORE_LOG("[ERROR] MessageQueueBlock: memory_size cannot be zero\n");
        base_ = nullptr;
        return;
    }
    
    if (block_size == 0) {
        INNER_CORE_LOG("[ERROR] MessageQueueBlock: block_size cannot be zero\n");
        base_ = nullptr;
        return;
    }
    
    // Calculate capacity from memory size
    capacity_ = static_cast<UInt32>(memory_size / block_size);
    
    if (capacity_ == 0) {
        INNER_CORE_LOG("[ERROR] MessageQueueBlock: memory_size (%zu) too small for even one block (need %zu)\n",
                       memory_size, block_size);
        base_ = nullptr;
        return;
    }
    
    // Initialize ring buffer to all zeros (null pointers)
    std::memset(base_, 0, memory_size);
    
    INNER_CORE_LOG("[INFO] MessageQueueBlock: RingBuffer initialized - addr=%p, capacity=%u, block_size=%zu, total=%zu\n",
                   base_addr, capacity_, block_size, memory_size);
}

MessageQueueBlock::~MessageQueueBlock() noexcept {
    // Note: Memory is NOT freed here - caller is responsible for deallocation
    base_ = nullptr;
}

bool MessageQueueBlock::enqueue(void* ptr) noexcept {
    if (!base_) return false;
    
    // Load current tail
    UInt32 current_tail = tail_.load(std::memory_order_relaxed);
    UInt32 next_tail = (current_tail + 1) % capacity_;
    
    // Check if buffer is full
    UInt32 current_head = head_.load(std::memory_order_acquire);
    if (next_tail == current_head) {
        return false;  // Buffer full
    }
    
    // Write data to tail position
    void** slots = static_cast<void**>(base_);
    slots[current_tail] = ptr;
    
    // Update tail (only one thread should update tail)
    tail_.store(next_tail, std::memory_order_release);
    
    return true;
}

bool MessageQueueBlock::dequeue(void*& ptr) noexcept {
    if (!base_) return false;
    
    // Load current head
    UInt32 current_head = head_.load(std::memory_order_relaxed);
    
    // Check if buffer is empty
    UInt32 current_tail = tail_.load(std::memory_order_acquire);
    if (current_head == current_tail) {
        return false;  // Buffer empty
    }
    
    // Read data from head position
    void** slots = static_cast<void**>(base_);
    ptr = slots[current_head];
    
    // Update head (only one thread should update head)
    UInt32 next_head = (current_head + 1) % capacity_;
    head_.store(next_head, std::memory_order_release);
    
    return true;
}

} // namespace lap::core
