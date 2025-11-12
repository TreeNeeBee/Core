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
        kInternalError              = 141   // Internal initialization error
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
        default:
            return "Unknown error";
        }
    }

    class CoreException : public Exception 
    {
    public:
        IMP_OPERATOR_NEW(CoreException)

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
        IMP_OPERATOR_NEW(CoreErrorDomain)

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