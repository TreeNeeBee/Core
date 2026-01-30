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
        shm_config.ipc_type   = config.ipc_type;

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

        auto config_ = config;        
        while ( true ) {
            // Allocate unique queue index from shared memory control block
            auto subscriber_id_result = ChannelRegistry::RegisterReadChannel( shm->GetControlBlock(), config.channel_id );
            if ( DEF_LAP_IF_UNLIKELY( !subscriber_id_result ) ) {
                if ( subscriber_id_result.Error() == CoreErrc::kIPCRetry ) {
                    // Retry allocation
                    std::this_thread::yield();
                    continue;
                } else {
                    return Result< Subscriber >( MakeErrorCode( CoreErrc::kResourceExhausted ) );
                }
            } else {
                config_.channel_id = subscriber_id_result.Value();
                
                // Get the allocated queue from shared memory
                ChannelQueue* queue = shm->GetChannelQueue( config_.channel_id );
                if ( !queue ) {
                    return Result< Subscriber >( MakeErrorCode( CoreErrc::kIPCShmNotFound ) );
                }
                queue->STmin.store( config_.STmin, std::memory_order_release );

                break;
            }
        }
        // Create subscriber
        Subscriber subscriber( shmPath, config_, std::move( shm ), 
                                std::move(allocator) );
        
        return Result< Subscriber >( std::move( subscriber ) );
    }

    Result< Vector< Sample > > Subscriber::Receive( UInt8 channel_id, SubscribePolicy policy ) noexcept
    {
        Vector< Sample > samples;
        if ( channel_id != kInvalidChannelID ) {
            auto result = InnerReceive( channel_id, policy );

            if ( !result ) {
                return Result< Vector< Sample > >::FromError( result.Error() );
            }

            samples.emplace_back( std::move( result ).Value() ) ;
            return Result< Vector< Sample > >( std::move(samples) );
        }

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
                return Result< Vector< Sample > >( MakeErrorCode( CoreErrc::kIPCInvalidChunkIndex ) );
            }

            if ( sample.GetState() != ChunkState::kSent &&
                sample.GetState() != ChunkState::kReceived ) {
                return Result< Vector< Sample > >( MakeErrorCode( CoreErrc::kIPCInvalidState ) );
            }
            sample.TransitionState( ChunkState::kReceived );

            samples.emplace_back( std::move( sample ) );
        }

        return Result< Vector< Sample > >( std::move( samples ) );
    }

    template<class Fn>
    Result< UInt8 > Subscriber::Receive( Fn&& read_fn, UInt8 channel_id,
                        SubscribePolicy policy ) noexcept
    {
        DEF_LAP_STATIC_ASSERT( ( std::is_invocable_r_v< Size, Fn, UInt8, Byte*, Size > ),
                    "Fn must be callable like Size( UInt8, Byte*, Size)" );
        
        if ( channel_id == kInvalidChannelID ) {
            auto sample_result = InnerReceive( channel_id, policy );
            if ( !sample_result ) {
                return Result< UInt8 >::FromError( sample_result.Error() );
            }
            auto sample = std::move( sample_result ).Value();
            Size readSize = read_fn( sample.ChannelID(), sample.RawData(), sample.RawDataSize() );
            if ( readSize > sample.RawDataSize() ) {
                return Result< UInt8 >( MakeErrorCode( CoreErrc::kIPCReadOverflow ) );
            } else {
                return Result< UInt8 >::FromValue( 1 );
            }
        } else {
            UInt8 totalRead = 0;
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
                    return Result< UInt8 >( MakeErrorCode( CoreErrc::kIPCInvalidChunkIndex ) );
                }

                if ( sample.GetState() != ChunkState::kSent &&
                    sample.GetState() != ChunkState::kReceived ) {
                    return Result< UInt8 >( MakeErrorCode( CoreErrc::kIPCInvalidState ) );
                }
                sample.TransitionState( ChunkState::kReceived );

                Size readSize = read_fn( sample.ChannelID(), sample.RawData(), sample.RawDataSize() );
                if ( readSize > sample.RawDataSize() ) {
                    // return Result< UInt8 >( MakeErrorCode( CoreErrc::kIPCReadOverflow ) );
                    INNER_CORE_LOG( "Subscriber::Receive - Read overflow on channel %u", sample.ChannelID() );
                } else {
                    totalRead++;
                }
            }
            return Result< UInt8 >::FromValue( totalRead );
        }
    }

    Result< Sample > Subscriber::ReceiveOnce( UInt8 channel_id, SubscribePolicy policy ) noexcept
    {
        if ( channel_id != kInvalidChannelID ) {
            auto result = InnerReceive( channel_id, policy );

            if ( !result ) {
                return Result< Sample >::FromError( result.Error() );
            }

            return Result< Sample >( std::move( result ).Value() );
        }

        auto active = active_channel_index_.load( std::memory_order_acquire );

        if ( read_channels_[ active ].empty() ) {
            return Result< Sample >( MakeErrorCode( CoreErrc::kIPCRetry ) );
        }

        auto it = read_channels_[ active ].begin();

        auto read_result = it->second->ReadWithPolicy( policy, config_.timeout );
        if ( !read_result )  {
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

        return Result< Sample >( std::move( sample ) );
    }

    template<class Fn>
    Result< Size > Subscriber::ReceiveOnce( Fn&& read_fn, UInt8 channel_id, SubscribePolicy policy ) noexcept
    {
        DEF_LAP_STATIC_ASSERT( ( std::is_invocable_r_v< Size, Fn, UInt8, Byte*, Size > ),
                      "Fn must be callable like Size( UInt8, Byte*, Size)" );
            
        if ( channel_id == kInvalidChannelID ) {
            auto sample_result = InnerReceive( channel_id, policy );
            if ( !sample_result ) {
                return Result< Size >::FromError( sample_result.Error() );
            }
            auto sample = std::move( sample_result ).Value();
            Size readSize = read_fn( sample.ChannelID(), sample.RawData(), sample.RawDataSize() );
            if ( readSize > sample.RawDataSize() ) {
                return Result< Size >( MakeErrorCode( CoreErrc::kIPCReadOverflow ) );
            } else {
                return Result< Size >::FromValue( readSize );
            }
        } 

        auto active = active_channel_index_.load( std::memory_order_acquire );
        if ( read_channels_[ active ].empty() ) {
            return Result< Size >( MakeErrorCode( CoreErrc::kIPCRetry ) );
        }

        auto it = read_channels_[ active ].begin();

        auto read_result = it->second->ReadWithPolicy( policy, config_.timeout );
        if ( !read_result )  {
            return Result< Size >( read_result.Error() );
        }
        // Extract chunk_index from ChannelQueueValue
        UInt16 chunk_index = read_result.Value().chunk_index;

        // Create Sample wrapper
        Sample sample( allocator_.get(), chunk_index );
        if ( !sample.IsValid() ) {
            return Result< Size >( MakeErrorCode( CoreErrc::kIPCInvalidChunkIndex ) );
        }

        if ( sample.GetState() != ChunkState::kSent &&
            sample.GetState() != ChunkState::kReceived ) {
            return Result< Size >( MakeErrorCode( CoreErrc::kIPCInvalidState ) );
        }
        sample.TransitionState( ChunkState::kReceived );

        Size readSize = read_fn( sample.ChannelID(), sample.RawData(), sample.RawDataSize() );
        if ( readSize > sample.RawDataSize() ) {
            // return Result< UInt8 >( MakeErrorCode( CoreErrc::kIPCReadOverflow ) );
            INNER_CORE_LOG( "Subscriber::Receive - Read overflow on channel %u", sample.ChannelID() );
        }
        return Result< Size >::FromValue( readSize );
    }
        
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

        ChannelRegistry::DeactiveChannel( shm_.get(), config_.channel_id );

        return {};
    }

    Subscriber::Subscriber( const String& shmPath,
                  const SubscriberConfig& config,
                  UniqueHandle<SharedMemoryManager> shm,
                  UniqueHandle<ChunkPoolAllocator> allocator) noexcept
            : shm_path_(shmPath)
            , config_(config)
            , shm_(std::move(shm))
            , allocator_(std::move(allocator))
    {
        DEF_LAP_ASSERT( shm_ != nullptr, "SharedMemoryManager must not be null" );
        DEF_LAP_ASSERT( allocator_ != nullptr, "ChunkPoolAllocator must not be null" );

        is_running_.store( false, std::memory_order_relaxed );
        active_channel_index_.store( 0, std::memory_order_relaxed );
        auto ctrl = shm_->GetControlBlock();

        INNER_CORE_LOG( "Subscriber::Subscriber - Created subscriber with ID: %u, type: %d, path: %s\n", 
                            config_.channel_id, 
                            static_cast<int>(ctrl->header.type),
                            shm_path_.c_str() );
        StartScanner( 1000, 10000 ); // 10ms interval
    }

    Subscriber::Subscriber(Subscriber&& other) noexcept
    {
        // Transfer ownership of scanner thread
        other.StopScanner();

        shm_path_ = std::move(other.shm_path_);
        config_ = std::move(other.config_);
        shm_ = std::move(other.shm_);
        allocator_ = std::move(other.allocator_);
        event_hooks_ = std::move(other.event_hooks_);

        is_running_.store( false, std::memory_order_relaxed );
        active_channel_index_.store( 0, std::memory_order_relaxed );
        auto ctrl = shm_->GetControlBlock();

        INNER_CORE_LOG( "Subscriber::Subscriber - Move subscriber with ID: %u, type: %d, path: %s\n", 
                            config_.channel_id, 
                            static_cast<int>(ctrl->header.type),
                            shm_path_.c_str() );

        StartScanner( 1000, 10000 ); // 10ms interval
    }
     
    Subscriber::~Subscriber() noexcept
    {
        StopScanner();
        read_channels_[0].clear();
        read_channels_[1].clear();

        if ( shm_ ) {
            // Step 1: Disconnect from service
            Disconnect();

            // ====== Step 2: Unregister from ChannelRegistry ======
            ChannelRegistry::UnregisterReadChannel( shm_->GetControlBlock(), config_.channel_id );
            
            // ====== Step 3: Consume remaining messages in queue ======
            // This ensures ref_count is properly decremented
            auto* queue = shm_->GetChannelQueue( config_.channel_id );
            
            if ( queue ) {
                while ( !IsQueueEmpty() ) {
                    auto sample_result = Receive();
                    if ( sample_result.HasValue() ) {
                        // Sample destructor will decrement ref_count automatically
                    } else {
                        break;  // Queue empty or error
                    }
                }
            }
        }    
    }

    void Subscriber::StartScanner( UInt16 timeout_microseconds, UInt16 interval_microseconds ) noexcept
    {
        if ( is_running_.load(std::memory_order_acquire) ) {
            return;  // Already running
        }

        is_running_.store( true, std::memory_order_release );
        scanner_thread_ = std::thread( &Subscriber::InnerChannelScanner, this, timeout_microseconds, interval_microseconds );
    }
        
    void Subscriber::StopScanner() noexcept
    {
        if ( !is_running_.load(std::memory_order_acquire) || shm_ == nullptr ) {
            return;  // Not running
        }

        is_running_.store( false, std::memory_order_release );

        DEF_LAP_ASSERT( ( shm_ != nullptr ) && ( shm_->GetControlBlock() ) != nullptr, "SharedMemoryManager pointer is null" ); 
        WaitSetHelper::FutexWake( &shm_->GetControlBlock()->registry.write_seq, INT32_MAX );
        if ( scanner_thread_.joinable() ) {
            scanner_thread_.join();
        }
    }

    /**
    * @brief Internal channel scanner thread
    * @details Periodically scans for active publishers and updates read channels
    */
    void Subscriber::InnerChannelScanner( UInt16 timeout_microseconds, UInt16 interval_microseconds ) noexcept
    {
        DEF_LAP_ASSERT( shm_ != nullptr && shm_->GetControlBlock() != nullptr, "SharedMemoryManager or ControlBlock must not be null" );

        auto* ctrl = shm_->GetControlBlock();
        auto& write_mask = ctrl->registry.write_mask;
        auto& write_seq = ctrl->registry.write_seq;

        // For SPMC or MPSC, we only need to track active publishers
        // Do not use in MPMC
        UInt32 last = 0;
        while ( is_running_.load(std::memory_order_acquire) ) {
            WaitSetHelper::FutexWait( &write_seq, last, timeout_microseconds * 1000 );

            if ( !is_running_.load(std::memory_order_acquire) ) break;
            
            if ( last != write_seq.load(std::memory_order_acquire) ) {
                last = write_seq.load(std::memory_order_acquire);
                UpdateReadChannel( write_mask.load(std::memory_order_acquire) );
            }

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

    Result< Sample > Subscriber::InnerReceive( UInt8 channel_id, SubscribePolicy policy ) noexcept
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
