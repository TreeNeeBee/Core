/**
 * @file        SubscriberRegistry.hpp
 * @author      LightAP Core Team
 * @brief       Lock-free operations for SubscriberRegistry in shared memory
 * @date        2026-01-13
 * @details     Implements double-buffered snapshot mechanism (configurable subscriber count)
 * @copyright   Copyright (c) 2026
 * @note        Based on iceoryx2 lock-free registry design
 */
#ifndef LAP_CORE_IPC_SUBSCRIBER_REGISTRY_OPS_HPP
#define LAP_CORE_IPC_SUBSCRIBER_REGISTRY_OPS_HPP

#include "ControlBlock.hpp"
#include "IPCConfig.hpp"
#include "CCoreErrorDomain.hpp"
#include "CResult.hpp"
#include <atomic>

namespace lap
{
namespace core
{
namespace ipc
{
    class SubscriberRegistry final
    {
    public:
        /**
        * @brief Allocate a unique queue index for new subscriber
        * @param ctrl Pointer to ControlBlock in shared memory
        * @return Unique queue index or kInvalidChunkIndex if full
        * 
        * @details
        * - Finds first available slot from ready_mask using bit operations
        * - Returns kInvalidChunkIndex if all slots occupied (kMaxSubscribers)
        */
        static Result<UInt32> AllocateQueueIndex(ControlBlock* ctrl) noexcept;
        
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
        static inline ControlBlock::SubscriberSnapshot GetSubscriberSnapshot(ControlBlock* ctrl) noexcept
        {
            return ctrl->GetSnapshot();
        }
        
        /**
        * @brief Register a new subscriber queue
        * @param ctrl Pointer to ControlBlock in shared memory
        * @param queue_index Queue index to register
        * @return true if registered successfully, false if full or already exists
        * 
        * @details
        * - Uses bitmask for O(1) registration
        * - Regenerates snapshot from ready_mask
        * - Lock-free with CAS on ready_mask
        */
        static Bool RegisterSubscriber(ControlBlock* ctrl, UInt32 queue_index) noexcept;
        
        /**
        * @brief Unregister a subscriber queue
        * @param ctrl Pointer to ControlBlock in shared memory
        * @param queue_index Queue index to unregister
        * @return true if unregistered successfully, false if not found
        * 
        * @details
        * - Uses bitmask for O(1) unregistration
        * - Regenerates snapshot from ready_mask
        * - Lock-free with CAS on ready_mask
        */
        static Bool UnregisterSubscriber(ControlBlock* ctrl, UInt32 queue_index) noexcept;
    
    protected:
        SubscriberRegistry() = delete;
        ~SubscriberRegistry() = default;

        SubscriberRegistry(const SubscriberRegistry&) = delete;
        SubscriberRegistry& operator=(const SubscriberRegistry&) = delete;
        SubscriberRegistry(SubscriberRegistry&&) = delete;
        SubscriberRegistry& operator=(SubscriberRegistry&&) = delete;
    };
}  // namespace ipc
}  // namespace core
}  // namespace lap

#endif  // LAP_CORE_IPC_SUBSCRIBER_REGISTRY_OPS_HPP
