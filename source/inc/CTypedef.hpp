/**
 * @file        CTypedef.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       Basic type definitions for AUTOSAR Adaptive Platform
 * @date        2025-10-29
 * @details     Provides fundamental type definitions and basic container aliases.
 *              For enhanced utilities and helper functions, include specific headers:
 *              - CString.hpp for String types and string utilities
 *              - CSpan.hpp for Span type and MakeSpan helpers
 *              - COptional.hpp for Optional type and MakeOptional helpers
 *              - CVariant.hpp for Variant type and visitor utilities
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R22-11 compliant
 * sdk:
 * platform:
 * project:     LightAP
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2023/07/16  <td>1.0      <td>ddkv587         <td>init version
 * <tr><td>2023/08/30  <td>1.1      <td>ddkv587         <td>update definitions
 * <tr><td>2025/10/27  <td>1.2      <td>ddkv587         <td>update header format
 * <tr><td>2025/10/29  <td>2.0      <td>ddkv587         <td>refactor: basic types only, enhanced features in dedicated headers
 * </table>
 */
#ifndef LAP_CORE_TYPEDEF_HPP
#define LAP_CORE_TYPEDEF_HPP

#include <cstdint>
#include <cstddef>
#include <memory>
#include <array>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <chrono>

#include "CMacroDefine.hpp"

namespace lap
{
namespace core
{
    // ========================================================================
    // Integer Types (AUTOSAR SWS_CORE_00001 - 00010)
    // ========================================================================
    
    using Int8                  = ::std::int8_t;
    using UInt8                 = ::std::uint8_t;

    using Int16                 = ::std::int16_t;  
    using UInt16                = ::std::uint16_t;

    using Int32                 = ::std::int32_t;
    using UInt32                = ::std::uint32_t;

    using Int64                 = ::std::int64_t;
    using UInt64                = ::std::uint64_t;

    // Pointer-sized integer types
    using IntPtr                = ::std::intptr_t;
    using UIntPtr               = ::std::uintptr_t;

    // Legacy aliases for compatibility
    using INT                   = Int32;
    using LONG                  = Int32;
    using LLONG                 = Int64;

    using UINT                  = UInt32;
    using ULONG                 = UInt32;
    using ULLONG                = UInt64;

    using Size                  = ::std::size_t;

    // ========================================================================
    // Floating Point Types (AUTOSAR SWS_CORE_00011 - 00012)
    // ========================================================================
    
    using Float                 = float;
    using Double                = double;

    // ========================================================================
    // Character and Boolean Types (AUTOSAR SWS_CORE_00013 - 00015)
    // ========================================================================
    
    using Bool                  = bool;
    using Char                  = char;
    using Byte                  = UInt8;

    // ========================================================================
    // Time Types (AUTOSAR SWS_CORE_00016 - 00017)
    // ========================================================================
    
    using SystemClock           = ::std::chrono::system_clock;
    using SteadyClock           = ::std::chrono::steady_clock;

    // ========================================================================
    // Container Type Aliases (AUTOSAR SWS_CORE_01xxx)
    // Note: For enhanced utilities, include the corresponding header files
    // ========================================================================
    
    /**
     * @brief Fixed-size array container
     * For enhanced features, include CArray.hpp (if available)
     */
    template < typename T, ::std::size_t N >
    using Array                 = ::std::array< T, N >;

    /**
     * @brief Dynamic array container
     * For enhanced features, include CVector.hpp (if available)
     */
    template < typename T, typename Alloc = ::std::allocator< T > >
    using Vector                = ::std::vector< T, Alloc >;

    /**
     * @brief Pair container (key-value pair)
     */
    template < typename T1, typename T2 >
    using Pair                  = ::std::pair< T1, T2 >;

    /**
     * @brief Ordered associative container (key-value pairs)
     */
    template < typename K, typename V, typename Compare = ::std::less< K >, typename Alloc = ::std::allocator< ::std::pair< const K, V > > >
    using Map                   = ::std::map< K, V, Compare, Alloc >;

    /**
     * @brief Ordered set container
     */
    template < typename T, typename Compare = ::std::less< T >, typename Alloc = ::std::allocator< T > >
    using Set                   = ::std::set< T, Compare, Alloc >;

    /**
     * @brief Unordered associative container (hash map)
     */
    template < typename K, typename V, typename Hash = ::std::hash< K >, typename KeyEqual = ::std::equal_to< K >, typename Alloc = ::std::allocator< ::std::pair< const K, V > > >
    using UnorderedMap          = ::std::unordered_map< K, V, Hash, KeyEqual, Alloc >;

    // ========================================================================
    // Smart Pointer Type Aliases (AUTOSAR SWS_CORE_10xxx)
    // ========================================================================
    
    /**
     * @brief Unique ownership smart pointer
     */
    template <typename T>
    using UniqueHandle          = ::std::unique_ptr< T >;

    /**
     * @brief Shared ownership smart pointer
     */
    template <typename T>
    using SharedHandle          = ::std::shared_ptr< T >;

    /**
     * @brief Weak reference to shared ownership smart pointer
     */
    template <typename T>
    using WeakHandle            = ::std::weak_ptr< T >;

    /**
     * @brief Create unique pointer (wrapper for std::make_unique)
     */
    template <typename T, typename... Args>
    inline UniqueHandle<T> MakeUnique(Args&&... args)
    {
        return ::std::make_unique<T>(::std::forward<Args>(args)...);
    }

    /**
     * @brief Create shared pointer (wrapper for std::make_shared)
     */
    template <typename T, typename... Args>
    inline SharedHandle<T> MakeShared(Args&&... args)
    {
        return ::std::make_shared<T>(::std::forward<Args>(args)...);
    }

} // core
} // lap

#endif // LAP_CORE_TYPEDEF_HPP


