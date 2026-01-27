/**
 * @file        Publisher.hpp
 * @author      LightAP Core Team
 * @brief       Zero-copy publisher implementation
 * @date        2026-01-07
 * @details     Loan-based publish API with lock-free message distribution
 * @copyright   Copyright (c) 2026
 * @note        Based on iceoryx2 design
 */
#ifndef LAP_CORE_IPC_PUBLISHER_HPP
#define LAP_CORE_IPC_PUBLISHER_HPP

#include "IPCTypes.hpp"
#include "Sample.hpp"
#include "SharedMemoryManager.hpp"
#include "ChunkPoolAllocator.hpp"
#include "ChannelRegistry.hpp"
#include "IPCEventHooks.hpp"
#include "CResult.hpp"
#include "CString.hpp"
#include "Message.hpp"
#include "CTypedef.hpp"

namespace lap
{
namespace core
{
namespace ipc
{
    /**
     * @brief Publisher configuration
     */
    struct PublisherConfig
    {
        UInt32 max_chunks       = kDefaultChunks;            ///< Maximum chunks in pool
        UInt32 chunk_size       = 0;                            ///< Chunk size (payload)
        UInt64 loan_timeout     = 100000000;                    ///< Loan timeout (ns), 0 means no wait
        UInt64 publish_timeout  = 100000000;                    ///< Publish timeout (ns), 0 means no wait
        LoanPolicy loan_policy  = LoanPolicy::kError;           ///< Loan failure policy
        PublishPolicy policy    = PublishPolicy::kOverwrite;    ///< Policy when full
    };
    
    /**
     * @brief Zero-copy publisher
     * @tparam T Message type (must be Message or derived from Message)
     * 
     * @details
     * Usage:
     * 1. Create publisher with service name
     * 2. Loan() to get writable chunk
     * 3. Write data to chunk
     * 4. Send() to publish to all subscribers
     * 
     * @note T must be Message or a class derived from Message
     */
    class Publisher
    {
    public:
        /**
         * @brief Create publisher
         * @param service_name Service name
         * @param config Configuration
         * @return Result with publisher or error
         */
        static Result< Publisher > Create(const String& shmPath,
                                          const PublisherConfig& config = {}) noexcept;
        
        /**
         * @brief Destructor
         */
        ~Publisher() noexcept = default;
        
        // Delete copy - external users must use Create()
        Publisher(const Publisher&) = delete;
        Publisher& operator=(const Publisher&) = delete;

        // Allow move - required by Result<Publisher>
        Publisher(Publisher&&) noexcept = default;
        Publisher& operator=(Publisher&&) noexcept = default;
        
        /**
         * @brief Get service name
         * @return Service name
         */
        inline const String& GetShmPath() const noexcept
        {
            return shm_path_;
        }
        
        /**
         * @brief Get allocated chunk count
         * @return Number of allocated chunks
         */
        UInt32 GetAllocatedCount() const noexcept;
        
        /**
         * @brief Check if chunk pool is exhausted
         * @return true if exhausted
         */
        Bool IsChunkPoolExhausted() const noexcept;
        
        /**
         * @brief Set event hooks for monitoring
         * @param hooks Event hooks implementation
         */
        void SetEventHooks(SharedHandle<IPCEventHooks> hooks) noexcept
        {
            event_hooks_ = std::move(hooks);
        }
        
        /**
         * @brief Get event hooks
         * @return Event hooks pointer (may be null)
         */
        IPCEventHooks* GetEventHooks() const noexcept
        {
            return event_hooks_.get();
        }

        /**
         * @brief Loan a chunk for writing
         * @return Result with Sample or error
         * 
         * @details
         * - Allocates chunk from pool
         * - Returns RAII Sample wrapper
         * - Behavior on pool exhaustion depends on loan_policy
         * 
         * @note For external use, prefer SendCopy() or SendEmplace() instead.
         *       This method is protected to allow advanced usage in derived classes.
         */
        Result< Sample > Loan() noexcept;
        
        /**
         * @brief Send sample to all subscribers
         * @param sample Sample to send (moved)
         * @param policy Queue full policy
         * @return Result
         * 
         * @details
         * - Transitions chunk state to kSent
         * - Enqueues chunk index to all subscriber queues
         * - Increments ref count for each subscriber
         * 
         * @note For external use, prefer SendCopy() or SendEmplace() instead.
         *       This method is protected to allow advanced usage in derived classes.
         */
        Result< void > Send( Sample&& sample, 
                         PublishPolicy policy = PublishPolicy::kOverwrite ) noexcept;
        
        Result< void > Send( Byte* buffer, Size size,
                         PublishPolicy policy = PublishPolicy::kOverwrite ) noexcept;
        
        /**
         * @brief Send message using lambda/function to write payload
         * @tparam Fn Callable type with signature Size(Byte*, Size)
         * @param write_fn Function to write data to chunk
         * @param policy Publish policy
         * @return Result with success or error
         * 
         * @details
         * - Template implementation must be in header for proper instantiation
         * - Loans a chunk, calls write_fn to populate it, then sends
         * - write_fn should return number of bytes written
         */
        template<class Fn>
        Result< void > Send( Fn&& write_fn,
                         PublishPolicy policy = PublishPolicy::kOverwrite ) noexcept
        {
            static_assert(std::is_invocable_r_v<Size, Fn, Byte*, Size>,
                      "Fn must be callable like Size(Byte*, Size)");

            auto sample_result = Loan();
            if ( !sample_result ) {
                return Result< void >( sample_result.Error() );
            }

            auto sample = std::move( sample_result ).Value();

            // Call fill function to populate chunk
            Byte* chunk_ptr = sample.RawData();
            Size chunk_size = sample.RawDataSize();
            Size written_size = write_fn( chunk_ptr, chunk_size );

            if ( written_size > chunk_size ) {
                // Fill function wrote more than chunk size
                return Result< void >( MakeErrorCode( CoreErrc::kInvalidArgument ) );
            }
            return Send( std::move( sample ), policy );
        }

    private:
        /**
         * @brief Private constructor
         */
        Publisher( const String& shmPath,
                 const PublisherConfig& config,
                 UniqueHandle<SharedMemoryManager> shm,
                 UniqueHandle<ChunkPoolAllocator> allocator) noexcept
            : shm_path_(shmPath)
            , config_(config)
            , shm_(std::move(shm))
            , allocator_(std::move(allocator))
            , event_hooks_(nullptr)
        {
            // Initialize last_send_ to epoch (far past) to ensure first send is not skipped
            for ( UInt32 i = 0; i < kMaxChannels; ++i ) {
                last_send_[i] = SteadyClock::time_point{};
            }
        }

    private:
        String shm_path_;                                     ///< Shared memory path
        PublisherConfig config_;                             ///< Configuration
        UniqueHandle< SharedMemoryManager > shm_;            ///< Shared memory manager
        UniqueHandle< ChunkPoolAllocator > allocator_;       ///< Chunk allocator
        SharedHandle< IPCEventHooks > event_hooks_;          ///< Event hooks for monitoring
        SteadyClock::time_point last_send_[kMaxChannels]; ///< Last send timestamps per subscriber
    };
    
}  // namespace ipc
}  // namespace core
}  // namespace lap

#endif  // LAP_CORE_IPC_PUBLISHER_HPP
