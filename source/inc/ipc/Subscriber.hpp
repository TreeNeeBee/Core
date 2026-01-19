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
#include "SubscriberRegistry.hpp"
#include "IPCEventHooks.hpp"
#include "CResult.hpp"
#include "CString.hpp"
#include "Message.hpp"
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
        UInt8  STmin = 0;                                           ///< Minimum interval between messages (ms)
        UInt32 max_chunks = kDefaultMaxChunks;                      ///< Maximum chunks in pool
        UInt32 chunk_size = 0;                                      ///< Chunk size (payload), 0 means default
        UInt32 queue_capacity = kQueueCapacity;                     ///< Queue capacity
        UInt64 timeout = 100000000;                                 ///< Receive timeout (ns), 0 means no wait
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
        /**
         * @brief Create subscriber
         * @param shm Service name
         * @param config Configuration
         * @return Result with subscriber or error
         */
        static Result< Subscriber > Create(const String& shmPath,
                                           const SubscriberConfig& config = {}) noexcept;
        
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
        Result< Sample > Receive( SubscribePolicy policy = SubscribePolicy::kBlock ) noexcept;

        Result< Size > Receive( Byte* buffer, Size size,
                         SubscribePolicy policy = SubscribePolicy::kBlock ) noexcept;
        
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
        Result< Size > Receive( Fn&& read_fn,
                         SubscribePolicy policy = SubscribePolicy::kBlock ) noexcept
        {
            static_assert(std::is_invocable_r_v<Size, Fn, Byte*, Size>,
                      "Fn must be callable like Size(Byte*, Size)");
            auto sample_result = Receive( policy );
            if ( !sample_result ) {
                return Result< Size >::FromError( sample_result.Error() );
            }
            auto sample = std::move( sample_result ).Value();

            return Result< Size >::FromValue( read_fn( sample.RawData(), sample.RawDataSize() ) );
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
                  UniqueHandle<ChunkPoolAllocator> allocator,
                  UInt32 subscriber_id) noexcept
            : shm_path_(shmPath)
            , config_(config)
            , shm_(std::move(shm))
            , allocator_(std::move(allocator))
            , subscriber_id_(subscriber_id)
        {
            ;
        }

    public:    
        String shm_path_;                                       ///< Shared memory path
        SubscriberConfig config_;                               ///< Configuration
        UniqueHandle<SharedMemoryManager> shm_;                 ///< Shared memory manager
        UniqueHandle<ChunkPoolAllocator> allocator_;            ///< Chunk allocator
        UInt32 subscriber_id_;                                  ///< Unique subscriber ID
        SharedHandle<IPCEventHooks> event_hooks_;               ///< Event hooks for monitoring
    };
    
}  // namespace ipc
}  // namespace core
}  // namespace lap

#endif  // LAP_CORE_IPC_SUBSCRIBER_HPP
