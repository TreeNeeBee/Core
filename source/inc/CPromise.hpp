/**
 * @file        CPromise.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       Promise implementation for AUTOSAR Adaptive Platform
 * @date        2025-10-27
 * @details     Provides Promise template class for asynchronous operations
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
#ifndef LAP_CORE_PROMISE_HPP
#define LAP_CORE_PROMISE_HPP

#include <future>
#include <type_traits>
#include "CTypedef.hpp"
#include "CErrorCode.hpp"
#include "CResult.hpp"
#include "CFuture.hpp"

namespace lap
{
namespace core
{
    template < typename T, typename E = ErrorCode >
    class Promise 
    {
    public:
        
        Promise () = default;
        Promise ( Promise &&other ) noexcept = default;
        Promise ( const Promise & ) = delete;
        ~Promise () noexcept = default;

        Promise& operator= ( Promise &&other ) noexcept
        {
            swap( ::std::move( other ) );

            return *this;
        }

        Promise& operator= ( const Promise & ) = delete;

        void swap ( Promise &other ) noexcept
        {
            ::std::swap( m_impPromise, other.m_impPromise );
        }

        Future<T, E> get_future ()
        {
            ::std::future< Result< T, E > > future;

            try {
                future = m_impPromise.get_future();
            } catch ( ::std::future_error const& e ) {
                m_impPromise = ::std::promise< Result< T, E > >();
                m_impPromise.set_value( Result< T, E >( ErrorCode( this->convertError( e ) ) ) );
                future = m_impPromise.get_future();
            }

            return Future< T, E >( ::std::move(future) );
        }

        template<typename U = T>
        ::std::enable_if_t< !( ::std::is_same< U, void >::value ) > set_value ( const T &value )
        {
            // set_value with T
            try {
                m_impPromise.set_value( Result< U, E >( value ) );
            }
            catch ( ::std::future_error const& e ) {
                m_impPromise = ::std::promise< Result< U, E > >();
                m_impPromise.set_value( Result< U, E >( ErrorCode( this->convertError( e ) ) ) );
            }
        }

        template<typename U = T>
        ::std::enable_if_t< !( ::std::is_same< U, void >::value ) >  set_value ( T &&value )
        {
            // set_value with T
            try {
                m_impPromise.set_value( Result< U, E >( ::std::forward< U >( value ) ) );
            }
            catch (std::future_error const& e) {
                m_impPromise = ::std::promise< Result< U, E > >();
                m_impPromise.set_value( Result< U, E >( ErrorCode( this->convertError( e ) ) ) );
            }
        }

        template<typename U = T>
        ::std::enable_if_t< ( ::std::is_same< U, void >::value ) > set_value ()
        {
            // set_value with void
            try {
                m_impPromise.set_value( Result< void, E >() );
            }
            catch (std::future_error const& e) {
                m_impPromise = ::std::promise< Result< void, E > >();
                m_impPromise.set_value( Result< void, E >( ErrorCode( this->convertError( e ) ) ) );
            }
        }

        void SetError ( E &&error)
        {
            try {
                m_impPromise.set_value( Result<T, E>( ::std::forward< E >( error ) ) );
            } catch ( ::std::future_error const& e ) {
                m_impPromise = ::std::promise< Result< T, E > >();
                m_impPromise.set_value( Result< T, E >( ErrorCode( this->convertError( e ) ) ) );
            }
        }

        void SetError ( const E &error )
        {
            try {
                m_impPromise.set_value( Result<T, E>( error ) );
            } catch ( ::std::future_error const& e ) {
                m_impPromise = ::std::promise< Result< T, E > >();
                m_impPromise.set_value( Result< T, E >( ErrorCode( this->convertError( e ) ) ) );
            }
        }

        void SetResult ( const Result< T, E > &result )
        {
            m_impPromise.set_value( result );
        }

        void SetResult ( Result< T, E > &&result )
        {
            m_impPromise.set_value(  ::std::forward< Result< T, E > >( result ) );
        }

    private:
        inline future_errc convertError( const ::std::future_error &error )
        {
            if ( error.code() == ::std::future_errc::promise_already_satisfied ) {
                return future_errc::promise_already_satisfied;
            } else if ( error.code() == ::std::future_errc::future_already_retrieved ) {
                return future_errc::future_already_retrieved;
            } else {
                return future_errc::no_state;
            }
        }

    private:
        ::std::promise< Result< T, E > >     m_impPromise;
    };
} // namespace core
} // namespace lap

#endif