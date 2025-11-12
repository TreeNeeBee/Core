#include "CMemory.hpp"
/**
 * @file        test_typedef_allocator.cpp
 * @brief       Unit tests for CTypedef container allocators
 * @date        2025-11-12
 * @details     Verifies that containers support custom allocators
 */

#include <gtest/gtest.h>
#include "CTypedef.hpp"
#include "CMemory.hpp"

using namespace lap::core;

/**
 * @brief Test suite for CTypedef allocators
 */
class TypedefAllocatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Nothing to set up
    }

    void TearDown() override {
        // Nothing to tear down
    }
};

/**
 * @brief Test Vector with default allocator
 */
TEST_F(TypedefAllocatorTest, VectorDefaultAllocator) {
    Vector<int> vec;
    vec.push_back(1);
    vec.push_back(2);
    vec.push_back(3);
    
    EXPECT_EQ(vec.size(), 3u);
    EXPECT_EQ(vec[0], 1);
    EXPECT_EQ(vec[1], 2);
    EXPECT_EQ(vec[2], 3);
}

/**
 * @brief Test Vector with custom StlMemoryAllocator
 */
TEST_F(TypedefAllocatorTest, VectorCustomAllocator) {
    Vector<int, ::lap::core::StlMemoryAllocator<int>> vec;
    vec.push_back(10);
    vec.push_back(20);
    vec.push_back(30);
    
    EXPECT_EQ(vec.size(), 3u);
    EXPECT_EQ(vec[0], 10);
    EXPECT_EQ(vec[1], 20);
    EXPECT_EQ(vec[2], 30);
}

/**
 * @brief Test Map with default allocator
 */
TEST_F(TypedefAllocatorTest, MapDefaultAllocator) {
    Map<int, String> map;
    map[1] = "one";
    map[2] = "two";
    map[3] = "three";
    
    EXPECT_EQ(map.size(), 3u);
    EXPECT_EQ(map[1], "one");
    EXPECT_EQ(map[2], "two");
    EXPECT_EQ(map[3], "three");
}

/**
 * @brief Test Map with custom StlMemoryAllocator
 */
TEST_F(TypedefAllocatorTest, MapCustomAllocator) {
    using CustomMap = Map<int, String, std::less<int>, ::lap::core::StlMemoryAllocator<std::pair<const int, String>>>;
    CustomMap map;
    
    map[10] = "ten";
    map[20] = "twenty";
    map[30] = "thirty";
    
    EXPECT_EQ(map.size(), 3u);
    EXPECT_EQ(map[10], "ten");
    EXPECT_EQ(map[20], "twenty");
    EXPECT_EQ(map[30], "thirty");
}

/**
 * @brief Test Set with default allocator
 */
TEST_F(TypedefAllocatorTest, SetDefaultAllocator) {
    Set<int> set;
    set.insert(1);
    set.insert(2);
    set.insert(3);
    set.insert(2); // Duplicate
    
    EXPECT_EQ(set.size(), 3u);
    EXPECT_TRUE(set.find(1) != set.end());
    EXPECT_TRUE(set.find(2) != set.end());
    EXPECT_TRUE(set.find(3) != set.end());
    EXPECT_FALSE(set.find(4) != set.end());
}

/**
 * @brief Test Set with custom StlMemoryAllocator
 */
TEST_F(TypedefAllocatorTest, SetCustomAllocator) {
    Set<String, std::less<String>, ::lap::core::StlMemoryAllocator<String>> set;
    
    set.insert("apple");
    set.insert("banana");
    set.insert("cherry");
    set.insert("banana"); // Duplicate
    
    EXPECT_EQ(set.size(), 3u);
    EXPECT_TRUE(set.find("apple") != set.end());
    EXPECT_TRUE(set.find("banana") != set.end());
    EXPECT_TRUE(set.find("cherry") != set.end());
    EXPECT_FALSE(set.find("date") != set.end());
}

/**
 * @brief Test UnorderedMap with default allocator
 */
TEST_F(TypedefAllocatorTest, UnorderedMapDefaultAllocator) {
    UnorderedMap<String, int> map;
    map["one"] = 1;
    map["two"] = 2;
    map["three"] = 3;
    
    EXPECT_EQ(map.size(), 3u);
    EXPECT_EQ(map["one"], 1);
    EXPECT_EQ(map["two"], 2);
    EXPECT_EQ(map["three"], 3);
}

/**
 * @brief Test UnorderedMap with custom StlMemoryAllocator
 */
TEST_F(TypedefAllocatorTest, UnorderedMapCustomAllocator) {
    using CustomUnorderedMap = UnorderedMap<
        String, 
        int, 
        std::hash<String>, 
        std::equal_to<String>, 
    ::lap::core::StlMemoryAllocator<Pair<const String, int>>
    >;
    
    CustomUnorderedMap map;
    map["ten"] = 10;
    map["twenty"] = 20;
    map["thirty"] = 30;
    
    EXPECT_EQ(map.size(), 3u);
    EXPECT_EQ(map["ten"], 10);
    EXPECT_EQ(map["twenty"], 20);
    EXPECT_EQ(map["thirty"], 30);
}

/**
 * @brief Test nested containers with allocators
 */
TEST_F(TypedefAllocatorTest, NestedContainers) {
    // Vector of Vectors with custom allocator
    using InnerVec = Vector<int, ::lap::core::StlMemoryAllocator<int>>;
    using OuterVec = Vector<InnerVec, ::lap::core::StlMemoryAllocator<InnerVec>>;
    
    OuterVec vec;
    vec.push_back(InnerVec{1, 2, 3});
    vec.push_back(InnerVec{4, 5, 6});
    
    EXPECT_EQ(vec.size(), 2u);
    EXPECT_EQ(vec[0].size(), 3u);
    EXPECT_EQ(vec[1].size(), 3u);
    EXPECT_EQ(vec[0][0], 1);
    EXPECT_EQ(vec[1][2], 6);
}

/**
 * @brief Test Map with Vector values using allocators
 */
TEST_F(TypedefAllocatorTest, MapOfVectors) {
    using ValueVec = Vector<int, ::lap::core::StlMemoryAllocator<int>>;
    using CustomMap = Map<
        String, 
        ValueVec, 
        std::less<String>, 
    ::lap::core::StlMemoryAllocator<Pair<const String, ValueVec>>
    >;
    
    CustomMap map;
    map["first"] = ValueVec{1, 2, 3};
    map["second"] = ValueVec{4, 5, 6};
    
    EXPECT_EQ(map.size(), 2u);
    EXPECT_EQ(map["first"].size(), 3u);
    EXPECT_EQ(map["second"].size(), 3u);
    EXPECT_EQ(map["first"][1], 2);
    EXPECT_EQ(map["second"][2], 6);
}

/**
 * @brief Test allocator compatibility with algorithms
 */
TEST_F(TypedefAllocatorTest, AlgorithmCompatibility) {
    Vector<int, ::lap::core::StlMemoryAllocator<int>> vec{5, 2, 8, 1, 9, 3};
    
    // Sort
    std::sort(vec.begin(), vec.end());
    
    EXPECT_EQ(vec[0], 1);
    EXPECT_EQ(vec[1], 2);
    EXPECT_EQ(vec[2], 3);
    EXPECT_EQ(vec[5], 9);
    
    // Find
    auto it = std::find(vec.begin(), vec.end(), 8);
    EXPECT_NE(it, vec.end());
    EXPECT_EQ(*it, 8);
    
    // Count
    int count = std::count_if(vec.begin(), vec.end(), [](int x) { return x > 5; });
    EXPECT_EQ(count, 2); // 8 and 9
}
