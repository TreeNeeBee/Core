/**
 * @file        CViolation.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       AUTOSAR-compliant violation handling
 * @date        2025-11-12
 * @version     1.0.0
 * 
 * @copyright   Copyright (c) 2025
 * 
 * @par AUTOSAR Specification References:
 *      - [SWS_CORE_00021] Violation is a non-recoverable condition
 *      - [SWS_CORE_00091] Violation message must be standardized
 *      - [SWS_CORE_00003] Non-standard violations must terminate process
 * 
 * @par Thread Safety:
 *      RaiseViolation() is thread-safe and signal-safe
 */

#ifndef LAP_CORE_CVIOLATION_HPP
#define LAP_CORE_CVIOLATION_HPP

#include "CTypedef.hpp"

namespace lap
{
namespace core
{
    /**
     * @brief Violation type enumeration
     * 
     * Defines standard violation categories as per AUTOSAR requirements.
     * All violations are non-recoverable and will terminate the process.
     * 
     * @threadsafe Type definition is inherently thread-safe
     */
    enum class ViolationType : UInt32
    {
        /** Platform/Core not properly initialized */
        kPlatformNotInitialized = 1,
        
        /** Invalid argument passed to function (precondition failure) */
        kInvalidArgument = 2,
        
        /** Required configuration missing or invalid */
        kConfigurationMissing = 3,
        
        /** Resource exhausted (memory, handles, etc.) */
        kResourceExhausted = 4,
        
        /** Internal state corruption detected */
        kStateCorruption = 5,
        
        /** Unrecoverable error from external system */
        kExternalSystemFailure = 6,
        
        /** Security policy violation */
        kSecurityViolation = 7,
        
        /** Generic assertion failure */
        kAssertionFailure = 8
    };
    
    /**
     * @brief Raise a violation and terminate the process
     * 
     * This function implements AUTOSAR-compliant violation handling.
     * It logs the violation with context information and terminates
     * the process via std::abort().
     * 
     * Per [SWS_CORE_00021], violations are analogous to failed assertions
     * and represent non-recoverable conditions. The process cannot continue.
     * 
     * Per [SWS_CORE_00091], the violation message must include:
     * - Violation type
     * - Descriptive message
     * - Source location (file, line) when available
     * 
     * @param type The type of violation that occurred
     * @param message Descriptive message explaining the violation context
     * @param file Source file where violation occurred (use __FILE__)
     * @param line Source line where violation occurred (use __LINE__)
     * 
     * @note This function never returns - it always terminates via std::abort()
     * @note This function is signal-safe and can be called from signal handlers
     * @threadsafe Thread-safe - uses only async-signal-safe functions
     */
    [[noreturn]] void RaiseViolation(
        ViolationType type,
        const Char* message,
        const Char* file = nullptr,
        UInt32 line = 0
    ) noexcept;
    
    /**
     * @brief Get string representation of violation type
     * 
     * @param type The violation type
     * @return const char* String name of the violation type
     * @threadsafe Thread-safe - returns pointer to static string
     */
    const Char* ViolationTypeToString(ViolationType type) noexcept;
    
    /**
     * @brief Macro for raising violations with automatic file/line
     * 
     * Usage:
     * @code
     * if (!ptr) {
     *     LAP_RAISE_VIOLATION(ViolationType::kInvalidArgument, 
     *                         "Null pointer passed to function");
     * }
     * @endcode
     */
    #define LAP_RAISE_VIOLATION(type, message) \
        ::lap::core::RaiseViolation((type), (message), __FILE__, __LINE__)
    
    /**
     * @brief Macro for assertion-style violation checking
     * 
     * Usage:
     * @code
     * LAP_ASSERT(ptr != nullptr, "Pointer must not be null");
     * @endcode
     */
    #define LAP_ASSERT(condition, message) \
        do { \
            if (!(condition)) { \
                ::lap::core::RaiseViolation( \
                    ::lap::core::ViolationType::kAssertionFailure, \
                    (message), __FILE__, __LINE__); \
            } \
        } while(0)

} // namespace core
} // namespace lap

#endif // LAP_CORE_CVIOLATION_HPP
