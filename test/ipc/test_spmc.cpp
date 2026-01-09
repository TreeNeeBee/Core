/**
 * @file        test_spmc.cpp
 * @brief       SPMC (Single Producer Multiple Consumers) Test
 * @date        2026-01-08
 */

#include "ipc/Publisher.hpp"
#include "ipc/Subscriber.hpp"
#include "ipc/SharedMemoryManager.hpp"
#include "ipc/SubscriberRegistryOps.hpp"
#include "CMacroDefine.hpp"
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <sys/mman.h>

// Include implementations for template instantiation
#include "../../source/src/ipc/Publisher.cpp"
#include "../../source/src/ipc/Subscriber.cpp"

using namespace lap::core;
using namespace lap::core::ipc;

struct TestMessage
{
    UInt32 sequence;
    UInt64 timestamp;
    float value;
};

void cleanup_shm(const char* name)
{
    String path = "/lightap_ipc_" + String(name);
    shm_unlink(path.c_str());
}

int main()
{
    std::cerr << "========================================" << std::endl;
    std::cerr << "  SPMC Test (Single Producer Multiple Consumers)" << std::endl;
    std::cerr << "========================================" << std::endl;
    
    const char* service_name = "test_spmc";
    cleanup_shm(service_name);
    
    const UInt32 NUM_SUBSCRIBERS = 3;
    const UInt32 NUM_MESSAGES = 10;
    
    std::atomic<bool> running{true};
    std::vector<std::atomic<UInt32>> received_counts(NUM_SUBSCRIBERS);
    for (auto& count : received_counts)
    {
        count = 0;
    }
    
    // Create subscriber threads
    std::vector<std::thread> subscriber_threads;
    for (UInt32 sub_id = 0; sub_id < NUM_SUBSCRIBERS; ++sub_id)
    {
        subscriber_threads.emplace_back([&, sub_id]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100 + sub_id * 50));
            
            auto sub_result = Subscriber<TestMessage>::Create(service_name, SubscriberConfig{});
            if (!sub_result.HasValue())
            {
                std::cerr << "  [Sub " << sub_id << "] Failed to create" << std::endl;
                return;
            }
            auto subscriber = std::move(sub_result).Value();
            std::cerr << "  [Sub " << sub_id << "] Created" << std::endl;
            
            UInt32 expected_seq = 0;
            bool first_received = false;
            
            while (running || received_counts[sub_id] < NUM_MESSAGES)
            {
                auto sample_result = subscriber.Receive(QueueEmptyPolicy::kError);
                if (sample_result.HasValue())
                {
                    auto sample = std::move(sample_result).Value();
                    const auto* data = sample.Get();
                    
                    if (!first_received)
                    {
                        expected_seq = data->sequence;
                        first_received = true;
                    }
                    
                    if (data->sequence != expected_seq)
                    {
                        std::cerr << "  [Sub " << sub_id << "] Sequence gap: expected=" 
                                  << expected_seq << ", got=" << data->sequence << std::endl;
                        expected_seq = data->sequence;
                    }
                    
                    // Print all messages for verification
                    std::cerr << "  [Sub " << sub_id << "] Received: seq=" << data->sequence 
                              << ", value=" << data->value << std::endl;
                    
                    received_counts[sub_id]++;
                    expected_seq++;
                    
                    if (received_counts[sub_id] >= NUM_MESSAGES)
                    {
                        break;
                    }
                }
                else
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
            
            std::cerr << "  [Sub " << sub_id << "] Total received: " << received_counts[sub_id] << std::endl;
        });
    }
    
    // Create publisher
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    auto pub_result = Publisher<TestMessage>::Create(service_name, PublisherConfig{});
    if (!pub_result.HasValue())
    {
        std::cerr << "Failed to create publisher" << std::endl;
        running = false;
        for (auto& thread : subscriber_threads)
        {
            thread.join();
        }
        return 1;
    }
    auto publisher = std::move(pub_result).Value();
    std::cerr << "  [Publisher] Created" << std::endl;
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Send messages
    UInt32 sent = 0;
    for (UInt32 i = 0; i < NUM_MESSAGES; ++i)
    {
        auto loan_result = publisher.Loan();
        if (loan_result.HasValue())
        {
            auto sample = std::move(loan_result).Value();
            auto* data = sample.Get();
            data->sequence = i;
            data->timestamp = i * 1000;
            data->value = i * 3.14f;
            
            auto send_result = publisher.Send(std::move(sample));
            if (send_result.HasValue())
            {
                sent++;
                // Print all messages for verification
                std::cerr << "  [Publisher] Sent: seq=" << i << std::endl;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cerr << "  [Publisher] Total sent: " << sent << std::endl;
    
    // Wait for all subscribers
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    running = false;
    
    for (auto& thread : subscriber_threads)
    {
        thread.join();
    }
    
    // Verify results
    std::cerr << "\n[Results]" << std::endl;
    bool all_passed = true;
    for (UInt32 i = 0; i < NUM_SUBSCRIBERS; ++i)
    {
        UInt32 count = received_counts[i];
        std::cerr << "  Subscriber " << i << ": " << count << "/" << NUM_MESSAGES;
        if (count == NUM_MESSAGES)
        {
            std::cerr << " ✓" << std::endl;
        }
        else
        {
            std::cerr << " ✗" << std::endl;
            all_passed = false;
        }
    }
    
    if (all_passed && sent == NUM_MESSAGES)
    {
        std::cerr << "\n✓ SPMC Test PASSED: All subscribers received all messages" << std::endl;
    }
    else
    {
        std::cerr << "\n✗ SPMC Test FAILED" << std::endl;
    }
    
    cleanup_shm(service_name);
    
    std::cerr << "\n========================================" << std::endl;
    std::cerr << "  SPMC Test Complete" << std::endl;
    std::cerr << "========================================" << std::endl;
    
    return all_passed ? 0 : 1;
}
