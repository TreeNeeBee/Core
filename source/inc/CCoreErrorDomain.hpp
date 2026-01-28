/**
 * @file        CCoreErrorDomain.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       Core error domain for AUTOSAR Adaptive Platform
 * @date        2025-10-27
 * @details     Provides core error codes and error domain implementation
 * @copyright   Copyright (c) 2025
 * @note
 * sdk:
 * platform:
 * project:     LightAP
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2023/07/16  <td>1.0      <td>ddkv587         <td>init version
 * <tr><td>2023/09/09  <td>1.1      <td>ddkv587         <td>update implementation
 * <tr><td>2025/10/27  <td>1.2      <td>ddkv587         <td>update header format
 * </table>
 */
#ifndef LAP_CORE_COREERRORDOMAIN_HPP
#define LAP_CORE_COREERRORDOMAIN_HPP

#include "CTypedef.hpp"
#include "CErrorDomain.hpp"
#include "CErrorCode.hpp"
#include "CException.hpp"

namespace lap
{
namespace core
{
    enum class CoreErrc : ErrorDomain::CodeType 
    {
        kInvalidArgument            = 22,
        kInvalidMetaModelShortname  = 137,
        kInvalidMetaModelPath       = 138,
        kAlreadyInitialized         = 139,  // Platform already initialized
        kNotInitialized             = 140,  // Platform not initialized
        kInternalError              = 141,  // Internal initialization error
        kOutOfMemory                = 142,  // Out of memory
        kResourceExhausted          = 143,  // Resource exhausted (e.g., max chunks reached)
        kWouldBlock                 = 144,  // Operation would block (no data available)
        
        // IPC Error Codes (145-170)
        kIPCShmCreateFailed         = 145,  // Failed to create shared memory
        kIPCShmNotFound             = 146,  // Shared memory not found
        kIPCShmMapFailed            = 147,  // Failed to map shared memory
        kIPCShmStatFailed           = 148,  // Failed to stat shared memory
        kIPCShmInvalidMagic         = 149,  // Invalid magic number in shared memory
        kIPCChunkPoolExhausted      = 150,  // Chunk pool exhausted
        kIPCQueueFull               = 151,  // Subscriber queue full
        kIPCQueueEmpty              = 152,  // Subscriber queue empty
        kIPCInvalidChannelIndex     = 153,  // Invalid queue index
        kIPCChannelAlreadyInUse     = 154,  // Queue index already in use
        kIPCRetry                   = 155,  // Retry operation
        kIPCInvalidChunkIndex       = 156,  // Invalid chunk index
        kIPCInvalidState            = 157,  // Invalid chunk state
        
        // Channel Error Codes (158-170)
        kChannelInvalid             = 158,  // Channel is not initialized or invalid
        kChannelFull                = 159,  // Channel queue is full (write failed)
        kChannelEmpty               = 160,  // Channel queue is empty (read failed)
        kChannelTimeout             = 161,  // Operation timed out
        kChannelWaitsetUnavailable  = 162,  // Waitset pointer is null
        kChannelWriteFailed         = 163,  // Write operation failed
        kChannelReadFailed          = 164,  // Read operation failed
        kChannelPolicyNotSupported  = 165,  // Policy not supported
        kChannelSpuriousWakeup      = 166,  // Spurious wakeup occurred
        kChannelNotFound            = 167   // Channel not found
    };

    inline constexpr const Char* CoreErrMessage( CoreErrc errCode )
    {
        auto const code = static_cast<CoreErrc>( errCode );

        switch ( code ) {
        case CoreErrc::kInvalidArgument:
            return "An invalid argument was passed to a function";
        case CoreErrc::kInvalidMetaModelShortname:
            return "Given string is not a valid model element shortname";
        case CoreErrc::kInvalidMetaModelPath:
            return "Missing or invalid path to model element";
        case CoreErrc::kAlreadyInitialized:
            return "Platform is already initialized";
        case CoreErrc::kNotInitialized:
            return "Platform is not initialized";
        case CoreErrc::kInternalError:
            return "Internal error during initialization/de-initialization";
        case CoreErrc::kOutOfMemory:
            return "Out of memory";
        case CoreErrc::kResourceExhausted:
            return "Resource exhausted";
        case CoreErrc::kWouldBlock:
            return "Operation would block (no data available)";
        case CoreErrc::kIPCShmCreateFailed:
            return "Failed to create shared memory";
        case CoreErrc::kIPCShmNotFound:
            return "Shared memory not found";
        case CoreErrc::kIPCShmMapFailed:
            return "Failed to map shared memory";
        case CoreErrc::kIPCShmStatFailed:
            return "Failed to stat shared memory";
        case CoreErrc::kIPCShmInvalidMagic:
            return "Invalid magic number in shared memory";
        case CoreErrc::kIPCChunkPoolExhausted:
            return "Chunk pool exhausted";
        case CoreErrc::kIPCQueueFull:
            return "Subscriber queue full";
        case CoreErrc::kIPCQueueEmpty:
            return "Subscriber queue empty";
        case CoreErrc::kIPCInvalidChannelIndex:
            return "Invalid channel index";
        case CoreErrc::kIPCChannelAlreadyInUse:
            return "Channel index already in use";
        case CoreErrc::kIPCRetry:
            return "Operation should be retried";
        case CoreErrc::kIPCInvalidChunkIndex:
            return "Invalid chunk index";
        case CoreErrc::kIPCInvalidState:
            return "Invalid chunk state";
        case CoreErrc::kChannelInvalid:
            return "Channel is not initialized or invalid";
        case CoreErrc::kChannelFull:
            return "Channel queue is full";
        case CoreErrc::kChannelEmpty:
            return "Channel queue is empty";
        case CoreErrc::kChannelTimeout:
            return "Channel operation timed out";
        case CoreErrc::kChannelWaitsetUnavailable:
            return "Channel waitset is unavailable";
        case CoreErrc::kChannelWriteFailed:
            return "Channel write operation failed";
        case CoreErrc::kChannelReadFailed:
            return "Channel read operation failed";
        case CoreErrc::kChannelPolicyNotSupported:
            return "Channel policy not supported";
        case CoreErrc::kChannelSpuriousWakeup:
            return "Channel spurious wakeup occurred";
        case CoreErrc::kChannelNotFound:
            return "Channel not found";
        default:
            return "Unknown error";
        }
    }

    class CoreException : public Exception 
    {
    public:

        explicit CoreException ( ErrorCode errorCode ) noexcept
            : Exception( errorCode )
        {
            ;
        }

        const Char* what() const noexcept 
        {
            return CoreErrMessage( static_cast< CoreErrc > ( Error().Value() ) );
        }
    };

    class CoreErrorDomain final : public ErrorDomain 
    {
    public:
        using Errc          = CoreErrc;
        using Exception     = CoreException;

    public:

        const Char*     Name () const noexcept override                                                     { return "Core"; }
        const Char*     Message ( CodeType errorCode ) const noexcept override                              { return CoreErrMessage( static_cast< Errc > ( errorCode ) ); }
        void            ThrowAsException ( const ErrorCode &errorCode ) const noexcept( false ) override    { throw Exception( errorCode ); }

        constexpr CoreErrorDomain() noexcept
            : ErrorDomain( ErrorDomain::IdType{ 0x8000000000000014 } )
        {
            ;
        }
    };

    static constexpr CoreErrorDomain g_coreErrorDomain;
    
    constexpr const ErrorDomain& GetCoreErrorDomain () noexcept
    {
        return g_coreErrorDomain;
    }

    // Provide a default support-data value so callers may invoke MakeErrorCode(code)
    // as a convenience (matches common usage in tests and clients).
    constexpr ErrorCode MakeErrorCode ( CoreErrc code, ErrorDomain::SupportDataType data = ErrorDomain::SupportDataType() ) noexcept
    {
        return { static_cast< ErrorDomain::CodeType >( code ), GetCoreErrorDomain(), data };
    }
} // core
} // ara

#endif