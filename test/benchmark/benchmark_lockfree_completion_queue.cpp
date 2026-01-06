/**
 * @file        benchmark_lockfree_completion_queue.cpp
 * @brief       Performance benchmark for lock-free completion_queue optimization
 * @date        2025-12-29
 * @details     Measures the performance improvement from removing mutex in completion_queue
 */

#include <benchmark/benchmark.h>
#include <thread>
#include <vector>
#include "memory/CSharedMemoryAllocator.hpp"

using namespace lap::core;

// ============================================================================
// Benchmark Setup
// ============================================================================

class CompletionQueueBenchmark : public benchmark::Fixture {
protected:
    SharedMemoryAllocator allocator_;
    PublisherHandle pub_;
    std::vector<SubscriberHandle> subscribers_;
    
    void SetUp(const ::benchmark::State& state) override {
        auto config = GetDefaultSharedMemoryConfig();
        config.chunk_count = 256;
        config.max_chunk_size = 4096;
        config.enable_debug_trace = false;
        
        allocator_.initialize(config);
        allocator_.createPublisher(pub_);
        
        // Create N subscribers based on state.range(0)
        int num_subscribers = state.range(0);
        subscribers_.resize(num_subscribers);
        for (int i = 0; i < num_subscribers; ++i) {
            allocator_.createSubscriber(subscribers_[i]);
        }
    }
    
    void TearDown(const ::benchmark::State&) override {
        for (auto& sub : subscribers_) {
            allocator_.destroySubscriber(sub);
        }
        allocator_.destroyPublisher(pub_);
    }
};

// ============================================================================
// Benchmark: Concurrent Release (Lock-Free Completion Queue)
// ============================================================================

BENCHMARK_DEFINE_F(CompletionQueueBenchmark, ConcurrentRelease)(benchmark::State& state) {
    const int num_subscribers = state.range(0);
    const int messages_per_iteration = 100;
    
    for (auto _ : state) {
        // Send messages to all subscribers
        std::vector<SharedMemoryMemoryBlock> blocks(num_subscribers * messages_per_iteration);
        
        for (int i = 0; i < messages_per_iteration; ++i) {
            SharedMemoryMemoryBlock block;
            if (allocator_.loan(pub_, 256, block).HasValue()) {
                allocator_.send(pub_, block);
            }
        }
        
        // All subscribers receive and release concurrently
        std::vector<std::thread> threads;
        std::atomic<uint64_t> total_released{0};
        
        auto release_worker = [&](SubscriberHandle sub) {
            for (int i = 0; i < messages_per_iteration; ++i) {
                SharedMemoryMemoryBlock recv_block;
                if (allocator_.receive(sub, recv_block).HasValue()) {
                    allocator_.release(sub, recv_block);
                    total_released.fetch_add(1, std::memory_order_relaxed);
                }
            }
        };
        
        benchmark::DoNotOptimize(total_released);
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < num_subscribers; ++i) {
            threads.emplace_back(release_worker, subscribers_[i]);
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        
        state.SetIterationTime(elapsed.count() / 1e9);
        state.counters["messages"] = benchmark::Counter(
            total_released.load(), 
            benchmark::Counter::kIsRate
        );
    }
}

BENCHMARK_REGISTER_F(CompletionQueueBenchmark, ConcurrentRelease)
    ->Args({1})    // 1 subscriber
    ->Args({4})    // 4 subscribers
    ->Args({8})    // 8 subscribers
    ->Args({16})   // 16 subscribers
    ->Args({32})   // 32 subscribers
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

// ============================================================================
// Benchmark: Batched Reclaim Performance
// ============================================================================

BENCHMARK_DEFINE_F(CompletionQueueBenchmark, BatchedReclaim)(benchmark::State& state) {
    const int num_messages = 1000;
    
    for (auto _ : state) {
        // Fill completion queues
        for (int i = 0; i < num_messages; ++i) {
            SharedMemoryMemoryBlock block;
            if (allocator_.loan(pub_, 256, block).HasValue()) {
                allocator_.send(pub_, block);
                
                // All subscribers receive
                for (auto& sub : subscribers_) {
                    SharedMemoryMemoryBlock recv_block;
                    if (allocator_.receive(sub, recv_block).HasValue()) {
                        // Release to completion_queue
                        allocator_.release(sub, recv_block);
                    }
                }
            }
        }
        
        // Measure batch reclaim time (triggered by next loan)
        auto start = std::chrono::high_resolution_clock::now();
        
        SharedMemoryMemoryBlock dummy_block;
        allocator_.loan(pub_, 256, dummy_block);  // Triggers retrieveReturnedSamples()
        
        auto end = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        
        state.SetIterationTime(elapsed.count() / 1e9);
        
        if (dummy_block.chunk_header) {
            allocator_.send(pub_, dummy_block);
        }
    }
}

BENCHMARK_REGISTER_F(CompletionQueueBenchmark, BatchedReclaim)
    ->Args({1})
    ->Args({10})
    ->Args({50})
    ->UseManualTime()
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
