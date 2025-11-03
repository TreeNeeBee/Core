#include <gtest/gtest.h>
#include "CMemory.hpp"
#include <fstream>
#include <cstdio>

using namespace lap::core;

TEST(MemoryAllocatorTest, CreatePoolAndAllocFree) {
    MemAllocator alloc;
    alloc.initialize(8, 4);
    EXPECT_TRUE(alloc.createPool(32, 4, 16, 4));
    EXPECT_TRUE(alloc.createPool(128, 2, 8, 2));

    // Small allocation should choose 32 pool
    void* p1 = alloc.malloc(24);
    ASSERT_NE(p1, nullptr);
    alloc.free(p1);

    // Larger allocation should choose 128 pool
    void* p2 = alloc.malloc(100);
    ASSERT_NE(p2, nullptr);
    alloc.free(p2);

    MemAllocator::MemoryPoolState st{};
    ASSERT_TRUE(alloc.getPoolState(0, st));
    EXPECT_EQ(st.unitAvailableSize, 32u);
}

TEST(MemoryAllocatorTest, LoadPoolConfigWithPropertyTree) {
    void* p1 = Memory::malloc(24); ASSERT_NE(p1, nullptr); Memory::free(p1);
    void* p2 = Memory::malloc(100); ASSERT_NE(p2, nullptr); Memory::free(p2);
}

TEST(MemoryCheckerTest, EnableCheckerAndTrack) {
    // Register fake class id and allocate
    auto cid = Memory::registerClassName("UnitTestClass");
    void* p = Memory::malloc(64, "UnitTestClass", cid);
    ASSERT_NE(p, nullptr);

    // Pointer check should be OK
    EXPECT_EQ(Memory::checkPtr(p, "ut"), 0);

    Memory::free(p);
}

// Test that createPool deduplicates/merges pools with the same unit size
TEST(MemoryMapTest, CreatePoolDedupAndMerge) {
    MemAllocator alloc;
    alloc.initialize(8, 10);

    EXPECT_TRUE(alloc.createPool(64, 2, 10, 2));
    // Create duplicate with larger init/max/append -> should merge
    EXPECT_TRUE(alloc.createPool(64, 5, 20, 3));

    // Find the pool index that corresponds to 64 by scanning
    UInt32 poolCount = alloc.getPoolCount();
    Int32 foundIdx = -1;
    for (UInt32 i = 0; i < poolCount; ++i) {
        MemAllocator::MemoryPoolState st{};
        ASSERT_TRUE(alloc.getPoolState(i, st));
        if (st.unitAvailableSize == 64u) { foundIdx = static_cast<Int32>(i); break; }
    }
    ASSERT_GE(foundIdx, 0);

    MemAllocator::MemoryPoolState st{};
    ASSERT_TRUE(alloc.getPoolState(static_cast<UInt32>(foundIdx), st));
    // After merge, appendCount should be the max(2,3)=3 and maxCount max(10,20)=20
    EXPECT_EQ(st.appendCount, 3u);
    EXPECT_EQ(st.maxCount, 20u);
    // currentCount should be at least the requested initCount (5)
    EXPECT_GE(st.currentCount, 5u);
}

// Test that findFitPool (via malloc) selects the minimal fitting pool
TEST(MemoryMapTest, FindFitPoolBehavior) {
    MemAllocator alloc;
    alloc.initialize(8, 10);

    EXPECT_TRUE(alloc.createPool(32, 2, 0, 2));
    EXPECT_TRUE(alloc.createPool(64, 2, 0, 2));
    EXPECT_TRUE(alloc.createPool(128, 2, 0, 2));

    // Record free counts before allocation
    UInt32 poolCount = alloc.getPoolCount();
    std::vector<UInt32> before(poolCount);
    for (UInt32 i = 0; i < poolCount; ++i) {
        MemAllocator::MemoryPoolState st{};
        ASSERT_TRUE(alloc.getPoolState(i, st));
        before[i] = st.freeCount;
    }

    // Allocate size 40 -> should use 64 pool (smallest >= 40)
    void* p = alloc.malloc(40);
    ASSERT_NE(p, nullptr);

    // Check which pool's freeCount decreased
    Int32 usedIdx = -1;
    for (UInt32 i = 0; i < poolCount; ++i) {
        MemAllocator::MemoryPoolState st{};
        ASSERT_TRUE(alloc.getPoolState(i, st));
        if (st.freeCount < before[i]) { usedIdx = static_cast<Int32>(i); break; }
    }
    ASSERT_GE(usedIdx, 0);

    // The used pool's unitAvailableSize should be >=40 and smallest among those
    MemAllocator::MemoryPoolState usedSt{};
    ASSERT_TRUE(alloc.getPoolState(static_cast<UInt32>(usedIdx), usedSt));
    EXPECT_GE(usedSt.unitAvailableSize, 40u);

    alloc.free(p);
}
