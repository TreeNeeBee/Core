
/**
 * @file        test_ipc_extend.cpp
 * @brief       Multi-process IPC test for EXTEND mode
 * @details     Validates EXTEND mode constraints and functionality
 *              - Max 64 subscribers
 *              - Queue capacity: 512
 *              - Large chunk pool support
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

constexpr const char* TEST_SHM_PATH = "/test_ipc_extend";
constexpr int MESSAGE_COUNT = 300;
constexpr int SUBSCRIBER_COUNT = 62;  // Test with 62 subscribers (EXTEND mode max)

// Test message structure - larger for EXTEND mode
struct TestMessage : public Message
{
    UInt32 sequence;
    UInt64 timestamp;
    UInt32 sender_id;
    UInt32 checksum;
    char payload[512];  // Large payload for EXTEND mode
    
    TestMessage() : sequence(0), timestamp(0), sender_id(0), checksum(0) {
        std::memset(payload, 0, sizeof(payload));
    }
    
    UInt32 ComputeChecksum() const {
        UInt32 sum = sequence + sender_id;
        for (size_t i = 0; i < sizeof(payload); i += 4) {
            sum += *reinterpret_cast<const UInt32*>(payload + i);
        }
        return sum;
    }
    
    // Implement pure virtual functions from Message
    void OnMessageSend(void* const /*chunk_ptr*/, Size /*size*/) noexcept override {
        // No special action needed for this test
    }
    
    void OnMessageReceived(const void* const /*chunk_ptr*/, Size /*size*/) noexcept override {
        // No special action needed for this test
    }
};

void publisher_process()
{
    std::cout << "[Publisher] Starting (PID: " << getpid() << ")" << std::endl;
    
    // Wait for subscribers to be ready
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    PublisherConfig config;
    config.max_chunks = 128;  // EXTEND mode allows large chunk pool
    config.chunk_size = sizeof(TestMessage);
    config.policy = PublishPolicy::kOverwrite;
    
    auto pub_result = Publisher::Create(TEST_SHM_PATH, config);
    if (!pub_result) {
        std::cerr << "[Publisher] Failed to create publisher" << std::endl;
        exit(1);
    }
    
    auto publisher = std::move(pub_result).Value();
    std::cout << "[Publisher] Created successfully" << std::endl;
    std::cout << "[Publisher] Max chunks: " << config.max_chunks << std::endl;
    
    int sent_count = 0;
    int loan_failures = 0;
    
    // Send messages
    for (int i = 0; i < MESSAGE_COUNT; ++i) {
        auto sample_result = publisher.Loan();
        if (!sample_result) {
            loan_failures++;
            std::cerr << "[Publisher] Loan failed at message " << i << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        
        auto sample = std::move(sample_result).Value();
        
        // Use Emplace to construct TestMessage in-place
        sample.Emplace<TestMessage>();
        auto* msg = reinterpret_cast<TestMessage*>(&(*sample));
        
        msg->sequence = i;
        msg->timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        msg->sender_id = getpid();
        
        // Fill payload with pattern
        std::snprintf(msg->payload, sizeof(msg->payload), 
                     "EXTEND-Mode-Large-Payload-Message-%d-From-PID-%d-", i, msg->sender_id);
        
        // Fill rest with data pattern
        size_t len = std::strlen(msg->payload);
        for (size_t j = len; j < sizeof(msg->payload) - 1; j++) {
            msg->payload[j] = 'A' + (j % 26);
        }
        msg->payload[sizeof(msg->payload) - 1] = '\0';
        
        msg->checksum = msg->ComputeChecksum();
        
        auto send_result = publisher.Send(std::move(sample));
        if (!send_result) {
            std::cerr << "[Publisher] Send failed at message " << i << std::endl;
        } else {
            sent_count++;
        }
        
        if (i % 100 == 0) {
            std::cout << "[Publisher] Progress: " << sent_count << "/" << MESSAGE_COUNT 
                      << " (failures: " << loan_failures << ")" << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    
    std::cout << "[Publisher] Statistics:" << std::endl;
    std::cout << "  - Sent: " << sent_count << "/" << MESSAGE_COUNT << std::endl;
    std::cout << "  - Loan failures: " << loan_failures << std::endl;
    
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
}

void subscriber_process(int subscriber_id)
{
    std::cout << "[Subscriber-" << subscriber_id << "] Starting (PID: " << getpid() << ")" << std::endl;
    
    SubscriberConfig config;
    config.max_chunks = 128;
    config.chunk_size = sizeof(TestMessage);
    config.queue_capacity = 512;  // EXTEND mode capacity
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
        std::cerr << "[Subscriber-" << subscriber_id << "] Failed to connect" << std::endl;
        exit(1);
    }
    std::cout << "[Subscriber-" << subscriber_id << "] Connected successfully" << std::endl;
    
    int received_count = 0;
    UInt32 last_sequence = 0;
    int timeout_count = 0;
    int checksum_errors = 0;
    int sequence_gaps = 0;
    
    while (received_count < MESSAGE_COUNT && timeout_count < 300) {
        auto sample_result = subscriber.Receive(SubscribePolicy::kError);
        
        if (!sample_result) {
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
            timeout_count++;
            continue;
        }
        
        timeout_count = 0;
        auto sample = std::move(sample_result).Value();
        auto* msg = reinterpret_cast<TestMessage*>(&(*sample));
        
        received_count++;
        
        // Verify checksum
        UInt32 computed_checksum = msg->ComputeChecksum();
        if (computed_checksum != msg->checksum) {
            checksum_errors++;
            std::cerr << "[Subscriber-" << subscriber_id << "] Checksum error at seq " 
                      << msg->sequence << std::endl;
        }
        
        // Check for sequence gaps
        if (msg->sequence > last_sequence + 1 && last_sequence > 0) {
            sequence_gaps++;
        }
        last_sequence = msg->sequence;
        
        if (received_count % 100 == 0) {
            std::cout << "[Subscriber-" << subscriber_id << "] Received " 
                      << received_count << " messages (seq: " << msg->sequence << ")" << std::endl;
        }
    }
    
    std::cout << "[Subscriber-" << subscriber_id << "] Statistics:" << std::endl;
    std::cout << "  - Received: " << received_count << "/" << MESSAGE_COUNT << std::endl;
    std::cout << "  - Last sequence: " << last_sequence << std::endl;
    std::cout << "  - Sequence gaps: " << sequence_gaps << std::endl;
    std::cout << "  - Checksum errors: " << checksum_errors << std::endl;
    
    double receive_rate = (received_count * 100.0) / MESSAGE_COUNT;
    bool passed = (receive_rate >= 70.0) && (checksum_errors == 0);
    
    if (passed) {
        std::cout << "[Subscriber-" << subscriber_id << "] TEST PASSED (" 
                  << receive_rate << "%, no checksum errors)" << std::endl;
    } else {
        std::cout << "[Subscriber-" << subscriber_id << "] TEST FAILED (" 
                  << receive_rate << "%, " << checksum_errors << " checksum errors)" << std::endl;
        exit(1);
    }
}

int main()
{
    std::cout << "========================================" << std::endl;
    std::cout << "  EXTEND Mode IPC Test (62 Subscribers)" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  Configuration:" << std::endl;
    std::cout << "    - Max Subscribers: 62" << std::endl;
    std::cout << "    - Queue Capacity: 1024" << std::endl;
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
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
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
        std::cout << "  ✓ EXTEND Mode Test PASSED" << std::endl;
        std::cout << "    All " << SUBSCRIBER_COUNT << " subscribers received messages" << std::endl;
        std::cout << "========================================" << std::endl;
        return 0;
    } else {
        std::cout << "  ✗ EXTEND Mode Test FAILED" << std::endl;
        std::cout << "    Failures: " << failures << std::endl;
        std::cout << "========================================" << std::endl;
        return 1;
    }
}
