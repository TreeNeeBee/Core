/**
 * @file        Subscriber.cpp
 * @author      LightAP Core Team
 * @brief       Implementation of Subscriber
 * @date        2026-01-07
 * @copyright   Copyright (c) 2026
 */

#include "ipc/Subscriber.hpp"
#include "ipc/Message.hpp"
#include "ipc/ChannelRegistry.hpp"
#include "ipc/WaitSetHelper.hpp"
#include "CCoreErrorDomain.hpp"
#include "CMacroDefine.hpp"
#include <thread>
#include <chrono>
#include <functional>

namespace lap
{
namespace core
{
namespace ipc
{
    Result< Subscriber > Subscriber::Create(const String& shmPath,
                                                const SubscriberConfig& config) noexcept
    {
        // Create shared memory manager
        auto shm = std::make_unique<SharedMemoryManager>();
        
        SharedMemoryConfig shm_config{};
        shm_config.max_chunks = config.max_chunks;
        shm_config.chunk_size = config.chunk_size;

        auto result = shm->Create( shmPath, shm_config );
        if ( !result ) {
            return Result< Subscriber >( result.Error() );
        }
        
        // Create chunk pool allocator
        auto allocator = std::make_unique< ChunkPoolAllocator >(
            shm->GetBaseAddress(),
            shm->GetControlBlock()
        );
        
        // Initialize (idempotent - will skip if already initialized)
        auto init_result = allocator->Initialize();
        if ( !init_result ) {
            return Result< Subscriber >( init_result.Error() );
        }
        
        // Allocate unique queue index from shared memory control block
        // Loop to handle race conditions
        auto* ctrl = shm->GetControlBlock();
        UInt32 queue_index = kInvalidChunkIndex;
        
        while ( true ) {
            // Allocate unique queue index from shared memory control block
            auto subscriber_id_result = ChannelRegistry::RegisterChannel( ctrl );
            if ( DEF_LAP_IF_UNLIKELY( !subscriber_id_result ) ) {
                if ( subscriber_id_result.Error() == CoreErrc::kIPCRetry ) {
                    // Retry allocation
                    std::this_thread::yield();
                    continue;
                } else {
                    return Result< Subscriber >( MakeErrorCode( CoreErrc::kResourceExhausted ) );
                }
            } else {
                queue_index = subscriber_id_result.Value();
                
                // Get the allocated queue from shared memory
                ChannelQueue* queue = shm->GetChannelQueue( queue_index );
                if ( !queue ) {
                    return Result< Subscriber >( MakeErrorCode( CoreErrc::kIPCShmNotFound ) );
                }
                queue->STmin.store( config.STmin, std::memory_order_release );

                break;
            }
        }   
        // Create subscriber
        Subscriber subscriber( shmPath, config, std::move( shm ), 
                                std::move(allocator), queue_index );
        
        return Result< Subscriber >( std::move( subscriber ) );
    }
    
    Subscriber::~Subscriber() noexcept
    {
        // Automatically disconnect on destruction
        (void)Disconnect();
    }

    Result< Sample > Subscriber::Receive( SubscribePolicy policy ) noexcept
    {
        // Get queue from shared memory
        auto* queue = shm_->GetChannelQueue(subscriber_id_);
        if ( !queue ) {
            return Result< Sample >( MakeErrorCode( CoreErrc::kIPCShmNotFound ) );
        }
        
        // Try to dequeue chunk index (SPSC consumer side)
        UInt16 head = queue->head.load(std::memory_order_relaxed);
        UInt16 tail = queue->tail.load(std::memory_order_acquire);
        
        if ( head == tail ) {
            // Queue empty - handle based on policy
            if ( DEF_LAP_IF_LIKELY( policy == SubscribePolicy::kBlock ) ) {
                // Block waiting for data using WaitSet
                Duration timeout = Duration( config_.timeout );  // 100ms default
                
                // Use futex-based wait for efficient blocking
                auto wait_result = WaitSetHelper::WaitForFlags(
                    &queue->queue_waitset,
                    EventFlag::kHasData,
                    timeout
                );
                
                if ( !wait_result ) {
                    // Timeout or error
                    return Result< Sample >( MakeErrorCode( CoreErrc::kIPCQueueEmpty ) );
                }
                
                // Recheck queue after wake
                head = queue->head.load( std::memory_order_relaxed );
                tail = queue->tail.load( std::memory_order_acquire );
                
                if ( head == tail ) {
                    // Spurious wakeup, return error
                    return Result< Sample >( MakeErrorCode( CoreErrc::kIPCQueueEmpty ) );
                }
            } else if ( policy == SubscribePolicy::kWait ) {
                // Busy-wait polling for data using WaitSet
                Duration timeout = Duration( config_.timeout );  // 10ms default
                
                // Use WaitSet poll for efficient busy-waiting
                Bool has_data = WaitSetHelper::PollForFlags(
                    &queue->queue_waitset,
                    EventFlag::kHasData,
                    timeout
                );
                
                if ( !has_data ) {
                    // Timeout
                    return Result< Sample >( MakeErrorCode( CoreErrc::kIPCQueueEmpty ) );
                }
                
                // Recheck queue after poll success
                head = queue->head.load( std::memory_order_relaxed );
                tail = queue->tail.load( std::memory_order_acquire );
                
                if ( head == tail ) {
                    // Data was consumed by another thread
                    return Result< Sample >( MakeErrorCode( CoreErrc::kIPCQueueEmpty ) );
                }
            } else if ( policy == SubscribePolicy::kSkip ) {
                return Result< Sample >( MakeErrorCode( CoreErrc::kWouldBlock ) );
            } else if ( policy == SubscribePolicy::kError ) {
                return Result< Sample >( MakeErrorCode( CoreErrc::kIPCQueueEmpty ) );
            } else {
                // Unknown policy
                return Result< Sample >( MakeErrorCode( CoreErrc::kInvalidArgument ) );
            }
        }
        
        // Dequeue chunk_index
        auto buffer = queue->GetBuffer();
        UInt16 chunk_index = buffer[head].chunk_index;
        
        // Chunk index read from queue
        
        // Update head (release semantics for producer visibility)
        UInt16 next_head = static_cast<UInt16>((head + 1) & (queue->capacity - 1));
        queue->head.store( next_head, std::memory_order_release );

        // Create Sample wrapper
        Sample sample( allocator_.get(), chunk_index );
        if ( !sample.IsValid() ) {
            return Result< Sample >( MakeErrorCode( CoreErrc::kIPCInvalidChunkIndex ) );
        }

        if ( sample.GetState() != ChunkState::kSent &&
             sample.GetState() != ChunkState::kReceived ) {
            return Result< Sample >( MakeErrorCode( CoreErrc::kIPCInvalidState ) );
        }

        sample.TransitionState( ChunkState::kReceived );

        // // Call message received callback
        // sample.onMessageReceived();
        
        return Result< Sample >( std::move( sample ) );
    }

    Result< Size > Subscriber::Receive( Byte* buffer, Size size,
                         SubscribePolicy policy ) noexcept
    {
        auto sample_result = Receive( policy );
        if ( !sample_result ) {
            return Result< Size >::FromError( sample_result.Error() );
        }
        
        auto sample = std::move( sample_result ).Value();
        
        Size copySize = size < sample.RawDataSize() ? size : sample.RawDataSize();
        
        std::memcpy( buffer, sample.RawData(), copySize );
        
        return Result< Size >::FromValue( copySize );
    }
        
    Bool Subscriber::IsQueueEmpty() const noexcept
    {
        auto* queue = shm_->GetChannelQueue( subscriber_id_ );
        DEF_LAP_ASSERT( queue != nullptr, "Subscriber queue must not be nullptr" );
        
        UInt32 head = queue->head.load( std::memory_order_acquire );
        UInt32 tail = queue->tail.load( std::memory_order_acquire );
        return head == tail;
    }
    
    UInt32 Subscriber::GetQueueSize() const noexcept
    {
        auto* queue = shm_->GetChannelQueue( subscriber_id_ );
        DEF_LAP_ASSERT( queue != nullptr, "Subscriber queue must not be nullptr" );
        
        UInt32 head = queue->head.load( std::memory_order_acquire );
        UInt32 tail = queue->tail.load( std::memory_order_acquire );
        
        if ( tail >= head ) {
            return tail - head;
        } else {
            return queue->capacity - head + tail;
        }
    }

    void Subscriber::UpdateSTMin( UInt8 stmin ) noexcept
    {
        config_.STmin = stmin;

        ChannelQueue* queue = shm_->GetChannelQueue( subscriber_id_ );
        if ( queue ) {
            queue->STmin.store( stmin, std::memory_order_release );
        }
    }

    Result<void> Subscriber::Connect() noexcept
    {
        if ( !shm_ || !shm_->GetControlBlock()->Validate() ) {
            return Result< void >( MakeErrorCode( CoreErrc::kIPCShmNotFound ) );
        }

        ChannelRegistry::ActiveChannel( shm_.get(), subscriber_id_ );

        return {};
    }

    Result<void> Subscriber::Disconnect() noexcept
    {
        if ( !shm_ || !shm_->GetControlBlock()->Validate() ) {
            return {};  // Already disconnected or not initialized
        }

        // ====== Step 1: Deactivate channel ======
        ChannelRegistry::DeactiveChannel( shm_.get(), subscriber_id_ );
        
        // ====== Step 2: Unregister from ChannelRegistry ======
        ChannelRegistry::UnregisterChannel( shm_->GetControlBlock(), subscriber_id_ );
        
        // ====== Step 3: Consume remaining messages in queue ======
        // This ensures ref_count is properly decremented
        auto* queue = shm_->GetChannelQueue( subscriber_id_ );
        
        if ( queue ) {
            while ( !IsQueueEmpty() ) {
                auto sample_result = Receive( SubscribePolicy::kError );
                if ( sample_result.HasValue() ) {
                    // Sample destructor will decrement ref_count automatically
                } else {
                    break;  // Queue empty or error
                }
            }
        }
        return {};
    }

}  // namespace ipc
}  // namespace core
}  // namespace lap
