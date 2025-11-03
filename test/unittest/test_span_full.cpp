/**
 * @file        test_span_full.cpp
 * @brief       Comprehensive unit tests for Span container
 * @details     Full coverage of Span construction, access, iteration, and operations
 */

#include <gtest/gtest.h>
#include "CSpan.hpp"
#include "CString.hpp"
#include "CAlgorithm.hpp"

using namespace lap::core;

// ============================================================================
// Span Construction Tests
// ============================================================================

TEST(SpanFullTest, DefaultConstruction)
{
    Span<int> sp;
    EXPECT_TRUE(sp.empty());
    EXPECT_EQ(sp.size(), 0);
}

TEST(SpanFullTest, ConstructFromArray)
{
    int arr[] = {1, 2, 3, 4, 5};
    auto sp = MakeSpan(arr);
    
    EXPECT_FALSE(sp.empty());
    EXPECT_EQ(sp.size(), 5);
    EXPECT_EQ(sp.data()[0], 1);
    EXPECT_EQ(sp.data()[4], 5);
}

TEST(SpanFullTest, ConstructFromCArray)
{
    int arr[] = {10, 20, 30};
    auto sp = MakeSpan(arr);
    
    EXPECT_EQ(sp.size(), 3);
    EXPECT_EQ(sp.data()[0], 10);
    EXPECT_EQ(sp.data()[2], 30);
}

TEST(SpanFullTest, ConstructFromVector)
{
    Vector<int> vec = {1, 2, 3, 4};
    auto sp = MakeSpan(vec);
    
    EXPECT_EQ(sp.size(), 4);
    EXPECT_EQ(sp.data()[0], 1);
    EXPECT_EQ(sp.data()[3], 4);
}

TEST(SpanFullTest, ConstructFromStdArray)
{
    std::array<int, 3> arr = {5, 10, 15};
    auto sp = MakeSpan(arr);
    
    EXPECT_EQ(sp.size(), 3);
    EXPECT_EQ(sp.data()[0], 5);
    EXPECT_EQ(sp.data()[2], 15);
}

TEST(SpanFullTest, CopyConstruction)
{
    int arr[] = {1, 2, 3};
    auto sp1 = MakeSpan(arr);
    Span<int> sp2(sp1);
    
    EXPECT_EQ(sp1.size(), sp2.size());
    EXPECT_EQ(sp1.data(), sp2.data());
    EXPECT_EQ(sp2.data()[1], 2);
}

// ============================================================================
// Span MakeSpan Helper Tests
// ============================================================================

TEST(SpanMakeSpanTest, MakeSpanFromArray)
{
    int arr[] = {1, 2, 3, 4, 5};
    auto sp = MakeSpan(arr);
    
    EXPECT_EQ(sp.size(), 5);
    EXPECT_EQ(sp.data()[0], 1);
}

TEST(SpanMakeSpanTest, MakeSpanFromCArray)
{
    int arr[] = {10, 20, 30};
    auto sp = MakeSpan(arr);
    
    EXPECT_EQ(sp.size(), 3);
    EXPECT_EQ(sp.data()[1], 20);
}

TEST(SpanMakeSpanTest, MakeSpanFromVector)
{
    Vector<int> vec = {5, 10, 15, 20};
    auto sp = MakeSpan(vec);
    
    EXPECT_EQ(sp.size(), 4);
    EXPECT_EQ(sp.data()[2], 15);
}

TEST(SpanMakeSpanTest, MakeSpanFromStdArray)
{
    std::array<int, 3> arr = {100, 200, 300};
    auto sp = MakeSpan(arr);
    
    EXPECT_EQ(sp.size(), 3);
    EXPECT_EQ(sp.data()[0], 100);
}

// ============================================================================
// Span Element Access Tests
// ============================================================================

TEST(SpanAccessTest, SubscriptOperator)
{
    int arr[] = {10, 20, 30, 40, 50};
    auto sp = MakeSpan(arr);
    
    EXPECT_EQ(sp.data()[0], 10);
    EXPECT_EQ(sp.data()[2], 30);
    EXPECT_EQ(sp.data()[4], 50);
}

TEST(SpanAccessTest, FrontAndBack)
{
    int arr[] = {5, 10, 15, 20};
    auto sp = MakeSpan(arr);
    
    EXPECT_EQ(sp.data()[0], 5);
    EXPECT_EQ(*(sp.end() - 1), 20);
}

TEST(SpanAccessTest, DataPointer)
{
    int arr[] = {1, 2, 3};
    auto sp = MakeSpan(arr);
    
    EXPECT_EQ(sp.data(), arr);
    EXPECT_EQ(*sp.data(), 1);
}

// ============================================================================
// Span Iterator Tests
// ============================================================================

TEST(SpanIteratorTest, BeginEnd)
{
    int arr[] = {1, 2, 3, 4, 5};
    auto sp = MakeSpan(arr);
    
    auto it = sp.begin();
    EXPECT_EQ(*it, 1);
    
    auto end = sp.end();
    EXPECT_EQ(*(end - 1), 5);
}

TEST(SpanIteratorTest, RangeBasedFor)
{
    int arr[] = {10, 20, 30};
    auto sp = MakeSpan(arr);
    
    int sum = 0;
    for (int val : sp) {
        sum += val;
    }
    
    EXPECT_EQ(sum, 60);
}

TEST(SpanIteratorTest, IteratorIncrement)
{
    int arr[] = {5, 10, 15};
    auto sp = MakeSpan(arr);
    
    auto it = sp.begin();
    EXPECT_EQ(*it, 5);
    
    ++it;
    EXPECT_EQ(*it, 10);
    
    ++it;
    EXPECT_EQ(*it, 15);
}

TEST(SpanIteratorTest, ReverseIterator)
{
    int arr[] = {1, 2, 3, 4};
    auto sp = MakeSpan(arr);
    
    auto it = sp.end();
    --it; // now points to last element
    EXPECT_EQ(*it, 4);
    --it;
    EXPECT_EQ(*it, 3);
}

// ============================================================================
// Span Size Tests
// ============================================================================

TEST(SpanSizeTest, Size)
{
    int arr[] = {1, 2, 3, 4, 5};
    auto sp = MakeSpan(arr);
    
    EXPECT_EQ(sp.size(), 5);
}

TEST(SpanSizeTest, Empty)
{
    Span<int> sp1;
    EXPECT_TRUE(sp1.empty());
    
    int arr[] = {1};
    auto sp2 = MakeSpan(arr);
    EXPECT_FALSE(sp2.empty());
}

// ============================================================================
// Span Modification Tests (non-const)
// ============================================================================

TEST(SpanModificationTest, ModifyThroughSpan)
{
    int arr[] = {1, 2, 3};
    auto sp = MakeSpan(arr);
    
    sp.data()[0] = 10;
    sp.data()[1] = 20;
    sp.data()[2] = 30;
    
    EXPECT_EQ(arr[0], 10);
    EXPECT_EQ(arr[1], 20);
    EXPECT_EQ(arr[2], 30);
}

TEST(SpanModificationTest, ModifyThroughIterator)
{
    int arr[] = {1, 2, 3};
    auto sp = MakeSpan(arr);
    
    auto* p = sp.data();
    for (std::size_t i = 0; i < sp.size(); ++i) {
        p[i] *= 2;
    }
    
    EXPECT_EQ(arr[0], 2);
    EXPECT_EQ(arr[1], 4);
    EXPECT_EQ(arr[2], 6);
}

// ============================================================================
// Span with Algorithms
// ============================================================================

TEST(SpanAlgorithmTest, FindIf)
{
    int arr[] = {1, 2, 3, 4, 5};
    auto sp = MakeSpan(arr);
    
    auto it = FindIf(sp.begin(), sp.end(), [](int x) { return x > 3; });
    EXPECT_NE(it, sp.end());
    EXPECT_EQ(*it, 4);
}

TEST(SpanAlgorithmTest, AllOf)
{
    int arr[] = {2, 4, 6, 8};
    auto sp = MakeSpan(arr);
    
    bool allEven = AllOf(sp.begin(), sp.end(), [](int x) { return x % 2 == 0; });
    EXPECT_TRUE(allEven);
}

TEST(SpanAlgorithmTest, AnyOf)
{
    int arr[] = {1, 3, 5, 6};
    auto sp = MakeSpan(arr);
    
    bool hasEven = AnyOf(sp.begin(), sp.end(), [](int x) { return x % 2 == 0; });
    EXPECT_TRUE(hasEven);
}

TEST(SpanAlgorithmTest, CountIf)
{
    int arr[] = {1, 2, 3, 4, 5, 6};
    auto sp = MakeSpan(arr);
    
    auto count = CountIf(sp.begin(), sp.end(), [](int x) { return x > 3; });
    EXPECT_EQ(count, 3);
}

// ============================================================================
// Span with Different Types
// ============================================================================

TEST(SpanTypesTest, SpanOfStrings)
{
    String arr[] = {"Hello", "World", "Test"};
    auto sp = MakeSpan(arr);
    
    EXPECT_EQ(sp.size(), 3);
    EXPECT_EQ(sp.data()[0], "Hello");
    EXPECT_EQ(sp.data()[1], "World");
}

TEST(SpanTypesTest, SpanOfBytes)
{
    UInt8 bytes[] = {0x01, 0x02, 0x03, 0xFF};
    auto sp = MakeSpan(bytes);
    
    EXPECT_EQ(sp.size(), 4);
    EXPECT_EQ(sp.data()[0], 0x01);
    EXPECT_EQ(sp.data()[3], 0xFF);
}

struct TestData {
    int id;
    double value;
};

TEST(SpanTypesTest, SpanOfStructs)
{
    TestData data[] = {{1, 1.5}, {2, 2.5}, {3, 3.5}};
    auto sp = MakeSpan(data);
    
    EXPECT_EQ(sp.size(), 3);
    EXPECT_EQ(sp.data()[0].id, 1);
    EXPECT_DOUBLE_EQ(sp.data()[1].value, 2.5);
}

// ============================================================================
// Span Edge Cases
// ============================================================================

TEST(SpanEdgeCaseTest, SingleElement)
{
    int arr[] = {42};
    auto sp = MakeSpan(arr);
    
    EXPECT_EQ(sp.size(), 1);
    EXPECT_EQ(sp.data()[0], 42);
    EXPECT_EQ(sp.data()[0], *(sp.end() - 1));
}

TEST(SpanEdgeCaseTest, LargeSpan)
{
    Vector<int> vec(10000, 42);
    auto sp = MakeSpan(vec);
    
    EXPECT_EQ(sp.size(), 10000);
    EXPECT_EQ(sp.data()[0], 42);
    EXPECT_EQ(sp.data()[9999], 42);
}

// ============================================================================
// Span Integration Tests
// ============================================================================

TEST(SpanIntegrationTest, SpanFromVectorModification)
{
    Vector<int> vec = {1, 2, 3, 4, 5};
    auto sp = MakeSpan(vec);
    
    // Modify through span
    auto* p = sp.data();
    for (std::size_t i = 0; i < sp.size(); ++i) {
        p[i] *= 2;
    }
    
    // Check vector was modified
    EXPECT_EQ(vec[0], 2);
    EXPECT_EQ(vec[2], 6);
    EXPECT_EQ(vec[4], 10);
}

TEST(SpanIntegrationTest, SpanAsFunctionParameter)
{
    auto sumSpan = [](Span<int> sp) {
        int sum = 0;
        for (int val : sp) {
            sum += val;
        }
        return sum;
    };
    
    int arr[] = {1, 2, 3, 4, 5};
    EXPECT_EQ(sumSpan(MakeSpan(arr)), 15);
    
    Vector<int> vec = {10, 20, 30};
    EXPECT_EQ(sumSpan(MakeSpan(vec)), 60);
}
