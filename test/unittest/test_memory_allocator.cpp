/**
 * @file        test_memory_allocator.cpp
 * @brief       Comprehensive unit tests for StlMemoryAllocator
 * @date        2025-11-12
 * @details     Tests for STL-compatible allocator including container usage,
 *              performance benchmarks, and edge cases
 */

#include <gtest/gtest.h>
#include <vector>
#include <map>
#include <list>
#include <set>
#include <deque>
#include <string>
#include <algorithm>
#include <chrono>
#include "CMemory.hpp"

using namespace lap::core;

// ============================================================================
// Test Fixture
// ============================================================================

class StlMemoryAllocatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Memory system is auto-initialized
    }

    void TearDown() override {
        // Cleanup is automatic
    }
};

// ============================================================================
// Basic Allocator Tests
// ============================================================================

TEST_F(StlMemoryAllocatorTest, BasicAllocation) {
    StlMemoryAllocator<int> alloc;
    
    // Allocate single int
    int* p = alloc.allocate(1);
    ASSERT_NE(p, nullptr);
    
    // Use placement new (allocator doesn't call constructor)
    new (p) int(42);
    EXPECT_EQ(*p, 42);
    
    // Deallocate (no need to call destructor for trivial types)
    alloc.deallocate(p, 1);
}

TEST_F(StlMemoryAllocatorTest, ArrayAllocation) {
    StlMemoryAllocator<double> alloc;
    
    const size_t count = 100;
    double* arr = alloc.allocate(count);
    ASSERT_NE(arr, nullptr);
    
    // Initialize array
    for (size_t i = 0; i < count; ++i) {
        new (&arr[i]) double(static_cast<double>(i) * 0.5);
    }
    
    // Verify values
    for (size_t i = 0; i < count; ++i) {
        EXPECT_DOUBLE_EQ(arr[i], static_cast<double>(i) * 0.5);
    }
    
    // Deallocate (no need to call destructor for trivial types)
    alloc.deallocate(arr, count);
}

TEST_F(StlMemoryAllocatorTest, AllocatorRebind) {
    StlMemoryAllocator<int> int_alloc;
    
    // Rebind to double
    using DoubleAlloc = typename StlMemoryAllocator<int>::rebind<double>::other;
    DoubleAlloc double_alloc(int_alloc);
    
    // Allocate double
    double* d = double_alloc.allocate(1);
    ASSERT_NE(d, nullptr);
    
    new (d) double(3.14159);
    EXPECT_DOUBLE_EQ(*d, 3.14159);
    
    // Deallocate (no need to call destructor for trivial types)
    double_alloc.deallocate(d, 1);
}

TEST_F(StlMemoryAllocatorTest, AllocatorEquality) {
    StlMemoryAllocator<int> alloc1;
    StlMemoryAllocator<int> alloc2;
    StlMemoryAllocator<double> alloc3;
    
    // All stateless allocators are equal
    EXPECT_TRUE(alloc1 == alloc2);
    EXPECT_FALSE(alloc1 != alloc2);
    EXPECT_TRUE(alloc1 == alloc3);  // Even different types
}

TEST_F(StlMemoryAllocatorTest, MaxSize) {
    StlMemoryAllocator<int> alloc;
    
    size_t max = alloc.max_size();
    EXPECT_GT(max, 0);
    EXPECT_LE(max, static_cast<size_t>(-1) / sizeof(int));
}

// ============================================================================
// STL Container Tests
// ============================================================================

TEST_F(StlMemoryAllocatorTest, VectorUsage) {
    Vector<int, StlMemoryAllocator<int>> vec;
    
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

TEST_F(StlMemoryAllocatorTest, MapUsage) {
    using MyMap = Map<int, String, 
                      std::less<int>,
                      StlMemoryAllocator<Pair<const int, String>>>;
    
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

TEST_F(StlMemoryAllocatorTest, ListUsage) {
    std::list<int, StlMemoryAllocator<int>> lst;
    
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

TEST_F(StlMemoryAllocatorTest, SetUsage) {
    std::set<int, std::less<int>, StlMemoryAllocator<int>> s;
    
    // Insert elements (unsorted)
    s.insert(5);
    s.insert(2);
    s.insert(8);
    s.insert(1);
    s.insert(9);
    
    EXPECT_EQ(s.size(), 5);
    
    // Set should be sorted
    std::vector<int> values(s.begin(), s.end());
    EXPECT_TRUE(std::is_sorted(values.begin(), values.end()));
}

TEST_F(StlMemoryAllocatorTest, DequeUsage) {
    std::deque<int, StlMemoryAllocator<int>> dq;
    
    // Add to front and back
    for (int i = 0; i < 25; ++i) {
        dq.push_back(i);
        dq.push_front(-i - 1);
    }
    
    EXPECT_EQ(dq.size(), 50);
    
    // Verify front and back
    EXPECT_EQ(dq.front(), -25);
    EXPECT_EQ(dq.back(), 24);
}

TEST_F(StlMemoryAllocatorTest, NestedContainers) {
    using InnerVec = Vector<int, StlMemoryAllocator<int>>;
    using OuterVec = Vector<InnerVec, StlMemoryAllocator<InnerVec>>;
    
    OuterVec matrix;
    
    // Create 10x10 matrix
    for (int i = 0; i < 10; ++i) {
        InnerVec row;
        for (int j = 0; j < 10; ++j) {
            row.push_back(i * 10 + j);
        }
        matrix.push_back(row);
    }
    
    EXPECT_EQ(matrix.size(), 10);
    EXPECT_EQ(matrix[0].size(), 10);
    EXPECT_EQ(matrix[5][7], 57);
}

// ============================================================================
// Complex Type Tests
// ============================================================================

struct ComplexType {
    int id;
    String name;
    Vector<double, StlMemoryAllocator<double>> data;
    
    ComplexType(int i, const String& n) : id(i), name(n) {}
};

TEST_F(StlMemoryAllocatorTest, ComplexTypeAllocation) {
    Vector<ComplexType, StlMemoryAllocator<ComplexType>> vec;
    
    vec.emplace_back(1, "First");
    vec.emplace_back(2, "Second");
    vec.emplace_back(3, "Third");
    
    EXPECT_EQ(vec.size(), 3);
    EXPECT_EQ(vec[1].id, 2);
    EXPECT_EQ(vec[1].name, "Second");
}

// ============================================================================
// Performance Benchmarks
// ============================================================================

TEST_F(StlMemoryAllocatorTest, PerformanceVsStdAllocator) {
    constexpr int kIterations = 10000;
    
    // Test with StlMemoryAllocator
    auto start1 = std::chrono::high_resolution_clock::now();
    {
        Vector<int, StlMemoryAllocator<int>> vec;
        for (int i = 0; i < kIterations; ++i) {
            vec.push_back(i);
        }
    }
    auto end1 = std::chrono::high_resolution_clock::now();
    auto duration1 = std::chrono::duration_cast<std::chrono::microseconds>(end1 - start1);
    
    // Test with std::allocator
    auto start2 = std::chrono::high_resolution_clock::now();
    {
        std::vector<int> vec;
        for (int i = 0; i < kIterations; ++i) {
            vec.push_back(i);
        }
    }
    auto end2 = std::chrono::high_resolution_clock::now();
    auto duration2 = std::chrono::duration_cast<std::chrono::microseconds>(end2 - start2);
    
    std::cout << "StlMemoryAllocator: " << duration1.count() << " µs" << std::endl;
    std::cout << "std::allocator:     " << duration2.count() << " µs" << std::endl;
    
    // Performance should be reasonable (not 10x slower)
    EXPECT_LT(duration1.count(), duration2.count() * 10);
}

TEST_F(StlMemoryAllocatorTest, SmallObjectAllocationSpeed) {
    constexpr int kAllocCount = 10000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    Vector<void*, StlMemoryAllocator<void*>> pointers;
    StlMemoryAllocator<char> alloc;
    
    for (int i = 0; i < kAllocCount; ++i) {
        char* p = alloc.allocate(32);  // Small object
        pointers.push_back(p);
    }
    
    for (void* p : pointers) {
        alloc.deallocate(static_cast<char*>(p), 32);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "Small object alloc/dealloc: " << duration.count() << " ms for " 
              << kAllocCount << " iterations" << std::endl;
    
    // Should complete in reasonable time
    EXPECT_LT(duration.count(), 1000);  // Less than 1 second
}

// ============================================================================
// Edge Cases and Error Handling
// ============================================================================

TEST_F(StlMemoryAllocatorTest, ZeroSizeAllocation) {
    StlMemoryAllocator<int> alloc;
    
    // Zero-size allocation (implementation-defined)
    int* p = alloc.allocate(0);
    if (p != nullptr) {
        alloc.deallocate(p, 0);
    }
    // Test passes if no crash
}

TEST_F(StlMemoryAllocatorTest, OversizeAllocation) {
    StlMemoryAllocator<int> alloc;
    
    // Try to allocate more than max_size()
    EXPECT_THROW({
        [[maybe_unused]] int* p = alloc.allocate(alloc.max_size() + 1);
    }, std::bad_alloc);
}

TEST_F(StlMemoryAllocatorTest, AllocateDeallocateCycle) {
    StlMemoryAllocator<int> alloc;
    
    // Rapid allocation/deallocation cycles
    for (int cycle = 0; cycle < 1000; ++cycle) {
        int* p = alloc.allocate(10);
        ASSERT_NE(p, nullptr);
        alloc.deallocate(p, 10);
    }
}

TEST_F(StlMemoryAllocatorTest, MoveSemantics) {
    Vector<String, StlMemoryAllocator<String>> vec1;
    vec1.push_back("Hello");
    vec1.push_back("World");
    
    // Move constructor
    Vector<String, StlMemoryAllocator<String>> vec2(std::move(vec1));
    
    EXPECT_EQ(vec2.size(), 2);
    EXPECT_EQ(vec2[0], "Hello");
    EXPECT_EQ(vec2[1], "World");
}

// ============================================================================
// Helper Function Tests
// ============================================================================

TEST_F(StlMemoryAllocatorTest, MakeVectorHelper) {
    auto vec = MakeVectorWithMemoryAllocator<int>();
    
    vec.push_back(1);
    vec.push_back(2);
    vec.push_back(3);
    
    EXPECT_EQ(vec.size(), 3);
    EXPECT_EQ(vec[0], 1);
    EXPECT_EQ(vec[1], 2);
    EXPECT_EQ(vec[2], 3);
}

// CMemoryAllocatorTest Fixture for additional allocator tests
class CMemoryAllocatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialization if needed
    }
    
    void TearDown() override {
        // Cleanup if needed
    }
};

// Test construct with multiple arguments
TEST_F(CMemoryAllocatorTest, ConstructWithArgs) {
    struct Point {
        int x, y;
        Point(int x_, int y_) : x(x_), y(y_) {}
    };
    
    ::lap::core::StlMemoryAllocator<Point> alloc;
    Point* p = alloc.allocate(1);
    ASSERT_NE(p, nullptr);
    
    // Construct with two arguments using placement new
    ::new (static_cast<void*>(p)) Point(10, 20);
    EXPECT_EQ(p->x, 10);
    EXPECT_EQ(p->y, 20);
    
    // Manually destroy the object
    p->~Point();
    alloc.deallocate(p, 1);
}

// Test max_size
TEST_F(CMemoryAllocatorTest, MaxSize) {
    ::lap::core::StlMemoryAllocator<int> int_alloc;
    ::lap::core::StlMemoryAllocator<char> char_alloc;
    
    EXPECT_GT(int_alloc.max_size(), 0);
    EXPECT_GT(char_alloc.max_size(), int_alloc.max_size());
}

// Test equality operators
TEST_F(CMemoryAllocatorTest, EqualityOperators) {
    ::lap::core::StlMemoryAllocator<int> alloc1;
    ::lap::core::StlMemoryAllocator<int> alloc2;
    ::lap::core::StlMemoryAllocator<double> alloc3;
    
    // Stateless allocators are always equal
    EXPECT_TRUE(alloc1 == alloc2);
    EXPECT_FALSE(alloc1 != alloc2);
    EXPECT_TRUE(alloc1 == alloc3);
}

// Test exception on allocation failure
TEST_F(CMemoryAllocatorTest, AllocationFailure) {
    ::lap::core::StlMemoryAllocator<int> alloc;
    
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
    std::vector<std::string, ::lap::core::StlMemoryAllocator<std::string>> vec;
    
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
    using InnerVec = std::vector<int, ::lap::core::StlMemoryAllocator<int>>;
    using OuterVec = std::vector<InnerVec, ::lap::core::StlMemoryAllocator<InnerVec>>;
    
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
    std::vector<int, ::lap::core::StlMemoryAllocator<int>> vec1;
    vec1.push_back(1);
    vec1.push_back(2);
    vec1.push_back(3);
    
    // Move construct
    std::vector<int, ::lap::core::StlMemoryAllocator<int>> vec2(std::move(vec1));
    EXPECT_EQ(vec2.size(), 3);
    EXPECT_EQ(vec2[0], 1);
    EXPECT_EQ(vec2[1], 2);
    EXPECT_EQ(vec2[2], 3);
    
    // vec1 should be empty after move
    EXPECT_TRUE(vec1.empty() || vec1.size() == 0);
}

// Performance test: allocate and deallocate many objects
TEST_F(CMemoryAllocatorTest, PerformanceTest) {
    ::lap::core::StlMemoryAllocator<int> alloc;
    const size_t iterations = 1000;
    
    for (size_t i = 0; i < iterations; ++i) {
        int* p = alloc.allocate(10);
        ASSERT_NE(p, nullptr);
        
        for (size_t j = 0; j < 10; ++j) {
            ::new (static_cast<void*>(&p[j])) int(static_cast<int>(j));
        }
        
        // No need to destroy trivial types like int
        alloc.deallocate(p, 10);
    }
}
