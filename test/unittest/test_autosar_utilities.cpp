/**
 * @file        test_autosar_utilities.cpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       Unit tests for AUTOSAR utility classes
 * @date        2025-10-29
 * @details     Tests for ByteOrder, Algorithm, Function, and Utility classes
 * @copyright   Copyright (c) 2025
 */

#include <gtest/gtest.h>
#include "CByteOrder.hpp"
#include "CAlgorithm.hpp"
#include "CFunction.hpp"
#include "CUtility.hpp"
#include "CString.hpp"
#include "CMemory.hpp"
#include <vector>
#include <algorithm>

using namespace lap::core;

// ============================================================================
// ByteOrder Tests
// ============================================================================

TEST(AutosarByteOrderTest, PlatformByteOrder)
{
    // Just verify it returns a valid value
    auto order = GetPlatformByteOrder();
    EXPECT_TRUE(order == ByteOrder::kLittleEndian || order == ByteOrder::kBigEndian);
}

TEST(AutosarByteOrderTest, ByteSwap16)
{
    UInt16 value = 0x1234;
    UInt16 swapped = ByteSwap16(value);
    EXPECT_EQ(swapped, 0x3412);
    
    // Double swap should return original
    EXPECT_EQ(ByteSwap16(swapped), value);
}

TEST(AutosarByteOrderTest, ByteSwap32)
{
    UInt32 value = 0x12345678;
    UInt32 swapped = ByteSwap32(value);
    EXPECT_EQ(swapped, 0x78563412);
    
    // Double swap should return original
    EXPECT_EQ(ByteSwap32(swapped), value);
}

TEST(AutosarByteOrderTest, ByteSwap64)
{
    UInt64 value = 0x123456789ABCDEF0ULL;
    UInt64 swapped = ByteSwap64(value);
    EXPECT_EQ(swapped, 0xF0DEBC9A78563412ULL);
    
    // Double swap should return original
    EXPECT_EQ(ByteSwap64(swapped), value);
}

TEST(AutosarByteOrderTest, HostToNetworkConversion)
{
    UInt16 host16 = 0x1234;
    UInt16 net16 = HostToNetwork16(host16);
    EXPECT_EQ(NetworkToHost16(net16), host16);
    
    UInt32 host32 = 0x12345678;
    UInt32 net32 = HostToNetwork32(host32);
    EXPECT_EQ(NetworkToHost32(net32), host32);
    
    UInt64 host64 = 0x123456789ABCDEF0ULL;
    UInt64 net64 = HostToNetwork64(host64);
    EXPECT_EQ(NetworkToHost64(net64), host64);
}

TEST(AutosarByteOrderTest, ByteOrderConversion)
{
    UInt32 value = 0x12345678;
    
    // Convert to big-endian and back
    auto be = HostToByteOrder(value, ByteOrder::kBigEndian);
    auto back = ByteOrderToHost(be, ByteOrder::kBigEndian);
    EXPECT_EQ(back, value);
    
    // Convert to little-endian and back
    auto le = HostToByteOrder(value, ByteOrder::kLittleEndian);
    auto back2 = ByteOrderToHost(le, ByteOrder::kLittleEndian);
    EXPECT_EQ(back2, value);
}

// ============================================================================
// Algorithm Tests
// ============================================================================

TEST(AutosarAlgorithmTest, FindIf)
{
    Vector<int> vec = {1, 2, 3, 4, 5};
    auto it = FindIf(vec.begin(), vec.end(), [](int x) { return x > 3; });
    
    ASSERT_NE(it, vec.end());
    EXPECT_EQ(*it, 4);
}

TEST(AutosarAlgorithmTest, AllOf)
{
    Vector<int> vec = {2, 4, 6, 8};
    Bool all_even = AllOf(vec.begin(), vec.end(), [](int x) { return x % 2 == 0; });
    EXPECT_TRUE(all_even);
    
    vec.push_back(5);
    all_even = AllOf(vec.begin(), vec.end(), [](int x) { return x % 2 == 0; });
    EXPECT_FALSE(all_even);
}

TEST(AutosarAlgorithmTest, AnyOf)
{
    Vector<int> vec = {1, 3, 5, 7};
    Bool any_even = AnyOf(vec.begin(), vec.end(), [](int x) { return x % 2 == 0; });
    EXPECT_FALSE(any_even);
    
    vec.push_back(4);
    any_even = AnyOf(vec.begin(), vec.end(), [](int x) { return x % 2 == 0; });
    EXPECT_TRUE(any_even);
}

TEST(AutosarAlgorithmTest, NoneOf)
{
    Vector<int> vec = {1, 3, 5, 7};
    Bool none_even = NoneOf(vec.begin(), vec.end(), [](int x) { return x % 2 == 0; });
    EXPECT_TRUE(none_even);
    
    vec.push_back(4);
    none_even = NoneOf(vec.begin(), vec.end(), [](int x) { return x % 2 == 0; });
    EXPECT_FALSE(none_even);
}

TEST(AutosarAlgorithmTest, CountIf)
{
    Vector<int> vec = {1, 2, 3, 4, 5, 6};
    auto count = CountIf(vec.begin(), vec.end(), [](int x) { return x % 2 == 0; });
    EXPECT_EQ(count, 3);
}

TEST(AutosarAlgorithmTest, Transform)
{
    Vector<int> src = {1, 2, 3, 4, 5};
    Vector<int> dst(src.size());
    
    Transform(src.begin(), src.end(), dst.begin(), [](int x) { return x * 2; });
    
    for (size_t i = 0; i < src.size(); ++i) {
        EXPECT_EQ(dst[i], src[i] * 2);
    }
}

TEST(AutosarAlgorithmTest, Sort)
{
    Vector<int> vec = {5, 2, 8, 1, 9, 3};
    Sort(vec.begin(), vec.end());
    
    EXPECT_TRUE(IsSorted(vec.begin(), vec.end()));
    EXPECT_EQ(vec[0], 1);
    EXPECT_EQ(vec[vec.size() - 1], 9);
}

TEST(AutosarAlgorithmTest, MinMax)
{
    EXPECT_EQ(Min(5, 3), 3);
    EXPECT_EQ(Max(5, 3), 5);
    
    Vector<int> vec = {5, 2, 8, 1, 9, 3};
    auto min_it = MinElement(vec.begin(), vec.end());
    auto max_it = MaxElement(vec.begin(), vec.end());
    
    EXPECT_EQ(*min_it, 1);
    EXPECT_EQ(*max_it, 9);
}

TEST(AutosarAlgorithmTest, Clamp)
{
    EXPECT_EQ(Clamp(5, 0, 10), 5);
    EXPECT_EQ(Clamp(-5, 0, 10), 0);
    EXPECT_EQ(Clamp(15, 0, 10), 10);
}

// ============================================================================
// Function Tests
// ============================================================================

TEST(AutosarFunctionTest, FunctionWrapper)
{
    Function<int(int, int)> add = [](int a, int b) { return a + b; };
    EXPECT_EQ(add(3, 4), 7);
    
    Function<int(int, int)> multiply = [](int a, int b) { return a * b; };
    EXPECT_EQ(multiply(3, 4), 12);
}

TEST(AutosarFunctionTest, Invoke)
{
    auto lambda = [](int a, int b) { return a + b; };
    auto result = Invoke(lambda, 5, 3);
    EXPECT_EQ(result, 8);
}

TEST(AutosarFunctionTest, Bind)
{
    auto add = [](int a, int b, int c) { return a + b + c; };
    auto bound = Bind(add, 10, placeholders::_1, placeholders::_2);
    
    EXPECT_EQ(bound(5, 3), 18); // 10 + 5 + 3
}

TEST(AutosarFunctionTest, ReferenceWrapper)
{
    int value = 42;
    auto ref = Ref(value);
    
    ref.get() = 100;
    EXPECT_EQ(value, 100);
    
    const int const_value = 42;
    auto cref = CRef(const_value);
    EXPECT_EQ(cref.get(), 42);
}

TEST(AutosarFunctionTest, ComparisonFunctions)
{
    EqualTo<int> eq;
    EXPECT_TRUE(eq(5, 5));
    EXPECT_FALSE(eq(5, 3));
    
    Less<int> less;
    EXPECT_TRUE(less(3, 5));
    EXPECT_FALSE(less(5, 3));
    
    Greater<int> greater;
    EXPECT_TRUE(greater(5, 3));
    EXPECT_FALSE(greater(3, 5));
}

// ============================================================================
// Utility Tests
// ============================================================================

TEST(AutosarUtilityTest, MoveAndForward)
{
    String str = "Hello";
    String moved = Move(str);
    
    EXPECT_EQ(moved, "Hello");
    // Original string may be in valid but unspecified state
}

TEST(AutosarUtilityTest, Swap)
{
    int a = 5, b = 10;
    Swap(a, b);
    
    EXPECT_EQ(a, 10);
    EXPECT_EQ(b, 5);
}

TEST(AutosarUtilityTest, DataAccess)
{
    Vector<int> vec = {1, 2, 3, 4, 5};
    auto ptr = std::data(vec);  // Use std:: to avoid ambiguity
    
    EXPECT_EQ(*ptr, 1);
    EXPECT_EQ(*(ptr + 1), 2);
    
    int arr[] = {10, 20, 30};
    auto arr_ptr = std::data(arr);  // Use std:: to avoid ambiguity
    EXPECT_EQ(*arr_ptr, 10);
}

TEST(AutosarUtilityTest, Size)
{
    Vector<int> vec = {1, 2, 3, 4, 5};
    EXPECT_EQ(std::size(vec), 5);  // Use std:: to avoid ambiguity
    
    int arr[] = {1, 2, 3};
    EXPECT_EQ(std::size(arr), 3);  // Use std:: to avoid ambiguity
}

TEST(AutosarUtilityTest, Empty)
{
    Vector<int> vec;
    EXPECT_TRUE(std::empty(vec));  // Use std:: to avoid ambiguity
    
    vec.push_back(1);
    EXPECT_FALSE(std::empty(vec));  // Use std:: to avoid ambiguity
}

TEST(AutosarUtilityTest, SignedSize)
{
    Vector<int> vec = {1, 2, 3};
    auto s = ssize(vec);
    
    EXPECT_EQ(s, 3);
    EXPECT_TRUE((std::is_same<decltype(s), std::ptrdiff_t>::value));
}

TEST(AutosarUtilityTest, TypeTraits)
{
    // Test RemoveCV
    static_assert(std::is_same<RemoveCV<const int>, int>::value, "RemoveCV failed");
    static_assert(std::is_same<RemoveCV<volatile int>, int>::value, "RemoveCV failed");
    
    // Test RemoveReference
    static_assert(std::is_same<RemoveReference<int&>, int>::value, "RemoveReference failed");
    static_assert(std::is_same<RemoveReference<int&&>, int>::value, "RemoveReference failed");
    
    // Test Decay
    static_assert(std::is_same<Decay<int&>, int>::value, "Decay failed");
    static_assert(std::is_same<Decay<const int&>, int>::value, "Decay failed");
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST(AutosarUtilitiesIntegrationTest, AlgorithmWithFunction)
{
    Vector<int> vec = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    
    // Use Function with algorithm
    Function<Bool(int)> is_even = [](int x) { return x % 2 == 0; };
    
    auto count = CountIf(vec.begin(), vec.end(), is_even);
    EXPECT_EQ(count, 5);
    
    Vector<int> evens;
    CopyIf(vec.begin(), vec.end(), std::back_inserter(evens), is_even);
    EXPECT_EQ(evens.size(), 5);
    EXPECT_TRUE(AllOf(evens.begin(), evens.end(), is_even));
}

TEST(AutosarUtilitiesIntegrationTest, ByteOrderWithContainer)
{
    Vector<UInt32> host_values = {0x12345678, 0xABCDEF00, 0x11223344};
    Vector<UInt32> network_values;
    
    Transform(host_values.begin(), host_values.end(), 
              std::back_inserter(network_values),
              [](UInt32 val) { return HostToNetwork32(val); });
    
    EXPECT_EQ(network_values.size(), host_values.size());
    
    // Convert back
    Vector<UInt32> back_to_host;
    Transform(network_values.begin(), network_values.end(),
              std::back_inserter(back_to_host),
              [](UInt32 val) { return NetworkToHost32(val); });
    
    EXPECT_EQ(back_to_host, host_values);
}

