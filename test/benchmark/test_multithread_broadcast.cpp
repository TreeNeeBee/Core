/**
 * @file        test_multithread_broadcast.cpp
 * @brief       Diagnose multi-threaded broadcast issues
 * @date        2025-12-30
 */

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include "memory/CSharedMemoryAllocator.hpp"
#include "MemoryCommon.hpp"

using namespace lap::core;
using namespace lap::core::memory_internal;
using namespace std::chrono;

std::atomic<bool> g_running{true};
std::atomic<uint64_t> g_pub_sent{0};
std::atomic<uint64_t> g_pub_blocked{0};
std::atomic<uint64_t> g_sub_received{0};
std::atomic<uint64_t> g_sub_released{0};

void publisher_thread(SharedMemoryAllocator& alloc, PublisherHandle pub) {
    std::cout << "[PUB] Starting publisher thread\n";
    std::this_thread::sleep_for(milliseconds(100));  // Wait for subscribers
    
    uint64_t iteration = 0;
    auto start = steady_clock::now();
    
    while (g_running && iteration < 200) {  // Send 200 messages total
        iteration++;
        
        SharedMemoryMemoryBlock block;
        if (alloc.loan(pub, 256, block).HasValue()) {
            auto send_start = steady_clock::now();
            
            if (alloc.send(pub, block).HasValue()) {
                g_pub_sent++;
                
                auto elapsed = duration_cast<milliseconds>(steady_clock::now() - send_start);
                if (elapsed.count() > 100) {
                    g_pub_blocked++;
                    std::cout << "[PUB] Message #" << iteration << " blocked for " 
                              << elapsed.count() << "ms\n";
                }
                
                if (iteration % 20 == 0) {
                    std::cout << "[PUB] Sent " << iteration << " messages (blocked: " 
                              << g_pub_blocked.load() << ")\n";
                }
            } else {
                std::cout << "[PUB] send() failed for message #" << iteration << "\n";
            }
        } else {
            std::cout << "[PUB] loan() failed for message #" << iteration << "\n";
        }
        
        std::this_thread::sleep_for(milliseconds(10));  // 10ms between messages
    }
    
    auto duration = duration_cast<seconds>(steady_clock::now() - start).count();
    std::cout << "[PUB] Publisher finished: sent=" << g_pub_sent.load() 
              << ", duration=" << duration << "s\n";
}

void subscriber_thread(SharedMemoryAllocator& alloc, SubscriberHandle sub, int id) {
    std::cout << "[SUB" << id << "] Starting subscriber thread (sub_id=" << sub.subscriber_id << ")\n";
    
    uint64_t local_received = 0;
    uint64_t local_released = 0;
    uint64_t empty_polls = 0;
    
    while (g_running) {
        // ✅ Use waitForData() for efficient waiting (iceoryx2-style)
        // Wait up to 100ms for new data (avoid infinite wait to allow g_running check)
        if (alloc.waitForData(sub, 100000)) {  // 100ms timeout
            SharedMemoryMemoryBlock block;
            if (alloc.receive(sub, block).HasValue()) {
                local_received++;
                empty_polls = 0;  // Reset empty poll counter
                
                // Simulate processing
                std::this_thread::sleep_for(microseconds(100));
                
                // Release sample
                alloc.release(sub, block);
                local_released++;
                
                if (local_released % 50 == 0) {
                    std::cout << "[SUB" << id << "] Processed " << local_released << " messages\n";
                }
            }
        } else {
            // Timeout or no data - check g_running and continue
            empty_polls++;
        }
    }
    
    g_sub_received += local_received;
    g_sub_released += local_released;
    
    std::cout << "[SUB" << id << "] Subscriber finished: received=" << local_received 
              << ", released=" << local_released << "\n";
}

int main() {
    std::cout << "╔════════════════════════════════════════════════════╗\n";
    std::cout << "║  Multi-threaded Broadcast Diagnostic Test         ║\n";
    std::cout << "╚════════════════════════════════════════════════════╝\n\n";
    
    // Test with different subscriber counts
    for (int num_subs : {1, 2, 4, 8}) {
        std::cout << "\n========================================\n";
        std::cout << "TEST: " << num_subs << " subscriber(s)\n";
        std::cout << "========================================\n\n";
        
        // Reset counters
        g_running = true;
        g_pub_sent = 0;
        g_pub_blocked = 0;
        g_sub_received = 0;
        g_sub_released = 0;
        
        // Create allocator
        auto config = GetDefaultSharedMemoryConfig();
        config.chunk_count = 512;
        config.max_chunk_size = 4096;
        config.allocation_policy = AllocationPolicy::WAIT_ASYNC;
        config.queue_overflow_policy = QueueOverflowPolicy::BLOCK_PUBLISHER;
        config.subscriber_queue_capacity = 64;  // Moderate queue size
        config.enable_event_notification = true;  // ✅ Enable notify for efficient waiting
        config.enable_debug_trace = false;
        
        SharedMemoryAllocator allocator;
        if (!allocator.initialize(config)) {
            std::cerr << "[ERROR] Failed to initialize allocator\n";
            return 1;
        }
        
        // Create publisher and subscribers
        PublisherHandle pub;
        allocator.createPublisher(pub);
        
        std::vector<SubscriberHandle> subscribers(num_subs);
        for (int i = 0; i < num_subs; ++i) {
            allocator.createSubscriber(subscribers[i]);
        }
        
        std::cout << "[INFO] Created 1 publisher and " << num_subs << " subscriber(s)\n";
        std::cout << "[INFO] Queue capacity: " << config.subscriber_queue_capacity << "\n";
        std::cout << "[INFO] Starting threads...\n\n";
        
        // Start threads
        std::vector<std::thread> threads;
        threads.emplace_back(publisher_thread, std::ref(allocator), pub);
        
        for (int i = 0; i < num_subs; ++i) {
            threads.emplace_back(subscriber_thread, std::ref(allocator), subscribers[i], i);
        }
        
        // Wait for publisher to finish
        threads[0].join();
        std::cout << "\n[INFO] Publisher finished, waiting 2 seconds for subscribers to drain...\n";
        std::this_thread::sleep_for(seconds(2));
        
        // Stop subscribers
        g_running = false;
        for (size_t i = 1; i < threads.size(); ++i) {
            threads[i].join();
        }
        
        // Print results
        std::cout << "\n--- Results ---\n";
        std::cout << "Publisher sent:         " << g_pub_sent.load() << "\n";
        std::cout << "Publisher blocked:      " << g_pub_blocked.load() << "\n";
        std::cout << "Total received:         " << g_sub_received.load() << "\n";
        std::cout << "Total released:         " << g_sub_released.load() << "\n";
        std::cout << "Expected (sent × subs): " << (g_pub_sent.load() * num_subs) << "\n";
        
        if (g_sub_released.load() == g_pub_sent.load() * num_subs) {
            std::cout << "✅ PASS: All messages accounted for\n";
        } else {
            std::cout << "❌ FAIL: Message count mismatch!\n";
        }
        
        // Cleanup
        for (auto& sub : subscribers) {
            allocator.destroySubscriber(sub);
        }
        allocator.destroyPublisher(pub);
        
        std::this_thread::sleep_for(milliseconds(500));
    }
    
    std::cout << "\n╔════════════════════════════════════════════════════╗\n";
    std::cout << "║  Diagnostic Test Complete                         ║\n";
    std::cout << "╚════════════════════════════════════════════════════╝\n";
    
    return 0;
}
