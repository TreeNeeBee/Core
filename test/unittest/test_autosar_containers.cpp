/**
 * @file        test_autosar_containers.cpp
 * @brief       Unit tests for AUTOSAR standard containers
 * @date        2025-10-29
 * @details     Tests for String, Span, Optional, and Variant
 */
#include <gtest/gtest.h>
#include "CString.hpp"
#include "CSpan.hpp"
#include "COptional.hpp"
#include "CVariant.hpp"

using namespace lap::core;

// ============================================================================
// String Tests
// ============================================================================

TEST(AutosarStringTest, BasicStringOperations)
{
    String str1 = "Hello";
    String str2 = "World"_s;
    
    EXPECT_EQ(str1.size(), 5);
    EXPECT_EQ(str2.size(), 5);
    
    String str3 = str1 + " " + str2;
    EXPECT_EQ(str3, "Hello World");
}

TEST(AutosarStringTest, WideStringOperations)
{
    WString wstr = L"Wide String"_ws;
    EXPECT_GT(wstr.size(), 0);
}

TEST(AutosarStringTest, ToStringConversions)
{
    EXPECT_EQ(ToString(42), "42");
    EXPECT_EQ(ToString(3.14), "3.140000");
    EXPECT_EQ(ToString(-100), "-100");
}

// ============================================================================
// Span Tests
// ============================================================================

TEST(AutosarSpanTest, ArraySpan)
{
    int arr[] = {1, 2, 3, 4, 5};
    auto span = MakeSpan(arr);
    
    EXPECT_EQ(span.size(), 5);
#if __cplusplus >= 202002L
    EXPECT_EQ(span[0], 1);
    EXPECT_EQ(span[4], 5);
#else
    EXPECT_EQ(span.data()[0], 1);
    EXPECT_EQ(span.data()[4], 5);
#endif
}

TEST(AutosarSpanTest, VectorSpan)
{
    Vector<int> vec = {10, 20, 30};
    auto span = MakeSpan(vec);
    
    EXPECT_EQ(span.size(), 3);
#if __cplusplus >= 202002L
    EXPECT_EQ(span[0], 10);
    EXPECT_EQ(span[2], 30);
#else
    EXPECT_EQ(span.data()[0], 10);
    EXPECT_EQ(span.data()[2], 30);
#endif
}

TEST(AutosarSpanTest, StdArraySpan)
{
    Array<int, 4> arr = {1, 2, 3, 4};
    auto span = MakeSpan(arr);
    
    EXPECT_EQ(span.size(), 4);
#if __cplusplus >= 202002L
    EXPECT_EQ(span[1], 2);
#else
    EXPECT_EQ(span.data()[1], 2);
#endif
}

TEST(AutosarSpanTest, PointerSpan)
{
    int data[] = {5, 6, 7, 8};
    auto span = MakeSpan(data, 4);
    
    EXPECT_EQ(span.size(), 4);
#if __cplusplus >= 202002L
    EXPECT_EQ(span[2], 7);
#else
    EXPECT_EQ(span.data()[2], 7);
#endif
}

// ============================================================================
// Optional Tests
// ============================================================================

TEST(AutosarOptionalTest, EmptyOptional)
{
    Optional<int> opt;
    
    EXPECT_FALSE(opt.has_value());
    EXPECT_EQ(opt, nullopt);
}

TEST(AutosarOptionalTest, OptionalWithValue)
{
    Optional<int> opt = 42;
    
    EXPECT_TRUE(opt.has_value());
    EXPECT_EQ(*opt, 42);
    EXPECT_EQ(opt.value(), 42);
}

TEST(AutosarOptionalTest, MakeOptional)
{
    auto opt1 = MakeOptional(123);
    EXPECT_TRUE(opt1.has_value());
    EXPECT_EQ(*opt1, 123);
    
    auto opt2 = MakeOptional<String>("test");
    EXPECT_TRUE(opt2.has_value());
    EXPECT_EQ(*opt2, "test");
}

TEST(AutosarOptionalTest, OptionalValueOr)
{
    Optional<int> opt1 = 10;
    Optional<int> opt2;
    
    EXPECT_EQ(opt1.value_or(99), 10);
    EXPECT_EQ(opt2.value_or(99), 99);
}

TEST(AutosarOptionalTest, OptionalReset)
{
    Optional<int> opt = 42;
    EXPECT_TRUE(opt.has_value());
    
    opt.reset();
    EXPECT_FALSE(opt.has_value());
}

// ============================================================================
// Variant Tests
// ============================================================================

TEST(AutosarVariantTest, VariantConstruction)
{
    Variant<int, double, String> var1 = 42;
    Variant<int, double, String> var2 = 3.14;
    Variant<int, double, String> var3 = String("hello");
    
    EXPECT_EQ(GetVariantIndex(var1), 0);
    EXPECT_EQ(GetVariantIndex(var2), 1);
    EXPECT_EQ(GetVariantIndex(var3), 2);
}

TEST(AutosarVariantTest, VariantGet)
{
    Variant<int, double, String> var = 42;
    
    EXPECT_EQ(get<int>(var), 42);
    EXPECT_EQ(get<0>(var), 42);
}

TEST(AutosarVariantTest, VariantGetIf)
{
    Variant<int, double, String> var = 3.14;
    
    auto* pInt = get_if<int>(&var);
    auto* pDouble = get_if<double>(&var);
    
    EXPECT_EQ(pInt, nullptr);
    EXPECT_NE(pDouble, nullptr);
    if (pDouble) {
        EXPECT_DOUBLE_EQ(*pDouble, 3.14);
    }
}

TEST(AutosarVariantTest, VariantHoldsAlternative)
{
    Variant<int, double, String> var = String("test");
    
#if __cplusplus >= 201703L
    EXPECT_TRUE(holds_alternative<String>(var));
    EXPECT_FALSE(holds_alternative<int>(var));
    EXPECT_FALSE(holds_alternative<double>(var));
#endif
}

TEST(AutosarVariantTest, VariantVisitor)
{
    Variant<int, double, String> var = 42;
    
    struct Visitor
    {
        String operator()(int i) const { return "int: " + ToString(i); }
        String operator()(double d) const { return "double: " + ToString(d); }
        String operator()(const String& s) const { return "string: " + s; }
    };
    
    auto result = visit(Visitor{}, var);
    EXPECT_EQ(result, "int: 42");
    
    var = 3.14;
    result = visit(Visitor{}, var);
    EXPECT_EQ(result, "double: 3.140000");
    
    var = String("hello");
    result = visit(Visitor{}, var);
    EXPECT_EQ(result, "string: hello");
}

TEST(AutosarVariantTest, VariantAssignment)
{
    Variant<int, double> var = 10;
    EXPECT_EQ(get<int>(var), 10);
    
    var = 2.5;
    EXPECT_DOUBLE_EQ(get<double>(var), 2.5);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST(AutosarContainersIntegration, OptionalOfVariant)
{
    using VarType = Variant<int, String>;
    Optional<VarType> opt;
    
    EXPECT_FALSE(opt.has_value());
    
    opt = VarType(42);
    EXPECT_TRUE(opt.has_value());
    EXPECT_EQ(get<int>(*opt), 42);
    
    opt = VarType(String("test"));
    EXPECT_TRUE(opt.has_value());
    EXPECT_EQ(get<String>(*opt), "test");
}

TEST(AutosarContainersIntegration, VectorOfOptionals)
{
    Vector<Optional<int>> vec;
    vec.push_back(MakeOptional(1));
    vec.push_back(nullopt);
    vec.push_back(MakeOptional(3));
    
    EXPECT_EQ(vec.size(), 3);
    EXPECT_TRUE(vec[0].has_value());
    EXPECT_FALSE(vec[1].has_value());
    EXPECT_TRUE(vec[2].has_value());
    
    EXPECT_EQ(*vec[0], 1);
    EXPECT_EQ(*vec[2], 3);
}

TEST(AutosarContainersIntegration, SpanOfVariants)
{
    Vector<Variant<int, double>> vec;
    vec.push_back(42);
    vec.push_back(3.14);
    vec.push_back(100);
    
    auto span = MakeSpan(vec);
    EXPECT_EQ(span.size(), 3);
    
#if __cplusplus >= 202002L
    EXPECT_EQ(get<int>(span[0]), 42);
    EXPECT_DOUBLE_EQ(get<double>(span[1]), 3.14);
    EXPECT_EQ(get<int>(span[2]), 100);
#else
    EXPECT_EQ(get<int>(span.data()[0]), 42);
    EXPECT_DOUBLE_EQ(get<double>(span.data()[1]), 3.14);
    EXPECT_EQ(get<int>(span.data()[2]), 100);
#endif
}
