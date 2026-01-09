/**
 * @file        benchmark_ipc_throughput.cpp
 * @author      LightAP Core Team
 * @brief       IPC Throughput Benchmark (Multi-Process)
 * @date        2026-01-08
 * @details     Measures message throughput using separate processes
 * @copyright   Copyright (c) 2026
 */

#include "ipc/Publisher.hpp"
#include "ipc/Subscriber.hpp"
#include "../../source/src/ipc/Publisher.cpp"
#include "../../source/src/ipc/Subscriber.cpp"
#include <iostream>
#include <chrono>
#include <atomic>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

using namespace lap::core;
using namespace lap::core::ipc;
using namespace std::chrono;

static void cleanup_shm(const char* name)
{
    String path = "/lightap_ipc_" + String(name);
    shm_unlink(path.c_str());
}

// Shared memory for statistics
struct BenchmarkStats
{
    std::atomic<bool> running;
    std::atomic<UInt64> sent_count;
    std::atomic<UInt64> received_count;
    
    BenchmarkStats() : running(true), sent_count(0), received_count(0) {}
};

static BenchmarkStats* CreateStatsShm(const char* name)
{
    String shm_name = String("/bench_stats_") + name;
    shm_unlink(shm_name.c_str());
    
    int fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, 0666);
    if (fd < 0)
    {
        std::cerr << "Failed to create stats shared memory" << std::endl;
        return nullptr;
    }
    
    if (ftruncate(fd, sizeof(BenchmarkStats)) < 0)
    {
        close(fd);
        return nullptr;
    }
    
    void* addr = mmap(nullptr, sizeof(BenchmarkStats), 
                      PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    
    if (addr == MAP_FAILED)
    {
        return nullptr;
    }
    
    return new (addr) BenchmarkStats();
}

static void CleanupStatsShm(const char* name, BenchmarkStats* stats)
{
    if (stats)
    {
        munmap(stats, sizeof(BenchmarkStats));
    }
    String shm_name = String("/bench_stats_") + name;
    shm_unlink(shm_name.c_str());
}

template<size_t PayloadSize>
struct Message
{
    UInt64 sequence;
    UInt8 payload[PayloadSize];
};

template<size_t PayloadSize>
void RunSubscriberProcess(const char* service_name, BenchmarkStats* stats)
{
    using MsgType = Message<PayloadSize>;
    
    // Create Subscriber in child process
    SubscriberConfig sub_config{};
    auto sub_result = Subscriber<MsgType>::Create(service_name, sub_config);
    if (!sub_result.HasValue())
    {
        std::cerr << "[Subscriber Process] Failed to create subscriber" << std::endl;
        exit(1);
    }
    auto subscriber = std::move(sub_result).Value();
    
    // Receive messages until stopped
    while (stats->running.load(std::memory_order_relaxed))
    {
        auto result = subscriber.Receive();
        if (result.HasValue())
        {
            stats->received_count.fetch_add(1, std::memory_order_relaxed);
        }
        else
        {
            // Small sleep to avoid busy-wait when queue is empty
            usleep(1);
        }
    }
    
    exit(0);
}

template<size_t PayloadSize>
void RunThroughputBenchmark(const char* service_name, int duration_seconds)
{
    using MsgType = Message<PayloadSize>;
    
    cleanup_shm(service_name);
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "Throughput Benchmark - Payload: " << PayloadSize << " bytes" << std::endl;
    std::cout << "Duration: " << duration_seconds << " seconds" << std::endl;
    std::cout << "Mode: Multi-Process (真实进程间通信)" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // Create shared memory for statistics
    BenchmarkStats* stats = CreateStatsShm(service_name);
    if (!stats)
    {
        std::cerr << "Failed to create stats shared memory" << std::endl;
        return;
    }
    
    // Create Publisher in parent process
    PublisherConfig pub_config{};
    pub_config.max_chunks = 128;
    pub_config.chunk_size = sizeof(MsgType);
    pub_config.auto_cleanup = false;  // Let parent cleanup
    
    auto pub_result = Publisher<MsgType>::Create(service_name, pub_config);
    if (!pub_result.HasValue())
    {
        std::cerr << "[Publisher Process] Failed to create publisher" << std::endl;
        CleanupStatsShm(service_name, stats);
        return;
    }
    auto publisher = std::move(pub_result).Value();
    
    // Fork subscriber process
    pid_t pid = fork();
    
    if (pid < 0)
    {
        std::cerr << "Failed to fork subscriber process" << std::endl;
        CleanupStatsShm(service_name, stats);
        return;
    }
    else if (pid == 0)
    {
        // Child process - run subscriber
        RunSubscriberProcess<PayloadSize>(service_name, stats);
        // Never reaches here
    }
    
    // Parent process - run publisher
    // Give subscriber time to initialize
    usleep(100000);  // 100ms
    
    std::cout << "[Parent] Publisher process: " << getpid() << std::endl;
    std::cout << "[Parent] Subscriber process: " << pid << std::endl;
    std::cout << "[Parent] Running benchmark for " << duration_seconds << " seconds..." << std::endl;
    
    // Record start time
    auto start_time = high_resolution_clock::now();
    auto end_time = start_time + seconds(duration_seconds);
    
    // Publisher loop
    UInt64 seq = 0;
    while (high_resolution_clock::now() < end_time)
    {
        MsgType msg;
        msg.sequence = seq++;
        
        if (publisher.SendCopy(msg).HasValue())
        {
            stats->sent_count.fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    // Stop subscriber
    stats->running.store(false, std::memory_order_release);
    
    // Wait for subscriber to finish
    int status;
    waitpid(pid, &status, 0);
    
    // Calculate results
    UInt64 total_sent = stats->sent_count.load();
    UInt64 total_received = stats->received_count.load();
    double msg_per_sec = static_cast<double>(total_received) / duration_seconds;
    double mb_per_sec = (msg_per_sec * sizeof(MsgType)) / (1024.0 * 1024.0);
    
    std::cout << "\n结果:" << std::endl;
    std::cout << "  发送消息数:     " << total_sent << std::endl;
    std::cout << "  接收消息数:     " << total_received << std::endl;
    std::cout << "  丢失率:         " << (100.0 * (total_sent - total_received) / total_sent) << "%" << std::endl;
    std::cout << "  吞吐量:         " << static_cast<UInt64>(msg_per_sec) << " msg/s" << std::endl;
    std::cout << "  带宽:           " << mb_per_sec << " MB/s" << std::endl;
    
    // Cleanup
    CleanupStatsShm(service_name, stats);
    cleanup_shm(service_name);
}

int main()
{
    std::cout << "=====================================" << std::endl;
    std::cout << "  IPC 吞吐量基准测试 (多进程模式)" << std::endl;
    std::cout << "=====================================" << std::endl;
    std::cout << "\n测试说明:" << std::endl;
    std::cout << "  - Publisher 运行在父进程" << std::endl;
    std::cout << "  - Subscriber 运行在子进程" << std::endl;
    std::cout << "  - 真实的进程间零拷贝通信" << std::endl;
    
    const int duration = 5;  // seconds
    
    // Test different message sizes
    RunThroughputBenchmark<64>("thr_bench_64", duration);
    RunThroughputBenchmark<256>("thr_bench_256", duration);
    RunThroughputBenchmark<1024>("thr_bench_1k", duration);
    RunThroughputBenchmark<4096>("thr_bench_4k", duration);
    
    std::cout << "\n=====================================" << std::endl;
    std::cout << "  基准测试完成!" << std::endl;
    std::cout << "=====================================" << std::endl;
    return 0;
}
