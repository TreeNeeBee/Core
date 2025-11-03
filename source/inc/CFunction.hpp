/**
 * @file        CFunction.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       Function wrapper utilities for AUTOSAR Adaptive Platform
 * @date        2025-10-29
 * @details     Provides function wrapper and binding utilities
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R22-11 SWS_CORE_03xxx - Function
 * sdk:
 * platform:
 * project:     LightAP
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/10/29  <td>1.0      <td>ddkv587         <td>init version, AUTOSAR compliant
 * </table>
 */
#ifndef LAP_CORE_FUNCTION_HPP
#define LAP_CORE_FUNCTION_HPP

#include "CTypedef.hpp"
#include <functional>
#include <type_traits>

namespace lap
{
namespace core
{
    /**
     * @brief Type-erased function wrapper
     * @tparam Signature Function signature
     * 
     * According to AUTOSAR SWS_CORE_03100, this is a type alias to std::function
     */
    template <typename Signature>
    using Function = ::std::function<Signature>;

    /**
     * @brief Invoke a callable with arguments
     * @tparam Callable Callable type
     * @tparam Args Argument types
     * @param func Callable object
     * @param args Arguments to pass
     * @return Result of invocation
     * According to AUTOSAR SWS_CORE_03200
     */
#if __cplusplus >= 201703L
    template <typename Callable, typename... Args>
    constexpr decltype(auto) Invoke(Callable&& func, Args&&... args)
        noexcept(noexcept(std::invoke(std::forward<Callable>(func), std::forward<Args>(args)...)))
    {
        return std::invoke(std::forward<Callable>(func), std::forward<Args>(args)...);
    }
#else
    // C++14 fallback implementation
    template <typename Callable, typename... Args>
    constexpr auto Invoke(Callable&& func, Args&&... args)
        -> decltype(std::forward<Callable>(func)(std::forward<Args>(args)...))
    {
        return std::forward<Callable>(func)(std::forward<Args>(args)...);
    }
#endif

    /**
     * @brief Bind arguments to a callable
     * @tparam Callable Callable type
     * @tparam Args Argument types
     * @param func Callable object
     * @param args Arguments to bind
     * @return Bound function object
     * According to AUTOSAR SWS_CORE_03210
     */
    template <typename Callable, typename... Args>
    auto Bind(Callable&& func, Args&&... args)
        -> decltype(std::bind(std::forward<Callable>(func), std::forward<Args>(args)...))
    {
        return std::bind(std::forward<Callable>(func), std::forward<Args>(args)...);
    }

    /**
     * @brief Reference wrapper for objects
     * @tparam T Type to wrap
     * 
     * According to AUTOSAR SWS_CORE_03300, this is a type alias to std::reference_wrapper
     */
    template <typename T>
    using ReferenceWrapper = ::std::reference_wrapper<T>;

    /**
     * @brief Create a reference wrapper
     * @tparam T Type to wrap
     * @param value Value to wrap
     * @return Reference wrapper
     * According to AUTOSAR SWS_CORE_03301
     */
    template <typename T>
    constexpr ReferenceWrapper<T> Ref(T& value) noexcept
    {
        return std::ref(value);
    }

    /**
     * @brief Create a const reference wrapper
     * @tparam T Type to wrap
     * @param value Value to wrap
     * @return Const reference wrapper
     * According to AUTOSAR SWS_CORE_03302
     */
    template <typename T>
    constexpr ReferenceWrapper<const T> CRef(const T& value) noexcept
    {
        return std::cref(value);
    }

    /**
     * @brief Hash function object
     * @tparam T Type to hash
     * 
     * According to AUTOSAR SWS_CORE_03400
     */
    template <typename T>
    using Hash = ::std::hash<T>;

    /**
     * @brief Equal comparison function object
     * @tparam T Type to compare
     * 
     * According to AUTOSAR SWS_CORE_03410
     */
    template <typename T = void>
    using EqualTo = ::std::equal_to<T>;

    /**
     * @brief Not-equal comparison function object
     * @tparam T Type to compare
     * 
     * According to AUTOSAR SWS_CORE_03411
     */
    template <typename T = void>
    using NotEqualTo = ::std::not_equal_to<T>;

    /**
     * @brief Less-than comparison function object
     * @tparam T Type to compare
     * 
     * According to AUTOSAR SWS_CORE_03412
     */
    template <typename T = void>
    using Less = ::std::less<T>;

    /**
     * @brief Less-or-equal comparison function object
     * @tparam T Type to compare
     * 
     * According to AUTOSAR SWS_CORE_03413
     */
    template <typename T = void>
    using LessEqual = ::std::less_equal<T>;

    /**
     * @brief Greater-than comparison function object
     * @tparam T Type to compare
     * 
     * According to AUTOSAR SWS_CORE_03414
     */
    template <typename T = void>
    using Greater = ::std::greater<T>;

    /**
     * @brief Greater-or-equal comparison function object
     * @tparam T Type to compare
     * 
     * According to AUTOSAR SWS_CORE_03415
     */
    template <typename T = void>
    using GreaterEqual = ::std::greater_equal<T>;

    // ========================================================================
    // Placeholder arguments for bind (AUTOSAR SWS_CORE_03500)
    // ========================================================================
    
    namespace placeholders
    {
        using namespace ::std::placeholders;
    } // namespace placeholders

} // namespace core
} // namespace lap

#endif // LAP_CORE_FUNCTION_HPP
