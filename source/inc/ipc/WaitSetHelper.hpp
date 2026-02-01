/**
 * @file        WaitSetHelper.hpp
 * @author      LightAP Core Team
 * @brief       Linux futex-based wait/wake mechanism
 * @date        2026-01-07
 * @details     Efficient event notification for IPC blocking operations
 * @copyright   Copyright (c) 2026
 * @note        Based on iceoryx2 design - lock-free fast path + futex slow path
 */
#ifndef LAP_CORE_IPC_WAIT_SET_HELPER_HPP
#define LAP_CORE_IPC_WAIT_SET_HELPER_HPP

#include "IPCTypes.hpp"
#include "CResult.hpp"
#include <atomic>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cerrno>

namespace lap
{
namespace core
{
namespace ipc
{
    /**
     * @brief Helper class for wait/wake operations using Linux futex
     * @details
     * - Fast path: Lock-free atomic flag check (< 50ns)
     * - Slow path: futex system call for blocking (< 1μs wake latency)
     * - Supports timeout for all operations
     * 
     * Performance Characteristics:
     * - WaitForFlags (fast path): < 50ns
     * - WaitForFlags (futex wake): < 1μs
     * - PollForFlags: < 10ns
     * - SetFlagsAndWake (wake=false): ~55ns
     * - SetFlagsAndWake (wake=true): ~255ns
     */
    class WaitSetHelper
    {
    public:
        /**
         * @brief Wait for specific flags to be set (blocking)
         * @param flags Pointer to atomic flag variable
         * @param mask Bit mask to check
         * @param timeout Maximum wait duration (0 = infinite)
         * @return Result with success or timeout error
         * 
         * @details
         * 1. Fast path: Check flags atomically
         * 2. Slow path: Call futex_wait if flags not set
         * 3. Handles spurious wakeups by rechecking condition
         */
        static Result<void> WaitForFlags(std::atomic<UInt32>* flags,
                                        UInt32 mask,
                                        std::chrono::nanoseconds timeout = std::chrono::nanoseconds::zero()) noexcept;
        
        /**
         * @brief Poll for flags with busy-wait (for kWait policy)
         * @param flags Pointer to atomic flag variable
         * @param mask Bit mask to check
         * @param timeout Maximum poll duration
         * @return true if flags are set, false on timeout
         * 
         * @details
         * - Busy-waits in user space (no system calls)
         * - Suitable for very short timeouts (< 10ms)
         * - High CPU usage during polling
         */
        static Bool PollForFlags(std::atomic<UInt32>* flags,
                                UInt32 mask,
                                std::chrono::nanoseconds timeout) noexcept;
        
        /**
         * @brief Set flags and optionally wake waiters
         * @param flags Pointer to atomic flag variable
         * @param mask Bit mask to set
         * @param wake If true, call futex_wake (default: true)
         * 
         * @details
         * - wake=false: Fast path optimization for kWait policy (~55ns)
         * - wake=true: Full wake with futex (~255ns)
         * - Always sets flags atomically
         */
        static void SetFlagsAndWake(std::atomic<UInt32>* flags,
                                   UInt32 mask,
                                   Bool wake = true) noexcept;
        
        /**
         * @brief Clear specific flags
         * @param flags Pointer to atomic flag variable
         * @param mask Bit mask to clear
         */
        static void ClearFlags(std::atomic<UInt32>* flags,
                              UInt32 mask) noexcept;
        
        /**
         * @brief Check if any flags in mask are set
         * @param flags Pointer to atomic flag variable
         * @param mask Bit mask to check
         * @return true if any flag in mask is set
         */
        static Bool CheckFlags(const std::atomic<UInt32>* flags,
                              UInt32 mask) noexcept;

        /**
         * @brief Low-level futex wait wrapper
         * @param uaddr Address of futex variable
         * @param expected Expected value
         * @param timeout_ns Timeout in nanoseconds (0 = infinite)
         * @return 0 on success, -1 on error (errno set)
         */
        static int FutexWait(std::atomic<UInt32>* uaddr,
                            UInt32 expected,
                            UInt64 timeout_ns) noexcept;
        
        /**
         * @brief Low-level futex wake wrapper
         * @param uaddr Address of futex variable
         * @param num_waiters Number of waiters to wake (default: INT32_MAX = all)
         * @return Number of waiters woken
         */
        static int FutexWake(std::atomic<UInt32>* uaddr,
                            int num_waiters = INT32_MAX) noexcept;
    };
    
}  // namespace ipc
}  // namespace core
}  // namespace lap

#endif  // LAP_CORE_IPC_WAIT_SET_HELPER_HPP
