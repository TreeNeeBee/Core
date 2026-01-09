/**
 * @file        Publisher.cpp
 * @author      LightAP Core Team
 * @brief       Implementation of Publisher
 * @date        2026-01-07
 * @copyright   Copyright (c) 2026
 */

#include "ipc/Publisher.hpp"
#include "ipc/WaitSetHelper.hpp"
#include "ipc/Message.hpp"
#include "ipc/SubscriberRegistryOps.hpp"
#include "CCoreErrorDomain.hpp"
#include "CMacroDefine.hpp"
#include <new>
#include <cstring>
#include <iostream>

namespace lap
{
namespace core
{
namespace ipc
{
    template <typename T>
    Result<Publisher<T>> Publisher<T>::Create(const String& service_name,
                                              const PublisherConfig& config) noexcept
    {
        // Create shared memory manager
        auto shm = std::make_unique<SharedMemoryManager>();
        
        SharedMemoryConfig shm_config{};
        shm_config.max_chunks = config.max_chunks;
        
        // Determine chunk size (payload size, not including ChunkHeader)
        UInt64 msg_size;
        if ( config.chunk_size == 0 ) {
            if constexpr (std::is_base_of<Message, T>::value) {
                // T is Message or derived from Message - use GetSize()
                T temp_msg{};
                msg_size = temp_msg.GetSize();
                INNER_CORE_LOG( "[DEBUG] Publisher::Create using GetSize()=%lu", msg_size );
            } else {
                // T is a plain type - use sizeof
                msg_size = sizeof(T);
                INNER_CORE_LOG( "[DEBUG] Publisher::Create plain type, msg_size=%lu", msg_size );
            }
        } else {
            msg_size = config.chunk_size;
            INNER_CORE_LOG( "[DEBUG] Publisher::Create using config.chunk_size=%lu", msg_size );
        }
        shm_config.chunk_size = msg_size;  // Payload size only
        INNER_CORE_LOG( "[DEBUG] Publisher::Create final shm_config.chunk_size=%lu", shm_config.chunk_size );
        shm_config.auto_cleanup = config.auto_cleanup;
        
        auto result = shm->Create(service_name, shm_config);
        if (!result)
        {
            return Result<Publisher<T>>(result.Error());
        }
        
        // Create chunk pool allocator
        auto allocator = std::make_unique<ChunkPoolAllocator>(
            shm->GetBaseAddress(),
            shm->GetControlBlock()
        );
        
        // Initialize if we are the creator
        if (shm->IsCreator())
        {
            auto init_result = allocator->Initialize();
            if (!init_result)
            {
                return Result<Publisher<T>>(init_result.Error());
            }
        }
        
        // Create publisher
        Publisher<T> publisher(service_name, config, std::move(shm), 
                              std::move(allocator));
        
        // Increment publisher count
        publisher.shm_->GetControlBlock()->publisher_count.fetch_add(1, std::memory_order_relaxed);
        
        return Result<Publisher<T>>(std::move(publisher));
    }
    
    template <typename T>
    Result<Sample<T>> Publisher<T>::Loan() noexcept
    {
        // Try to allocate chunk
        auto chunk_index = allocator_->Allocate();
        
        if (!chunk_index.has_value())
        {
            // Trigger hook: Loan failed
            if (event_hooks_)
            {
                event_hooks_->OnLoanFailed(
                    service_name_.c_str(),
                    config_.loan_policy,
                    allocator_->GetAllocatedCount(),
                    allocator_->GetMaxChunks()
                );
            }
            
            // Pool exhausted - handle based on policy
            if (config_.loan_policy == LoanFailurePolicy::kError)
            {
                if (event_hooks_)
                {
                    event_hooks_->OnChunkPoolExhausted(
                        service_name_.c_str(),
                        allocator_->GetMaxChunks()
                    );
                }
                return Result<Sample<T>>(MakeErrorCode(CoreErrc::kIPCChunkPoolExhausted));
            }
            else if (config_.loan_policy == LoanFailurePolicy::kWait)
            {
                // Busy-wait polling for free chunk
                Duration timeout = Duration(10000000);  // 10ms default
                bool success = WaitSetHelper::PollForFlags(
                    &shm_->GetControlBlock()->loan_waitset,
                    EventFlag::kHasFreeChunk,
                    timeout
                );
                
                if (!success)
                {
                    return Result<Sample<T>>(MakeErrorCode(CoreErrc::kIPCChunkPoolExhausted));
                }
                
                // Retry allocation
                chunk_index = allocator_->Allocate();
                if (!chunk_index.has_value())
                {
                    return Result<Sample<T>>(MakeErrorCode(CoreErrc::kIPCChunkPoolExhausted));
                }
            }
            else if (config_.loan_policy == LoanFailurePolicy::kBlock)
            {
                // Block on futex until chunk available
                Duration timeout = Duration(100000000);  // 100ms default
                auto wait_result = WaitSetHelper::WaitForFlags(
                    &shm_->GetControlBlock()->loan_waitset,
                    EventFlag::kHasFreeChunk,
                    timeout
                );
                
                if (!wait_result)
                {
                    return Result<Sample<T>>(MakeErrorCode(CoreErrc::kIPCChunkPoolExhausted));
                }
                
                // Retry allocation
                chunk_index = allocator_->Allocate();
                if (!chunk_index.has_value())
                {
                    return Result<Sample<T>>(MakeErrorCode(CoreErrc::kIPCChunkPoolExhausted));
                }
            }
        }
        
        // Create Sample wrapper
        Sample<T> sample(allocator_.get(), chunk_index.value());
        
        // Initialize reference count to 1 (loaned state)
        auto* header = sample.GetHeader();
        if (header)
        {
            header->ref_count.store(1, std::memory_order_release);
        }
        
        return Result<Sample<T>>(std::move(sample));
    }
    
    template <typename T>
    Result<void> Publisher<T>::Send(Sample<T>&& sample,
                                        QueueFullPolicy /* policy */) noexcept
    {
        if (!sample.IsValid())
        {
            return Result<void>(MakeErrorCode(CoreErrc::kInvalidArgument));
        }
        
        auto* header = sample.GetHeader();
        UInt32 chunk_index = sample.GetChunkIndex();
        
        // Transition state: Loaned -> Sent
        if (!header->TransitionState(ChunkState::kLoaned, ChunkState::kSent))
        {
            return Result<void>(MakeErrorCode(CoreErrc::kIPCInvalidState));
        }
        
        // Get snapshot of subscribers from shared memory registry
        auto snapshot = GetSubscriberSnapshot(shm_->GetControlBlock());
        
        INNER_CORE_LOG("[DEBUG] Publisher::Send - snapshot.count=%u\n", snapshot.count);
        
        if (snapshot.count == 0)
        {
            // No subscribers - release chunk directly back to pool
            // Since Sample::Release() no longer decrements ref_count,
            // we need to manually deallocate
            allocator_->Deallocate(chunk_index);
            sample.Release();  // Release Sample's ownership
            return {};
        }
        
        // Set initial reference count to number of subscribers
        header->ref_count.store(snapshot.count, std::memory_order_release);
        
        // Enqueue to all subscriber queues (broadcast)
        UInt32 successful_sends = 0;
        
        for (UInt32 i = 0; i < snapshot.count; ++i)
        {
            UInt32 queue_index = snapshot.queue_indices[i];
            
            INNER_CORE_LOG("[DEBUG] Publisher::Send - Sending to queue_index=%u\n", queue_index);
            
            // Get subscriber queue from shared memory
            auto* queue = shm_->GetSubscriberQueue(queue_index);
            if (!queue || !queue->active.load(std::memory_order_acquire))
            {
                INNER_CORE_LOG("[DEBUG] Publisher::Send - Queue %u not active\n", queue_index);
                // Queue not available, decrement ref count
                header->ref_count.fetch_sub(1, std::memory_order_relaxed);
                continue;
            }
            
            // SPSC enqueue (Publisher is producer)
            UInt32 tail = queue->tail.load(std::memory_order_relaxed);
            UInt32 next_tail = (tail + 1) & (queue->capacity - 1);
            
            INNER_CORE_LOG("[DEBUG] Publisher::Send - Queue %u: tail=%u, next_tail=%u\n", 
                           queue_index, tail, next_tail);
            
            // Check if queue is full
            UInt32 head = queue->head.load(std::memory_order_acquire);
            if (next_tail == head)
            {
                // Queue full - handle based on policy
                QueueFullPolicy queue_policy = static_cast<QueueFullPolicy>(
                    queue->queue_full_policy.load(std::memory_order_acquire)
                );
                
                if (queue_policy == QueueFullPolicy::kOverwrite)
                {
                    // Overwrite oldest message (move head forward)
                    UInt32 new_head = (head + 1) & (queue->capacity - 1);
                    queue->head.store(new_head, std::memory_order_release);
                    queue->overrun_count.fetch_add(1, std::memory_order_relaxed);
                    
                    // Now we have space, continue to enqueue
                }
                else
                {
                    // Drop message
                    header->ref_count.fetch_sub(1, std::memory_order_relaxed);
                    
                    // Trigger hook: Queue full
                    if (event_hooks_)
                    {
                        QueueFullPolicy full_policy = static_cast<QueueFullPolicy>(
                            queue->queue_full_policy.load(std::memory_order_acquire)
                        );
                        event_hooks_->OnQueueFull(service_name_.c_str(), queue_index, full_policy);
                    }
                    
                    continue;
                }
            }
            
            // Write chunk_index to buffer
            UInt32* buffer = queue->GetBuffer();
            buffer[tail] = chunk_index;
            
            INNER_CORE_LOG("[DEBUG] Publisher::Send - Wrote chunk_index=%u to buffer[%u]\n", 
                           chunk_index, tail);
            
            // Update tail (release semantics for consumer visibility)
            queue->tail.store(next_tail, std::memory_order_release);
            
            INNER_CORE_LOG("[DEBUG] Publisher::Send - Updated tail to %u\n", next_tail);
            
            successful_sends++;
        }
        
        // Release Sample's ownership (Sample won't decrement ref_count on destruction)
        // The ref_count is now managed by subscribers
        sample.Release();
        
        // NOTE: Publisher does NOT decrement ref_count here!
        // The ref_count was set to snapshot.count, and each Subscriber
        // will decrement it when they receive. The last Subscriber
        // will trigger deallocation when ref_count reaches 0.
        
        // Trigger hook: Message sent
        if (event_hooks_)
        {
            event_hooks_->OnMessageSent(
                service_name_.c_str(),
                chunk_index,
                snapshot.count
            );
        }
        
        return {};
    }
    
    template <typename T>
    Result<void> Publisher<T>::SendCopy(const T& data,
                                       QueueFullPolicy policy) noexcept
    {
        auto sample_result = Loan();
        if (!sample_result)
        {
            return Result<void>(sample_result.Error());
        }
        
        auto sample = std::move(sample_result).Value();
        
        // Copy data to chunk using assignment operator or placement new
        *sample.Get() = data;
        
        return Send(std::move(sample), policy);
    }
    
    template <typename T>
    template <typename... Args>
    Result<void> Publisher<T>::SendEmplace(QueueFullPolicy policy, Args&&... args) noexcept
    {
        auto sample_result = Loan();
        if (!sample_result)
        {
            return Result<void>(sample_result.Error());
        }
        
        auto sample = std::move(sample_result).Value();
        
        // Construct in-place
        new (sample.Get()) T(std::forward<Args>(args)...);
        
        return Send(std::move(sample), policy);
    }
    
    template <typename T>
    UInt32 Publisher<T>::GetAllocatedCount() const noexcept
    {
        return allocator_->GetAllocatedCount();
    }
    
    template <typename T>
    bool Publisher<T>::IsChunkPoolExhausted() const noexcept
    {
        return allocator_->IsExhausted();
    }
    
    // Explicit template instantiation for common types
    template class Publisher<UInt8>;
    template class Publisher<UInt32>;
    template class Publisher<UInt64>;
    
    // Explicit instantiation for Message base class
    template class Publisher<Message>;
    
}  // namespace ipc
}  // namespace core
}  // namespace lap
