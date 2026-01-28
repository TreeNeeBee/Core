/**
 * @file        Subscriber.cpp
 * @author      LightAP Core Team
 * @brief       Implementation of Subscriber
 * @date        2026-01-07
 * @copyright   Copyright (c) 2026
 */

#include "ipc/Subscriber.hpp"
#include "ipc/Channel.hpp"
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
            auto subscriber_id_result = ChannelRegistry::RegisterReadChannel( ctrl, config.channel_id );
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
                                std::move(allocator) );
        
        return Result< Subscriber >( std::move( subscriber ) );
    }

    Result< Vector< Sample > > Subscriber::Receive( UInt8 channel_id, SubscribePolicy policy ) noexcept
    {
        if ( channel_id != kInvalidChannelID ) {
            auto result = InnerReceive( channel_id, policy );

            if ( !result ) {
                return Result< Vector< Sample > >::FromError( result.Error() );
            }

            return Result< Vector< Sample > >( Vector< Sample >( result.Value() ) );
        }

        Vector< Sample > samples;
        for ( const auto& [_, read_channel] : read_channels_[ active_channel_index_.load( std::memory_order_acquire ) ] ) {
            auto read_result = read_channel->ReadWithPolicy( policy, config_.timeout );
            if ( !read_result )  {
                // skip
                continue;
            }
            // Extract chunk_index from ChannelQueueValue
            UInt16 chunk_index = read_result.Value().chunk_index;

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

            samples.emplace_back( std::move( sample ) );
        }

        return Result< Vector< Sample > >( std::move( samples ) );
    }

    // Result< Size > Subscriber::Receive( Byte* buffer, Size size,
    //                      SubscribePolicy policy ) noexcept
    // {
    //     auto sample_result = Receive( policy );
    //     if ( !sample_result ) {
    //         return Result< Size >::FromError( sample_result.Error() );
    //     }
        
    //     auto sample = std::move( sample_result ).Value();
        
    //     Size copySize = size < sample.RawDataSize() ? size : sample.RawDataSize();
        
    //     std::memcpy( buffer, sample.RawData(), copySize );
        
    //     return Result< Size >::FromValue( copySize );
    // }
        
    Bool Subscriber::IsQueueEmpty() const noexcept
    {
        auto* queue = shm_->GetChannelQueue( config_.channel_id );
        DEF_LAP_ASSERT( queue != nullptr, "Subscriber queue must not be nullptr" );
        
        UInt32 head = queue->head.load( std::memory_order_acquire );
        UInt32 tail = queue->tail.load( std::memory_order_acquire );
        return head == tail;
    }
    
    UInt32 Subscriber::GetQueueSize() const noexcept
    {
        auto* queue = shm_->GetChannelQueue( config_.channel_id );
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

        ChannelQueue* queue = shm_->GetChannelQueue( config_.channel_id );
        if ( queue ) {
            queue->STmin.store( stmin, std::memory_order_release );
        }
    }

    Result<void> Subscriber::Connect() noexcept
    {
        if ( !shm_ || !shm_->GetControlBlock()->Validate() ) {
            return Result< void >( MakeErrorCode( CoreErrc::kIPCShmNotFound ) );
        }

        ChannelRegistry::ActiveChannel( shm_.get(), config_.channel_id );

        return {};
    }

    Result<void> Subscriber::Disconnect() noexcept
    {
        if ( !shm_ || !shm_->GetControlBlock()->Validate() ) {
            return {};  // Already disconnected or not initialized
        }

        // ====== Step 1: Deactivate channel ======
        ChannelRegistry::DeactiveChannel( shm_.get(), config_.channel_id );

        return {};
    }

    Subscriber( const String& shmPath,
                  const SubscriberConfig& config,
                  UniqueHandle<SharedMemoryManager> shm,
                  UniqueHandle<ChunkPoolAllocator> allocator) noexcept
            : shm_path_(shmPath)
            , config_(config)
            , shm_(std::move(shm))
            , allocator_(std::move(allocator))
            , read_channel_(nullptr)
    {
        DEF_LAP_ASSERT( shm_ != nullptr, "SharedMemoryManager must not be null" );
        DEF_LAP_ASSERT( allocator_ != nullptr, "ChunkPoolAllocator must not be null" );

        active_channel_index_.store( 0, std::memory_order_relaxed );

        auto ctrl = shm_->GetControlBlock();
        if ( ctrl->header.type == IPCType::kMPSC ) {
            // Start channel scanner thread for MPSC
            is_running_ = true;
            scanner_thread_ = std::thread( &Subscriber::InnerChannelScanner, this , 1000, 10000 ); // 10ms interval
        } else if ( ctrl->header.type == IPCType::kMPSC || ctrl->header.type == IPCType::kMPMC ) {
            // No scanner needed for MPSC or MPMC, just create one read channel for each subscriber
            is_running_ = false;
            read_channels_[0].emplace( config_.channel_id,
                ChannelFactory<ChannelQueueValue>::CreateReadChannelFromQueue(
                    shm_->GetChannelQueue( config_.channel_id ) ) );
        } else {
            DEF_LAP_ASSERT( false, "Unsupported IPC type" );
        }
    }
     
    Subscriber::~Subscriber() noexcept
    {
        is_running_ = false;

        // Step 1: Disconnect from service
        Disconnect();

        // ====== Step 2: Unregister from ChannelRegistry ======
        ChannelRegistry::UnregisterChannel( shm_->GetControlBlock(), config_.channel_id );
        
        // ====== Step 3: Consume remaining messages in queue ======
        // This ensures ref_count is properly decremented
        auto* queue = shm_->GetChannelQueue( config_.channel_id );
        
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

        if ( scanner_thread_.joinable() ) {
            scanner_thread_.join();
        }
        read_channels_[0].clear();
        read_channels_[1].clear();
    }

    /**
    * @brief Internal channel scanner thread
    * @details Periodically scans for active publishers and updates read channels
    */
    void Subscriber::InnerChannelScanner( UInt16 timeout_microseconds, UInt16 interval_microseconds ) noexcept
    {
        auto* ctrl = shm_->GetControlBlock();

        DEF_LAP_ASSERT( ctrl != nullptr, "ControlBlock must not be null" );

        auto& write_mask = ctrl->registry.write_mask;
        auto& write_seq = ctrl->registry.write_seq;

        struct timespec ts = {
            .tv_sec  = 0,
            .tv_nsec = ( timeout_microseconds == 0 ? 10'000'000 : timeout_microseconds * 1000 ),
        };

        // For SPMC or MPSC, we only need to track active publishers
        // Do not use in MPMC
        UInt32 last = 0;
        while ( is_running_ ) {
            while ( write_seq.load( std::memory_order_acquire ) == last ) {
                futex_wait( &write_seq, last, &ts );
            }

            last = write_seq.load(std::memory_order_acquire);
            auto mask = write_mask.load(std::memory_order_acquire);
            UpdateReadChannel( mask );

            std::this_thread::sleep_for( std::chrono::microseconds( interval_microseconds ) );
        }
    }

    /* @brief Update read channels based on active publishers
    * @details Called periodically to refresh the list of active channels
    * - Scans ChannelRegistry for active publishers
    * - Updates internal read_channels_ vector accordingly
    */
    void Subscriber::UpdateReadChannel( UInt64 write_mask ) noexcept
    {
        auto updateIndex = ( active_channel_index_.load(std::memory_order_acquire) + 1 ) % 2;
        auto& read_channel = read_channels_[ updateIndex ]; 

        read_channel.clear();
        if ( write_mask == 0 ) {
            // No active subscribers
            active_channel_index_.store( updateIndex, std::memory_order_release );
            return;
        }

        while ( write_mask ) {
            int idx = __builtin_ctzll( write_mask );  // Count trailing zeros
            
            // Get subscriber queue from shared memory
            auto* queue = shm_->GetChannelQueue( idx );
            if ( queue != nullptr ) {
                auto channel = ChannelFactory<ChannelQueueValue>::CreateReadChannelFromQueue( queue );
                if ( channel ) {
                    read_channel.emplace( idx, std::move( channel ) );
                }
            }

            write_mask &= write_mask - 1;  // Clear lowest set bit
        }
        active_channel_index_.store( updateIndex, std::memory_order_release );
    }

    Result< Sample > InnerReceive( UInt8 channel_id, SubscribePolicy policy ) noexcept
    {
        if ( channel_id >= kMaxChannels ) {
            return Result< Sample >( MakeErrorCode( CoreErrc::kIPCInvalidChannelIndex ) );
        }

        auto it = read_channels_[ active_channel_index_.load( std::memory_order_acquire ) ].find( channel_id );
        if ( it == read_channels_[ active_channel_index_.load( std::memory_order_acquire ) ].end() ) {
            return Result< Sample >( MakeErrorCode( CoreErrc::kChannelInvalid ) );
        }

        auto read_result = it->second->ReadWithPolicy( policy, config_.timeout );
        if (!read_result) {
            return Result< Sample >( read_result.Error() );
        }
        // Extract chunk_index from ChannelQueueValue
        UInt16 chunk_index = read_result.Value().chunk_index;

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

        return Result< Sample >::FromValue( std::move( sample ) );
    }
}  // namespace ipc
}  // namespace core
}  // namespace lap
