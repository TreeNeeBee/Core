/**
 * @file        CByteOrder.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       Byte order conversion utilities for AUTOSAR Adaptive Platform
 * @date        2025-10-29
 * @details     Provides endianness detection and byte order conversion functions
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R22-11 SWS_CORE_10xxx - Byte Order
 * sdk:
 * platform:
 * project:     LightAP
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/10/29  <td>1.0      <td>ddkv587         <td>init version, AUTOSAR compliant
 * </table>
 */
#ifndef LAP_CORE_BYTEORDER_HPP
#define LAP_CORE_BYTEORDER_HPP

#include "CTypedef.hpp"
#include <cstring>

namespace lap
{
namespace core
{
    /**
     * @brief Enumeration for byte order (endianness)
     * According to AUTOSAR SWS_CORE_10100
     */
    enum class ByteOrder : UInt8
    {
        kLittleEndian = 0,  // Little-endian byte order (LSB first)
        kBigEndian = 1      // Big-endian byte order (MSB first)
    };

    /**
     * @brief Get the native byte order of the platform
     * @return ByteOrder::kLittleEndian or ByteOrder::kBigEndian
     * According to AUTOSAR SWS_CORE_10101
     */
    constexpr ByteOrder GetPlatformByteOrder() noexcept
    {
        // Use GCC/Clang predefined macros for compile-time detection
        #if defined(__BYTE_ORDER__)
            #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
                return ByteOrder::kLittleEndian;
            #elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
                return ByteOrder::kBigEndian;
            #else
                // Default to little-endian for most modern platforms
                return ByteOrder::kLittleEndian;
            #endif
        #else
            // Fallback: most modern systems are little-endian
            return ByteOrder::kLittleEndian;
        #endif
    }

    /**
     * @brief Swap bytes of a 16-bit value
     * @param value Input value
     * @return Byte-swapped value
     * According to AUTOSAR SWS_CORE_10110
     */
    constexpr UInt16 ByteSwap16(UInt16 value) noexcept
    {
        return static_cast<UInt16>((value >> 8) | (value << 8));
    }

    /**
     * @brief Swap bytes of a 32-bit value
     * @param value Input value
     * @return Byte-swapped value
     * According to AUTOSAR SWS_CORE_10111
     */
    constexpr UInt32 ByteSwap32(UInt32 value) noexcept
    {
        return ((value >> 24) & 0x000000FF) |
               ((value >> 8)  & 0x0000FF00) |
               ((value << 8)  & 0x00FF0000) |
               ((value << 24) & 0xFF000000);
    }

    /**
     * @brief Swap bytes of a 64-bit value
     * @param value Input value
     * @return Byte-swapped value
     * According to AUTOSAR SWS_CORE_10112
     */
    constexpr UInt64 ByteSwap64(UInt64 value) noexcept
    {
        return ((value >> 56) & 0x00000000000000FFULL) |
               ((value >> 40) & 0x000000000000FF00ULL) |
               ((value >> 24) & 0x0000000000FF0000ULL) |
               ((value >> 8)  & 0x00000000FF000000ULL) |
               ((value << 8)  & 0x000000FF00000000ULL) |
               ((value << 24) & 0x0000FF0000000000ULL) |
               ((value << 40) & 0x00FF000000000000ULL) |
               ((value << 56) & 0xFF00000000000000ULL);
    }

    /**
     * @brief Convert 16-bit value from host to network byte order (big-endian)
     * @param hostValue Value in host byte order
     * @return Value in network byte order
     * According to AUTOSAR SWS_CORE_10120
     */
    inline UInt16 HostToNetwork16(UInt16 hostValue) noexcept
    {
        if constexpr (GetPlatformByteOrder() == ByteOrder::kLittleEndian) {
            return ByteSwap16(hostValue);
        } else {
            return hostValue;
        }
    }

    /**
     * @brief Convert 32-bit value from host to network byte order (big-endian)
     * @param hostValue Value in host byte order
     * @return Value in network byte order
     * According to AUTOSAR SWS_CORE_10121
     */
    inline UInt32 HostToNetwork32(UInt32 hostValue) noexcept
    {
        if constexpr (GetPlatformByteOrder() == ByteOrder::kLittleEndian) {
            return ByteSwap32(hostValue);
        } else {
            return hostValue;
        }
    }

    /**
     * @brief Convert 64-bit value from host to network byte order (big-endian)
     * @param hostValue Value in host byte order
     * @return Value in network byte order
     * According to AUTOSAR SWS_CORE_10122
     */
    inline UInt64 HostToNetwork64(UInt64 hostValue) noexcept
    {
        if constexpr (GetPlatformByteOrder() == ByteOrder::kLittleEndian) {
            return ByteSwap64(hostValue);
        } else {
            return hostValue;
        }
    }

    /**
     * @brief Convert 16-bit value from network to host byte order
     * @param networkValue Value in network byte order
     * @return Value in host byte order
     * According to AUTOSAR SWS_CORE_10130
     */
    inline UInt16 NetworkToHost16(UInt16 networkValue) noexcept
    {
        if constexpr (GetPlatformByteOrder() == ByteOrder::kLittleEndian) {
            return ByteSwap16(networkValue);
        } else {
            return networkValue;
        }
    }

    /**
     * @brief Convert 32-bit value from network to host byte order
     * @param networkValue Value in network byte order
     * @return Value in host byte order
     * According to AUTOSAR SWS_CORE_10131
     */
    inline UInt32 NetworkToHost32(UInt32 networkValue) noexcept
    {
        if constexpr (GetPlatformByteOrder() == ByteOrder::kLittleEndian) {
            return ByteSwap32(networkValue);
        } else {
            return networkValue;
        }
    }

    /**
     * @brief Convert 64-bit value from network to host byte order
     * @param networkValue Value in network byte order
     * @return Value in host byte order
     * According to AUTOSAR SWS_CORE_10132
     */
    inline UInt64 NetworkToHost64(UInt64 networkValue) noexcept
    {
        if constexpr (GetPlatformByteOrder() == ByteOrder::kLittleEndian) {
            return ByteSwap64(networkValue);
        } else {
            return networkValue;
        }
    }

    /**
     * @brief Convert value from host byte order to specified byte order
     * @tparam T Type of value (must be UInt16, UInt32, or UInt64)
     * @param hostValue Value in host byte order
     * @param targetOrder Target byte order
     * @return Value in target byte order
     * According to AUTOSAR SWS_CORE_10140
     */
    template <typename T>
    inline T HostToByteOrder(T hostValue, ByteOrder targetOrder) noexcept
    {
        static_assert(std::is_same<T, UInt16>::value || 
                      std::is_same<T, UInt32>::value || 
                      std::is_same<T, UInt64>::value,
                      "HostToByteOrder only supports UInt16, UInt32, UInt64");

        if (GetPlatformByteOrder() == targetOrder) {
            return hostValue;
        }

        if constexpr (std::is_same<T, UInt16>::value) {
            return ByteSwap16(hostValue);
        } else if constexpr (std::is_same<T, UInt32>::value) {
            return ByteSwap32(hostValue);
        } else if constexpr (std::is_same<T, UInt64>::value) {
            return ByteSwap64(hostValue);
        }
    }

    /**
     * @brief Convert value from specified byte order to host byte order
     * @tparam T Type of value (must be UInt16, UInt32, or UInt64)
     * @param value Value in source byte order
     * @param sourceOrder Source byte order
     * @return Value in host byte order
     * According to AUTOSAR SWS_CORE_10141
     */
    template <typename T>
    inline T ByteOrderToHost(T value, ByteOrder sourceOrder) noexcept
    {
        static_assert(std::is_same<T, UInt16>::value || 
                      std::is_same<T, UInt32>::value || 
                      std::is_same<T, UInt64>::value,
                      "ByteOrderToHost only supports UInt16, UInt32, UInt64");

        if (GetPlatformByteOrder() == sourceOrder) {
            return value;
        }

        if constexpr (std::is_same<T, UInt16>::value) {
            return ByteSwap16(value);
        } else if constexpr (std::is_same<T, UInt32>::value) {
            return ByteSwap32(value);
        } else if constexpr (std::is_same<T, UInt64>::value) {
            return ByteSwap64(value);
        }
    }

    // Aliases for common network/host byte order functions (POSIX-style)
    inline UInt16 htons(UInt16 hostshort) noexcept { return HostToNetwork16(hostshort); }
    inline UInt32 htonl(UInt32 hostlong) noexcept { return HostToNetwork32(hostlong); }
    inline UInt64 htonll(UInt64 hostlonglong) noexcept { return HostToNetwork64(hostlonglong); }
    inline UInt16 ntohs(UInt16 netshort) noexcept { return NetworkToHost16(netshort); }
    inline UInt32 ntohl(UInt32 netlong) noexcept { return NetworkToHost32(netlong); }
    inline UInt64 ntohll(UInt64 netlonglong) noexcept { return NetworkToHost64(netlonglong); }

} // namespace core
} // namespace lap

#endif // LAP_CORE_BYTEORDER_HPP
