/**
 * @file        benchmark_ipc_latency.cpp
 * @author      LightAP Core Team
 * @brief       IPC Latency Benchmark
 * @date        2026-01-08
 * @details     Measures round-trip latency for different message sizes
 * @copyright   Copyright (c) 2026
 */

#include "ipc/Publisher.hpp"
#include "ipc/Subscriber.hpp"
#include "../../source/src/ipc/Publisher.cpp"
#include "../../source/src/ipc/Subscriber.cpp"
#include <iostream>
#include <chrono>
#include <vector>
#include <numeric>
#include <algorithm>
#include <sys/mman.h>

using namespace lap::core;
using namespace lap::core::ipc;
using namespace std::chrono;

static void cleanup_shm(const char* name)
{
    String path = "/lightap_ipc_" + String(name);
    shm_unlink(path.c_str());
}

template<size_t PayloadSize>
struct Message
{
    UInt64 send_timestamp;
    UInt8 payload[PayloadSize];
};

template<size_t PayloadSize>
void RunLatencyBenchmark(const char* service_name, int iterations)
{
    using MsgType = Message<PayloadSize>;
    
    cleanup_shm(service_name);
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "Latency Benchmark - Payload: " << PayloadSize << " bytes" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // Create Publisher
    PublisherConfig pub_config{};
    pub_config.max_chunks = 64;
    pub_config.chunk_size = sizeof(MsgType);
    pub_config.auto_cleanup = true;
    
    auto pub_result = Publisher<MsgType>::Create(service_name, pub_config);
    if (!pub_result.HasValue())
    {
        std::cerr << "Failed to create publisher" << std::endl;
        return;
    }
    auto publisher = std::move(pub_result).Value();
    
    // Create Subscriber
    SubscriberConfig sub_config{};
    auto sub_result = Subscriber<MsgType>::Create(service_name, sub_config);
    if (!sub_result.HasValue())
    {
        std::cerr << "Failed to create subscriber" << std::endl;
        return;
    }
    auto subscriber = std::move(sub_result).Value();
    
    std::this_thread::sleep_for(milliseconds(10));
    
    // Warm-up
    for (int i = 0; i < 100; ++i)
    {
        MsgType msg;
        msg.send_timestamp = 0;
        publisher.SendCopy(msg);
        auto result = subscriber.Receive();
    }
    
    // Benchmark
    std::vector<double> latencies;
    latencies.reserve(iterations);
    
    for (int i = 0; i < iterations; ++i)
    {
        MsgType msg;
        
        auto t1 = high_resolution_clock::now();
        msg.send_timestamp = duration_cast<nanoseconds>(t1.time_since_epoch()).count();
        
        publisher.SendCopy(msg);
        
        auto result = subscriber.Receive();
        auto t2 = high_resolution_clock::now();
        
        if (result.HasValue())
        {
            auto latency_ns = duration_cast<nanoseconds>(t2 - t1).count();
            latencies.push_back(latency_ns / 1000.0);  // Convert to microseconds
        }
    }
    
    // Calculate statistics
    std::sort(latencies.begin(), latencies.end());
    double avg = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
    double min_val = latencies.front();
    double max_val = latencies.back();
    double p50 = latencies[latencies.size() * 50 / 100];
    double p99 = latencies[latencies.size() * 99 / 100];
    double p999 = latencies[latencies.size() * 999 / 1000];
    
    std::cout << "Results (" << latencies.size() << " samples):" << std::endl;
    std::cout << "  Min:  " << min_val << " μs" << std::endl;
    std::cout << "  Avg:  " << avg << " μs" << std::endl;
    std::cout << "  P50:  " << p50 << " μs" << std::endl;
    std::cout << "  P99:  " << p99 << " μs" << std::endl;
    std::cout << "  P999: " << p999 << " μs" << std::endl;
    std::cout << "  Max:  " << max_val << " μs" << std::endl;
}

int main()
{
    std::cout << "IPC Latency Benchmark" << std::endl;
    std::cout << "=====================" << std::endl;
    
    const int iterations = 10000;
    
    // Test different message sizes
    RunLatencyBenchmark<64>("lat_bench_64", iterations);
    RunLatencyBenchmark<256>("lat_bench_256", iterations);
    RunLatencyBenchmark<1024>("lat_bench_1k", iterations);
    RunLatencyBenchmark<4096>("lat_bench_4k", iterations);
    
    std::cout << "\nBenchmark Complete!" << std::endl;
    return 0;
}
