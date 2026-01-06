/**
 * @file CEvent.hpp
 * @brief Event messaging system (iceoryx2-style pub/sub)
 * @author LightAP Team
 * @date 2026-01-02
 * 
 * @details
 * Event-based messaging system inspired by iceoryx2's pub/sub architecture.
 * 
 * Architecture (iceoryx2-style):
 * Service (defined by SOA module)
 *  └── Event (message type, this class)
 *       ├── Chunk Pool (fixed payload size, zero-copy)
 *       ├── Publisher Port (sender)
 *       └── Subscriber Ports (receivers)
 *            └── Each Subscriber has its own Queue
 * 
 * Key features:
 * - Zero-copy message transmission via shared memory
 * - Fixed payload size per Event type (compile-time or runtime)
 * - One Publisher, multiple Subscribers (broadcast)
 * - Lock-free operations for high performance
 * - Compatible with WaitSet for multiplexing
 * 
 * Usage:
 * @code
 * // Create event with 256-byte payload
 * EventConfig config;
 * config.event_name = "sensor_data";
 * config.payload_size = 256;
 * config.max_chunks = 64;
 * config.max_subscribers = 8;
 * 
 * Event event(config);
 * 
 * // Create publisher and subscriber
 * auto publisher = event.createPublisher();
 * auto subscriber = event.createSubscriber();
 * 
 * // Publish message
 * if (auto sample = publisher->loan()) {
 *     memcpy(sample.Value(), sensor_data, 256);
 *     publisher->send(sample.Value());
 * }
 * 
 * // Receive message
 * if (auto sample = subscriber->receive()) {
 *     process(sample.Value());
 *     subscriber->release(sample.Value());
 * }
 * @endcode
 */

#ifndef LAP_CORE_EVENT_HPP
#define LAP_CORE_EVENT_HPP

#include "CTypedef.hpp"
#include "CResult.hpp"
#include "memory/CSharedMemoryAllocator.hpp"
#include <atomic>
#include <memory>
#include <string>

namespace lap::core {

// Forward declarations
class Event;
class EventPublisher;
class EventSubscriber;

/**
 * @brief Sample handle (wraps SharedMemoryMemoryBlock)
 * 
 * RAII wrapper for message samples, ensuring proper cleanup.
 */
class Sample {
public:
    Sample() noexcept : block_{}, valid_(false) {}
    
    explicit Sample(const SharedMemoryMemoryBlock& block) noexcept 
        : block_(block), valid_(true) {}
    
    /**
     * @brief Get raw data pointer
     * @return Pointer to payload data
     */
    void* data() noexcept { return block_.ptr; }
    const void* data() const noexcept { return block_.ptr; }
    
    /**
     * @brief Get payload size
     * @return Size in bytes
     */
    Size size() const noexcept { return block_.size; }
    
    /**
     * @brief Check if sample is valid
     */
    bool isValid() const noexcept { return valid_; }
    
    /**
     * @brief Get internal block (for release)
     */
    SharedMemoryMemoryBlock& getBlock() noexcept { return block_; }
    const SharedMemoryMemoryBlock& getBlock() const noexcept { return block_; }
    
    /**
     * @brief Invalidate sample (after release)
     */
    void invalidate() noexcept { valid_ = false; }

private:
    SharedMemoryMemoryBlock block_;
    bool valid_;
};

/**
 * @brief Event configuration
 */
struct EventConfig {
    std::string event_name;           ///< Event name (identifier)
    Size        payload_size;         ///< Fixed payload size (bytes)
    UInt32      max_chunks;           ///< Chunk pool capacity
    UInt32      max_subscribers;      ///< Max concurrent subscribers
    UInt32      subscriber_queue_capacity; ///< Queue size per subscriber (0=unlimited)
    bool        use_shm_for_queues;   ///< Use shared memory for queue nodes
    
    EventConfig() noexcept
        : event_name("unnamed_event")
        , payload_size(1024)
        , max_chunks(64)
        , max_subscribers(8)
        , subscriber_queue_capacity(16)
        , use_shm_for_queues(false) {}
};

/**
 * @brief Event publisher port (sender)
 * 
 * Publisher can loan samples from the chunk pool, write data,
 * and send to all subscribers.
 * 
 * Thread-safe: One publisher per thread recommended.
 */
class EventPublisher {
public:
    /**
     * @brief Destructor
     */
    ~EventPublisher() noexcept;
    
    /**
     * @brief Loan a sample for writing
     * @return Result with Sample on success, error otherwise
     * 
     * Allocates a chunk from the pool. User must either send() or release().
     */
    Result<Sample> loan() noexcept;
    
    /**
     * @brief Send a sample to all subscribers
     * @param sample Sample to send (will be invalidated)
     * @return Result indicating success or failure
     * 
     * Broadcasts the sample to all active subscribers.
     * Sample becomes invalid after this call.
     */
    Result<void> send(Sample& sample) noexcept;
    
    /**
     * @brief Release a loaned sample without sending
     * @param sample Sample to release
     */
    void release(Sample& sample) noexcept;
    
    /**
     * @brief Get event name
     */
    const std::string& getEventName() const noexcept;

private:
    friend class Event;
    
    /**
     * @brief Private constructor (use Event::createPublisher())
     */
    EventPublisher(SharedMemoryAllocator* allocator, PublisherHandle handle, 
                   Size payload_size, std::string event_name) noexcept;
    
    SharedMemoryAllocator* allocator_;
    PublisherHandle handle_;
    Size payload_size_;
    std::string event_name_;
};

/**
 * @brief Event subscriber port (receiver)
 * 
 * Subscriber receives samples from publisher via its own queue.
 * Each subscriber has an independent queue for broadcast semantics.
 * 
 * Thread-safe: One subscriber per thread recommended.
 */
class EventSubscriber {
public:
    /**
     * @brief Destructor
     */
    ~EventSubscriber() noexcept;
    
    /**
     * @brief Receive a sample (non-blocking)
     * @return Result with Sample if available, error otherwise
     * 
     * Dequeues a sample from this subscriber's queue.
     * User must call release() after processing.
     */
    Result<Sample> receive() noexcept;
    
    /**
     * @brief Release a received sample
     * @param sample Sample to release
     * 
     * Decrements reference count. Sample is returned to pool
     * when all subscribers have released it.
     */
    void release(Sample& sample) noexcept;
    
    /**
     * @brief Check if data is available (non-blocking peek)
     * @return true if queue has samples
     */
    bool hasData() const noexcept;
    
    /**
     * @brief Wait for data (blocking)
     * @param timeout_us Timeout in microseconds (-1=infinite, 0=poll)
     * @return true if data became available, false on timeout
     */
    bool waitForData(int64_t timeout_us = -1) noexcept;
    
    /**
     * @brief Get event name
     */
    const std::string& getEventName() const noexcept;

private:
    friend class Event;
    
    /**
     * @brief Private constructor (use Event::createSubscriber())
     */
    EventSubscriber(SharedMemoryAllocator* allocator, SubscriberHandle handle,
                    std::string event_name) noexcept;
    
    SharedMemoryAllocator* allocator_;
    SubscriberHandle handle_;
    std::string event_name_;
};

/**
 * @brief Event (iceoryx2-style message type)
 * 
 * Event represents a typed message channel with:
 * - Fixed payload size (compile-time or runtime)
 * - Dedicated chunk pool for zero-copy
 * - Publisher and Subscriber ports
 * - Broadcast semantics (1:N)
 * 
 * Architecture:
 *   Event
 *    ├── SharedMemoryAllocator (chunk pool)
 *    ├── 1 Publisher Port
 *    └── N Subscriber Ports (each with queue)
 * 
 * Thread-safety:
 * - Event itself is thread-safe for port creation
 * - Ports should be used by single threads
 */
class Event {
public:
    /**
     * @brief Constructor
     * @param config Event configuration
     * 
     * Initializes chunk pool with fixed payload size.
     */
    explicit Event(const EventConfig& config) noexcept;
    
    /**
     * @brief Destructor
     */
    ~Event() noexcept;
    
    /**
     * @brief Create a publisher port
     * @return Unique pointer to EventPublisher, or nullptr on failure
     * 
     * Typically only one publisher per Event.
     * Multiple publishers are supported but not common.
     */
    std::unique_ptr<EventPublisher> createPublisher() noexcept;
    
    /**
     * @brief Create a subscriber port
     * @return Unique pointer to EventSubscriber, or nullptr on failure
     * 
     * Multiple subscribers supported (broadcast).
     * Each subscriber gets its own queue.
     */
    std::unique_ptr<EventSubscriber> createSubscriber() noexcept;
    
    /**
     * @brief Get event name
     */
    const std::string& getName() const noexcept { return config_.event_name; }
    
    /**
     * @brief Get payload size
     */
    Size getPayloadSize() const noexcept { return config_.payload_size; }
    
    /**
     * @brief Get statistics
     */
    void getStats(SharedMemoryAllocatorStats& stats) const noexcept;
    
    /**
     * @brief Check if initialized
     */
    bool isInitialized() const noexcept { 
        return allocator_ && allocator_->isInitialized(); 
    }

private:
    EventConfig config_;
    std::unique_ptr<SharedMemoryAllocator> allocator_;
};

} // namespace lap::core

#endif // LAP_CORE_EVENT_HPP
