/**
 * @file        Subscriber.hpp
 * @author      LightAP Core Team
 * @brief       Zero-copy subscriber implementation
 * @date        2026-01-07
 * @details     Lock-free message reception with queue-based buffering
 * @copyright   Copyright (c) 2026
 * @note        Based on iceoryx2 design
 */
#ifndef LAP_CORE_IPC_SUBSCRIBER_HPP
#define LAP_CORE_IPC_SUBSCRIBER_HPP

#include "IPCTypes.hpp"
#include "Sample.hpp"
#include "SharedMemoryManager.hpp"
#include "ChunkPoolAllocator.hpp"
#include "ChannelRegistry.hpp"
#include "IPCEventHooks.hpp"
#include "CResult.hpp"
#include "CString.hpp"
#include "Message.hpp"
#include "Channel.hpp"
#include <memory>
#include <type_traits>
#include <chrono>

namespace lap
{
namespace core
{
namespace ipc
{
    /**
     * @brief Subscriber configuration
     */
    struct SubscriberConfig
    {
        UInt8   channel_id = 0xFF;                                  ///< Channel ID (for multi-channel support)
        UInt16  STmin = 0;                                          ///< Minimum interval between messages (microseconds)
        UInt32  max_chunks = kDefaultChunks;                     ///< Maximum chunks in pool
        UInt32  chunk_size = 0;                                     ///< Chunk size (payload), 0 means default
        UInt32  channel_capacity = kMaxChannelCapacity;                  ///< Queue capacity
        UInt64  timeout = 100000000;                                ///< Receive timeout (ns), 0 means no wait
        SubscribePolicy empty_policy = SubscribePolicy::kBlock;     ///< Default policy
    };
    
    /**
     * @brief Zero-copy subscriber
     * @tparam T Message type (must be Message or derived from Message)
     * 
     * @details
     * Usage:
     * 1. Create subscriber with service name
     * 2. Receive() to get message
     * 3. Process message
     * 4. Sample automatically released on destruction
     * 
     * @note T must be Message or a class derived from Message
     */
    class Subscriber
    {
    public:
        using _MapChannel = Map< UInt8, UniqueHandle< Channel< ChannelQueueValue > > >;

    public:
        /**
         * @brief Create subscriber
         * @param shm Service name
         * @param config Configuration
         * @return Result with subscriber or error
         */
        static Result< Subscriber > Create( const String& shmPath,
                                           const SubscriberConfig& config = {} ) noexcept;
        
        /**
         * @brief Destructor - automatically disconnects
         */
        ~Subscriber() noexcept;
        
        // Delete copy - external users must use Create()
        Subscriber(const Subscriber&) = delete;
        Subscriber& operator=(const Subscriber&) = delete;
        
        // Allow move - required by Result<Subscriber>
        Subscriber(Subscriber&&) noexcept = default;
        Subscriber& operator=(Subscriber&&) noexcept = default;
        
        /**
         * @brief Receive next message
         * @param policy Queue empty policy (overrides config)
         * @return Result with Sample or error
         * 
         * @details
         * - Dequeues chunk index from queue
         * - Creates Sample wrapper
         * - Behavior on empty queue depends on policy
         */
        Result< Vector< Sample > > Receive( UInt8 channel_id = kInvalidChannelID, SubscribePolicy policy = SubscribePolicy::kBlock ) noexcept;
        
        /**
         * @brief Receive message using lambda/function to read payload
         * @tparam Fn Callable type with signature Size(Byte*, Size)
         * @param read_fn Function to read data from chunk
         * @param policy Subscribe policy
         * @return Result with bytes read or error
         * 
         * @details
         * - Template implementation must be in header for proper instantiation
         * - Receives a sample, calls read_fn to process it
         * - read_fn should return number of bytes read
         */
        template<class Fn>
        Result< UInt8 > Receive( Fn&& read_fn, UInt8 channel_id = kInvalidChannelID,
                         SubscribePolicy policy = SubscribePolicy::kBlock ) noexcept
        {
            DEF_LAP_STATIC_ASSERT( std::is_invocable_r_v<Size, Fn, UInt8, Byte*, Size>,
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

        /**
         * @brief Get service name
         * @return Service name
         */
        inline const String& GetShmPath() const noexcept
        {
            return shm_path_;
        }
        
        /**
         * @brief Check if queue is empty
         * @return true if empty
         */
        Bool IsQueueEmpty() const noexcept;
        
        /**
         * @brief Get queue size (approximate)
         * @return Number of messages in queue
         */
        UInt32 GetQueueSize() const noexcept;
        
        /**
         * @brief Set event hooks for monitoring
         * @param hooks Event hooks implementation
         */
        inline void SetEventHooks( SharedHandle<IPCEventHooks> hooks ) noexcept
        {
            event_hooks_ = std::move(hooks);
        }
        
        /**
         * @brief Get event hooks
         * @return Event hooks pointer (may be null)
         */
        inline IPCEventHooks* GetEventHooks() const noexcept
        {
            return event_hooks_.get();
        }

        void UpdateSTMin(UInt8 stmin) noexcept;

        Result< void > Connect() noexcept;
        /**
         * @brief Disconnect from service and cleanup
         * @return Result with success or error
         * 
         * @details
         * - Unregister from shared memory registry
         * - Consume remaining messages in queue
         * - Deactivate queue slot
         * - Idempotent (safe to call multiple times)
         */
        Result< void > Disconnect() noexcept;
    
    protected:
        /**
         * @brief protected constructor
         */
        Subscriber( const String& shmPath,
                  const SubscriberConfig& config,
                  UniqueHandle<SharedMemoryManager> shm,
                  UniqueHandle<ChunkPoolAllocator> allocator) noexcept;

        /**
        * @brief Internal channel scanner thread
        * @details Periodically scans for active publishers and updates read channels
        */
        void InnerChannelScanner( UInt16 timeout_microseconds = 0, UInt16 interval_microseconds = 0 ) noexcept;

        /* @brief Update read channels based on active publishers
        * @details Called periodically to refresh the list of active channels
        * - Scans ChannelRegistry for active publishers
        * - Updates internal read_channels_ vector accordingly
        */
        void UpdateReadChannel( UInt64 read_mask ) noexcept;

        Result< Sample > InnerReceive( UInt8 channel_id, SubscribePolicy policy ) noexcept;

    public:
        String                              shm_path_;              ///< Shared memory path
        SubscriberConfig                    config_;                ///< Configuration
        UniqueHandle<SharedMemoryManager>   shm_;                   ///< Shared memory manager
        UniqueHandle<ChunkPoolAllocator>    allocator_;             ///< Chunk allocator
        SharedHandle<IPCEventHooks>         event_hooks_;           ///< Event hooks for monitoring
        Bool                                is_running_{false};     ///< thread running flag
        std::thread                         scanner_thread_;        ///< Channel scanner thread
        Atomic<UInt8>                       active_channel_index_;  ///< Active read channel index
        _MapChannel                         read_channels_[2];      ///< Read channels for this subscriber
    };
    
}  // namespace ipc
}  // namespace core
}  // namespace lap

#endif  // LAP_CORE_IPC_SUBSCRIBER_HPP
