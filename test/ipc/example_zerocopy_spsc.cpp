/**
 * @file        example_zerocopy_spsc.cpp
 * @author      LightAP Core Team
 * @brief       Zero-copy SPSC example using Loan/Send
 * @date        2026-01-08
 * @details     Demonstrates zero-copy message passing with Loan API
 * @copyright   Copyright (c) 2026
 */

#include "ipc/Publisher.hpp"
#include "ipc/Subscriber.hpp"
#include "../../source/src/ipc/Publisher.cpp"
#include "../../source/src/ipc/Subscriber.cpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <sys/mman.h>

using namespace lap::core;
using namespace lap::core::ipc;

// Large payload structure (benefits from zero-copy)
struct ImageData
{
    UInt64 frame_id;
    UInt32 width;
    UInt32 height;
    UInt8 pixels[1024];  // Simulated image data
    
    void GenerateTestPattern(UInt32 frame)
    {
        for (size_t i = 0; i < sizeof(pixels); ++i)
        {
            pixels[i] = static_cast<UInt8>((frame + i) % 256);
        }
    }
    
    bool ValidateTestPattern(UInt32 frame) const
    {
        for (size_t i = 0; i < sizeof(pixels); ++i)
        {
            if (pixels[i] != static_cast<UInt8>((frame + i) % 256))
            {
                return false;
            }
        }
        return true;
    }
};

static void cleanup_shm(const char* name)
{
    String path = "/lightap_ipc_" + String(name);
    shm_unlink(path.c_str());
}

int main()
{
    std::cout << "=== Zero-Copy SPSC Example ===" << std::endl;
    std::cout << "Message size: " << sizeof(ImageData) << " bytes" << std::endl;
    
    const char* service_name = "image_example";
    cleanup_shm(service_name);
    
    // Configuration
    PublisherConfig pub_config{};
    pub_config.max_chunks = 16;
    pub_config.chunk_size = sizeof(ImageData);
    pub_config.auto_cleanup = true;
    
    // Create Publisher
    auto pub_result = Publisher<ImageData>::Create(service_name, pub_config);
    if (!pub_result.HasValue())
    {
        std::cerr << "Failed to create publisher" << std::endl;
        return 1;
    }
    
    auto publisher = std::move(pub_result).Value();
    std::cout << "✓ Publisher created" << std::endl;
    
    // Create Subscriber
    SubscriberConfig sub_config{};
    auto sub_result = Subscriber<ImageData>::Create(service_name, sub_config);
    if (!sub_result.HasValue())
    {
        std::cerr << "Failed to create subscriber" << std::endl;
        return 1;
    }
    
    auto subscriber = std::move(sub_result).Value();
    std::cout << "✓ Subscriber created" << std::endl;
    
    // Wait for subscriber registration
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Send messages using zero-copy Loan/Send API
    std::cout << "\n--- Zero-Copy Publishing ---" << std::endl;
    const int num_frames = 3;
    
    for (int i = 0; i < num_frames; ++i)
    {
        // Loan a chunk from shared memory (zero-copy)
        auto loan_result = publisher.Loan();
        if (!loan_result.HasValue())
        {
            std::cerr << "  [Publisher] Failed to loan chunk for frame " << i << std::endl;
            continue;
        }
        
        auto sample = std::move(loan_result).Value();
        
        // Write directly to shared memory
        sample->frame_id = i;
        sample->width = 1920;
        sample->height = 1080;
        sample->GenerateTestPattern(i);
        
        std::cout << "  [Publisher] Prepared frame " << i 
                  << " (" << sample->width << "x" << sample->height << ")" << std::endl;
        
        // Send to subscribers (ownership transferred, no copy)
        auto send_result = publisher.Send(std::move(sample));
        if (send_result.HasValue())
        {
            std::cout << "  [Publisher] Sent frame " << i << " (zero-copy)" << std::endl;
        }
        else
        {
            std::cerr << "  [Publisher] Failed to send frame " << i << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Receive messages
    std::cout << "\n--- Zero-Copy Receiving ---" << std::endl;
    int received_count = 0;
    
    for (int i = 0; i < num_frames; ++i)
    {
        auto result = subscriber.Receive();
        if (result.HasValue())
        {
            auto sample = std::move(result).Value();
            
            // Data is directly accessible in shared memory
            bool valid = sample->ValidateTestPattern(sample->frame_id);
            
            std::cout << "  [Subscriber] Received frame " << sample->frame_id
                      << " (" << sample->width << "x" << sample->height << ")"
                      << " - Data integrity: " << (valid ? "OK" : "FAIL") << std::endl;
            
            received_count++;
            
            // Sample destructor will automatically release chunk
        }
        else
        {
            std::cerr << "  [Subscriber] Failed to receive frame" << std::endl;
        }
    }
    
    std::cout << "\n=== Summary ===" << std::endl;
    std::cout << "Frames sent: " << num_frames << std::endl;
    std::cout << "Frames received: " << received_count << std::endl;
    std::cout << "✓ All data passed through shared memory with ZERO COPY!" << std::endl;
    
    return (received_count == num_frames) ? 0 : 1;
}
