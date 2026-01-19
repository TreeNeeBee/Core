/**
 * @file        test_ipc_shrink.cpp
 * @brief       Multi-process IPC test for SHRINK mode
 * @details     Validates SHRINK mode with minimal footprint:
 *              - 4KB shared memory total
 *              - 1 Publisher, 2 Subscribers
 *              - Direct Sample write/read (no Message inheritance)
 *              - Multi-process communication
 */

#include "ipc/Publisher.hpp"
#include "ipc/Subscriber.hpp"
#include "ipc/IPCConfig.hpp"
#include "CInitialization.hpp"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <thread>
#include <chrono>

#ifndef LIGHTAP_IPC_MODE_SHRINK
#error "SHRINK mode not defined! Check compilation flags."
#endif

using namespace lap::core;
using namespace lap::core::ipc;

constexpr const char* TEST_SHM_PATH = "/test_ipc_shrink";
constexpr int MESSAGE_COUNT = 30;
constexpr Size SHM_SIZE = 4096;  // 4KB total

// Simple test data structure (no Message inheritance)
struct TestData
{
    UInt32 sequence;
    UInt64 timestamp;
    char text[16];  // Compact payload
};

void publisher_process()
{
    std::cout << "[Publisher] Starting (PID: " << getpid() << ")" << std::endl;
    
    // Wait for subscribers to initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Configure for 4KB shared memory
    PublisherConfig config;
    config.max_chunks = 8;       // Small chunk pool for 4KB limit
    config.chunk_size = sizeof(TestData);
    config.policy = PublishPolicy::kOverwrite;
    
    auto pub_result = Publisher::Create(TEST_SHM_PATH, config);
    if (!pub_result) {
        std::cerr << "[Publisher] Failed to create publisher: " 
                  << pub_result.Error().Message().data() << std::endl;
        exit(1);
    }
    
    auto publisher = std::move(pub_result).Value();
    std::cout << "[Publisher] Created successfully" << std::endl;
    
    // Send messages using direct Sample write
    for (int i = 0; i < MESSAGE_COUNT; ++i) {
        auto sample_result = publisher.Loan();
        if (!sample_result) {
            std::cerr << "[Publisher] Loan failed at message " << i << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        
        auto sample = std::move(sample_result).Value();
        
        // Prepare data
        TestData data;
        data.sequence = i;
        data.timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        std::snprintf(data.text, sizeof(data.text), "Msg-%d", i);
        
        // Direct write to Sample
        size_t written = sample.Write(reinterpret_cast<const Byte*>(&data), sizeof(TestData));
        if (written != sizeof(TestData)) {
            std::cerr << "[Publisher] Write incomplete: " << written << "/" << sizeof(TestData) << std::endl;
        }
        
        auto send_result = publisher.Send(std::move(sample));
        if (!send_result) {
            std::cerr << "[Publisher] Send failed at message " << i << std::endl;
        }
        
        if (i % 10 == 0 || i == MESSAGE_COUNT - 1) {
            std::cout << "[Publisher] Sent message " << i << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    
    std::cout << "[Publisher] Completed " << MESSAGE_COUNT << " messages" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void subscriber_process(int subscriber_id)
{
    std::cout << "[Subscriber-" << subscriber_id << "] Starting (PID: " << getpid() << ")" << std::endl;
    
    SubscriberConfig config;
    config.chunk_size = sizeof(TestData);
    config.empty_policy = SubscribePolicy::kError;
    
    auto sub_result = Subscriber::Create(TEST_SHM_PATH, config);
    if (!sub_result) {
        std::cerr << "[Subscriber-" << subscriber_id << "] Failed to create: " 
                  << sub_result.Error().Message().data() << std::endl;
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
    int last_sequence = -1;
    int timeout_count = 0;
    
    while (received_count < MESSAGE_COUNT && timeout_count < 150) {
        auto sample_result = subscriber.Receive(SubscribePolicy::kError);
        
        if (!sample_result) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            timeout_count++;
            continue;
        }
        
        timeout_count = 0;
        auto sample = std::move(sample_result).Value();
        
        // Direct read from Sample
        TestData data;
        size_t read_bytes = sample.Read(reinterpret_cast<Byte*>(&data), sizeof(TestData));
        if (read_bytes != sizeof(TestData)) {
            std::cerr << "[Subscriber-" << subscriber_id << "] Read incomplete: " 
                      << read_bytes << "/" << sizeof(TestData) << std::endl;
            continue;
        }
        
        received_count++;
        
        if (received_count % 10 == 0 || received_count == 1) {
            std::cout << "[Subscriber-" << subscriber_id << "] Received seq=" 
                      << data.sequence << " text=\"" << data.text << "\"" << std::endl;
        }
        
        // Check sequence order
        if (static_cast<int>(data.sequence) <= last_sequence) {
            std::cerr << "[Subscriber-" << subscriber_id << "] WARNING: Out-of-order - "
                      << "last=" << last_sequence << " current=" << data.sequence << std::endl;
        }
        last_sequence = data.sequence;
    }
    
    std::cout << "[Subscriber-" << subscriber_id << "] Completed - received " 
              << received_count << "/" << MESSAGE_COUNT << " messages" << std::endl;
    
    // Accept >70% due to overwrite policy
    if (received_count >= MESSAGE_COUNT * 0.7) {
        std::cout << "[Subscriber-" << subscriber_id << "] TEST PASSED" << std::endl;
    } else {
        std::cout << "[Subscriber-" << subscriber_id << "] TEST FAILED - only " 
                  << (received_count * 100 / MESSAGE_COUNT) << "%" << std::endl;
        exit(1);
    }
}

int main()
{
    std::cout << "========================================" << std::endl;
    std::cout << "  SHRINK Mode IPC Test (4KB Memory)" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  Configuration:" << std::endl;
    std::cout << "    - Shared Memory: 4KB" << std::endl;
    std::cout << "    - Processes: 1 Publisher + 2 Subscribers" << std::endl;
    std::cout << "    - Messages: " << MESSAGE_COUNT << std::endl;
    std::cout << "    - Data Size: " << sizeof(TestData) << " bytes" << std::endl;
    std::cout << "  Compiled Mode:" << std::endl;
#ifdef LIGHTAP_IPC_MODE_SHRINK
    std::cout << "    - SHRINK mode: ENABLED" << std::endl;
    std::cout << "    - kMaxSubscribers: " << ipc::kMaxSubscribers << std::endl;
    std::cout << "    - kQueueCapacity: " << ipc::kQueueCapacity << std::endl;
#else
    std::cout << "    - SHRINK mode: NOT DEFINED!" << std::endl;
#endif
    std::cout << "========================================" << std::endl;
    
    // Initialize Core
    Result<void> init_result = Initialize();
    if (!init_result) {
        std::cerr << "ERROR: Failed to initialize Core" << std::endl;
        return 1;
    }
    
    // Clean up any existing shared memory
    shm_unlink(TEST_SHM_PATH);
    
    // Fork Subscriber 1
    pid_t sub1_pid = fork();
    if (sub1_pid == 0) {
        subscriber_process(1);
        exit(0);
    }
    
    // Fork Subscriber 2
    pid_t sub2_pid = fork();
    if (sub2_pid == 0) {
        subscriber_process(2);
        exit(0);
    }
    
    // Fork Publisher
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
        std::cerr << "Publisher exited with error" << std::endl;
        failures++;
    }
    
    waitpid(sub1_pid, &status, 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        std::cerr << "Subscriber-1 exited with error" << std::endl;
        failures++;
    }
    
    waitpid(sub2_pid, &status, 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        std::cerr << "Subscriber-2 exited with error" << std::endl;
        failures++;
    }
    
    // Cleanup
    shm_unlink(TEST_SHM_PATH);
    Deinitialize();
    
    std::cout << "\n========================================" << std::endl;
    if (failures == 0) {
        std::cout << "  ✓ SHRINK Mode Test PASSED" << std::endl;
        std::cout << "========================================" << std::endl;
        return 0;
    } else {
        std::cout << "  ✗ SHRINK Mode Test FAILED" << std::endl;
        std::cout << "    Failures: " << failures << std::endl;
        std::cout << "========================================" << std::endl;
        return 1;
    }
}
