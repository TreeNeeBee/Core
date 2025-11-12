/**
 * @file        CTime.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       Time utility helpers for AUTOSAR Adaptive Platform
 * @date        2025-10-27
 * @details     Provides time conversions, current time for multiple clocks, and sleep utilities
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
#ifndef LAP_CORE_TIME_HPP
#define LAP_CORE_TIME_HPP

#include <chrono>
#include <thread>
#include <ctime>
#include <iomanip>
#include <sstream>
#include "CTypedef.hpp"
#include "CString.hpp"

namespace lap
{
namespace core
{
    // Lightweight time utility helpers inspired by common patterns from Abseil and Folly.
    // Provides conversions, current time for multiple clocks, and simple sleep utilities.
    class Time final
    {
    public:
        // Now (system and steady)
        static SystemClock::time_point nowSystem() noexcept { return SystemClock::now(); }
        static SteadyClock::time_point nowSteady() noexcept { return SteadyClock::now(); }

        // Milliseconds since Unix epoch (system clock)
        static UInt64 getCurrentTime() noexcept
        {
            return toUnixMillis( nowSystem() );
        }

        static UInt64 toUnixMillis( SystemClock::time_point tp ) noexcept
        {
            return ::std::chrono::duration_cast< ::std::chrono::milliseconds >( tp.time_since_epoch() ).count();
        }

        static SystemClock::time_point fromUnixMillis( UInt64 ms ) noexcept
        {
            return SystemClock::time_point{ ::std::chrono::milliseconds{ ms } };
        }

        template < typename Rep, typename Period >
        static UInt64 toMillis( ::std::chrono::duration< Rep, Period > d ) noexcept
        {
            return ::std::chrono::duration_cast< ::std::chrono::milliseconds >( d ).count();
        }

        // Sleep helpers (not marked noexcept because standard does not specify noexcept)
        template < typename Rep, typename Period >
        static void sleepFor( ::std::chrono::duration< Rep, Period > d )
        {
            ::std::this_thread::sleep_for( d );
        }

        template < typename Clock, typename Duration >
        static void sleepUntil( ::std::chrono::time_point< Clock, Duration > tp )
        {
            ::std::this_thread::sleep_until( tp );
        }

        // Formatting (system time only). Example format: "%Y-%m-%d %H:%M:%S"
        static String formatSystem( SystemClock::time_point tp, const Char* fmt = "%Y-%m-%d %H:%M:%S" )
        {
            using namespace std;
            time_t tt = SystemClock::to_time_t( tp );
            tm tmv{};
            // localtime_r is thread-safe
            localtime_r( &tt, &tmv );
            ostringstream oss;
            oss << put_time( &tmv, fmt );
            return oss.str();
        }
        
        /**
         * @brief Get current time in ISO 8601 format (UTC)
         * @return ISO 8601 formatted string (e.g., "2025-11-11T10:30:45Z")
         */
        static String GetCurrentTimeISO() noexcept
        {
            using namespace std;
            try {
                time_t now = time(nullptr);
                tm tmv{};
                gmtime_r(&now, &tmv);  // UTC time
                
                char buffer[32];
                strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tmv);
                return String(buffer);
            } catch (...) {
                return String("1970-01-01T00:00:00Z");  // Fallback
            }
        }
    };
} // namespace core
} // namespace lap
#endif