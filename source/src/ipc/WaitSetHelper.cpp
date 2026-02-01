/**
 * @file        WaitSetHelper.cpp
 * @author      LightAP Core Team
 * @brief       Implementation of WaitSetHelper
 * @date        2026-01-07
 * @copyright   Copyright (c) 2026
 */

#include "ipc/WaitSetHelper.hpp"
#include "CCoreErrorDomain.hpp"
#include <chrono>
#include <thread>
#include <ctime>

namespace lap
{
namespace core
{
namespace ipc
{
    Result< void > WaitSetHelper::WaitForFlags( std::atomic< UInt32 >* flags,
                                            UInt32 mask,
                                            std::chrono::nanoseconds timeout ) noexcept
    {
        if ( DEF_LAP_IF_UNLIKELY( !flags ) ) {
            return Result< void >( MakeErrorCode( CoreErrc::kInvalidArgument ) );
        }
        
        auto start_time = std::chrono::steady_clock::now();
        UInt64 timeout_ns = timeout.count();
        Bool has_timeout = ( timeout_ns > 0 );
        
        while ( true ) {
            // Fast path: Check if flags are already set
            UInt32 current_flags = flags->load( std::memory_order_acquire );
            if ( ( current_flags & mask ) != 0 ) {
                return {};  // Success
            }
            
            // Calculate remaining timeout
            UInt64 remaining_ns = timeout_ns;
            if ( DEF_LAP_IF_LIKELY( has_timeout ) ) {
                auto elapsed = std::chrono::steady_clock::now() - start_time;
                auto elapsed_ns = std::chrono::duration_cast< std::chrono::nanoseconds >( elapsed ).count();
                
                if ( static_cast< UInt64 >( elapsed_ns ) >= timeout_ns ) {
                    return Result< void >( MakeErrorCode( CoreErrc::kWouldBlock ) );  // Timeout
                }
                
                remaining_ns = timeout_ns - static_cast< UInt64 >( elapsed_ns );
            }
            
            // Slow path: Call futex_wait        
            if ( FutexWait( flags, current_flags, has_timeout ? remaining_ns : 0 ) == 0 ) {
                // Woken up - recheck condition (handle spurious wakeups)
                continue;
            }
            
            // Check errno
            if ( errno == ETIMEDOUT ) {
                return Result< void >( MakeErrorCode( CoreErrc::kWouldBlock ) );
            } else if ( errno == EAGAIN || errno == EINTR ) {
                // Value changed or interrupted - retry
                continue;
            }
            
            // Other errors - treat as timeout
            return Result< void >( MakeErrorCode( CoreErrc::kWouldBlock ) );
        }
    }
    
    Bool WaitSetHelper::PollForFlags(std::atomic<UInt32>* flags,
                                    UInt32 mask,
                                    std::chrono::nanoseconds timeout) noexcept
    {
        DEF_LAP_ASSERT( flags != nullptr, "Flags pointer is null" );
        
        auto start_time = SteadyClock::now();
        UInt64 timeout_ns = timeout.count();
        
        // Busy-wait loop
        while ( true ) {
            // Check flags
            UInt32 current_flags = flags->load( std::memory_order_acquire );
            if ( ( current_flags & mask ) != 0 ) {
                return true;  // Success
            }
            
            // Check timeout
            auto elapsed = SteadyClock::now() - start_time;
            auto elapsed_ns = std::chrono::duration_cast< std::chrono::nanoseconds >( elapsed ).count();
            
            if ( static_cast< UInt64 >( elapsed_ns ) >= timeout_ns ) {
                return false;  // Timeout
            }
            
            std::this_thread::yield();
        }
    }
    
    void WaitSetHelper::SetFlagsAndWake(std::atomic<UInt32>* flags,
                                       UInt32 mask,
                                       Bool wake) noexcept
    {
        if ( DEF_LAP_IF_UNLIKELY( !flags ) ) {
            return;
        }
        
        // Set flags atomically
        flags->fetch_or( mask, std::memory_order_release );
        
        // Optionally wake waiters
        if ( wake ) {
            FutexWake( flags );
        }
    }
    
    void WaitSetHelper::ClearFlags(std::atomic<UInt32>* flags,
                                  UInt32 mask) noexcept
    {
        if ( !flags ) {
            return;
        }
        
        flags->fetch_and( ~mask, std::memory_order_release );
    }
    
    Bool WaitSetHelper::CheckFlags(const std::atomic<UInt32>* flags,
                                  UInt32 mask) noexcept
    {
        if ( DEF_LAP_IF_UNLIKELY( !flags ) ) {
            return false;
        }
        
        UInt32 current = flags->load( std::memory_order_acquire );
        return ( current & mask ) != 0;
    }
    
    int WaitSetHelper::FutexWait(std::atomic<UInt32>* uaddr,
                                UInt32 expected,
                                UInt64 timeout_ns) noexcept
    {
        struct timespec ts = { 0, 0 };
        
        if ( timeout_ns > 0 ) {
            ts.tv_sec = timeout_ns / 1000000000ULL;
            ts.tv_nsec = timeout_ns % 1000000000ULL;
        }
        
        // FUTEX_WAIT_PRIVATE: More efficient for process-private futexes
        // But we need FUTEX_WAIT for shared memory across processes
        return syscall(SYS_futex,
                      uaddr,
                      FUTEX_WAIT,
                      expected,
                      &ts,
                      nullptr,
                      0);
    }
    
    int WaitSetHelper::FutexWake(std::atomic<UInt32>* uaddr,
                                int num_waiters) noexcept
    {
        return syscall(SYS_futex,
                      uaddr,
                      FUTEX_WAKE,
                      num_waiters,
                      nullptr,
                      nullptr,
                      0);
    }
    
}  // namespace ipc
}  // namespace core
}  // namespace lap
