/**
 * @file        CFutureErrorDomain.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       Future error domain for AUTOSAR Adaptive Platform
 * @date        2025-10-27
 * @details     Provides error handling for Future and Promise operations
 * @copyright   Copyright (c) 2025
 * @note
 * sdk:
 * platform:
 * project:     LightAP
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2023/07/16  <td>1.0      <td>ddkv587         <td>init version
 * <tr><td>2023/08/30  <td>1.1      <td>ddkv587         <td>update implementation
 * <tr><td>2025/10/27  <td>1.2      <td>ddkv587         <td>update header format
 * </table>
 */
#ifndef LAP_CORE_FUTUREERRORDOMAIN_HPP
#define LAP_CORE_FUTUREERRORDOMAIN_HPP

#include "CTypedef.hpp"
#include "CErrorCode.hpp"
#include "CException.hpp"

namespace lap
{
namespace core
{
    enum class FutureErrc : Int32
    {
        kBrokenPromise              = 101,
        kFutureAlreadyRetrieved     = 102,
        kPromiseAlreadySatisfied    = 103,
        kNoState                    = 104 
    };

    inline constexpr const Char* FutureErrMessage( FutureErrc errCode )
    {
        auto const code = static_cast<FutureErrc>( errCode );

        switch ( code ) {
        case FutureErrc::kBrokenPromise:
            return "the asynchronous task abandoned its shared state";
        case FutureErrc::kFutureAlreadyRetrieved:
            return "the contents of the shared state were already accessed";
        case FutureErrc::kPromiseAlreadySatisfied:
            return "attempt to store a value into the shared state twice";
        case FutureErrc::kNoState:
            return "attempt to access Promise or Future without an associated state";
        default:
            return "Unknown error";
        }
    }

    class FutureException : public Exception
    {
    public:
        explicit FutureException ( ErrorCode errorCode ) noexcept
            : Exception( errorCode )
        {
            ;
        }

        const Char* what() const noexcept 
        {
            return FutureErrMessage( static_cast< FutureErrc > ( Error().Value() ) );
        }
    };

    class FutureErrorDomain final : public ErrorDomain 
    {
    public:
        using Errc          = FutureErrc;
        using Exception     = FutureException;

    public:
        const Char*     Name () const noexcept override                                                     { return "Future"; }
        const Char*     Message ( CodeType errorCode ) const noexcept override                              { return FutureErrMessage( static_cast< Errc > ( errorCode ) ); }
        void            ThrowAsException ( const ErrorCode &errorCode ) const noexcept( false ) override    { throw Exception( errorCode ); }

        constexpr FutureErrorDomain() noexcept
            : ErrorDomain( ErrorDomain::IdType{0x8000000000000013} )
        {
            ;
        }
   };

    static constexpr FutureErrorDomain g_coreFutureDomain;
    
    constexpr const ErrorDomain& GetFutureErrorDomain () noexcept
    {
        return g_coreFutureDomain;
    }

    constexpr ErrorCode MakeErrorCode ( FutureErrc code, ErrorDomain::SupportDataType data = ErrorDomain::SupportDataType() ) noexcept
    {
        return { static_cast< ErrorDomain::CodeType >( code ), GetFutureErrorDomain(), data };
    }
} // namespace core
} // namespace lap
#endif
