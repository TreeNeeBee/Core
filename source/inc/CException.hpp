/**
 * @file        CException.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       Exception classes for AUTOSAR error handling
 * @date        2025-10-27
 * @details     Provides Exception class hierarchy for AUTOSAR Adaptive Platform
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
#ifndef LAP_CORE_EXCEPTION_HPP
#define LAP_CORE_EXCEPTION_HPP

#include <exception>
#include <new>
#include <memory>
#include <utility>
#include <type_traits>
#include "CErrorCode.hpp"

namespace lap
{
namespace core
{
    class Exception : public ::std::exception
    {
    public:

        explicit Exception ( ErrorCode err ) noexcept : m_errCode( err )    { ; }
        // Explicit copy constructor to ensure correct copy semantics and to
        // avoid the deprecated implicit copy when a user-defined operator=
        // is present. ErrorCode provides a copy constructor.
        Exception ( Exception const &other ) noexcept
            : m_errCode( other.m_errCode )
        {
            ;
        }

        const Char*         what () const noexcept override                 { return m_errCode.Message().data(); }
        const ErrorCode&    Error () const noexcept                         { return m_errCode; }

    protected:
        // Provide a protected copy-assignment operator that correctly copies
        // the underlying ErrorCode. Kept protected to preserve exception
        // semantics (users normally throw/catch by value/reference).
        // Operator= must be noexcept per API requirements. Ensure at compile
        // time that ErrorCode copy construction cannot throw so this is safe.
        Exception& operator= ( Exception const &other ) noexcept
        {
            if ( this != &other ) {
                static_assert(std::is_nothrow_copy_constructible<ErrorCode>::value,
                              "ErrorCode must be nothrow-copy-constructible for noexcept operator=");

                // Use destroy-then-reconstruct with explicit placement new syntax
                // to avoid calling overloaded operator new from IMP_OPERATOR_NEW.
                // We need ::new to use the global placement new, not ErrorCode's operator new.
                m_errCode.~ErrorCode();
                ::new (static_cast<void*>(std::addressof(m_errCode))) ErrorCode(other.m_errCode);
            }

            return *this;
        }
    
    private:
        ErrorCode           m_errCode;
    };
} // namespace core
} // namespace lap

#endif