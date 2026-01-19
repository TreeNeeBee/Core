/**
 * @file        SharedMemoryManager.hpp
 * @author      LightAP Core Team
 * @brief       POSIX shared memory management
 * @date        2026-01-07
 * @details     Manages shared memory creation, mapping, and lifecycle
 * @copyright   Copyright (c) 2026
 * @note        Based on iceoryx2 design
 */
#ifndef LAP_CORE_IPC_SHARED_MEMORY_MANAGER_HPP
#define LAP_CORE_IPC_SHARED_MEMORY_MANAGER_HPP

#include "IPCTypes.hpp"
#include "IPCConfig.hpp"
#include "ControlBlock.hpp"
#include "CResult.hpp"
#include "CString.hpp"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace lap
{
namespace core
{
namespace ipc
{
    /**
     * @brief Configuration for shared memory segment
     */
    struct SharedMemoryConfig
    {
        UInt32 max_chunks = kDefaultMaxChunks;              ///< Maximum chunks
        UInt64 chunk_size = kDefaultChunkSize;              ///< Chunk size
        UInt32 max_subscriber_queues = kMaxSubscribers;     ///< Max queues
        UInt32 queue_capacity = kQueueCapacity;      ///< Queue capacity
    };
    
    /**
     * @brief Shared memory segment manager
     * @details Handles POSIX shared memory lifecycle
     * 
     * Usage Pattern:
     * 1. First process: Create() creates new segment
     * 2. Subsequent processes: Create() opens existing segment
     * 3. Last process: Destructor optionally cleans up
     */
    class SharedMemoryManager
    {
    public:
        /**
         * @brief Constructor
         */
        SharedMemoryManager() noexcept
            : shm_fd_(-1)
            , base_addr_(nullptr)
            , size_(0)
        {
        }
        
        /**
         * @brief Destructor - unmaps memory and optionally unlinks
         */
        ~SharedMemoryManager() noexcept
        {
            Cleanup();
        }
        
        // Delete copy/move
        SharedMemoryManager(const SharedMemoryManager&) = delete;
        SharedMemoryManager& operator=(const SharedMemoryManager&) = delete;
        SharedMemoryManager(SharedMemoryManager&&) = delete;
        SharedMemoryManager& operator=(SharedMemoryManager&&) = delete;
        
        /**
         * @brief Create or open shared memory segment
         * @param shmPath Shared memory path (e.g., "/lightap_ipc_sensor_data")
         * @param config Configuration
         * @return Result with success or error
         * 
         * @details
         * - First caller: Creates and initializes shared memory
         * - Subsequent callers: Opens existing shared memory
         * - Path format: /lightap_ipc_<service_name>
         */
        Result<void> Create(const String& shmPath, 
                           const SharedMemoryConfig& config) noexcept;
        
        /**
         * @brief Get base address of shared memory
         * @return Base address pointer
         */
        void* GetBaseAddress() const noexcept { return base_addr_; }
        
        /**
         * @brief Get control block
         * @return Control block pointer
         */
        ControlBlock* GetControlBlock() const noexcept
        {
            return static_cast<ControlBlock*>(base_addr_);
        }
        
        /**
         * @brief Get total size of shared memory
         * @return Size in bytes
         */
        UInt64 GetSize() const noexcept { return size_; }

        /**
         * @brief Get shared memory path
         * @return Shared memory path
         */
        inline String GetShmPath() const noexcept
        {
            return shm_path_;
        }
        
        /**
         * @brief Get subscriber queue by index
         * @param queue_index Queue index (0 to max_queues-1)
         * @return Pointer to SubscriberQueue in shared memory
         * 
         * @details Memory layout (optimized fixed partitions):
         * - Region 1: ControlBlock @ 0x000000-0x01FFFF (128KB)
         * - Region 2: Queues @ 0x020000-0x0E7FFF (800KB, 100 × 8KB)
         * - Region 2.5: Reserved @ 0x0E8000-0x0FFFFF (96KB)
         * - Region 3: ChunkPool @ 0x100000+ (dynamic)
         */
        SubscriberQueue* GetSubscriberQueue(UInt32 queue_index) const noexcept
        {
            if (!base_addr_) return nullptr;
            
            auto* ctrl = GetControlBlock();
            if (queue_index >= ctrl->header.max_subscribers) return nullptr;
            
            // Use fixed partition layout: Queue region starts at 128KB offset
            UInt8* addr = reinterpret_cast<UInt8*>(base_addr_);
            addr += kQueueRegionOffset;  // Skip to queue region (128KB)
            addr += kSubscriberQueueSize * queue_index;  // Each queue is 8KB
            
            return reinterpret_cast<SubscriberQueue*>(addr);
        }
        
    private:
        /**
         * @brief Calculate total size needed for shared memory
         * @param config Configuration
         * @return Total size (aligned to 2MB)
         */
        UInt64 CalculateTotalSize(const SharedMemoryConfig& config) const noexcept;
        
        /**
         * @brief Initialize shared memory structures
         * @param config Configuration
         * @return Result
         */
        Result<void> InitializeSharedMemory(const SharedMemoryConfig& config) noexcept;
        
        /**
         * @brief Cleanup resources
         */
        void Cleanup() noexcept;
        
        int shm_fd_;                ///< Shared memory file descriptor
        void* base_addr_;           ///< Base address of mapped memory
        UInt64 size_;               ///< Total size of shared memory
        String shm_path_;           ///< Shared memory path
        SharedMemoryConfig config_; ///< Configuration
    };
    
}  // namespace ipc
}  // namespace core
}  // namespace lap

#endif  // LAP_CORE_IPC_SHARED_MEMORY_MANAGER_HPP
