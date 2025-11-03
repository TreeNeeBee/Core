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
#include "CTypedef.hpp"
#include "CErrorCode.hpp"
#include "CResult.hpp"
#include "CFutureErrorDomain.hpp"
#include "CMemory.hpp"

namespace lap
{
namespace core
{
    enum class future_status : uint8_t 
    {
        ready   = 1,
        timeout = 2,
    };

    template < typename T, typename E = ErrorCode >
    class Future final
    {
    public:
        IMP_OPERATOR_NEW(Future)
        
#ifdef __EXCEPTIONS
        T get ()                      
        {
            if ( !m_impFuture.valid() ) {
                lap::core::GetFutureErrorDomain().ThrowAsException( lap::core::future_errc::no_state );
            }

            Result<T, E> result =
                Result< T, E >( ErrorCode( lap::core::future_errc::broken_promise ) );

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
                return Result< T, E >( ErrorCode( lap::core::future_errc::no_state ) );
            }

            try {
                return m_impFuture.get();
            }
            catch ( std::future_error const& ) {
                return Result< T, E >( ErrorCode( lap::core::future_errc::broken_promise ) );
            }
        }

        bool valid () const noexcept     
        {
            return m_impFuture.valid(); 
        }

        void wait () const noexcept
        {
            if ( !m_impFuture.valid() ) return;

            m_impFuture.wait(); 
        }

        template < typename Rep, typename Period >
        future_status wait_for ( const std::chrono::duration< Rep, Period > &timeoutDuration ) const noexcept
        {
            if ( !m_impFuture.valid() ) {
                return future_status::timeout;
            }
            auto status = m_impFuture.wait_for( timeoutDuration );

            if ( status == std::future_status::ready ) return future_status::ready;

            return future_status::timeout;
        }

        template < typename Clock, typename Duration >
        future_status wait_until ( const std::chrono::time_point< Clock, Duration > &deadline ) const noexcept
        {
            if ( !m_impFuture.valid() ) {
                return future_status::timeout;
            }
            auto status = m_impFuture.wait_until( deadline );

            if ( status == std::future_status::ready ) return future_status::ready;

            return future_status::timeout;
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
        // template < typename F >
        // auto then ( F &&func )
        // {
            // using U = std::result_of_t< std::decay_t< F >( Future< T, E > ) >;

            // auto promise = std::promise< U >();
            // auto future  = promise.get_future();

            // if ( !valid() ) {
            //     //promise.SetError( ErrorCode( lap::core::future_errc::no_state ) );

            //     return future;
            // }

            // std::thread( std::bind( [ = ]( std::promise< U >& promise ) {
            //     try {
            //         promise.set_value( func() ); // Note: Will not work with std::promise<void>. Needs some meta-template programming which is out of scope for this question.
            //     } catch ( ... ) {
            //         promise.set_exception( std::current_exception() );
            //     }
            // }, std::move( promise ) ) ).detach();

            // return std::move( future );
        // }

        // Register a callable that gets called when the Future becomes ready.
        // When func is called, it is guaranteed that get() and GetResult() will not block.
        // func is called in the context of the provided execution context executor.
        // The return type of depends on the return type of func (aka continuation).
        // Let U be the return type of the continuation (i.e. a type equivalent to std::result_
        // of<std::decay<F>::type(Future<T,E>)>::type). If U is Future<T2,E2> for some types T2, E2,
        // then the return type of then() is Future<T2,E2>. This is known as implicit Future unwrapping. If
        // U is Result<T2,E2> for some types T2, E2, then the return type of then() is Future<T2,E2>.
        // This is known as implicit Result unwrapping. Otherwise it is Future<U,E>.
        // template < typename F, typename ExecutorT >
        // auto then ( F &&func, ExecutorT &&executor ) -> std::future< decltype(func()) >
        // {
        //     //
        //     ;
        // }

        bool is_ready () const noexcept
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
        // /// Evaluate ready handler and store result in shared state.
        // template<typename N, typename F>
        // void evaluate_ready_handler(Future&& f, Promise<N, E> p, F fn)
        // {
        //     p.set_value(fn(std::move(f)));
        // }

        // /// Explicit specialization for evaluate_ready_handler<void, F>.
        // template<typename F>
        // void evaluate_ready_handler(Future&& f, Promise<void, E> p, F fn)
        // {
        //     fn(std::move(f));
        //     p.set_value();
        // }

    private:
        ::std::future< Result< T, E > >     m_impFuture;
    };
} // namespace core
} // namespace lap
#endif
