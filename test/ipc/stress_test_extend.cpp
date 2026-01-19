/**
 * @file        stress_test_extend.cpp
 * @brief       Stress test for EXTEND mode IPC
 * @details     High-frequency multi-process test with maximum subscribers
 *              - 62 subscribers (max for EXTEND)
 *              - 30000+ messages
 *              - Performance metrics collection
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
#include <algorithm>

using namespace lap::core;
using namespace lap::core::ipc;

constexpr const char* TEST_SHM_PATH = "/stress_test_extend";
constexpr int MESSAGE_COUNT = 30000;  // High volume for stress test
constexpr int SUBSCRIBER_COUNT = 62;  // EXTEND mode max

struct TestMessage : public Message
{
    UInt32 sequence;
    UInt64 timestamp_ns;
    UInt32 sender_id;
    UInt32 checksum;
    char payload[480];
    
    TestMessage() : sequence(0), timestamp_ns(0), sender_id(0), checksum(0) {
        std::memset(payload, 0, sizeof(payload));
    }
    
    void OnMessageSend(void* const /*chunk_ptr*/, Size /*size*/) noexcept override {}
    void OnMessageReceived(const void* const /*chunk_ptr*/, Size /*size*/) noexcept override {}
};

void publisher_process()
{
    std::cout << "[Publisher] Starting stress test (PID: " << getpid() << ")" << std::endl;
    
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    PublisherConfig config;
    config.max_chunks = 256;
    config.chunk_size = sizeof(TestMessage);
    config.policy = PublishPolicy::kOverwrite;
    
    auto pub_result = Publisher::Create(TEST_SHM_PATH, config);
    if (!pub_result) {
        std::cerr << "[Publisher] Failed to create publisher" << std::endl;
        exit(1);
    }
    
    auto publisher = std::move(pub_result).Value();
    
    auto start = std::chrono::high_resolution_clock::now();
    int sent_count = 0;
    
    for (int i = 0; i < MESSAGE_COUNT; ++i) {
        auto sample_result = publisher.Loan();
        if (!sample_result) {
            continue;  // Retry immediately without sleep
        }
        
        auto sample = std::move(sample_result).Value();
        sample.Emplace<TestMessage>();
        auto* msg = reinterpret_cast<TestMessage*>(&(*sample));
        
        msg->sequence = i;
        msg->timestamp_ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        msg->sender_id = getpid();
        msg->payload[0] = 'E';  // Simple marker instead of snprintf
        msg->checksum = msg->sequence + msg->sender_id;
        
        auto send_result = publisher.Send(std::move(sample));
        if (send_result) {
            sent_count++;
        }
        
        // Brief pause every 1000 messages to avoid overwhelming
        if (i % 1000 == 0 && i > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    std::cout << "[Publisher] Stress test completed" << std::endl;
    std::cout << "  - Sent: " << sent_count << "/" << MESSAGE_COUNT << std::endl;
    std::cout << "  - Duration: " << duration_ms << " ms" << std::endl;
    std::cout << "  - Throughput: " << (sent_count * 1000.0 / duration_ms) << " msg/s" << std::endl;
}

void subscriber_process(int subscriber_id)
{
    SubscriberConfig config;
    config.max_chunks = 256;
    config.chunk_size = sizeof(TestMessage);
    config.queue_capacity = 1024;
    config.empty_policy = SubscribePolicy::kError;
    
    auto sub_result = Subscriber::Create(TEST_SHM_PATH, config);
    if (!sub_result) {
        std::cerr << "[Subscriber-" << subscriber_id << "] Failed to create" << std::endl;
        exit(1);
    }
    
    auto subscriber = std::move(sub_result).Value();
    
    auto connect_result = subscriber.Connect();
    if (!connect_result) {
        std::cerr << "[Subscriber-" << subscriber_id << "] Failed to connect" << std::endl;
        exit(1);
    }
    
    int received_count = 0;
    int timeout_count = 0;
    UInt32 last_sequence = 0;
    int sequence_gaps = 0;
    int checksum_errors = 0;
    std::vector<uint64_t> latencies;
    latencies.reserve(MESSAGE_COUNT / 10);  // Sample 10% for latency
    
    auto start = std::chrono::high_resolution_clock::now();
    
    while (received_count < MESSAGE_COUNT && timeout_count < 3000) {
        auto sample_result = subscriber.Receive(SubscribePolicy::kError);
        
        if (!sample_result) {
            timeout_count++;
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            continue;
        }
        
        timeout_count = 0;
        auto sample = std::move(sample_result).Value();
        auto* msg = reinterpret_cast<TestMessage*>(&(*sample));
        
        // Sample latency (every 10th message)
        if (received_count % 10 == 0) {
            auto now_ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            uint64_t latency_ns = now_ns - msg->timestamp_ns;
            latencies.push_back(latency_ns);
        }
        
        // Verify checksum
        UInt32 expected_checksum = msg->sequence + msg->sender_id;
        if (msg->checksum != expected_checksum) {
            checksum_errors++;
        }
        
        if (received_count > 0 && msg->sequence != last_sequence + 1) {
            sequence_gaps++;
        }
        last_sequence = msg->sequence;
        received_count++;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());
        uint64_t avg_latency_us = 0;
        for (auto l : latencies) avg_latency_us += l;
        avg_latency_us = (avg_latency_us / latencies.size()) / 1000;
        
        uint64_t p50_latency_us = latencies[latencies.size() / 2] / 1000;
        uint64_t p99_latency_us = latencies[latencies.size() * 99 / 100] / 1000;
        
        std::cout << "[Sub-" << subscriber_id << "] Completed" << std::endl;
        std::cout << "  Received: " << received_count << "/" << MESSAGE_COUNT 
                  << " (" << (received_count * 100.0 / MESSAGE_COUNT) << "%)" << std::endl;
        std::cout << "  Duration: " << duration_ms << " ms | Throughput: " 
                  << (received_count * 1000.0 / duration_ms) << " msg/s" << std::endl;
        std::cout << "  Gaps: " << sequence_gaps << " | Checksum errors: " << checksum_errors << std::endl;
        std::cout << "  Latency (avg/p50/p99): " << avg_latency_us << "/" 
                  << p50_latency_us << "/" << p99_latency_us << " μs" << std::endl;
    }
    
    if (received_count < MESSAGE_COUNT * 0.85 || checksum_errors > 0) {
        std::cout << "[Subscriber-" << subscriber_id << "] FAILED" << std::endl;
        exit(1);
    }
}

int main()
{
    std::cout << "========================================" << std::endl;
    std::cout << "  EXTEND Mode Stress Test" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  Subscribers: " << SUBSCRIBER_COUNT << " (max)" << std::endl;
    std::cout << "  Messages: " << MESSAGE_COUNT << std::endl;
    std::cout << "  Message size: " << sizeof(TestMessage) << " bytes" << std::endl;
    std::cout << "========================================" << std::endl;
    
    Result<void> init_result = Initialize();
    if (!init_result) {
        std::cerr << "Failed to initialize Core" << std::endl;
        return 1;
    }
    
    shm_unlink(TEST_SHM_PATH);
    
    std::vector<pid_t> subscriber_pids;
    
    for (int i = 0; i < SUBSCRIBER_COUNT; ++i) {
        pid_t pid = fork();
        if (pid == 0) {
            subscriber_process(i + 1);
            exit(0);
        }
        subscriber_pids.push_back(pid);
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    
    pid_t pub_pid = fork();
    if (pub_pid == 0) {
        publisher_process();
        exit(0);
    }
    
    int status;
    int failures = 0;
    
    waitpid(pub_pid, &status, 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        failures++;
    }
    
    for (auto pid : subscriber_pids) {
        waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            failures++;
        }
    }
    
    shm_unlink(TEST_SHM_PATH);
    Deinitialize();
    
    std::cout << "\n========================================" << std::endl;
    if (failures == 0) {
        std::cout << "  ✓ EXTEND Stress Test PASSED" << std::endl;
        std::cout << "========================================" << std::endl;
        return 0;
    } else {
        std::cout << "  ✗ EXTEND Stress Test FAILED" << std::endl;
        std::cout << "    Failures: " << failures << std::endl;
        std::cout << "========================================" << std::endl;
        return 1;
    }
}
