/**
 * @file CSharedMemoryWaitSet.cpp
 * @brief WaitSet implementation for multiplexing subscribers
 * @author LightAP Team
 * @date 2025-12-29
 */

#include "CSharedMemoryWaitSet.hpp"
#include "CSharedMemoryAllocator.hpp"
#include <chrono>
#include <algorithm>

namespace lap::core {
// We only access the condition variable through the interface

bool WaitSet::attach(const SubscriberHandle& subscriber) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check if already attached
    for (const auto& sub : subscribers_) {
        if (sub.subscriber_id == subscriber.subscriber_id) {
            return false;  // Already attached
        }
    }
    
    subscribers_.push_back(subscriber);
    return true;
}

void WaitSet::detach(const SubscriberHandle& subscriber) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = std::remove_if(subscribers_.begin(), subscribers_.end(),
        [&](const SubscriberHandle& sub) {
            return sub.subscriber_id == subscriber.subscriber_id;
        });
    
    subscribers_.erase(it, subscribers_.end());
}

std::vector<SubscriberHandle> WaitSet::wait(int64_t timeout_us) noexcept {
    std::vector<SubscriberHandle> ready;
    
    if (!allocator_) {
        return ready;  // No allocator set
    }
    
    std::unique_lock<std::mutex> lock(mutex_);
    
    // Poll mode (timeout == 0)
    if (timeout_us == 0) {
        // Check each subscriber's queue immediately
        for (const auto& sub : subscribers_) {
            if (allocator_->hasData(sub)) {
                ready.push_back(sub);
            }
        }
        return ready;
    }
    
    // Blocking wait with condition variable
    auto wait_until_ready = [&]() {
        for (const auto& sub : subscribers_) {
            if (allocator_->hasData(sub)) {
                return true;
            }
        }
        return false;
    };
    
    if (timeout_us < 0) {
        // Infinite wait
        cv_.wait(lock, wait_until_ready);
    } else {
        // Timed wait
        auto timeout = std::chrono::microseconds(timeout_us);
        cv_.wait_for(lock, timeout, wait_until_ready);
    }
    
    // Collect all ready subscribers
    for (const auto& sub : subscribers_) {
        if (allocator_->hasData(sub)) {
            ready.push_back(sub);
        }
    }
    
    return ready;
}

void WaitSet::notify() noexcept {
    data_available_.store(true, std::memory_order_release);
    cv_.notify_all();  // Wake up all waiters
}

} // namespace lap::core
