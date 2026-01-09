/**
 * @file        SharedMemoryManager.cpp
 * @author      LightAP Core Team
 * @brief       Implementation of SharedMemoryManager
 * @date        2026-01-07
 * @copyright   Copyright (c) 2026
 */

#include "ipc/SharedMemoryManager.hpp"
#include "ipc/ChunkPoolAllocator.hpp"
#include "CCoreErrorDomain.hpp"
#include <cstring>
#include <cerrno>
#include <iostream>

namespace lap
{
namespace core
{
namespace ipc
{
    Result<void> SharedMemoryManager::Create(const String& service_name,
                                             const SharedMemoryConfig& config) noexcept
    {
        service_name_ = service_name;
        config_ = config;
        
        String shm_path = GetShmPath();
        
        // Try to create new shared memory (O_CREAT | O_EXCL)
        shm_fd_ = shm_open(shm_path.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666);
        
        if (shm_fd_ >= 0)
        {
            // We are the creator
            is_creator_ = true;
            
            // Calculate and align total size
            size_ = CalculateTotalSize(config);
            
            // Set size
            if (ftruncate(shm_fd_, static_cast<off_t>(size_)) != 0)
            {
                close(shm_fd_);
                shm_fd_ = -1;
                return Result<void>(MakeErrorCode(CoreErrc::kIPCShmCreateFailed));
            }
            
            // Map memory
            base_addr_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE,
                            MAP_SHARED, shm_fd_, 0);
            
            if (base_addr_ == MAP_FAILED)
            {
                close(shm_fd_);
                shm_fd_ = -1;
                base_addr_ = nullptr;
                return Result<void>(MakeErrorCode(CoreErrc::kIPCShmMapFailed));
            }
            
            // Initialize structures
            auto result = InitializeSharedMemory(config);
            if (!result)
            {
                Cleanup();
                return result;
            }
            
            return {};
        }
        else if (errno == EEXIST)
        {
            // Shared memory already exists, open it
            is_creator_ = false;
            
            shm_fd_ = shm_open(shm_path.c_str(), O_RDWR, 0666);
            if (shm_fd_ < 0)
            {
                return Result<void>(MakeErrorCode(CoreErrc::kIPCShmNotFound));
            }
            
            // Get existing size
            struct stat sb;
            if (fstat(shm_fd_, &sb) != 0)
            {
                close(shm_fd_);
                shm_fd_ = -1;
                return Result<void>(MakeErrorCode(CoreErrc::kIPCShmStatFailed));
            }
            
            size_ = static_cast<UInt64>(sb.st_size);
            
            // Map memory
            base_addr_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE,
                            MAP_SHARED, shm_fd_, 0);
            
            if (base_addr_ == MAP_FAILED)
            {
                close(shm_fd_);
                shm_fd_ = -1;
                base_addr_ = nullptr;
                return Result<void>(MakeErrorCode(CoreErrc::kIPCShmMapFailed));
            }
            
            // Validate control block
            auto* ctrl = GetControlBlock();
            if (!ctrl || !ctrl->Validate())
            {
                Cleanup();
                return Result<void>(MakeErrorCode(CoreErrc::kIPCShmInvalidMagic));
            }
            
            return {};
        }
        else
        {
            // Other error
            return Result<void>(MakeErrorCode(CoreErrc::kIPCShmCreateFailed));
        }
    }
    
    UInt64 SharedMemoryManager::CalculateTotalSize(const SharedMemoryConfig& config) const noexcept
    {
        // Fixed partition layout (optimized):
        // Region 1: ControlBlock (128KB = 0x20000)
        // Region 2: SubscriberQueue[100] (800KB = 0xC8000)
        // Region 2.5: Reserved (96KB = 0x18000)
        // Region 3: ChunkPool (starts at 1MB = 0x100000, dynamic size)
        
        UInt64 total_size = kChunkPoolOffset;  // 1MB (includes ControlBlock + Queues + Reserved)
        
        // ChunkPool region
        UInt64 chunk_total_size = sizeof(ChunkHeader) + config.chunk_size;
        UInt64 aligned_chunk_size = ((chunk_total_size + kCacheLineSize - 1) / kCacheLineSize) * kCacheLineSize;
        UInt64 chunkpool_size = aligned_chunk_size * config.max_chunks;
        
        total_size += chunkpool_size;
        
        // Round up to 2MB boundary for better alignment
        UInt64 alignment = 2 * 1024 * 1024;  // 2MB
        total_size = ((total_size + alignment - 1) / alignment) * alignment;
        
        return total_size;
    }
    
    Result<void> SharedMemoryManager::InitializeSharedMemory(const SharedMemoryConfig& config) noexcept
    {
        // Zero out entire shared memory
        std::memset(base_addr_, 0, size_);
        
        // Initialize ControlBlock in Region 1 (0x000000-0x0FFFFF)
        auto* ctrl = GetControlBlock();
        ctrl->Initialize(config.max_chunks,
                        config.chunk_size,
                        config.max_subscriber_queues > 0 ? config.max_subscriber_queues : kMaxSubscribers,
                        config.queue_capacity > 0 ? config.queue_capacity : kDefaultQueueCapacity);
        
        // Initialize all SubscriberQueue slots in Region 2 (0x100000-0x1FFFFF)
        // Each queue is 8KB, allowing up to 128 queues in 1MB
        UInt32 max_queues = ctrl->max_subscriber_queues;
        UInt32 queue_capacity = ctrl->queue_capacity;
        
        INNER_CORE_LOG("[DEBUG] Initializing %u queues, capacity=%u\n", max_queues, queue_capacity);
        
        for (UInt32 i = 0; i < max_queues; ++i)
        {
            auto* queue = GetSubscriberQueue(i);
            if (queue)
            {
                // Initialize queue structure (sets active=false)
                queue->Initialize(queue_capacity);
                
                // Zero-initialize buffer (inline after queue struct)
                UInt32* buffer = queue->GetBuffer();
                for (UInt32 j = 0; j < queue_capacity; ++j)
                {
                    buffer[j] = kInvalidChunkIndex;
                }
            }
        }
        
        INNER_CORE_LOG("[DEBUG] Shared memory initialization complete\n");
        INNER_CORE_LOG("  - ControlBlock: 0x000000-0x01FFFF (128KB)\n");
        INNER_CORE_LOG("  - Queue region: 0x020000-0x0E7FFF (800KB, 100 queues × 8KB)\n");
        INNER_CORE_LOG("  - Reserved:     0x0E8000-0x0FFFFF (96KB)\n");
        INNER_CORE_LOG("  - ChunkPool:    0x100000+ (dynamic)\n");
        
        // ChunkPool initialization will be done in ChunkPoolAllocator
        
        return {};
    }
    
    void SharedMemoryManager::Cleanup() noexcept
    {
        if (base_addr_ != nullptr && base_addr_ != MAP_FAILED)
        {
            munmap(base_addr_, size_);
            base_addr_ = nullptr;
        }
        
        if (shm_fd_ >= 0)
        {
            close(shm_fd_);
            shm_fd_ = -1;
        }
        
        if (is_creator_ && config_.auto_cleanup && !service_name_.empty())
        {
            String shm_path = GetShmPath();
            shm_unlink(shm_path.c_str());
        }
    }
    
}  // namespace ipc
}  // namespace core
}  // namespace lap
