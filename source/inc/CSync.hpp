/**
 * @file        CSync.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       Synchronization primitives for AUTOSAR Adaptive Platform
 * @date        2026-02-06
 * @details     Provides mutex, lock guard, condition variable, and other synchronization
 *              primitives as thin type aliases over std library types.
 *              C++17 is the default standard; C++14 backward compatibility is provided
 *              where necessary via conditional compilation.
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
 * <tr><td>2026/02/06  <td>2.0      <td>ddkv587         <td>refactor: use std types directly, add ScopedLock
 * </table>
 */
#ifndef LAP_CORE_SYNC_HPP
#define LAP_CORE_SYNC_HPP

#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <chrono>
#include <tuple>
#include <utility>

#include "CTypedef.hpp"

namespace lap
{
namespace core
{

// ==================== Mutex Type Aliases ====================

/**
 * @brief Non-recursive mutex for mutual exclusion
 */
using Mutex             = ::std::mutex;

/**
 * @brief Recursive mutex allowing multiple locks by the same thread
 */
using RecursiveMutex    = ::std::recursive_mutex;

/**
 * @brief Read-Write lock for multi-reader / single-writer scenarios
 * @details C++17: std::shared_mutex, C++14: std::shared_timed_mutex
 */
#if __cplusplus >= 201703L
using RWLock            = ::std::shared_mutex;
#else
using RWLock            = ::std::shared_timed_mutex;
#endif

// ==================== Condition Variable Aliases ====================

/**
 * @brief Condition variable (works with std::unique_lock< std::mutex >)
 */
using ConditionVariable     = ::std::condition_variable;

/**
 * @brief Condition variable for any lockable type
 */
using ConditionVariableAny  = ::std::condition_variable_any;

// ==================== Template Lock Aliases (Generic) ====================

/**
 * @brief Generic RAII lock guard, wraps std::lock_guard
 * @tparam M Mutex type (Mutex, RecursiveMutex, RWLock, etc.)
 */
template < typename M >
using TLockGuard    = ::std::lock_guard< M >;

/**
 * @brief Generic RAII unique lock with manual control, wraps std::unique_lock
 * @tparam M Mutex type
 */
template < typename M >
using TUniqueLock   = ::std::unique_lock< M >;

/**
 * @brief Generic RAII shared (read) lock, wraps std::shared_lock
 * @tparam M SharedMutex type (RWLock, etc.)
 */
template < typename M >
using TSharedLock   = ::std::shared_lock< M >;

// ==================== Concrete Lock Aliases (Backward Compatible) ====================

/**
 * @brief RAII lock guard for Mutex (drop-in replacement for previous LockGuard)
 * @note  Usage: LockGuard lock( m_mutex );
 */
using LockGuard         = TLockGuard< Mutex >;

/**
 * @brief RAII lock guard for RecursiveMutex
 */
using RecursiveLockGuard = TLockGuard< RecursiveMutex >;

/**
 * @brief RAII unique lock for Mutex (supports deferred locking, condition_variable)
 * @note  Usage: UniqueLock lock( m_mutex );
 */
using UniqueLock        = TUniqueLock< Mutex >;

/**
 * @brief RAII shared (read) lock for RWLock
 * @note  Usage: ReadLockGuard lock( m_rwLock );
 */
using ReadLockGuard     = TSharedLock< RWLock >;

/**
 * @brief RAII exclusive (write) lock for RWLock
 * @note  Usage: WriteLockGuard lock( m_rwLock );
 */
using WriteLockGuard    = TLockGuard< RWLock >;

// ==================== ScopedLock ====================

#if __cplusplus >= 201703L

/**
 * @brief RAII scoped lock supporting multiple mutexes with deadlock avoidance
 * @tparam MutexTypes  One or more mutex types
 * @details C++17: directly aliases std::scoped_lock
 *
 * Usage:
 * @code
 *   ScopedLock lock( m_mutex );                // single mutex (CTAD)
 *   ScopedLock lock( m_mutex1, m_mutex2 );     // multi-mutex, deadlock-free
 * @endcode
 */
template < typename... MutexTypes >
using ScopedLock = ::std::scoped_lock< MutexTypes... >;

#else // C++14 fallback

/**
 * @brief C++14 ScopedLock — general variadic template (multiple mutexes)
 * @details Uses std::lock() for deadlock avoidance when locking multiple mutexes.
 */
template < typename... MutexTypes >
class ScopedLock
{
public:
    explicit ScopedLock( MutexTypes&... mutexes )
        : m_mutexes( mutexes... )
    {
        ::std::lock( mutexes... );
    }

    ~ScopedLock()
    {
        unlockAll( ::std::index_sequence_for< MutexTypes... >{} );
    }

    ScopedLock( const ScopedLock& )             = delete;
    ScopedLock& operator=( const ScopedLock& )  = delete;

private:
    template < ::std::size_t... Is >
    void unlockAll( ::std::index_sequence< Is... > )
    {
        int dummy[] = { ( ::std::get< Is >( m_mutexes ).unlock(), 0 )... };
        (void)dummy;
    }

    ::std::tuple< MutexTypes&... > m_mutexes;
};

/**
 * @brief C++14 ScopedLock — single mutex specialization
 * @details No std::lock() overhead; direct lock/unlock.
 */
template < typename MutexType >
class ScopedLock< MutexType >
{
public:
    explicit ScopedLock( MutexType& mutex ) noexcept
        : m_mutex( mutex )
    {
        m_mutex.lock();
    }

    ~ScopedLock() noexcept
    {
        m_mutex.unlock();
    }

    ScopedLock( const ScopedLock& )             = delete;
    ScopedLock& operator=( const ScopedLock& )  = delete;

private:
    MutexType& m_mutex;
};

/**
 * @brief C++14 ScopedLock — zero mutex specialization (no-op)
 */
template <>
class ScopedLock<>
{
public:
    explicit ScopedLock() noexcept = default;
    ScopedLock( const ScopedLock& )             = delete;
    ScopedLock& operator=( const ScopedLock& )  = delete;
};

#endif // __cplusplus >= 201703L

// ==================== Higher-Level Synchronization Primitives ====================

/**
 * @brief Manual event for thread synchronization
 * @details Supports signaling and waiting, inspired by Win32/Boost event objects.
 *          Uses std::condition_variable + std::mutex directly for efficiency.
 * @note    Thread-safe
 */
class Event
{
public:
    Event() noexcept = default;
    ~Event() noexcept = default;

    Event( const Event& )               = delete;
    Event& operator=( const Event& )    = delete;

    /**
     * @brief Wait indefinitely for the event to be signaled
     */
    void Wait()
    {
        UniqueLock lock( m_mutex );
        m_cv.wait( lock, [this] { return m_bSignaled; } );
    }

    /**
     * @brief Wait with timeout
     * @param relTime  Maximum duration to wait
     * @return true if signaled, false if timed out
     */
    template < typename Rep, typename Period >
    Bool WaitFor( const ::std::chrono::duration< Rep, Period >& relTime )
    {
        UniqueLock lock( m_mutex );
        return m_cv.wait_for( lock, relTime, [this] { return m_bSignaled; } );
    }

    /**
     * @brief Signal the event, waking all waiters
     */
    void Signal() noexcept
    {
        {
            LockGuard lock( m_mutex );
            m_bSignaled = true;
        }
        m_cv.notify_all();
    }

    /**
     * @brief Reset the event to unsignaled state
     */
    void Reset() noexcept
    {
        LockGuard lock( m_mutex );
        m_bSignaled = false;
    }

private:
    Mutex               m_mutex;
    ConditionVariable   m_cv;
    Bool                m_bSignaled = false;
};

/**
 * @brief Counting semaphore for resource limiting
 * @details Emulates a semaphore using std::condition_variable + std::mutex.
 * @note    Thread-safe
 */
class Semaphore
{
public:
    explicit Semaphore( UInt32 initialCount ) noexcept
        : m_iCount( initialCount )
    {}

    ~Semaphore() noexcept = default;

    Semaphore( const Semaphore& )               = delete;
    Semaphore& operator=( const Semaphore& )    = delete;

    /**
     * @brief Acquire the semaphore (decrement count, block if zero)
     */
    void Acquire()
    {
        UniqueLock lock( m_mutex );
        m_cv.wait( lock, [this] { return m_iCount > 0; } );
        --m_iCount;
    }

    /**
     * @brief Try to acquire without blocking
     * @return true if acquired, false otherwise
     */
    Bool TryAcquire() noexcept
    {
        LockGuard lock( m_mutex );
        if ( m_iCount > 0 ) {
            --m_iCount;
            return true;
        }
        return false;
    }

    /**
     * @brief Release the semaphore (increment count, wake one waiter)
     */
    void Release() noexcept
    {
        {
            LockGuard lock( m_mutex );
            ++m_iCount;
        }
        m_cv.notify_one();
    }

private:
    Mutex               m_mutex;
    ConditionVariable   m_cv;
    UInt32              m_iCount;
};

} // namespace core
} // namespace lap

#endif // LAP_CORE_SYNC_HPP
