/**
 * @file        CSync.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       Synchronization primitives for AUTOSAR Adaptive Platform
 * @date        2025-10-27
 * @details     Provides mutex, condition variable, and other synchronization objects
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
#ifndef LAP_CORE_SYNC_HPP
#define LAP_CORE_SYNC_HPP

#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <chrono>
#include "CTypedef.hpp"

namespace lap {
namespace core {

/**
 * @brief Abstract base class for synchronization objects.
 * 
 * Defines a common interface for mutex-like synchronization, inspired by Boost.Thread and MFC's CSyncObject.
 */
class SyncObject {
public:
    virtual ~SyncObject() noexcept = default;
    virtual void lock() noexcept = 0;
    virtual Bool try_lock() noexcept = 0;
    virtual void unlock() noexcept = 0;
};

/**
 * @brief Non-recursive mutex for mutual exclusion.
 * 
 * Wraps std::mutex, compatible with lap::core types.
 */
class Mutex : public SyncObject {
public:
    void lock() noexcept override { mtx_.lock(); }
    Bool try_lock() noexcept override { return mtx_.try_lock(); }
    void unlock() noexcept override { mtx_.unlock(); }

private:
    std::mutex mtx_;
};

/**
 * @brief Recursive mutex allowing multiple locks by the same thread.
 * 
 * Wraps std::recursive_mutex, compatible with lap::core types.
 */
class RecursiveMutex : public SyncObject {
public:
    void lock() noexcept override { mtx_.lock(); }
    Bool try_lock() noexcept override { return mtx_.try_lock(); }
    void unlock() noexcept override { mtx_.unlock(); }

private:
    std::recursive_mutex mtx_;
};

/**
 * @brief Read-Write lock (shared mutex) for multi-reader/single-writer scenarios.
 *
 * Wraps std::shared_mutex. Writer APIs conform to SyncObject; reader APIs are provided separately.
 */
class RWLock : public SyncObject {
public:
    // Writer (exclusive) APIs
    void lock() noexcept override { sm_.lock(); }
    Bool try_lock() noexcept override { return sm_.try_lock(); }
    void unlock() noexcept override { sm_.unlock(); }

    // Reader (shared) APIs
    void lock_shared() noexcept { sm_.lock_shared(); }
    Bool try_lock_shared() noexcept { return sm_.try_lock_shared(); }
    void unlock_shared() noexcept { sm_.unlock_shared(); }

private:
    std::shared_mutex sm_;
};

/**
 * @brief RAII lock guard for automatic unlocking.
 * 
 * Ensures lock is released on scope exit, similar to std::lock_guard.
 */
class LockGuard {
public:
    explicit LockGuard(SyncObject& sync) : sync_(sync) {
        sync_.lock();
    }
    ~LockGuard() noexcept {
        sync_.unlock();
    }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;

private:
    SyncObject& sync_;
};

/**
 * @brief Flexible RAII unique lock for manual lock control.
 * 
 * Supports deferred locking and try-locking, similar to std::unique_lock.
 */
class UniqueLock {
public:
    explicit UniqueLock(SyncObject& sync) : sync_(sync), owned_(false) {
        lock();
    }
    explicit UniqueLock(SyncObject& sync, std::defer_lock_t) noexcept
        : sync_(sync), owned_(false) {}
    
    ~UniqueLock() noexcept {
        if (owned_) unlock();
    }
    
    void lock() {
        if (!owned_) {
            sync_.lock();
            owned_ = true;
        }
    }
    
    Bool try_lock() {
        if (!owned_) {
            owned_ = sync_.try_lock();
        }
        return owned_;
    }
    
    void unlock() noexcept {
        if (owned_) {
            sync_.unlock();
            owned_ = false;
        }
    }
    
    Bool owns_lock() const noexcept { return owned_; }
    SyncObject* mutex() const noexcept { return &sync_; }
    
    UniqueLock(const UniqueLock&) = delete;
    UniqueLock& operator=(const UniqueLock&) = delete;
    
    UniqueLock(UniqueLock&& other) noexcept
        : sync_(other.sync_), owned_(other.owned_) {
        other.owned_ = false;
    }
    UniqueLock& operator=(UniqueLock&& other) noexcept {
        if (this != &other) {
            if (owned_) sync_.unlock();
            sync_ = other.sync_;
            owned_ = other.owned_;
            other.owned_ = false;
        }
        return *this;
    }

private:
    SyncObject& sync_;
    Bool owned_;
};

/**
 * @brief RAII read lock for RWLock.
 */
class ReadLockGuard {
public:
    explicit ReadLockGuard(RWLock& rw) : rw_(rw) { rw_.lock_shared(); }
    ~ReadLockGuard() noexcept { rw_.unlock_shared(); }
    ReadLockGuard(const ReadLockGuard&) = delete;
    ReadLockGuard& operator=(const ReadLockGuard&) = delete;
private:
    RWLock& rw_;
};

/**
 * @brief RAII write lock for RWLock.
 */
class WriteLockGuard {
public:
    explicit WriteLockGuard(RWLock& rw) : rw_(rw) { rw_.lock(); }
    ~WriteLockGuard() noexcept { rw_.unlock(); }
    WriteLockGuard(const WriteLockGuard&) = delete;
    WriteLockGuard& operator=(const WriteLockGuard&) = delete;
private:
    RWLock& rw_;
};

/**
 * @brief Manual event for thread synchronization.
 * 
 * Supports signaling and waiting, inspired by Boost's condition_variable.
 */
class Event {
public:
    /**
     * @brief Wait for the event to be signaled.
     */
    void wait() {
        UniqueLock lock(mtx_);
        cv_.wait(lock, [this] { return signaled_; });
    }
    
    /**
     * @brief Wait with timeout.
     * @return true if signaled, false if timed out.
     */
    template <typename Rep, typename Period>
    Bool wait_for(const std::chrono::duration<Rep, Period>& rel_time) {
        UniqueLock lock(mtx_);
        return cv_.wait_for(lock, rel_time, [this] { return signaled_; });
    }
    
    /**
     * @brief Signal the event, waking all waiters.
     */
    void signal() noexcept {
        LockGuard lock(mtx_);
        signaled_ = true;
        cv_.notify_all();
    }
    
    /**
     * @brief Reset the event to unsignaled state.
     */
    void reset() noexcept {
        LockGuard lock(mtx_);
        signaled_ = false;
    }

private:
    Mutex mtx_;
    std::condition_variable_any cv_;
    Bool signaled_ = false;
};

/**
 * @brief Counting semaphore for resource limiting.
 * 
 * Emulates a semaphore using std::condition_variable, compatible with lap::core types.
 */
class Semaphore {
public:
    explicit Semaphore(UInt32 initial_count) : count_(initial_count) {}
    
    /**
     * @brief Acquire the semaphore (decrement count).
     */
    void acquire() {
        UniqueLock lock(mtx_);
        cv_.wait(lock, [this] { return count_ > 0; });
        --count_;
    }
    
    /**
     * @brief Try to acquire without blocking.
     * @return true if acquired, false otherwise.
     */
    Bool try_acquire() noexcept {
        UniqueLock lock(mtx_);
        if (count_ > 0) {
            --count_;
            return true;
        }
        return false;
    }
    
    /**
     * @brief Release the semaphore (increment count).
     */
    void release() noexcept {
        LockGuard lock(mtx_);
        ++count_;
        cv_.notify_one();
    }

private:
    Mutex mtx_;
    std::condition_variable_any cv_;
    UInt32 count_;
};
} // namespace core
} // namespace lap

#endif // LAP_CORE_SYNC_HPP
