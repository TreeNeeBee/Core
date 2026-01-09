/**
 * @file        RingBufferBlock.hpp
 * @author      LightAP Core Team
 * @brief       Lock-free SPSC ring buffer
 * @date        2026-01-07
 * @details     Single-Producer Single-Consumer lock-free queue
 * @copyright   Copyright (c) 2026
 * @note        Based on iceoryx2 design
 */
#ifndef LAP_CORE_IPC_RING_BUFFER_BLOCK_HPP
#define LAP_CORE_IPC_RING_BUFFER_BLOCK_HPP

#include "IPCTypes.hpp"
#include "COptional.hpp"
#include <atomic>

namespace lap
{
namespace core
{
namespace ipc
{
    /**
     * @brief Lock-free ring buffer for SPSC communication
     * @tparam T Element type (typically UInt32 for chunk indices)
     * @tparam Capacity Buffer capacity (default 256)
     * 
     * @details
     * - Single Producer, Single Consumer (SPSC)
     * - Lock-free using atomic head/tail pointers
     * - Fixed capacity determined at compile time
     * - Uses power-of-2 capacity for efficient modulo
     */
    template <typename T, UInt32 Capacity = kDefaultQueueCapacity>
    class RingBufferBlock
    {
        static_assert((Capacity & (Capacity - 1)) == 0, 
                     "Capacity must be a power of 2");
        
    public:
        /**
         * @brief Constructor
         */
        RingBufferBlock() noexcept
            : head_(0)
            , tail_(0)
        {
            // Zero-initialize buffer
            for (UInt32 i = 0; i < Capacity; ++i)
            {
                buffer_[i] = T{};
            }
        }
        
        /**
         * @brief Enqueue element (producer only)
         * @param value Value to enqueue
         * @return true if enqueued, false if full
         */
        bool Enqueue(T value) noexcept
        {
            UInt32 tail = tail_.load(std::memory_order_relaxed);
            UInt32 next_tail = (tail + 1) & (Capacity - 1);
            
            // Check if full
            UInt32 head = head_.load(std::memory_order_acquire);
            if (next_tail == head)
            {
                return false;  // Queue full
            }
            
            // Write data
            buffer_[tail] = value;
            
            // Update tail (release semantics for consumer visibility)
            tail_.store(next_tail, std::memory_order_release);
            
            return true;
        }
        
        /**
         * @brief Dequeue element (consumer only)
         * @return Optional value (empty if queue empty)
         */
        Optional<T> Dequeue() noexcept
        {
            UInt32 head = head_.load(std::memory_order_relaxed);
            
            // Check if empty
            UInt32 tail = tail_.load(std::memory_order_acquire);
            if (head == tail)
            {
                return {};  // Queue empty
            }
            
            // Read data
            T value = buffer_[head];
            
            // Update head (release semantics for producer visibility)
            UInt32 next_head = (head + 1) & (Capacity - 1);
            head_.store(next_head, std::memory_order_release);
            
            return value;
        }
        
        /**
         * @brief Check if queue is full
         * @return true if full
         */
        bool IsFull() const noexcept
        {
            UInt32 tail = tail_.load(std::memory_order_relaxed);
            UInt32 next_tail = (tail + 1) & (Capacity - 1);
            UInt32 head = head_.load(std::memory_order_acquire);
            
            return next_tail == head;
        }
        
        /**
         * @brief Check if queue is empty
         * @return true if empty
         */
        bool IsEmpty() const noexcept
        {
            UInt32 head = head_.load(std::memory_order_relaxed);
            UInt32 tail = tail_.load(std::memory_order_acquire);
            
            return head == tail;
        }
        
        /**
         * @brief Get current size (approximate)
         * @return Number of elements
         * @note This is an approximation due to concurrent access
         */
        UInt32 Size() const noexcept
        {
            UInt32 head = head_.load(std::memory_order_relaxed);
            UInt32 tail = tail_.load(std::memory_order_relaxed);
            
            return (tail - head) & (Capacity - 1);
        }
        
        /**
         * @brief Get capacity
         * @return Maximum capacity
         */
        static constexpr UInt32 GetCapacity() noexcept
        {
            return Capacity;
        }
        
    private:
        alignas(kCacheLineSize) std::atomic<UInt32> head_;  ///< Consumer pointer
        alignas(kCacheLineSize) std::atomic<UInt32> tail_;  ///< Producer pointer
        T buffer_[Capacity];                                 ///< Data buffer
    };
    
}  // namespace ipc
}  // namespace core
}  // namespace lap

#endif  // LAP_CORE_IPC_RING_BUFFER_BLOCK_HPP
