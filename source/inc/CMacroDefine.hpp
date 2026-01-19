/**
 * @file        CMacroDefine.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       Macro definitions for AUTOSAR Adaptive Platform
 * @date        2025-10-27
 * @details     Provides common macro definitions used throughout the platform
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
#ifndef LAP_CORE_MACRODEFINE_HPP
#define LAP_CORE_MACRODEFINE_HPP

#ifdef LAP_BUILD_ENABLE_ASSERT
    #include <cassert>
#endif

// Define INNER_CORE_LOG outside of namespaces to access global std
#ifdef __INNER_LOG_ENABLE
#include <cstdio>
// Accept one or more arguments and forward directly to fprintf.
// Using a single variadic pack avoids warnings when no extra args are provided.
#define INNER_CORE_LOG(...) do { std::fprintf(stderr, __VA_ARGS__); } while(0)
#else
#define INNER_CORE_LOG(...) do { } while(0)
#endif

namespace lap
{
namespace core
{

    #ifndef UNUSED
    #define UNUSED( X )                             ( void )( X )
    #endif

    #if GCC_VERSION > 40000
    #define DEF_LAP_DEBUG_FUNCTION                  __attribute__((used, cold))
    #define DEF_LAP_DEBUG_VARIABLE                  __attribute__((used, cold))
    #else 
    #define DEF_LAP_DEBUG_FUNCTION
    #define DEF_LAP_DEBUG_VARIABLE
    #endif

    #define DEF_LAP_MIN( a, b )                      ( ( (a) < (b) ) ? (a) : (b) )
    #define DEF_LAP_MAX( a, b )                      ( ( (a) > (b) ) ? (a) : (b) )

    #define DEF_LAP_IF_LIKELY( x )                  __builtin_expect(!!(x), 1)
    #define DEF_LAP_IF_UNLIKELY( x )                __builtin_expect(!!(x), 0)

    #define DEF_LAP_CACHELINE_ALIGN                 alignas(64)   ///< Align to cache line size (64 bytes) 
    #define DEF_LAP_ATT_PACKAGE                     __attribute__((packed))   ///< Pack structure (no padding)

    #if __x86_64__ || __ppc64__ || __aarch64__
    #define DEF_LAP_ATT_SYS_ALIGN                   __attribute__((aligned(8)))
    #define DEF_LAP_SYS_ALIGN                       alignas(8)   ///< C++11 standard alignment for struct
    #else
    #define DEF_LAP_ATT_SYS_ALIGN                   __attribute__((aligned(4)))
    #define DEF_LAP_SYS_ALIGN                       alignas(4)   ///< C++11 standard alignment for struct
    #endif

    #define DEF_LAP_ALIGN_FORMAT( x, _alignSize )  ( ( ( ( x ) + _alignSize - 1 ) / _alignSize ) * _alignSize )

    #ifdef LAP_BUILD_ENABLE_ASSERT
        #define DEF_LAP_ASSERT( x, msg )            assert( x && msg )
    #else
        #define DEF_LAP_ASSERT( x, msg )
    #endif

    #define DEF_LAP_STATIC_ASSERT( x, msg )         static_assert( x, msg )


    #define DEF_LAP_STATIC_ASSERT_CACHELINE( x ) \
        DEF_LAP_STATIC_ASSERT( sizeof( x ) % 64 == 0, "Size of " #x " must be multiple of cache line size (64 bytes)" )

    #define DEF_LAP_STATIC_ASSERT_CACHELINE_MATCH( x, cache_line ) \
        DEF_LAP_STATIC_ASSERT( sizeof( x ) <= cache_line, "Size of " #x " must be less or match cache line size (" #cache_line " bytes)" )

    // ========================================================================
    // Symbol Visibility Control for Shared Libraries
    // ========================================================================
    
    /**
     * @brief Symbol visibility macros for controlling exports in shared libraries
     * @note GCC Wiki: https://gcc.gnu.org/wiki/Visibility
     * 
     * Usage:
     *   LAP_API void PublicFunction();              // Export function
     *   class LAP_API PublicClass { ... };          // Export class
     *   LAP_LOCAL void InternalFunction();          // Hide function
     *   LAP_DEPRECATED("Use NewAPI") void OldAPI(); // Mark deprecated
     * 
     * Module-specific variants:
     *   LAP_CORE_API, LAP_COM_API, LAP_LOG_API, etc.
     *   Define MODULE_NAME_BUILDING when compiling the module
     */
    
    /**
     * @def LAP_API_EXPORT
     * @brief Generic export macro for shared library symbols
     */
    #if defined(_WIN32) || defined(_WIN64)
        // Windows: DLL export/import
        #define LAP_API_EXPORT __declspec(dllexport)
        #define LAP_API_IMPORT __declspec(dllimport)
    #elif defined(__GNUC__) || defined(__clang__)
        // GCC/Clang: Visibility attribute
        #define LAP_API_EXPORT __attribute__((visibility("default")))
        #define LAP_API_IMPORT __attribute__((visibility("default")))
    #else
        // Unknown compiler
        #define LAP_API_EXPORT
        #define LAP_API_IMPORT
        #warning "Unknown compiler - symbol visibility not controlled"
    #endif
    
    /**
     * @def LAP_LOCAL
     * @brief Mark symbol as internal (hidden from shared library users)
     */
    #if defined(__GNUC__) || defined(__clang__)
        #define LAP_LOCAL __attribute__((visibility("hidden")))
    #else
        #define LAP_LOCAL
    #endif
    
    /**
     * @def LAP_DEPRECATED
     * @brief Mark symbol as deprecated with custom message
     */
    #if defined(__GNUC__) || defined(__clang__)
        #define LAP_DEPRECATED(msg) __attribute__((deprecated(msg)))
    #elif defined(_MSC_VER)
        #define LAP_DEPRECATED(msg) __declspec(deprecated(msg))
    #else
        #define LAP_DEPRECATED(msg)
    #endif
    
    // Generic LAP API (for modules without specific macro)
    #define LAP_API LAP_API_EXPORT
    
} // core
} // lap

#endif
