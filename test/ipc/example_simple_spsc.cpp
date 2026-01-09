/**
 * @file        example_simple_spsc.cpp
 * @author      LightAP Core Team
 * @brief       Simple SPSC (Single Producer Single Consumer) example
 * @date        2026-01-08
 * @details     Demonstrates basic Publisher-Subscriber usage with SendCopy
 * @copyright   Copyright (c) 2026
 */

#include "ipc/Publisher.hpp"
#include "ipc/Subscriber.hpp"
#include "../../source/src/ipc/Publisher.cpp"
#include "../../source/src/ipc/Subscriber.cpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <sys/mman.h>

using namespace lap::core;
using namespace lap::core::ipc;

// Simple message structure
struct SensorData
{
    UInt64 timestamp;
    double temperature;
    UInt32 sensor_id;
};

static void cleanup_shm(const char* name)
{
    String path = "/lightap_ipc_" + String(name);
    shm_unlink(path.c_str());
}

int main()
{
    std::cout << "=== Simple SPSC Example ===" << std::endl;
    
    const char* service_name = "sensor_example";
    cleanup_shm(service_name);
    
    // Configuration for Publisher
    PublisherConfig pub_config{};
    pub_config.max_chunks = 32;
    pub_config.chunk_size = sizeof(SensorData);
    pub_config.auto_cleanup = true;
    
    // Create Publisher
    auto pub_result = Publisher<SensorData>::Create(service_name, pub_config);
    if (!pub_result.HasValue())
    {
        std::cerr << "Failed to create publisher" << std::endl;
        return 1;
    }
    
    auto publisher = std::move(pub_result).Value();
    std::cout << "✓ Publisher created: " << publisher.GetServiceName() << std::endl;
    
    // Configuration for Subscriber
    SubscriberConfig sub_config{};
    
    // Create Subscriber
    auto sub_result = Subscriber<SensorData>::Create(service_name, sub_config);
    if (!sub_result.HasValue())
    {
        std::cerr << "Failed to create subscriber" << std::endl;
        return 1;
    }
    
    auto subscriber = std::move(sub_result).Value();
    std::cout << "✓ Subscriber created for service: " << subscriber.GetServiceName() << std::endl;
    
    // Give time for subscriber registration
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Send messages
    std::cout << "\n--- Publishing Messages ---" << std::endl;
    const int num_messages = 5;
    
    for (int i = 0; i < num_messages; ++i)
    {
        SensorData data;
        data.timestamp = i * 1000;
        data.temperature = 25.5 + i * 0.5;
        data.sensor_id = 100;
        
        auto result = publisher.SendCopy(data);
        if (result.HasValue())
        {
            std::cout << "  [Publisher] Sent message " << i
                      << " (temp=" << data.temperature << "°C)" << std::endl;
        }
        else
        {
            std::cerr << "  [Publisher] Failed to send message " << i << std::endl;
        }
        
        // Small delay between sends
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Receive messages
    std::cout << "\n--- Receiving Messages ---" << std::endl;
    int received_count = 0;
    
    for (int i = 0; i < num_messages; ++i)
    {
        auto result = subscriber.Receive();
        if (result.HasValue())
        {
            auto sample = std::move(result).Value();
            std::cout << "  [Subscriber] Received message " << received_count
                      << " (timestamp=" << sample->timestamp
                      << ", temp=" << sample->temperature << "°C"
                      << ", sensor=" << sample->sensor_id << ")" << std::endl;
            received_count++;
        }
        else
        {
            std::cerr << "  [Subscriber] Failed to receive message" << std::endl;
        }
    }
    
    std::cout << "\n=== Summary ===" << std::endl;
    std::cout << "Messages sent: " << num_messages << std::endl;
    std::cout << "Messages received: " << received_count << std::endl;
    
    if (received_count == num_messages)
    {
        std::cout << "✓ SUCCESS: All messages delivered!" << std::endl;
        return 0;
    }
    else
    {
        std::cout << "✗ FAILURE: Some messages were lost!" << std::endl;
        return 1;
    }
}
