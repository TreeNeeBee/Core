/**
 * @file        test_stringview_r24.cpp
 * @brief       Unit tests for AUTOSAR R24-11 StringView enhancements
 * @date        2025-11-12
 * @details     Tests C++20 compatibility functions for C++17
 */

#include <gtest/gtest.h>
#include "CString.hpp"

using namespace lap::core;

// ============================================================================
// StringView starts_with Tests
// ============================================================================

TEST(StringViewR24Test, StartsWithStringView)
{
    StringView sv("Hello, World!");
    
#if __cplusplus >= 202002L
    EXPECT_TRUE(sv.starts_with("Hello"));
    EXPECT_TRUE(sv.starts_with(StringView("Hello")));
    EXPECT_FALSE(sv.starts_with("World"));
#else
    EXPECT_TRUE(starts_with(sv, "Hello"));
    EXPECT_TRUE(starts_with(sv, StringView("Hello")));
    EXPECT_FALSE(starts_with(sv, "World"));
#endif
}

TEST(StringViewR24Test, StartsWithCharacter)
{
    StringView sv("Hello");
    
#if __cplusplus >= 202002L
    EXPECT_TRUE(sv.starts_with('H'));
    EXPECT_FALSE(sv.starts_with('h'));
    EXPECT_FALSE(sv.starts_with('W'));
#else
    EXPECT_TRUE(starts_with(sv, 'H'));
    EXPECT_FALSE(starts_with(sv, 'h'));
    EXPECT_FALSE(starts_with(sv, 'W'));
#endif
}

TEST(StringViewR24Test, StartsWithEmptyString)
{
    StringView sv("Hello");
    StringView empty;
    
#if __cplusplus >= 202002L
    EXPECT_TRUE(sv.starts_with(""));
    EXPECT_TRUE(sv.starts_with(empty));
#else
    EXPECT_TRUE(starts_with(sv, ""));
    EXPECT_TRUE(starts_with(sv, empty));
#endif
}

TEST(StringViewR24Test, StartsWithEmptyView)
{
    StringView empty;
    
#if __cplusplus >= 202002L
    EXPECT_TRUE(empty.starts_with(""));
    EXPECT_FALSE(empty.starts_with("a"));
    EXPECT_FALSE(empty.starts_with('a'));
#else
    EXPECT_TRUE(starts_with(empty, ""));
    EXPECT_FALSE(starts_with(empty, "a"));
    EXPECT_FALSE(starts_with(empty, 'a'));
#endif
}

// ============================================================================
// StringView ends_with Tests
// ============================================================================

TEST(StringViewR24Test, EndsWithStringView)
{
    StringView sv("Hello, World!");
    
#if __cplusplus >= 202002L
    EXPECT_TRUE(sv.ends_with("World!"));
    EXPECT_TRUE(sv.ends_with(StringView("World!")));
    EXPECT_FALSE(sv.ends_with("Hello"));
#else
    EXPECT_TRUE(ends_with(sv, "World!"));
    EXPECT_TRUE(ends_with(sv, StringView("World!")));
    EXPECT_FALSE(ends_with(sv, "Hello"));
#endif
}

TEST(StringViewR24Test, EndsWithCharacter)
{
    StringView sv("Hello");
    
#if __cplusplus >= 202002L
    EXPECT_TRUE(sv.ends_with('o'));
    EXPECT_FALSE(sv.ends_with('O'));
    EXPECT_FALSE(sv.ends_with('H'));
#else
    EXPECT_TRUE(ends_with(sv, 'o'));
    EXPECT_FALSE(ends_with(sv, 'O'));
    EXPECT_FALSE(ends_with(sv, 'H'));
#endif
}

TEST(StringViewR24Test, EndsWithEmptyString)
{
    StringView sv("Hello");
    StringView empty;
    
#if __cplusplus >= 202002L
    EXPECT_TRUE(sv.ends_with(""));
    EXPECT_TRUE(sv.ends_with(empty));
#else
    EXPECT_TRUE(ends_with(sv, ""));
    EXPECT_TRUE(ends_with(sv, empty));
#endif
}

TEST(StringViewR24Test, EndsWithEmptyView)
{
    StringView empty;
    
#if __cplusplus >= 202002L
    EXPECT_TRUE(empty.ends_with(""));
    EXPECT_FALSE(empty.ends_with("a"));
    EXPECT_FALSE(empty.ends_with('a'));
#else
    EXPECT_TRUE(ends_with(empty, ""));
    EXPECT_FALSE(ends_with(empty, "a"));
    EXPECT_FALSE(ends_with(empty, 'a'));
#endif
}

// ============================================================================
// StringView contains Tests (C++23 feature)
// ============================================================================

TEST(StringViewR24Test, ContainsStringView)
{
    StringView sv("Hello, World!");
    
#if __cplusplus >= 202300L
    EXPECT_TRUE(sv.contains("World"));
    EXPECT_TRUE(sv.contains(StringView("Hello")));
    EXPECT_FALSE(sv.contains("Goodbye"));
#else
    EXPECT_TRUE(contains(sv, "World"));
    EXPECT_TRUE(contains(sv, StringView("Hello")));
    EXPECT_FALSE(contains(sv, "Goodbye"));
#endif
}

TEST(StringViewR24Test, ContainsCharacter)
{
    StringView sv("Hello");
    
#if __cplusplus >= 202300L
    EXPECT_TRUE(sv.contains('e'));
    EXPECT_TRUE(sv.contains('H'));
    EXPECT_FALSE(sv.contains('x'));
#else
    EXPECT_TRUE(contains(sv, 'e'));
    EXPECT_TRUE(contains(sv, 'H'));
    EXPECT_FALSE(contains(sv, 'x'));
#endif
}

TEST(StringViewR24Test, ContainsEmptyString)
{
    StringView sv("Hello");
    
#if __cplusplus >= 202300L
    EXPECT_TRUE(sv.contains(""));
#else
    EXPECT_TRUE(contains(sv, ""));
#endif
}

TEST(StringViewR24Test, ContainsEmptyView)
{
    StringView empty;
    
#if __cplusplus >= 202300L
    EXPECT_TRUE(empty.contains(""));
    EXPECT_FALSE(empty.contains("a"));
#else
    EXPECT_TRUE(contains(empty, ""));
    EXPECT_FALSE(contains(empty, "a"));
#endif
}

// ============================================================================
// StringView Existing C++17 Features Tests
// ============================================================================

TEST(StringViewR24Test, FindMethods)
{
    StringView sv("Hello, World!");
    
    EXPECT_EQ(sv.find("World"), 7);
    EXPECT_EQ(sv.find("Hello"), 0);
    EXPECT_EQ(sv.find("xyz"), StringView::npos);
    
    EXPECT_EQ(sv.rfind('o'), 8);  // Last 'o'
    EXPECT_EQ(sv.rfind('H'), 0);
    
    EXPECT_EQ(sv.find_first_of("WH"), 0);  // 'H' at position 0
    EXPECT_EQ(sv.find_last_of("WH"), 7);   // 'W' at position 7
}

TEST(StringViewR24Test, CompareMethods)
{
    StringView sv1("Hello");
    StringView sv2("World");
    StringView sv3("Hello");
    
    EXPECT_LT(sv1.compare(sv2), 0);  // "Hello" < "World"
    EXPECT_GT(sv2.compare(sv1), 0);  // "World" > "Hello"
    EXPECT_EQ(sv1.compare(sv3), 0);  // "Hello" == "Hello"
}

TEST(StringViewR24Test, SubstrMethod)
{
    StringView sv("Hello, World!");
    
    StringView sub1 = sv.substr(0, 5);
    EXPECT_EQ(sub1, "Hello");
    
    StringView sub2 = sv.substr(7);
    EXPECT_EQ(sub2, "World!");
    
    StringView sub3 = sv.substr(7, 5);
    EXPECT_EQ(sub3, "World");
}

TEST(StringViewR24Test, RemovePrefixSuffix)
{
    StringView sv("Hello, World!");
    
    sv.remove_prefix(7);  // Remove "Hello, "
    EXPECT_EQ(sv, "World!");
    
    sv.remove_suffix(1);  // Remove "!"
    EXPECT_EQ(sv, "World");
}

TEST(StringViewR24Test, FrontBack)
{
    StringView sv("Hello");
    
    EXPECT_EQ(sv.front(), 'H');
    EXPECT_EQ(sv.back(), 'o');
}

TEST(StringViewR24Test, EmptySize)
{
    StringView empty;
    StringView non_empty("Hello");
    
    EXPECT_TRUE(empty.empty());
    EXPECT_EQ(empty.size(), 0);
    EXPECT_EQ(empty.length(), 0);
    
    EXPECT_FALSE(non_empty.empty());
    EXPECT_EQ(non_empty.size(), 5);
    EXPECT_EQ(non_empty.length(), 5);
}

// ============================================================================
// StringView Literal Operator Tests
// ============================================================================

TEST(StringViewR24Test, LiteralOperators)
{
    using namespace lap::core;
    
    String s = "Hello"_s;
    EXPECT_EQ(s, "Hello");
    EXPECT_EQ(s.size(), 5);
    
    U16String u16s = u"世界"_u16s;
    EXPECT_EQ(u16s.size(), 2);
    
    U32String u32s = U"🌍"_u32s;
    EXPECT_EQ(u32s.size(), 1);
    
    WString ws = L"Wide"_ws;
    EXPECT_EQ(ws.size(), 4);
}

// ============================================================================
// StringView Integration Tests
// ============================================================================

TEST(StringViewR24Test, WorksWithStdString)
{
    String str = "Hello, World!";
    StringView sv(str);
    
#if __cplusplus >= 202002L
    EXPECT_TRUE(sv.starts_with("Hello"));
    EXPECT_TRUE(sv.ends_with("World!"));
#else
    EXPECT_TRUE(starts_with(sv, "Hello"));
    EXPECT_TRUE(ends_with(sv, "World!"));
#endif
    
    EXPECT_EQ(sv.find("World"), 7);
}

TEST(StringViewR24Test, Constexpr)
{
    // StringView should work in constexpr context (C++17)
    constexpr StringView sv("Hello");
    
    static_assert(sv.size() == 5, "Size should be 5");
    static_assert(!sv.empty(), "Should not be empty");
    static_assert(sv[0] == 'H', "First char should be 'H'");
}
