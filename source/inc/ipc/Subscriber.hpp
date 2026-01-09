/**
 * @file        Subscriber.hpp
 * @author      LightAP Core Team
 * @brief       Zero-copy subscriber implementation
 * @date        2026-01-07
 * @details     Lock-free message reception with queue-based buffering
 * @copyright   Copyright (c) 2026
 * @note        Based on iceoryx2 design
 */
#ifndef LAP_CORE_IPC_SUBSCRIBER_HPP
#define LAP_CORE_IPC_SUBSCRIBER_HPP

#include "IPCTypes.hpp"
#include "Sample.hpp"
#include "SharedMemoryManager.hpp"
#include "ChunkPoolAllocator.hpp"
#include "SubscriberRegistryOps.hpp"
#include "IPCEventHooks.hpp"
#include "CResult.hpp"
#include "CString.hpp"
#include "Message.hpp"
#include <memory>
#include <type_traits>
#include <chrono>

namespace lap
{
namespace core
{
namespace ipc
{
    /**
     * @brief Subscriber configuration
     */
    struct SubscriberConfig
    {
        UInt32 max_chunks = kDefaultMaxChunks;                      ///< Maximum chunks in pool
        UInt64 chunk_size = 0;                                      ///< Chunk size (payload), 0 means default
        UInt32 queue_capacity = kDefaultQueueCapacity;              ///< Queue capacity
        QueueEmptyPolicy empty_policy = QueueEmptyPolicy::kBlock;   ///< Default policy
    };
    
    /**
     * @brief Zero-copy subscriber
     * @tparam T Message type (must be Message or derived from Message)
     * 
     * @details
     * Usage:
     * 1. Create subscriber with service name
     * 2. Receive() to get message
     * 3. Process message
     * 4. Sample automatically released on destruction
     * 
     * @note T must be Message or a class derived from Message
     */
    template <typename T>
    class Subscriber
    {
    public:
        /**
         * @brief Create subscriber
         * @param service_name Service name
         * @param config Configuration
         * @return Result with subscriber or error
         */
        static Result<Subscriber<T>> Create(const String& service_name,
                                           const SubscriberConfig& config = {}) noexcept;
        
        /**
         * @brief Destructor - automatically disconnects
         */
        ~Subscriber() noexcept;
        
        // Delete copy
        Subscriber(const Subscriber&) = delete;
        Subscriber& operator=(const Subscriber&) = delete;
        
        // Allow move
        Subscriber(Subscriber&&) noexcept = default;
        Subscriber& operator=(Subscriber&&) noexcept = default;
        
        /**
         * @brief Disconnect from service and cleanup
         * @return Result with success or error
         * 
         * @details
         * - Unregister from shared memory registry
         * - Consume remaining messages in queue
         * - Deactivate queue slot
         * - Idempotent (safe to call multiple times)
         */
        Result<void> Disconnect() noexcept;
        
        /**
         * @brief Receive next message
         * @param policy Queue empty policy (overrides config)
         * @return Result with Sample or error
         * 
         * @details
         * - Dequeues chunk index from queue
         * - Creates Sample wrapper
         * - Behavior on empty queue depends on policy
         */
        Result<Sample<T>> Receive(QueueEmptyPolicy policy) noexcept;
        
        /**
         * @brief Receive with default policy
         * @return Result with Sample or error
         */
        Result<Sample<T>> Receive() noexcept
        {
            return Receive(config_.empty_policy);
        }
        
        /**
         * @brief Receive with timeout
         * @param timeout Timeout duration
         * @return Result with Sample or error
         */
        Result<Sample<T>> ReceiveWithTimeout(Duration timeout) noexcept;
        
        /**
         * @brief Get service name
         * @return Service name
         */
        const String& GetServiceName() const noexcept
        {
            return service_name_;
        }
        
        /**
         * @brief Check if queue is empty
         * @return true if empty
         */
        bool IsQueueEmpty() const noexcept;
        
        /**
         * @brief Get queue size (approximate)
         * @return Number of messages in queue
         */
        UInt32 GetQueueSize() const noexcept;
        
        /**
         * @brief Set event hooks for monitoring
         * @param hooks Event hooks implementation
         */
        void SetEventHooks(std::shared_ptr<IPCEventHooks> hooks) noexcept
        {
            event_hooks_ = std::move(hooks);
        }
        
        /**
         * @brief Get event hooks
         * @return Event hooks pointer (may be null)
         */
        IPCEventHooks* GetEventHooks() const noexcept
        {
            return event_hooks_.get();
        }
        
    private:
        /**
         * @brief Private constructor
         */
        Subscriber(const String& service_name,
                  const SubscriberConfig& config,
                  std::unique_ptr<SharedMemoryManager> shm,
                  std::unique_ptr<ChunkPoolAllocator> allocator,
                  UInt32 queue_index,
                  UInt32 subscriber_id) noexcept
            : service_name_(service_name)
            , config_(config)
            , shm_(std::move(shm))
            , allocator_(std::move(allocator))
            , queue_index_(queue_index)
            , subscriber_id_(subscriber_id)
            , is_disconnected_(false)
        {
        }
        
        String service_name_;                                   ///< Service name
        SubscriberConfig config_;                               ///< Configuration
        std::unique_ptr<SharedMemoryManager> shm_;              ///< Shared memory manager
        std::unique_ptr<ChunkPoolAllocator> allocator_;         ///< Chunk allocator
        UInt32 queue_index_;                                    ///< Queue index in shared memory
        UInt32 subscriber_id_;                                  ///< Unique subscriber ID
        std::shared_ptr<IPCEventHooks> event_hooks_;            ///< Event hooks for monitoring
        bool is_disconnected_;                                  ///< Disconnected flag
    };
    
}  // namespace ipc
}  // namespace core
}  // namespace lap

#endif  // LAP_CORE_IPC_SUBSCRIBER_HPP
