/**
 * @file        CErrorCode.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       ErrorCode class for AUTOSAR error handling
 * @date        2025-10-27
 * @details     Provides ErrorCode class for error handling in AUTOSAR Adaptive Platform
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
#ifndef LAP_CORE_ERRORCODE_HPP
#define LAP_CORE_ERRORCODE_HPP

#include "CTypedef.hpp"
#include "CString.hpp"
#include "CErrorDomain.hpp"

namespace lap
{
namespace core
{
    class ErrorCode final
    {
    public:
        template < typename EnumT >
        constexpr ErrorCode ( EnumT e, ErrorDomain::SupportDataType data = ErrorDomain::SupportDataType() ) noexcept
            : ErrorCode( MakeErrorCode( e, data ) )
        {
            ;
        }

        constexpr ErrorCode ( ErrorDomain::CodeType value, const ErrorDomain& domain, ErrorDomain::SupportDataType data = ErrorDomain::SupportDataType() ) noexcept
            : m_errCode( value )
            , m_errDomain( domain )
            , m_errData( data )
        {
        }

        constexpr ErrorDomain::CodeType             Value () const noexcept             { return m_errCode; }
        const ErrorDomain&                          Domain () const noexcept            { return m_errDomain; }
        constexpr ErrorDomain::SupportDataType      SupportData () const noexcept       { return m_errData; }
        StringView                                  Message () const noexcept           { return Domain().Message( Value() ); }

        void                                        ThrowAsException () const           { Domain().ThrowAsException( *this ); }

    private:
        friend constexpr Bool operator== ( const ErrorCode &lhs, const ErrorCode &rhs ) noexcept;
        friend constexpr Bool operator!= ( const ErrorCode &lhs, const ErrorCode &rhs ) noexcept;

    private:
        ErrorDomain::CodeType                   m_errCode;
        const ErrorDomain&                      m_errDomain;
        ErrorDomain::SupportDataType            m_errData;
    }; // class ErrorCode

    constexpr Bool operator== ( const ErrorCode &lhs, const ErrorCode &rhs ) noexcept
    {
        return ( lhs.m_errCode == rhs.m_errCode ) && ( &lhs.m_errDomain == &rhs.m_errDomain || lhs.m_errDomain == rhs.m_errDomain );
    }

    constexpr Bool operator!= (const ErrorCode &lhs, const ErrorCode &rhs) noexcept
    {
        return !( lhs == rhs );
    }

    // constexpr Bool operator== ( const ErrorCode &lhs, const ErrorCode &rhs ) noexcept;
    // constexpr Bool operator!= (const ErrorCode &lhs, const ErrorCode &rhs) noexcept;
} // core
} // ara

#endif