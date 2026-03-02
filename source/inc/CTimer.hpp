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
#include <chrono>
#include <type_traits>
#include "CTypedef.hpp"
#include "CFunction.hpp"
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
    ~Timer() { Stop(); }

    Bool IsRunning() const noexcept { return m_bRunning.load(std::memory_order_acquire); }

    // One-shot or periodic start after a relative delay
    void StartAfter(duration delay,
                    Function<void()> cb,
                    duration period = duration::zero())
    {
        StartAt(Clock::now() + delay, ::std::move(cb), period);
    }

    // One-shot or periodic start at an absolute time point
    void StartAt(time_point when,
                 Function<void()> cb,
                 duration period = duration::zero())
    {
        Stop(); // ensure clean state
        {
            LockGuard lock(m_mtx);
            m_callback = ::std::move(cb);
            m_next = when;
            m_period = period;
            m_bRunning.store(true, std::memory_order_release);
        }
        m_worker = ::std::thread([this]{ this->run(); });
    }

    void Stop() noexcept
    {
        const auto self_id = ::std::this_thread::get_id();
        {
            LockGuard lock(m_mtx);
            if (!m_bRunning.exchange(false, std::memory_order_acq_rel)) {
                // already stopped
            }
            m_cv.notify_all();
        }
        // Avoid joining if called from the timer thread itself
        if (m_worker.joinable() && m_worker.get_id() != self_id) {
            m_worker.join();
        }
    }

private:
    void run()
    {
        // record this thread id for stop-from-callback safety
        for (;;) {
            Function<void()> cb;
            time_point when;
            duration p;
            {
                UniqueLock lock(m_mtx, std::defer_lock);
                lock.lock();
                // wait until next trigger or stop
                m_cv.wait_until(lock, m_next, [this]{ return !m_bRunning.load(std::memory_order_acquire) || Clock::now() >= m_next; });
                if (!m_bRunning.load(std::memory_order_acquire)) {
                    break;
                }
                cb = m_callback;
                when = m_next;
                p = m_period;
            }

            // Execute callback outside lock to prevent blocking Stop()
            try {
                if (cb) cb();
            } catch (...) {
                // Swallow exceptions to keep timer thread alive; user code should handle errors.
            }

            if (p == duration::zero()) {
                // one-shot
                LockGuard lock(m_mtx);
                m_bRunning.store(false, std::memory_order_release);
                m_cv.notify_all();
                break;
            } else {
                // periodic: schedule next; catch up if we are behind
                LockGuard lock(m_mtx);
                auto now = Clock::now();
                do {
                    m_next += p;
                } while (m_next <= now);
                // loop continues
            }
        }
    }

private:
    mutable Mutex m_mtx;
    ConditionVariableAny m_cv;
    ::std::thread m_worker;
    Function<void()> m_callback;
    time_point m_next{};
    duration m_period{}; // zero means one-shot
    Atomic<Bool> m_bRunning{false};
};

using SteadyTimer = Timer<SteadyClock>;
using SystemTimer = Timer<SystemClock>;

} // namespace core
} // namespace lap

#endif // LAP_CORE_TIMER_HPP
