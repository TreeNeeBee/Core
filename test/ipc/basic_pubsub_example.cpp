/**
 * @file        basic_pubsub_example.cpp
 * @author      LightAP Core Team
 * @brief       Basic Publisher-Subscriber example
 * @date        2026-01-07
 * @details     Demonstrates zero-copy IPC usage
 * @copyright   Copyright (c) 2026
 */

#include "ipc/Publisher.hpp"
#include "ipc/Subscriber.hpp"
#include <iostream>
#include <cstring>

using namespace lap::core;
using namespace lap::core::ipc;

// Simple message structure
struct SensorData
{
    UInt64 timestamp;
    Float temperature;
    Float humidity;
    UInt32 sensor_id;
    
    void Print() const
    {
        std::cout << "SensorData {" << std::endl;
        std::cout << "  timestamp: " << timestamp << std::endl;
        std::cout << "  temperature: " << temperature << "°C" << std::endl;
        std::cout << "  humidity: " << humidity << "%" << std::endl;
        std::cout << "  sensor_id: " << sensor_id << std::endl;
        std::cout << "}" << std::endl;
    }
};

void PublisherExample()
{
    std::cout << "=== Publisher Example ===" << std::endl;
    
    // Create publisher
    PublisherConfig config{};
    config.max_chunks = 16;
    config.chunk_size = sizeof(SensorData);
    config.auto_cleanup = false;
    
    auto pub_result = Publisher<SensorData>::Create("sensor_data", config);
    if (!pub_result)
    {
        std::cerr << "Failed to create publisher" << std::endl;
        return;
    }
    
    auto publisher = std::move(pub_result.Value());
    std::cout << "Publisher created for service: " << publisher.GetServiceName() << std::endl;
    
    // Publish 5 messages
    for (UInt32 i = 0; i < 5; ++i)
    {
        // Method 1: Loan and send
        auto sample_result = publisher.Loan();
        if (!sample_result)
        {
            std::cerr << "Failed to loan chunk" << std::endl;
            continue;
        }
        
        auto sample = std::move(sample_result.Value());
        
        // Write data
        sample->timestamp = i * 1000;
        sample->temperature = 25.5f + i;
        sample->humidity = 60.0f + i * 2;
        sample->sensor_id = 100;
        
        std::cout << "\nPublishing message " << i << ":" << std::endl;
        sample->Print();
        
        // Send to subscribers
        auto send_result = publisher.Send(std::move(sample));
        if (!send_result)
        {
            std::cerr << "Failed to send message" << std::endl;
        }
    }
    
    std::cout << "\nPublisher statistics:" << std::endl;
    std::cout << "  Allocated chunks: " << publisher.GetAllocatedCount() << std::endl;
    std::cout << "  Pool exhausted: " << (publisher.IsChunkPoolExhausted() ? "Yes" : "No") << std::endl;
}

void SubscriberExample()
{
    std::cout << "\n=== Subscriber Example ===" << std::endl;
    
    // Create subscriber
    SubscriberConfig config{};
    config.queue_capacity = 256;
    config.empty_policy = QueueEmptyPolicy::kSkip;
    
    auto sub_result = Subscriber<SensorData>::Create("sensor_data", config);
    if (!sub_result)
    {
        std::cerr << "Failed to create subscriber" << std::endl;
        return;
    }
    
    auto subscriber = std::move(sub_result.Value());
    std::cout << "Subscriber created for service: " << subscriber.GetServiceName() << std::endl;
    
    // Receive messages
    UInt32 msg_count = 0;
    while (msg_count < 5)
    {
        auto sample_result = subscriber.Receive();
        if (!sample_result)
        {
            // No data available
            std::cout << "No data available (queue empty)" << std::endl;
            break;
        }
        
        auto sample = std::move(sample_result.Value());
        
        std::cout << "\nReceived message " << msg_count << ":" << std::endl;
        sample->Print();
        
        ++msg_count;
    }
    
    std::cout << "\nSubscriber statistics:" << std::endl;
    std::cout << "  Messages received: " << msg_count << std::endl;
    std::cout << "  Queue size: " << subscriber.GetQueueSize() << std::endl;
}

void SendCopyExample()
{
    std::cout << "\n=== SendCopy Example ===" << std::endl;
    
    PublisherConfig config{};
    config.max_chunks = 16;
    config.auto_cleanup = true;
    
    auto pub_result = Publisher<SensorData>::Create("sensor_data_copy", config);
    if (!pub_result)
    {
        std::cerr << "Failed to create publisher" << std::endl;
        return;
    }
    
    auto publisher = std::move(pub_result.Value());
    
    // Prepare data
    SensorData data{};
    data.timestamp = 123456;
    data.temperature = 28.5f;
    data.humidity = 65.0f;
    data.sensor_id = 200;
    
    std::cout << "Sending data using SendCopy:" << std::endl;
    data.Print();
    
    // Send using convenient API
    auto result = publisher.SendCopy(data);
    if (result)
    {
        std::cout << "Message sent successfully" << std::endl;
    }
    else
    {
        std::cerr << "Failed to send message" << std::endl;
    }
}

int main()
{
    std::cout << "LightAP Core IPC - Basic Pub-Sub Example" << std::endl;
    std::cout << "=========================================" << std::endl;
    
    // Note: In this simplified version, Publisher and Subscriber
    // are not yet connected through SubscriberRegistry.
    // This will be implemented in Phase 4.
    
    PublisherExample();
    SubscriberExample();
    SendCopyExample();
    
    std::cout << "\n=========================================" << std::endl;
    std::cout << "Example completed" << std::endl;
    
    return 0;
}
