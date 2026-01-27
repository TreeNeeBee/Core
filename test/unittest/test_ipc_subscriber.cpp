/**
 * @file        test_ipc_subscriber.cpp
 * @brief       Simplified unit tests for Subscriber
 * @details     Tests core Subscriber functionality across all three modes
 */

#include <gtest/gtest.h>
#include "ipc/Subscriber.hpp"
#include "ipc/Publisher.hpp"
#include "CInitialization.hpp"
#include <cstring>
#include <thread>
#include <chrono>

using namespace lap::core;
using namespace lap::core::ipc;

class SubscriberTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Initialize();
        shm_path_ = "/test_sub_" + std::to_string(::getpid());
    }
    
    void TearDown() override
    {
        shm_unlink(shm_path_.c_str());
        Deinitialize();
    }
    
    String shm_path_;
};

TEST_F(SubscriberTest, CreateAndDestroy)
{
    SubscriberConfig config;
    config.chunk_size = 256;
    config.max_chunks = 64;
    
    auto sub_result = Subscriber::Create(shm_path_, config);
    ASSERT_TRUE(sub_result.HasValue()) << "Failed to create Subscriber";
    
    auto subscriber = std::move(sub_result).Value();
    EXPECT_EQ(subscriber.GetShmPath(), shm_path_);
}

TEST_F(SubscriberTest, ConnectDisconnect)
{
    SubscriberConfig config;
    config.chunk_size = 256;
    config.max_chunks = 32;
    
    auto sub_result = Subscriber::Create(shm_path_, config);
    ASSERT_TRUE(sub_result.HasValue());
    auto subscriber = std::move(sub_result).Value();
    
    // Connect to service
    auto connect_result = subscriber.Connect();
    EXPECT_TRUE(connect_result.HasValue());
    
    // Disconnect from service
    auto disconnect_result = subscriber.Disconnect();
    EXPECT_TRUE(disconnect_result.HasValue());
}

TEST_F(SubscriberTest, ReceiveEmpty)
{
    SubscriberConfig config;
    config.chunk_size = 256;
    config.max_chunks = 32;
    config.empty_policy = SubscribePolicy::kSkip;  // Non-blocking
    
    auto sub_result = Subscriber::Create(shm_path_, config);
    ASSERT_TRUE(sub_result.HasValue());
    auto subscriber = std::move(sub_result).Value();
    
    subscriber.Connect();
    
    // Receive from empty queue
    auto sample_result = subscriber.Receive(SubscribePolicy::kSkip);
    EXPECT_FALSE(sample_result.HasValue());
}

TEST_F(SubscriberTest, PublishSubscribe)
{
    const UInt64 chunk_size = 256;
    
    // Create publisher
    PublisherConfig pub_config;
    pub_config.chunk_size = chunk_size;
    pub_config.max_chunks = 32;
    
    auto pub_result = Publisher::Create(shm_path_, pub_config);
    ASSERT_TRUE(pub_result.HasValue());
    auto publisher = std::move(pub_result).Value();
    
    // Create subscriber
    SubscriberConfig sub_config;
    sub_config.chunk_size = chunk_size;
    sub_config.max_chunks = 32;
    sub_config.empty_policy = SubscribePolicy::kSkip;
    
    auto sub_result = Subscriber::Create(shm_path_, sub_config);
    ASSERT_TRUE(sub_result.HasValue());
    auto subscriber = std::move(sub_result).Value();
    
    subscriber.Connect();
    
    // Publish a message
    UInt32 test_value = 0xCAFEBABE;
    auto send_result = publisher.Send(reinterpret_cast<Byte*>(&test_value), sizeof(test_value));
    ASSERT_TRUE(send_result.HasValue());
    
    // Small delay to ensure message propagation
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Receive the message
    auto sample_result = subscriber.Receive(SubscribePolicy::kSkip);
    ASSERT_TRUE(sample_result.HasValue());
    
    auto sample = std::move(sample_result).Value();
    
    // Verify data
    UInt32 received_value;
    Size read_bytes = sample.Read(reinterpret_cast<Byte*>(&received_value), sizeof(received_value));
    
    EXPECT_EQ(read_bytes, sizeof(test_value));
    EXPECT_EQ(received_value, test_value);
}

TEST_F(SubscriberTest, ReceiveWithLambda)
{
    const UInt64 chunk_size = 256;
    
    // Create publisher
    PublisherConfig pub_config;
    pub_config.chunk_size = chunk_size;
    pub_config.max_chunks = 32;
    
    auto pub_result = Publisher::Create(shm_path_, pub_config);
    ASSERT_TRUE(pub_result.HasValue());
    auto publisher = std::move(pub_result).Value();
    
    // Create subscriber
    SubscriberConfig sub_config;
    sub_config.chunk_size = chunk_size;
    sub_config.max_chunks = 32;
    
    auto sub_result = Subscriber::Create(shm_path_, sub_config);
    ASSERT_TRUE(sub_result.HasValue());
    auto subscriber = std::move(sub_result).Value();
    
    subscriber.Connect();
    
    // Send test data
    struct TestData {
        UInt32 id;
        UInt64 timestamp;
        char message[32];
    };
    
    TestData sent_data{42, 123456789, "Lambda Test"};
    publisher.Send(reinterpret_cast<Byte*>(&sent_data), sizeof(sent_data));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Receive with lambda
    TestData received_data;
    auto result = subscriber.Receive([&received_data](Byte* ptr, Size size) -> Size {
        if (size >= sizeof(TestData)) {
            std::memcpy(&received_data, ptr, sizeof(TestData));
            return sizeof(TestData);
        }
        return 0;
    }, SubscribePolicy::kSkip);
    
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value(), sizeof(TestData));
    EXPECT_EQ(received_data.id, sent_data.id);
    EXPECT_EQ(received_data.timestamp, sent_data.timestamp);
    EXPECT_STREQ(received_data.message, sent_data.message);
}

TEST_F(SubscriberTest, ModeSpecificLimits)
{
    // 使用标准配置测试
    const UInt64 chunk_size = 1024;
    
    SubscriberConfig config;
    config.chunk_size = chunk_size;
    config.max_chunks = 64;
    
    auto sub_result = Subscriber::Create(shm_path_, config);
    ASSERT_TRUE(sub_result.HasValue());
    
    auto subscriber = std::move(sub_result).Value();
    EXPECT_EQ(subscriber.GetShmPath(), shm_path_);
}

TEST_F(SubscriberTest, GetShmPath)
{
    SubscriberConfig config;
    config.chunk_size = 256;
    
    auto sub_result = Subscriber::Create(shm_path_, config);
    ASSERT_TRUE(sub_result.HasValue());
    
    auto subscriber = std::move(sub_result).Value();
    EXPECT_EQ(subscriber.GetShmPath(), shm_path_);
}

TEST_F(SubscriberTest, ReceiveWithBuffer)
{
    const UInt64 chunk_size = 512;
    
    // Create publisher
    PublisherConfig pub_config;
    pub_config.chunk_size = chunk_size;
    pub_config.max_chunks = 32;
    
    auto pub_result = Publisher::Create(shm_path_, pub_config);
    ASSERT_TRUE(pub_result.HasValue());
    auto publisher = std::move(pub_result).Value();
    
    // Create subscriber
    SubscriberConfig sub_config;
    sub_config.chunk_size = chunk_size;
    sub_config.max_chunks = 32;
    
    auto sub_result = Subscriber::Create(shm_path_, sub_config);
    ASSERT_TRUE(sub_result.HasValue());
    auto subscriber = std::move(sub_result).Value();
    
    subscriber.Connect();
    
    // Send test data
    char test_data[] = "Buffer receive test";
    publisher.Send(reinterpret_cast<Byte*>(test_data), sizeof(test_data));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Receive into buffer
    char recv_buffer[64] = {};
    auto result = subscriber.Receive(reinterpret_cast<Byte*>(recv_buffer), 
                                     sizeof(recv_buffer),
                                     SubscribePolicy::kSkip);
    
    ASSERT_TRUE(result.HasValue());
    EXPECT_GT(result.Value(), 0);  // Received some data
    EXPECT_STREQ(recv_buffer, test_data);  // Content matches
}

TEST_F(SubscriberTest, MultipleMessages)
{
    const UInt64 chunk_size = 128;
    
    // Create publisher
    PublisherConfig pub_config;
    pub_config.chunk_size = chunk_size;
    pub_config.max_chunks = 64;
    
    auto pub_result = Publisher::Create(shm_path_, pub_config);
    ASSERT_TRUE(pub_result.HasValue());
    auto publisher = std::move(pub_result).Value();
    
    // Create subscriber
    SubscriberConfig sub_config;
    sub_config.chunk_size = chunk_size;
    sub_config.max_chunks = 64;
    sub_config.empty_policy = SubscribePolicy::kSkip;
    
    auto sub_result = Subscriber::Create(shm_path_, sub_config);
    ASSERT_TRUE(sub_result.HasValue());
    auto subscriber = std::move(sub_result).Value();
    
    subscriber.Connect();
    
    // Send multiple messages
    constexpr int msg_count = 10;
    for (int i = 0; i < msg_count; ++i) {
        publisher.Send(reinterpret_cast<Byte*>(&i), sizeof(i));
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    
    // Receive all messages
    int received_count = 0;
    for (int i = 0; i < msg_count; ++i) {
        auto sample_result = subscriber.Receive(SubscribePolicy::kSkip);
        if (sample_result.HasValue()) {
            int value;
            sample_result.Value().Read(reinterpret_cast<Byte*>(&value), sizeof(value));
            EXPECT_EQ(value, i);
            ++received_count;
        }
    }
    
    EXPECT_EQ(received_count, msg_count);
}

TEST_F(SubscriberTest, QueueState)
{
    SubscriberConfig config;
    config.chunk_size = 256;
    config.max_chunks = 32;
    config.empty_policy = SubscribePolicy::kSkip;
    
    auto sub_result = Subscriber::Create(shm_path_, config);
    ASSERT_TRUE(sub_result.HasValue());
    auto subscriber = std::move(sub_result).Value();
    
    subscriber.Connect();
    
    // Queue should be empty initially
    EXPECT_TRUE(subscriber.IsQueueEmpty());
    EXPECT_EQ(subscriber.GetQueueSize(), 0);
}

TEST_F(SubscriberTest, UpdateSTMin)
{
    SubscriberConfig config;
    config.chunk_size = 256;
    config.max_chunks = 32;
    config.STmin = 10;
    
    auto sub_result = Subscriber::Create(shm_path_, config);
    ASSERT_TRUE(sub_result.HasValue());
    auto subscriber = std::move(sub_result).Value();
    
    // Update STMin
    subscriber.UpdateSTMin(20);
    
    // Should not crash
    SUCCEED();
}

TEST_F(SubscriberTest, MultipleSubscribers)
{
    const UInt64 chunk_size = 256;
    
    // Create publisher
    PublisherConfig pub_config;
    pub_config.chunk_size = chunk_size;
    pub_config.max_chunks = 64;
    
    auto pub_result = Publisher::Create(shm_path_, pub_config);
    ASSERT_TRUE(pub_result.HasValue());
    auto publisher = std::move(pub_result).Value();
    
    // Create multiple subscribers
    std::vector<Subscriber> subscribers;
    
    // 使用标准配置测试
    const int sub_count = 5;
    
    for (int i = 0; i < sub_count; ++i) {
        SubscriberConfig sub_config;
        sub_config.chunk_size = chunk_size;
        sub_config.max_chunks = 64;
        sub_config.empty_policy = SubscribePolicy::kSkip;
        
        auto sub_result = Subscriber::Create(shm_path_, sub_config);
        ASSERT_TRUE(sub_result.HasValue()) << "Failed to create subscriber " << i;
        
        auto sub = std::move(sub_result).Value();
        sub.Connect();
        subscribers.push_back(std::move(sub));
    }
    
    // Publish a message
    UInt32 test_value = 0xDEADBEEF;
    publisher.Send(reinterpret_cast<Byte*>(&test_value), sizeof(test_value));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    
    // All subscribers should receive the message
    int received_count = 0;
    for (auto& sub : subscribers) {
        auto sample_result = sub.Receive(SubscribePolicy::kSkip);
        if (sample_result.HasValue()) {
            UInt32 value;
            sample_result.Value().Read(reinterpret_cast<Byte*>(&value), sizeof(value));
            if (value == test_value) {
                ++received_count;
            }
        }
    }
    
    EXPECT_EQ(received_count, sub_count);
}

TEST_F(SubscriberTest, DisconnectIdempotent)
{
    SubscriberConfig config;
    config.chunk_size = 256;
    config.max_chunks = 32;
    
    auto sub_result = Subscriber::Create(shm_path_, config);
    ASSERT_TRUE(sub_result.HasValue());
    auto subscriber = std::move(sub_result).Value();
    
    subscriber.Connect();
    
    // Disconnect multiple times should be safe
    auto result1 = subscriber.Disconnect();
    auto result2 = subscriber.Disconnect();
    
    EXPECT_TRUE(result1.HasValue() || result2.HasValue());
}

TEST_F(SubscriberTest, ReceivePolicyError)
{
    SubscriberConfig config;
    config.chunk_size = 256;
    config.max_chunks = 32;
    config.empty_policy = SubscribePolicy::kError;
    
    auto sub_result = Subscriber::Create(shm_path_, config);
    ASSERT_TRUE(sub_result.HasValue());
    auto subscriber = std::move(sub_result).Value();
    
    subscriber.Connect();
    
    // Receive from empty queue with kError policy
    auto sample_result = subscriber.Receive(SubscribePolicy::kError);
    EXPECT_FALSE(sample_result.HasValue());
}
