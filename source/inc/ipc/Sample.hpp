/**
 * @file        Sample.hpp
 * @author      LightAP Core Team
 * @brief       RAII wrapper for IPC chunks
 * @date        2026-01-07
 * @details     Manages chunk lifecycle with automatic reference counting
 * @copyright   Copyright (c) 2026
 * @note        Based on iceoryx2 loan-based API
 */
#ifndef LAP_CORE_IPC_SAMPLE_HPP
#define LAP_CORE_IPC_SAMPLE_HPP

#include "IPCTypes.hpp"
#include "ChunkHeader.hpp"
#include "ChunkPoolAllocator.hpp"
#include "Message.hpp"
#include <utility>
#include <cstring>
#include <cassert>

namespace lap
{
namespace core
{
namespace ipc
{
    /**
     * @brief RAII wrapper for loaned chunks
     * @tparam T Payload type
     * 
     * @details
     * - Automatically manages chunk reference counting
     * - Move-only (no copy)
     * - Releases chunk on destruction
     * - Provides safe access to payload
     */
    class Publisher;
    class Subscriber;
    class Sample final
    {
    public:
        /**
         * @brief Constructor
         * @param allocator Chunk pool allocator
         * @param chunk_index Chunk index
         */
        Sample( ChunkPoolAllocator* allocator, UInt32 chunk_index ) noexcept
            : chunk_index_( chunk_index )
            , allocator_( allocator )
            , header_( nullptr )
            , payload_( nullptr )
        {
            DEF_LAP_ASSERT( allocator_ != nullptr, "ChunkPoolAllocator must not be nullptr" );
            DEF_LAP_ASSERT( chunk_index_ != kInvalidChunkIndex, "Chunk index must be valid" );

            if ( allocator_ && chunk_index_ != kInvalidChunkIndex ) {
                header_ = allocator_->GetChunkHeader( chunk_index_ );
                if ( header_ ) {
                    // header_->ref_count.fetch_add( 1, std::memory_order_acq_rel );
                    payload_ = static_cast< Byte* >( header_->GetPayload() );
                }
            }

            DEF_LAP_ASSERT( header_ != nullptr, "ChunkHeader must not be nullptr" );
            DEF_LAP_ASSERT( payload_ != nullptr, "Payload must not be nullptr" );
        }
        
        /**
         * @brief Destructor - decrements ref count and releases chunk if last reference
         */
        ~Sample() noexcept
        {
            if ( header_ && allocator_ && chunk_index_ != kInvalidChunkIndex ) {
                // Decrement reference count
                //UInt8 ref_count = header_->ref_count.load( std::memory_order_acq_rel );
                UInt8 ref_count = header_->ref_count.fetch_sub( 1, std::memory_order_acq_rel );

                // If ref count reaches 0, return to pool
                if ( ref_count == 1 ) {
                    allocator_->Deallocate( chunk_index_ );
                }
            }

            Release();
        }
        
        // Delete copy
        Sample(const Sample&) = delete;
        Sample& operator=(const Sample&) = delete;
        
        /**
         * @brief Move constructor
         */
        Sample( Sample&& other ) noexcept
            : chunk_index_( other.chunk_index_ )
            , allocator_( other.allocator_ )
            , header_( other.header_ )
            , payload_( other.payload_ )
        {
            other.Release();
        }
        
        /**
         * @brief Move assignment
         */
        Sample& operator=( Sample&& other ) noexcept
        {
            if ( this != &other) {
                // Decrement ref count for current chunk before overwriting
                if ( header_ && allocator_ && chunk_index_ != kInvalidChunkIndex) {
                    UInt64 new_count = header_->ref_count.fetch_sub( 1, std::memory_order_acq_rel );
                    if ( new_count == 1 ) {
                        allocator_->Deallocate(chunk_index_);
                    }
                }
                
                allocator_ = other.allocator_;
                chunk_index_ = other.chunk_index_;
                header_ = other.header_;
                payload_ = other.payload_;
                
                other.Release();
            }
            return *this;
        }

        template < typename T >
        inline T* Payload() noexcept
        {
            return reinterpret_cast<T*>( payload_ );
        }

        template < typename T >
        inline const T* Payload() const noexcept
        {
            return reinterpret_cast<T*>( payload_ );
        }
        
        /**
         * @brief Get payload pointer
         * @return Payload pointer (nullptr if invalid)
         */
        inline Byte* RawData() noexcept
        {
            return payload_;
        }
        
        /**
         * @brief Get const payload pointer
         * @return Const payload pointer
         */
        inline const Byte* RawData() const noexcept
        {
            return payload_;
        }

        inline Size RawDataSize() const noexcept
        {
            if ( header_ ) {
                return header_->payload_size_;
            } else {
                return 0;
            }
        }

        inline UInt8 ChannelID() const noexcept
        {
            if ( header_ ) {
                return header_->channel_id.load(std::memory_order_acquire);
            } else {
                return kInvalidChannelID;
            }
        }

        inline void SetChannelID( UInt8 channel_id ) const noexcept
        {
            if ( header_ ) {
                header_->channel_id.store(channel_id, std::memory_order_release);
            }
        }
        
        /**
         * @brief Dereference operator
         */
        inline Byte& operator*() noexcept
        {
            return *payload_;
        }
        
        /**
         * @brief Const dereference operator
         */
        inline const Byte& operator*() const noexcept
        {
            return *payload_;
        }
        
        /**
         * @brief Arrow operator
         */
        inline Byte* operator->() noexcept
        {
            return payload_;
        }
        
        /**
         * @brief Const arrow operator
         */
        inline const Byte* operator->() const noexcept
        {
            return payload_;
        }

        Size Write( const Byte* const buffer,  Size size ) const noexcept
        {
            if ( !payload_ || !header_ || !buffer || size == 0 ) {
                return 0;
            }

            Size copy_size = size < header_->payload_size_ ? size : header_->payload_size_;
        
            std::memcpy( payload_, buffer, copy_size );

            return copy_size;
        }

        Size Read( Byte* const buffer, Size size ) const noexcept
        {
            if ( !payload_ || !header_ || !buffer || size == 0 ) {
                return 0;
            }

            Size copy_size = size < header_->payload_size_ ? size : header_->payload_size_;
        
            std::memcpy( buffer, payload_, copy_size );

            return copy_size;
        }

        template < typename T, typename... Args >
        void Emplace( Args&&... args ) noexcept
        {
            static_assert( std::is_base_of_v< Message, T >,
                      "T must derive from Message");

            if ( payload_ ) {
                new ( payload_ ) T( std::forward<Args>(args)... );
            }
        }

        // template <typename T>
        // void onMessageSend() noexcept
        // {
        //     if constexpr ( std::is_base_of<Message, T>::value ) {
        //         if ( payload_ && header_ ) {
        //             payload_->OnMessageSend( static_cast< void* >( payload_ ), header_->payload_size_ );
        //         }
        //     }
        // }

        // template <typename T>
        // void onMessageReceived() noexcept
        // {
        //     if constexpr ( std::is_base_of<Message, T>::value ) {
        //         if ( payload_ && header_ ) {
        //             payload_->OnMessageReceived( static_cast< const void* >( payload_ ), header_->payload_size_ );
        //         }
        //     }
        // }
        
        /**
         * @brief Check if sample is valid
         * @return true if valid
         */
        inline Bool IsValid() const noexcept
        {
            return ( chunk_index_ != kInvalidChunkIndex )
                   && ( allocator_ != nullptr ) 
                   && ( header_ != nullptr ) 
                   && ( payload_ != nullptr );
        }
        
        /**
         * @brief Boolean conversion
         */
        explicit operator Bool() const noexcept
        {
            return IsValid();
        }
        
        /**
         * @brief Get chunk index
         * @return Chunk index
         */
        inline UInt16 GetChunkIndex() const noexcept
        {
            return chunk_index_;
        }

        /**
         * @brief Release ownership of the chunk without decrementing ref_count
         * @details Used by Publisher::Send to transfer ownership to subscribers
         *          The chunk's ref_count will be managed by subscribers
         */
        void Release() noexcept
        {
            header_ = nullptr;
            payload_ = nullptr;
            chunk_index_ = kInvalidChunkIndex;
            allocator_ = nullptr;
        }

        /**
         * @brief Get current state
         * @return Current chunk state
         */
        inline ChunkState GetState() const noexcept
        {
            DEF_LAP_ASSERT( header_ != nullptr, "ChunkHeader must not be nullptr" );
            return static_cast< ChunkState >( header_->state.load( std::memory_order_acquire ) );
        }

    protected:
        friend class Publisher;
        friend class Subscriber;

        inline UInt8 FetchAdd( UInt8 delta ) noexcept
        {
            DEF_LAP_ASSERT( header_ != nullptr, "ChunkHeader must not be nullptr" );

            return header_->ref_count.fetch_add( delta, std::memory_order_acq_rel );
        }

        inline UInt8 IncrementRef() noexcept
        {
            DEF_LAP_ASSERT( header_ != nullptr, "ChunkHeader must not be nullptr" );

            return header_->ref_count.fetch_add( 1, std::memory_order_acq_rel );
        }

        inline UInt8 DecrementRef() noexcept
        {
            DEF_LAP_ASSERT( header_ != nullptr, "ChunkHeader must not be nullptr" );

            return header_->ref_count.fetch_sub( 1, std::memory_order_acq_rel );
        }

        /**
         * @brief Transition state atomically
         * @param expected Expected current state
         * @param desired Desired new state
         * @return true if transition succeeded
         */
        Bool TransitionState( ChunkState expected, ChunkState desired ) noexcept
        {
            DEF_LAP_ASSERT( header_ != nullptr, "ChunkHeader must not be nullptr" );

            UInt8 expected_val = static_cast< UInt8 >( expected );
            UInt8 desired_val = static_cast< UInt8 >( desired );
            
            return header_->state.compare_exchange_strong(
                expected_val,
                desired_val,
                std::memory_order_acq_rel,
                std::memory_order_acquire
            );
        }

        inline ChunkState TransitionState( ChunkState desired ) noexcept
        {
            DEF_LAP_ASSERT( header_ != nullptr, "ChunkHeader must not be nullptr" );

            return static_cast< ChunkState >( header_->state.exchange( static_cast< UInt8 >( desired ), std::memory_order_acq_rel ) );
        }
    
    private:
        /**
         * @brief Get chunk header
         * @return Chunk header pointer
         */
        inline ChunkHeader* GetHeader() noexcept
        {
            return header_;
        }

        inline ChunkPoolAllocator* GetChunkPoolAllocator() noexcept
        {
            return allocator_;
        }
        
    private:
        UInt16                  chunk_index_;   ///< Chunk index
        ChunkPoolAllocator*     allocator_;     ///< Allocator reference
        ChunkHeader*            header_;        ///< Chunk header
        Byte*                   payload_;       ///< Typed payload pointer
    };
    
}  // namespace ipc
}  // namespace core
}  // namespace lap

#endif  // LAP_CORE_IPC_SAMPLE_HPP
