/**
 * @file        test_SharedMemoryManager.cpp
 * @author      LightAP Core Team
 * @brief       Unit tests for SharedMemoryManager
 * @date        2026-01-07
 * @copyright   Copyright (c) 2026
 */

#include <gtest/gtest.h>
#include "ipc/SharedMemoryManager.hpp"
#include "ipc/IPCTypes.hpp"
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

using namespace lap::core::ipc;
using namespace lap::core;

class SharedMemoryManagerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Cleanup any leftover shared memory from previous tests
        shm_unlink(test_name_.c_str());
    }
    
    void TearDown() override
    {
        // Cleanup after test
        shm_unlink(test_name_.c_str());
    }
    
    const String test_name_ = "/lap_ipc_test_shm";
    const UInt64 test_size_ = 4 * 1024 * 1024;  // 4MB
};

// Test: Create shared memory successfully
TEST_F(SharedMemoryManagerTest, CreateSuccess)
{
    SharedMemoryConfig config;
    config.total_size = test_size_;
    
    SharedMemoryManager manager;
    auto result = manager.Create(test_name_, config);
    
    ASSERT_TRUE(result.HasValue()) << "Failed to create shared memory";
    EXPECT_NE(manager.GetBaseAddress(), nullptr);
    EXPECT_EQ(manager.GetSize(), AlignToShmSize(test_size_));
    EXPECT_TRUE(manager.IsCreator());
}

// Test: Open existing shared memory
TEST_F(SharedMemoryManagerTest, OpenExisting)
{
    SharedMemoryConfig config;
    config.total_size = test_size_;
    
    // First creator
    SharedMemoryManager creator;
    auto create_result = creator.Create(test_name_, config);
    ASSERT_TRUE(create_result.HasValue());
    EXPECT_TRUE(creator.IsCreator());
    
    // Second opener
    SharedMemoryManager opener;
    auto open_result = opener.Create(test_name_, config);
    ASSERT_TRUE(open_result.HasValue());
    EXPECT_FALSE(opener.IsCreator());
    EXPECT_EQ(opener.GetBaseAddress(), creator.GetBaseAddress());
}

// Test: Open non-existent shared memory fails
TEST_F(SharedMemoryManagerTest, OpenNonExistent)
{
    SharedMemoryConfig config;
    config.total_size = test_size_;
    config.create_if_not_exist = false;
    
    SharedMemoryManager manager;
    auto result = manager.Create("/lap_ipc_nonexistent", config);
    
    EXPECT_FALSE(result.HasValue());
}

// Test: Shared memory magic number validation
TEST_F(SharedMemoryManagerTest, MagicNumberValidation)
{
    SharedMemoryConfig config;
    config.total_size = test_size_;
    
    SharedMemoryManager creator;
    auto create_result = creator.Create(test_name_, config);
    ASSERT_TRUE(create_result.HasValue());
    
    // Verify magic number
    auto* ctrl = reinterpret_cast<ControlBlock*>(creator.GetBaseAddress());
    EXPECT_EQ(ctrl->magic_number, kIPCMagicNumber);
    EXPECT_EQ(ctrl->version, kIPCVersion);
}

// Test: Multiple mappers to same memory
TEST_F(SharedMemoryManagerTest, MultipleMappers)
{
    SharedMemoryConfig config;
    config.total_size = test_size_;
    
    SharedMemoryManager manager1;
    auto result1 = manager1.Create(test_name_, config);
    ASSERT_TRUE(result1.HasValue());
    
    // Write data through first mapper
    auto* ctrl1 = reinterpret_cast<ControlBlock*>(manager1.GetBaseAddress());
    ctrl1->subscriber_count.store(42);
    
    // Open with second mapper
    SharedMemoryManager manager2;
    auto result2 = manager2.Create(test_name_, config);
    ASSERT_TRUE(result2.HasValue());
    
    // Verify data is shared
    auto* ctrl2 = reinterpret_cast<ControlBlock*>(manager2.GetBaseAddress());
    EXPECT_EQ(ctrl2->subscriber_count.load(), 42u);
}

// Test: Alignment is correct
TEST_F(SharedMemoryManagerTest, AlignmentCorrect)
{
    SharedMemoryConfig config;
    config.total_size = 3 * 1024 * 1024;  // 3MB
    
    SharedMemoryManager manager;
    auto result = manager.Create(test_name_, config);
    ASSERT_TRUE(result.HasValue());
    
    // Should be aligned to 2MB boundary
    UInt64 expected_size = AlignToShmSize(3 * 1024 * 1024);
    EXPECT_EQ(manager.GetSize(), expected_size);
    EXPECT_EQ(manager.GetSize() % kShmAlignment, 0u);
}

// Test: Cleanup works
TEST_F(SharedMemoryManagerTest, CleanupWorks)
{
    SharedMemoryConfig config;
    config.total_size = test_size_;
    
    {
        SharedMemoryManager manager;
        auto result = manager.Create(test_name_, config);
        ASSERT_TRUE(result.HasValue());
        EXPECT_NE(manager.GetBaseAddress(), nullptr);
        
        manager.Cleanup();
        EXPECT_EQ(manager.GetBaseAddress(), nullptr);
    }
    
    // Verify shared memory is unlinked
    int fd = shm_open(test_name_.c_str(), O_RDONLY, 0);
    EXPECT_EQ(fd, -1);
}

// Test: Size validation
TEST_F(SharedMemoryManagerTest, MinimumSizeEnforced)
{
    SharedMemoryConfig config;
    config.total_size = 100;  // Too small
    
    SharedMemoryManager manager;
    auto result = manager.Create(test_name_, config);
    
    // Should still create with minimum size
    ASSERT_TRUE(result.HasValue());
    EXPECT_GE(manager.GetSize(), kShmAlignment);
}

// Test: Destructor cleanup
TEST_F(SharedMemoryManagerTest, DestructorCleanup)
{
    SharedMemoryConfig config;
    config.total_size = test_size_;
    
    {
        SharedMemoryManager manager;
        auto result = manager.Create(test_name_, config);
        ASSERT_TRUE(result.HasValue());
        EXPECT_NE(manager.GetBaseAddress(), nullptr);
        // Destructor should cleanup
    }
    
    // Memory should be accessible by second creator
    SharedMemoryManager manager2;
    auto result2 = manager2.Create(test_name_, config);
    EXPECT_TRUE(result2.HasValue());
}
