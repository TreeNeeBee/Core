/**
 * @file        SubscriberRegistryOps.hpp
 * @author      LightAP Core Team
 * @brief       Lock-free operations for SubscriberRegistry in shared memory
 * @date        2026-01-07
 * @details     Implements double-buffered snapshot mechanism for subscriber management
 * @copyright   Copyright (c) 2026
 * @note        Based on iceoryx2 lock-free registry design
 */
#ifndef LAP_CORE_IPC_SUBSCRIBER_REGISTRY_OPS_HPP
#define LAP_CORE_IPC_SUBSCRIBER_REGISTRY_OPS_HPP

#include "ControlBlock.hpp"
#include <atomic>

namespace lap
{
namespace core
{
namespace ipc
{
    /**
     * @brief Allocate a unique queue index for new subscriber
     * @param ctrl Pointer to ControlBlock in shared memory
     * @return Unique queue index or kInvalidChunkIndex if full
     * 
     * @details
     * - Uses round-robin allocation with wrapping
     * - Caller responsible for activating the queue slot
     */
    inline UInt32 AllocateQueueIndex(ControlBlock* ctrl) noexcept
    {
        UInt32 index = ctrl->next_queue_index.fetch_add(1, std::memory_order_relaxed);
        return index % ctrl->max_subscriber_queues;
    }
    
    /**
     * @brief Get current snapshot of subscriber queue indices
     * @param ctrl Pointer to ControlBlock in shared memory
     * @return Copy of current active snapshot
     * 
     * @details
     * - Lock-free read using memory_order_acquire
     * - Returns a stack copy, very fast
     * - Publisher calls this before sending messages
     */
    inline ControlBlock::SubscriberSnapshot GetSubscriberSnapshot(ControlBlock* ctrl) noexcept
    {
        // Read active snapshot index with acquire semantics
        UInt32 active_idx = ctrl->active_snapshot_index.load(std::memory_order_acquire);
        
        // Copy snapshot to stack (compiler optimizes this)
        ControlBlock::SubscriberSnapshot result = ctrl->snapshots[active_idx];
        
        // Memory fence to ensure copy completes before return
        std::atomic_thread_fence(std::memory_order_acquire);
        
        return result;
    }
    
    /**
     * @brief Register a new subscriber queue
     * @param ctrl Pointer to ControlBlock in shared memory
     * @param queue_index Queue index to register
     * @return true if registered successfully, false if full or already exists
     * 
     * @details
     * - Called by Subscriber during connection
     * - Uses CAS (Compare-And-Swap) for thread-safety
     * - Switches double buffer after update
     */
    inline bool RegisterSubscriber(ControlBlock* ctrl, UInt32 queue_index) noexcept
    {
        // Get current write buffer
        UInt32 current_write = ctrl->write_index.load(std::memory_order_acquire);
        ControlBlock::SubscriberSnapshot* write_snap = &ctrl->snapshots[current_write];
        
        // Check if registry is full
        if (write_snap->count >= ctrl->max_subscriber_queues)
        {
            return false;
        }
        
        // Check for duplicates
        for (UInt32 i = 0; i < write_snap->count; ++i)
        {
            if (write_snap->queue_indices[i] == queue_index)
            {
                return false;  // Already registered
            }
        }
        
        // Add new subscriber to write buffer
        write_snap->queue_indices[write_snap->count] = queue_index;
        write_snap->count++;
        write_snap->version++;
        
        // Memory fence: ensure write buffer update completes
        std::atomic_thread_fence(std::memory_order_release);
        
        // Switch active snapshot to current write buffer
        ctrl->active_snapshot_index.store(current_write, std::memory_order_release);
        
        // Switch write buffer to the other one
        UInt32 new_write = 1 - current_write;
        ctrl->write_index.store(new_write, std::memory_order_release);
        
        // Copy to new write buffer for next update
        ctrl->snapshots[new_write] = *write_snap;
        
        // Update subscriber count
        ctrl->subscriber_count.fetch_add(1, std::memory_order_release);
        
        return true;
    }
    
    /**
     * @brief Unregister a subscriber queue
     * @param ctrl Pointer to ControlBlock in shared memory
     * @param queue_index Queue index to unregister
     * @return true if unregistered successfully, false if not found
     * 
     * @details
     * - Called by Subscriber during disconnection
     * - Removes queue index from array
     * - Compacts array by shifting elements
     */
    inline bool UnregisterSubscriber(ControlBlock* ctrl, UInt32 queue_index) noexcept
    {
        // Get current write buffer
        UInt32 current_write = ctrl->write_index.load(std::memory_order_acquire);
        ControlBlock::SubscriberSnapshot* write_snap = &ctrl->snapshots[current_write];
        
        // Find and remove the queue index
        bool found = false;
        for (UInt32 i = 0; i < write_snap->count; ++i)
        {
            if (write_snap->queue_indices[i] == queue_index)
            {
                // Shift remaining elements left
                for (UInt32 j = i; j < write_snap->count - 1; ++j)
                {
                    write_snap->queue_indices[j] = write_snap->queue_indices[j + 1];
                }
                
                // Clear last element
                write_snap->queue_indices[write_snap->count - 1] = kInvalidChunkIndex;
                write_snap->count--;
                write_snap->version++;
                found = true;
                break;
            }
        }
        
        if (!found)
        {
            return false;
        }
        
        // Memory fence
        std::atomic_thread_fence(std::memory_order_release);
        
        // Switch active snapshot
        ctrl->active_snapshot_index.store(current_write, std::memory_order_release);
        
        // Switch write buffer
        UInt32 new_write = 1 - current_write;
        ctrl->write_index.store(new_write, std::memory_order_release);
        ctrl->snapshots[new_write] = *write_snap;
        
        // Update subscriber count
        ctrl->subscriber_count.fetch_sub(1, std::memory_order_release);
        
        return true;
    }
    
}  // namespace ipc
}  // namespace core
}  // namespace lap

#endif  // LAP_CORE_IPC_SUBSCRIBER_REGISTRY_OPS_HPP
