/**
 * @file        test_Publisher_Subscriber.cpp
 * @author      LightAP Core Team
 * @brief       Integration tests for Publisher and Subscriber
 * @date        2026-01-07
 * @copyright   Copyright (c) 2026
 */

#include <gtest/gtest.h>
#include "ipc/Publisher.hpp"
#include "ipc/Subscriber.hpp"
#include <thread>
#include <chrono>

using namespace lap::core::ipc;
using namespace lap::core;

class PublisherSubscriberTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        service_name_ = "/lap_ipc_test_pubsub_" + std::to_string(test_counter_++);
        shm_unlink(service_name_.c_str());
    }
    
    void TearDown() override
    {
        shm_unlink(service_name_.c_str());
    }
    
    String service_name_;
    static int test_counter_;
};

int PublisherSubscriberTest::test_counter_ = 0;

// Test: Create publisher
TEST_F(PublisherSubscriberTest, CreatePublisher)
{
    PublisherConfig config;
    config.max_chunks = 16;
    config.chunk_size = 256;
    
    auto result = Publisher<UInt8>::Create(service_name_, config);
    ASSERT_TRUE(result.HasValue());
}

// Test: Create subscriber
TEST_F(PublisherSubscriberTest, CreateSubscriber)
{
    PublisherConfig pub_config;
    pub_config.max_chunks = 16;
    pub_config.chunk_size = 256;
    
    auto pub_result = Publisher<UInt8>::Create(service_name_, pub_config);
    ASSERT_TRUE(pub_result.HasValue());
    
    SubscriberConfig sub_config;
    auto sub_result = Subscriber<UInt8>::Create(service_name_, sub_config);
    ASSERT_TRUE(sub_result.HasValue());
}

// Test: Simple message passing
TEST_F(PublisherSubscriberTest, SimpleMessagePassing)
{
    struct TestMsg
    {
        UInt64 id;
        UInt32 value;
    };
    
    PublisherConfig pub_config;
    pub_config.max_chunks = 16;
    pub_config.chunk_size = sizeof(TestMsg);
    
    auto pub = Publisher<TestMsg>::Create(service_name_, pub_config).Value();
    
    SubscriberConfig sub_config;
    auto sub = Subscriber<TestMsg>::Create(service_name_, sub_config).Value();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Send message
    auto loan_result = pub->Loan();
    ASSERT_TRUE(loan_result.HasValue());
    
    auto sample = std::move(loan_result.Value());
    auto* msg = sample.GetPayload();
    msg->id = 12345;
    msg->value = 999;
    
    auto send_result = sample.Send();
    ASSERT_TRUE(send_result.HasValue());
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Receive message
    auto recv_result = sub->Receive();
    ASSERT_TRUE(recv_result.HasValue());
    
    auto recv_sample = std::move(recv_result.Value());
    auto* recv_msg = recv_sample.GetPayload();
    EXPECT_EQ(recv_msg->id, 12345u);
    EXPECT_EQ(recv_msg->value, 999u);
}

// Test: Multiple subscribers
TEST_F(PublisherSubscriberTest, MultipleSubscribers)
{
    PublisherConfig pub_config;
    pub_config.max_chunks = 16;
    pub_config.chunk_size = sizeof(UInt64);
    
    auto pub = Publisher<UInt64>::Create(service_name_, pub_config).Value();
    
    SubscriberConfig sub_config;
    auto sub1 = Subscriber<UInt64>::Create(service_name_, sub_config).Value();
    auto sub2 = Subscriber<UInt64>::Create(service_name_, sub_config).Value();
    auto sub3 = Subscriber<UInt64>::Create(service_name_, sub_config).Value();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Send message
    UInt64 test_value = 0xDEADBEEF;
    auto send_result = pub->SendCopy(test_value);
    ASSERT_TRUE(send_result.HasValue());
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // All subscribers should receive
    auto recv1 = sub1->Receive();
    ASSERT_TRUE(recv1.HasValue());
    EXPECT_EQ(*recv1.Value().GetPayload(), test_value);
    
    auto recv2 = sub2->Receive();
    ASSERT_TRUE(recv2.HasValue());
    EXPECT_EQ(*recv2.Value().GetPayload(), test_value);
    
    auto recv3 = sub3->Receive();
    ASSERT_TRUE(recv3.HasValue());
    EXPECT_EQ(*recv3.Value().GetPayload(), test_value);
}

// Test: Burst messaging
TEST_F(PublisherSubscriberTest, BurstMessaging)
{
    const UInt32 num_messages = 50;
    
    PublisherConfig pub_config;
    pub_config.max_chunks = 64;
    pub_config.chunk_size = sizeof(UInt32);
    
    auto pub = Publisher<UInt32>::Create(service_name_, pub_config).Value();
    
    SubscriberConfig sub_config;
    auto sub = Subscriber<UInt32>::Create(service_name_, sub_config).Value();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Send burst
    for (UInt32 i = 0; i < num_messages; ++i)
    {
        auto result = pub->SendCopy(i);
        ASSERT_TRUE(result.HasValue()) << "Failed to send message " << i;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Receive all
    UInt32 received_count = 0;
    for (UInt32 i = 0; i < num_messages; ++i)
    {
        auto recv = sub->Receive();
        if (recv.HasValue())
        {
            EXPECT_EQ(*recv.Value().GetPayload(), i);
            received_count++;
        }
    }
    
    EXPECT_EQ(received_count, num_messages);
}

// Test: SendCopy convenience API
TEST_F(PublisherSubscriberTest, SendCopyAPI)
{
    PublisherConfig pub_config;
    pub_config.max_chunks = 16;
    pub_config.chunk_size = sizeof(double);
    
    auto pub = Publisher<double>::Create(service_name_, pub_config).Value();
    
    SubscriberConfig sub_config;
    auto sub = Subscriber<double>::Create(service_name_, sub_config).Value();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    double test_value = 3.14159;
    auto send_result = pub->SendCopy(test_value);
    ASSERT_TRUE(send_result.HasValue());
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    auto recv_result = sub->Receive();
    ASSERT_TRUE(recv_result.HasValue());
    EXPECT_DOUBLE_EQ(*recv_result.Value().GetPayload(), test_value);
}

// Test: Sample RAII (automatic cleanup)
TEST_F(PublisherSubscriberTest, SampleRAII)
{
    PublisherConfig pub_config;
    pub_config.max_chunks = 4;
    pub_config.chunk_size = sizeof(UInt32);
    
    auto pub = Publisher<UInt32>::Create(service_name_, pub_config).Value();
    
    {
        auto sample1 = pub->Loan().Value();
        auto sample2 = pub->Loan().Value();
        auto sample3 = pub->Loan().Value();
        // sample1, sample2, sample3 destructed here
    }
    
    // Should be able to loan again (chunks released)
    auto sample4 = pub->Loan();
    EXPECT_TRUE(sample4.HasValue());
    auto sample5 = pub->Loan();
    EXPECT_TRUE(sample5.HasValue());
}

// Test: Multi-threaded pub-sub
TEST_F(PublisherSubscriberTest, MultiThreadedPubSub)
{
    const UInt32 num_messages = 1000;
    
    PublisherConfig pub_config;
    pub_config.max_chunks = 128;
    pub_config.chunk_size = sizeof(UInt64);
    
    auto pub = Publisher<UInt64>::Create(service_name_, pub_config).Value();
    
    SubscriberConfig sub_config;
    auto sub = Subscriber<UInt64>::Create(service_name_, sub_config).Value();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::atomic<UInt32> received_count{0};
    std::atomic<bool> pub_done{false};
    
    // Publisher thread
    std::thread pub_thread([&]() {
        for (UInt64 i = 0; i < num_messages; ++i)
        {
            while (!pub->SendCopy(i).HasValue())
            {
                std::this_thread::yield();
            }
        }
        pub_done.store(true);
    });
    
    // Subscriber thread
    std::thread sub_thread([&]() {
        while (received_count.load() < num_messages || !pub_done.load())
        {
            auto recv = sub->Receive();
            if (recv.HasValue())
            {
                received_count.fetch_add(1);
            }
            else
            {
                std::this_thread::yield();
            }
        }
    });
    
    pub_thread.join();
    sub_thread.join();
    
    EXPECT_EQ(received_count.load(), num_messages);
}

// Test: Queue overflow handling
TEST_F(PublisherSubscriberTest, QueueOverflow)
{
    PublisherConfig pub_config;
    pub_config.max_chunks = 32;
    pub_config.chunk_size = sizeof(UInt32);
    
    auto pub = Publisher<UInt32>::Create(service_name_, pub_config).Value();
    
    SubscriberConfig sub_config;
    auto sub = Subscriber<UInt32>::Create(service_name_, sub_config).Value();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Send more than queue capacity (256)
    for (UInt32 i = 0; i < 300; ++i)
    {
        pub->SendCopy(i);
    }
    
    // Should receive up to queue capacity
    UInt32 received = 0;
    while (true)
    {
        auto recv = sub->Receive();
        if (!recv.HasValue())
        {
            break;
        }
        received++;
    }
    
    EXPECT_GT(received, 0u);
    EXPECT_LE(received, 300u);
}

// Test: Late subscriber
TEST_F(PublisherSubscriberTest, LateSubscriber)
{
    PublisherConfig pub_config;
    pub_config.max_chunks = 16;
    pub_config.chunk_size = sizeof(UInt32);
    
    auto pub = Publisher<UInt32>::Create(service_name_, pub_config).Value();
    
    // Send some messages before subscriber exists
    pub->SendCopy(100u);
    pub->SendCopy(200u);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Create subscriber
    SubscriberConfig sub_config;
    auto sub = Subscriber<UInt32>::Create(service_name_, sub_config).Value();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Send new messages
    pub->SendCopy(300u);
    pub->SendCopy(400u);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Should receive only messages sent after subscription
    auto recv1 = sub->Receive();
    ASSERT_TRUE(recv1.HasValue());
    EXPECT_EQ(*recv1.Value().GetPayload(), 300u);
    
    auto recv2 = sub->Receive();
    ASSERT_TRUE(recv2.HasValue());
    EXPECT_EQ(*recv2.Value().GetPayload(), 400u);
}
