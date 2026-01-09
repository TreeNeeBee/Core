/**
 * @file        Subscriber.cpp
 * @author      LightAP Core Team
 * @brief       Implementation of Subscriber
 * @date        2026-01-07
 * @copyright   Copyright (c) 2026
 */

#include "ipc/Subscriber.hpp"
#include "ipc/Message.hpp"
#include "ipc/SubscriberRegistryOps.hpp"
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
    template <typename T>
    Result<Subscriber<T>> Subscriber<T>::Create(const String& service_name,
                                                const SubscriberConfig& config) noexcept
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
            } else {
                // T is a plain type - use sizeof
                msg_size = sizeof(T);
            }
        } else {
            msg_size = config.chunk_size;
        }
        shm_config.chunk_size = msg_size;  // Payload size only
        
        INNER_CORE_LOG("[DEBUG] Subscriber::Create - Opening shared memory: %s\n", service_name.c_str());
        auto result = shm->Create(service_name, shm_config);
        if (!result)
        {
            INNER_CORE_LOG("[DEBUG] Failed to create/open shared memory\n");
            return Result<Subscriber<T>>(result.Error());
        }
        INNER_CORE_LOG("[DEBUG] Shared memory opened, IsCreator=%d\n", shm->IsCreator());
        
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
                return Result<Subscriber<T>>(init_result.Error());
            }
        }
        
        // Allocate unique queue index from shared memory control block
        UInt32 queue_index = AllocateQueueIndex(shm->GetControlBlock());
        INNER_CORE_LOG("[DEBUG] Allocated queue_index=%u\n", queue_index);
        if (queue_index == kInvalidChunkIndex)
        {
            INNER_CORE_LOG("[DEBUG] Failed to allocate queue index\n");
            return Result<Subscriber<T>>(MakeErrorCode(CoreErrc::kResourceExhausted));
        }
        
        // Get the allocated queue from shared memory
        auto* queue = shm->GetSubscriberQueue(queue_index);
        if (!queue)
        {
            INNER_CORE_LOG("[DEBUG] GetSubscriberQueue returned nullptr for index %u\n", queue_index);
            return Result<Subscriber<T>>(MakeErrorCode(CoreErrc::kIPCShmNotFound));
        }
        INNER_CORE_LOG("[DEBUG] Got queue pointer, checking active status...\n");
        
        // Activate the queue
        bool expected = false;
        INNER_CORE_LOG("[DEBUG] Queue[%u] current active state: %d\n", 
                       queue_index, queue->active.load(std::memory_order_acquire));
        
        if (!queue->active.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        {
            INNER_CORE_LOG("[DEBUG] Failed to activate queue (CAS failed, expected=false, actual=%d)\n", expected);
            return Result<Subscriber<T>>(MakeErrorCode(CoreErrc::kResourceExhausted));
        }
        INNER_CORE_LOG("[DEBUG] Queue activated successfully\n");
        
        // Generate unique subscriber ID (simple hash of service name + queue_index)
        std::hash<String> hasher;
        UInt32 subscriber_id = static_cast<UInt32>(hasher(service_name) ^ queue_index);
        queue->subscriber_id = subscriber_id;
        
        // Create subscriber
        Subscriber<T> subscriber(service_name, config, std::move(shm), 
                                std::move(allocator), queue_index, subscriber_id);
        
        INNER_CORE_LOG("[DEBUG] Registering subscriber with queue_index=%u\n", queue_index);
        // Register with shared memory registry
        if (!RegisterSubscriber(subscriber.shm_->GetControlBlock(), queue_index))
        {
            INNER_CORE_LOG("[DEBUG] Failed to register subscriber in registry\n");
            return Result<Subscriber<T>>(MakeErrorCode(CoreErrc::kResourceExhausted));
        }
        INNER_CORE_LOG("[DEBUG] Subscriber registered successfully\n");
        
        // Increment subscriber count
        subscriber.shm_->GetControlBlock()->subscriber_count.fetch_add(1, std::memory_order_relaxed);
        INNER_CORE_LOG("[DEBUG] Subscriber creation complete\n");
        
        return Result<Subscriber<T>>(std::move(subscriber));
    }
    
    template <typename T>
    Subscriber<T>::~Subscriber() noexcept
    {
        // Automatically disconnect on destruction
        (void)Disconnect();
    }
    
    template <typename T>
    Result<void> Subscriber<T>::Disconnect() noexcept
    {
        if (is_disconnected_ || !shm_)
        {
            is_disconnected_ = true;
            return {};  // Already disconnected or not initialized
        }
        
        // ====== Step 1: Unregister from SubscriberRegistry ======
        bool unregister_success = UnregisterSubscriber(
            shm_->GetControlBlock(), 
            queue_index_
        );
        
        if (!unregister_success)
        {
            // Already unregistered, continue cleanup
        }
        
        // ====== Step 2: Consume remaining messages in queue ======
        // This ensures ref_count is properly decremented
        UInt32 consumed_count = 0;
        auto* queue = shm_->GetSubscriberQueue(queue_index_);
        
        if (queue)
        {
            while (!IsQueueEmpty())
            {
                auto sample_result = Receive(QueueEmptyPolicy::kError);
                if (sample_result.HasValue())
                {
                    // Sample destructor will decrement ref_count automatically
                    consumed_count++;
                }
                else
                {
                    break;  // Queue empty or error
                }
            }
            
            // ====== Step 3: Deactivate queue slot ======
            queue->active.store(false, std::memory_order_release);
        }
        
        // Decrement subscriber count
        shm_->GetControlBlock()->subscriber_count.fetch_sub(1, std::memory_order_relaxed);
        
        is_disconnected_ = true;
        return {};
    }
    
    template <typename T>
    Result<Sample<T>> Subscriber<T>::Receive(QueueEmptyPolicy policy) noexcept
    {
        // Get queue from shared memory
        auto* queue = shm_->GetSubscriberQueue(queue_index_);
        if (!queue || !queue->active.load(std::memory_order_acquire))
        {
            INNER_CORE_LOG("[DEBUG] Subscriber::Receive - Queue %u not active or nullptr\n", queue_index_);
            return Result<Sample<T>>(MakeErrorCode(CoreErrc::kIPCShmNotFound));
        }
        
        // Try to dequeue chunk index (SPSC consumer side)
        UInt32 head = queue->head.load(std::memory_order_relaxed);
        UInt32 tail = queue->tail.load(std::memory_order_acquire);
        
        INNER_CORE_LOG("[DEBUG] Subscriber::Receive - Queue %u: head=%u, tail=%u\n", 
                       queue_index_, head, tail);
        
        if ( head == tail ) {
            // Queue empty - handle based on policy
            if ( DEF_LAP_IF_LIKELY( policy == QueueEmptyPolicy::kBlock ) ) {
                // Block waiting for data
                Duration timeout = Duration(100000000);  // 100ms default
                
                auto start = std::chrono::steady_clock::now();
                while (true)
                {
                    head = queue->head.load(std::memory_order_relaxed);
                    tail = queue->tail.load(std::memory_order_acquire);
                    
                    if (head != tail) { break; }
                    
                    auto elapsed = std::chrono::steady_clock::now() - start;
                    if (std::chrono::duration_cast<Duration>(elapsed).count() >= timeout.count()) {
                        return Result<Sample<T>>(MakeErrorCode(CoreErrc::kIPCQueueEmpty));
                    }
                    
                    // Sleep briefly to avoid busy-waiting
                    std::this_thread::sleep_for( std::chrono::microseconds(100) );
                }
            } else if (policy == QueueEmptyPolicy::kWait) {
                // Busy-wait polling for data
                Duration timeout = Duration(10000000);  // 10ms default
                
                auto start = std::chrono::steady_clock::now();
                while (true)
                {
                    head = queue->head.load(std::memory_order_relaxed);
                    tail = queue->tail.load(std::memory_order_acquire);
                    
                    if (head != tail) { break; }
                    
                    auto elapsed = std::chrono::steady_clock::now() - start;
                    if (std::chrono::duration_cast<Duration>(elapsed).count() >= timeout.count())
                    {
                        return Result<Sample<T>>(MakeErrorCode(CoreErrc::kIPCQueueEmpty));
                    }
                    
                    ::std::this_thread::yield();  // Yield to reduce CPU usage
                }
            } else if ( policy == QueueEmptyPolicy::kSkip ) {
                return Result<Sample<T>>(MakeErrorCode(CoreErrc::kWouldBlock));
            } else if ( policy == QueueEmptyPolicy::kError ) {
                return Result<Sample<T>>(MakeErrorCode(CoreErrc::kIPCQueueEmpty));
            } else {
                // Unknown policy
                return Result<Sample<T>>(MakeErrorCode(CoreErrc::kInvalidArgument));
            }
        }
        
        // Dequeue chunk_index
        UInt32* buffer = queue->GetBuffer();
        UInt32 chunk_index = buffer[head];
        
        INNER_CORE_LOG("[DEBUG] Subscriber::Receive - Dequeued chunk_index=%u from buffer[%u]\n", 
                       chunk_index, head);
        
        // Update head (release semantics for producer visibility)
        UInt32 next_head = (head + 1) & (queue->capacity - 1);
        queue->head.store(next_head, std::memory_order_release);
        
        INNER_CORE_LOG("[DEBUG] Subscriber::Receive - Updated head to %u\n", next_head);
        
        // Get chunk header
        auto* header = allocator_->GetChunkHeader(chunk_index);
        if (!header)
        {
            INNER_CORE_LOG("[DEBUG] Subscriber::Receive - GetChunkHeader returned nullptr for chunk %u\n", chunk_index);
            return Result<Sample<T>>(MakeErrorCode(CoreErrc::kIPCInvalidChunkIndex));
        }
        
        // In broadcast mode (SPMC), multiple subscribers receive the same chunk
        // The state can be kSent (first receiver) or kReceived (subsequent receivers)
        // We just ensure it's in a valid state for receiving
        ChunkState current_state = static_cast<ChunkState>(header->state.load(std::memory_order_acquire));
        
        if (current_state != ChunkState::kSent && current_state != ChunkState::kReceived)
        {
            INNER_CORE_LOG("[DEBUG] Subscriber::Receive - Invalid state %u for chunk %u\n", 
                           static_cast<UInt32>(current_state), chunk_index);
            return Result<Sample<T>>(MakeErrorCode(CoreErrc::kIPCInvalidState));
        }
        
        // Set state to kReceived (idempotent - may already be kReceived from another subscriber)
        header->state.store(static_cast<UInt32>(ChunkState::kReceived), std::memory_order_release);
        
        INNER_CORE_LOG("[DEBUG] Subscriber::Receive - Successfully received chunk %u\n", chunk_index);
        
        // Create Sample wrapper
        Sample<T> sample(allocator_.get(), chunk_index);
        
        // Trigger hook: Message received
        if (event_hooks_)
        {
            event_hooks_->OnMessageReceived(
                service_name_.c_str(),
                chunk_index
            );
        }
        
        return Result<Sample<T>>(std::move(sample));
    }
    
    template <typename T>
    Result<Sample<T>> Subscriber<T>::ReceiveWithTimeout(Duration /* timeout */) noexcept
    {
        // TODO: Implement timeout using WaitSet mechanism
        // For now, just try non-blocking receive
        return Receive(QueueEmptyPolicy::kSkip);
    }
    
    template <typename T>
    bool Subscriber<T>::IsQueueEmpty() const noexcept
    {
        auto* queue = shm_->GetSubscriberQueue(queue_index_);
        if (!queue) return true;
        
        UInt32 head = queue->head.load(std::memory_order_acquire);
        UInt32 tail = queue->tail.load(std::memory_order_acquire);
        return head == tail;
    }
    
    template <typename T>
    UInt32 Subscriber<T>::GetQueueSize() const noexcept
    {
        auto* queue = shm_->GetSubscriberQueue(queue_index_);
        if (!queue) return 0;
        
        UInt32 head = queue->head.load(std::memory_order_acquire);
        UInt32 tail = queue->tail.load(std::memory_order_acquire);
        
        if (tail >= head)
        {
            return tail - head;
        }
        else
        {
            return queue->capacity - head + tail;
        }
    }
    
    // Explicit template instantiation for common types
    template class Subscriber<UInt8>;
    template class Subscriber<UInt32>;
    template class Subscriber<UInt64>;
    template class Subscriber<Message>;
    
}  // namespace ipc
}  // namespace core
}  // namespace lap
