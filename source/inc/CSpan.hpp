/**
 * @file        CSpan.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       AUTOSAR Adaptive Platform Span utilities
 * @date        2025-10-29
 * @details     Provides span utilities and helper functions as per AUTOSAR AP SWS Core specification
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R22-11 SWS_CORE_01801 - Span utilities
 * sdk:
 * platform:
 * project:     LightAP
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/10/29  <td>1.0      <td>ddkv587         <td>init version, AUTOSAR compliant
 * </table>
 */
#ifndef LAP_CORE_SPAN_HPP
#define LAP_CORE_SPAN_HPP

#include "CTypedef.hpp"
#include <array>
#include <vector>

// C++20 features: span
#if __cplusplus >= 202002L
    #include <span>
#else
    #include <boost/beast/core/span.hpp>
#endif

namespace lap
{
namespace core
{
    // ========================================================================
    // Span Type Definition (AUTOSAR SWS_CORE_01801)
    // ========================================================================
    
    /**
     * @brief Non-owning view over a contiguous sequence
     * Prefer std::span (C++20), fallback to boost::beast::span
     */
#if __cplusplus >= 202002L
    template < typename T >
    using Span                  = ::std::span< T >;
#else
    template < typename T >
    using Span                  = ::boost::beast::span< T >;
#endif
    
    // dynamic_extent constant for AUTOSAR compliance
#if __cplusplus >= 202002L
    inline constexpr std::size_t dynamic_extent = std::dynamic_extent;
#else
    inline constexpr std::size_t dynamic_extent = static_cast<std::size_t>(-1);
#endif

    // Helper functions for creating spans (AUTOSAR extensions)
    
    /**
     * @brief Create a Span from a C-style array
     * @tparam T Element type
     * @tparam N Array size
     * @param arr C-style array
     * @return Span view over the array
     */
    template <typename T, std::size_t N>
    constexpr Span<T> MakeSpan(T (&arr)[N]) noexcept
    {
        return Span<T>(arr, N);
    }

    /**
     * @brief Create a Span from std::array
     * @tparam T Element type
     * @tparam N Array size
     * @param arr std::array
     * @return Span view over the array
     */
    template <typename T, std::size_t N>
    constexpr Span<T> MakeSpan(Array<T, N>& arr) noexcept
    {
        return Span<T>(arr.data(), arr.size());
    }

    /**
     * @brief Create a const Span from const std::array
     * @tparam T Element type
     * @tparam N Array size
     * @param arr const std::array
     * @return const Span view over the array
     */
    template <typename T, std::size_t N>
    constexpr Span<const T> MakeSpan(const Array<T, N>& arr) noexcept
    {
        return Span<const T>(arr.data(), arr.size());
    }

    /**
     * @brief Create a Span from std::vector
     * @tparam T Element type
     * @param vec std::vector
     * @return Span view over the vector
     */
    template <typename T>
    Span<T> MakeSpan(Vector<T>& vec) noexcept
    {
        return Span<T>(vec.data(), vec.size());
    }

    /**
     * @brief Create a const Span from const std::vector
     * @tparam T Element type
     * @param vec const std::vector
     * @return const Span view over the vector
     */
    template <typename T>
    Span<const T> MakeSpan(const Vector<T>& vec) noexcept
    {
        return Span<const T>(vec.data(), vec.size());
    }

    /**
     * @brief Create a Span from pointer and size
     * @tparam T Element type
     * @param ptr Pointer to first element
     * @param count Number of elements
     * @return Span view over the range
     */
    template <typename T>
    constexpr Span<T> MakeSpan(T* ptr, std::size_t count) noexcept
    {
        return Span<T>(ptr, count);
    }

    /**
     * @brief Create a Span from pointer range
     * @tparam T Element type
     * @param first Pointer to first element
     * @param last Pointer past the last element
     * @return Span view over the range
     */
    template <typename T>
    constexpr Span<T> MakeSpan(T* first, T* last) noexcept
    {
#if __cplusplus >= 202002L
        return Span<T>(first, last);
#else
        return Span<T>(first, static_cast<std::size_t>(last - first));
#endif
    }

} // namespace core
} // namespace lap

#endif // LAP_CORE_SPAN_HPP
