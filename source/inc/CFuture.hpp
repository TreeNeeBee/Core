/**
 * @file        CFuture.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       Future implementation for AUTOSAR Adaptive Platform
 * @date        2025-10-27
 * @details     Provides Future template class for asynchronous operations
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
#ifndef LAP_CORE_FUTURE_HPP
#define LAP_CORE_FUTURE_HPP

#include <future>
#include <thread>
#include <type_traits>
#include "CTypedef.hpp"
#include "CErrorCode.hpp"
#include "CResult.hpp"
#include "CFutureErrorDomain.hpp"

namespace lap
{
namespace core
{
    enum class FutureStatus : UInt8 
    {
        kReady   = 1,
        kTimeout = 2,
    };

    // Forward declarations for type traits
    template < typename T, typename E > class Future;
    template < typename T, typename E > class Promise;

    namespace detail
    {
        // Type trait: is_future<U> detects Future<T2, E2>
        template < typename U >
        struct is_future : std::false_type {};

        template < typename T2, typename E2 >
        struct is_future< Future< T2, E2 > > : std::true_type {};

        // Type trait: is_result<U> detects Result<T2, E2>
        template < typename U >
        struct is_result : std::false_type {};

        template < typename T2, typename E2 >
        struct is_result< Result< T2, E2 > > : std::true_type {};

        // ---- then() return type deduction ----
        // Case 1: U is Future<T2, E2> → return Future<T2, E2>  (implicit Future unwrapping)
        // Case 2: U is Result<T2, E2> → return Future<T2, E2>  (implicit Result unwrapping)
        // Case 3: U is anything else  → return Future<U, E>

        // Primary template: Case 3 — plain U → Future<U, E>
        template < typename U, typename E, typename = void >
        struct then_return_type
        {
            using type = Future< U, E >;
        };

        // Specialization: Case 1 — Future<T2, E2> → Future<T2, E2>
        template < typename U, typename E >
        struct then_return_type< U, E, std::enable_if_t< is_future< U >::value > >
        {
            using type = U;
        };

        // Specialization: Case 2 — Result<T2, E2> → Future<value_type, error_type>
        template < typename U, typename E >
        struct then_return_type< U, E, std::enable_if_t< is_result< U >::value && !is_future< U >::value > >
        {
            using type = Future< typename U::value_type, typename U::error_type >;
        };

        template < typename U, typename E >
        using then_return_type_t = typename then_return_type< U, E >::type;

        // ---- Fulfillment helpers ----

        // fulfill: set promise value from continuation result
        // Case 3: plain value U
        template < typename N, typename E2, typename U >
        std::enable_if_t< !is_future< U >::value && !is_result< U >::value >
        fulfill( Promise< N, E2 >& p, U&& value )
        {
            p.SetValue( std::forward< U >( value ) );
        }

        // Case 2: Result<T2, E2> → unwrap
        template < typename N, typename E2, typename U >
        std::enable_if_t< is_result< std::decay_t< U > >::value >
        fulfill( Promise< N, E2 >& p, U&& result )
        {
            if ( result.HasValue() )
            {
                p.SetValue( std::move( result ).Value() );
            }
            else
            {
                p.SetError( std::move( result ).Error() );
            }
        }

        // Case 2 (void Result): Result<void, E2> → unwrap
        template < typename E2, typename U >
        std::enable_if_t< is_result< std::decay_t< U > >::value
                          && std::is_void< typename std::decay_t< U >::value_type >::value >
        fulfill( Promise< void, E2 >& p, U&& result )
        {
            if ( result.HasValue() )
            {
                p.SetValue();
            }
            else
            {
                p.SetError( std::move( result ).Error() );
            }
        }

        // Case 1: Future<T2, E2> → unwrap inner future
        template < typename N, typename E2, typename U >
        std::enable_if_t< is_future< std::decay_t< U > >::value >
        fulfill( Promise< N, E2 >& p, U&& innerFuture )
        {
            auto innerResult = innerFuture.GetResult();
            if ( innerResult.HasValue() )
            {
                p.SetValue( std::move( innerResult ).Value() );
            }
            else
            {
                p.SetError( std::move( innerResult ).Error() );
            }
        }

    } // namespace detail

    template < typename T, typename E = ErrorCode >
    class Future final
    {
    public:
        using value_type = T;
        using error_type = E;
        
#ifdef __EXCEPTIONS
        T get ()                      
        {
            if ( !m_impFuture.valid() ) {
                lap::core::GetFutureErrorDomain().ThrowAsException( lap::core::FutureErrc::kNoState );
            }

            Result<T, E> result =
                Result< T, E >( ErrorCode( lap::core::FutureErrc::kBrokenPromise ) );

            try {
                result = m_impFuture.get();
            } catch ( std::future_error const& ) {
                // no need to do
                ;
            }

            if ( !result.HasValue() ) {
                lap::core::GetFutureErrorDomain().ThrowAsException( result.Error() );
            }

            return result.Value();
        }
#endif

        Result<T, E> GetResult () noexcept
        {
            if ( !m_impFuture.valid() ) {
                return Result< T, E >( ErrorCode( lap::core::FutureErrc::kNoState ) );
            }

            try {
                return m_impFuture.get();
            }
            catch ( std::future_error const& ) {
                return Result< T, E >( ErrorCode( lap::core::FutureErrc::kBrokenPromise ) );
            }
        }

        Bool Valid () const noexcept     
        {
            return m_impFuture.valid(); 
        }

        void Wait () const noexcept
        {
            if ( !m_impFuture.valid() ) return;

            m_impFuture.wait(); 
        }

        template < typename Rep, typename Period >
        FutureStatus WaitFor ( const std::chrono::duration< Rep, Period > &timeoutDuration ) const noexcept
        {
            if ( !m_impFuture.valid() ) {
                return FutureStatus::kTimeout;
            }
            auto status = m_impFuture.wait_for( timeoutDuration );

            if ( status == std::future_status::ready ) return FutureStatus::kReady;

            return FutureStatus::kTimeout;
        }

        template < typename Clock, typename Duration >
        FutureStatus WaitUntil ( const std::chrono::time_point< Clock, Duration > &deadline ) const noexcept
        {
            if ( !m_impFuture.valid() ) {
                return FutureStatus::kTimeout;
            }
            auto status = m_impFuture.wait_until( deadline );

            if ( status == std::future_status::ready ) return FutureStatus::kReady;

            return FutureStatus::kTimeout;
        }

        // Register a callable that gets called when the Future becomes ready.
        // When func is called, it is guaranteed that get() and GetResult() will not block.
        // func may be called in the context of this call or in the context of Promise::set_value() or
        // Promise::SetError() or somewhere else.
        // The return type of then depends on the return type of func (aka continuation).
        // Let U be the return type of the continuation (i.e. a type equivalent to std::result_
        // of<std::decay<F>::type(Future<T,E>)>::type). If U is Future<T2,E2> for some types T2, E2,
        // then the return type of then() is Future<T2,E2>. This is known as implicit Future unwrapping. If
        // U is Result<T2,E2> for some types T2, E2, then the return type of then() is Future<T2,E2>.
        // This is known as implicit Result unwrapping. Otherwise it is Future<U,E>.
        template < typename F >
        auto Then ( F &&func ) -> detail::then_return_type_t< std::result_of_t< std::decay_t< F >( Future< T, E > ) >, E >
        {
            using U = std::result_of_t< std::decay_t< F >( Future< T, E > ) >;
            using ReturnFuture = detail::then_return_type_t< U, E >;
            using N = typename ReturnFuture::value_type;      // new value type
            using E2 = typename ReturnFuture::error_type;     // new error type

            auto promise = std::make_shared< Promise< N, E2 > >();
            auto future  = promise->GetFuture();

            if ( !m_impFuture.valid() )
            {
                promise->SetError( E2( lap::core::FutureErrc::kNoState ) );
                return future;
            }

            // If already ready, run continuation inline (no thread spawn)
            if ( IsReady() )
            {
                try
                {
                    auto result = func( std::move( *this ) );
                    detail::fulfill( *promise, std::move( result ) );
                }
                catch ( ... )
                {
                    promise->SetError( E2( lap::core::FutureErrc::kBrokenPromise ) );
                }
                return future;
            }

            // Not ready — detach a thread to wait and invoke continuation
            auto sharedFuture = std::make_shared< std::future< Result< T, E > > >(
                std::move( m_impFuture ) );
            std::thread( [ sharedFuture, promise, fn = std::forward< F >( func ) ]() mutable
            {
                try
                {
                    sharedFuture->wait();
                    // Reconstruct a Future from the shared_future
                    Future< T, E > readyFuture( std::move( *sharedFuture ) );
                    auto result = fn( std::move( readyFuture ) );
                    detail::fulfill( *promise, std::move( result ) );
                }
                catch ( ... )
                {
                    promise->SetError( E2( lap::core::FutureErrc::kBrokenPromise ) );
                }
            } ).detach();

            return future;
        }

        Bool IsReady () const noexcept
        {
            if ( !m_impFuture.valid() ) return false;

            return std::future_status::ready ==
               m_impFuture.wait_for( std::chrono::seconds::zero() );
        }

        Future () noexcept = default;
        explicit Future ( ::std::future< Result< T, E > > &&impFuture ) noexcept
            : m_impFuture( ::std::move( impFuture ) )
        {
            ;
        }
        Future ( const Future & ) = delete;
        Future ( Future &&other ) noexcept = default;
        ~Future () noexcept = default;

        Future& operator= ( const Future & ) = delete;
        Future& operator= ( Future &&other ) noexcept = default;

    private:
        ::std::future< Result< T, E > >     m_impFuture;
    };
} // namespace core
} // namespace lap
#endif
