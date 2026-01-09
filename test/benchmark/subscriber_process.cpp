/**
 * @file        subscriber_process.cpp
 * @author      LightAP Core Team
 * @brief       Subscriber Process for Stress Test
 * @date        2026-01-08
 * @details     Independent subscriber process for multi-process stress testing
 * @copyright   Copyright (c) 2026
 */

#include "ipc/Subscriber.hpp"
#include "../../source/src/ipc/Subscriber.cpp"
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
    (void)signum;  // Suppress unused parameter warning
    g_running.store(false);
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
        std::cerr << "用法: " << argv[0] << " <service_name> <subscriber_id> <stats_file>" << std::endl;
        return 1;
    }
    
    const char* service_name = argv[1];
    int subscriber_id = std::atoi(argv[2]);
    const char* stats_file = argv[3];
    
    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 创建订阅者
    SubscriberConfig config{};
    auto sub_result = Subscriber<StressMessage>::Create(service_name, config);
    
    if (!sub_result.HasValue()) {
        std::cerr << "[订阅者-" << subscriber_id << "] 创建失败, PID=" << getpid() << std::endl;
        
        std::ofstream ofs(stats_file);
        if (ofs.is_open()) {
            ofs << "type=subscriber\n";
            ofs << "id=" << subscriber_id << "\n";
            ofs << "pid=" << getpid() << "\n";
            ofs << "status=error\n";
            ofs << "error=create_failed\n";
            ofs.close();
        }
        return 1;
    }
    
    auto subscriber = std::move(sub_result).Value();
    
    std::cout << "[订阅者-" << subscriber_id << "] 已连接, PID=" << getpid() << std::endl;
    
    UInt64 total_received = 0;
    UInt64 total_lost = 0;
    UInt64 last_sequence = 0;
    auto start_time = steady_clock::now();
    auto last_stats_time = start_time;
    
    while (g_running.load()) {
        auto result = subscriber.Receive(QueueEmptyPolicy::kSkip);
        
        if (result.HasValue()) {
            auto sample = std::move(result).Value();
            const StressMessage& msg = *sample;
            
            // 检测消息丢失
            if (last_sequence > 0 && msg.sequence > last_sequence + 1) {
                UInt64 lost = msg.sequence - last_sequence - 1;
                total_lost += lost;
            }
            last_sequence = msg.sequence;
            total_received++;
        } else {
            // 队列空，短暂休眠
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        
        // 每10秒更新统计文件
        auto now = steady_clock::now();
        if (duration_cast<seconds>(now - last_stats_time).count() >= 10) {
            auto elapsed = duration_cast<seconds>(now - start_time).count();
            
            std::ofstream ofs(stats_file);
            if (ofs.is_open()) {
                ofs << "type=subscriber\n";
                ofs << "id=" << subscriber_id << "\n";
                ofs << "pid=" << getpid() << "\n";
                ofs << "elapsed_sec=" << elapsed << "\n";
                ofs << "total_received=" << total_received << "\n";
                ofs << "total_lost=" << total_lost << "\n";
                ofs << "rate=" << (elapsed > 0 ? total_received / elapsed : 0) << "\n";
                ofs << "status=running\n";
                ofs.close();
            }
            
            last_stats_time = now;
        }
    }
    
    // 最终统计
    auto elapsed = duration_cast<seconds>(steady_clock::now() - start_time).count();
    
    std::cout << "[订阅者-" << subscriber_id << "] 断开连接, PID=" << getpid() << std::endl;
    std::cout << "  接收: " << total_received << " 消息" << std::endl;
    std::cout << "  丢失: " << total_lost << " 消息" << std::endl;
    std::cout << "  运行时长: " << elapsed << " 秒" << std::endl;
    
    // 保存最终统计
    std::ofstream ofs(stats_file);
    if (ofs.is_open()) {
        ofs << "type=subscriber\n";
        ofs << "id=" << subscriber_id << "\n";
        ofs << "pid=" << getpid() << "\n";
        ofs << "elapsed_sec=" << elapsed << "\n";
        ofs << "total_received=" << total_received << "\n";
        ofs << "total_lost=" << total_lost << "\n";
        ofs << "rate=" << (elapsed > 0 ? total_received / elapsed : 0) << "\n";
        ofs << "status=completed\n";
        ofs.close();
    }
    
    return 0;
}
