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
#include <utility>

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
    template <typename T>
    class Sample
    {
    public:
        /**
         * @brief Constructor
         * @param allocator Chunk pool allocator
         * @param chunk_index Chunk index
         */
        Sample(ChunkPoolAllocator* allocator, UInt32 chunk_index) noexcept
            : allocator_(allocator)
            , chunk_index_(chunk_index)
            , header_(nullptr)
            , payload_(nullptr)
        {
            if (allocator_ && chunk_index_ != kInvalidChunkIndex)
            {
                header_ = allocator_->GetChunkHeader(chunk_index_);
                if (header_)
                {
                    payload_ = static_cast<T*>(header_->GetPayload());
                }
            }
        }
        
        /**
         * @brief Destructor - decrements ref count and releases chunk if last reference
         */
        ~Sample() noexcept
        {
            if (header_ && allocator_ && chunk_index_ != kInvalidChunkIndex)
            {
                // Decrement reference count
                UInt64 new_count = header_->DecrementRef();
                
                // If ref count reaches 0, return to pool
                if (new_count == 0)
                {
                    allocator_->Deallocate(chunk_index_);
                }
            }
        }
        
        // Delete copy
        Sample(const Sample&) = delete;
        Sample& operator=(const Sample&) = delete;
        
        /**
         * @brief Move constructor
         */
        Sample(Sample&& other) noexcept
            : allocator_(other.allocator_)
            , chunk_index_(other.chunk_index_)
            , header_(other.header_)
            , payload_(other.payload_)
        {
            other.allocator_ = nullptr;
            other.chunk_index_ = kInvalidChunkIndex;
            other.header_ = nullptr;
            other.payload_ = nullptr;
        }
        
        /**
         * @brief Move assignment
         */
        Sample& operator=(Sample&& other) noexcept
        {
            if (this != &other)
            {
                // Decrement ref count for current chunk before overwriting
                if (header_ && allocator_ && chunk_index_ != kInvalidChunkIndex)
                {
                    UInt64 new_count = header_->DecrementRef();
                    if (new_count == 0)
                    {
                        allocator_->Deallocate(chunk_index_);
                    }
                }
                
                allocator_ = other.allocator_;
                chunk_index_ = other.chunk_index_;
                header_ = other.header_;
                payload_ = other.payload_;
                
                other.allocator_ = nullptr;
                other.chunk_index_ = kInvalidChunkIndex;
                other.header_ = nullptr;
                other.payload_ = nullptr;
            }
            return *this;
        }
        
        /**
         * @brief Get payload pointer
         * @return Payload pointer (nullptr if invalid)
         */
        T* Get() noexcept
        {
            return payload_;
        }
        
        /**
         * @brief Get const payload pointer
         * @return Const payload pointer
         */
        const T* Get() const noexcept
        {
            return payload_;
        }
        
        /**
         * @brief Dereference operator
         */
        T& operator*() noexcept
        {
            return *payload_;
        }
        
        /**
         * @brief Const dereference operator
         */
        const T& operator*() const noexcept
        {
            return *payload_;
        }
        
        /**
         * @brief Arrow operator
         */
        T* operator->() noexcept
        {
            return payload_;
        }
        
        /**
         * @brief Const arrow operator
         */
        const T* operator->() const noexcept
        {
            return payload_;
        }
        
        /**
         * @brief Check if sample is valid
         * @return true if valid
         */
        bool IsValid() const noexcept
        {
            return payload_ != nullptr;
        }
        
        /**
         * @brief Boolean conversion
         */
        explicit operator bool() const noexcept
        {
            return IsValid();
        }
        
        /**
         * @brief Get chunk index
         * @return Chunk index
         */
        UInt32 GetChunkIndex() const noexcept
        {
            return chunk_index_;
        }
        
        /**
         * @brief Get chunk header
         * @return Chunk header pointer
         */
        ChunkHeader* GetHeader() noexcept
        {
            return header_;
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
        
    private:
        ChunkPoolAllocator* allocator_;  ///< Allocator reference
        UInt32 chunk_index_;             ///< Chunk index
        ChunkHeader* header_;            ///< Chunk header
        T* payload_;                     ///< Typed payload pointer
    };
    
}  // namespace ipc
}  // namespace core
}  // namespace lap

#endif  // LAP_CORE_IPC_SAMPLE_HPP
