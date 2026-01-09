/**
 * @file        example_broadcast_spmc.cpp
 * @author      LightAP Core Team
 * @brief       SPMC (Single Producer Multiple Consumers) broadcast example
 * @date        2026-01-08
 * @details     Demonstrates one-to-many broadcast messaging
 * @copyright   Copyright (c) 2026
 */

#include "ipc/Publisher.hpp"
#include "ipc/Subscriber.hpp"
#include "../../source/src/ipc/Publisher.cpp"
#include "../../source/src/ipc/Subscriber.cpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <sys/mman.h>

using namespace lap::core;
using namespace lap::core::ipc;

struct BroadcastMessage
{
    UInt64 message_id;
    double value;
    char text[32];
};

static void cleanup_shm(const char* name)
{
    String path = "/lightap_ipc_" + String(name);
    shm_unlink(path.c_str());
}

// Subscriber thread function
void SubscriberThread(int subscriber_id, const char* service_name, int expected_count)
{
    // Create subscriber
    SubscriberConfig config{};
    auto sub_result = Subscriber<BroadcastMessage>::Create(service_name, config);
    if (!sub_result.HasValue())
    {
        std::cerr << "  [Sub-" << subscriber_id << "] Failed to create subscriber" << std::endl;
        return;
    }
    
    auto subscriber = std::move(sub_result).Value();
    std::cout << "  [Sub-" << subscriber_id << "] Created and waiting for messages..." << std::endl;
    
    // Receive messages
    int received = 0;
    for (int i = 0; i < expected_count * 2; ++i)  // Try more times in case of timing
    {
        auto result = subscriber.Receive();
        if (result.HasValue())
        {
            auto sample = std::move(result).Value();
            std::cout << "  [Sub-" << subscriber_id << "] Received msg "
                      << sample->message_id << ": value=" << sample->value
                      << ", text=\"" << sample->text << "\"" << std::endl;
            received++;
            
            if (received >= expected_count)
            {
                break;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    
    std::cout << "  [Sub-" << subscriber_id << "] Total received: " 
              << received << "/" << expected_count << std::endl;
}

int main()
{
    std::cout << "=== SPMC Broadcast Example ===" << std::endl;
    
    const char* service_name = "broadcast_example";
    cleanup_shm(service_name);
    const int num_subscribers = 4;
    const int num_messages = 5;
    
    // Configuration
    PublisherConfig pub_config{};
    pub_config.max_chunks = 32;
    pub_config.chunk_size = sizeof(BroadcastMessage);
    pub_config.auto_cleanup = true;
    
    // Create Publisher
    auto pub_result = Publisher<BroadcastMessage>::Create(service_name, pub_config);
    if (!pub_result.HasValue())
    {
        std::cerr << "Failed to create publisher" << std::endl;
        return 1;
    }
    
    auto publisher = std::move(pub_result).Value();
    std::cout << "✓ Publisher created" << std::endl;
    
    // Create subscriber threads
    std::cout << "\n--- Creating Subscribers ---" << std::endl;
    std::vector<std::thread> threads;
    
    for (int i = 0; i < num_subscribers; ++i)
    {
        threads.emplace_back(SubscriberThread, i, service_name, num_messages);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Give all subscribers time to initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Broadcast messages to all subscribers
    std::cout << "\n--- Broadcasting Messages ---" << std::endl;
    
    for (int i = 0; i < num_messages; ++i)
    {
        BroadcastMessage msg;
        msg.message_id = i;
        msg.value = i * 3.14;
        snprintf(msg.text, sizeof(msg.text), "Broadcast #%d", i);
        
        auto result = publisher.SendCopy(msg);
        if (result.HasValue())
        {
            std::cout << "  [Publisher] Broadcasted message " << i 
                      << " to all subscribers" << std::endl;
        }
        else
        {
            std::cerr << "  [Publisher] Failed to broadcast message " << i << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // Wait for all subscribers to finish
    std::cout << "\n--- Waiting for Subscribers ---" << std::endl;
    for (auto& thread : threads)
    {
        thread.join();
    }
    
    std::cout << "\n=== Summary ===" << std::endl;
    std::cout << "Number of subscribers: " << num_subscribers << std::endl;
    std::cout << "Messages broadcasted: " << num_messages << std::endl;
    std::cout << "Expected total deliveries: " << (num_subscribers * num_messages) << std::endl;
    std::cout << "✓ Broadcast complete - check individual subscriber logs above" << std::endl;
    
    return 0;
}
