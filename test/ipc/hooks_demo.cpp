/**
 * @file        hooks_demo.cpp
 * @author      LightAP Core Team
 * @brief       Demonstrates IPC event hooks usage
 * @date        2026-01-07
 * @details     Shows how to monitor and debug IPC with hooks
 * @copyright   Copyright (c) 2026
 */

#include "../../source/inc/ipc/Publisher.hpp"
#include "../../source/inc/ipc/Subscriber.hpp"
#include "LoggingHooks.hpp"
#include "StatisticsHooks.hpp"

#include <iostream>
#include <thread>
#include <chrono>

using namespace lap::core::ipc;
using namespace lap::core;

struct SensorData
{
    UInt64 timestamp;
    float  temperature;
    float  pressure;
    UInt32 sequence;
};

int main()
{
    std::cout << "========== IPC Event Hooks Demo ==========" << std::endl;
    
    // Create event hooks
    auto logging_hooks = std::make_shared<LoggingHooks>(true);  // verbose mode
    auto stats_hooks = std::make_shared<StatisticsHooks>();
    
    const char* service_name = "sensor_data";
    
    // Publisher configuration
    PublisherConfig pub_config;
    pub_config.max_chunks = 16;
    pub_config.chunk_size = sizeof(SensorData);
    pub_config.loan_policy = LoanFailurePolicy::kError;
    
    // Subscriber configuration
    SubscriberConfig sub_config;
    
    try
    {
        // Create publisher with hooks
        std::cout << "\n[1] Creating publisher with event hooks..." << std::endl;
        auto pub_result = Publisher<SensorData>::Create(service_name, pub_config);
        if (!pub_result.HasValue())
        {
            std::cerr << "Failed to create publisher" << std::endl;
            return 1;
        }
        auto publisher = std::make_shared<Publisher<SensorData>>(std::move(pub_result).Value());
        
        // Attach hooks to publisher
        publisher->SetEventHooks(logging_hooks);
        
        // Create subscribers with hooks
        std::cout << "\n[2] Creating 3 subscribers with event hooks..." << std::endl;
        std::vector<std::shared_ptr<Subscriber<SensorData>>> subscribers;
        
        for (int i = 0; i < 3; ++i)
        {
            auto sub_result = Subscriber<SensorData>::Create(service_name, sub_config);
            if (!sub_result.HasValue())
            {
                std::cerr << "Failed to create subscriber " << i << std::endl;
                continue;
            }
            
            auto subscriber = std::make_shared<Subscriber<SensorData>>(std::move(sub_result).Value());
            subscriber->SetEventHooks(stats_hooks);  // Use statistics hooks
            subscribers.push_back(subscriber);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Scenario 1: Normal message flow
        std::cout << "\n[3] Sending 5 messages (normal flow)..." << std::endl;
        for (UInt32 seq = 0; seq < 5; ++seq)
        {
            auto loan_result = publisher->Loan();
            if (loan_result.HasValue())
            {
                auto sample = std::move(loan_result).Value();
                auto* data = sample.Get();
                
                data->timestamp = seq * 1000;
                data->temperature = 20.0f + seq;
                data->pressure = 1013.25f + seq * 0.1f;
                data->sequence = seq;
                
                publisher->Send(std::move(sample));
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        
        // Subscribers receive messages
        std::cout << "\n[4] Subscribers receiving messages..." << std::endl;
        for (auto& subscriber : subscribers)
        {
            int count = 0;
            while (true)
            {
                auto receive_result = subscriber->Receive();
                if (!receive_result.HasValue())
                {
                    break;
                }
                
                auto sample = std::move(receive_result).Value();
                auto* data = sample.Get();
                
                std::cout << "  Subscriber received: seq=" << data->sequence
                          << ", temp=" << data->temperature << "°C" << std::endl;
                count++;
            }
        }
        
        // Scenario 2: Trigger loan failures
        std::cout << "\n[5] Triggering loan failures (exhaust pool)..." << std::endl;
        std::vector<Sample<SensorData>> loaned_samples;
        
        for (UInt32 i = 0; i < 20; ++i)  // Try to loan more than max_chunks
        {
            auto loan_result = publisher->Loan();
            if (loan_result.HasValue())
            {
                loaned_samples.push_back(std::move(loan_result).Value());
            }
            else
            {
                // Hook should log the failure
                std::cout << "  Loan attempt " << i << " failed (expected)" << std::endl;
            }
        }
        
        std::cout << "  Total loaned: " << loaned_samples.size() << std::endl;
        
        // Release some samples
        std::cout << "\n[6] Releasing loaned samples..." << std::endl;
        loaned_samples.clear();  // RAII cleanup
        
        // Scenario 3: High-frequency messaging
        std::cout << "\n[7] High-frequency messaging (100 messages)..." << std::endl;
        
        auto start = std::chrono::steady_clock::now();
        
        for (UInt32 seq = 0; seq < 100; ++seq)
        {
            auto loan_result = publisher->Loan();
            if (loan_result.HasValue())
            {
                auto sample = std::move(loan_result).Value();
                auto* data = sample.Get();
                
                data->timestamp = seq;
                data->temperature = 25.0f;
                data->pressure = 1013.25f;
                data->sequence = seq;
                
                publisher->Send(std::move(sample));
            }
        }
        
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        std::cout << "  Sent 100 messages in " << duration.count() << " μs" << std::endl;
        std::cout << "  Average latency: " << (duration.count() / 100.0) << " μs/msg" << std::endl;
        
        // Subscribers receive high-frequency messages
        std::cout << "\n[8] Receiving high-frequency messages..." << std::endl;
        for (size_t i = 0; i < subscribers.size(); ++i)
        {
            int count = 0;
            while (true)
            {
                auto receive_result = subscribers[i]->Receive();
                if (!receive_result.HasValue())
                {
                    break;
                }
                count++;
            }
            std::cout << "  Subscriber " << i << " received: " << count << " messages" << std::endl;
        }
        
        // Print statistics
        std::cout << "\n[9] Event statistics:" << std::endl;
        stats_hooks->PrintSummary();
        
        std::cout << "\n[10] Cleaning up..." << std::endl;
        subscribers.clear();
        
        std::cout << "\n========== Demo Complete ==========" << std::endl;
        
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}

// Force template instantiation
#include "../../source/src/ipc/Subscriber.cpp"
#include "../../source/src/ipc/Publisher.cpp"
