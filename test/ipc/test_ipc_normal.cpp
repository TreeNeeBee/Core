/**
 * @file        test_ipc_normal.cpp
 * @brief       Multi-process IPC test for NORMAL mode
 * @details     Validates NORMAL mode constraints and functionality
 *              - Max 32 subscribers
 *              - Queue capacity: 256
 *              - Chunk pool: configurable
 */

#include "ipc/Publisher.hpp"
#include "ipc/Subscriber.hpp"
#include "ipc/Message.hpp"
#include "CInitialization.hpp"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <vector>
#include <thread>
#include <chrono>

using namespace lap::core;

using namespace lap::core;
using namespace lap::core::ipc;

constexpr const char* TEST_SHM_PATH = "/test_ipc_normal";
constexpr int MESSAGE_COUNT = 200;
constexpr int SUBSCRIBER_COUNT = 30;  // Test with 30 subscribers (NORMAL mode max)

// Test message structure
class TestMessage : public Message
{
public: 
    UInt32 sequence_;
    UInt64 timestamp_;
    UInt32 sender_id_;
    char payload[128];  // Larger payload for NORMAL mode

    TestMessage(UInt32 sequence, UInt64 timestamp, UInt32 sender_id)
        : sequence_(sequence)
        , timestamp_(timestamp)
        , sender_id_(sender_id)
    {
        std::memset( payload, 0, sizeof( payload ) );
    }
};

void publisher_process()
{
    std::cout << "[Publisher] Starting (PID: " << getpid() << ")" << std::endl;
    
    // Wait for subscribers to be ready
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    PublisherConfig config;
    config.max_chunks = 64;  // NORMAL mode allows more chunks
    config.chunk_size = sizeof(TestMessage);
    config.policy = PublishPolicy::kOverwrite;
    
    auto pub_result = Publisher::Create(TEST_SHM_PATH, config);
    if (!pub_result) {
        std::cerr << "[Publisher] Failed to create publisher" << std::endl;
        exit(1);
    }
    
    auto publisher = std::move(pub_result).Value();
    std::cout << "[Publisher] Created successfully" << std::endl;
    std::cout << "[Publisher] Allocated chunks: " << publisher.GetAllocatedCount() << std::endl;
    
    // Send messages
    for (int i = 0; i < MESSAGE_COUNT; ++i) {
        auto sample_result = publisher.Loan();
        if (!sample_result) {
            std::cerr << "[Publisher] Loan failed at message " << i << std::endl;
            continue;
        }
        
        auto sample = std::move(sample_result).Value();
        
        // Use Emplace to construct TestMessage in-place
        sample.Emplace<TestMessage>(i, std::chrono::steady_clock::now().time_since_epoch().count(), getpid());

        auto send_result = publisher.Send(std::move(sample));
        if (!send_result) {
            std::cerr << "[Publisher] Send failed at message " << i << std::endl;
        }
        
        if (i % 50 == 0) {
            std::cout << "[Publisher] Progress: " << i << "/" << MESSAGE_COUNT << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    
    std::cout << "[Publisher] Completed sending " << MESSAGE_COUNT << " messages" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
}

void subscriber_process(int subscriber_id)
{
    std::cout << "[Subscriber-" << subscriber_id << "] Starting (PID: " << getpid() << ")" << std::endl;
    
    SubscriberConfig config;
    config.max_chunks = 64;
    config.chunk_size = sizeof(TestMessage);
    config.channel_capacity = 256;  // NORMAL mode capacity
    config.empty_policy = SubscribePolicy::kError;
    
    auto sub_result = Subscriber::Create(TEST_SHM_PATH, config);
    if (!sub_result) {
        std::cerr << "[Subscriber-" << subscriber_id << "] Failed to create subscriber" << std::endl;
        exit(1);
    }
    
    auto subscriber = std::move(sub_result).Value();
    std::cout << "[Subscriber-" << subscriber_id << "] Created successfully" << std::endl;
    
    // Connect to publisher
    auto connect_result = subscriber.Connect();
    if (!connect_result) {
        std::cerr << "[Subscriber-" << subscriber_id << "] Failed to connect: "
                  << connect_result.Error().Message().data() << std::endl;
        exit(1);
    }
    std::cout << "[Subscriber-" << subscriber_id << "] Connected successfully" << std::endl;
    
    int received_count = 0;
    UInt32 last_sequence = 0;
    int timeout_count = 0;
    int sequence_gaps = 0;
    
    while (received_count < MESSAGE_COUNT && timeout_count < 200) {
        auto sample_result = subscriber.Receive(SubscribePolicy::kError);
        
        if (!sample_result) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            timeout_count++;
            continue;
        }
        
        timeout_count = 0;
        auto sample = std::move(sample_result).Value();
        auto* msg = sample.Payload<TestMessage>();
        
        received_count++;
        
        // Check for sequence gaps (acceptable due to overwrite policy)
        if (msg->sequence_ > last_sequence + 1 && last_sequence > 0) {
            sequence_gaps++;
        }
        last_sequence = msg->sequence_;
        
        if (received_count % 50 == 0) {
            std::cout << "[Subscriber-" << subscriber_id << "] Received " 
                      << received_count << " messages (seq: " << msg->sequence_ << ")" << std::endl;
        }
    }
    
    std::cout << "[Subscriber-" << subscriber_id << "] Statistics:" << std::endl;
    std::cout << "  - Received: " << received_count << "/" << MESSAGE_COUNT << std::endl;
    std::cout << "  - Last sequence: " << last_sequence << std::endl;
    std::cout << "  - Sequence gaps: " << sequence_gaps << std::endl;
    
    double receive_rate = (received_count * 100.0) / MESSAGE_COUNT;
    if (receive_rate >= 75.0) {
        std::cout << "[Subscriber-" << subscriber_id << "] TEST PASSED (" 
                  << receive_rate << "%)" << std::endl;
    } else {
        std::cout << "[Subscriber-" << subscriber_id << "] TEST FAILED (" 
                  << receive_rate << "%)" << std::endl;
        exit(1);
    }
}

int main()
{
    std::cout << "========================================" << std::endl;
    std::cout << "  NORMAL Mode IPC Test (30 Subscribers)" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  Configuration:" << std::endl;
    std::cout << "    - Max Subscribers: 30" << std::endl;
    std::cout << "    - Queue Capacity: 256" << std::endl;
    std::cout << "    - Processes: 1 Publisher + " << SUBSCRIBER_COUNT << " Subscribers" << std::endl;
    std::cout << "    - Messages: " << MESSAGE_COUNT << std::endl;
    std::cout << "    - Using Emplace for Message construction" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // Initialize Core
    Result<void> init_result = Initialize();
    if (!init_result) {
        std::cerr << "Failed to initialize Core" << std::endl;
        return 1;
    }
    
    // Clean up any existing shared memory
    shm_unlink(TEST_SHM_PATH);
    
    std::vector<pid_t> subscriber_pids;
    
    // Fork multiple subscribers
    for (int i = 0; i < SUBSCRIBER_COUNT; ++i) {
        pid_t pid = fork();
        if (pid == 0) {
            subscriber_process(i + 1);
            exit(0);
        }
        subscriber_pids.push_back(pid);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // Fork publisher
    pid_t pub_pid = fork();
    if (pub_pid == 0) {
        publisher_process();
        exit(0);
    }
    
    // Wait for all child processes
    int status;
    int failures = 0;
    
    waitpid(pub_pid, &status, 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        failures++;
        std::cout << "Publisher process failed" << std::endl;
    }
    
    for (size_t i = 0; i < subscriber_pids.size(); ++i) {
        waitpid(subscriber_pids[i], &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            failures++;
            std::cout << "Subscriber " << (i+1) << " process failed" << std::endl;
        }
    }
    
    // Cleanup
    shm_unlink(TEST_SHM_PATH);
    Deinitialize();
    
    std::cout << "\n========================================" << std::endl;
    if (failures == 0) {
        std::cout << "  ✓ NORMAL Mode Test PASSED" << std::endl;
        std::cout << "    All " << SUBSCRIBER_COUNT << " subscribers received messages" << std::endl;
        std::cout << "========================================" << std::endl;
        return 0;
    } else {
        std::cout << "  ✗ NORMAL Mode Test FAILED" << std::endl;
        std::cout << "    Failures: " << failures << std::endl;
        std::cout << "========================================" << std::endl;
        return 1;
    }
}
