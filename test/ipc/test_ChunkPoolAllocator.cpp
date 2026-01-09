/**
 * @file        test_ChunkPoolAllocator.cpp
 * @author      LightAP Core Team
 * @brief       Unit tests for ChunkPoolAllocator
 * @date        2026-01-07
 * @copyright   Copyright (c) 2026
 */

#include <gtest/gtest.h>
#include "ipc/ChunkPoolAllocator.hpp"
#include "ipc/SharedMemoryManager.hpp"
#include "ipc/ChunkHeader.hpp"
#include <thread>
#include <vector>
#include <atomic>

using namespace lap::core::ipc;
using namespace lap::core;

class ChunkPoolAllocatorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        shm_unlink(test_name_.c_str());
        
        SharedMemoryConfig config;
        config.total_size = 8 * 1024 * 1024;  // 8MB
        
        shm_manager_ = std::make_shared<SharedMemoryManager>();
        auto result = shm_manager_->Create(test_name_, config);
        ASSERT_TRUE(result.HasValue());
        
        control_ = reinterpret_cast<ControlBlock*>(shm_manager_->GetBaseAddress());
    }
    
    void TearDown() override
    {
        shm_manager_->Cleanup();
        shm_unlink(test_name_.c_str());
    }
    
    const String test_name_ = "/lap_ipc_test_chunk";
    std::shared_ptr<SharedMemoryManager> shm_manager_;
    ControlBlock* control_ = nullptr;
};

// Test: Initialize allocator
TEST_F(ChunkPoolAllocatorTest, InitializeSuccess)
{
    ChunkPoolAllocator allocator(shm_manager_, control_);
    
    control_->max_chunks = 16;
    control_->chunk_size = 256;
    
    auto result = allocator.Initialize();
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(allocator.GetMaxChunks(), 16u);
    EXPECT_EQ(allocator.GetAllocatedCount(), 0u);
}

// Test: Allocate and deallocate single chunk
TEST_F(ChunkPoolAllocatorTest, AllocateDeallocateSingle)
{
    ChunkPoolAllocator allocator(shm_manager_, control_);
    control_->max_chunks = 16;
    control_->chunk_size = 256;
    allocator.Initialize();
    
    auto chunk_index = allocator.Allocate();
    ASSERT_TRUE(chunk_index.has_value());
    EXPECT_LT(chunk_index.value(), 16u);
    EXPECT_EQ(allocator.GetAllocatedCount(), 1u);
    
    allocator.Deallocate(chunk_index.value());
    EXPECT_EQ(allocator.GetAllocatedCount(), 0u);
}

// Test: Allocate all chunks
TEST_F(ChunkPoolAllocatorTest, AllocateAll)
{
    const UInt32 max_chunks = 8;
    ChunkPoolAllocator allocator(shm_manager_, control_);
    control_->max_chunks = max_chunks;
    control_->chunk_size = 256;
    allocator.Initialize();
    
    std::vector<UInt32> chunks;
    for (UInt32 i = 0; i < max_chunks; ++i)
    {
        auto chunk_index = allocator.Allocate();
        ASSERT_TRUE(chunk_index.has_value()) << "Failed to allocate chunk " << i;
        chunks.push_back(chunk_index.value());
    }
    
    EXPECT_EQ(allocator.GetAllocatedCount(), max_chunks);
    
    // Next allocation should fail
    auto overflow = allocator.Allocate();
    EXPECT_FALSE(overflow.has_value());
}

// Test: Deallocate and reallocate
TEST_F(ChunkPoolAllocatorTest, DeallocateReallocate)
{
    ChunkPoolAllocator allocator(shm_manager_, control_);
    control_->max_chunks = 4;
    control_->chunk_size = 256;
    allocator.Initialize();
    
    auto idx1 = allocator.Allocate();
    auto idx2 = allocator.Allocate();
    ASSERT_TRUE(idx1.has_value() && idx2.has_value());
    
    allocator.Deallocate(idx1.value());
    EXPECT_EQ(allocator.GetAllocatedCount(), 1u);
    
    auto idx3 = allocator.Allocate();
    ASSERT_TRUE(idx3.has_value());
    // Should reuse freed chunk
    EXPECT_EQ(idx3.value(), idx1.value());
}

// Test: Get chunk header
TEST_F(ChunkPoolAllocatorTest, GetChunkHeader)
{
    ChunkPoolAllocator allocator(shm_manager_, control_);
    control_->max_chunks = 16;
    control_->chunk_size = 256;
    allocator.Initialize();
    
    auto chunk_index = allocator.Allocate();
    ASSERT_TRUE(chunk_index.has_value());
    
    auto* header = allocator.GetChunkHeader(chunk_index.value());
    ASSERT_NE(header, nullptr);
    EXPECT_EQ(header->chunk_index, chunk_index.value());
}

// Test: Get payload
TEST_F(ChunkPoolAllocatorTest, GetPayload)
{
    ChunkPoolAllocator allocator(shm_manager_, control_);
    control_->max_chunks = 16;
    control_->chunk_size = 256;
    allocator.Initialize();
    
    auto chunk_index = allocator.Allocate();
    ASSERT_TRUE(chunk_index.has_value());
    
    auto* payload = allocator.GetPayload<UInt8>(chunk_index.value());
    ASSERT_NE(payload, nullptr);
    
    // Write and read back
    payload[0] = 42;
    payload[1] = 100;
    EXPECT_EQ(payload[0], 42);
    EXPECT_EQ(payload[1], 100);
}

// Test: Reference counting
TEST_F(ChunkPoolAllocatorTest, ReferenceCount)
{
    ChunkPoolAllocator allocator(shm_manager_, control_);
    control_->max_chunks = 16;
    control_->chunk_size = 256;
    allocator.Initialize();
    
    auto chunk_index = allocator.Allocate();
    ASSERT_TRUE(chunk_index.has_value());
    
    auto* header = allocator.GetChunkHeader(chunk_index.value());
    EXPECT_EQ(header->ref_count.load(), 1u);
    
    allocator.AddReference(chunk_index.value());
    EXPECT_EQ(header->ref_count.load(), 2u);
    
    allocator.RemoveReference(chunk_index.value());
    EXPECT_EQ(header->ref_count.load(), 1u);
    EXPECT_EQ(allocator.GetAllocatedCount(), 1u);
    
    allocator.RemoveReference(chunk_index.value());
    EXPECT_EQ(header->ref_count.load(), 0u);
    EXPECT_EQ(allocator.GetAllocatedCount(), 0u);
}

// Test: Thread-safe allocation (concurrent allocate)
TEST_F(ChunkPoolAllocatorTest, ConcurrentAllocate)
{
    const UInt32 max_chunks = 64;
    const int num_threads = 8;
    const int allocs_per_thread = 8;
    
    ChunkPoolAllocator allocator(shm_manager_, control_);
    control_->max_chunks = max_chunks;
    control_->chunk_size = 256;
    allocator.Initialize();
    
    std::vector<std::thread> threads;
    std::atomic<UInt32> success_count{0};
    std::vector<std::vector<UInt32>> thread_chunks(num_threads);
    
    for (int i = 0; i < num_threads; ++i)
    {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < allocs_per_thread; ++j)
            {
                auto chunk_index = allocator.Allocate();
                if (chunk_index.has_value())
                {
                    thread_chunks[i].push_back(chunk_index.value());
                    success_count.fetch_add(1);
                }
            }
        });
    }
    
    for (auto& t : threads)
    {
        t.join();
    }
    
    EXPECT_EQ(success_count.load(), static_cast<UInt32>(num_threads * allocs_per_thread));
    EXPECT_EQ(allocator.GetAllocatedCount(), static_cast<UInt32>(num_threads * allocs_per_thread));
    
    // Verify no duplicate chunk indices
    std::set<UInt32> all_chunks;
    for (const auto& chunks : thread_chunks)
    {
        for (auto idx : chunks)
        {
            EXPECT_TRUE(all_chunks.insert(idx).second) << "Duplicate chunk index: " << idx;
        }
    }
}

// Test: Thread-safe deallocation
TEST_F(ChunkPoolAllocatorTest, ConcurrentDeallocate)
{
    const UInt32 max_chunks = 64;
    ChunkPoolAllocator allocator(shm_manager_, control_);
    control_->max_chunks = max_chunks;
    control_->chunk_size = 256;
    allocator.Initialize();
    
    // Allocate all chunks first
    std::vector<UInt32> chunks;
    for (UInt32 i = 0; i < max_chunks; ++i)
    {
        auto idx = allocator.Allocate();
        ASSERT_TRUE(idx.has_value());
        chunks.push_back(idx.value());
    }
    
    // Deallocate concurrently
    const int num_threads = 8;
    const int chunks_per_thread = max_chunks / num_threads;
    std::vector<std::thread> threads;
    
    for (int i = 0; i < num_threads; ++i)
    {
        threads.emplace_back([&, i]() {
            int start = i * chunks_per_thread;
            int end = (i + 1) * chunks_per_thread;
            for (int j = start; j < end; ++j)
            {
                allocator.Deallocate(chunks[j]);
            }
        });
    }
    
    for (auto& t : threads)
    {
        t.join();
    }
    
    EXPECT_EQ(allocator.GetAllocatedCount(), 0u);
}

// Test: Invalid chunk index
TEST_F(ChunkPoolAllocatorTest, InvalidChunkIndex)
{
    ChunkPoolAllocator allocator(shm_manager_, control_);
    control_->max_chunks = 16;
    control_->chunk_size = 256;
    allocator.Initialize();
    
    auto* header = allocator.GetChunkHeader(999);
    EXPECT_EQ(header, nullptr);
    
    auto* payload = allocator.GetPayload<UInt8>(999);
    EXPECT_EQ(payload, nullptr);
}

// Test: Chunk alignment
TEST_F(ChunkPoolAllocatorTest, ChunkAlignment)
{
    ChunkPoolAllocator allocator(shm_manager_, control_);
    control_->max_chunks = 16;
    control_->chunk_size = 256;
    allocator.Initialize();
    
    auto chunk_index = allocator.Allocate();
    ASSERT_TRUE(chunk_index.has_value());
    
    auto* header = allocator.GetChunkHeader(chunk_index.value());
    auto* payload = allocator.GetPayload<UInt8>(chunk_index.value());
    
    // Verify alignment
    EXPECT_EQ(reinterpret_cast<uintptr_t>(header) % kCacheLineSize, 0u);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(payload) % kCacheLineSize, 0u);
}
