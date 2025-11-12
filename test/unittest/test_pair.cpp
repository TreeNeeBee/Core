/**
 * @file        test_pair.cpp
 * @brief       Unit tests for Pair type alias
 * @date        2025-11-12
 */

#include <gtest/gtest.h>
#include "CTypedef.hpp"
#include "CString.hpp"

using namespace lap::core;

/**
 * @brief Test suite for Pair type alias
 */
class PairTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

/**
 * @brief Test basic Pair construction and access
 */
TEST_F(PairTest, BasicConstruction) {
    Pair<int, String> p1(42, "hello");
    
    EXPECT_EQ(p1.first, 42);
    EXPECT_EQ(p1.second, "hello");
}

/**
 * @brief Test Pair with make_pair
 */
TEST_F(PairTest, MakePair) {
    auto p = std::make_pair(100, String("world"));
    Pair<int, String> p2 = p;
    
    EXPECT_EQ(p2.first, 100);
    EXPECT_EQ(p2.second, "world");
}

/**
 * @brief Test Pair comparison
 */
TEST_F(PairTest, Comparison) {
    Pair<int, int> p1(1, 2);
    Pair<int, int> p2(1, 2);
    Pair<int, int> p3(1, 3);
    
    EXPECT_TRUE(p1 == p2);
    EXPECT_FALSE(p1 == p3);
    EXPECT_TRUE(p1 != p3);
    EXPECT_TRUE(p1 < p3);
}

/**
 * @brief Test Pair with complex types
 */
TEST_F(PairTest, ComplexTypes) {
    Pair<String, Vector<int>> p1("numbers", {1, 2, 3, 4, 5});
    
    EXPECT_EQ(p1.first, "numbers");
    EXPECT_EQ(p1.second.size(), 5u);
    EXPECT_EQ(p1.second[2], 3);
}

/**
 * @brief Test Pair used in Map
 */
TEST_F(PairTest, UsedInMap) {
    Map<int, String> myMap;
    
    myMap.insert(Pair<const int, String>(1, "one"));
    myMap.insert(std::make_pair(2, String("two")));
    myMap[3] = "three";
    
    EXPECT_EQ(myMap.size(), 3u);
    EXPECT_EQ(myMap[1], "one");
    EXPECT_EQ(myMap[2], "two");
    EXPECT_EQ(myMap[3], "three");
}

/**
 * @brief Test Pair assignment and copy
 */
TEST_F(PairTest, AssignmentAndCopy) {
    Pair<double, String> p1(3.14, "pi");
    Pair<double, String> p2;
    
    p2 = p1;
    
    EXPECT_DOUBLE_EQ(p2.first, 3.14);
    EXPECT_EQ(p2.second, "pi");
    
    // Modify p2, p1 should remain unchanged
    p2.first = 2.71;
    p2.second = "e";
    
    EXPECT_DOUBLE_EQ(p1.first, 3.14);
    EXPECT_EQ(p1.second, "pi");
}

/**
 * @brief Test Pair with move semantics
 */
TEST_F(PairTest, MoveSemantics) {
    Pair<String, String> p1("key", "value");
    Pair<String, String> p2 = std::move(p1);
    
    EXPECT_EQ(p2.first, "key");
    EXPECT_EQ(p2.second, "value");
    // p1 is in valid but unspecified state after move
}

/**
 * @brief Test Pair with const key (for Map usage)
 */
TEST_F(PairTest, ConstKey) {
    Pair<const int, String> p1(42, "answer");
    
    EXPECT_EQ(p1.first, 42);
    EXPECT_EQ(p1.second, "answer");
    
    // Can modify value but not key
    p1.second = "new value";
    EXPECT_EQ(p1.second, "new value");
    // p1.first = 43; // This would not compile (const key)
}
