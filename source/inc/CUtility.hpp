/**
 * @file        CUtility.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       Utility types for AUTOSAR Adaptive Platform
 * @date        2025-10-29
 * @details     Provides utility types and helper functions for AUTOSAR compliance
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R22-11 SWS_CORE_20xxx - Utility
 * sdk:
 * platform:
 * project:     LightAP
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2023/07/16  <td>1.0      <td>ddkv587         <td>init version
 * <tr><td>2025/10/27  <td>1.1      <td>ddkv587         <td>update header format
 * <tr><td>2025/10/29  <td>2.0      <td>ddkv587         <td>add AUTOSAR standard utilities
 * </table>
 */
#ifndef LAP_CORE_UTILITY_HPP
#define LAP_CORE_UTILITY_HPP

#include <initializer_list>
#include <cstddef>
#include "CTypedef.hpp"

namespace lap
{
namespace core
{
    // ========================================================================
    // In-place construction tags (AUTOSAR SWS_CORE_20100 - 20120)
    // ========================================================================
    
    /**
     * @brief Tag type for in-place construction
     * According to AUTOSAR SWS_CORE_20100
     */
    struct in_place_t 
    {
        explicit in_place_t () = default;
    };
    constexpr in_place_t in_place;

    /**
     * @brief Tag type for in-place construction with type
     * @tparam T Type to construct
     * According to AUTOSAR SWS_CORE_20110
     */
    template < typename T >
    struct in_place_type_t 
    {
        explicit in_place_type_t () = default;
    };
    template < typename T >
    constexpr in_place_type_t< T > in_place_type;

    /**
     * @brief Tag type for in-place construction with index
     * @tparam I Index value
     * According to AUTOSAR SWS_CORE_20120
     */
    template < ::std::size_t I >
    struct in_place_index_t 
    {
        explicit in_place_index_t () = default;
    };
    template < ::std::size_t I >
    constexpr in_place_index_t< I > in_place_index {};

    // ========================================================================
    // Move and Forward utilities (AUTOSAR SWS_CORE_20200 - 20220)
    // ========================================================================

    /**
     * @brief Cast to rvalue reference (move semantics)
     * @tparam T Type to move
     * @param value Value to move
     * @return Rvalue reference
     * According to AUTOSAR SWS_CORE_20200
     */
    template <typename T>
    constexpr typename std::remove_reference<T>::type&& Move(T&& value) noexcept
    {
        return std::move(value);
    }

    /**
     * @brief Perfect forwarding
     * @tparam T Type to forward
     * @param value Value to forward
     * @return Forwarded reference
     * According to AUTOSAR SWS_CORE_20210
     */
    template <typename T>
    constexpr T&& Forward(typename std::remove_reference<T>::type& value) noexcept
    {
        return std::forward<T>(value);
    }

    template <typename T>
    constexpr T&& Forward(typename std::remove_reference<T>::type&& value) noexcept
    {
        return std::forward<T>(value);
    }

    /**
     * @brief Swap two values
     * @tparam T Type of values
     * @param a First value
     * @param b Second value
     * According to AUTOSAR SWS_CORE_20220
     */
    template <typename T>
    constexpr void Swap(T& a, T& b) noexcept(std::is_nothrow_move_constructible<T>::value &&
                                              std::is_nothrow_move_assignable<T>::value)
    {
        std::swap(a, b);
    }

    // ========================================================================
    // Container data access (AUTOSAR SWS_CORE_20300 - 20320)
    // ========================================================================

    /**
     * @brief Get pointer to container's data
     * @tparam Container Container type
     * @param c Container
     * @return Pointer to data
     * According to AUTOSAR SWS_CORE_20300
     */
    template <typename Container>
    constexpr auto data ( Container &c ) -> decltype( c.data() )
    {
        return c.data();
    }

    template <typename Container>
    constexpr auto data (const Container &c) -> decltype(c.data())
    {
        return c.data();
    }

    template <typename T, ::std::size_t N>
    constexpr T* data ( T( &array )[N] ) noexcept
    {
        return array;
    }

    template <typename T, ::std::size_t N>
    constexpr const T* data ( const T( &array )[N] ) noexcept
    {
        return array;
    }

    template <typename E>
    constexpr const E* data ( ::std::initializer_list< E > il ) noexcept
    {
        return il.begin();
    }

    /**
     * @brief Get size of container
     * @tparam Container Container type
     * @param c Container
     * @return Size
     * According to AUTOSAR SWS_CORE_20310
     */
    template <typename Container>
    constexpr auto size ( const Container &c ) -> decltype(c.size())
    {
        return c.size();
    }

    template <typename T, ::std::size_t N>
    constexpr ::std::size_t size ( const T( &array )[N] ) noexcept
    {
        UNUSED(array);
        return N;
    }

    /**
     * @brief Check if container is empty
     * @tparam Container Container type
     * @param c Container
     * @return true if empty, false otherwise
     * According to AUTOSAR SWS_CORE_20320
     */
    template <typename Container>
    constexpr auto empty ( const Container &c ) -> decltype( c.empty() )
    {
        return c.empty();
    }

    template <typename T, ::std::size_t N>
    constexpr bool empty ( const T( &array )[N] ) noexcept
    {
        UNUSED( array );
        return N == 0;
    }

    template <typename E>
    constexpr bool empty ( ::std::initializer_list< E > il ) noexcept
    {
        return il.size() == 0;
    }

    /**
     * @brief Get signed size of container (C++20 backport)
     * @tparam Container Container type
     * @param c Container
     * @return Signed size
     * According to AUTOSAR SWS_CORE_20330
     */
    template <typename Container>
    constexpr auto ssize ( const Container &c ) noexcept -> ::std::ptrdiff_t
    {
        return static_cast< ::std::ptrdiff_t >( c.size() );
    }

    template <typename T, ::std::size_t N>
    constexpr ::std::ptrdiff_t ssize ( const T( & )[N] ) noexcept
    {
        return static_cast< ::std::ptrdiff_t >( N );
    }

    // ========================================================================
    // Type traits helpers (AUTOSAR SWS_CORE_20400 - 20499)
    // ========================================================================

    /**
     * @brief Remove const and volatile qualifiers from type
     * @tparam T Type to modify
     * According to AUTOSAR SWS_CORE_20400
     */
    template <typename T>
    using RemoveCV = typename std::remove_cv<T>::type;

    /**
     * @brief Remove reference from type
     * @tparam T Type to modify
     * According to AUTOSAR SWS_CORE_20401
     */
    template <typename T>
    using RemoveReference = typename std::remove_reference<T>::type;

    /**
     * @brief Remove const, volatile, and reference from type
     * @tparam T Type to modify
     * According to AUTOSAR SWS_CORE_20402
     */
    template <typename T>
    using RemoveCVRef = RemoveCV<RemoveReference<T>>;

    /**
     * @brief Add const qualifier to type
     * @tparam T Type to modify
     * According to AUTOSAR SWS_CORE_20410
     */
    template <typename T>
    using AddConst = typename std::add_const<T>::type;

    /**
     * @brief Add lvalue reference to type
     * @tparam T Type to modify
     * According to AUTOSAR SWS_CORE_20411
     */
    template <typename T>
    using AddLValueReference = typename std::add_lvalue_reference<T>::type;

    /**
     * @brief Add rvalue reference to type
     * @tparam T Type to modify
     * According to AUTOSAR SWS_CORE_20412
     */
    template <typename T>
    using AddRValueReference = typename std::add_rvalue_reference<T>::type;

    /**
     * @brief Decay type (remove reference and cv-qualifiers, convert arrays/functions to pointers)
     * @tparam T Type to decay
     * According to AUTOSAR SWS_CORE_20420
     */
    template <typename T>
    using Decay = typename std::decay<T>::type;

    /**
     * @brief Enable if type trait
     * @tparam B Boolean condition
     * @tparam T Type to enable
     * According to AUTOSAR SWS_CORE_20430
     */
    template <bool B, typename T = void>
    using EnableIf = typename std::enable_if<B, T>::type;

    /**
     * @brief Conditional type selection
     * @tparam B Boolean condition
     * @tparam T Type if true
     * @tparam F Type if false
     * According to AUTOSAR SWS_CORE_20431
     */
    template <bool B, typename T, typename F>
    using Conditional = typename std::conditional<B, T, F>::type;

} // namespace core
} // namespace lap
#endif