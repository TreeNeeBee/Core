/**
 * @file        Publisher.hpp
 * @author      LightAP Core Team
 * @brief       Zero-copy publisher implementation
 * @date        2026-01-07
 * @details     Loan-based publish API with lock-free message distribution
 * @copyright   Copyright (c) 2026
 * @note        Based on iceoryx2 design
 */
#ifndef LAP_CORE_IPC_PUBLISHER_HPP
#define LAP_CORE_IPC_PUBLISHER_HPP

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

namespace lap
{
namespace core
{
namespace ipc
{
    /**
     * @brief Publisher configuration
     */
    struct PublisherConfig
    {
        UInt32 max_chunks = kDefaultMaxChunks;      ///< Maximum chunks in pool
        UInt64 chunk_size = 0;                      ///< Chunk size (payload), 0 means default
        LoanFailurePolicy loan_policy = LoanFailurePolicy::kError;  ///< Loan failure policy
        bool auto_cleanup = false;                   ///< Auto cleanup shared memory
    };
    
    /**
     * @brief Zero-copy publisher
     * @tparam T Message type (must be Message or derived from Message)
     * 
     * @details
     * Usage:
     * 1. Create publisher with service name
     * 2. Loan() to get writable chunk
     * 3. Write data to chunk
     * 4. Send() to publish to all subscribers
     * 
     * @note T must be Message or a class derived from Message
     */
    template <typename T>
    class Publisher
    {
    public:
        /**
         * @brief Create publisher
         * @param service_name Service name
         * @param config Configuration
         * @return Result with publisher or error
         */
        static Result<Publisher<T>> Create(const String& service_name,
                                          const PublisherConfig& config = {}) noexcept;
        
        /**
         * @brief Destructor
         */
        ~Publisher() noexcept = default;
        
        // Delete copy
        Publisher(const Publisher&) = delete;
        Publisher& operator=(const Publisher&) = delete;
        
        // Allow move
        Publisher(Publisher&&) noexcept = default;
        Publisher& operator=(Publisher&&) noexcept = default;
        
        /**
         * @brief Loan a chunk for writing
         * @return Result with Sample or error
         * 
         * @details
         * - Allocates chunk from pool
         * - Returns RAII Sample wrapper
         * - Behavior on pool exhaustion depends on loan_policy
         */
        Result<Sample<T>> Loan() noexcept;
        
        /**
         * @brief Send sample to all subscribers
         * @param sample Sample to send (moved)
         * @param policy Queue full policy
         * @return Result
         * 
         * @details
         * - Transitions chunk state to kSent
         * - Enqueues chunk index to all subscriber queues
         * - Increments ref count for each subscriber
         */
        Result<void> Send(Sample<T>&& sample, 
                         QueueFullPolicy policy = QueueFullPolicy::kDrop) noexcept;
        
        /**
         * @brief Convenience: Loan, copy data, and send
         * @param data Data to copy
         * @param policy Queue full policy
         * @return Result
         */
        Result<void> SendCopy(const T& data,
                             QueueFullPolicy policy = QueueFullPolicy::kDrop) noexcept;
        
        /**
         * @brief Convenience: Loan, construct in-place, and send
         * @tparam Args Constructor argument types
         * @param policy Queue full policy
         * @param args Constructor arguments
         * @return Result
         */
        template <typename... Args>
        Result<void> SendEmplace(QueueFullPolicy policy, Args&&... args) noexcept;
        
        /**
         * @brief Get service name
         * @return Service name
         */
        const String& GetServiceName() const noexcept
        {
            return service_name_;
        }
        
        /**
         * @brief Get allocated chunk count
         * @return Number of allocated chunks
         */
        UInt32 GetAllocatedCount() const noexcept;
        
        /**
         * @brief Check if chunk pool is exhausted
         * @return true if exhausted
         */
        bool IsChunkPoolExhausted() const noexcept;
        
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
        Publisher(const String& service_name,
                 const PublisherConfig& config,
                 std::unique_ptr<SharedMemoryManager> shm,
                 std::unique_ptr<ChunkPoolAllocator> allocator) noexcept
            : service_name_(service_name)
            , config_(config)
            , shm_(std::move(shm))
            , allocator_(std::move(allocator))
        {
        }
        
        String service_name_;                              ///< Service name
        PublisherConfig config_;                           ///< Configuration
        std::unique_ptr<SharedMemoryManager> shm_;         ///< Shared memory manager
        std::unique_ptr<ChunkPoolAllocator> allocator_;    ///< Chunk allocator
        std::shared_ptr<IPCEventHooks> event_hooks_;       ///< Event hooks for monitoring
    };
    
}  // namespace ipc
}  // namespace core
}  // namespace lap

#endif  // LAP_CORE_IPC_PUBLISHER_HPP
