/**
 * @file        CTimer.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       Generic timer implementation for AUTOSAR Adaptive Platform
 * @date        2025-10-27
 * @details     Provides generic timer that works with any std::chrono clock with one-shot and periodic modes
 * @copyright   Copyright (c) 2025
 * @note
 * sdk:
 * platform:
 * project:     LightAP
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2023/07/16  <td>1.0      <td>ddkv587         <td>init version
 * <tr><td>2025/10/27  <td>1.1      <td>ddkv587         <td>update header format
 * </table>
 */
#ifndef LAP_CORE_TIMER_HPP
#define LAP_CORE_TIMER_HPP

#include <thread>
#include <functional>
#include <atomic>
#include <chrono>
#include <type_traits>
#include "CTypedef.hpp"
#include "CSync.hpp"

namespace lap {
namespace core {

// Generic timer that works with any std::chrono clock (e.g., SteadyClock, SystemClock).
// Usage:
//   SteadyTimer t;
//   t.startAfter(std::chrono::milliseconds(50), []{ /* ... */ }); // one-shot
//   t.startAfter(std::chrono::milliseconds(50), cb, std::chrono::milliseconds(10)); // periodic
// Thread-safe stop(). Safe to call stop() from within the callback (no deadlock).

template <typename Clock>
class Timer {
public:
    using clock_type = Clock;
    using time_point = typename clock_type::time_point;
    using duration   = typename clock_type::duration;

public:

    Timer() noexcept = default;
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    ~Timer() { stop(); }

    bool isRunning() const noexcept { return running_.load(std::memory_order_acquire); }

    // One-shot or periodic start after a relative delay
    void startAfter(duration delay,
                    ::std::function<void()> cb,
                    duration period = duration::zero())
    {
        startAt(Clock::now() + delay, ::std::move(cb), period);
    }

    // One-shot or periodic start at an absolute time point
    void startAt(time_point when,
                 ::std::function<void()> cb,
                 duration period = duration::zero())
    {
        stop(); // ensure clean state
        {
            LockGuard lock(mtx_);
            callback_ = ::std::move(cb);
            next_ = when;
            period_ = period;
            running_.store(true, std::memory_order_release);
        }
        worker_ = ::std::thread([this]{ this->run(); });
    }

    void stop() noexcept
    {
        const auto self_id = ::std::this_thread::get_id();
        {
            LockGuard lock(mtx_);
            if (!running_.exchange(false, std::memory_order_acq_rel)) {
                // already stopped
            }
            cv_.notify_all();
        }
        // Avoid joining if called from the timer thread itself
        if (worker_.joinable() && worker_.get_id() != self_id) {
            worker_.join();
        }
    }

private:
    void run()
    {
        // record this thread id for stop-from-callback safety
        for (;;) {
            ::std::function<void()> cb;
            time_point when;
            duration p;
            {
                UniqueLock lock(mtx_, std::defer_lock);
                lock.lock();
                // wait until next trigger or stop
                cv_.wait_until(lock, next_, [this]{ return !running_.load(std::memory_order_acquire) || Clock::now() >= next_; });
                if (!running_.load(std::memory_order_acquire)) {
                    break;
                }
                cb = callback_;
                when = next_;
                p = period_;
            }

            // Execute callback outside lock to prevent blocking stop()
            try {
                if (cb) cb();
            } catch (...) {
                // Swallow exceptions to keep timer thread alive; user code should handle errors.
            }

            if (p == duration::zero()) {
                // one-shot
                LockGuard lock(mtx_);
                running_.store(false, std::memory_order_release);
                cv_.notify_all();
                break;
            } else {
                // periodic: schedule next; catch up if we are behind
                LockGuard lock(mtx_);
                auto now = Clock::now();
                do {
                    next_ += p;
                } while (next_ <= now);
                // loop continues
            }
        }
    }

private:
    mutable Mutex mtx_;
    ::std::condition_variable_any cv_;
    ::std::thread worker_;
    ::std::function<void()> callback_;
    time_point next_{};
    duration period_{}; // zero means one-shot
    ::std::atomic<bool> running_{false};
};

using SteadyTimer = Timer<SteadyClock>;
using SystemTimer = Timer<SystemClock>;

} // namespace core
} // namespace lap

#endif // LAP_CORE_TIMER_HPP
