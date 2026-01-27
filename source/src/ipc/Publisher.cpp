/**
 * @file        Publisher.cpp
 * @author      LightAP Core Team
 * @brief       Implementation of Publisher
 * @date        2026-01-07
 * @copyright   Copyright (c) 2026
 */

#include "ipc/Publisher.hpp"
#include "ipc/WaitSetHelper.hpp"
#include "ipc/Message.hpp"
#include "ipc/ChannelRegistry.hpp"
#include "CCoreErrorDomain.hpp"
#include "CMacroDefine.hpp"
#include <cstring>
#include <chrono>

namespace lap
{
namespace core
{
namespace ipc
{
    Result< Publisher > Publisher::Create(const String& shmPath,
                                              const PublisherConfig& config) noexcept
    {
        // Create shared memory manager
        auto shm = std::make_unique<SharedMemoryManager>();
        
        SharedMemoryConfig shm_config{};
        shm_config.max_chunks = config.max_chunks;
        shm_config.chunk_size = config.chunk_size;  // Payload size only
        
        auto result = shm->Create( shmPath, shm_config );
        if ( !result ) {
            return Result< Publisher >( result.Error() );
        }
        
        // Create chunk pool allocator
        auto allocator = std::make_unique< ChunkPoolAllocator >(
            shm->GetBaseAddress(),
            shm->GetControlBlock()
        );
        
        // Initialize (idempotent - will skip if already initialized)
        auto init_result = allocator->Initialize();
        if ( !init_result ) {
            return Result< Publisher >( init_result.Error() );
        }
        
        // Create publisher
        Publisher publisher( shmPath, config, std::move(shm), 
                              std::move(allocator) );
        
        return Result< Publisher >( std::move( publisher ) );
    }

    Result< Sample > Publisher::Loan() noexcept
    {
        // Try to allocate chunk
        auto chunk_index = allocator_->Allocate();

        if ( chunk_index == kInvalidChunkIndex ) {
            // Trigger hook: Loan failed
            if ( event_hooks_ ) {
                event_hooks_->OnLoanFailed(
                    config_.loan_policy,
                    allocator_->GetAllocatedCount(),
                    allocator_->GetMaxChunks()
                );
            }
            
            // Pool exhausted - handle based on policy
            if (config_.loan_policy == LoanPolicy::kError) {
                return Result< Sample >( MakeErrorCode( CoreErrc::kIPCChunkPoolExhausted ) );
            } else if ( config_.loan_policy == LoanPolicy::kWait ) {
                // Busy-wait polling for free chunk
                Duration timeout = Duration( config_.loan_timeout );  // 10ms default
                Bool success = WaitSetHelper::PollForFlags(
                    &shm_->GetControlBlock()->pool_state.remain_count,
                    0xFFFFFFFF,  // Any non-zero value
                    timeout
                );
                
                if ( !success ) {
                    return Result< Sample >( MakeErrorCode( CoreErrc::kIPCChunkPoolExhausted ) );
                }

                // Retry allocation
                chunk_index = allocator_->Allocate();
                if ( chunk_index == kInvalidChunkIndex ) {
                    return Result< Sample >( MakeErrorCode( CoreErrc::kIPCChunkPoolExhausted ) );
                }
            } else if ( config_.loan_policy == LoanPolicy::kBlock ) {
                // Block on futex until chunk available
                Duration timeout = Duration( config_.loan_timeout );  // 100ms default
                auto wait_result = WaitSetHelper::WaitForFlags(
                    &shm_->GetControlBlock()->pool_state.remain_count,
                    0xFFFFFFFF,  // Any non-zero value
                    timeout
                );
                
                if ( !wait_result ) {
                    return Result< Sample >( MakeErrorCode( CoreErrc::kIPCChunkPoolExhausted ) );
                }

                // Retry allocation
                chunk_index = allocator_->Allocate();
                if ( chunk_index == kInvalidChunkIndex ) {
                    return Result< Sample >( MakeErrorCode( CoreErrc::kIPCChunkPoolExhausted ) );
                }
            } else {
                return Result< Sample >( MakeErrorCode( CoreErrc::kIPCChunkPoolExhausted ) );
            }
        }
        
        return Result< Sample >( Sample{ allocator_.get(), chunk_index } );
    }
    
    Result<void> Publisher::Send( Sample&& sample,
                                        PublishPolicy /* policy */ ) noexcept
    {
        if ( DEF_LAP_IF_UNLIKELY( !sample.IsValid() ) ) {
            return Result< void >( MakeErrorCode( CoreErrc::kInvalidArgument ) );
        }

        UInt32 chunk_index = sample.GetChunkIndex();

        DEF_LAP_ASSERT( sample.GetState() == ChunkState::kLoaned, 
                        "Sending chunk must be in Loaned state" );
        sample.TransitionState( ChunkState::kLoaned, ChunkState::kSent );
        sample.IncrementRef();  // For publisher's own reference

        // Get snapshot of subscribers from shared memory registry
        auto snapshot = ChannelRegistry::GetChannelSnapshot( shm_->GetControlBlock() );
        auto snapshot_count = ChannelRegistry::GetChannelSnapshotCount( shm_->GetControlBlock() );

        if ( snapshot_count == 0 ) {
            //sample.DecrementRef();  // No subscribers, release chunk
            return {};
        }
       
        // Set initial reference count to number of subscribers
        sample.FetchAdd( snapshot_count );
        
        auto now = SteadyClock::now();
        for ( UInt32 i = 0; i < snapshot_count; ++i ) {
            UInt32 queue_index = snapshot[i];
            
            // Get subscriber queue from shared memory
            auto* queue = shm_->GetChannelQueue( queue_index );
            if ( !queue || !queue->IsActive() ) {
                // Queue not available, decrement ref count
                sample.DecrementRef();
                continue;
            }

            auto interval = std::chrono::milliseconds( queue->STmin );
            if ( ( now - last_send_[queue_index] ) < interval ) {
                // Skip sending to this subscriber due to STmin
                sample.DecrementRef();
                continue;
            }

            // SPSC enqueue (Publisher is producer)
            UInt16 tail = queue->tail.load(std::memory_order_relaxed);
            UInt16 next_tail = static_cast<UInt16>((tail + 1) & (queue->capacity - 1));
            
            // Check if queue is full
            UInt16 head = queue->head.load(std::memory_order_acquire);
            if ( next_tail == head ) {
                // Queue full - handle based on policy            
                if ( config_.policy == PublishPolicy::kOverwrite ) {
                    // Overwrite oldest message (move head forward)
                    UInt16 new_head = static_cast<UInt16>((head + 1) & (queue->capacity - 1));
                    queue->head.store( new_head, std::memory_order_release );
                    // Now we have space, continue to enqueue
                } else if ( config_.policy == PublishPolicy::kBlock ) {
                    // Block on futex until space available
                    Duration timeout = Duration( config_.publish_timeout );  // 100ms default
                    auto wait_result = WaitSetHelper::WaitForFlags(
                        &queue->queue_waitset,
                        EventFlag::kHasSpace,
                        timeout
                    );
                    
                    if ( !wait_result ) {
                        // Timeout or error
                        sample.DecrementRef();
                        continue;
                    }
                    
                    // Recheck head/tail after wake
                    head = queue->head.load( std::memory_order_acquire );
                    tail = queue->tail.load( std::memory_order_relaxed );
                    next_tail = static_cast<UInt16>((tail + 1) & (queue->capacity - 1));
                    
                    if ( next_tail == head ) {
                        // Still full, drop message
                        sample.DecrementRef();
                        continue;
                    }
                } else if ( config_.policy == PublishPolicy::kWait ) {
                    // Busy-wait polling for space
                    Duration timeout = Duration(10000000);  // 10ms default
                    Bool success = WaitSetHelper::PollForFlags(
                        &queue->queue_waitset,
                        EventFlag::kHasSpace,
                        timeout
                    );
                    
                    if ( !success ) {
                        // Timeout
                        sample.DecrementRef();
                        continue;
                    }
                    
                    // Recheck head/tail after poll
                    head = queue->head.load( std::memory_order_acquire );
                    tail = queue->tail.load( std::memory_order_relaxed );
                    next_tail = static_cast<UInt16>((tail + 1) & (queue->capacity - 1));
                    
                    if ( next_tail == head ) {
                        // Still full, drop message
                        sample.DecrementRef();
                        continue;
                    }
                } else if ( config_.policy == PublishPolicy::kDrop ) {
                    // Drop message
                    sample.DecrementRef();
                    
                    // Trigger hook: Queue full
                    if ( event_hooks_ ) {
                        event_hooks_->OnQueueFull( queue_index, config_.policy );
                    }
                    
                    continue;
                } else if ( config_.policy == PublishPolicy::kError ) {
                    // error policy
                    sample.DecrementRef();
                    continue;
                } else {
                    // Unknown policy, drop message
                    sample.DecrementRef();
                    continue;
                }
            }

            // Update last send time for STmin enforcement
            last_send_[queue_index] = now;
            
            // Write chunk_index to buffer
            auto buffer = queue->GetBuffer();
            buffer[tail].chunk_index = static_cast<UInt16>(chunk_index);
            
            // Update tail (release semantics for consumer visibility)
            queue->tail.store( next_tail, std::memory_order_release );

            // Set event flag and wake waiting subscriber
            WaitSetHelper::SetFlagsAndWake( &queue->queue_waitset, EventFlag::kHasData, true );
        }

        // sample.DecrementRef();  // Release publisher's own reference
        return {};
    }

    Result< void > Publisher::Send( Byte* buffer, Size size,
                         PublishPolicy policy ) noexcept
    {
        auto sample_result = Loan();
        if ( !sample_result ) {
            return Result< void >( sample_result.Error() );
        }
        
        auto sample = std::move( sample_result ).Value();
        
        // Copy data to chunk
        sample.Write( buffer, size );
        
        return Send( std::move( sample ), policy );
    }
        
    UInt32 Publisher::GetAllocatedCount() const noexcept
    {
        return allocator_->GetAllocatedCount();
    }
    
    Bool Publisher::IsChunkPoolExhausted() const noexcept
    {
        return allocator_->IsExhausted();
    }
    
    // Remove Message instantiation - use Publisher<UInt8> with SendMessage
    
}  // namespace ipc
}  // namespace core
}  // namespace lap
