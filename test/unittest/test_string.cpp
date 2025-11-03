/**
 * @file        test_string.cpp
 * @brief       Comprehensive unit tests for CString types and utilities
 * @details     Tests AUTOSAR string types, StringView, literals, and conversions
 */

#include <gtest/gtest.h>
#include "CString.hpp"
#include <sstream>

using namespace lap::core;

// ============================================================================
// String Type Tests
// ============================================================================

TEST(StringTest, BasicConstruction)
{
    String s1;
    EXPECT_TRUE(s1.empty());
    EXPECT_EQ(s1.size(), 0);
    
    String s2("Hello");
    EXPECT_EQ(s2, "Hello");
    EXPECT_EQ(s2.size(), 5);
    
    String s3(s2);
    EXPECT_EQ(s3, s2);
    
    String s4(std::move(s3));
    EXPECT_EQ(s4, "Hello");
}

TEST(StringTest, StringOperations)
{
    String s1 = "Hello";
    String s2 = " World";
    
    String result = s1 + s2;
    EXPECT_EQ(result, "Hello World");
    
    s1 += s2;
    EXPECT_EQ(s1, "Hello World");
    
    EXPECT_TRUE(s1.find("World") != String::npos);
    EXPECT_EQ(s1.substr(0, 5), "Hello");
}

TEST(StringTest, WideStringBasic)
{
    WString ws1;
    EXPECT_TRUE(ws1.empty());
    
    WString ws2(L"Wide String");
    EXPECT_EQ(ws2.size(), 11);
    EXPECT_EQ(ws2, L"Wide String");
}

TEST(StringTest, U16StringBasic)
{
    U16String u16s1;
    EXPECT_TRUE(u16s1.empty());
    
    U16String u16s2(u"UTF16 String");
    EXPECT_EQ(u16s2.size(), 12);
    EXPECT_EQ(u16s2, u"UTF16 String");
}

TEST(StringTest, U32StringBasic)
{
    U32String u32s1;
    EXPECT_TRUE(u32s1.empty());
    
    U32String u32s2(U"UTF32 String");
    EXPECT_EQ(u32s2.size(), 12);
    EXPECT_EQ(u32s2, U"UTF32 String");
}

// ============================================================================
// StringView Tests
// ============================================================================

TEST(StringViewTest, BasicConstruction)
{
    const char* cstr = "Hello StringView";
    StringView sv1(cstr);
    
    EXPECT_EQ(sv1.size(), 16);
    EXPECT_FALSE(sv1.empty());
    EXPECT_EQ(sv1, "Hello StringView");
}

TEST(StringViewTest, ViewOperations)
{
    String s = "Hello World";
    StringView sv(s);
    
    EXPECT_EQ(sv.size(), s.size());
    EXPECT_EQ(sv, s);
    
    StringView sub = sv.substr(0, 5);
    EXPECT_EQ(sub, "Hello");
    
    EXPECT_TRUE(sv.find("World") != StringView::npos);
    EXPECT_EQ(sv.find("World"), 6);
}

TEST(StringViewTest, ComparisonOperations)
{
    StringView sv1("ABC");
    StringView sv2("ABC");
    StringView sv3("XYZ");
    
    EXPECT_EQ(sv1, sv2);
    EXPECT_NE(sv1, sv3);
    EXPECT_LT(sv1, sv3);
    EXPECT_LE(sv1, sv2);
    EXPECT_GT(sv3, sv1);
    EXPECT_GE(sv2, sv1);
}

TEST(StringViewTest, IteratorSupport)
{
    StringView sv("Test");
    
    int count = 0;
    for(auto c : sv) {
        EXPECT_TRUE(c == 'T' || c == 'e' || c == 's' || c == 't');
        count++;
    }
    EXPECT_EQ(count, 4);
    
    EXPECT_EQ(*sv.begin(), 'T');
    EXPECT_EQ(sv.front(), 'T');
    EXPECT_EQ(sv.back(), 't');
}

TEST(StringViewTest, WideStringView)
{
    WString ws = L"Wide View";
    WStringView wsv(ws);
    
    EXPECT_EQ(wsv.size(), 9);
    EXPECT_FALSE(wsv.empty());
}

// ============================================================================
// String Literal Operator Tests
// ============================================================================

TEST(StringLiteralTest, NarrowStringLiteral)
{
    auto s = "Hello"_s;
    EXPECT_EQ(s, "Hello");
    EXPECT_EQ(s.size(), 5);
    
    String s2 = "World"_s;
    EXPECT_EQ(s2, "World");
}

TEST(StringLiteralTest, U16StringLiteral)
{
    auto s = u"UTF16"_u16s;
    EXPECT_EQ(s, u"UTF16");
    EXPECT_EQ(s.size(), 5);
}

TEST(StringLiteralTest, U32StringLiteral)
{
    auto s = U"UTF32"_u32s;
    EXPECT_EQ(s, U"UTF32");
    EXPECT_EQ(s.size(), 5);
}

TEST(StringLiteralTest, WideStringLiteral)
{
    auto s = L"Wide"_ws;
    EXPECT_EQ(s, L"Wide");
    EXPECT_EQ(s.size(), 4);
}

// ============================================================================
// ToString Conversion Tests
// ============================================================================

TEST(ToStringTest, IntegerConversions)
{
    EXPECT_EQ(ToString(0), "0");
    EXPECT_EQ(ToString(123), "123");
    EXPECT_EQ(ToString(-456), "-456");
    
    EXPECT_EQ(ToString(123L), "123");
    EXPECT_EQ(ToString(123LL), "123");
    
    EXPECT_EQ(ToString(123U), "123");
    EXPECT_EQ(ToString(123UL), "123");
    EXPECT_EQ(ToString(123ULL), "123");
}

TEST(ToStringTest, FloatingPointConversions)
{
    String result = ToString(3.14f);
    EXPECT_TRUE(result.find("3.14") != String::npos);
    
    result = ToString(2.718);
    EXPECT_TRUE(result.find("2.718") != String::npos);
    
    result = ToString(1.414L);
    EXPECT_TRUE(result.find("1.414") != String::npos);
}

TEST(ToStringTest, EdgeCaseNumbers)
{
    EXPECT_EQ(ToString(0), "0");
    
    String maxInt = ToString(std::numeric_limits<int>::max());
    EXPECT_FALSE(maxInt.empty());
    
    String minInt = ToString(std::numeric_limits<int>::min());
    EXPECT_FALSE(minInt.empty());
    EXPECT_EQ(minInt[0], '-');
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST(StringIntegrationTest, StringViewFromString)
{
    String s = "Integration Test";
    StringView sv(s);
    
    EXPECT_EQ(sv, s);
    EXPECT_EQ(sv.data(), s.data());
    EXPECT_EQ(sv.size(), s.size());
    
    // Modify string, view should still point to it
    s += " Extended";
    // Note: sv might be invalidated after modification
    StringView sv2(s);
    EXPECT_EQ(sv2, "Integration Test Extended");
}

TEST(StringIntegrationTest, LiteralAndConversion)
{
    auto s1 = "Number: "_s;
    auto s2 = ToString(42);
    
    String result = s1 + s2;
    EXPECT_EQ(result, "Number: 42");
}

TEST(StringIntegrationTest, MixedStringTypes)
{
    String narrow = "Narrow";
    WString wide = L"Wide";
    U16String utf16 = u"UTF16";
    U32String utf32 = U"UTF32";
    
    EXPECT_EQ(narrow.size(), 6);
    EXPECT_EQ(wide.size(), 4);
    EXPECT_EQ(utf16.size(), 5);
    EXPECT_EQ(utf32.size(), 5);
}

// ============================================================================
// Performance and Edge Cases
// ============================================================================

TEST(StringEdgeCaseTest, EmptyStrings)
{
    String s1;
    String s2("");
    StringView sv("");
    
    EXPECT_TRUE(s1.empty());
    EXPECT_TRUE(s2.empty());
    EXPECT_TRUE(sv.empty());
    
    EXPECT_EQ(s1.size(), 0);
    EXPECT_EQ(s2.size(), 0);
    EXPECT_EQ(sv.size(), 0);
}

TEST(StringEdgeCaseTest, LargeStrings)
{
    String large(10000, 'A');
    EXPECT_EQ(large.size(), 10000);
    EXPECT_EQ(large[0], 'A');
    EXPECT_EQ(large[9999], 'A');
    
    StringView sv(large);
    EXPECT_EQ(sv.size(), 10000);
}

TEST(StringEdgeCaseTest, SpecialCharacters)
{
    String special = "Tab\tNewline\nReturn\rNull";
    EXPECT_TRUE(special.find('\t') != String::npos);
    EXPECT_TRUE(special.find('\n') != String::npos);
    EXPECT_TRUE(special.find('\r') != String::npos);
}

TEST(StringEdgeCaseTest, StringViewLifetime)
{
    StringView sv;
    {
        String temp = "Temporary";
        sv = StringView(temp);
        EXPECT_EQ(sv, "Temporary");
    }
    // Note: sv is now dangling - this test just verifies basic behavior
    // In real code, ensure StringView doesn't outlive source
}
