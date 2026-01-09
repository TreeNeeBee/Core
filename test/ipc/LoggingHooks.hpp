/**
 * @file        LoggingHooks.hpp
 * @author      LightAP Core Team
 * @brief       Logging implementation of IPC event hooks
 * @date        2026-01-07
 * @details     Logs all IPC events to console/file
 * @copyright   Copyright (c) 2026
 */

#include "../inc/ipc/IPCEventHooks.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <sstream>

namespace lap
{
namespace core
{
namespace ipc
{
    /**
     * @brief Logging hook implementation
     * @details Logs all IPC events with timestamps to console
     */
    class LoggingHooks : public IPCEventHooks
    {
    public:
        LoggingHooks(bool verbose = false) noexcept
            : verbose_(verbose)
        {
        }
        
        void OnLoanFailed(const char* topic,
                         LoanFailurePolicy policy,
                         UInt32 allocated_count,
                         UInt32 max_chunks) noexcept override
        {
            std::cout << GetTimestamp() 
                      << " [WARN] Loan failed for '" << topic << "'"
                      << " | Policy: " << PolicyToString(policy)
                      << " | Allocated: " << allocated_count << "/" << max_chunks
                      << std::endl;
        }
        
        void OnChunkPoolExhausted(const char* topic,
                                 UInt32 total_chunks) noexcept override
        {
            std::cout << GetTimestamp()
                      << " [ERROR] ChunkPool exhausted for '" << topic << "'"
                      << " | Total chunks: " << total_chunks
                      << std::endl;
        }
        
        void OnQueueFull(const char* topic,
                        UInt32 subscriber_id,
                        QueueFullPolicy policy) noexcept override
        {
            std::cout << GetTimestamp()
                      << " [WARN] Queue full for '" << topic << "'"
                      << " | Subscriber: " << subscriber_id
                      << " | Policy: " << QueuePolicyToString(policy)
                      << std::endl;
        }
        
        void OnMessageSent(const char* topic,
                          UInt32 chunk_index,
                          UInt32 subscriber_count) noexcept override
        {
            if (verbose_)
            {
                std::cout << GetTimestamp()
                          << " [INFO] Message sent for '" << topic << "'"
                          << " | Chunk: " << chunk_index
                          << " | Subscribers: " << subscriber_count
                          << std::endl;
            }
        }
        
        void OnLoanCountWarning(const char* topic,
                               UInt32 current_count,
                               UInt32 threshold) noexcept override
        {
            std::cout << GetTimestamp()
                      << " [WARN] Loan count warning for '" << topic << "'"
                      << " | Current: " << current_count
                      << " | Threshold: " << threshold
                      << std::endl;
        }
        
        void OnReceiveTimeout(const char* topic,
                             UInt64 timeout_ns) noexcept override
        {
            std::cout << GetTimestamp()
                      << " [WARN] Receive timeout for '" << topic << "'"
                      << " | Timeout: " << (timeout_ns / 1000000) << "ms"
                      << std::endl;
        }
        
        void OnQueueOverrun(const char* topic,
                           UInt32 subscriber_id,
                           UInt32 dropped_count) noexcept override
        {
            std::cout << GetTimestamp()
                      << " [ERROR] Queue overrun for '" << topic << "'"
                      << " | Subscriber: " << subscriber_id
                      << " | Dropped: " << dropped_count
                      << std::endl;
        }
        
        void OnMessageReceived(const char* topic,
                              UInt32 chunk_index) noexcept override
        {
            if (verbose_)
            {
                std::cout << GetTimestamp()
                          << " [INFO] Message received for '" << topic << "'"
                          << " | Chunk: " << chunk_index
                          << std::endl;
            }
        }
        
        void OnSharedMemoryCreated(const char* path,
                                  UInt64 size) noexcept override
        {
            std::cout << GetTimestamp()
                      << " [INFO] Shared memory created: " << path
                      << " | Size: " << (size / (1024*1024)) << "MB"
                      << std::endl;
        }
        
        void OnSharedMemoryOpened(const char* path,
                                 UInt64 size) noexcept override
        {
            std::cout << GetTimestamp()
                      << " [INFO] Shared memory opened: " << path
                      << " | Size: " << (size / (1024*1024)) << "MB"
                      << std::endl;
        }
        
        void OnSharedMemoryError(const char* path,
                                int error_code,
                                const char* error_msg) noexcept override
        {
            std::cout << GetTimestamp()
                      << " [ERROR] Shared memory error: " << path
                      << " | Code: " << error_code
                      << " | Message: " << error_msg
                      << std::endl;
        }
        
        void OnChunkPoolInitialized(const char* topic,
                                   UInt32 max_chunks,
                                   UInt64 chunk_size) noexcept override
        {
            std::cout << GetTimestamp()
                      << " [INFO] ChunkPool initialized for '" << topic << "'"
                      << " | Max chunks: " << max_chunks
                      << " | Chunk size: " << chunk_size << " bytes"
                      << std::endl;
        }
        
        void OnChunkPoolStats(const char* topic,
                             UInt32 allocated,
                             UInt32 max_chunks,
                             float utilization) noexcept override
        {
            if (verbose_)
            {
                std::cout << GetTimestamp()
                          << " [INFO] ChunkPool stats for '" << topic << "'"
                          << " | Allocated: " << allocated << "/" << max_chunks
                          << " | Utilization: " << std::fixed << std::setprecision(1) 
                          << utilization << "%"
                          << std::endl;
            }
        }
        
    private:
        std::string GetTimestamp() const noexcept
        {
            auto now = std::chrono::system_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()
            ) % 1000;
            
            auto timer = std::chrono::system_clock::to_time_t(now);
            std::tm bt = *std::localtime(&timer);
            
            std::ostringstream oss;
            oss << std::put_time(&bt, "%H:%M:%S");
            oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
            
            return oss.str();
        }
        
        const char* PolicyToString(LoanFailurePolicy policy) const noexcept
        {
            switch (policy)
            {
                case LoanFailurePolicy::kError: return "Error";
                case LoanFailurePolicy::kWait:  return "Wait";
                case LoanFailurePolicy::kBlock: return "Block";
                default: return "Unknown";
            }
        }
        
        const char* QueuePolicyToString(QueueFullPolicy policy) const noexcept
        {
            switch (policy)
            {
                case QueueFullPolicy::kOverwrite: return "Overwrite";
                case QueueFullPolicy::kWait:      return "Wait";
                case QueueFullPolicy::kBlock:     return "Block";
                case QueueFullPolicy::kDrop:      return "Drop";
                default: return "Unknown";
            }
        }
        
        bool verbose_;
    };
    
}  // namespace ipc
}  // namespace core
}  // namespace lap
