/*
 * @Author: Daokuan.deng
 * @Date: 2023-07-16 00:42:09
 * @LastEditors: Daokuan.deng
 * @LastEditTime: 2023-08-30 00:36:39
 * @Description: 
 */
#ifndef LAP_CORE_INSTANCESPECIFIER_HPP
#define LAP_CORE_INSTANCESPECIFIER_HPP

#include <boost/regex.hpp>
#include "CTypedef.hpp"
#include "CResult.hpp"
#include "CPath.hpp"
#include "CCoreErrorDomain.hpp"

namespace lap 
{
namespace core 
{
    class InstanceSpecifier final
    {
    public:
    public:
        explicit InstanceSpecifier( StringView metaModelIdentifier )
        {
            if ( !IsValidMetaModelIdentifier( metaModelIdentifier ) ) {
                throw CoreException( ErrorCode( CoreErrc::kInvalidMetaModelPath ) );
            }

            m_metaModelIdentifier = metaModelIdentifier;
        }

        InstanceSpecifier( const InstanceSpecifier &other ) = default;
        InstanceSpecifier( InstanceSpecifier &&other ) noexcept = default;
        ~InstanceSpecifier() noexcept = default;

        static Result< InstanceSpecifier > Create ( StringView metaModelIdentifier )
        {
            if ( !IsValidMetaModelIdentifier( metaModelIdentifier ) ) {
                return Result<InstanceSpecifier>::FromError( CoreErrc::kInvalidMetaModelPath );
            } else {
                return Result<InstanceSpecifier>::FromValue( InstanceSpecifier{ metaModelIdentifier } );
            }
        }

        constexpr StringView ToString() const noexcept
        {
            return m_metaModelIdentifier;
        }

        InstanceSpecifier&                  operator=( const InstanceSpecifier &other ) noexcept = default;
        InstanceSpecifier&                  operator=( InstanceSpecifier &&other ) noexcept = default;
        Bool                                operator==( const InstanceSpecifier &other ) const noexcept        { return m_metaModelIdentifier == other.m_metaModelIdentifier; }
        Bool                                operator==( StringView other ) const noexcept                      { return m_metaModelIdentifier == other; }
        Bool                                operator!=( const InstanceSpecifier &other ) const noexcept        { return m_metaModelIdentifier != other.m_metaModelIdentifier; }
        Bool                                operator!=( StringView other ) const noexcept                      { return m_metaModelIdentifier != other; }
        Bool                                operator<( const InstanceSpecifier &other ) const noexcept         { return m_metaModelIdentifier < other.m_metaModelIdentifier; }

    private:
        // Validate identifier: accepts the following formats:
        // 1. Simple identifier: [A-Za-z0-9_]+ (e.g., "test")
        // 2. Relative path: [A-Za-z0-9_]+(/[A-Za-z0-9_]+)+ (e.g., "valid/meta_model")
        // 3. Absolute path: /[A-Za-z0-9_]+(/[A-Za-z0-9_]+)* (e.g., "/tmp/test_kvs")
        // Uses Boost.Regex for C++14 compatibility.
        static Bool IsValidMetaModelIdentifier( StringView id ) noexcept
        {
            if ( id.empty() ) {
                return false;
            }
            // Pattern breakdown:
            // ^                              - Start of string
            // (?:                            - Non-capturing group (one of three patterns):
            //   /[A-Za-z0-9_]+(?:/[A-Za-z0-9_]+)*  - Absolute path: starts with /, followed by segments
            //   |                            - OR
            //   [A-Za-z0-9_]+(?:/[A-Za-z0-9_]+)*   - Simple id or relative path: starts without /
            // )
            // $                              - End of string
            static const ::boost::regex re( "^(?:/[A-Za-z0-9_]+(?:/[A-Za-z0-9_]+)*|[A-Za-z0-9_]+(?:/[A-Za-z0-9_]+)*)$" );
            return ::boost::regex_match( id.begin(), id.end(), re );
        }
        friend constexpr Bool operator==( StringView lhs, const InstanceSpecifier &rhs ) noexcept;
        friend constexpr Bool operator!=( StringView lhs, const InstanceSpecifier &rhs ) noexcept;

        // stringified meta model identifier (short name path)
        // where path separator is ’/’. Lifetime of underlying
        // string has to exceed the lifetime of the constructed
        // InstanceSpecifier.

    private:
        StringView  m_metaModelIdentifier{""};
    };

    constexpr Bool operator==( StringView lhs, const InstanceSpecifier &rhs ) noexcept
    {
        return ( lhs == rhs.m_metaModelIdentifier );
    }

    constexpr Bool operator!=( StringView lhs, const InstanceSpecifier &rhs ) noexcept
    {
        return ( lhs != rhs.m_metaModelIdentifier );
    }
} // core
} // ara

#endif