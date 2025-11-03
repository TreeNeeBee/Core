/**
 * @file        test_memory_allocator.cpp
 * @brief       Unit tests for CMemoryAllocator
 * @date        2025-11-01
 */

#include <gtest/gtest.h>
#include <vector>
#include <map>
#include <list>
#include <string>
#include "CMemoryAllocator.hpp"
#include "CMemory.hpp"

using namespace lap::core;

class CMemoryAllocatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Memory subsystem is initialized via MemManager singleton automatically
    }

    void TearDown() override {
        // Cleanup after each test
    }
};

// Test basic allocation and deallocation
TEST_F(CMemoryAllocatorTest, BasicAllocation) {
    MemoryAllocator<int> alloc;
    
    // Allocate single int
    int* p = alloc.allocate(1);
    ASSERT_NE(p, nullptr);
    
    // Construct and use
    alloc.construct(p, 42);
    EXPECT_EQ(*p, 42);
    
    // Destroy and deallocate
    alloc.destroy(p);
    alloc.deallocate(p, 1);
}

// Test allocating array
TEST_F(CMemoryAllocatorTest, ArrayAllocation) {
    MemoryAllocator<int> alloc;
    
    const size_t count = 100;
    int* arr = alloc.allocate(count);
    ASSERT_NE(arr, nullptr);
    
    // Initialize array
    for (size_t i = 0; i < count; ++i) {
        alloc.construct(&arr[i], static_cast<int>(i));
    }
    
    // Verify values
    for (size_t i = 0; i < count; ++i) {
        EXPECT_EQ(arr[i], static_cast<int>(i));
    }
    
    // Cleanup
    for (size_t i = 0; i < count; ++i) {
        alloc.destroy(&arr[i]);
    }
    alloc.deallocate(arr, count);
}

// Test with std::vector
TEST_F(CMemoryAllocatorTest, VectorUsage) {
    std::vector<int, MemoryAllocator<int>> vec;
    
    // Add elements
    for (int i = 0; i < 100; ++i) {
        vec.push_back(i);
    }
    
    EXPECT_EQ(vec.size(), 100);
    
    // Verify values
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(vec[i], i);
    }
    
    // Test clear and reuse
    vec.clear();
    EXPECT_EQ(vec.size(), 0);
    
    vec.push_back(999);
    EXPECT_EQ(vec[0], 999);
}

// Test with std::map
TEST_F(CMemoryAllocatorTest, MapUsage) {
    using MyMap = std::map<int, std::string, 
                           std::less<int>,
                           MemoryAllocator<std::pair<const int, std::string>>>;
    
    MyMap m;
    
    // Insert elements
    m[1] = "one";
    m[2] = "two";
    m[3] = "three";
    
    EXPECT_EQ(m.size(), 3);
    EXPECT_EQ(m[1], "one");
    EXPECT_EQ(m[2], "two");
    EXPECT_EQ(m[3], "three");
    
    // Test find
    auto it = m.find(2);
    ASSERT_NE(it, m.end());
    EXPECT_EQ(it->second, "two");
}

// Test with std::list
TEST_F(CMemoryAllocatorTest, ListUsage) {
    std::list<int, MemoryAllocator<int>> lst;
    
    // Add elements
    for (int i = 0; i < 50; ++i) {
        lst.push_back(i);
    }
    
    EXPECT_EQ(lst.size(), 50);
    
    // Verify order
    int expected = 0;
    for (auto val : lst) {
        EXPECT_EQ(val, expected++);
    }
}

// Test rebind mechanism
TEST_F(CMemoryAllocatorTest, RebindAllocator) {
    MemoryAllocator<int> int_alloc;
    
    // Rebind to double
    using DoubleAlloc = typename MemoryAllocator<int>::rebind<double>::other;
    DoubleAlloc double_alloc(int_alloc);
    
    // Allocate double
    double* d = double_alloc.allocate(1);
    ASSERT_NE(d, nullptr);
    
    double_alloc.construct(d, 3.14159);
    EXPECT_DOUBLE_EQ(*d, 3.14159);
    
    double_alloc.destroy(d);
    double_alloc.deallocate(d, 1);
}

// Test construct with multiple arguments
TEST_F(CMemoryAllocatorTest, ConstructWithArgs) {
    struct Point {
        int x, y;
        Point(int x_, int y_) : x(x_), y(y_) {}
    };
    
    MemoryAllocator<Point> alloc;
    Point* p = alloc.allocate(1);
    ASSERT_NE(p, nullptr);
    
    // Construct with two arguments
    alloc.construct(p, 10, 20);
    EXPECT_EQ(p->x, 10);
    EXPECT_EQ(p->y, 20);
    
    alloc.destroy(p);
    alloc.deallocate(p, 1);
}

// Test max_size
TEST_F(CMemoryAllocatorTest, MaxSize) {
    MemoryAllocator<int> int_alloc;
    MemoryAllocator<char> char_alloc;
    
    EXPECT_GT(int_alloc.max_size(), 0);
    EXPECT_GT(char_alloc.max_size(), int_alloc.max_size());
}

// Test equality operators
TEST_F(CMemoryAllocatorTest, EqualityOperators) {
    MemoryAllocator<int> alloc1;
    MemoryAllocator<int> alloc2;
    MemoryAllocator<double> alloc3;
    
    // Stateless allocators are always equal
    EXPECT_TRUE(alloc1 == alloc2);
    EXPECT_FALSE(alloc1 != alloc2);
    EXPECT_TRUE(alloc1 == alloc3);
}

// Test exception on allocation failure
TEST_F(CMemoryAllocatorTest, AllocationFailure) {
    MemoryAllocator<int> alloc;
    
    // Test 1: Allocating more than max_size() should throw (overflow check)
    // This is guaranteed by the allocator's overflow check
    size_t over_max = alloc.max_size() + 1;
    EXPECT_THROW({
        [[maybe_unused]] int* p = alloc.allocate(over_max);
    }, std::bad_alloc);
    
    // Test 2: Verify max_size() calculation is reasonable
    // For int on 64-bit systems: SIZE_MAX / sizeof(int) = SIZE_MAX / 4
    EXPECT_GT(alloc.max_size(), 0);
    EXPECT_LE(alloc.max_size(), std::numeric_limits<size_t>::max() / sizeof(int));
}

// Test with complex objects (string)
TEST_F(CMemoryAllocatorTest, ComplexObjects) {
    std::vector<std::string, MemoryAllocator<std::string>> vec;
    
    vec.push_back("Hello");
    vec.push_back("World");
    vec.push_back("LightAP");
    
    EXPECT_EQ(vec.size(), 3);
    EXPECT_EQ(vec[0], "Hello");
    EXPECT_EQ(vec[1], "World");
    EXPECT_EQ(vec[2], "LightAP");
}

// Test nested containers
TEST_F(CMemoryAllocatorTest, NestedContainers) {
    using InnerVec = std::vector<int, MemoryAllocator<int>>;
    using OuterVec = std::vector<InnerVec, MemoryAllocator<InnerVec>>;
    
    OuterVec outer;
    
    for (int i = 0; i < 3; ++i) {
        InnerVec inner;
        for (int j = 0; j < 5; ++j) {
            inner.push_back(i * 10 + j);
        }
        outer.push_back(inner);
    }
    
    EXPECT_EQ(outer.size(), 3);
    EXPECT_EQ(outer[0].size(), 5);
    EXPECT_EQ(outer[1][2], 12);
    EXPECT_EQ(outer[2][4], 24);
}

// Test move semantics
TEST_F(CMemoryAllocatorTest, MoveSemantics) {
    std::vector<int, MemoryAllocator<int>> vec1;
    vec1.push_back(1);
    vec1.push_back(2);
    vec1.push_back(3);
    
    // Move construct
    std::vector<int, MemoryAllocator<int>> vec2(std::move(vec1));
    EXPECT_EQ(vec2.size(), 3);
    EXPECT_EQ(vec2[0], 1);
    EXPECT_EQ(vec2[1], 2);
    EXPECT_EQ(vec2[2], 3);
    
    // vec1 should be empty after move
    EXPECT_TRUE(vec1.empty() || vec1.size() == 0);
}

// Performance test: allocate and deallocate many objects
TEST_F(CMemoryAllocatorTest, PerformanceTest) {
    MemoryAllocator<int> alloc;
    const size_t iterations = 1000;
    
    for (size_t i = 0; i < iterations; ++i) {
        int* p = alloc.allocate(10);
        ASSERT_NE(p, nullptr);
        
        for (size_t j = 0; j < 10; ++j) {
            alloc.construct(&p[j], static_cast<int>(j));
        }
        
        for (size_t j = 0; j < 10; ++j) {
            alloc.destroy(&p[j]);
        }
        
        alloc.deallocate(p, 10);
    }
}
