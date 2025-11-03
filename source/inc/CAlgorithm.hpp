/**
 * @file        CAlgorithm.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       Algorithm utilities for AUTOSAR Adaptive Platform
 * @date        2025-10-29
 * @details     Provides AUTOSAR-compliant algorithm utilities and transformations
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R22-11 SWS_CORE_18xxx - Algorithm
 * sdk:
 * platform:
 * project:     LightAP
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/10/29  <td>1.0      <td>ddkv587         <td>init version, AUTOSAR compliant
 * </table>
 */
#ifndef LAP_CORE_ALGORITHM_HPP
#define LAP_CORE_ALGORITHM_HPP

#include "CTypedef.hpp"
#include <algorithm>
#include <iterator>
#include <functional>

namespace lap
{
namespace core
{
    // ========================================================================
    // Non-modifying sequence operations (AUTOSAR SWS_CORE_18100 - 18199)
    // ========================================================================

    /**
     * @brief Find first element satisfying a predicate
     * @tparam InputIt Input iterator type
     * @tparam UnaryPredicate Predicate function type
     * @param first Beginning of the range
     * @param last End of the range
     * @param pred Unary predicate
     * @return Iterator to the first element satisfying pred, or last if not found
     * According to AUTOSAR SWS_CORE_18101
     */
    template <typename InputIt, typename UnaryPredicate>
    constexpr InputIt FindIf(InputIt first, InputIt last, UnaryPredicate pred)
    {
        return std::find_if(first, last, pred);
    }

    /**
     * @brief Find first element not satisfying a predicate
     * @tparam InputIt Input iterator type
     * @tparam UnaryPredicate Predicate function type
     * @param first Beginning of the range
     * @param last End of the range
     * @param pred Unary predicate
     * @return Iterator to the first element not satisfying pred, or last if all satisfy
     * According to AUTOSAR SWS_CORE_18102
     */
    template <typename InputIt, typename UnaryPredicate>
    constexpr InputIt FindIfNot(InputIt first, InputIt last, UnaryPredicate pred)
    {
        return std::find_if_not(first, last, pred);
    }

    /**
     * @brief Check if all elements satisfy a predicate
     * @tparam InputIt Input iterator type
     * @tparam UnaryPredicate Predicate function type
     * @param first Beginning of the range
     * @param last End of the range
     * @param pred Unary predicate
     * @return true if all elements satisfy pred, false otherwise
     * According to AUTOSAR SWS_CORE_18110
     */
    template <typename InputIt, typename UnaryPredicate>
    constexpr Bool AllOf(InputIt first, InputIt last, UnaryPredicate pred)
    {
        return std::all_of(first, last, pred);
    }

    /**
     * @brief Check if any element satisfies a predicate
     * @tparam InputIt Input iterator type
     * @tparam UnaryPredicate Predicate function type
     * @param first Beginning of the range
     * @param last End of the range
     * @param pred Unary predicate
     * @return true if any element satisfies pred, false otherwise
     * According to AUTOSAR SWS_CORE_18111
     */
    template <typename InputIt, typename UnaryPredicate>
    constexpr Bool AnyOf(InputIt first, InputIt last, UnaryPredicate pred)
    {
        return std::any_of(first, last, pred);
    }

    /**
     * @brief Check if no element satisfies a predicate
     * @tparam InputIt Input iterator type
     * @tparam UnaryPredicate Predicate function type
     * @param first Beginning of the range
     * @param last End of the range
     * @param pred Unary predicate
     * @return true if no element satisfies pred, false otherwise
     * According to AUTOSAR SWS_CORE_18112
     */
    template <typename InputIt, typename UnaryPredicate>
    constexpr Bool NoneOf(InputIt first, InputIt last, UnaryPredicate pred)
    {
        return std::none_of(first, last, pred);
    }

    /**
     * @brief Count elements satisfying a predicate
     * @tparam InputIt Input iterator type
     * @tparam UnaryPredicate Predicate function type
     * @param first Beginning of the range
     * @param last End of the range
     * @param pred Unary predicate
     * @return Number of elements satisfying pred
     * According to AUTOSAR SWS_CORE_18120
     */
    template <typename InputIt, typename UnaryPredicate>
    constexpr typename std::iterator_traits<InputIt>::difference_type
    CountIf(InputIt first, InputIt last, UnaryPredicate pred)
    {
        return std::count_if(first, last, pred);
    }

    // ========================================================================
    // Modifying sequence operations (AUTOSAR SWS_CORE_18200 - 18299)
    // ========================================================================

    /**
     * @brief Copy elements from source to destination
     * @tparam InputIt Input iterator type
     * @tparam OutputIt Output iterator type
     * @param first Beginning of source range
     * @param last End of source range
     * @param dest Beginning of destination range
     * @return Iterator to the end of destination range
     * According to AUTOSAR SWS_CORE_18201
     */
    template <typename InputIt, typename OutputIt>
    constexpr OutputIt Copy(InputIt first, InputIt last, OutputIt dest)
    {
        return std::copy(first, last, dest);
    }

    /**
     * @brief Copy elements satisfying a predicate
     * @tparam InputIt Input iterator type
     * @tparam OutputIt Output iterator type
     * @tparam UnaryPredicate Predicate function type
     * @param first Beginning of source range
     * @param last End of source range
     * @param dest Beginning of destination range
     * @param pred Unary predicate
     * @return Iterator to the end of destination range
     * According to AUTOSAR SWS_CORE_18202
     */
    template <typename InputIt, typename OutputIt, typename UnaryPredicate>
    constexpr OutputIt CopyIf(InputIt first, InputIt last, OutputIt dest, UnaryPredicate pred)
    {
        return std::copy_if(first, last, dest, pred);
    }

    /**
     * @brief Fill range with value
     * @tparam ForwardIt Forward iterator type
     * @tparam T Value type
     * @param first Beginning of the range
     * @param last End of the range
     * @param value Value to fill with
     * According to AUTOSAR SWS_CORE_18210
     */
    template <typename ForwardIt, typename T>
    constexpr void Fill(ForwardIt first, ForwardIt last, const T& value)
    {
        std::fill(first, last, value);
    }

    /**
     * @brief Transform elements using a function
     * @tparam InputIt Input iterator type
     * @tparam OutputIt Output iterator type
     * @tparam UnaryOperation Transformation function type
     * @param first Beginning of source range
     * @param last End of source range
     * @param dest Beginning of destination range
     * @param op Unary transformation function
     * @return Iterator to the end of destination range
     * According to AUTOSAR SWS_CORE_18220
     */
    template <typename InputIt, typename OutputIt, typename UnaryOperation>
    constexpr OutputIt Transform(InputIt first, InputIt last, OutputIt dest, UnaryOperation op)
    {
        return std::transform(first, last, dest, op);
    }

    /**
     * @brief Remove consecutive duplicates
     * @tparam ForwardIt Forward iterator type
     * @param first Beginning of the range
     * @param last End of the range
     * @return Iterator to the new logical end of the range
     * According to AUTOSAR SWS_CORE_18230
     */
    template <typename ForwardIt>
    constexpr ForwardIt Unique(ForwardIt first, ForwardIt last)
    {
        return std::unique(first, last);
    }

    /**
     * @brief Remove consecutive duplicates using a predicate
     * @tparam ForwardIt Forward iterator type
     * @tparam BinaryPredicate Binary predicate function type
     * @param first Beginning of the range
     * @param last End of the range
     * @param pred Binary predicate for equality comparison
     * @return Iterator to the new logical end of the range
     * According to AUTOSAR SWS_CORE_18231
     */
    template <typename ForwardIt, typename BinaryPredicate>
    constexpr ForwardIt Unique(ForwardIt first, ForwardIt last, BinaryPredicate pred)
    {
        return std::unique(first, last, pred);
    }

    // ========================================================================
    // Sorting operations (AUTOSAR SWS_CORE_18300 - 18399)
    // ========================================================================

    /**
     * @brief Sort elements in ascending order
     * @tparam RandomIt Random access iterator type
     * @param first Beginning of the range
     * @param last End of the range
     * According to AUTOSAR SWS_CORE_18301
     */
    template <typename RandomIt>
    constexpr void Sort(RandomIt first, RandomIt last)
    {
        std::sort(first, last);
    }

    /**
     * @brief Sort elements using a comparison function
     * @tparam RandomIt Random access iterator type
     * @tparam Compare Comparison function type
     * @param first Beginning of the range
     * @param last End of the range
     * @param comp Comparison function
     * According to AUTOSAR SWS_CORE_18302
     */
    template <typename RandomIt, typename Compare>
    constexpr void Sort(RandomIt first, RandomIt last, Compare comp)
    {
        std::sort(first, last, comp);
    }

    /**
     * @brief Check if range is sorted
     * @tparam ForwardIt Forward iterator type
     * @param first Beginning of the range
     * @param last End of the range
     * @return true if sorted, false otherwise
     * According to AUTOSAR SWS_CORE_18310
     */
    template <typename ForwardIt>
    constexpr Bool IsSorted(ForwardIt first, ForwardIt last)
    {
        return std::is_sorted(first, last);
    }

    /**
     * @brief Check if range is sorted using a comparison function
     * @tparam ForwardIt Forward iterator type
     * @tparam Compare Comparison function type
     * @param first Beginning of the range
     * @param last End of the range
     * @param comp Comparison function
     * @return true if sorted according to comp, false otherwise
     * According to AUTOSAR SWS_CORE_18311
     */
    template <typename ForwardIt, typename Compare>
    constexpr Bool IsSorted(ForwardIt first, ForwardIt last, Compare comp)
    {
        return std::is_sorted(first, last, comp);
    }

    // ========================================================================
    // Min/Max operations (AUTOSAR SWS_CORE_18400 - 18499)
    // ========================================================================

    /**
     * @brief Get the smaller of two values
     * @tparam T Value type
     * @param a First value
     * @param b Second value
     * @return Reference to the smaller value
     * According to AUTOSAR SWS_CORE_18401
     */
    template <typename T>
    constexpr const T& Min(const T& a, const T& b)
    {
        return std::min(a, b);
    }

    /**
     * @brief Get the larger of two values
     * @tparam T Value type
     * @param a First value
     * @param b Second value
     * @return Reference to the larger value
     * According to AUTOSAR SWS_CORE_18402
     */
    template <typename T>
    constexpr const T& Max(const T& a, const T& b)
    {
        return std::max(a, b);
    }

    /**
     * @brief Clamp value between bounds
     * @tparam T Value type
     * @param value Value to clamp
     * @param low Lower bound
     * @param high Upper bound
     * @return Clamped value
     * According to AUTOSAR SWS_CORE_18410
     */
    template <typename T>
    constexpr const T& Clamp(const T& value, const T& low, const T& high)
    {
#if __cplusplus >= 201703L
        return std::clamp(value, low, high);
#else
        return (value < low) ? low : (high < value) ? high : value;
#endif
    }

    /**
     * @brief Find minimum element in range
     * @tparam ForwardIt Forward iterator type
     * @param first Beginning of the range
     * @param last End of the range
     * @return Iterator to minimum element
     * According to AUTOSAR SWS_CORE_18420
     */
    template <typename ForwardIt>
    constexpr ForwardIt MinElement(ForwardIt first, ForwardIt last)
    {
        return std::min_element(first, last);
    }

    /**
     * @brief Find maximum element in range
     * @tparam ForwardIt Forward iterator type
     * @param first Beginning of the range
     * @param last End of the range
     * @return Iterator to maximum element
     * According to AUTOSAR SWS_CORE_18421
     */
    template <typename ForwardIt>
    constexpr ForwardIt MaxElement(ForwardIt first, ForwardIt last)
    {
        return std::max_element(first, last);
    }

} // namespace core
} // namespace lap

#endif // LAP_CORE_ALGORITHM_HPP
