/**
 * @file        end_to_end_example.cpp
 * @author      LightAP Core Team
 * @brief       End-to-end Publisher-Subscriber messaging example
 * @date        2026-01-07
 * @details     Demonstrates complete zero-copy IPC with message distribution
 * @copyright   Copyright (c) 2026
 */

#include "ipc/Publisher.hpp"
#include "ipc/Subscriber.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>

using namespace lap::core;
using namespace lap::core::ipc;

// Test message structure
struct SensorData
{
    UInt64 timestamp;
    Float temperature;
    Float pressure;
    UInt32 sensor_id;
    
    void Print() const
    {
        std::cout << "  timestamp: " << timestamp 
                  << ", temp: " << temperature << "°C"
                  << ", pressure: " << pressure << "kPa"
                  << ", sensor_id: " << sensor_id << std::endl;
    }
};

void PublisherThread()
{
    std::cout << "[Publisher] Starting..." << std::endl;
    
    // Create publisher
    PublisherConfig config{};
    config.max_chunks = 32;
    config.chunk_size = sizeof(SensorData);
    config.loan_policy = LoanFailurePolicy::kError;
    config.auto_cleanup = false;
    
    auto pub_result = Publisher<SensorData>::Create("sensor_stream", config);
    if (!pub_result)
    {
        std::cerr << "[Publisher] Failed to create" << std::endl;
        return;
    }
    
    auto publisher = std::move(pub_result.Value());
    std::cout << "[Publisher] Created successfully" << std::endl;
    
    // Wait for subscribers to connect
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Publish messages
    for (UInt32 i = 0; i < 10; ++i)
    {
        auto sample_result = publisher.Loan();
        if (!sample_result)
        {
            std::cerr << "[Publisher] Failed to loan chunk" << std::endl;
            continue;
        }
        
        auto sample = std::move(sample_result.Value());
        
        // Fill data
        sample->timestamp = i * 100;
        sample->temperature = 25.0f + i * 0.5f;
        sample->pressure = 101.3f + i * 0.1f;
        sample->sensor_id = 100;
        
        std::cout << "[Publisher] Sending message " << i << ":" << std::endl;
        sample->Print();
        
        // Send to all subscribers
        auto send_result = publisher.Send(std::move(sample), QueueFullPolicy::kDrop);
        if (!send_result)
        {
            std::cerr << "[Publisher] Failed to send" << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "[Publisher] Finished sending " << 10 << " messages" << std::endl;
}

void SubscriberThread(UInt32 sub_id)
{
    std::cout << "[Subscriber " << sub_id << "] Starting..." << std::endl;
    
    // Create subscriber
    SubscriberConfig config{};
    config.queue_capacity = 256;
    config.empty_policy = QueueEmptyPolicy::kBlock;
    
    auto sub_result = Subscriber<SensorData>::Create("sensor_stream", config);
    if (!sub_result)
    {
        std::cerr << "[Subscriber " << sub_id << "] Failed to create" << std::endl;
        return;
    }
    
    auto subscriber = std::move(sub_result.Value());
    std::cout << "[Subscriber " << sub_id << "] Created successfully" << std::endl;
    
    // Receive messages
    UInt32 received = 0;
    
    while (received < 10)
    {
        auto sample_result = subscriber.Receive();
        if (!sample_result)
        {
            // Timeout or error
            auto error = sample_result.Error();
            if (error.Value() == static_cast<Int32>(CoreErrc::kIPCQueueEmpty))
            {
                std::cout << "[Subscriber " << sub_id << "] Queue empty, waiting..." << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            else
            {
                std::cerr << "[Subscriber " << sub_id << "] Receive error" << std::endl;
                break;
            }
        }
        
        auto sample = std::move(sample_result.Value());
        
        std::cout << "[Subscriber " << sub_id << "] Received message " << received << ":" << std::endl;
        sample->Print();
        
        ++received;
    }
    
    std::cout << "[Subscriber " << sub_id << "] Finished, received " << received << " messages" << std::endl;
}

int main()
{
    std::cout << "=========================================" << std::endl;
    std::cout << "LightAP Core IPC - End-to-End Test" << std::endl;
    std::cout << "=========================================" << std::endl;
    std::cout << std::endl;
    
    // Create subscribers first
    std::vector<std::thread> subscribers;
    for (UInt32 i = 0; i < 3; ++i)
    {
        subscribers.emplace_back(SubscriberThread, i + 1);
    }
    
    // Small delay before starting publisher
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Create publisher
    std::thread publisher(PublisherThread);
    
    // Wait for all threads
    publisher.join();
    
    for (auto& sub : subscribers)
    {
        sub.join();
    }
    
    std::cout << std::endl;
    std::cout << "=========================================" << std::endl;
    std::cout << "Test completed successfully!" << std::endl;
    std::cout << "=========================================" << std::endl;
    
    return 0;
}
