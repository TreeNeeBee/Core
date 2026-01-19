/**
 * @file        stress_test_shrink.cpp
 * @brief       Stress test for SHRINK mode IPC
 * @details     High-frequency multi-process test with maximum subscribers
 *              - 2 subscribers (max for SHRINK)
 *              - 10000+ messages
 *              - Performance metrics collection
 */

#include "ipc/Publisher.hpp"
#include "ipc/Subscriber.hpp"
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

constexpr const char* TEST_SHM_PATH = "/stress_test_shrink";
constexpr int MESSAGE_COUNT = 5000;   // Reduced for SHRINK mode (limited queue)
constexpr int SUBSCRIBER_COUNT = 2;   // SHRINK mode max

struct TestData
{
    uint64_t sequence;
    uint64_t timestamp_ns;
    char text[16];
};

void publisher_process()
{
    std::cout << "[Publisher] Starting stress test (PID: " << getpid() << ")" << std::endl;
    
    // Wait for subscribers to be ready
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    PublisherConfig config;
    config.max_chunks = 64;
    config.chunk_size = sizeof(TestData);
    config.policy = PublishPolicy::kOverwrite;
    config.loan_policy = LoanPolicy::kBlock;
    
    auto pub_result = Publisher::Create(TEST_SHM_PATH, config);
    if (!pub_result) {
        std::cerr << "[Publisher] Failed to create publisher" << std::endl;
        exit(1);
    }
    
    auto publisher = std::move(pub_result).Value();
    
    auto start = std::chrono::high_resolution_clock::now();
    int sent_count = 0;
    
    // High-frequency sending
    for (int i = 0; i < MESSAGE_COUNT; ++i) {
        auto sample_result = publisher.Loan();
        if (!sample_result) {
            std::cerr << "[Publisher] Loan failed at " << i << std::endl;
            continue;
        }
        
        auto sample = std::move(sample_result).Value();
        
        TestData data;
        data.sequence = i;
        data.timestamp_ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        std::snprintf(data.text, sizeof(data.text), "MSG-%d", i);

        auto send_result = publisher.Send([&data]( Byte* ptr, Size size ) {
            Size copySize = sizeof(TestData) > size ? size : sizeof(TestData);
            std::memcpy(ptr, &data, copySize);
            return copySize;
        });

        if (send_result) {
            sent_count++;
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
    config.max_chunks = 64;
    config.chunk_size = sizeof(TestData);
    config.queue_capacity = 64;
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
    uint64_t last_sequence = 0;
    int sequence_gaps = 0;
    std::vector<uint64_t> latencies;
    latencies.reserve(MESSAGE_COUNT);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    while (received_count < MESSAGE_COUNT && timeout_count < 1000) {
        auto sample_result = subscriber.Receive(SubscribePolicy::kError);
        
        if (!sample_result) {
            timeout_count++;
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }
        
        timeout_count = 0;
        auto sample = std::move(sample_result).Value();
        
        TestData data;
        sample.Read(reinterpret_cast<Byte*>(&data), sizeof(TestData));
        
        // Calculate latency
        auto now_ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        uint64_t latency_ns = now_ns - data.timestamp_ns;
        latencies.push_back(latency_ns);
        
        // Check sequence
        if (received_count > 0 && data.sequence != last_sequence + 1) {
            sequence_gaps++;
        }
        last_sequence = data.sequence;
        received_count++;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    // Calculate latency statistics
    std::sort(latencies.begin(), latencies.end());
    uint64_t avg_latency_us = 0;
    for (auto l : latencies) avg_latency_us += l;
    avg_latency_us = (avg_latency_us / latencies.size()) / 1000;
    
    uint64_t p50_latency_us = latencies[latencies.size() / 2] / 1000;
    uint64_t p99_latency_us = latencies[latencies.size() * 99 / 100] / 1000;
    
    std::cout << "[Subscriber-" << subscriber_id << "] Stress test completed" << std::endl;
    std::cout << "  - Received: " << received_count << "/" << MESSAGE_COUNT << std::endl;
    std::cout << "  - Duration: " << duration_ms << " ms" << std::endl;
    std::cout << "  - Throughput: " << (received_count * 1000.0 / duration_ms) << " msg/s" << std::endl;
    std::cout << "  - Sequence gaps: " << sequence_gaps << std::endl;
    std::cout << "  - Latency (avg/p50/p99): " << avg_latency_us << "/" 
              << p50_latency_us << "/" << p99_latency_us << " μs" << std::endl;
    
    if (received_count < MESSAGE_COUNT * 0.95) {
        std::cout << "[Subscriber-" << subscriber_id << "] TEST FAILED (< 95% received)" << std::endl;
        exit(1);
    }
}

int main()
{
    std::cout << "========================================" << std::endl;
    std::cout << "  SHRINK Mode Stress Test" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  Subscribers: " << SUBSCRIBER_COUNT << " (max)" << std::endl;
    std::cout << "  Messages: " << MESSAGE_COUNT << std::endl;
    std::cout << "  Message size: " << sizeof(TestData) << " bytes" << std::endl;
    std::cout << "========================================" << std::endl;
    
    Result<void> init_result = Initialize();
    if (!init_result) {
        std::cerr << "Failed to initialize Core" << std::endl;
        return 1;
    }
    
    shm_unlink(TEST_SHM_PATH);
    
    std::vector<pid_t> subscriber_pids;
    
    // Fork subscribers
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
    
    // Wait for all processes
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
        std::cout << "  ✓ SHRINK Stress Test PASSED" << std::endl;
        std::cout << "========================================" << std::endl;
        return 0;
    } else {
        std::cout << "  ✗ SHRINK Stress Test FAILED" << std::endl;
        std::cout << "    Failures: " << failures << std::endl;
        std::cout << "========================================" << std::endl;
        return 1;
    }
}
