/**
 * @file        Publisher.cpp
 * @author      LightAP Core Team
 * @brief       Implementation of Publisher
 * @date        2026-01-07
 * @copyright   Copyright (c) 2026
 */

#include "ipc/Publisher.hpp"
#include "ipc/Channel.hpp"
#include "ipc/WaitSetHelper.hpp"
#include "ipc/Message.hpp"
#include "ipc/ChannelRegistry.hpp"
#include "CCoreErrorDomain.hpp"
#include "CMacroDefine.hpp"
#include <cstring>
#include <chrono>
#include <thread>
#include <time.h>

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
        
        // Initialize write channels based on IPC type
        auto* ctrl = publisher.shm_->GetControlBlock();
        IPCType ipc_type = ctrl->header.type;

        while ( true ) {
            // Allocate unique queue index from shared memory control block
            auto publisher_id_result = ChannelRegistry::RegisterWriteChannel( ctrl, config.channel_id );
            if ( DEF_LAP_IF_UNLIKELY( !publisher_id_result ) ) {
                if ( publisher_id_result.Error() == CoreErrc::kIPCRetry ) {
                    // Retry allocation
                    std::this_thread::yield();
                    continue;
                } else {
                    return Result< Publisher >( MakeErrorCode( CoreErrc::kResourceExhausted ) );
                }
            } else {
                config_.channel_id = publisher_id_result.Value();
                INNER_CORE_LOG( "Publisher::Create - Allocated publisher ID: %u", config_.channel_id );
                break;
            }
        }
        
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
        Sample sample{ allocator_.get(), chunk_index };
        sample.TransitionState( ChunkState::kLoaned );
        
        return Result< Sample >( std::move(sample) );
    }
    
    Result<void> Publisher::Send( Sample&& sample, UInt8 channel_id,
                                        PublishPolicy policy ) noexcept
    {
        if ( DEF_LAP_IF_UNLIKELY( !sample.IsValid() ) ) {
            return Result< void >( MakeErrorCode( CoreErrc::kInvalidArgument ) );
        }

        DEF_LAP_ASSERT( sample.GetState() == ChunkState::kLoaned || sample.GetState() == ChunkState::kSent, 
                        "Sending chunk must be in Loaned or Sent state" );
        sample.TransitionState( ChunkState::kSent );
        sample.SetChannelID( channel_id );
        sample.IncrementRef();  // For publisher's own reference

        // Get snapshot of subscribers from shared memory registry
        auto& snapshot = write_channels_[ active_channel_index_.load( std::memory_order_acquire ) ];

        if ( snapshot.size() == 0 ) {
            //sample.DecrementRef();  // No subscribers, release chunk
            return {};
        }

        if ( channel_id != kInvalidChannelID ) {
            sample.FetchAdd( 1 );  // For specific subscriber

            // Send to specific channel only
            auto it = snapshot.find( channel_id );
            if ( it != snapshot.end() ) {
                auto& channel = it->second;

                if ( !channel || !channel->IsActive() || !channel->IsValid() ) {
                    // Queue not available, decrement ref count
                    sample.DecrementRef();
                    
                    return Result< void >( MakeErrorCode( CoreErrc::kChannelInvalid ) );
                }

                auto interval = std::chrono::milliseconds( channel->GetSTMin() );
                if ( ( now - last_send_[queue_index] ) < interval ) {
                    // Skip sending to this subscriber due to STmin
                    sample.DecrementRef();
                    
                    return Result< void >( MakeErrorCode( CoreErrc::kIPCRetry ) );
                }

                // Prepare value to write
                ChannelQueueValue value{ 0, sample.GetChunkIndex() };  // sequence can be used for message ordering if needed

                // Write using Channel API with policy
                auto write_result = channel->WriteWithPolicy( value, policy, config_.publish_timeout );
                
                if ( !write_result ) {
                    // Write failed, decrement ref count
                    sample.DecrementRef();

                    return Result< void >::FromError( write_result.Error() );
                }

                // Update last send time for STmin enforcement
                last_send_[channel_id] = SteadyClock::now();
            } else {
                // Channel ID not found, decrement ref count
                sample.DecrementRef();

                return Result< void >( MakeErrorCode( CoreErrc::kChannelNotFound ) );
            }
        } else {
            // Set initial reference count to number of subscribers
            sample.FetchAdd( snapshot.size() );
            
            auto now = SteadyClock::now();
            for ( auto& [channel_id, channel] : snapshot ) {
                if ( !channel || !channel->IsActive() || !channel->IsValid() ) {
                    // Queue not available, decrement ref count
                    sample.DecrementRef();
                    continue;
                }

                auto interval = std::chrono::milliseconds( channel->GetSTMin() );
                if ( ( now - last_send_[channel_id] ) < interval ) {
                    // Skip sending to this subscriber due to STmin
                    sample.DecrementRef();
                    continue;
                }

                // Prepare value to write
                ChannelQueueValue value{ 0, sample.GetChunkIndex() };  // sequence can be used for message ordering if needed

                // Write using Channel API with policy
                auto write_result = channel->WriteWithPolicy( value, policy, config_.publish_timeout );
                
                if ( !write_result ) {
                    // Write failed, decrement ref count
                    sample.DecrementRef();
                    continue;
                }

                // Update last send time for STmin enforcement
                last_send_[channel_id] = now;
            }
        }

        // sample.DecrementRef();  // Release publisher's own reference
        return {};       
    }

    Result< void > Publisher::Send( Byte* buffer, Size size, UInt8 channel_id,
                         PublishPolicy policy ) noexcept
    {
        auto sample_result = Loan();
        if ( !sample_result ) {
            return Result< void >( sample_result.Error() );
        }
        
        auto sample = std::move( sample_result ).Value();
   
        // Copy data to chunk
        sample.Write( buffer, size );
        
        return Send( std::move( sample ), channel_id, policy );
    }
        
    UInt32 Publisher::GetAllocatedCount() const noexcept
    {
        return allocator_->GetAllocatedCount();
    }
    
    Bool Publisher::IsChunkPoolExhausted() const noexcept
    {
        return allocator_->IsExhausted();
    }
    
    Publisher::Publisher( const String& shmPath,
                 const PublisherConfig& config,
                 UniqueHandle<SharedMemoryManager> shm,
                 UniqueHandle<ChunkPoolAllocator> allocator) noexcept
            : shm_path_(shmPath)
            , config_(config)
            , shm_(std::move(shm))
            , allocator_(std::move(allocator))
            , event_hooks_(nullptr)
            , write_channels_()
    {
        DEF_LAP_ASSERT( shm_ != nullptr, "SharedMemoryManager must not be null" );
        DEF_LAP_ASSERT( allocator_ != nullptr, "ChunkPoolAllocator must not be null" );

        // Initialize last_send_ to epoch (far past) to ensure first send is not skipped
        for ( UInt32 i = 0; i < kMaxChannels; ++i ) {
            last_send_[i] = SteadyClock::time_point{};
        }

        active_channel_index_.store( 0, std::memory_order_relaxed );
    
        auto ctrl = shm_->GetControlBlock();
        if ( ctrl->header.type == IPCType::kSPMC || 
             ctrl->header.type == IPCType::kMPMC ) {
            // For SPMC or MPMC, we need to track active subscribers
            is_running_ = true;
            scanner_thread_ = std::thread( &Publisher::InnerChannelScanner, this , 1000, 10000 ); // 10ms interval
        } else if ( ctrl->header.type == IPCType::kMPSC ) {
            // MPSC does not need to track subscribers
            is_running_ = false;
            write_channels_[0].emplace( config_.channel_id, 
                ChannelFactory<ChannelQueueValue>::CreateWriteChannelFromQueue( 
                    shm_->GetChannelQueue( config_.channel_id ) ) );
        } else {
            DEF_LAP_ASSERT( false, "Unsupported IPC type" );
        }
    }

    Publisher::~Publisher() noexcept
    {
        is_running_ = false;

        // Deregister from ChannelRegistry
        auto* ctrl = shm_->GetControlBlock();
        ChannelRegistry::DeregisterChannel( ctrl, config_.channel_id );

        // Clear write channels
        if ( scanner_thread_.joinable() ) {
            scanner_thread_.join();
        }   
        write_channels_[0].clear();
        write_channels_[1].clear();
    }

    void Publisher::InnerChannelScanner( UInt16 timeout_microseconds, UInt16 interval_microseconds ) noexcept
    {
        auto* ctrl = shm_->GetControlBlock();

        DEF_LAP_ASSERT( ctrl != nullptr, "ControlBlock must not be null" );

        auto& read_mask = ctrl->registry.read_mask;
        auto& read_seq = ctrl->registry.read_seq;

        struct timespec ts = {
            .tv_sec  = 0,
            .tv_nsec = ( timeout_microseconds == 0 ? 10'000'000 : timeout_microseconds * 1000 ),
        };

        UInt32 last = 0;
        while ( is_running_ ) {
            while ( read_seq.load( std::memory_order_acquire ) == last ) {
                futex_wait( &read_seq, last, &ts );
            }

            last = read_seq.load(std::memory_order_acquire);
            auto mask = read_mask.load(std::memory_order_acquire);
            UpdateWriteChannel( mask );

            std::this_thread::sleep_for( std::chrono::microseconds( interval_microseconds ) );
        }
    }

    void Publisher::UpdateWriteChannel( UInt64 read_mask ) noexcept
    {
        auto updateIndex = ( active_channel_index_.load(std::memory_order_acquire) + 1 ) % 2;
        auto& write_channel = write_channels_[ updateIndex ]; 

        write_channel.clear();
        if ( read_mask == 0 ) {
            // No active subscribers
            active_channel_index_.store( updateIndex, std::memory_order_release );
            return;
        }

        while ( read_mask ) {
            int idx = __builtin_ctzll( read_mask );  // Count trailing zeros
            
            // Get subscriber queue from shared memory
            auto* queue = shm_->GetChannelQueue( idx );
            if ( queue != nullptr ) {
                auto channel = ChannelFactory<ChannelQueueValue>::CreateWriteChannelFromQueue( queue );
                if ( channel ) {
                    write_channel.emplace( idx, std::move( channel ) );
                }
            }

            read_mask &= read_mask - 1;  // Clear lowest set bit
        }
        active_channel_index_.store( updateIndex, std::memory_order_release );
    }

    // Result< void > Publisher::InnerSend( Sample&& sample, UInt8 channel_id,
    //                                     PublishPolicy policy ) noexcept
    // {
    //     if ( channel_id >= kMaxChannels ) {
    //         return Result< void >( MakeErrorCode( CoreErrc::kIPCInvalidChannelIndex ) );
    //     }


    // }

}  // namespace ipc
}  // namespace core
}  // namespace lap
