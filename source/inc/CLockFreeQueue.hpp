/**
 * @file        CLockFreeQueue.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       Lock-free queue implementation using CAS operations
 * @date        2025-11-01
 * @details     A thread-safe lock-free queue supporting custom allocators
 * @copyright   Copyright (c) 2025
 * @note
 * sdk:
 * platform:
 * project:     LightAP
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2023/07/16  <td>1.0      <td>ddkv587         <td>init version
 * <tr><td>2025/10/27  <td>1.1      <td>ddkv587         <td>update header format
 * <tr><td>2025/10/29  <td>2.0      <td>ddkv587         <td>migrate to std::filesystem (C++17)
 * <tr><td>2025/11/01  <td>2.1      <td>ddkv587         <td>add missing includes and fix implementation
 * </table>
 */

#ifndef LAP_LOCKFREEQUEUE_HPP
#define LAP_LOCKFREEQUEUE_HPP

#include <atomic>
#include <memory>
#include <thread>
#include "CTypedef.hpp"

namespace lap {
namespace core {

// Define magic type based on pointer size
#if defined(__LP64__) || defined(_WIN64) || (defined(__WORDSIZE) && __WORDSIZE == 64)
    // 64-bit system
    using MagicType = UInt64;
    #define MAGIC_VALUE 0x5A5A5A5A5A5A5A5AULL  // Base constant for backward compatibility
#else
    // 32-bit system
    using MagicType = UInt32;
    #define MAGIC_VALUE 0x5A5A5A5AU  // Base constant for backward compatibility
#endif

    template <typename T>
    struct QueueNode {
        T data;
        std::atomic<QueueNode<T>*> next;
        std::atomic<MagicType> version{0};
        MagicType magic = MAGIC_VALUE;
        
        QueueNode() : next(nullptr), version(0), magic(MAGIC_VALUE) {}
        explicit QueueNode(const T& val) : data(val), next(nullptr), version(0), magic(MAGIC_VALUE) {}
    };

    template <typename T, typename Alloc = std::allocator<T>>
    class LockFreeQueue {
    private:
        using AllocTraits = std::allocator_traits<Alloc>;
        using Node = QueueNode<T>;  // 前文定义
        using NodeAlloc = typename AllocTraits::template rebind_alloc<Node>;

        std::atomic<Node*> m_pHead;
        std::atomic<Node*> m_pTail;
        NodeAlloc m_nodeAlloc;  // Allocator instance

    public:
        explicit LockFreeQueue(const Alloc& alloc = Alloc())
            : m_nodeAlloc(alloc)
        {
            Node* sentinel = m_nodeAlloc.allocate(1);
            new (sentinel) Node();  // Placement new to construct
            sentinel->next.store(nullptr, std::memory_order_relaxed);
            sentinel->version.store(0, std::memory_order_relaxed);
            m_pHead.store(sentinel, std::memory_order_release);
            m_pTail.store(sentinel, std::memory_order_release);
        }

        ~LockFreeQueue()
        {
            Node* cur = m_pHead.load(std::memory_order_acquire);
            while (cur) {
                Node* next = cur->next.load(std::memory_order_relaxed);
                cur->~Node();  // Explicit destructor call
                m_nodeAlloc.deallocate(cur, 1);
                cur = next;
            }
        }

        void Enqueue(const T& val)
        {
            Node* new_node = m_nodeAlloc.allocate(1);
            new (new_node) Node(val);
            new_node->next.store(nullptr, std::memory_order_relaxed);

            int retry_count = 0;
            while (true) {
                Node* old_tail = m_pTail.load(std::memory_order_acquire);
                Node* old_next = old_tail->next.load(std::memory_order_acquire);
                
                // Check if tail is still consistent
                if (old_tail == m_pTail.load(std::memory_order_acquire)) {
                    // Was tail pointing to the last node?
                    if (old_next == nullptr) {
                        // Try to link new node at the end
                        if (old_tail->next.compare_exchange_weak(old_next, new_node,
                                                                std::memory_order_release,
                                                                std::memory_order_relaxed)) {
                            // Successfully enqueued, try to swing tail
                            MagicType old_version = old_tail->version.load(std::memory_order_relaxed);
                            new_node->version.store(old_version + 1, std::memory_order_relaxed);
                            m_pTail.compare_exchange_weak(old_tail, new_node,
                                                       std::memory_order_release,
                                                       std::memory_order_relaxed);
                            return;
                        }
                    } else {
                        // Tail was not pointing to the last node, help advance it
                        m_pTail.compare_exchange_weak(old_tail, old_next,
                                                   std::memory_order_release,
                                                   std::memory_order_relaxed);
                    }
                }
                // Adaptive backoff
                if (++retry_count > 5) {
                    std::this_thread::yield();
                    retry_count = 0;
                }
            }
        }

        Bool Dequeue(T& val)
        {
            int retry_count = 0;
            while (true) {
                Node* old_head = m_pHead.load(std::memory_order_acquire);
                Node* old_tail = m_pTail.load(std::memory_order_acquire);
                Node* next = old_head->next.load(std::memory_order_acquire);
                
                // Check if head is still consistent
                if (old_head == m_pHead.load(std::memory_order_acquire)) {
                    // Is queue empty or tail falling behind?
                    if (old_head == old_tail) {
                        // Is queue empty?
                        if (next == nullptr) {
                            return false;  // Queue is empty
                        }
                        // Tail is falling behind, help advance it
                        m_pTail.compare_exchange_weak(old_tail, next,
                                                   std::memory_order_release,
                                                   std::memory_order_relaxed);
                    } else {
                        // Read value before CAS
                        if (next == nullptr) {
                            continue;  // Tail not updated yet, retry
                        }
                        
                        val = next->data;
                        
                        // Try to swing head to the next node
                        if (m_pHead.compare_exchange_weak(old_head, next,
                                                       std::memory_order_release,
                                                       std::memory_order_relaxed)) {
                            // Successfully dequeued, reclaim old dummy node
                            old_head->~Node();
                            m_nodeAlloc.deallocate(old_head, 1);
                            return true;
                        }
                    }
                }
                // Adaptive backoff
                if (++retry_count > 5) {
                    std::this_thread::yield();
                    retry_count = 0;
                }
            }
        }
        
        // Check if queue is empty (not thread-safe for precise state)
        Bool Empty() const
        {
            Node* h = m_pHead.load(std::memory_order_acquire);
            Node* t = m_pTail.load(std::memory_order_acquire);
            return h == t && h->next.load(std::memory_order_relaxed) == nullptr;
        }
    };
} // namespace core
} // namespace lap
#endif // LAP_LOCKFREEQUEUE_HPP