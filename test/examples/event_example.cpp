/**
 * @file event_example.cpp
 * @brief Event messaging system usage example
 * @date 2026-01-02
 * 
 * Demonstrates how to use the Event system for pub/sub messaging
 * with zero-copy shared memory.
 */

#include "memory/CEvent.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>

using namespace lap::core;
using namespace std::chrono_literals;

// Example message structure
struct SensorData {
    uint64_t timestamp;
    float temperature;
    float pressure;
    float humidity;
    char sensor_id[32];
};

int main() {
    std::cout << "\n=== Event Messaging System Example ===\n\n";
    
    // 1. Create Event with configuration
    EventConfig config;
    config.event_name = "sensor_data_stream";
    config.payload_size = sizeof(SensorData);  // Fixed payload size
    config.max_chunks = 32;                    // Chunk pool capacity
    config.max_channels = 4;                // Max concurrent subscribers
    config.subscriber_queue_capacity = 16;     // Queue size per subscriber
    config.use_shm_for_queues = false;         // Use malloc for queue nodes
    
    std::cout << "Creating Event: " << config.event_name << "\n";
    std::cout << "  Payload size: " << config.payload_size << " bytes\n";
    std::cout << "  Max chunks: " << config.max_chunks << "\n";
    std::cout << "  Max subscribers: " << config.max_channels << "\n\n";
    
    Event event(config);
    
    if (!event.isInitialized()) {
        std::cerr << "Failed to initialize event!\n";
        return 1;
    }
    
    // 2. Create Publisher and Subscribers
    auto publisher = event.createPublisher();
    auto subscriber1 = event.createSubscriber();
    auto subscriber2 = event.createSubscriber();
    
    if (!publisher || !subscriber1 || !subscriber2) {
        std::cerr << "Failed to create publisher/subscribers!\n";
        return 1;
    }
    
    std::cout << "Created 1 publisher and 2 subscribers\n\n";
    
    // 3. Publisher: loan, fill, and send samples
    std::cout << "=== Publishing Messages ===\n";
    
    for (int i = 0; i < 5; ++i) {
        // Loan a sample from the chunk pool
        auto loan_result = publisher->loan();
        if (!loan_result.HasValue()) {
            std::cerr << "Failed to loan sample " << i << "\n";
            continue;
        }
        
        Sample sample = loan_result.Value();
        
        // Fill the sample with sensor data
        SensorData* data = static_cast<SensorData*>(sample.data());
        data->timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        data->temperature = 20.0f + i * 0.5f;
        data->pressure = 1013.25f + i * 0.1f;
        data->humidity = 50.0f + i * 1.0f;
        snprintf(data->sensor_id, sizeof(data->sensor_id), "SENSOR_%03d", i);
        
        std::cout << "Publishing message " << i << ": " << data->sensor_id 
                  << " (temp=" << data->temperature << "°C)\n";
        
        // Send to all subscribers (zero-copy broadcast)
        auto send_result = publisher->send(sample);
        if (!send_result.HasValue()) {
            std::cerr << "Failed to send sample " << i << "\n";
        }
        
        std::this_thread::sleep_for(10ms);
    }
    
    std::cout << "\n=== Receiving Messages ===\n";
    
    // 4. Subscribers: receive and process samples
    auto receive_and_process = [](EventSubscriber* sub, const std::string& name) {
        int count = 0;
        while (true) {
            auto recv_result = sub->receive();
            if (!recv_result.HasValue()) {
                break;  // No more data
            }
            
            Sample sample = recv_result.Value();
            const SensorData* data = static_cast<const SensorData*>(sample.data());
            
            std::cout << name << " received: " << data->sensor_id
                      << " (temp=" << data->temperature << "°C, "
                      << "pressure=" << data->pressure << "hPa, "
                      << "humidity=" << data->humidity << "%)\n";
            
            // Release the sample (reference counting)
            sub->release(sample);
            count++;
        }
        
        std::cout << name << " total received: " << count << " messages\n";
    };
    
    std::cout << "\nSubscriber 1:\n";
    receive_and_process(subscriber1.get(), "Sub1");
    
    std::cout << "\nSubscriber 2:\n";
    receive_and_process(subscriber2.get(), "Sub2");
    
    // 5. Get statistics
    std::cout << "\n=== Event Statistics ===\n";
    SharedMemoryAllocatorStats stats;
    event.getStats(stats);
    
    std::cout << "Total loans: " << stats.total_loans << "\n";
    std::cout << "Total sends: " << stats.total_sends << "\n";
    std::cout << "Total receives: " << stats.total_receives << "\n";
    std::cout << "Total releases: " << stats.total_releases << "\n";
    std::cout << "Free chunks: " << stats.free_chunks << "\n";
    std::cout << "Peak memory usage: " << stats.peak_memory_usage << " bytes\n";
    
    std::cout << "\n=== Cleanup ===\n";
    std::cout << "Destroying Event and cleaning up resources...\n";
    
    return 0;
}
