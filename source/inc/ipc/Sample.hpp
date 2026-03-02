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
            : m_iChunkIndex( chunk_index )
            , m_pAllocator( allocator )
            , m_pHeader( nullptr )
            , m_pPayload( nullptr )
        {
            DEF_LAP_ASSERT( m_pAllocator != nullptr, "ChunkPoolAllocator must not be nullptr" );
            DEF_LAP_ASSERT( m_iChunkIndex != kInvalidChunkIndex, "Chunk index must be valid" );

            if ( m_pAllocator && m_iChunkIndex != kInvalidChunkIndex ) {
                m_pHeader = m_pAllocator->GetChunkHeader( m_iChunkIndex );
                if ( m_pHeader ) {
                    // m_pHeader->ref_count.fetch_add( 1, std::memory_order_acq_rel );
                    m_pPayload = static_cast< Byte* >( m_pHeader->GetPayload() );
                }
            }

            DEF_LAP_ASSERT( m_pHeader != nullptr, "ChunkHeader must not be nullptr" );
            DEF_LAP_ASSERT( m_pPayload != nullptr, "Payload must not be nullptr" );
        }
        
        /**
         * @brief Destructor - decrements ref count and releases chunk if last reference
         */
        ~Sample() noexcept
        {
            if ( m_pHeader && m_pAllocator && m_iChunkIndex != kInvalidChunkIndex ) {
                // Decrement reference count
                //UInt8 ref_count = m_pHeader->ref_count.load( std::memory_order_acq_rel );
                UInt8 ref_count = m_pHeader->ref_count.fetch_sub( 1, std::memory_order_acq_rel );

                // If ref count reaches 0, return to pool
                if ( ref_count == 1 ) {
                    m_pAllocator->Deallocate( m_iChunkIndex );
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
            : m_iChunkIndex( other.m_iChunkIndex )
            , m_pAllocator( other.m_pAllocator )
            , m_pHeader( other.m_pHeader )
            , m_pPayload( other.m_pPayload )
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
                if ( m_pHeader && m_pAllocator && m_iChunkIndex != kInvalidChunkIndex) {
                    UInt64 new_count = m_pHeader->ref_count.fetch_sub( 1, std::memory_order_acq_rel );
                    if ( new_count == 1 ) {
                        m_pAllocator->Deallocate(m_iChunkIndex);
                    }
                }
                
                m_pAllocator = other.m_pAllocator;
                m_iChunkIndex = other.m_iChunkIndex;
                m_pHeader = other.m_pHeader;
                m_pPayload = other.m_pPayload;
                
                other.Release();
            }
            return *this;
        }

        template < typename T >
        inline T* Payload() noexcept
        {
            return reinterpret_cast<T*>( m_pPayload );
        }

        template < typename T >
        inline const T* Payload() const noexcept
        {
            return reinterpret_cast<T*>( m_pPayload );
        }
        
        /**
         * @brief Get payload pointer
         * @return Payload pointer (nullptr if invalid)
         */
        inline Byte* RawData() noexcept
        {
            return m_pPayload;
        }
        
        /**
         * @brief Get const payload pointer
         * @return Const payload pointer
         */
        inline const Byte* RawData() const noexcept
        {
            return m_pPayload;
        }

        inline Size RawDataSize() const noexcept
        {
            if ( m_pHeader ) {
                return m_pHeader->payload_size;
            } else {
                return 0;
            }
        }

        inline UInt8 ChannelID() const noexcept
        {
            if ( m_pHeader ) {
                return m_pHeader->channel_id.load(std::memory_order_acquire);
            } else {
                return kInvalidChannelID;
            }
        }

        inline void SetChannelID( UInt8 channel_id ) const noexcept
        {
            if ( m_pHeader ) {
                m_pHeader->channel_id.store(channel_id, std::memory_order_release);
            }
        }
        
        /**
         * @brief Dereference operator
         */
        inline Byte& operator*() noexcept
        {
            return *m_pPayload;
        }
        
        /**
         * @brief Const dereference operator
         */
        inline const Byte& operator*() const noexcept
        {
            return *m_pPayload;
        }
        
        /**
         * @brief Arrow operator
         */
        inline Byte* operator->() noexcept
        {
            return m_pPayload;
        }
        
        /**
         * @brief Const arrow operator
         */
        inline const Byte* operator->() const noexcept
        {
            return m_pPayload;
        }

        Size Write( const Byte* const buffer,  Size size ) const noexcept
        {
            if ( !m_pPayload || !m_pHeader || !buffer || size == 0 ) {
                return 0;
            }

            Size copy_size = size < m_pHeader->payload_size ? size : m_pHeader->payload_size;
        
            std::memcpy( m_pPayload, buffer, copy_size );

            return copy_size;
        }

        Size Read( Byte* const buffer, Size size ) const noexcept
        {
            if ( !m_pPayload || !m_pHeader || !buffer || size == 0 ) {
                return 0;
            }

            Size copy_size = size < m_pHeader->payload_size ? size : m_pHeader->payload_size;
        
            std::memcpy( buffer, m_pPayload, copy_size );

            return copy_size;
        }

        template < typename T, typename... Args >
        void Emplace( Args&&... args ) noexcept
        {
            static_assert( std::is_base_of_v< Message, T >,
                      "T must derive from Message");

            if ( m_pPayload ) {
                new ( m_pPayload ) T( std::forward<Args>(args)... );
            }
        }

        // template <typename T>
        // void onMessageSend() noexcept
        // {
        //     if constexpr ( std::is_base_of<Message, T>::value ) {
        //         if ( m_pPayload && m_pHeader ) {
        //             m_pPayload->OnMessageSend( static_cast< void* >( m_pPayload ), m_pHeader->payload_size );
        //         }
        //     }
        // }

        // template <typename T>
        // void onMessageReceived() noexcept
        // {
        //     if constexpr ( std::is_base_of<Message, T>::value ) {
        //         if ( m_pPayload && m_pHeader ) {
        //             m_pPayload->OnMessageReceived( static_cast< const void* >( m_pPayload ), m_pHeader->payload_size );
        //         }
        //     }
        // }
        
        /**
         * @brief Check if sample is valid
         * @return true if valid
         */
        inline Bool IsValid() const noexcept
        {
            // return ( m_iChunkIndex != kInvalidChunkIndex )
            //        && ( m_pAllocator != nullptr ) 
            //        && ( m_pHeader != nullptr ) 
            //        && ( m_pPayload != nullptr );
            return ( m_pAllocator != nullptr ) 
                   && ( m_pHeader != nullptr ) 
                   && ( m_pPayload != nullptr );
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
            return m_iChunkIndex;
        }

        /**
         * @brief Release ownership of the chunk without decrementing ref_count
         * @details Used by Publisher::Send to transfer ownership to subscribers
         *          The chunk's ref_count will be managed by subscribers
         */
        void Release() noexcept
        {
            m_pHeader = nullptr;
            m_pPayload = nullptr;
            m_iChunkIndex = kInvalidChunkIndex;
            m_pAllocator = nullptr;
        }

        /**
         * @brief Get current state
         * @return Current chunk state
         */
        inline ChunkState GetState() const noexcept
        {
            DEF_LAP_ASSERT( m_pHeader != nullptr, "ChunkHeader must not be nullptr" );
            return static_cast< ChunkState >( m_pHeader->state.load( std::memory_order_acquire ) );
        }

    protected:
        friend class Publisher;
        friend class Subscriber;

        inline UInt8 FetchAdd( UInt8 delta ) noexcept
        {
            DEF_LAP_ASSERT( m_pHeader != nullptr, "ChunkHeader must not be nullptr" );

            return m_pHeader->ref_count.fetch_add( delta, std::memory_order_acq_rel );
        }

        inline UInt8 IncrementRef() noexcept
        {
            DEF_LAP_ASSERT( m_pHeader != nullptr, "ChunkHeader must not be nullptr" );

            return m_pHeader->ref_count.fetch_add( 1, std::memory_order_acq_rel );
        }

        inline UInt8 DecrementRef() noexcept
        {
            DEF_LAP_ASSERT( m_pHeader != nullptr, "ChunkHeader must not be nullptr" );

            return m_pHeader->ref_count.fetch_sub( 1, std::memory_order_acq_rel );
        }

        /**
         * @brief Transition state atomically
         * @param expected Expected current state
         * @param desired Desired new state
         * @return true if transition succeeded
         */
        Bool TransitionState( ChunkState expected, ChunkState desired ) noexcept
        {
            DEF_LAP_ASSERT( m_pHeader != nullptr, "ChunkHeader must not be nullptr" );

            UInt8 expected_val = static_cast< UInt8 >( expected );
            UInt8 desired_val = static_cast< UInt8 >( desired );
            
            return m_pHeader->state.compare_exchange_strong(
                expected_val,
                desired_val,
                std::memory_order_acq_rel,
                std::memory_order_acquire
            );
        }

        inline ChunkState TransitionState( ChunkState desired ) noexcept
        {
            DEF_LAP_ASSERT( m_pHeader != nullptr, "ChunkHeader must not be nullptr" );

            return static_cast< ChunkState >( m_pHeader->state.exchange( static_cast< UInt8 >( desired ), std::memory_order_acq_rel ) );
        }
    
    private:
        /**
         * @brief Get chunk header
         * @return Chunk header pointer
         */
        inline ChunkHeader* getHeader() noexcept
        {
            return m_pHeader;
        }

        inline ChunkPoolAllocator* getChunkPoolAllocator() noexcept
        {
            return m_pAllocator;
        }
        
    private:
        UInt16                  m_iChunkIndex;   ///< Chunk index
        ChunkPoolAllocator*     m_pAllocator;     ///< Allocator reference
        ChunkHeader*            m_pHeader;        ///< Chunk header
        Byte*                   m_pPayload;       ///< Typed payload pointer
    };
    
}  // namespace ipc
}  // namespace core
}  // namespace lap

#endif  // LAP_CORE_IPC_SAMPLE_HPP
