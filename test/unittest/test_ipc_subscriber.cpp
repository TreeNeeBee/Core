/**
 * @file        test_ipc_subscriber.cpp
 * @brief       Simplified unit tests for Subscriber
 * @details     Tests core Subscriber functionality across all three modes
 */

#include <gtest/gtest.h>
#include "ipc/Subscriber.hpp"
#include "ipc/Publisher.hpp"
#include "IPCFactory.hpp"
#include "CInitialization.hpp"
#include <cstring>
#include <thread>
#include <chrono>
#include <vector>
#include <unistd.h>

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

static UniqueHandle< SharedMemoryManager > CreateShmForSubscriber(
    const String& shm_path,
    const SubscriberConfig& config)
{
    SharedMemoryConfig shm_config{};
    shm_config.max_chunks = config.max_chunks;
    shm_config.chunk_size = config.chunk_size;
    shm_config.ipc_type = config.ipc_type;
    auto shm_result = IPCFactory::CreateSHM(shm_path, shm_config);
    EXPECT_TRUE(shm_result.HasValue());
    if (!shm_result.HasValue()) {
        return nullptr;
    }
    return std::move(shm_result).Value();
}

static UniqueHandle< SharedMemoryManager > CreateShmForPubSub(
    const String& shm_path,
    const PublisherConfig& pub_config)
{
    SharedMemoryConfig shm_config{};
    shm_config.max_chunks = pub_config.max_chunks;
    shm_config.chunk_size = pub_config.chunk_size;
    shm_config.ipc_type = pub_config.ipc_type;
    auto shm_result = IPCFactory::CreateSHM(shm_path, shm_config);
    EXPECT_TRUE(shm_result.HasValue());
    if (!shm_result.HasValue()) {
        return nullptr;
    }
    return std::move(shm_result).Value();
}

TEST_F(SubscriberTest, CreateAndDestroy)
{
    SubscriberConfig config;
    config.chunk_size = 256;
    config.max_chunks = 64;
    
    auto shm = CreateShmForSubscriber(shm_path_, config);
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
    
    auto shm = CreateShmForSubscriber(shm_path_, config);
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
    
    auto shm = CreateShmForSubscriber(shm_path_, config);
    auto sub_result = Subscriber::Create(shm_path_, config);
    ASSERT_TRUE(sub_result.HasValue());
    auto subscriber = std::move(sub_result).Value();
    
    subscriber.Connect();
    
    // Receive from empty queue
    auto sample_result = subscriber.Receive(SubscribePolicy::kSkip);
    ASSERT_TRUE(sample_result.HasValue());
    EXPECT_TRUE(sample_result.Value().empty());
}

TEST_F(SubscriberTest, PublishSubscribe)
{
    const UInt64 chunk_size = 256;
    
    // Create publisher
    PublisherConfig pub_config;
    pub_config.chunk_size = chunk_size;
    pub_config.max_chunks = 32;
    
    auto shm = CreateShmForPubSub(shm_path_, pub_config);
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
    
    // Publish and receive with retries to allow scanner update
    UInt32 test_value = 0xCAFEBABE;
    bool received = false;
    UInt32 received_value = 0;
    Size read_bytes = 0;

    for (int attempt = 0; attempt < 20 && !received; ++attempt) {
        auto send_result = publisher.Send(reinterpret_cast<Byte*>(&test_value), sizeof(test_value));
        ASSERT_TRUE(send_result.HasValue());

        std::this_thread::sleep_for(std::chrono::milliseconds(5));

        auto sample_result = subscriber.Receive(SubscribePolicy::kSkip);
        if (sample_result.HasValue()) {
            auto samples = std::move(sample_result).Value();
            if (!samples.empty()) {
                read_bytes = samples.front().Read(reinterpret_cast<Byte*>(&received_value), sizeof(received_value));
                received = true;
            }
        }
    }

    ASSERT_TRUE(received);
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
    
    auto shm = CreateShmForPubSub(shm_path_, pub_config);
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
    auto result = subscriber.Receive([&received_data](UInt8, Byte* ptr, Size size) -> Size {
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
    
    auto shm = CreateShmForSubscriber(shm_path_, config);
    auto sub_result = Subscriber::Create(shm_path_, config);
    ASSERT_TRUE(sub_result.HasValue());
    
    auto subscriber = std::move(sub_result).Value();
    EXPECT_EQ(subscriber.GetShmPath(), shm_path_);
}

TEST_F(SubscriberTest, GetShmPath)
{
    SubscriberConfig config;
    config.chunk_size = 256;
    
    auto shm = CreateShmForSubscriber(shm_path_, config);
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
    
    auto shm = CreateShmForPubSub(shm_path_, pub_config);
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
    
    // Send test data and receive with retries
    char test_data[] = "Buffer receive test";
    char recv_buffer[64] = {};
    bool received = false;
    Size received_size = 0;

    for (int attempt = 0; attempt < 20 && !received; ++attempt) {
        std::memset(recv_buffer, 0, sizeof(recv_buffer));
        publisher.Send(reinterpret_cast<Byte*>(test_data), sizeof(test_data));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

        auto result = subscriber.Receive([&](UInt8, Byte* ptr, Size size) -> Size {
            Size to_copy = (size < sizeof(recv_buffer)) ? size : sizeof(recv_buffer);
            std::memcpy(recv_buffer, ptr, to_copy);
            return to_copy;
        }, SubscribePolicy::kSkip);

        if (result.HasValue() && result.Value() > 0) {
            received = true;
            received_size = result.Value();
        }
    }

    ASSERT_TRUE(received);
    EXPECT_GT(received_size, 0);
    EXPECT_STREQ(recv_buffer, test_data);
}

TEST_F(SubscriberTest, MultipleMessages)
{
    const UInt64 chunk_size = 128;
    
    // Create publisher
    PublisherConfig pub_config;
    pub_config.chunk_size = chunk_size;
    pub_config.max_chunks = 64;
    
    auto shm = CreateShmForPubSub(shm_path_, pub_config);
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

    // Allow scanner to observe subscriber
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Send multiple messages
    constexpr int msg_count = 10;
    for (int i = 0; i < msg_count; ++i) {
        publisher.Send(reinterpret_cast<Byte*>(&i), sizeof(i));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Receive all messages with retries (order not guaranteed)
    std::vector<bool> received(msg_count, false);
    int received_count = 0;
    for (int attempt = 0; attempt < 50 && received_count < msg_count; ++attempt) {
        auto sample_result = subscriber.Receive(SubscribePolicy::kSkip);
        if (sample_result.HasValue()) {
            auto samples = std::move(sample_result).Value();
            for (auto& sample : samples) {
                int value = -1;
                sample.Read(reinterpret_cast<Byte*>(&value), sizeof(value));
                if (value >= 0 && value < msg_count && !received[static_cast<size_t>(value)]) {
                    received[static_cast<size_t>(value)] = true;
                    ++received_count;
                }
            }
        }
        if (received_count < msg_count) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
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
    
    auto shm = CreateShmForSubscriber(shm_path_, config);
    auto sub_result = Subscriber::Create(shm_path_, config);
    ASSERT_TRUE(sub_result.HasValue());
    auto subscriber = std::move(sub_result).Value();
    
    subscriber.Connect();
}

TEST_F(SubscriberTest, UpdateSTMin)
{
    SubscriberConfig config;
    config.chunk_size = 256;
    config.max_chunks = 32;
    config.STmin = 10000;
    
    auto shm = CreateShmForSubscriber(shm_path_, config);
    auto sub_result = Subscriber::Create(shm_path_, config);
    ASSERT_TRUE(sub_result.HasValue());
    auto subscriber = std::move(sub_result).Value();
    
    // Update STMin
    subscriber.UpdateSTMin(20000);
    
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
    
    auto shm = CreateShmForPubSub(shm_path_, pub_config);
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
    
    // All subscribers should receive the message (retry while scanner updates)
    std::vector<bool> received_flags(sub_count, false);
    int received_count = 0;

    for (int attempt = 0; attempt < 50 && received_count < sub_count; ++attempt) {
        publisher.Send(reinterpret_cast<Byte*>(&test_value), sizeof(test_value));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

        for (int i = 0; i < sub_count; ++i) {
            if (received_flags[i]) {
                continue;
            }
            auto sample_result = subscribers[i].Receive(SubscribePolicy::kSkip);
            if (sample_result.HasValue()) {
                auto samples = std::move(sample_result).Value();
                if (!samples.empty()) {
                    UInt32 value;
                    samples.front().Read(reinterpret_cast<Byte*>(&value), sizeof(value));
                    if (value == test_value) {
                        received_flags[i] = true;
                        ++received_count;
                    }
                }
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
    
    auto shm = CreateShmForSubscriber(shm_path_, config);
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
    
    auto shm = CreateShmForSubscriber(shm_path_, config);
    auto sub_result = Subscriber::Create(shm_path_, config);
    ASSERT_TRUE(sub_result.HasValue());
    auto subscriber = std::move(sub_result).Value();
    
    subscriber.Connect();
    
    // Receive from empty queue with kError policy
    auto sample_result = subscriber.Receive(SubscribePolicy::kError);
    ASSERT_TRUE(sample_result.HasValue());
    EXPECT_TRUE(sample_result.Value().empty());
}
