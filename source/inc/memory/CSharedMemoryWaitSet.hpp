/**
 * @file CSharedMemoryWaitSet.hpp
 * @brief WaitSet for multiplexing multiple subscribers (iceoryx2-style)
 * @author LightAP Team
 * @date 2025-12-29
 */

#ifndef LAP_CORE_SHARED_MEMORY_WAITSET_HPP
#define LAP_CORE_SHARED_MEMORY_WAITSET_HPP

#include "CTypedef.hpp"
#include <mutex>
#include <condition_variable>
#include <vector>
#include <atomic>

namespace lap::core {

// Forward declaration
struct SubscriberHandle;
class SharedMemoryAllocator;

/**
 * @brief WaitSet for efficient multiplexing of multiple subscribers
 * 
 * Similar to epoll for sockets, but designed for shared memory IPC.
 * Allows waiting on multiple subscribers simultaneously using condition variables.
 * 
 * Usage:
 * @code
 * auto waitset = allocator.createWaitSet();
 * waitset->attach(sub1);
 * waitset->attach(sub2);
 * 
 * while (running) {
 *     auto ready = waitset->wait(1000000); // 1 second timeout
 *     for (auto& sub : ready) {
 *         SharedMemoryMemoryBlock block;
 *         allocator.receive(sub, block);
 *         // process...
 *         allocator.release(sub, block);
 *     }
 * }
 * @endcode
 */
class WaitSet {
public:
    WaitSet() = default;
    ~WaitSet() = default;

    /**
     * @brief Attach a subscriber to this WaitSet
     * @param subscriber Subscriber handle to monitor
     * @return true if attached successfully
     */
    bool attach(const SubscriberHandle& subscriber) noexcept;

    /**
     * @brief Detach a subscriber from this WaitSet
     * @param subscriber Subscriber handle to remove
     */
    void detach(const SubscriberHandle& subscriber) noexcept;

    /**
     * @brief Wait for data on any attached subscriber
     * 
     * Blocks until at least one attached subscriber has data available,
     * or until timeout expires.
     * 
     * @param timeout_us Timeout in microseconds (0=poll, -1=infinite)
     * @return Vector of subscribers with data ready
     */
    std::vector<SubscriberHandle> wait(int64_t timeout_us = -1) noexcept;

    /**
     * @brief Get number of attached subscribers
     */
    size_t size() const noexcept { return subscribers_.size(); }

    /**
     * @brief Clear all attached subscribers
     */
    void clear() noexcept { subscribers_.clear(); }

    /**
     * @brief Internal notification from allocator (called by send())
     */
    void notify() noexcept;

private:
    std::vector<SubscriberHandle> subscribers_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> data_available_{false};
    
    // Need allocator reference to check hasData()
    SharedMemoryAllocator* allocator_{nullptr};
    
    friend class SharedMemoryAllocator;  // Allow setting allocator_
};

} // namespace lap::core

#endif // LAP_CORE_SHARED_MEMORY_WAITSET_HPP
