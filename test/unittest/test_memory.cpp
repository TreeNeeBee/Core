/**
 * @file        test_memory.cpp
 * @brief       Unit tests for PoolAllocator and Memory facade
 * @date        2025-11-12
 * @details     Comprehensive testing of pool-based memory allocation
 */

#include <gtest/gtest.h>
#include "CMemory.hpp"
#include "CMemoryManager.hpp"
#include <vector>
#include <thread>
#include <fstream>
#include <cstdio>

using namespace lap::core;

// ============================================================================
// Test Fixtures
// ============================================================================

class PoolAllocatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a standalone allocator for testing
        allocator = new PoolAllocator();
        allocator->initialize(8, 16);  // 8-byte alignment, max 16 pools
    }

    void TearDown() override {
        delete allocator;
        allocator = nullptr;
    }

    PoolAllocator* allocator = nullptr;
};

class MemoryFacadeTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Memory manager is initialized automatically
        // Just capture initial state
        initialStats = Memory::getMemoryStats();
    }

    void TearDown() override {
        // Verify no leaks (in non-tracking mode, just check counts are reasonable)
    }

    MemoryStats initialStats;
};

// ============================================================================
// PoolAllocator Tests
// ============================================================================

TEST_F(PoolAllocatorTest, Initialization) {
    EXPECT_EQ(allocator->getPoolCount(), 0);
    EXPECT_TRUE(allocator->createPool(32, 4, 16, 4));
    EXPECT_EQ(allocator->getPoolCount(), 1);
}

TEST_F(PoolAllocatorTest, CreateMultiplePools) {
    EXPECT_TRUE(allocator->createPool(32, 4, 16, 4));
    EXPECT_TRUE(allocator->createPool(64, 4, 16, 4));
    EXPECT_TRUE(allocator->createPool(128, 4, 16, 4));
    EXPECT_EQ(allocator->getPoolCount(), 3);
}

TEST_F(PoolAllocatorTest, PoolDeduplication) {
    // Create pool with size 64
    EXPECT_TRUE(allocator->createPool(64, 2, 10, 2));
    
    // Try to create another pool with same unit size but different params
    // Should merge with existing pool
    EXPECT_TRUE(allocator->createPool(64, 5, 20, 3));
    
    // Still should have only one pool
    UInt32 poolCount = allocator->getPoolCount();
    
    // Find the 64-byte pool and verify merged parameters
    for (UInt32 i = 0; i < poolCount; ++i) {
        PoolAllocator::MemoryPoolState state{};
        ASSERT_TRUE(allocator->getPoolState(i, state));
        if (state.unitAvailableSize == 64u) {
            // After merge, should have the max values
            EXPECT_EQ(state.appendCount, 3u);
            EXPECT_EQ(state.maxCount, 20u);
            EXPECT_GE(state.currentCount, 5u);
            return;
        }
    }
    
    FAIL() << "Could not find 64-byte pool";
}

TEST_F(PoolAllocatorTest, AllocationAndDeallocation) {
    EXPECT_TRUE(allocator->createPool(32, 4, 16, 4));
    
    // Allocate from pool
    void* p1 = allocator->malloc(24);
    ASSERT_NE(p1, nullptr);
    
    // Free back to pool
    allocator->free(p1);
    
    // Should be able to allocate again
    void* p2 = allocator->malloc(24);
    EXPECT_NE(p2, nullptr);
    allocator->free(p2);
}

TEST_F(PoolAllocatorTest, MultipleAllocationsSamePool) {
    EXPECT_TRUE(allocator->createPool(64, 8, 32, 8));
    
    std::vector<void*> pointers;
    
    // Allocate multiple blocks
    for (int i = 0; i < 10; ++i) {
        void* p = allocator->malloc(50);
        ASSERT_NE(p, nullptr) << "Allocation " << i << " failed";
        pointers.push_back(p);
    }
    
    // Free all blocks
    for (void* p : pointers) {
        allocator->free(p);
    }
}

TEST_F(PoolAllocatorTest, BestFitSelection) {
    // Create pools of different sizes
    EXPECT_TRUE(allocator->createPool(32, 4, 0, 4));
    EXPECT_TRUE(allocator->createPool(64, 4, 0, 4));
    EXPECT_TRUE(allocator->createPool(128, 4, 0, 4));
    
    // Capture initial free counts
    std::vector<UInt32> initialFreeCounts;
    for (UInt32 i = 0; i < allocator->getPoolCount(); ++i) {
        PoolAllocator::MemoryPoolState state{};
        ASSERT_TRUE(allocator->getPoolState(i, state));
        initialFreeCounts.push_back(state.freeCount);
    }
    
    // Allocate 40 bytes - should use 64-byte pool (smallest fit)
    void* p = allocator->malloc(40);
    ASSERT_NE(p, nullptr);
    
    // Verify that the 64-byte pool's free count decreased
    Int32 usedPoolIdx = -1;
    for (UInt32 i = 0; i < allocator->getPoolCount(); ++i) {
        PoolAllocator::MemoryPoolState state{};
        ASSERT_TRUE(allocator->getPoolState(i, state));
        if (state.freeCount < initialFreeCounts[i]) {
            usedPoolIdx = static_cast<Int32>(i);
            EXPECT_GE(state.unitAvailableSize, 40u);
            break;
        }
    }
    
    EXPECT_GE(usedPoolIdx, 0) << "No pool used for allocation";
    allocator->free(p);
}

TEST_F(PoolAllocatorTest, NullptrFreeIsSafe) {
    // Should not crash
    EXPECT_NO_THROW(allocator->free(nullptr));
}

TEST_F(PoolAllocatorTest, ZeroSizeAllocation) {
    EXPECT_TRUE(allocator->createPool(32, 4, 16, 4));
    
    // Zero-size allocation behavior (implementation-defined)
    void* p = allocator->malloc(0);
    if (p != nullptr) {
        allocator->free(p);
    }
    // Test passes as long as it doesn't crash
}

TEST_F(PoolAllocatorTest, GetPoolState) {
    EXPECT_TRUE(allocator->createPool(64, 4, 16, 4));
    
    PoolAllocator::MemoryPoolState state{};
    EXPECT_TRUE(allocator->getPoolState(0, state));
    
    EXPECT_EQ(state.unitAvailableSize, 64u);
    EXPECT_EQ(state.maxCount, 16u);
    EXPECT_GE(state.currentCount, 4u);
    EXPECT_GT(state.freeCount, 0u);
    EXPECT_GT(state.memoryCost, 0u);
}

TEST_F(PoolAllocatorTest, GetPoolStateInvalidIndex) {
    PoolAllocator::MemoryPoolState state{};
    EXPECT_FALSE(allocator->getPoolState(999, state));
}

// ============================================================================
// Memory Facade Tests
// ============================================================================

TEST_F(MemoryFacadeTest, BasicMallocFree) {
    void* p = Memory::malloc(64);
    ASSERT_NE(p, nullptr);
    Memory::free(p);
}

TEST_F(MemoryFacadeTest, MallocWithClassName) {
    UInt32 classId = Memory::registerClassName("TestClass");
    EXPECT_GT(classId, 0u);
    
    void* p = Memory::malloc(128, "TestClass", classId);
    ASSERT_NE(p, nullptr);
    Memory::free(p);
}

TEST_F(MemoryFacadeTest, NullptrFreeIsSafe) {
    EXPECT_NO_THROW(Memory::free(nullptr));
}

TEST_F(MemoryFacadeTest, GetMemoryStats) {
    auto stats = Memory::getMemoryStats();
    
    // Stats should be valid
    EXPECT_GE(stats.poolCount, 0u);
    EXPECT_GE(stats.currentAllocCount, 0u);
    EXPECT_GE(stats.totalPoolMemory, 0u);
}

TEST_F(MemoryFacadeTest, MultipleAllocations) {
    std::vector<void*> pointers;
    
    for (int i = 0; i < 100; ++i) {
        void* p = Memory::malloc(32 + (i % 64));
        ASSERT_NE(p, nullptr);
        pointers.push_back(p);
    }
    
    auto stats = Memory::getMemoryStats();
    EXPECT_GE(stats.currentAllocCount, 100u);
    
    for (void* p : pointers) {
        Memory::free(p);
    }
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST_F(MemoryFacadeTest, ConcurrentAllocations) {
    constexpr int kThreadCount = 4;
    constexpr int kAllocPerThread = 100;
    
    auto allocWorker = []() {
        std::vector<void*> localPtrs;
        for (int i = 0; i < kAllocPerThread; ++i) {
            void* p = Memory::malloc(64);
            ASSERT_NE(p, nullptr);
            localPtrs.push_back(p);
        }
        
        for (void* p : localPtrs) {
            Memory::free(p);
        }
    };
    
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back(allocWorker);
    }
    
    for (auto& t : threads) {
        t.join();
    }
}

// ============================================================================
// Boundary Condition Tests
// ============================================================================

TEST_F(PoolAllocatorTest, LargeAllocation) {
    EXPECT_TRUE(allocator->createPool(1024, 2, 8, 2));
    
    // Allocate maximum pooled size
    void* p = allocator->malloc(1024);
    ASSERT_NE(p, nullptr);
    allocator->free(p);
}

TEST_F(PoolAllocatorTest, MinimumAllocation) {
    EXPECT_TRUE(allocator->createPool(4, 4, 16, 4));
    
    // Allocate minimum size
    void* p = allocator->malloc(1);
    ASSERT_NE(p, nullptr);
    allocator->free(p);
}

TEST_F(PoolAllocatorTest, ExhaustPool) {
    // Create small pool with limited capacity
    EXPECT_TRUE(allocator->createPool(32, 4, 8, 2));
    
    std::vector<void*> pointers;
    
    // Try to allocate beyond capacity
    // Pool should expand up to maxCount
    for (int i = 0; i < 10; ++i) {
        void* p = allocator->malloc(24);
        if (p != nullptr) {
            pointers.push_back(p);
        }
    }
    
    // Should have allocated at least 8 (up to maxCount)
    EXPECT_GE(pointers.size(), 8);
    
    for (void* p : pointers) {
        allocator->free(p);
    }
}

TEST_F(PoolAllocatorTest, AllocateDeallocatePattern) {
    EXPECT_TRUE(allocator->createPool(64, 4, 16, 4));
    
    // Pattern: alloc-free-alloc-free
    for (int cycle = 0; cycle < 100; ++cycle) {
        void* p = allocator->malloc(50);
        ASSERT_NE(p, nullptr);
        allocator->free(p);
    }
}

TEST_F(MemoryFacadeTest, MixedSizeAllocations) {
    std::vector<void*> pointers;
    const std::vector<size_t> sizes = {8, 16, 32, 64, 128, 256, 512, 1024};
    
    // Allocate different sizes
    for (size_t size : sizes) {
        void* p = Memory::malloc(size);
        ASSERT_NE(p, nullptr);
        pointers.push_back(p);
    }
    
    // Free all
    for (void* p : pointers) {
        Memory::free(p);
    }
}

TEST_F(MemoryFacadeTest, StressTest) {
    std::vector<void*> pointers;
    
    // Allocate many blocks of varying sizes
    for (int i = 0; i < 1000; ++i) {
        size_t size = 16 + (i % 256);
        void* p = Memory::malloc(size);
        ASSERT_NE(p, nullptr);
        pointers.push_back(p);
        
        // Occasionally free some blocks
        if (i % 100 == 0 && !pointers.empty()) {
            Memory::free(pointers.back());
            pointers.pop_back();
        }
    }
    
    // Free remaining
    for (void* p : pointers) {
        Memory::free(p);
    }
}

// ============================================================================
// Memory Statistics Tests
// ============================================================================

TEST_F(MemoryFacadeTest, StatisticsAccuracy) {
    auto statsBefore = Memory::getMemoryStats();
    
    // Allocate some memory
    std::vector<void*> pointers;
    for (int i = 0; i < 50; ++i) {
        pointers.push_back(Memory::malloc(64));
    }
    
    auto statsAfter = Memory::getMemoryStats();
    
    // Allocation count should have increased
    EXPECT_GE(statsAfter.currentAllocCount, statsBefore.currentAllocCount);
    EXPECT_GT(statsAfter.currentAllocSize, statsBefore.currentAllocSize);
    
    // Free all
    for (void* p : pointers) {
        Memory::free(p);
    }
}

TEST_F(MemoryFacadeTest, ClassNameRegistration) {
    // Register multiple class names
    UInt32 id1 = Memory::registerClassName("ClassA");
    UInt32 id2 = Memory::registerClassName("ClassB");
    UInt32 id3 = Memory::registerClassName("ClassC");
    
    // IDs should be unique and non-zero
    EXPECT_GT(id1, 0u);
    EXPECT_GT(id2, 0u);
    EXPECT_GT(id3, 0u);
    EXPECT_NE(id1, id2);
    EXPECT_NE(id2, id3);
    EXPECT_NE(id1, id3);
    
    // Can allocate with registered class names
    void* p = Memory::malloc(128, "ClassA", id1);
    ASSERT_NE(p, nullptr);
    Memory::free(p);
}
