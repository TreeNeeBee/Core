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
#include "IPCEventHooks.hpp"
#include "CResult.hpp"
#include "CString.hpp"
#include "Channel.hpp"
#include "CFunction.hpp"
#include <type_traits>
#include <chrono>
#include <thread>

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
        UInt8               channel_id = 0xFF;                          ///< Channel ID (for multi-channel support)
        UInt16              STmin = 0;                                  ///< Minimum interval between messages (microseconds)
        UInt32              max_chunks = kDefaultChunks;                ///< Maximum chunks in pool
        UInt32              chunk_size = 0;                             ///< Chunk size (payload), 0 means default
        UInt32              channel_capacity = kMaxChannelCapacity;     ///< Queue capacity
        UInt64              timeout = 100000000;                        ///< Receive timeout (ns), 0 means no wait
        SubscribePolicy     empty_policy = SubscribePolicy::kBlock;     ///< Default policy
        IPCType             ipc_type = IPCType::kSPMC;                  ///< IPC type
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
        using _MapChannel   = Map< UInt8, UniqueHandle< Channel< ChannelQueueValue > > >;
        using _ReadFunc     = Function< Size( UInt8, Byte*, Size ) >;


        /**
         * @brief Create subscriber
         * @param shm Service name
         * @param config Configuration
         * @return Result with subscriber or error
         */
        static Result< Subscriber > Create( const String& shmPath,
                                           const SubscriberConfig& config = {} ) noexcept;
        
        /**
         * @brief Create subscriber from existing shared memory file descriptor
         * @param shm_fd Shared memory file descriptor
         * @param config Configuration
         * @return Result with subscriber or error
         */
        static Result< Subscriber > Create( int shm_fd,
                                           const SubscriberConfig& config = {} ) noexcept;
         
        /**
         * @brief Destructor - automatically disconnects
         */
        ~Subscriber() noexcept;
        
        // Delete copy - external users must use Create()
        Subscriber(const Subscriber&) = delete;
        Subscriber& operator=(const Subscriber&) = delete;
        
        // Allow move - required by Result<Subscriber>
        Subscriber(Subscriber&&) noexcept;
        Subscriber& operator=(Subscriber&&) noexcept = delete;

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
        Result< Vector< Sample > > Receive( SubscribePolicy policy = SubscribePolicy::kBlock ) noexcept;

        /**
         * @brief Receive message using lambda/function to read payload
         * @tparam Fn Callable type with signature Size(UInt8, Byte*, Size)
         * @param read_fn Function to read data from chunk
         * @param policy Subscribe policy
         * @return Result with bytes read or error
         * 
         * @details
         * - Template implementation must be in header for proper instantiation
         * - Receives a sample, calls read_fn to process it
         * - read_fn should return number of bytes read
         */
        Result< Size > Receive( _ReadFunc read_fn,
                         SubscribePolicy policy = SubscribePolicy::kBlock ) noexcept;

        /*** @brief Receive next message from specific channel
         * @param channel_id Channel ID to receive from
         * @param policy Queue empty policy (overrides config)
         * @return Result with Sample or error
         */
        Result< Sample > ReceiveFrom( UInt8 channel_id, SubscribePolicy policy = SubscribePolicy::kBlock ) noexcept;

        /**
         * @brief Receive message from specific channel using lambda/function to read payload
         * @tparam Fn Callable type with signature Size(UInt8, Byte*, Size)
         * @param read_fn Function to read data from chunk
         * @param channel_id Channel ID to receive from
         * @param policy Subscribe policy
         * @return Result with bytes read or error
         * 
         * @details
         * - Template implementation must be in header for proper instantiation
         * - Receives a sample, calls read_fn to process it
         * - read_fn should return number of bytes read
         */
        Result< Size > ReceiveFrom( _ReadFunc read_fn, UInt8 channel_id, 
                         SubscribePolicy policy = SubscribePolicy::kBlock ) noexcept;
        
        /**
         * @brief Set event hooks for monitoring
         * @param hooks Event hooks implementation
         */
        inline void SetEventHooks( SharedHandle<IPCEventHooks> hooks ) noexcept
        {
            m_pEventHooks = std::move(hooks);
        }
        
        /**
         * @brief Get event hooks
         * @return Event hooks pointer (may be null)
         */
        inline IPCEventHooks* GetEventHooks() const noexcept
        {
            return m_pEventHooks.get();
        }

        /**
         * @brief Update STmin for this subscriber
         * @param stmin Minimum interval between messages (microseconds)
         */
        void UpdateSTMin(UInt16 stmin) noexcept;

        /**
         * @brief Connect to service and activate read channels
         * @return Result with success or error
         */
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

    private:
        /**
         * @brief Private constructor
         * @param config Configuration
         * @param shm Shared memory manager
         * @param allocator Chunk pool allocator
         */
        Subscriber( const SubscriberConfig& config,
                  UniqueHandle<SharedMemoryManager> shm,
                  UniqueHandle<ChunkPoolAllocator> allocator) noexcept;

        /**
         * @brief Start internal channel scanner thread
         * @param timeout_microseconds Futex wait timeout in microseconds (0 = infinite)
         * @param interval_microseconds Scan interval in microseconds
         */
        void startScanner( UInt16 timeout_microseconds = 0, UInt16 interval_microseconds = 0 ) noexcept;
        
        /**
         * @brief Stop internal channel scanner thread
         */
        void stopScanner() noexcept;

        /**
        * @brief Internal channel scanner thread
        * @details Periodically scans for active publishers and updates read channels
        */
        void innerChannelScanner( UInt16 timeout_microseconds = 0, UInt16 interval_microseconds = 0 ) noexcept;

        /* @brief Update read channels based on active publishers
        * @details Called periodically to refresh the list of active channels
        * - Scans ChannelRegistry for active publishers
        * - Updates internal m_readChannels vector accordingly
        */
        void updateReadChannel( UInt64 read_mask ) noexcept;

        Result< Sample > innerReceive( UInt8 channel_id, SubscribePolicy policy ) noexcept;
    
    private:
        SubscriberConfig                    m_config;                ///< Configuration
        UniqueHandle<SharedMemoryManager>   m_pShm;                   ///< Shared memory manager
        UniqueHandle<ChunkPoolAllocator>    m_pAllocator;             ///< Chunk allocator
        SharedHandle<IPCEventHooks>         m_pEventHooks;           ///< Event hooks for monitoring
        Atomic<Bool>                        m_bConnected;          ///< Connection state
        Atomic<Bool>                        m_bRunning;            ///< thread running flag
        std::thread                         m_scannerThread;        ///< Channel scanner thread
        Atomic<UInt8>                       m_iActiveChannelIdx;  ///< Active read channel index
        _MapChannel                         m_readChannels[2];      ///< Read channels for this subscriber
    };
    
}  // namespace ipc
}  // namespace core
}  // namespace lap

#endif  // LAP_CORE_IPC_SUBSCRIBER_HPP
