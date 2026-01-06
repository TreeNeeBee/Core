/**
 * @file CMessageQueueBlock.hpp
 * @brief Pre-allocated node pool for lock-free message queues
 * @author LightAP Team
 * @date 2026-01-02
 * 
 * @details
 * MessageQueueBlock provides a pre-allocated node pool for MessageQueue,
 * eliminating malloc/free overhead during enqueue/dequeue operations.
 * 
 * Key features:
 * - Offset-based node management (base + offset instead of raw pointers)
 * - Memory can be allocated via mmap (shared memory) or system malloc
 * - Lock-free allocation using atomic free list (Treiber stack)
 * - Zero malloc/free overhead during queue operations
 * - Compatible with broadcast semantics (one chunk, multiple queue nodes)
 * 
 * Usage:
 * @code
 * // Create node pool with 1024 nodes using system malloc
 * MessageQueueBlock block(1024, false);
 * 
 * // Initialize queues with this block
 * MessageQueue queue;
 * queue.initialize(&block);
 * 
 * // Allocate node
 * UInt32 offset = block.allocateNode();
 * if (offset != 0) {
 *     MessageQueueBlock::Node* node = block.getNode(offset);
 *     node->chunk = my_chunk;
 *     // ... use node ...
 *     block.freeNode(offset);
 * }
 * @endcode
 */

#ifndef LAP_CORE_MESSAGE_QUEUE_BLOCK_HPP
#define LAP_CORE_MESSAGE_QUEUE_BLOCK_HPP

#include "CTypedef.hpp"
#include <atomic>

namespace lap::core {

// Forward declaration
struct ChunkHeader;

/**
 * @brief Lock-free fixed-size ring buffer for message queues
 * 
 * Design principles:
 * 1. Fixed capacity - no dynamic expansion/shrinking at runtime
 * 2. Deterministic behavior - O(1) enqueue/dequeue with bounded retry
 * 3. Lock-free operations - CAS-based head/tail management
 * 4. Cache-friendly - contiguous memory layout
 * 
 * This is a SPSC (Single Producer Single Consumer) or MPMC ring buffer:
 * - Each slot stores a void* pointer (typically ChunkHeader*)
 * - Fixed block_size per element
 * - Bounded capacity ensures deterministic resource usage
 * - Uses external memory (caller provides storage)
 */
class MessageQueueBlock {
public:
    
    /**
     * @brief Constructor - initializes FIXED-SIZE ring buffer from external memory
     * @param base_addr Pre-allocated memory region base address
     * @param memory_size Size of memory region in bytes
     * @param block_size Size of each block/element in bytes (typically sizeof(void*))
     * 
     * Deterministic guarantees:
     * - Uses exactly the provided memory region, no allocation
     * - No dynamic expansion or shrinking during lifetime
     * - Capacity = memory_size / block_size (fixed)
     * - Memory size is fixed and deterministic
     * 
     * @note Caller is responsible for memory allocation and deallocation
     * @note For pointer queue: block_size = sizeof(void*)
     * @note capacity is calculated as: memory_size / block_size
     */
    MessageQueueBlock(void* base_addr, Size memory_size, Size block_size) noexcept;
    
    /**
     * @brief Destructor - does NOT free memory (caller responsible)
     * 
     * Note: Memory is NOT freed here - caller is responsible for deallocation
     */
    ~MessageQueueBlock() noexcept;
    
    /**
     * @brief Enqueue a pointer to the ring buffer (lock-free)
     * @param ptr Pointer to enqueue
     * @return true if enqueued successfully, false if buffer full
     * 
     * Lock-free operation using CAS on tail index.
     * Returns false if ring buffer is full.
     */
    bool enqueue(void* ptr) noexcept;
    
    /**
     * @brief Dequeue a pointer from the ring buffer (lock-free)
     * @param[out] ptr Output pointer
     * @return true if dequeued successfully, false if buffer empty
     * 
     * Lock-free operation using CAS on head index.
     * Returns false if ring buffer is empty.
     */
    bool dequeue(void*& ptr) noexcept;
    
    /**
     * @brief Get current element count in the queue
     * @return Number of elements currently in the ring buffer
     */
    UInt32 size() const noexcept {
        UInt32 head = head_.load(std::memory_order_relaxed);
        UInt32 tail = tail_.load(std::memory_order_relaxed);
        if (tail >= head) {
            return tail - head;
        } else {
            return capacity_ - head + tail;
        }
    }
    
    /**
     * @brief Get ring buffer capacity (fixed)
     * @return Total capacity (immutable)
     */
    UInt32 getCapacity() const noexcept { 
        return capacity_; 
    }
    
    /**
     * @brief Get block size
     * @return Size of each block in bytes
     */
    Size getBlockSize() const noexcept {
        return block_size_;
    }
    
    /**
     * @brief Get total memory size (deterministic)
     * @return Total memory in bytes (fixed at construction)
     */
    Size getTotalMemorySize() const noexcept {
        return total_memory_size_;
    }
    
    /**
     * @brief Check if ring buffer is empty
     * @return true if no elements in buffer
     */
    bool isEmpty() const noexcept {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }
    
    /**
     * @brief Check if ring buffer is full
     * @return true if buffer is full
     */
    bool isFull() const noexcept {
        UInt32 head = head_.load(std::memory_order_relaxed);
        UInt32 tail = tail_.load(std::memory_order_relaxed);
        UInt32 next_tail = (tail + 1) % capacity_;
        return next_tail == head;
    }
    
    /**
     * @brief Get base address of memory region
     * @return Base address
     */
    void* getBaseAddress() const noexcept {
        return base_;
    }

private:
    void*  base_;                     ///< Base address of ring buffer (external memory)
    Size   block_size_;               ///< Size of each block/element
    UInt32 capacity_;                 ///< Ring buffer capacity (FIXED, immutable)
    Size   total_memory_size_;        ///< Total memory size (FIXED, deterministic)
    std::atomic<UInt32> head_;        ///< Ring buffer head index (dequeue position)
    std::atomic<UInt32> tail_;        ///< Ring buffer tail index (enqueue position)
    
    // Non-copyable, non-movable
    MessageQueueBlock(const MessageQueueBlock&) = delete;
    MessageQueueBlock& operator=(const MessageQueueBlock&) = delete;
    MessageQueueBlock(MessageQueueBlock&&) = delete;
    MessageQueueBlock& operator=(MessageQueueBlock&&) = delete;
};

} // namespace lap::core

#endif // LAP_CORE_MESSAGE_QUEUE_BLOCK_HPP
