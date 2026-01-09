/**
 * @file        StatisticsHooks.hpp
 * @author      LightAP Core Team
 * @brief       Statistics tracking implementation of IPC event hooks
 * @date        2026-01-07
 * @details     Collects and tracks IPC performance statistics
 * @copyright   Copyright (c) 2026
 */

#include "../inc/ipc/IPCEventHooks.hpp"
#include <atomic>
#include <iostream>
#include <iomanip>

namespace lap
{
namespace core
{
namespace ipc
{
    /**
     * @brief Statistics tracking hook implementation
     * @details Collects counters for all IPC events
     */
    class StatisticsHooks : public IPCEventHooks
    {
    public:
        StatisticsHooks() noexcept
        {
            Reset();
        }
        
        // Publisher events
        void OnLoanFailed(const char*, LoanFailurePolicy, UInt32, UInt32) noexcept override
        {
            loan_failures_.fetch_add(1, std::memory_order_relaxed);
        }
        
        void OnChunkPoolExhausted(const char*, UInt32) noexcept override
        {
            pool_exhaustions_.fetch_add(1, std::memory_order_relaxed);
        }
        
        void OnQueueFull(const char*, UInt32, QueueFullPolicy) noexcept override
        {
            queue_full_events_.fetch_add(1, std::memory_order_relaxed);
        }
        
        void OnMessageSent(const char*, UInt32, UInt32 subscriber_count) noexcept override
        {
            messages_sent_.fetch_add(1, std::memory_order_relaxed);
            total_deliveries_.fetch_add(subscriber_count, std::memory_order_relaxed);
        }
        
        void OnLoanCountWarning(const char*, UInt32, UInt32) noexcept override
        {
            loan_warnings_.fetch_add(1, std::memory_order_relaxed);
        }
        
        // Subscriber events
        void OnReceiveTimeout(const char*, UInt64) noexcept override
        {
            receive_timeouts_.fetch_add(1, std::memory_order_relaxed);
        }
        
        void OnQueueOverrun(const char*, UInt32, UInt32 dropped) noexcept override
        {
            queue_overruns_.fetch_add(1, std::memory_order_relaxed);
            messages_dropped_.fetch_add(dropped, std::memory_order_relaxed);
        }
        
        void OnMessageReceived(const char*, UInt32) noexcept override
        {
            messages_received_.fetch_add(1, std::memory_order_relaxed);
        }
        
        // Shared memory events
        void OnSharedMemoryCreated(const char*, UInt64) noexcept override
        {
            shm_creates_.fetch_add(1, std::memory_order_relaxed);
        }
        
        void OnSharedMemoryOpened(const char*, UInt64) noexcept override
        {
            shm_opens_.fetch_add(1, std::memory_order_relaxed);
        }
        
        void OnSharedMemoryError(const char*, int, const char*) noexcept override
        {
            shm_errors_.fetch_add(1, std::memory_order_relaxed);
        }
        
        /**
         * @brief Reset all counters
         */
        void Reset() noexcept
        {
            loan_failures_.store(0);
            pool_exhaustions_.store(0);
            queue_full_events_.store(0);
            messages_sent_.store(0);
            total_deliveries_.store(0);
            loan_warnings_.store(0);
            
            receive_timeouts_.store(0);
            queue_overruns_.store(0);
            messages_dropped_.store(0);
            messages_received_.store(0);
            
            shm_creates_.store(0);
            shm_opens_.store(0);
            shm_errors_.store(0);
        }
        
        /**
         * @brief Print statistics summary
         */
        void PrintSummary() const noexcept
        {
            std::cout << "\n========== IPC Statistics Summary ==========" << std::endl;
            
            std::cout << "\n[Publisher]" << std::endl;
            std::cout << "  Messages sent:       " << messages_sent_.load() << std::endl;
            std::cout << "  Total deliveries:    " << total_deliveries_.load() << std::endl;
            std::cout << "  Loan failures:       " << loan_failures_.load() << std::endl;
            std::cout << "  Pool exhaustions:    " << pool_exhaustions_.load() << std::endl;
            std::cout << "  Queue full events:   " << queue_full_events_.load() << std::endl;
            std::cout << "  Loan warnings:       " << loan_warnings_.load() << std::endl;
            
            std::cout << "\n[Subscriber]" << std::endl;
            std::cout << "  Messages received:   " << messages_received_.load() << std::endl;
            std::cout << "  Receive timeouts:    " << receive_timeouts_.load() << std::endl;
            std::cout << "  Queue overruns:      " << queue_overruns_.load() << std::endl;
            std::cout << "  Messages dropped:    " << messages_dropped_.load() << std::endl;
            
            std::cout << "\n[Shared Memory]" << std::endl;
            std::cout << "  Creates:             " << shm_creates_.load() << std::endl;
            std::cout << "  Opens:               " << shm_opens_.load() << std::endl;
            std::cout << "  Errors:              " << shm_errors_.load() << std::endl;
            
            // Calculate statistics
            UInt64 sent = messages_sent_.load();
            UInt64 received = messages_received_.load();
            
            if (sent > 0)
            {
                double success_rate = (received * 100.0) / sent;
                std::cout << "\n[Performance]" << std::endl;
                std::cout << "  Success rate:        " << std::fixed << std::setprecision(2) 
                          << success_rate << "%" << std::endl;
                
                UInt64 deliveries = total_deliveries_.load();
                if (deliveries > 0)
                {
                    double avg_subs = static_cast<double>(deliveries) / sent;
                    std::cout << "  Avg subscribers:     " << std::fixed << std::setprecision(2)
                              << avg_subs << std::endl;
                }
            }
            
            std::cout << "============================================\n" << std::endl;
        }
        
        // Getters
        UInt64 GetMessagesSent() const noexcept { return messages_sent_.load(); }
        UInt64 GetMessagesReceived() const noexcept { return messages_received_.load(); }
        UInt64 GetLoanFailures() const noexcept { return loan_failures_.load(); }
        UInt64 GetQueueOverruns() const noexcept { return queue_overruns_.load(); }
        UInt64 GetMessagesDropped() const noexcept { return messages_dropped_.load(); }
        
    private:
        // Publisher counters
        std::atomic<UInt64> loan_failures_{0};
        std::atomic<UInt64> pool_exhaustions_{0};
        std::atomic<UInt64> queue_full_events_{0};
        std::atomic<UInt64> messages_sent_{0};
        std::atomic<UInt64> total_deliveries_{0};
        std::atomic<UInt64> loan_warnings_{0};
        
        // Subscriber counters
        std::atomic<UInt64> receive_timeouts_{0};
        std::atomic<UInt64> queue_overruns_{0};
        std::atomic<UInt64> messages_dropped_{0};
        std::atomic<UInt64> messages_received_{0};
        
        // Shared memory counters
        std::atomic<UInt64> shm_creates_{0};
        std::atomic<UInt64> shm_opens_{0};
        std::atomic<UInt64> shm_errors_{0};
    };
    
}  // namespace ipc
}  // namespace core
}  // namespace lap
