/**
 * @file        CVariant.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       AUTOSAR Adaptive Platform Variant class
 * @date        2025-10-29
 * @details     Provides ara::core::Variant as per AUTOSAR AP SWS Core specification
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R22-11 SWS_CORE_01801
 * sdk:
 * platform:
 * project:     LightAP
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/10/29  <td>1.0      <td>ddkv587         <td>init version, AUTOSAR compliant
 * </table>
 */
#ifndef LAP_CORE_VARIANT_HPP
#define LAP_CORE_VARIANT_HPP

#include "CTypedef.hpp"
#include "CUtility.hpp"

// C++17 has std::variant
#if __cplusplus >= 201703L
    #include <variant>
#else
    #include <boost/variant.hpp>
#endif

#include <stdexcept>
#include <type_traits>

namespace lap
{
namespace core
{
    // ========================================================================
    // Variant Type Definition (AUTOSAR SWS_CORE_01801)
    // ========================================================================
    
    /**
     * @brief A type-safe discriminated union.
     * @tparam Types Alternative types
     * 
     * According to AUTOSAR SWS_CORE_01801, this is a type alias to std::variant
     * (C++17) or boost::variant (C++14).
     */
#if __cplusplus >= 201703L
    template <typename... Types>
    using Variant = std::variant<Types...>;
    
    // variant_size and variant_alternative (AUTOSAR SWS_CORE_01811-01812)
    template <typename T>
    struct variant_size : std::variant_size<T> {};
    
    template <typename T>
    inline constexpr std::size_t variant_size_v = std::variant_size_v<T>;
    
    template <std::size_t I, typename T>
    struct variant_alternative : std::variant_alternative<I, T> {};
    
    template <std::size_t I, typename T>
    using variant_alternative_t = typename std::variant_alternative<I, T>::type;
    
    // variant_npos constant (AUTOSAR SWS_CORE_01813)
    inline constexpr std::size_t variant_npos = std::variant_npos;
    
    // monostate (AUTOSAR SWS_CORE_01814)
    using monostate = std::monostate;
    
    // bad_variant_access exception (AUTOSAR SWS_CORE_01815)
    using bad_variant_access = std::bad_variant_access;
    
    // Visitor functions (AUTOSAR SWS_CORE_01821-01824)
    using std::visit;
    using std::get;
    using std::get_if;
    using std::holds_alternative;
    
#else
    template <typename... Types>
    using Variant = boost::variant<Types...>;
    
    // monostate for boost::variant
    struct monostate {};
    
    // bad_variant_access exception for boost
    class bad_variant_access : public std::exception
    {
    public:
        const char* what() const noexcept override
        {
            return "bad variant access";
        }
    };
    
    // variant_npos for boost::variant
    inline constexpr std::size_t variant_npos = static_cast<std::size_t>(-1);
    
    // get function for boost::variant
    template <std::size_t I, typename... Types>
    auto get(Variant<Types...>& v) -> typename boost::mpl::at_c<typename Variant<Types...>::types, I>::type&
    {
        using TargetType = typename boost::mpl::at_c<typename Variant<Types...>::types, I>::type;
        return boost::get<TargetType>(v);
    }
    
    template <std::size_t I, typename... Types>
    auto get(const Variant<Types...>& v) -> const typename boost::mpl::at_c<typename Variant<Types...>::types, I>::type&
    {
        using TargetType = typename boost::mpl::at_c<typename Variant<Types...>::types, I>::type;
        return boost::get<TargetType>(v);
    }
    
    template <typename T, typename... Types>
    T& get(Variant<Types...>& v)
    {
        return boost::get<T>(v);
    }
    
    template <typename T, typename... Types>
    const T& get(const Variant<Types...>& v)
    {
        return boost::get<T>(v);
    }
    
    // get_if function for boost::variant
    template <std::size_t I, typename... Types>
    auto get_if(Variant<Types...>* pv) noexcept -> typename boost::mpl::at_c<typename Variant<Types...>::types, I>::type*
    {
        using TargetType = typename boost::mpl::at_c<typename Variant<Types...>::types, I>::type;
        return boost::get<TargetType>(pv);
    }
    
    template <std::size_t I, typename... Types>
    auto get_if(const Variant<Types...>* pv) noexcept -> const typename boost::mpl::at_c<typename Variant<Types...>::types, I>::type*
    {
        using TargetType = typename boost::mpl::at_c<typename Variant<Types...>::types, I>::type;
        return boost::get<TargetType>(pv);
    }
    
    template <typename T, typename... Types>
    T* get_if(Variant<Types...>* pv) noexcept
    {
        return boost::get<T>(pv);
    }
    
    template <typename T, typename... Types>
    const T* get_if(const Variant<Types...>* pv) noexcept
    {
        return boost::get<T>(pv);
    }
    
    // visit function for boost::variant
    template <typename Visitor, typename... Variants>
    auto visit(Visitor&& vis, Variants&&... vars)
        -> decltype(boost::apply_visitor(std::forward<Visitor>(vis), std::forward<Variants>(vars)...))
    {
        return boost::apply_visitor(std::forward<Visitor>(vis), std::forward<Variants>(vars)...);
    }
    
    // holds_alternative function for boost::variant
    template <typename T, typename... Types>
    bool holds_alternative(const Variant<Types...>& v) noexcept
    {
        return v.which() == boost::mpl::find<typename Variant<Types...>::types, T>::type::pos::value;
    }
#endif

    // Helper function to get variant index (AUTOSAR extension)
    /**
     * @brief Get the zero-based index of the alternative held by the variant
     * @tparam Types Alternative types
     * @param v Variant object
     * @return Index of current alternative, or variant_npos if valueless
     */
    template <typename... Types>
    constexpr std::size_t GetVariantIndex(const Variant<Types...>& v) noexcept
    {
#if __cplusplus >= 201703L
        return v.index();
#else
        return static_cast<std::size_t>(v.which());
#endif
    }

    // Helper to check if variant holds a value (AUTOSAR extension)
    /**
     * @brief Check if the variant holds a valid value
     * @tparam Types Alternative types
     * @param v Variant object
     * @return true if variant holds a value, false if valueless
     */
    template <typename... Types>
    constexpr bool HasVariantValue(const Variant<Types...>& v) noexcept
    {
#if __cplusplus >= 201703L
        return !v.valueless_by_exception();
#else
        return v.which() != boost::detail::variant::void_index;
#endif
    }

} // namespace core
} // namespace lap

#endif // LAP_CORE_VARIANT_HPP
