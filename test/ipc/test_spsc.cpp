/**
 * @file        test_spsc.cpp
 * @brief       SPSC (Single Producer Single Consumer) Test
 * @date        2026-01-08
 */

#include "ipc/Publisher.hpp"
#include "ipc/Subscriber.hpp"
#include "ipc/SharedMemoryManager.hpp"
#include "ipc/SubscriberRegistryOps.hpp"
#include "CMacroDefine.hpp"
#include <iostream>
#include <thread>
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
    std::cerr << "  SPSC Test (Single Producer Single Consumer)" << std::endl;
    std::cerr << "========================================" << std::endl;
    
    const char* service_name = "test_spsc";
    cleanup_shm(service_name);
    
    // Test configuration
    const UInt32 NUM_MESSAGES = 10;
    
    // ========================================================================
    // SP0C: Single Producer, Zero Consumer
    // ========================================================================
    std::cerr << "\n[Test 1] SP0C - Publisher without Subscriber" << std::endl;
    std::cerr << "--------------------------------------------" << std::endl;
    
    {
        auto pub_result = Publisher<TestMessage>::Create(service_name, PublisherConfig{});
        if (!pub_result.HasValue())
        {
            std::cerr << "Failed to create publisher" << std::endl;
            return 1;
        }
        auto publisher = std::move(pub_result).Value();
        std::cerr << "✓ Publisher created" << std::endl;
        
        // Send messages with no subscribers
        UInt32 sent = 0;
        for (UInt32 i = 0; i < 5; ++i)
        {
            auto loan_result = publisher.Loan();
            if (loan_result.HasValue())
            {
                auto sample = std::move(loan_result).Value();
                auto* data = sample.Get();
                data->sequence = i;
                data->timestamp = i * 1000;
                data->value = i * 1.5f;
                
                auto send_result = publisher.Send(std::move(sample));
                if (send_result.HasValue())
                {
                    sent++;
                }
            }
        }
        std::cerr << "✓ Sent " << sent << "/5 messages (no subscribers)" << std::endl;
    }
    
    cleanup_shm(service_name);
    
    // ========================================================================
    // SPSC: Single Producer, Single Consumer
    // ========================================================================
    std::cerr << "\n[Test 2] SPSC - 1 Publisher + 1 Subscriber" << std::endl;
    std::cerr << "--------------------------------------------" << std::endl;
    
    std::atomic<bool> running{true};
    std::atomic<UInt32> received_count{0};
    
    // Create subscriber thread
    std::thread subscriber_thread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        auto sub_result = Subscriber<TestMessage>::Create(service_name, SubscriberConfig{});
        if (!sub_result.HasValue())
        {
            std::cerr << "Failed to create subscriber" << std::endl;
            return;
        }
        auto subscriber = std::move(sub_result).Value();
        std::cerr << "  [Subscriber] Created" << std::endl;
        
        UInt32 expected_seq = 0;
        while (running || received_count < NUM_MESSAGES)
        {
            auto sample_result = subscriber.Receive(QueueEmptyPolicy::kError);
            if (sample_result.HasValue())
            {
                auto sample = std::move(sample_result).Value();
                const auto* data = sample.Get();
                
                if (data->sequence != expected_seq)
                {
                    std::cerr << "  [Subscriber] ERROR: Expected seq=" << expected_seq 
                              << ", got " << data->sequence << std::endl;
                }
                else if (received_count < 3 || received_count == NUM_MESSAGES - 1)
                {
                    std::cerr << "  [Subscriber] Received: seq=" << data->sequence 
                              << ", value=" << data->value << std::endl;
                }
                
                received_count++;
                expected_seq++;
                
                if (received_count >= NUM_MESSAGES)
                {
                    break;
                }
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        
        std::cerr << "  [Subscriber] Total received: " << received_count << std::endl;
    });
    
    // Create publisher and send messages
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    auto pub_result = Publisher<TestMessage>::Create(service_name, PublisherConfig{});
    if (!pub_result.HasValue())
    {
        std::cerr << "Failed to create publisher" << std::endl;
        running = false;
        subscriber_thread.join();
        return 1;
    }
    auto publisher = std::move(pub_result).Value();
    std::cerr << "  [Publisher] Created" << std::endl;
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
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
            data->value = i * 2.5f;
            
            auto send_result = publisher.Send(std::move(sample));
            if (send_result.HasValue())
            {
                sent++;
                if (i < 3 || i == NUM_MESSAGES - 1)
                {
                    std::cerr << "  [Publisher] Sent: seq=" << i << std::endl;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    std::cerr << "  [Publisher] Total sent: " << sent << std::endl;
    
    // Wait for subscriber
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    running = false;
    subscriber_thread.join();
    
    // Verify
    if (received_count == sent && sent == NUM_MESSAGES)
    {
        std::cerr << "✓ SPSC Test PASSED: " << received_count << "/" << NUM_MESSAGES << " messages" << std::endl;
    }
    else
    {
        std::cerr << "✗ SPSC Test FAILED: Sent=" << sent << ", Received=" << received_count 
                  << ", Expected=" << NUM_MESSAGES << std::endl;
    }
    
    cleanup_shm(service_name);
    
    std::cerr << "\n========================================" << std::endl;
    std::cerr << "  SPSC Test Complete" << std::endl;
    std::cerr << "========================================" << std::endl;
    
    return (received_count == NUM_MESSAGES) ? 0 : 1;
}
