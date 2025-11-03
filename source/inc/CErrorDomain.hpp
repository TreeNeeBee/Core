/**
 * @file        CErrorDomain.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       ErrorDomain base class for AUTOSAR error handling
 * @date        2025-10-27
 * @details     Provides ErrorDomain base class for error domain implementations
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
#ifndef LAP_CORE_ERRORDOMAIN_HPP
#define LAP_CORE_ERRORDOMAIN_HPP

#include "CTypedef.hpp"
#include "CMemory.hpp"

namespace lap
{
namespace core
{
    class ErrorCode;
    class ErrorDomain
    {
    public:
        using IdType            = UInt64;
        using CodeType          = Int32;
        using SupportDataType   = Int32;

    public:
        IMP_OPERATOR_NEW(ErrorDomain)

        constexpr IdType            Id () const noexcept                                                    { return m_id; }
        virtual const Char*         Name () const noexcept                                                  = 0;
        virtual const Char*         Message ( CodeType errorCode ) const noexcept                           = 0;
        virtual void                ThrowAsException ( const ErrorCode &errorCode ) const noexcept(false)   = 0;

        constexpr Bool              operator== ( const ErrorDomain &other ) const noexcept                  { return ( m_id == other.m_id ); }
        constexpr Bool              operator!= ( const ErrorDomain &other ) const noexcept                  { return ( m_id != other.m_id ); }

    protected:
        explicit constexpr ErrorDomain ( IdType id ) noexcept : m_id( id )                                  { ; }
        ErrorDomain ( const ErrorDomain & )  = delete;
        ErrorDomain ( ErrorDomain && ) = delete;
        ErrorDomain& operator= ( const ErrorDomain & ) = delete;
        ErrorDomain& operator= ( ErrorDomain && ) = delete;
        
        // Non-virtual destructor for constexpr static instances (AUTOSAR standard)
        ~ErrorDomain () noexcept = default;

    private:
        IdType                      m_id{0};
    }; // ErrorDomain
} // core
} // ara

#endif