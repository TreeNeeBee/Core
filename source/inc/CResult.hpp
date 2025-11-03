/**
 * @file        CResult.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       Result type for error handling in AUTOSAR Adaptive Platform
 * @date        2025-10-29
 * @details     Provides Result template class for value or error returns with functional combinators
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
 * <tr><td>2025/10/29  <td>2.0      <td>ddkv587         <td>add functional combinators (map, and_then, or_else, match)
 * </table>
 */
#ifndef LAP_CORE_RESULT_HPP
#define LAP_CORE_RESULT_HPP

#include "CTypedef.hpp"
#include "COptional.hpp"
#include "CErrorCode.hpp"
#include "CMemory.hpp"
#include <utility>  // for std::forward, std::move
#include <type_traits>

namespace lap
{
namespace core
{
    template < typename T, typename E = ErrorCode >
    class Result final
    {
    public:
        IMP_OPERATOR_NEW(Result)
        
        using value_type = T;
        using error_type = E;

    public:
        Result () = delete;
        explicit Result ( const T &t ) noexcept : m_value{ t }  { }
        explicit Result ( T &&t ) noexcept : m_value{ std::move( t ) }   { }
        explicit Result ( const E &e ) : m_error{ e }           { }
        explicit Result ( E &&e ) : m_error{ std::move( e ) }   { }

        Result ( const Result &other )
        {
            if ( other.HasValue() ) {
                m_value = other.m_value;
            }
            else {
                m_error = other.m_error;
            }
        }

        Result ( Result &&other ) noexcept( std::is_nothrow_move_constructible< T >::value && std::is_nothrow_move_constructible< E >::value )
            : m_value{ std::move( other.m_value ) }
            , m_error{ std::move( other.m_error ) }
        { }

        ~Result () noexcept = default;

        static Result                       FromValue ( const T &t ) noexcept   { return Result( t ); }
        static Result                       FromValue ( T &&t ) noexcept        { return Result( std::move( t ) ); }

        template < typename... Args >
        static Result                       FromValue ( Args &&... args )
        {
            return  Result( T{ std::forward< Args >( args )... } );
        }

        static Result                       FromError ( const E &e )            { return Result( e ); }
        static Result                       FromError ( E &&e )                 { return Result( std::move( e ) ); }

        template < typename... Args >
        static Result                       FromError ( Args &&... args )
        {
            return Result( E{ std::forward< Args >( args )... } );
        }

        Result& operator= ( const Result &other )
        {
            if ( other.HasValue() ) {
                m_value = other.m_value;
                m_error.reset();
            } else {
                m_error = other.m_error;
                m_value.reset();
            }

            return *this;
        }

        Result& operator= ( Result &&other ) noexcept( std::is_nothrow_move_assignable< T >::value && std::is_nothrow_move_assignable< E >::value )
        {
            if ( other.HasValue() ) {
                m_value = std::move( other.m_value );
                m_error.reset();
            } else {
                m_error = std::move( other.m_error );
                m_value.reset();
            }

            return *this;
        }

        template < typename... Args >
        void EmplaceValue ( Args &&... args )
        {
            m_value.emplace( std::forward< Args >( args )... );
            m_error.reset();
        }

        template <typename... Args>
        void EmplaceError (Args &&... args)
        {
            m_error.emplace( std::forward< Args >( args )... );
            m_value.reset();
        }

        void Swap ( Result &other ) noexcept( std::is_nothrow_move_constructible< T >::value && std::is_nothrow_move_assignable< T >::value && std::is_nothrow_move_constructible< E >::value && std::is_nothrow_move_assignable< E >::value )
        {
            std::swap( m_value, other.m_value );
            std::swap( m_error, other.m_error );
        }

        Bool                HasValue () const noexcept                  { return m_value.has_value(); }
        explicit            operator Bool () const noexcept             { return m_value.has_value(); }

        const T&            operator* () const &                        { return *m_value; }
        T&&                 operator* () &&                             { return std::move( *m_value ); }

        const T*            operator-> () const                         { return &*m_value; }

        const T&            Value () const &                            { return *m_value; }
        T&&                 Value () &&                                 { return std::move( *m_value ); }

        const E&            Error () const &                            { return *m_error; }
        E&&                 Error () &&                                 { return std::move( *m_error ); }

        Optional<T>         Ok () const &                               { return m_value; }
        Optional<T>         Ok () &&                                    { return std::move( m_value ); }
        Optional<E>         Err () const &                              { return m_error; }
        Optional<E>         Err () &&                                   { return std::move( m_error ); }

        template < typename U >
        T ValueOr ( U &&defaultValue ) const &
        {
            return m_value.value_or( std::forward< U >( defaultValue ) );
        }

        template < typename U >
        T ValueOr ( U &&defaultValue ) &&
        {
            if ( m_value.has_value() ) {
                return std::move( *m_value );
            } else {
                return std::forward< U >( defaultValue );
            }
        }

        template < typename G >
        E ErrorOr ( G &&defaultError ) const &
        {
            return m_error.value_or( std::forward< G >( defaultError ) );
        }

        template < typename G >
        E ErrorOr ( G &&defaultError ) &&
        {
            if ( m_error.has_value() ) {
                return std::move( *m_error );
            } else {
                return std::forward< G >( defaultError );
            }
        }

        template < typename G >
        Bool CheckError ( G &&error ) const
        {
            if ( HasValue() ) {
                return false;
            } else {
                return *m_error == std::forward< G >( error );
            }
        }

        const T& ValueOrThrow () const & noexcept( false )
        {
            if ( !HasValue() ) {
                const auto& e = Error();
                e.Domain().ThrowAsException( e );
            }

            return *m_value;
        }

        T ValueOrThrow () && noexcept( false )
        {
            if ( !HasValue() ) {
                auto&& e = Error();
                e.Domain().ThrowAsException( e );
            }

            return std::move( *m_value );
        }

        template <typename F>
        T Resolve ( F &&f ) const
        {
            return HasValue() ? Value() : f( Error() );
        }

        // ===== Functional Combinators (New in v2.0) =====
        
        /**
         * @brief Map function - transform the value if present
         * @tparam F Function type: T -> U
         * @param f Function to apply to the value
         * @return Result<U, E> with transformed value or original error
         * 
         * Example:
         *   Result<int, Error> r = Result<int, Error>::FromValue(42);
         *   auto r2 = r.Map([](int x) { return x * 2; });  // Result<int, Error> with value 84
         */
        template <typename F>
        auto Map ( F &&f ) const & -> Result<std::decay_t<decltype(f(std::declval<T>()))>, E>
        {
            using U = std::decay_t<decltype(f(std::declval<T>()))>;
            if ( HasValue() ) {
                return Result< U, E >::FromValue( f( Value() ) );
            } else {
                return Result< U, E >::FromError( Error() );
            }
        }

        template <typename F>
        auto Map ( F &&f ) && -> Result<std::decay_t<decltype(f(std::declval<T>()))>, E>
        {
            using U = std::decay_t<decltype(f(std::declval<T>()))>;
            if ( HasValue() ) {
                return Result< U, E >::FromValue( f( std::move( *m_value ) ) );
            } else {
                return Result< U, E >::FromError( std::move( *m_error ) );
            }
        }

        /**
         * @brief AndThen (flatMap/bind) - chain operations that return Result
         * @tparam F Function type: T -> Result<U, E>
         * @param f Function to apply to the value
         * @return Result<U, E> from function or original error
         * 
         * Example:
         *   Result<int, Error> parse(string s);
         *   Result<int, Error> validate(int x);
         *   auto r = parse("42").AndThen([](int x) { return validate(x); });
         */
        template <typename F>
        auto AndThen ( F &&f ) const & -> decltype(f(std::declval<T>()))
        {
            using ResultType = decltype(f(std::declval<T>()));
            if ( HasValue() ) {
                return f( Value() );
            } else {
                return ResultType::FromError( Error() );
            }
        }

        template <typename F>
        auto AndThen ( F &&f ) && -> decltype(f(std::declval<T>()))
        {
            using ResultType = decltype(f(std::declval<T>()));
            if ( HasValue() ) {
                return f( std::move( *m_value ) );
            } else {
                return ResultType::FromError( std::move( *m_error ) );
            }
        }

        /**
         * @brief OrElse - recover from error by providing alternative
         * @tparam F Function type: E -> Result<T, F>
         * @param f Function to handle error
         * @return Result<T, F> from function or original value
         * 
         * Example:
         *   auto r = operation()
         *       .OrElse([](Error e) { return loadDefault(); });
         */
        template <typename F>
        auto OrElse ( F &&f ) const & -> decltype(f(std::declval<E>()))
        {
            using ResultType = decltype(f(std::declval<E>()));
            if ( HasValue() ) {
                return ResultType::FromValue( Value() );
            } else {
                return f( Error() );
            }
        }

        template <typename F>
        auto OrElse ( F &&f ) && -> decltype(f(std::declval<E>()))
        {
            using ResultType = decltype(f(std::declval<E>()));
            if ( HasValue() ) {
                return ResultType::FromValue( std::move( *m_value ) );
            } else {
                return f( std::move( *m_error ) );
            }
        }

        /**
         * @brief Match - pattern matching on Result
         * @tparam OnValue Function type: T -> R
         * @tparam OnError Function type: E -> R
         * @param onValue Function to call if value present
         * @param onError Function to call if error present
         * @return R - common return type
         * 
         * Example:
         *   auto message = result.Match(
         *       [](int x) { return "Success: " + std::to_string(x); },
         *       [](Error e) { return "Error: " + e.message(); }
         *   );
         */
        template <typename OnValue, typename OnError>
        auto Match ( OnValue &&onValue, OnError &&onError ) const & 
            -> std::common_type_t<
                decltype(onValue(std::declval<T>())), 
                decltype(onError(std::declval<E>()))>
        {
            if ( HasValue() ) {
                return onValue( Value() );
            } else {
                return onError( Error() );
            }
        }

        template <typename OnValue, typename OnError>
        auto Match ( OnValue &&onValue, OnError &&onError ) && 
            -> std::common_type_t<
                decltype(onValue(std::declval<T>())), 
                decltype(onError(std::declval<E>()))>
        {
            if ( HasValue() ) {
                return onValue( std::move( *m_value ) );
            } else {
                return onError( std::move( *m_error ) );
            }
        }

        /**
         * @brief MapError - transform the error if present
         * @tparam F Function type: E -> F
         * @param f Function to apply to the error
         * @return Result<T, F> with original value or transformed error
         * 
         * Example:
         *   auto r2 = r.MapError([](Error e) { return DatabaseError(e); });
         */
        template <typename F>
        auto MapError ( F &&f ) const & -> Result<T, std::decay_t<decltype(f(std::declval<E>()))>>
        {
            using F_result = std::decay_t<decltype(f(std::declval<E>()))>;
            if ( HasValue() ) {
                return Result< T, F_result >::FromValue( Value() );
            } else {
                return Result< T, F_result >::FromError( f( Error() ) );
            }
        }

        template <typename F>
        auto MapError ( F &&f ) && -> Result<T, std::decay_t<decltype(f(std::declval<E>()))>>
        {
            using F_result = std::decay_t<decltype(f(std::declval<E>()))>;
            if ( HasValue() ) {
                return Result< T, F_result >::FromValue( std::move( *m_value ) );
            } else {
                return Result< T, F_result >::FromError( f( std::move( *m_error ) ) );
            }
        }

    private:
        Bool HasError() const noexcept       { return m_error.has_value(); }

    private:
        Optional< T >               m_value;
        Optional< E >               m_error;
    };


    template< typename E >
    class Result< void, E >
    {
    public:
        IMP_OPERATOR_NEW(Result)
        
        using value_type = void;
        using error_type = E;

        Result() noexcept : m_error{} { }
        explicit Result(E const& e) : m_error{ e }              { }
        explicit Result(E&& e) : m_error{ std::move( e ) }      { }
        Result( const Result &other ) : m_error{ other.m_error } { }
        Result( Result&& other ) noexcept( std::is_nothrow_move_constructible< E >::value )
            : m_error{ std::move( other.m_error ) } { }

        ~Result() noexcept = default;
        static Result                   FromValue() noexcept                        { return Result(); }
        static Result                   FromError(E const& e)                       { return Result(e); }
        static Result                   FromError(E&& e)                            { return Result( std::move( e ) ); }

        template< typename... Args >
        static Result FromError( Args&&... args )
        {
            return Result( E{ std::forward< Args >( args )... } );
        }

        Result& operator= ( const Result &other ) &
        {
            if ( other.HasError() ) {
                m_error = other.m_error;
            } else {
                m_error.reset();
            }

            return *this;
        }
        Result& operator= ( Result&& other ) & noexcept( std::is_nothrow_move_assignable< E >::value )
        {
            if ( other.HasError() ) {
                m_error = std::move( other.m_error );
            } else {
                m_error.reset();
            }

            return *this;
        }

        void EmplaceValue() noexcept
        {
            m_error.reset();
        }

        template<typename... Args>
        void EmplaceError(Args &&... args)
        {
            m_error.emplace( std::forward< Args >( args )... );
        }

        void Swap(Result& other) noexcept
        {
            std::swap( m_error, other.m_error );
        }

        Bool                            HasValue() const noexcept               { return !HasError(); }
        explicit                        operator Bool() const noexcept          { return HasValue(); }

        void                            Value() const                           { }
        const E&                        Error() const&                          { return *m_error; }
        E&&                             Error() &&                              { return std::move( *m_error ); }

        template <typename G>
        E                               ErrorOr (G &&defaultError) const &
        {
            return m_error.value_or( std::forward< G >( defaultError ) );
        }

        template <typename G>
        E                               ErrorOr (G &&defaultError) &&
        {
            if ( m_error.has_value() ) {
                E err = std::move( *m_error );
                return err;
            } else {
                return std::forward< G >( defaultError );
            }
        }

        template<typename G>
        Bool                            CheckError( G&& error ) const
        {
            if ( HasError() ) {
                return *m_error == std::forward< G >( error );
            }
            return false;
        }

        void                            ValueOrThrow() noexcept(false)
        {
            if ( HasError() ) {
                auto& e = Error();
                e.Domain().ThrowAsException(e);
            }
        }

    private:
        Bool                            HasError() const noexcept       { return m_error.has_value(); }

    private:
        Optional< E >                   m_error;
    };

    // ===== TRY Macro for Error Propagation =====
    
    /**
     * @brief TRY macro - early return on error (like Rust's ? operator)
     * 
     * Usage:
     *   Result<Config, Error> loadConfig() {
     *       auto data = TRY(readFile("config.json"));
     *       auto parsed = TRY(parseJson(data));
     *       return Result<Config, Error>::FromValue(parsed);
     *   }
     * 
     * The macro will:
     * 1. Evaluate the Result expression
     * 2. If error, return the error immediately
     * 3. If value, unwrap and continue with the value
     */
    // NOTE: Uses GNU statement-expression to enable expression-like TRY with early return.
    // Suppress pedantic/gnu warnings locally to preserve portability with -Werror builds.
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wgnu-statement-expression"
    #endif
    #if defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wpedantic"
    #endif
    #define LAP_TRY(expr)                                                      \
        __extension__ ({                                                       \
            auto __lap_result = (expr);                                        \
            if (!__lap_result.HasValue()) {                                    \
                return decltype(__lap_result)::FromError(                      \
                    std::move(__lap_result).Error());                          \
            }                                                                  \
            std::move(__lap_result).Value();                                   \
        })
    #if defined(__GNUC__)
    #pragma GCC diagnostic pop
    #endif
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #endif

    // Shorter alias
    #ifndef TRY
    #define TRY LAP_TRY
    #endif

} // core
} // lap

#endif
