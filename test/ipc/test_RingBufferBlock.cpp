/**
 * @file        test_RingBufferBlock.cpp
 * @author      LightAP Core Team
 * @brief       Unit tests for RingBufferBlock (SPSC queue)
 * @date        2026-01-07
 * @copyright   Copyright (c) 2026
 */

#include <gtest/gtest.h>
#include "ipc/RingBufferBlock.hpp"
#include <thread>
#include <vector>
#include <chrono>

using namespace lap::core::ipc;
using namespace lap::core;

class RingBufferBlockTest : public ::testing::Test
{
protected:
    static constexpr UInt32 kTestCapacity = 16;
};

// Test: Initial state
TEST_F(RingBufferBlockTest, InitialState)
{
    RingBufferBlock<UInt32, kTestCapacity> buffer;
    
    EXPECT_TRUE(buffer.IsEmpty());
    EXPECT_FALSE(buffer.IsFull());
    EXPECT_EQ(buffer.GetSize(), 0u);
}

// Test: Enqueue single element
TEST_F(RingBufferBlockTest, EnqueueSingle)
{
    RingBufferBlock<UInt32, kTestCapacity> buffer;
    
    bool success = buffer.Enqueue(42);
    EXPECT_TRUE(success);
    EXPECT_FALSE(buffer.IsEmpty());
    EXPECT_EQ(buffer.GetSize(), 1u);
}

// Test: Dequeue single element
TEST_F(RingBufferBlockTest, DequeueSingle)
{
    RingBufferBlock<UInt32, kTestCapacity> buffer;
    
    buffer.Enqueue(42);
    
    auto value = buffer.Dequeue();
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value.value(), 42u);
    EXPECT_TRUE(buffer.IsEmpty());
}

// Test: Enqueue and dequeue multiple
TEST_F(RingBufferBlockTest, EnqueueDequeueMultiple)
{
    RingBufferBlock<UInt32, kTestCapacity> buffer;
    
    for (UInt32 i = 0; i < 10; ++i)
    {
        EXPECT_TRUE(buffer.Enqueue(i));
    }
    
    EXPECT_EQ(buffer.GetSize(), 10u);
    
    for (UInt32 i = 0; i < 10; ++i)
    {
        auto value = buffer.Dequeue();
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(value.value(), i);
    }
    
    EXPECT_TRUE(buffer.IsEmpty());
}

// Test: Fill buffer completely
TEST_F(RingBufferBlockTest, FillBuffer)
{
    RingBufferBlock<UInt32, kTestCapacity> buffer;
    
    for (UInt32 i = 0; i < kTestCapacity; ++i)
    {
        EXPECT_TRUE(buffer.Enqueue(i)) << "Failed at index " << i;
    }
    
    EXPECT_TRUE(buffer.IsFull());
    EXPECT_EQ(buffer.GetSize(), kTestCapacity);
    
    // Next enqueue should fail
    EXPECT_FALSE(buffer.Enqueue(999));
}

// Test: Dequeue from empty buffer
TEST_F(RingBufferBlockTest, DequeueEmpty)
{
    RingBufferBlock<UInt32, kTestCapacity> buffer;
    
    auto value = buffer.Dequeue();
    EXPECT_FALSE(value.has_value());
}

// Test: Wrap around
TEST_F(RingBufferBlockTest, WrapAround)
{
    RingBufferBlock<UInt32, kTestCapacity> buffer;
    
    // Fill buffer
    for (UInt32 i = 0; i < kTestCapacity; ++i)
    {
        buffer.Enqueue(i);
    }
    
    // Empty half
    for (UInt32 i = 0; i < kTestCapacity / 2; ++i)
    {
        auto value = buffer.Dequeue();
        EXPECT_EQ(value.value(), i);
    }
    
    // Fill again (should wrap around)
    for (UInt32 i = 0; i < kTestCapacity / 2; ++i)
    {
        EXPECT_TRUE(buffer.Enqueue(100 + i));
    }
    
    EXPECT_TRUE(buffer.IsFull());
    
    // Verify order
    for (UInt32 i = kTestCapacity / 2; i < kTestCapacity; ++i)
    {
        auto value = buffer.Dequeue();
        EXPECT_EQ(value.value(), i);
    }
    
    for (UInt32 i = 0; i < kTestCapacity / 2; ++i)
    {
        auto value = buffer.Dequeue();
        EXPECT_EQ(value.value(), 100 + i);
    }
}

// Test: SPSC concurrent access
TEST_F(RingBufferBlockTest, SPSCConcurrent)
{
    RingBufferBlock<UInt32, 256> buffer;
    const UInt32 num_items = 10000;
    std::atomic<bool> producer_done{false};
    
    // Producer thread
    std::thread producer([&]() {
        for (UInt32 i = 0; i < num_items; ++i)
        {
            while (!buffer.Enqueue(i))
            {
                std::this_thread::yield();
            }
        }
        producer_done.store(true);
    });
    
    // Consumer thread
    std::thread consumer([&]() {
        UInt32 expected = 0;
        while (expected < num_items)
        {
            auto value = buffer.Dequeue();
            if (value.has_value())
            {
                EXPECT_EQ(value.value(), expected);
                expected++;
            }
            else
            {
                std::this_thread::yield();
            }
        }
    });
    
    producer.join();
    consumer.join();
    
    EXPECT_TRUE(buffer.IsEmpty());
}

// Test: Stress test with fast producer/consumer
TEST_F(RingBufferBlockTest, StressTest)
{
    RingBufferBlock<UInt64, 128> buffer;
    const UInt64 num_items = 100000;
    std::atomic<UInt64> consumed{0};
    
    std::thread producer([&]() {
        for (UInt64 i = 0; i < num_items; ++i)
        {
            while (!buffer.Enqueue(i))
            {
                // Busy wait
            }
        }
    });
    
    std::thread consumer([&]() {
        while (consumed.load() < num_items)
        {
            auto value = buffer.Dequeue();
            if (value.has_value())
            {
                consumed.fetch_add(1);
            }
        }
    });
    
    producer.join();
    consumer.join();
    
    EXPECT_EQ(consumed.load(), num_items);
}

// Test: Complex data type
TEST_F(RingBufferBlockTest, ComplexDataType)
{
    struct TestData
    {
        UInt64 id;
        double value;
        char name[32];
    };
    
    RingBufferBlock<TestData, 16> buffer;
    
    TestData data1{1, 3.14, "Test1"};
    TestData data2{2, 2.71, "Test2"};
    
    EXPECT_TRUE(buffer.Enqueue(data1));
    EXPECT_TRUE(buffer.Enqueue(data2));
    
    auto result1 = buffer.Dequeue();
    ASSERT_TRUE(result1.has_value());
    EXPECT_EQ(result1.value().id, 1u);
    EXPECT_DOUBLE_EQ(result1.value().value, 3.14);
    EXPECT_STREQ(result1.value().name, "Test1");
    
    auto result2 = buffer.Dequeue();
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result2.value().id, 2u);
}

// Test: Size tracking accuracy
TEST_F(RingBufferBlockTest, SizeTracking)
{
    RingBufferBlock<UInt32, kTestCapacity> buffer;
    
    for (UInt32 i = 0; i < 5; ++i)
    {
        buffer.Enqueue(i);
        EXPECT_EQ(buffer.GetSize(), i + 1);
    }
    
    for (UInt32 i = 5; i > 0; --i)
    {
        buffer.Dequeue();
        EXPECT_EQ(buffer.GetSize(), i - 1);
    }
}

// Test: Performance benchmark (informational)
TEST_F(RingBufferBlockTest, PerformanceBenchmark)
{
    RingBufferBlock<UInt64, 256> buffer;
    const UInt64 iterations = 1000000;
    
    auto start = std::chrono::steady_clock::now();
    
    for (UInt64 i = 0; i < iterations; ++i)
    {
        buffer.Enqueue(i);
        buffer.Dequeue();
    }
    
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    
    double ns_per_op = static_cast<double>(duration.count()) / (iterations * 2);  // *2 for enqueue+dequeue
    
    std::cout << "RingBuffer performance: " << ns_per_op << " ns per operation" << std::endl;
    
    // Just verify it's reasonably fast (< 100ns per operation)
    EXPECT_LT(ns_per_op, 100.0);
}
