/**
 * @file        CString.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       AUTOSAR Adaptive Platform String types and utilities
 * @date        2025-10-29
 * @details     Provides string types and utilities as per AUTOSAR AP SWS Core specification
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R22-11 SWS_CORE_01601 - String types and utilities
 * sdk:
 * platform:
 * project:     LightAP
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/10/29  <td>1.0      <td>ddkv587         <td>init version, AUTOSAR compliant
 * </table>
 */
#ifndef LAP_CORE_STRING_HPP
#define LAP_CORE_STRING_HPP

#include <string>
#include "CTypedef.hpp"

// StringView support
#if __cplusplus >= 201703L
    #include <string_view>
#else
    #include <boost/utility/string_view.hpp>
#endif

namespace lap
{
namespace core
{
    // ========================================================================
    // String Types (AUTOSAR SWS_CORE_01601 - 01614)
    // ========================================================================
    
    /**
     * @brief Standard narrow character string
     * According to AUTOSAR SWS_CORE_01601
     */
    using String = std::basic_string<char>;

    /**
     * @brief Wide character string type
     * According to AUTOSAR SWS_CORE_01612
     */
    using WString = std::basic_string<wchar_t>;

    /**
     * @brief UTF-16 encoded string type
     * According to AUTOSAR SWS_CORE_01613
     */
    using U16String = std::basic_string<char16_t>;

    /**
     * @brief UTF-32 encoded string type
     * According to AUTOSAR SWS_CORE_01614
     */
    using U32String = std::basic_string<char32_t>;

    // ========================================================================
    // StringView Types (AUTOSAR SWS_CORE_01631 - 01644, R24-11 Enhanced)
    // ========================================================================
    
    /**
     * @brief Non-owning view over a string
     * According to AUTOSAR SWS_CORE_01631 with R24-11 enhancements
     * 
     * @threadsafe All member functions are thread-safe (read-only view)
     * @note Enhanced with C++20 string_view features for R24-11 compliance
     */
#if __cplusplus >= 201703L
    using StringView = std::string_view;
#else
    using StringView = boost::string_view;
#endif

    /**
     * @brief Non-owning view over a wide string
     * @threadsafe All member functions are thread-safe (read-only view)
     */
#if __cplusplus >= 201703L
    using WStringView = std::wstring_view;
#else
    using WStringView = boost::wstring_view;
#endif

    /**
     * @brief Non-owning view over a UTF-16 string
     * @threadsafe All member functions are thread-safe (read-only view)
     */
#if __cplusplus >= 201703L
    using U16StringView = std::u16string_view;
#else
    using U16StringView = boost::basic_string_view<char16_t>;
#endif

    /**
     * @brief Non-owning view over a UTF-32 string
     * @threadsafe All member functions are thread-safe (read-only view)
     */
#if __cplusplus >= 201703L
    using U32StringView = std::u32string_view;
#else
    using U32StringView = boost::basic_string_view<char32_t>;
#endif
    
    // ========================================================================
    // StringView Extension Functions (R24-11 C++20 compatibility)
    // ========================================================================
    
#if __cplusplus < 202002L
    /**
     * @brief Check if StringView starts with given prefix
     * @param sv The StringView to check
     * @param prefix The prefix to search for
     * @return true if sv starts with prefix
     * @threadsafe Thread-safe (no shared state)
     * @note Provides C++20 starts_with functionality for C++17
     */
    inline bool starts_with(StringView sv, StringView prefix) noexcept
    {
        return sv.size() >= prefix.size() && 
               sv.compare(0, prefix.size(), prefix) == 0;
    }
    
    /**
     * @brief Check if StringView starts with given character
     * @param sv The StringView to check
     * @param ch The character to search for
     * @return true if sv starts with ch
     * @threadsafe Thread-safe (no shared state)
     */
    inline bool starts_with(StringView sv, char ch) noexcept
    {
        return !sv.empty() && sv.front() == ch;
    }
    
    /**
     * @brief Check if StringView starts with given C-string
     * @param sv The StringView to check
     * @param prefix The null-terminated prefix to search for
     * @return true if sv starts with prefix
     * @threadsafe Thread-safe (no shared state)
     */
    inline bool starts_with(StringView sv, const char* prefix) noexcept
    {
        return starts_with(sv, StringView(prefix));
    }
    
    /**
     * @brief Check if StringView ends with given suffix
     * @param sv The StringView to check
     * @param suffix The suffix to search for
     * @return true if sv ends with suffix
     * @threadsafe Thread-safe (no shared state)
     * @note Provides C++20 ends_with functionality for C++17
     */
    inline bool ends_with(StringView sv, StringView suffix) noexcept
    {
        return sv.size() >= suffix.size() && 
               sv.compare(sv.size() - suffix.size(), StringView::npos, suffix) == 0;
    }
    
    /**
     * @brief Check if StringView ends with given character
     * @param sv The StringView to check
     * @param ch The character to search for
     * @return true if sv ends with ch
     * @threadsafe Thread-safe (no shared state)
     */
    inline bool ends_with(StringView sv, char ch) noexcept
    {
        return !sv.empty() && sv.back() == ch;
    }
    
    /**
     * @brief Check if StringView ends with given C-string
     * @param sv The StringView to check
     * @param suffix The null-terminated suffix to search for
     * @return true if sv ends with suffix
     * @threadsafe Thread-safe (no shared state)
     */
    inline bool ends_with(StringView sv, const char* suffix) noexcept
    {
        return ends_with(sv, StringView(suffix));
    }
    
    /**
     * @brief Check if StringView contains given substring
     * @param sv The StringView to check
     * @param substr The substring to search for
     * @return true if sv contains substr
     * @threadsafe Thread-safe (no shared state)
     * @note Provides C++23 contains functionality
     */
    inline bool contains(StringView sv, StringView substr) noexcept
    {
        return sv.find(substr) != StringView::npos;
    }
    
    /**
     * @brief Check if StringView contains given character
     * @param sv The StringView to check
     * @param ch The character to search for
     * @return true if sv contains ch
     * @threadsafe Thread-safe (no shared state)
     */
    inline bool contains(StringView sv, char ch) noexcept
    {
        return sv.find(ch) != StringView::npos;
    }
    
    /**
     * @brief Check if StringView contains given C-string
     * @param sv The StringView to check
     * @param substr The null-terminated substring to search for
     * @return true if sv contains substr
     * @threadsafe Thread-safe (no shared state)
     */
    inline bool contains(StringView sv, const char* substr) noexcept
    {
        return contains(sv, StringView(substr));
    }
#endif // __cplusplus < 202002L

    // ========================================================================
    // String Literal Operators (AUTOSAR SWS_CORE_01621-01624)
    // ========================================================================
    
    /**
     * @brief User-defined literal for String
     * @param str String literal
     * @param len Length of string
     * @return String object
     */
    inline String operator""_s(const char* str, std::size_t len)
    {
        return String(str, len);
    }

#if __cplusplus >= 202002L
    /**
     * @brief User-defined literal for U8String (C++20 and later)
     * @param str String literal
     * @param len Length of string
     * @return U8String object
     */
    inline std::u8string operator""_u8s(const char8_t* str, std::size_t len)
    {
        return std::u8string(str, len);
    }
#endif

    /**
     * @brief User-defined literal for U16String
     * @param str String literal
     * @param len Length of string
     * @return U16String object
     */
    inline U16String operator""_u16s(const char16_t* str, std::size_t len)
    {
        return U16String(str, len);
    }

    /**
     * @brief User-defined literal for U32String
     * @param str String literal
     * @param len Length of string
     * @return U32String object
     */
    inline U32String operator""_u32s(const char32_t* str, std::size_t len)
    {
        return U32String(str, len);
    }

    /**
     * @brief User-defined literal for WString
     * @param str String literal
     * @param len Length of string
     * @return WString object
     */
    inline WString operator""_ws(const wchar_t* str, std::size_t len)
    {
        return WString(str, len);
    }

    // ========================================================================
    // String Conversion Utilities (AUTOSAR extensions)
    // ========================================================================
    
    /**
     * @brief Convert integer to String
     * @param value Integer value
     * @return String representation
     */
    inline String ToString(int value)
    {
        return std::to_string(value);
    }

    inline String ToString(long value)
    {
        return std::to_string(value);
    }

    inline String ToString(long long value)
    {
        return std::to_string(value);
    }

    inline String ToString(unsigned value)
    {
        return std::to_string(value);
    }

    inline String ToString(unsigned long value)
    {
        return std::to_string(value);
    }

    inline String ToString(unsigned long long value)
    {
        return std::to_string(value);
    }

    inline String ToString(float value)
    {
        return std::to_string(value);
    }

    inline String ToString(double value)
    {
        return std::to_string(value);
    }

    inline String ToString(long double value)
    {
        return std::to_string(value);
    }

} // namespace core
} // namespace lap

#endif // LAP_CORE_STRING_HPP
