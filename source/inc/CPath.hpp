/**
 * @file        CPath.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       Util of application path environment
 * @date        2025-10-29
 * @details     A helper class about application path, do not instantiate
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
 * <tr><td>2025/10/29  <td>2.0      <td>ddkv587         <td>migrate to std::filesystem (C++17)
 * </table>
 */

#ifndef LAP_CORE_PATH_HPP
#define LAP_CORE_PATH_HPP

#include <limits.h>
#include <unistd.h>
#include <regex>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fstream>
#include <cstring>
#include "CTypedef.hpp"
#include "CString.hpp"
#include "CMemory.hpp"

// Use std::filesystem (C++17) or fallback to boost::filesystem
#if __cplusplus >= 201703L && __has_include(<filesystem>)
    #include <filesystem>
    namespace fs = std::filesystem;
#else
    #include <boost/config.hpp>
    #include <boost/filesystem.hpp>
    #include <boost/system/error_code.hpp>
    namespace fs = boost::filesystem;
#endif

namespace lap
{
namespace core
{
    class Path final
    {
    public:
        IMP_OPERATOR_NEW(Path)
        
        static String getApplicationFolder( Bool bWithSlash = false ) noexcept
        {
            try {
                auto p = fs::current_path();
                String s = p.string();
                if ( bWithSlash && !s.empty() && s.back() != '/' ) s.push_back('/');
                return s;
            } catch ( ... ) {
                // Fallback to getcwd
                char buff[PATH_MAX] = {0};
                if ( ::getcwd( buff, sizeof(buff) ) != nullptr ) {
                    String s{ buff };
                    if ( bWithSlash && !s.empty() && s.back() != '/' ) s.push_back('/');
                    return s;
                }
                return String();
            }
        }

        static StringView getBaseName( StringView strPath ) noexcept
        {
            if ( !valid( strPath ) )   return strPath;
            return strPath.substr( strPath.find_last_of( "/\\" ) + 1 );
        }

        static StringView getFolder( StringView strPath ) noexcept
        {
            if ( !valid( strPath ) )   return "";
            return strPath.substr( 0, strPath.rfind( '/' ) );
        }

        static StringView append( StringView strBase, StringView extra ) noexcept
        {
            thread_local String s_buffer;
            s_buffer.clear();
            s_buffer.reserve( std::strlen( strBase.data() ) + 1 + std::strlen( extra.data() ) );
            s_buffer.append( strBase.data() );
            if ( !s_buffer.empty() && s_buffer.back() != '/' ) s_buffer.push_back('/');
            s_buffer.append( extra.data() );
            return StringView{ s_buffer };
        }

        static Bool createDirectory( StringView strPath ) noexcept
        {
            if ( !valid( strPath ) )   return false;
            try {
                fs::path p{ strPath.data() };
#if __cplusplus >= 201703L && __has_include(<filesystem>)
                std::error_code ec;
#else
                boost::system::error_code ec;
#endif
                if ( fs::exists(p, ec) ) return fs::is_directory(p, ec);
                return fs::create_directories(p, ec);
            } catch ( ... ) {
                return false;
            }
        }

        static Bool createFile( StringView strPath ) noexcept
        {
            if ( !valid( strPath ) )   return false;
            try {
                fs::path p{ strPath.data() };
#if __cplusplus >= 201703L && __has_include(<filesystem>)
                std::error_code ec;
#else
                boost::system::error_code ec;
#endif
                if ( fs::exists(p, ec) ) return fs::is_regular_file(p, ec);
                std::ofstream ofs( strPath.data() );
                return ofs.good();
            } catch ( ... ) {
                return false;
            }
        }

        static Bool isDirectory( StringView strPath ) noexcept
        {
            try {
                return fs::is_directory( fs::path( strPath.data() ) );
            } catch ( ... ) {
                return false;
            }
        }

        static Bool isFile( StringView strPath ) noexcept
        {
            try {
                return fs::is_regular_file( fs::path( strPath.data() ) );
            } catch ( ... ) {
                return false;
            }
        }

        static Bool exist( StringView strPath ) noexcept
        {
            try {
                return fs::exists( fs::path( strPath.data() ) );
            } catch ( ... ) {
                return false;
            }
        }

        static Bool valid( StringView strPath ) noexcept
        {
            return ( strPath.data() != nullptr ) && ( std::strlen( strPath.data() ) > 0 );
        }

        ~Path() noexcept = default;

    protected:
        Path() = delete;

    private:
    };
} // namespace core
} // namespace lap
#endif
