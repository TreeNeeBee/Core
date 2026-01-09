/**
 * @file        IPCEventHooks.hpp
 * @author      LightAP Core Team
 * @brief       Event hooks for IPC monitoring and debugging
 * @date        2026-01-07
 * @details     Provides callback interface for IPC events
 * @copyright   Copyright (c) 2026
 * @note        Based on iceoryx2 design - extensible event system
 */
#ifndef LAP_CORE_IPC_EVENT_HOOKS_HPP
#define LAP_CORE_IPC_EVENT_HOOKS_HPP

#include "IPCTypes.hpp"
#include "CString.hpp"

namespace lap
{
namespace core
{
namespace ipc
{
    /**
     * @brief Interface for IPC event callbacks
     * @details
     * - Override methods to receive notifications
     * - Used for logging, statistics, alerting
     * - Minimal performance impact (virtual calls only on events)
     * 
     * Usage:
     * ```cpp
     * class MyHooks : public IPCEventHooks {
     *     void OnLoanFailed(const char* topic, ...) override {
     *         LOG_WARN("Loan failed for {}", topic);
     *     }
     * };
     * 
     * auto hooks = std::make_shared<MyHooks>();
     * publisher.SetEventHooks(hooks);
     * ```
     */
    class IPCEventHooks
    {
    public:
        virtual ~IPCEventHooks() = default;
        
        // ====================================================================
        // Publisher Events
        // ====================================================================
        
        /**
         * @brief Called when Publisher::Loan() fails
         * @param topic Service/topic name
         * @param policy Loan failure policy used
         * @param allocated_count Current allocated chunk count
         * @param max_chunks Maximum chunks in pool
         */
        virtual void OnLoanFailed(const char* /* topic */,
                                 LoanFailurePolicy /* policy */,
                                 UInt32 /* allocated_count */,
                                 UInt32 /* max_chunks */) noexcept
        {
            // Default: no-op
        }
        
        /**
         * @brief Called when ChunkPool is exhausted
         * @param topic Service/topic name
         * @param total_chunks Total number of chunks
         */
        virtual void OnChunkPoolExhausted(const char* /* topic */,
                                         UInt32 /* total_chunks */) noexcept
        {
            // Default: no-op
        }
        
        /**
         * @brief Called when queue full prevents message delivery
         * @param topic Service/topic name
         * @param subscriber_id Subscriber ID
         * @param policy Queue full policy
         */
        virtual void OnQueueFull(const char* /* topic */,
                                UInt32 /* subscriber_id */,
                                QueueFullPolicy /* policy */) noexcept
        {
            // Default: no-op
        }
        
        /**
         * @brief Called when message is successfully sent
         * @param topic Service/topic name
         * @param chunk_index Chunk index
         * @param subscriber_count Number of subscribers
         */
        virtual void OnMessageSent(const char* /* topic */,
                                  UInt32 /* chunk_index */,
                                  UInt32 /* subscriber_count */) noexcept
        {
            // Default: no-op
        }
        
        /**
         * @brief Called when loan count exceeds threshold
         * @param topic Service/topic name
         * @param current_count Current loaned chunks
         * @param threshold Warning threshold
         */
        virtual void OnLoanCountWarning(const char* /* topic */,
                                       UInt32 /* current_count */,
                                       UInt32 /* threshold */) noexcept
        {
            // Default: no-op
        }
        
        // ====================================================================
        // Subscriber Events
        // ====================================================================
        
        /**
         * @brief Called when Subscriber::Receive() times out
         * @param topic Service/topic name
         * @param timeout Timeout duration (nanoseconds)
         */
        virtual void OnReceiveTimeout(const char* /* topic */,
                                     UInt64 /* timeout_ns */) noexcept
        {
            // Default: no-op
        }
        
        /**
         * @brief Called when queue overrun occurs (messages dropped)
         * @param topic Service/topic name
         * @param subscriber_id Subscriber ID
         * @param dropped_count Number of messages dropped
         */
        virtual void OnQueueOverrun(const char* /* topic */,
                                   UInt32 /* subscriber_id */,
                                   UInt32 /* dropped_count */) noexcept
        {
            // Default: no-op
        }
        
        /**
         * @brief Called when message is successfully received
         * @param topic Service/topic name
         * @param chunk_index Chunk index
         */
        virtual void OnMessageReceived(const char* /* topic */,
                                      UInt32 /* chunk_index */) noexcept
        {
            // Default: no-op
        }
        
        // ====================================================================
        // Shared Memory Events
        // ====================================================================
        
        /**
         * @brief Called when shared memory is created
         * @param path Shared memory path
         * @param size Total size in bytes
         */
        virtual void OnSharedMemoryCreated(const char* /* path */,
                                          UInt64 /* size */) noexcept
        {
            // Default: no-op
        }
        
        /**
         * @brief Called when shared memory is opened
         * @param path Shared memory path
         * @param size Total size in bytes
         */
        virtual void OnSharedMemoryOpened(const char* /* path */,
                                         UInt64 /* size */) noexcept
        {
            // Default: no-op
        }
        
        /**
         * @brief Called when shared memory operation fails
         * @param path Shared memory path
         * @param error_code Error code
         * @param error_msg Error message
         */
        virtual void OnSharedMemoryError(const char* /* path */,
                                        int /* error_code */,
                                        const char* /* error_msg */) noexcept
        {
            // Default: no-op
        }
        
        // ====================================================================
        // ChunkPool Events
        // ====================================================================
        
        /**
         * @brief Called when ChunkPool is initialized
         * @param topic Service/topic name
         * @param max_chunks Maximum chunks
         * @param chunk_size Size per chunk
         */
        virtual void OnChunkPoolInitialized(const char* /* topic */,
                                           UInt32 /* max_chunks */,
                                           UInt64 /* chunk_size */) noexcept
        {
            // Default: no-op
        }
        
        /**
         * @brief Called periodically with pool statistics
         * @param topic Service/topic name
         * @param allocated Currently allocated
         * @param max_chunks Maximum chunks
         * @param utilization Utilization percentage (0-100)
         */
        virtual void OnChunkPoolStats(const char* /* topic */,
                                     UInt32 /* allocated */,
                                     UInt32 /* max_chunks */,
                                     float /* utilization */) noexcept
        {
            // Default: no-op
        }
    };
    
    /**
     * @brief Null hook implementation (default)
     */
    class NullEventHooks : public IPCEventHooks
    {
        // All methods use default no-op implementation
    };
    
}  // namespace ipc
}  // namespace core
}  // namespace lap

#endif  // LAP_CORE_IPC_EVENT_HOOKS_HPP
