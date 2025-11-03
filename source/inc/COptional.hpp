/**
 * @file        COptional.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       AUTOSAR Adaptive Platform Optional class
 * @date        2025-10-29
 * @details     Provides ara::core::Optional as per AUTOSAR AP SWS Core specification
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R22-11 SWS_CORE_01701
 * sdk:
 * platform:
 * project:     LightAP
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/10/29  <td>1.0      <td>ddkv587         <td>init version, AUTOSAR compliant
 * </table>
 */
#ifndef LAP_CORE_OPTIONAL_HPP
#define LAP_CORE_OPTIONAL_HPP

#include "CTypedef.hpp"
#include "CUtility.hpp"

// C++17 has std::optional
#if __cplusplus >= 201703L
    #include <optional>
#else
    #include <boost/optional.hpp>
    #include <boost/none.hpp>
#endif

#include <stdexcept>

namespace lap
{
namespace core
{
    // ========================================================================
    // Optional Type Definition (AUTOSAR SWS_CORE_01701)
    // ========================================================================
    
    /**
     * @brief A type-safe container for an optional value.
     * @tparam T Type of the value
     * 
     * According to AUTOSAR SWS_CORE_01701, this is a type alias to std::optional
     * (C++17) or boost::optional (C++14).
     */
#if __cplusplus >= 201703L
    template <typename T>
    using Optional = std::optional<T>;
    
    // nullopt constant for indicating no value (AUTOSAR SWS_CORE_01711)
    using nullopt_t = std::nullopt_t;
    inline constexpr nullopt_t nullopt = std::nullopt;
    
    // bad_optional_access exception (AUTOSAR SWS_CORE_01712)
    using bad_optional_access = std::bad_optional_access;
#else
    template <typename T>
    using Optional = boost::optional<T>;
    
    // nullopt constant for boost::optional
    using nullopt_t = boost::none_t;
    inline const nullopt_t nullopt = boost::none;
    
    // bad_optional_access exception for boost
    class bad_optional_access : public std::logic_error
    {
    public:
        bad_optional_access() : std::logic_error("bad optional access") {}
    };
#endif

    // Helper functions for Optional (AUTOSAR extensions)
    
    /**
     * @brief Create an Optional with a value
     * @tparam T Value type
     * @param value Value to store
     * @return Optional containing the value
     */
    template <typename T>
    constexpr Optional<std::decay_t<T>> MakeOptional(T&& value)
    {
#if __cplusplus >= 201703L
        return std::make_optional(std::forward<T>(value));
#else
        return boost::make_optional(std::forward<T>(value));
#endif
    }

    /**
     * @brief Create an Optional by constructing in-place
     * @tparam T Value type
     * @tparam Args Constructor argument types
     * @param args Constructor arguments
     * @return Optional containing the constructed value
     */
    template <typename T, typename... Args>
    constexpr Optional<T> MakeOptional(Args&&... args)
    {
#if __cplusplus >= 201703L
        return std::make_optional<T>(std::forward<Args>(args)...);
#else
        return Optional<T>(in_place, std::forward<Args>(args)...);
#endif
    }

    /**
     * @brief Create an Optional by constructing in-place with initializer list
     * @tparam T Value type
     * @tparam U Initializer list element type
     * @tparam Args Additional constructor argument types
     * @param il Initializer list
     * @param args Additional constructor arguments
     * @return Optional containing the constructed value
     */
    template <typename T, typename U, typename... Args>
    constexpr Optional<T> MakeOptional(std::initializer_list<U> il, Args&&... args)
    {
#if __cplusplus >= 201703L
        return std::make_optional<T>(il, std::forward<Args>(args)...);
#else
        return Optional<T>(in_place, il, std::forward<Args>(args)...);
#endif
    }

} // namespace core
} // namespace lap

#endif // LAP_CORE_OPTIONAL_HPP
