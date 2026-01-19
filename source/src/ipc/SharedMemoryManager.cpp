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

namespace lap
{
namespace core
{
namespace ipc
{
    Result<void> SharedMemoryManager::Create(const String& shmPath,
                                             const SharedMemoryConfig& config) noexcept
    {
        shm_path_ = shmPath;
        config_ = config;
        
        // Try to create new shared memory (O_CREAT | O_EXCL)
        shm_fd_ = shm_open( shm_path_.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666 );
        
        if ( shm_fd_ >= 0 ) {
            // We are the creator (ref_count will determine cleanup)
            
            // Calculate and align total size
            size_ = CalculateTotalSize( config );

            // Set size
            if ( ftruncate( shm_fd_, static_cast< off_t >( size_ ) ) != 0 ) {
                close(shm_fd_);
                shm_fd_ = -1;
                return Result< void >( MakeErrorCode( CoreErrc::kIPCShmCreateFailed ) );
            }
            
            // Map memory
            base_addr_ = mmap( nullptr, size_, PROT_READ | PROT_WRITE,
                            MAP_SHARED, shm_fd_, 0 );
            
            if ( base_addr_ == MAP_FAILED ) {
                close( shm_fd_ );
                shm_fd_ = -1;
                base_addr_ = nullptr;
                return Result< void >( MakeErrorCode( CoreErrc::kIPCShmMapFailed ) );
            }
            
            // Initialize structures
            auto result = InitializeSharedMemory( config );
            if ( !result ) {
                Cleanup();
                return result;
            }
            
            return {};
        } else if ( errno == EEXIST ) {
            // Shared memory already exists, open it
            
            shm_fd_ = shm_open(shm_path_.c_str(), O_RDWR, 0666);
            if ( shm_fd_ < 0 ) {
                return Result< void >( MakeErrorCode( CoreErrc::kIPCShmNotFound ) );
            }
            
            // Get existing size
            struct stat sb;
            if ( fstat( shm_fd_, &sb ) != 0 ) {
                close( shm_fd_ );
                shm_fd_ = -1;
                return Result< void >( MakeErrorCode( CoreErrc::kIPCShmStatFailed ) );
            }
            
            size_ = static_cast< UInt64 >( sb.st_size );
            
            // Map memory
            base_addr_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE,
                            MAP_SHARED, shm_fd_, 0);
            
            if ( base_addr_ == MAP_FAILED ) {
                close( shm_fd_ );
                shm_fd_ = -1;
                base_addr_ = nullptr;
                return Result< void >( MakeErrorCode( CoreErrc::kIPCShmMapFailed ) );
            }
            
            // Validate control block
            auto* ctrl = GetControlBlock();
            if ( !ctrl || !ctrl->Validate() ) {
                Cleanup();
                return Result< void >( MakeErrorCode( CoreErrc::kIPCShmInvalidMagic ) );
            }

            ctrl->header.ref_count.fetch_add( 1, std::memory_order_release );
            
            return {};
        } else {
            // Other error
            return Result< void >( MakeErrorCode( CoreErrc::kIPCShmCreateFailed ) );
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
        UInt64 chunkpool_size = DEF_LAP_ALIGN_FORMAT( sizeof( ChunkHeader ) + config.chunk_size, kCacheLineSize ) * config.max_chunks;
        total_size += chunkpool_size;
        
        // Round up to 4K/2MB boundary for better alignment
        total_size = DEF_LAP_ALIGN_FORMAT( total_size, kShmAlignment );
        
        return total_size;
    }
    
    Result<void> SharedMemoryManager::InitializeSharedMemory(const SharedMemoryConfig& config) noexcept
    {
        // Zero out entire shared memory
        std::memset(base_addr_, 0, size_);

        auto* ctrl = GetControlBlock();
        ctrl->Initialize(config.max_chunks,
                        config.max_subscriber_queues > 0 ? config.max_subscriber_queues : kMaxSubscribers,
                        config.chunk_size,
                        config.queue_capacity > 0 ? config.queue_capacity : kQueueCapacity);
        
        // Initialize all SubscriberQueue slots in Region 2 (0x100000-0x1FFFFF)
        // Each queue is 8KB, allowing up to 128 queues in 1MB
        UInt32 max_queues = ctrl->header.max_subscribers;
        UInt32 queue_capacity = ctrl->header.queue_capacity;
        
        for ( UInt32 i = 0; i < max_queues; ++i ) {
            auto* queue = GetSubscriberQueue(i);
            if ( queue ) {
                // Initialize queue structure (sets active=false)
                queue->Initialize( queue_capacity );
                
                // Zero-initialize buffer (inline after queue struct)
                UInt32* buffer = queue->GetBuffer();
                for ( UInt32 j = 0; j < queue_capacity; ++j ) {
                    buffer[j] = kInvalidChunkIndex;
                }
            }
        }
        
        INNER_CORE_LOG("[DEBUG] Shared memory initialization complete\n");
        INNER_CORE_LOG("  - Mode: %s\n", GetIPCModeName());
        INNER_CORE_LOG("  - Max subscribers: %u\n", static_cast<UInt32>(kMaxSubscribers));
        INNER_CORE_LOG("  - Queue capacity: %u per subscriber\n", static_cast<UInt32>(kQueueCapacity));
        INNER_CORE_LOG("  - ControlBlock size: %zu bytes\n", sizeof(ControlBlock));
        INNER_CORE_LOG("  - Total queues initialized: %u\n", config.max_subscriber_queues);
        
        // ChunkPool initialization will be done in ChunkPoolAllocator
        ctrl->header.ref_count.store( 1, std::memory_order_release );
        
        return {};
    }
    
    void SharedMemoryManager::Cleanup() noexcept
    {
        Bool should_unlink = false;

        // Check if we should unlink the shared memory
        if ( base_addr_ != nullptr && base_addr_ != MAP_FAILED ) {
            auto* ctrl = GetControlBlock();
            if ( ctrl && ctrl->Validate() && ( ctrl->header.ref_count.fetch_sub( 1, std::memory_order_acquire ) == 1 ) ) {
                // Simplified: always unlink (no reference counting)
                // TODO: Implement proper reference counting if multi-process support needed
                should_unlink = true;
            }
            
            munmap(base_addr_, size_);
            base_addr_ = nullptr;
        }
        
        if ( shm_fd_ >= 0 ) {
            close( shm_fd_ );
            shm_fd_ = -1;
        }
        
        // Unlink only if we're the last process
        if ( should_unlink && !shm_path_.empty() ) {
            shm_unlink(shm_path_.c_str());
        }
    }
    
}  // namespace ipc
}  // namespace core
}  // namespace lap
