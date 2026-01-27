/**
 * @file test_event_basic.cpp
 * @brief Basic test for Event messaging system
 * @date 2026-01-02
 */

#include <gtest/gtest.h>
#include "memory/CEvent.hpp"
#include <thread>
#include <chrono>
#include <cstring>

using namespace lap::core;
using namespace std::chrono_literals;

// ============================================================================
// Test Fixture
// ============================================================================

class EventTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.event_name = "test_event";
        config_.payload_size = 256;
        config_.max_chunks = 16;
        config_.max_channels = 4;
        config_.subscriber_queue_capacity = 8;
        config_.use_shm_for_queues = false;
    }
    
    EventConfig config_;
};

// ============================================================================
// Basic Tests
// ============================================================================

TEST_F(EventTest, CreateEvent) {
    Event event(config_);
    EXPECT_TRUE(event.isInitialized());
    EXPECT_EQ(event.getName(), "test_event");
    EXPECT_EQ(event.getPayloadSize(), 256u);
}

TEST_F(EventTest, CreatePublisher) {
    Event event(config_);
    auto publisher = event.createPublisher();
    
    ASSERT_NE(publisher, nullptr);
    EXPECT_EQ(publisher->getEventName(), "test_event");
}

TEST_F(EventTest, CreateSubscriber) {
    Event event(config_);
    auto subscriber = event.createSubscriber();
    
    ASSERT_NE(subscriber, nullptr);
    EXPECT_EQ(subscriber->getEventName(), "test_event");
}

TEST_F(EventTest, LoanAndRelease) {
    Event event(config_);
    auto publisher = event.createPublisher();
    ASSERT_NE(publisher, nullptr);
    
    // Loan a sample
    auto sample_result = publisher->loan();
    ASSERT_TRUE(sample_result.HasValue());
    
    Sample sample = sample_result.Value();
    EXPECT_TRUE(sample.isValid());
    EXPECT_EQ(sample.size(), 256u);
    EXPECT_NE(sample.data(), nullptr);
    
    // Release without sending
    publisher->release(sample);
    EXPECT_FALSE(sample.isValid());
}

TEST_F(EventTest, SendAndReceive) {
    Event event(config_);
    auto publisher = event.createPublisher();
    auto subscriber = event.createSubscriber();
    
    ASSERT_NE(publisher, nullptr);
    ASSERT_NE(subscriber, nullptr);
    
    // Loan, write, and send
    auto loan_result = publisher->loan();
    ASSERT_TRUE(loan_result.HasValue());
    
    Sample send_sample = loan_result.Value();
    const char* test_data = "Hello Event System!";
    std::memcpy(send_sample.data(), test_data, strlen(test_data) + 1);
    
    auto send_result = publisher->send(send_sample);
    ASSERT_TRUE(send_result.HasValue());
    EXPECT_FALSE(send_sample.isValid());  // Sample invalidated after send
    
    // Receive
    auto recv_result = subscriber->receive();
    ASSERT_TRUE(recv_result.HasValue());
    
    Sample recv_sample = recv_result.Value();
    EXPECT_TRUE(recv_sample.isValid());
    EXPECT_STREQ(static_cast<const char*>(recv_sample.data()), test_data);
    
    // Release
    subscriber->release(recv_sample);
    EXPECT_FALSE(recv_sample.isValid());
}

TEST_F(EventTest, BroadcastToMultipleSubscribers) {
    Event event(config_);
    auto publisher = event.createPublisher();
    auto sub1 = event.createSubscriber();
    auto sub2 = event.createSubscriber();
    auto sub3 = event.createSubscriber();
    
    ASSERT_NE(publisher, nullptr);
    ASSERT_NE(sub1, nullptr);
    ASSERT_NE(sub2, nullptr);
    ASSERT_NE(sub3, nullptr);
    
    // Send one message
    auto loan_result = publisher->loan();
    ASSERT_TRUE(loan_result.HasValue());
    
    Sample sample = loan_result.Value();
    int* value = static_cast<int*>(sample.data());
    *value = 42;
    
    ASSERT_TRUE(publisher->send(sample).HasValue());
    
    // All 3 subscribers should receive
    auto recv1 = sub1->receive();
    auto recv2 = sub2->receive();
    auto recv3 = sub3->receive();
    
    ASSERT_TRUE(recv1.HasValue());
    ASSERT_TRUE(recv2.HasValue());
    ASSERT_TRUE(recv3.HasValue());
    
    // Extract samples (non-const)
    Sample s1 = recv1.Value();
    Sample s2 = recv2.Value();
    Sample s3 = recv3.Value();
    
    EXPECT_EQ(*static_cast<int*>(s1.data()), 42);
    EXPECT_EQ(*static_cast<int*>(s2.data()), 42);
    EXPECT_EQ(*static_cast<int*>(s3.data()), 42);
    
    // Release all
    sub1->release(s1);
    sub2->release(s2);
    sub3->release(s3);
}

TEST_F(EventTest, HasData) {
    Event event(config_);
    auto publisher = event.createPublisher();
    auto subscriber = event.createSubscriber();
    
    ASSERT_NE(publisher, nullptr);
    ASSERT_NE(subscriber, nullptr);
    
    // No data initially
    EXPECT_FALSE(subscriber->hasData());
    
    // Send message
    auto sample = publisher->loan();
    ASSERT_TRUE(sample.HasValue());
    Sample s = sample.Value();
    ASSERT_TRUE(publisher->send(s).HasValue());
    
    // Should have data now
    EXPECT_TRUE(subscriber->hasData());
    
    // Receive and release
    auto recv = subscriber->receive();
    ASSERT_TRUE(recv.HasValue());
    Sample rs = recv.Value();
    subscriber->release(rs);
    
    // No data after consumption
    EXPECT_FALSE(subscriber->hasData());
}

TEST_F(EventTest, MultipleMessages) {
    Event event(config_);
    auto publisher = event.createPublisher();
    auto subscriber = event.createSubscriber();
    
    ASSERT_NE(publisher, nullptr);
    ASSERT_NE(subscriber, nullptr);
    
    // Send 5 messages
    for (int i = 0; i < 5; ++i) {
        auto sample = publisher->loan();
        ASSERT_TRUE(sample.HasValue());
        
        Sample s = sample.Value();
        int* data = static_cast<int*>(s.data());
        *data = i * 10;
        
        ASSERT_TRUE(publisher->send(s).HasValue());
    }
    
    // Receive and verify all 5
    for (int i = 0; i < 5; ++i) {
        auto recv = subscriber->receive();
        ASSERT_TRUE(recv.HasValue());
        
        Sample s = recv.Value();
        EXPECT_EQ(*static_cast<int*>(s.data()), i * 10);
        subscriber->release(s);
    }
    
    // No more data
    EXPECT_FALSE(subscriber->hasData());
}

TEST_F(EventTest, PoolExhaustion) {
    // NOTE: With segment-based allocator, the pool will grow dynamically.
    // To test pool exhaustion, we need to loan more than publisher's max_loaned_samples limit.
    config_.max_chunks = 16;  // Publisher max_loaned_samples = 16
    Event event(config_);
    auto publisher = event.createPublisher();
    
    ASSERT_NE(publisher, nullptr);
    
    // Loan up to publisher's limit (max_loaned_samples = 16)
    std::vector<Sample> samples;
    for (int i = 0; i < 16; ++i) {
        auto result = publisher->loan();
        ASSERT_TRUE(result.HasValue()) << "Failed to loan sample " << i;
        samples.push_back(result.Value());
    }
    
    // Next loan should fail (publisher's loan counter exhausted)
    auto fail_result = publisher->loan();
    EXPECT_FALSE(fail_result.HasValue()) << "Expected loan to fail after 16 samples";
    
    // Release one sample
    publisher->release(samples[0]);
    
    // Now loan should succeed
    auto success_result = publisher->loan();
    EXPECT_TRUE(success_result.HasValue()) << "Expected loan to succeed after release";
}

TEST_F(EventTest, Statistics) {
    Event event(config_);
    auto publisher = event.createPublisher();
    auto subscriber = event.createSubscriber();
    
    // Send and receive one message
    auto sample = publisher->loan();
    ASSERT_TRUE(sample.HasValue());
    Sample s = sample.Value();
    publisher->send(s);
    
    auto recv = subscriber->receive();
    ASSERT_TRUE(recv.HasValue());
    Sample rs = recv.Value();
    subscriber->release(rs);
    
    // Check stats
    SharedMemoryAllocatorStats stats;
    event.getStats(stats);
    
    EXPECT_GT(stats.total_sends, 0u);
    EXPECT_GT(stats.total_receives, 0u);
    EXPECT_GT(stats.total_releases, 0u);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
