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
#include <ctime>

namespace lap
{
namespace core
{
namespace ipc
{
    namespace
    {
        constexpr UInt16 kScannerTimeoutUs = 1000U;
        constexpr UInt16 kScannerIntervalUs = 10000U;
    }

    Result< Publisher > Publisher::Create(const String& shmPath,
                                              const PublisherConfig& config) noexcept
    {
        // Create shared memory manager
        auto shm = MakeUnique< SharedMemoryManager >();
        
        SharedMemoryConfig shm_config{};
        shm_config.max_chunks   = config.max_chunks;
        shm_config.chunk_size   = config.chunk_size;  // Payload size only
        shm_config.ipc_type     = config.ipc_type;
        
        auto result = shm->Open( shmPath, shm_config );
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

        auto m_config = config;
        while ( true ) {
            // Allocate unique queue index from shared memory control block
            auto publisher_id_result = ChannelRegistry::RegisterWriteChannel( shm->GetControlBlock(), m_config.channel_id );
            if ( DEF_LAP_IF_UNLIKELY( !publisher_id_result ) ) {
                if ( publisher_id_result.Error() == CoreErrc::kIPCRetry ) {
                    // Retry allocation
                    std::this_thread::yield();
                    continue;
                } else {
                    return Result< Publisher >( MakeErrorCode( CoreErrc::kResourceExhausted ) );
                }
            } else {
                m_config.channel_id = publisher_id_result.Value();
                break;
            }
        }

        // Create publisher
        Publisher publisher( m_config, std::move(shm), std::move(allocator) );
        
        return Result< Publisher >( std::move( publisher ) );
    }

    Result< Publisher > Publisher::Create(int shm_fd,
                                            const PublisherConfig& config) noexcept
    {
        // Create shared memory manager
        auto shm = MakeUnique< SharedMemoryManager >();
        
        SharedMemoryConfig shm_config{};
        shm_config.max_chunks   = config.max_chunks;
        shm_config.chunk_size   = config.chunk_size;  // Payload size only
        shm_config.ipc_type     = config.ipc_type;
        
        auto result = shm->Open( shm_fd, shm_config );
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

        auto m_config = config;
        while ( true ) {
            // Allocate unique queue index from shared memory control block
            auto publisher_id_result = ChannelRegistry::RegisterWriteChannel( shm->GetControlBlock(), m_config.channel_id );
            if ( DEF_LAP_IF_UNLIKELY( !publisher_id_result ) ) {
                if ( publisher_id_result.Error() == CoreErrc::kIPCRetry ) {
                    // Retry allocation
                    std::this_thread::yield();
                    continue;
                } else {
                    return Result< Publisher >( MakeErrorCode( CoreErrc::kResourceExhausted ) );
                }
            } else {
                m_config.channel_id = publisher_id_result.Value();
                break;
            }
        }

        // Create publisher
        Publisher publisher( m_config, std::move(shm), std::move(allocator) );
        
        return Result< Publisher >( std::move( publisher ) );
    }
       

    Result< Sample > Publisher::Loan() noexcept
    {
        // Try to allocate chunk
        auto chunk_index = m_pAllocator->Allocate();

        if ( chunk_index == kInvalidChunkIndex ) {
            // Trigger hook: Loan failed
            // if ( m_pEventHooks ) {
            //     m_pEventHooks->OnLoanFailed(
            //         m_config.loan_policy,
            //         m_pAllocator->GetAllocatedCount(),
            //         m_pAllocator->GetMaxChunks()
            //     );
            // }
            
            // Pool exhausted - handle based on policy
            if (m_config.loan_policy == LoanPolicy::kError) {
                return Result< Sample >( MakeErrorCode( CoreErrc::kIPCChunkPoolExhausted ) );
            } else if ( m_config.loan_policy == LoanPolicy::kWait ) {
                // Busy-wait polling for free chunk
                std::chrono::nanoseconds timeout = std::chrono::nanoseconds( m_config.loan_timeout );  // 10ms default
                Bool success = WaitSetHelper::PollForFlags(
                    &m_pShm->GetControlBlock()->pool_state.remain_count,
                    0xFFFFFFFF,  // Any non-zero value
                    timeout
                );
                
                if ( !success ) {
                    return Result< Sample >( MakeErrorCode( CoreErrc::kIPCChunkPoolExhausted ) );
                }

                // Retry allocation
                chunk_index = m_pAllocator->Allocate();
                if ( chunk_index == kInvalidChunkIndex ) {
                    return Result< Sample >( MakeErrorCode( CoreErrc::kIPCChunkPoolExhausted ) );
                }
            } else if ( m_config.loan_policy == LoanPolicy::kBlock ) {
                // Block on futex until chunk available
                std::chrono::nanoseconds timeout = std::chrono::nanoseconds( m_config.loan_timeout );  // 100ms default
                auto wait_result = WaitSetHelper::WaitForFlags(
                    &m_pShm->GetControlBlock()->pool_state.remain_count,
                    0xFFFFFFFF,  // Any non-zero value
                    timeout
                );
                
                if ( !wait_result ) {
                    return Result< Sample >( MakeErrorCode( CoreErrc::kIPCChunkPoolExhausted ) );
                }

                // Retry allocation
                chunk_index = m_pAllocator->Allocate();
                if ( chunk_index == kInvalidChunkIndex ) {
                    return Result< Sample >( MakeErrorCode( CoreErrc::kIPCChunkPoolExhausted ) );
                }
            } else {
                return Result< Sample >( MakeErrorCode( CoreErrc::kIPCChunkPoolExhausted ) );
            }
        }
        Sample sample{ m_pAllocator.get(), chunk_index };

        sample.SetChannelID( m_config.channel_id );
        sample.TransitionState( ChunkState::kLoaned );
        
        return Result< Sample >( std::move(sample) );
    }
    
    Result<void> Publisher::Send( Sample&& sample,
                                        PublishPolicy policy ) noexcept
    {
        if ( DEF_LAP_IF_UNLIKELY( !sample.IsValid() ) ) {
            return Result< void >( MakeErrorCode( CoreErrc::kInvalidArgument ) );
        }

        DEF_LAP_ASSERT( sample.GetState() == ChunkState::kLoaned || sample.GetState() == ChunkState::kSent, 
                        "Sending chunk must be in Loaned or Sent state" );
        sample.TransitionState( ChunkState::kSent );
        sample.IncrementRef();  // For publisher's own reference

        // Get snapshot of subscribers from shared memory registry
        auto& snapshot = m_writeChannels[ m_iActiveChannelIdx.load( std::memory_order_acquire ) ];

        if ( snapshot.size() == 0 ) {
            //sample.DecrementRef();  // No subscribers, release chunk
            return {};
        }

        auto now = SteadyClock::now();
        
        // Set initial reference count to number of subscribers
        sample.FetchAdd( snapshot.size() );

        for ( auto& [channel_id, channel] : snapshot ) {
            if ( !channel || !channel->IsActive() || !channel->IsValid() ) {
                // Queue not available, decrement ref count
                sample.DecrementRef();
                continue;
            }

            auto interval = std::chrono::microseconds( channel->GetSTMin() );
            if ( ( now - m_lastSend[channel_id] ) < interval ) {
                // Skip sending to this subscriber due to STmin
                sample.DecrementRef();
                continue;
            }

            while( !channel->TryLock() ) {
                // Busy-wait until lock acquired
                std::this_thread::yield();
            }

            // Prepare value to write
            ChannelQueueValue value{ 0, sample.GetChunkIndex() };  // sequence can be used for message ordering if needed

            // Write using Channel API with policy
            auto write_result = channel->WriteWithPolicy( value, policy, m_config.publish_timeout );
            
            channel->Unlock();

            if ( !write_result ) {
                // Write failed, decrement ref count
                sample.DecrementRef();
                continue;
            }

            // Update last send time for STmin enforcement
            m_lastSend[channel_id] = now;
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

    Result< void > Publisher::Send( _WriteFunc write_fn,
                         PublishPolicy policy ) noexcept
    {

        auto sample_result = Loan();
        if ( !sample_result ) {
            return Result< void >( sample_result.Error() );
        }

        auto sample = std::move( sample_result ).Value();

        // Call fill function to populate chunk
        Byte* chunk_ptr = sample.RawData();
        Size chunk_size = sample.RawDataSize();
        Size written_size = write_fn( kInvalidChannelID, chunk_ptr, chunk_size );

        if ( written_size > chunk_size ) {
            // Fill function wrote more than chunk size
            return Result< void >( MakeErrorCode( CoreErrc::kInvalidArgument ) );
        }
        return Send( std::move( sample ), policy );
    }

    Result< void > Publisher::SendTo( Sample&& sample, UInt8 channel_id,
                         PublishPolicy policy ) noexcept
    {
        if ( !sample.IsValid() ) {
            return Result< void >( MakeErrorCode( CoreErrc::kInvalidArgument ) );
        }

        DEF_LAP_ASSERT( channel_id < kMaxChannels, "channel_id must be valid" );
        DEF_LAP_ASSERT( sample.GetState() == ChunkState::kLoaned || sample.GetState() == ChunkState::kSent, 
                        "Sending chunk must be in Loaned or Sent state" );
        sample.TransitionState( ChunkState::kSent );

        sample.IncrementRef();  // For publisher's own reference

        // Get snapshot of subscribers from shared memory registry
        auto& snapshot = m_writeChannels[ m_iActiveChannelIdx.load( std::memory_order_acquire ) ];

        if ( snapshot.size() == 0 ) {
            //sample.DecrementRef();  // No subscribers, release chunk
            return {};
        }
        
        auto now = SteadyClock::now();
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

            auto interval = std::chrono::microseconds( channel->GetSTMin() );
            if ( ( now - m_lastSend[channel_id] ) < interval ) {
                // Skip sending to this subscriber due to STmin
                sample.DecrementRef();
                
                return Result< void >( MakeErrorCode( CoreErrc::kIPCRetry ) );
            }

            while( !channel->TryLock() ) {
                // Busy-wait until lock acquired
                std::this_thread::yield();
            }

            // Prepare value to write
            ChannelQueueValue value{ 0, sample.GetChunkIndex() };  // sequence can be used for message ordering if needed

            // Write using Channel API with policy
            auto write_result = channel->WriteWithPolicy( value, policy, m_config.publish_timeout );
            
            channel->Unlock();

            if ( !write_result ) {
                // Write failed, decrement ref count
                sample.DecrementRef();

                return Result< void >::FromError( write_result.Error() );
            }

            // Update last send time for STmin enforcement
            m_lastSend[channel_id] = SteadyClock::now();
        } else {
            // Channel ID not found, decrement ref count
            sample.DecrementRef();

            return Result< void >( MakeErrorCode( CoreErrc::kChannelNotFound ) );
        }

        return {};
    }
        
    Result< void > Publisher::SendTo( Byte* buffer, Size size, UInt8 channel_id,
                         PublishPolicy policy ) noexcept
    {
        if ( channel_id >= kMaxChannels ) {
            return Result< void >( MakeErrorCode( CoreErrc::kInvalidArgument ) );
        }

        auto sample_result = Loan();
        if ( !sample_result ) {
            return Result< void >( sample_result.Error() );
        }
        
        auto sample = std::move( sample_result ).Value();

        // Copy data to chunk
        sample.Write( buffer, size );
        
        return SendTo( std::move( sample ), channel_id, policy );
    }

    Result< void > Publisher::SendTo( _WriteFunc write_fn, UInt8 channel_id,
                        PublishPolicy policy ) noexcept
    {
        if ( channel_id >= kMaxChannels ) {
            return Result< void >( MakeErrorCode( CoreErrc::kInvalidArgument ) );
        }

        auto sample_result = Loan();
        if ( !sample_result ) {
            return Result< void >( sample_result.Error() );
        }

        auto sample = std::move( sample_result ).Value();

        // Call fill function to populate chunk
        Byte* chunk_ptr = sample.RawData();
        Size chunk_size = sample.RawDataSize();
        Size written_size = write_fn( channel_id, chunk_ptr, chunk_size );

        if ( written_size > chunk_size ) {
            // Fill function wrote more than chunk size
            return Result< void >( MakeErrorCode( CoreErrc::kInvalidArgument ) );
        }
        return SendTo( std::move( sample ), channel_id, policy );
    }

    UInt32 Publisher::GetAllocatedCount() const noexcept
    {
        return m_pAllocator->GetAllocatedCount();
    }
    
    Bool Publisher::IsChunkPoolExhausted() const noexcept
    {
        return m_pAllocator->IsExhausted();
    }
    
    Publisher::Publisher( const PublisherConfig& config,
                 UniqueHandle<SharedMemoryManager> shm,
                 UniqueHandle<ChunkPoolAllocator> allocator) noexcept
            : m_config(config)
            , m_pShm(std::move(shm))
            , m_pAllocator(std::move(allocator))
            , m_pEventHooks(nullptr)
            , m_writeChannels()
    {
        DEF_LAP_ASSERT( m_pShm != nullptr, "SharedMemoryManager must not be null" );
        DEF_LAP_ASSERT( m_pAllocator != nullptr, "ChunkPoolAllocator must not be null" );

        // Initialize m_lastSend to epoch (far past) to ensure first send is not skipped
        for ( UInt8 i = 0; i < kMaxChannels; ++i ) {
            m_lastSend[i] = SteadyClock::time_point{};
        }

        m_bRunning.store( false, std::memory_order_relaxed );
        m_iActiveChannelIdx.store( 0, std::memory_order_relaxed );
        auto ctrl = m_pShm->GetControlBlock();
        INNER_CORE_LOG( "Publisher::Publisher - Created publisher with ID: %u, type: %d\n", 
                            m_config.channel_id, 
                            static_cast<int>(ctrl->header.type) );

        if ( ctrl->header.type == IPCType::kMPSC ) {
            // write to own channel only
            m_writeChannels[0].emplace( m_config.channel_id,
                ChannelFactory<ChannelQueueValue>::CreateWriteChannelFromQueue(
                    m_pShm->GetChannelQueue( m_config.channel_id )
                )
            );
        } else {
            startScanner( kScannerTimeoutUs, kScannerIntervalUs );
        }
    }

    Publisher::Publisher(Publisher&& other) noexcept
    {
        // Transfer ownership of scanner thread
        other.stopScanner();

        m_config = std::move(other.m_config);
        m_pShm = std::move(other.m_pShm);
        m_pAllocator = std::move(other.m_pAllocator);
        m_pEventHooks = std::move(other.m_pEventHooks);

        for ( UInt8 i = 0; i < kMaxChannels; ++i ) {
            m_lastSend[i] = SteadyClock::time_point{};
        }

        m_bRunning.store( false, std::memory_order_relaxed );
        m_iActiveChannelIdx.store( 0, std::memory_order_relaxed );

        auto ctrl = m_pShm->GetControlBlock();
        INNER_CORE_LOG( "Publisher::Publisher - Move publisher with ID: %u, type: %d\n", 
                            m_config.channel_id, 
                            static_cast<int>(ctrl->header.type) );

        if ( ctrl->header.type == IPCType::kMPSC ) {
            // write to own channel only
            m_writeChannels[0].emplace( m_config.channel_id,
                ChannelFactory<ChannelQueueValue>::CreateWriteChannelFromQueue(
                    m_pShm->GetChannelQueue( m_config.channel_id )
                )
            );
        } else {
            startScanner( kScannerTimeoutUs, kScannerIntervalUs );
        }
    }

    Publisher::~Publisher() noexcept
    {
        stopScanner();
        m_writeChannels[0].clear();
        m_writeChannels[1].clear();

        // Deregister from ChannelRegistry
        if ( m_pShm ) {
            ChannelRegistry::UnregisterWriteChannel( m_pShm->GetControlBlock(), m_config.channel_id );
        }
    }

    void Publisher::startScanner( UInt16 timeout_microseconds, UInt16 interval_microseconds ) noexcept
    {
        if ( m_bRunning.load(std::memory_order_acquire) ) {
            return;  // Already running
        }

        m_bRunning.store( true, std::memory_order_release );
        m_scannerThread = std::thread( &Publisher::innerChannelScanner, this, timeout_microseconds, interval_microseconds );
    }

    /**
     * @brief Stop internal channel scanner thread
     */
    void Publisher::stopScanner() noexcept
    {
        if ( !m_bRunning.load(std::memory_order_acquire) || m_pShm == nullptr ) {
            return;  // Not running
        }

        m_bRunning.store( false, std::memory_order_release );

        DEF_LAP_ASSERT( ( m_pShm != nullptr ) && ( m_pShm->GetControlBlock() ) != nullptr, "SharedMemoryManager pointer is null" );

        // notify the futex to wake up  
        WaitSetHelper::FutexWake( &( m_pShm->GetControlBlock()->registry.read_seq ), INT32_MAX );
        if ( m_scannerThread.joinable() ) {
            m_scannerThread.join();
        }
    }

    void Publisher::innerChannelScanner( UInt16 timeout_microseconds, UInt16 interval_microseconds ) noexcept
    {
        DEF_LAP_ASSERT( m_pShm != nullptr && m_pShm->GetControlBlock() != nullptr, "SharedMemoryManager or ControlBlock must not be null" );

        auto* ctrl = m_pShm->GetControlBlock();

        auto& read_mask = ctrl->registry.read_mask;
        auto& read_seq = ctrl->registry.read_seq;

        UInt32 last = 0;
        while ( m_bRunning.load(std::memory_order_acquire) ) {
            WaitSetHelper::FutexWait( &read_seq, last, timeout_microseconds * 1000 );

            if ( !m_bRunning.load(std::memory_order_acquire) ) break;

            if ( last != read_seq.load(std::memory_order_acquire) ) {
                last = read_seq.load(std::memory_order_acquire);
                updateWriteChannel( read_mask.load(std::memory_order_acquire) );
            }
            std::this_thread::sleep_for( std::chrono::microseconds( interval_microseconds ) );
        }
    }

    void Publisher::updateWriteChannel( UInt64 read_mask ) noexcept
    {
        auto updateIndex = ( m_iActiveChannelIdx.load(std::memory_order_acquire) + 1 ) % 2;
        auto& write_channel = m_writeChannels[ updateIndex ]; 

        write_channel.clear();
        if ( read_mask == 0 ) {
            // No active subscribers
            m_iActiveChannelIdx.store( updateIndex, std::memory_order_release );
            return;
        }

        while ( read_mask ) {
            int idx = __builtin_ctzll( read_mask );  // Count trailing zeros
            
            // Get subscriber queue from shared memory
            auto* queue = m_pShm->GetChannelQueue( idx );
            if ( queue != nullptr ) {
                auto channel = ChannelFactory<ChannelQueueValue>::CreateWriteChannelFromQueue( queue );
                if ( channel ) {
                    write_channel.emplace( idx, std::move( channel ) );
                }
            }

            read_mask &= read_mask - 1;  // Clear lowest set bit
        }
        m_iActiveChannelIdx.store( updateIndex, std::memory_order_release );
    }
}  // namespace ipc
}  // namespace core
}  // namespace lap
