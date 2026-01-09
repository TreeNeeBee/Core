/**
 * @file        publisher_process.cpp
 * @author      LightAP Core Team
 * @brief       Publisher Process for Stress Test
 * @date        2026-01-08
 * @details     Independent publisher process for multi-process stress testing
 * @copyright   Copyright (c) 2026
 */

#include "ipc/Publisher.hpp"
#include "../../source/src/ipc/Publisher.cpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <signal.h>
#include <fstream>
#include <sys/mman.h>

using namespace lap::core;
using namespace lap::core::ipc;
using namespace std::chrono;

static std::atomic<bool> g_running{true};

static void signal_handler(int signum)
{
    std::cout << "[发布者] 收到信号 " << signum << ", 准备退出..." << std::endl;
    g_running.store(false);
}

static void cleanup_shm(const char* name)
{
    String path = "/lightap_ipc_" + String(name);
    shm_unlink(path.c_str());
}

struct StressMessage
{
    UInt64 sequence;
    UInt64 timestamp_ns;
    UInt32 publisher_id;
    UInt32 padding;  // 对齐
    char payload[4072];  // 总共4096字节 (4KB)
};

int main(int argc, char* argv[])
{
    if (argc < 4) {
        std::cerr << "用法: " << argv[0] << " <service_name> <rate_msg_per_sec> <stats_file>" << std::endl;
        return 1;
    }
    
    const char* service_name = argv[1];
    int rate = std::atoi(argv[2]);
    const char* stats_file = argv[3];
    
    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "[发布者] 启动" << std::endl;
    std::cout << "  服务名: " << service_name << std::endl;
    std::cout << "  发送速率: " << rate << " msg/s" << std::endl;
    std::cout << "  统计文件: " << stats_file << std::endl;
    std::cout << "  PID: " << getpid() << std::endl;
    
    // 清理旧的共享内存
    cleanup_shm(service_name);
    
    // 创建发布者
    PublisherConfig config{};
    config.max_chunks = 256;  // 增加以支持更多订阅者和更大消息
    config.chunk_size = sizeof(StressMessage) + sizeof(ChunkHeader);
    
    auto pub_result = Publisher<StressMessage>::Create(service_name, config);
    if (!pub_result.HasValue()) {
        std::cerr << "[发布者] 创建失败" << std::endl;
        return 1;
    }
    
    auto publisher = std::move(pub_result).Value();
    std::cout << "[发布者] 创建成功，开始发送..." << std::endl;
    
    UInt64 sequence = 0;
    UInt64 total_sent = 0;
    UInt64 total_errors = 0;
    auto sleep_interval = std::chrono::microseconds(1000000 / rate);
    auto start_time = steady_clock::now();
    auto last_stats_time = start_time;
    
    while (g_running.load()) {
        StressMessage msg;
        msg.sequence = ++sequence;
        msg.timestamp_ns = duration_cast<nanoseconds>(
            steady_clock::now().time_since_epoch()).count();
        msg.publisher_id = getpid();
        
        snprintf(msg.payload, sizeof(msg.payload), 
                "Seq=%lu, PID=%u", msg.sequence, msg.publisher_id);
        
        auto result = publisher.SendCopy(msg);
        if (result.HasValue()) {
            total_sent++;
        } else {
            total_errors++;
        }
        
        // 每10秒更新统计文件
        auto now = steady_clock::now();
        if (duration_cast<seconds>(now - last_stats_time).count() >= 10) {
            auto elapsed = duration_cast<seconds>(now - start_time).count();
            
            std::ofstream ofs(stats_file);
            if (ofs.is_open()) {
                ofs << "type=publisher\n";
                ofs << "pid=" << getpid() << "\n";
                ofs << "elapsed_sec=" << elapsed << "\n";
                ofs << "total_sent=" << total_sent << "\n";
                ofs << "total_errors=" << total_errors << "\n";
                ofs << "rate=" << (elapsed > 0 ? total_sent / elapsed : 0) << "\n";
                ofs.close();
            }
            
            last_stats_time = now;
        }
        
        std::this_thread::sleep_for(sleep_interval);
    }
    
    // 最终统计
    auto elapsed = duration_cast<seconds>(steady_clock::now() - start_time).count();
    std::cout << "\n[发布者] 停止发送" << std::endl;
    std::cout << "  总发送: " << total_sent << " 消息" << std::endl;
    std::cout << "  错误数: " << total_errors << std::endl;
    std::cout << "  运行时长: " << elapsed << " 秒" << std::endl;
    std::cout << "  平均速率: " << (elapsed > 0 ? total_sent / elapsed : 0) << " msg/s" << std::endl;
    
    // 保存最终统计
    std::ofstream ofs(stats_file);
    if (ofs.is_open()) {
        ofs << "type=publisher\n";
        ofs << "pid=" << getpid() << "\n";
        ofs << "elapsed_sec=" << elapsed << "\n";
        ofs << "total_sent=" << total_sent << "\n";
        ofs << "total_errors=" << total_errors << "\n";
        ofs << "rate=" << (elapsed > 0 ? total_sent / elapsed : 0) << "\n";
        ofs << "status=completed\n";
        ofs.close();
    }
    
    return 0;
}
