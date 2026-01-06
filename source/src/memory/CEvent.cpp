/**
 * @file CEvent.cpp
 * @brief Implementation of Event messaging system
 * @author LightAP Team
 * @date 2026-01-02
 */

#include "memory/CEvent.hpp"
#include "CCoreErrorDomain.hpp"

namespace lap::core {

// ============================================================================
// Event Implementation
// ============================================================================

Event::Event(const EventConfig& config) noexcept
    : config_(config)
    , allocator_(nullptr) {
    
    // Create SharedMemoryAllocator with fixed payload size
    allocator_ = std::make_unique<SharedMemoryAllocator>();
    
    // Configure allocator for this event
    SharedMemoryAllocatorConfig alloc_config = GetDefaultSharedMemoryConfig();
    alloc_config.max_chunk_size = config.payload_size;  // Fixed payload size
    alloc_config.chunk_count = config.max_chunks;       // Chunk pool capacity
    alloc_config.max_subscribers = config.max_subscribers;
    alloc_config.subscriber_queue_capacity = config.subscriber_queue_capacity;
    alloc_config.use_shm_for_queue_nodes = config.use_shm_for_queues;
    alloc_config.enable_safe_overflow = false;  // Event uses fixed pool, no overflow
    alloc_config.allocation_policy = AllocationPolicy::ABORT_ON_FULL;  // Fail fast if exhausted
    
    // CRITICAL: Disable segment growth for deterministic pool behavior
    alloc_config.segment_growth_size = 0;  // No dynamic growth
    alloc_config.max_segment_size = 0;     // No growth limit needed
    alloc_config.initial_segments = 1;     // Single fixed segment
    
    if (!allocator_->initialize(alloc_config)) {
        INNER_CORE_LOG("[ERROR] Event '%s': Failed to initialize allocator\n", 
                       config.event_name.c_str());
        allocator_.reset();
    } else {
        INNER_CORE_LOG("[INFO] Event '%s': Initialized (payload=%zu, chunks=%u, subs=%u)\n",
                       config.event_name.c_str(), config.payload_size, 
                       config.max_chunks, config.max_subscribers);
    }
}

Event::~Event() noexcept {
    if (allocator_) {
        INNER_CORE_LOG("[INFO] Event '%s': Destroying\n", config_.event_name.c_str());
        allocator_->uninitialize();
    }
}

std::unique_ptr<EventPublisher> Event::createPublisher() noexcept {
    if (!allocator_ || !allocator_->isInitialized()) {
        INNER_CORE_LOG("[ERROR] Event '%s': Cannot create publisher, not initialized\n",
                       config_.event_name.c_str());
        return nullptr;
    }
    
    PublisherHandle handle;
    auto result = allocator_->createPublisher(handle);
    
    if (!result.HasValue()) {
        INNER_CORE_LOG("[ERROR] Event '%s': Failed to create publisher\n",
                       config_.event_name.c_str());
        return nullptr;
    }
    
    return std::unique_ptr<EventPublisher>(
        new EventPublisher(allocator_.get(), handle, config_.payload_size, config_.event_name)
    );
}

std::unique_ptr<EventSubscriber> Event::createSubscriber() noexcept {
    if (!allocator_ || !allocator_->isInitialized()) {
        INNER_CORE_LOG("[ERROR] Event '%s': Cannot create subscriber, not initialized\n",
                       config_.event_name.c_str());
        return nullptr;
    }
    
    SubscriberHandle handle;
    auto result = allocator_->createSubscriber(handle);
    
    if (!result.HasValue()) {
        INNER_CORE_LOG("[ERROR] Event '%s': Failed to create subscriber\n",
                       config_.event_name.c_str());
        return nullptr;
    }
    
    return std::unique_ptr<EventSubscriber>(
        new EventSubscriber(allocator_.get(), handle, config_.event_name)
    );
}

void Event::getStats(SharedMemoryAllocatorStats& stats) const noexcept {
    if (allocator_) {
        allocator_->getStats(stats);
    }
}

// ============================================================================
// EventPublisher Implementation
// ============================================================================

EventPublisher::EventPublisher(SharedMemoryAllocator* allocator, PublisherHandle handle,
                               Size payload_size, std::string event_name) noexcept
    : allocator_(allocator)
    , handle_(handle)
    , payload_size_(payload_size)
    , event_name_(std::move(event_name)) {
}

EventPublisher::~EventPublisher() noexcept {
    if (allocator_) {
        allocator_->destroyPublisher(handle_);
    }
}

Result<Sample> EventPublisher::loan() noexcept {
    if (!allocator_) {
        return Result<Sample>::FromError(MakeErrorCode(CoreErrc::kNotInitialized));
    }
    
    SharedMemoryMemoryBlock block;
    auto result = allocator_->loan(handle_, payload_size_, block);
    
    if (!result.HasValue()) {
        return Result<Sample>::FromError(result.Error());
    }
    
    return Result<Sample>::FromValue(Sample(block));
}

Result<void> EventPublisher::send(Sample& sample) noexcept {
    if (!allocator_) {
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kNotInitialized));
    }
    
    if (!sample.isValid()) {
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    }
    
    auto result = allocator_->send(handle_, sample.getBlock());
    
    // Sample is now invalid (ownership transferred)
    sample.invalidate();
    
    return result;
}

void EventPublisher::release(Sample& sample) noexcept {
    if (!allocator_ || !sample.isValid()) {
        return;
    }
    
    allocator_->release(sample.getBlock());
    sample.invalidate();
}

const std::string& EventPublisher::getEventName() const noexcept {
    return event_name_;
}

// ============================================================================
// EventSubscriber Implementation
// ============================================================================

EventSubscriber::EventSubscriber(SharedMemoryAllocator* allocator, SubscriberHandle handle,
                                 std::string event_name) noexcept
    : allocator_(allocator)
    , handle_(handle)
    , event_name_(std::move(event_name)) {
}

EventSubscriber::~EventSubscriber() noexcept {
    if (allocator_) {
        allocator_->destroySubscriber(handle_);
    }
}

Result<Sample> EventSubscriber::receive() noexcept {
    if (!allocator_) {
        return Result<Sample>::FromError(MakeErrorCode(CoreErrc::kNotInitialized));
    }
    
    SharedMemoryMemoryBlock block;
    auto result = allocator_->receive(handle_, block);
    
    if (!result.HasValue()) {
        return Result<Sample>::FromError(result.Error());
    }
    
    return Result<Sample>::FromValue(Sample(block));
}

void EventSubscriber::release(Sample& sample) noexcept {
    if (!allocator_ || !sample.isValid()) {
        return;
    }
    
    allocator_->release(handle_, sample.getBlock());
    sample.invalidate();
}

bool EventSubscriber::hasData() const noexcept {
    if (!allocator_) {
        return false;
    }
    
    return allocator_->hasData(handle_);
}

bool EventSubscriber::waitForData(int64_t timeout_us) noexcept {
    if (!allocator_) {
        return false;
    }
    
    return allocator_->waitForData(handle_, timeout_us);
}

const std::string& EventSubscriber::getEventName() const noexcept {
    return event_name_;
}

} // namespace lap::core
