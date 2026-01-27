/**
 * @file        ipc_chain_example.cpp
 * @brief       IPC链式传递示例 - 演示STMin限流与多区域消息传递
 * @details     使用SHRINK模式演示IPC框架的STMin限流功能和链式消息转发
 * @author      LightAP Core Team
 * @date        2026-01-17
 *
 * ============================================================================
 * 链式结构图
 * ============================================================================
 *
 *   [Proc0: 初始发布者]
 *         |
 *         | 100条消息 (0.1ms间隔)
 *         v
 *    ┌─────────────────────────────────────────────────────────────┐
 *    │  Region 0 (共享内存: /lightap_chain_region0)                │
 *    └─────────────────────────────────────────────────────────────┘
 *         |                                  |
 *         | [Proc1: SubB0]                   | [Proc5: Monitor SubA0]
 *         | STMin=1ms                        | STMin=0ms (无限流)
 *         | Block策略                        | Block策略
 *         v                                  v
 *    [Pub1转发]                         (监控接收间隔)
 *         |
 *         v
 *    ┌─────────────────────────────────────────────────────────────┐
 *    │  Region 1 (共享内存: /lightap_chain_region1)                │
 *    └─────────────────────────────────────────────────────────────┘
 *         |                                  |
 *         | [Proc2: SubB1]                   | [Proc6: Monitor SubA1]
 *         | STMin=5ms                        | STMin=0ms (无限流)
 *         | Block策略                        | Block策略
 *         v                                  v
 *    [Pub2转发]                         (观测接收间隔~5ms)
 *         |
 *         v
 *    ┌─────────────────────────────────────────────────────────────┐
 *    │  Region 2 (共享内存: /lightap_chain_region2)                │
 *    └─────────────────────────────────────────────────────────────┘
 *         |                                  |
 *         | [Proc3: SubB2]                   | [Proc7: Monitor SubA2]
 *         | STMin=10ms                       | STMin=0ms (无限流)
 *         | Block策略                        | Block策略
 *         v                                  v
 *    [Pub3转发]                         (观测接收间隔~10ms)
 *         |
 *         v
 *    ┌─────────────────────────────────────────────────────────────┐
 *    │  Region 3 (共享内存: /lightap_chain_region3)                │
 *    └─────────────────────────────────────────────────────────────┘
 *         |                                  |
 *         | [Proc4: SubB3]                   | [Proc8: Monitor SubA3]
 *         | STMin=20ms                       | STMin=0ms (无限流)
 *         | Block策略                        | Block策略
 *         v                                  v
 *    [Pub4转发]                         (观测接收间隔~20ms)
 *         |
 *         v
 *    ┌─────────────────────────────────────────────────────────────┐
 *    │  Region 4 (共享内存: /lightap_chain_region4)                │
 *    └─────────────────────────────────────────────────────────────┘
 *         |                                  |
 *         | [Proc5: SubB4]                   | [Proc9: Monitor SubA4]
 *         | STMin=50ms                       | STMin=0ms (无限流)
 *         | Block策略                        | Block策略
 *         v                                  v
 *    (链尾订阅者)                       (观测接收间隔~50ms)
 *
 * ============================================================================
 * 进程架构说明
 * ============================================================================
 * 
 * 总进程数: 11个独立进程
 * 
 * 1. Proc0 (初始发布者):
 *    - 发送100条消息到Region 0
 *    - 发送间隔: 0.1ms (高速发送，测试STMin限流效果)
 *    - 消息包含: 序列号、区域ID、时间戳、负载数据
 * 
 * 2. Proc1-5 (转发器节点):
 *    - 每个进程包含: 订阅者(SubB) + 发布者(Pub)
 *    - STMin配置: 1ms, 5ms, 10ms, 20ms, 50ms (递增限流)
 *    - 策略: Block阻塞策略，自动实现STMin限流
 *    - 功能: 接收消息 -> 更新时间戳 -> 转发到下一区域
 * 
 * 3. Proc6-10 (监控节点):
 *    - 每个进程独立监控对应区域的消息流
 *    - STMin配置: 全部为0ms (不限流，真实观测消息到达速率)
 *    - 功能: 统计接收间隔、消息数量、传输延迟
 *    - 验证: 观测转发链中STMin的累积限流效果
 *
 * ============================================================================
 * 技术特性
 * ============================================================================
 * 
 * 1. SHRINK模式内存优化:
 *    - 每个共享内存区域: 2个订阅者 + 64队列容量
 *    - 内存占用: ~4KB/区域 (共5个区域: 20KB总计)
 *    - 适用场景: 轻量级多订阅者场景
 * 
 * 2. STMin限流机制:
 *    - **Publisher端实现**: STMin配置在Subscriber，但限流逻辑在Publisher::Send()中执行
 *    - **工作原理**: Publisher为每个Subscriber维护last_send时间戳，仅当 (now - last_send) >= STmin 时才发送
 *    - **优点**: 减少不必要的enqueue操作，节省共享内存队列空间
 *    - **局限**: Subscriber无法独立控制接收速率，依赖Publisher执行STMin检查
 *    - **注意**: 高速发送场景下(如本例0.1ms间隔)，STMin限流在Publisher端有效工作
 * 
 * 3. 零拷贝消息传递:
 *    - 共享内存机制，进程间高效通信
 *    - 消息大小: 64字节 (含序列号、时间戳、负载)
 *    - 避免内核态数据拷贝
 * 
 * 4. 独立进程架构:
 *    - 每个组件独立进程，故障隔离
 *    - 使用fork()创建子进程
 *    - 主进程waitpid()等待所有子进程完成
 *
 * ============================================================================
 * 运行方式
 * ============================================================================
 * 
 * cd /workspace/LightAP/build/modules/Core
 * make ipc_chain_example
 * ./ipc_chain_example
 *
 * ============================================================================
 * 预期输出
 * ============================================================================
 * 
 * 【接收间隔统计】
 * Monitor-0 (上游STMin=0ms):   接收间隔 ~1.1ms  (观测Proc0发送速率，受SubB0限流影响)
 * Monitor-1 (上游STMin=1ms):   接收间隔 ~1.8ms  (观测SubB0转发速率，STMin=1ms)
 * Monitor-2 (上游STMin=5ms):   接收间隔 ~6.2ms  (观测SubB1转发速率，STMin=5ms)
 * Monitor-3 (上游STMin=10ms):  接收间隔 ~11.4ms (观测SubB2转发速率，STMin=10ms)
 * Monitor-4 (上游STMin=20ms):  接收间隔 ~22ms   (观测SubB3转发速率，STMin=20ms)
 * 
 * 【延时统计】(30秒测试)
 * - 平均延时: 10us ~ 1200us (共享内存零拷贝机制)
 * - P50延时:  2us ~ 9us     (中位数延时极低，共享内存访问优势明显)
 * - P99延时:  89us ~ 10ms   (尾部延时劣化，见下方原因分析)
 * 
 * 【P99延时劣化原因】
 * 1. STMin阻塞调度延迟: Block策略使订阅者休眠STMin时长，唤醒时间存在10ms级内核调度抖动
 * 2. 多进程CPU竞争: 11个进程竞争CPU时间片，高负载时调度延迟达10ms级别
 * 3. 链式累积效应: Monitor-3/4的P99延时包含上游多级转发的累积调度延迟
 * 4. Overwrite策略开销: 覆盖老数据时的额外同步操作可能引入微小延迟
 * 
 * 注: P50延时稳定在微秒级说明共享内存零拷贝机制性能优异
 *     P99劣化主因进程调度抖动，非缓存耗尽或IPC机制问题
 *
 * ============================================================================
 * 验证要点
 * ============================================================================
 * 
 * 1. STMin累积限流效果: 观察Monitor接收间隔随转发链递增 (1ms→5ms→10ms→20ms)
 * 2. 转发器限流准确性: 每个SubB的STMin配置是否正确限制了转发速率
 * 3. Monitor观测准确性: Monitor自身STMin=0ms，真实反映上游发送速率
 * 4. 链式传递完整性: 验证消息序列号连续性和消息数量衰减规律
 * 5. 进程稳定性: 所有进程正常启动和退出，共享内存正确管理
 * 6. 性能基准: P50延时在微秒级，P99延时在多进程竞争下仍可控
 * 7. 上游监控: 每个Monitor显示的STMin是其监控的上游发布者配置
 */

#include "ipc/Publisher.hpp"
#include "ipc/Subscriber.hpp"
#include "CInitialization.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <algorithm>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

using namespace lap::core;
using namespace lap::core::ipc;

// 链式区域数量
const int kNumRegions = 5;

// 转发器STMin序列 (ms)
constexpr UInt32 kForwarderSTMin[kNumRegions] = {1, 5, 10, 20, 50};

// 监控器STMin序列 (ms) - 全部为0以真实观测消息到达速率
constexpr UInt32 kMonitorSTMin[kNumRegions] = {0, 0, 0, 0, 0};

// 消息结构
struct ChainMessage {
    UInt32 sequence_id;      // 序列号
    UInt32 region_id;        // 当前区域ID (0-3)
    UInt64 timestamp_us;     // 时间戳(微秒)
    char payload[48];        // 负载数据
};

// 统计数据结构 - 用于共享内存
struct MonitorStats {
    int monitor_id;           // Monitor ID
    UInt32 stmin;             // 上游发布者的STMin配置(ms) - Monitor监控的根本目的
    UInt32 msg_count;         // 消息数量
    double avg_latency;       // 平均延时(us)
    UInt64 p50_latency;       // P50延时(us)
    UInt64 p99_latency;       // P99延时(us)
    double avg_interval;      // 平均间隔(ms)
    double p50_interval;      // P50间隔(ms)
    double p99_interval;      // P99间隔(ms)
    bool valid;               // 数据是否有效
};

// 获取当前时间戳(微秒)
inline UInt64 GetTimestampUs() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
}

// 生成共享内存路径
inline std::string GetShmPath(int region_id) {
    return "/lightap_chain_region" + std::to_string(region_id);
}

//=============================================================================
// 进程0: 初始发布者
//=============================================================================
void RunInitialPublisher() {
    std::cout << "[Proc0] 初始发布者启动 (PID=" << getpid() << ")" << std::endl;
    
    // 创建Publisher for Region 0
    PublisherConfig config;
    config.chunk_size = sizeof(ChainMessage);
    config.max_chunks = 16;
    config.policy = PublishPolicy::kOverwrite;  // 使用Overwrite策略，覆盖老数据
    
    auto pub_result = Publisher::Create(GetShmPath(0), config);
    if (!pub_result.HasValue()) {
        std::cerr << "[Proc0] 创建Publisher失败" << std::endl;
        return;
    }
    auto publisher = std::move(pub_result).Value();
    
    std::cout << "[Proc0] Publisher创建成功: " << GetShmPath(0) << std::endl;
    
    // 等待订阅者就绪
    std::cout << "[Proc0] 等待订阅者就绪..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 持续发送消息30秒，间隔0.1ms
    UInt32 send_fail_count = 0;
    UInt32 sequence = 0;
    auto start_time = std::chrono::steady_clock::now();
    
    while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(30)) {
        ChainMessage msg;
        msg.sequence_id = sequence++;
        msg.region_id = 0;
        msg.timestamp_us = GetTimestampUs();
        std::snprintf(msg.payload, sizeof(msg.payload), 
                     "Message#%u from Region0", msg.sequence_id);
        
        auto result = publisher.Send(reinterpret_cast<Byte*>(&msg), sizeof(msg));
        if (!result.HasValue()) {
            send_fail_count++;
            if (send_fail_count <= 5) {  // 只打印前5次失败
                std::cout << "[Proc0 WARN] 发送失败 seq=" << msg.sequence_id << ", 累计失败=" << send_fail_count << std::endl;
            }
        }
        
        // 0.1ms间隔发送
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    UInt32 total_sent = sequence;
    std::cout << "[Proc0] 发送完成: " << total_sent << " 条消息, 失败=" << send_fail_count << std::endl;
}

//=============================================================================
// 链式转发进程 (SubB + Pub) - 每个区域独立进程
// 使用Block策略，STMin控制接收间隔，直接路由转发
//=============================================================================
void RunForwarderNode(int region_id) {
    std::cout << "[Forwarder-" << region_id << "] 启动 (PID=" << getpid() 
             << ", STMin=" << kForwarderSTMin[region_id] << "ms)" << std::endl;
    
    // 创建当前区域的Subscriber
    SubscriberConfig sub_config;
    sub_config.chunk_size = sizeof(ChainMessage);
    sub_config.max_chunks = 16;
    sub_config.STmin = kForwarderSTMin[region_id];
    sub_config.empty_policy = SubscribePolicy::kBlock;  // 使用阻塞策略
    
    auto sub_result = Subscriber::Create(GetShmPath(region_id), sub_config);
    if (!sub_result.HasValue()) {
        std::cerr << "[Forwarder-" << region_id << "] 创建Subscriber失败" << std::endl;
        return;
    }
    auto subscriber = std::move(sub_result).Value();
    subscriber.Connect();
    
    std::cout << "[Forwarder-" << region_id << "] SubB" << region_id 
             << " 创建成功，使用Block策略" << std::endl;
    
    // 如果不是最后一个区域，创建下一个区域的Publisher
    bool is_last_region = (region_id >= kNumRegions - 1);
    
    std::shared_ptr<Publisher> publisher_ptr = nullptr;
    if (!is_last_region) {
        // 等待下一个区域的Subscriber先创建（它会初始化共享内存）
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        PublisherConfig pub_config;
        pub_config.chunk_size = sizeof(ChainMessage);
        pub_config.max_chunks = 16;
        pub_config.policy = PublishPolicy::kOverwrite;  // 使用Overwrite策略
        
        auto pub_result = Publisher::Create(GetShmPath(region_id + 1), pub_config);
        if (!pub_result.HasValue()) {
            std::cerr << "[Forwarder-" << region_id << "] 创建Publisher失败" << std::endl;
            return;
        }
        publisher_ptr = std::make_shared<Publisher>(std::move(pub_result).Value());
        std::cout << "[Forwarder-" << region_id << "] Pub" << (region_id + 1) 
                 << " 创建成功" << std::endl;
    }
    
    std::cout << "[Forwarder-" << region_id << "] 开始接收并转发..." << std::endl;
    
    // 接收并转发循环 (使用Block策略，STMin自动控制间隔)
    UInt32 msg_count = 0;
    UInt32 send_fail_count = 0;
    auto start_time = std::chrono::steady_clock::now();
    
    while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(30)) {
        // Block策略会根据STMin自动控制接收频率
        auto sample_result = subscriber.Receive(SubscribePolicy::kBlock);
        
        if (sample_result.HasValue()) {
            ChainMessage msg;
            sample_result.Value().Read(reinterpret_cast<Byte*>(&msg), sizeof(msg));
            msg_count++;
            
            // 直接路由到下一个区域
            if (!is_last_region && publisher_ptr) {
                msg.region_id++;
                msg.timestamp_us = GetTimestampUs();
                auto send_result = publisher_ptr->Send(reinterpret_cast<Byte*>(&msg), sizeof(msg));
                if (!send_result.HasValue()) {
                    send_fail_count++;
                    if (send_fail_count <= 5) {
                        std::cout << "[Forwarder-" << region_id << " WARN] 转发失败 seq=" << msg.sequence_id 
                                 << ", 累计=" << send_fail_count << std::endl;
                    }
                }
            }
        }
    }
    
    std::cout << "[Forwarder-" << region_id << "] 完成，共处理 " 
             << msg_count << " 条消息"
             << ", Send失败=" << send_fail_count << std::endl;
}

//=============================================================================
// 监控订阅者进程 (SubA) - 验证STMin限流效果
//=============================================================================
void RunMonitorNode(int region_id) {
    std::cout << "[Monitor-" << region_id << "] 启动 (PID=" << getpid() 
             << ", STMin=" << kMonitorSTMin[region_id] << "ms)" << std::endl;
    
    // 创建监控Subscriber
    SubscriberConfig config;
    config.chunk_size = sizeof(ChainMessage);
    config.max_chunks = 16;
    config.STmin = kMonitorSTMin[region_id];
    config.empty_policy = SubscribePolicy::kBlock;
    
    auto sub_result = Subscriber::Create(GetShmPath(region_id), config);
    if (!sub_result.HasValue()) {
        std::cerr << "[Monitor-" << region_id << "] 创建Subscriber失败" << std::endl;
        return;
    }
    
    auto subscriber = std::move(sub_result).Value();
    subscriber.Connect();
    
    std::cout << "[Monitor-" << region_id << "] SubA" << region_id 
             << " 创建成功 (STMin=" << kMonitorSTMin[region_id] << "ms)" << std::endl;
    
    // 监控循环
    UInt32 msg_count = 0;
    auto start_time = std::chrono::steady_clock::now();
    UInt64 last_receive_time = 0;
    
    // 用于统计延时
    std::vector<UInt64> latencies;
    std::vector<UInt64> intervals;
    latencies.reserve(1000);
    intervals.reserve(1000);
    
    while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(30)) {
        auto sample_result = subscriber.Receive(SubscribePolicy::kBlock);
        
        if (sample_result.HasValue()) {
            ChainMessage msg;
            sample_result.Value().Read(reinterpret_cast<Byte*>(&msg), sizeof(msg));
            
            UInt64 recv_timestamp = GetTimestampUs();
            UInt64 latency_us = recv_timestamp - msg.timestamp_us;
            UInt64 interval_us = (last_receive_time > 0) ? (recv_timestamp - last_receive_time) : 0;
            msg_count++;
            
            // 记录延时数据
            latencies.push_back(latency_us);
            if (interval_us > 0) {
                intervals.push_back(interval_us);
            }
            
            last_receive_time = recv_timestamp;
        }
    }
    
    // 计算延时统计
    double avg_latency = 0;
    UInt64 p50_latency = 0;
    UInt64 p99_latency = 0;
    double avg_interval = 0;
    UInt64 p50_interval = 0;
    UInt64 p99_interval = 0;
    
    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());
        UInt64 sum_latency = 0;
        for (auto lat : latencies) sum_latency += lat;
        avg_latency = static_cast<double>(sum_latency) / latencies.size();
        p50_latency = latencies[latencies.size() * 50 / 100];
        p99_latency = latencies[latencies.size() * 99 / 100];
        
        std::cout << "[Monitor-" << region_id << "] ┌────────────────────────────────────────────────────────────┐" << std::endl;
        std::cout << "[Monitor-" << region_id << "] │  延时统计 (Latency Statistics) - " << msg_count << " 条消息" << std::string(15 - std::to_string(msg_count).length(), ' ') << "│" << std::endl;
        std::cout << "[Monitor-" << region_id << "] ├────────────────┬───────────────────────────────────────────┤" << std::endl;
        std::cout << "[Monitor-" << region_id << "] │  指标          │  数值                                     │" << std::endl;
        std::cout << "[Monitor-" << region_id << "] ├────────────────┼───────────────────────────────────────────┤" << std::endl;
        
        char buf[100];
        std::snprintf(buf, sizeof(buf), "[Monitor-%d] │  平均延时      │  %-6.2f us                               │", region_id, avg_latency);
        std::cout << buf << std::endl;
        
        std::snprintf(buf, sizeof(buf), "[Monitor-%d] │  P50 延时      │  %-6llu us                               │", region_id, (unsigned long long)p50_latency);
        std::cout << buf << std::endl;
        
        std::snprintf(buf, sizeof(buf), "[Monitor-%d] │  P99 延时      │  %-6llu us                               │", region_id, (unsigned long long)p99_latency);
        std::cout << buf << std::endl;
        
        std::cout << "[Monitor-" << region_id << "] └────────────────┴───────────────────────────────────────────┘" << std::endl;
    }
    
    // 计算接收间隔统计
    if (!intervals.empty()) {
        std::sort(intervals.begin(), intervals.end());
        UInt64 sum_interval = 0;
        for (auto intv : intervals) sum_interval += intv;
        avg_interval = static_cast<double>(sum_interval) / intervals.size();
        p50_interval = intervals[intervals.size() * 50 / 100];
        p99_interval = intervals[intervals.size() * 99 / 100];
        
        std::cout << "[Monitor-" << region_id << "] ┌────────────────────────────────────────────────────────────┐" << std::endl;
        std::cout << "[Monitor-" << region_id << "] │  接收间隔统计 (Interval Statistics)                        │" << std::endl;
        std::cout << "[Monitor-" << region_id << "] ├────────────────┬───────────────────────────────────────────┤" << std::endl;
        std::cout << "[Monitor-" << region_id << "] │  指标          │  数值                                     │" << std::endl;
        std::cout << "[Monitor-" << region_id << "] ├────────────────┼───────────────────────────────────────────┤" << std::endl;
        
        char buf[100];
        std::snprintf(buf, sizeof(buf), "[Monitor-%d] │  平均间隔      │  %-6.2f ms                               │", region_id, avg_interval / 1000.0);
        std::cout << buf << std::endl;
        
        std::snprintf(buf, sizeof(buf), "[Monitor-%d] │  P50 间隔      │  %-6.2f ms                               │", region_id, p50_interval / 1000.0);
        std::cout << buf << std::endl;
        
        std::snprintf(buf, sizeof(buf), "[Monitor-%d] │  P99 间隔      │  %-6.2f ms                               │", region_id, p99_interval / 1000.0);
        std::cout << buf << std::endl;
        
        std::cout << "[Monitor-" << region_id << "] └────────────────┴───────────────────────────────────────────┘" << std::endl;
    }
    
    // 输出CSV格式的汇总数据
    std::cout << "[STATS]Monitor-" << region_id 
             << "," << msg_count
             << "," << avg_latency
             << "," << p50_latency
             << "," << p99_latency
             << "," << (avg_interval / 1000.0)
             << "," << (p50_interval / 1000.0)
             << "," << (p99_interval / 1000.0)
             << std::endl;
    
    // 将统计数据写入共享内存
    int stats_fd = shm_open("/lightap_chain_stats", O_RDWR, 0666);
    if (stats_fd >= 0) {
        MonitorStats* stats_array = (MonitorStats*)mmap(nullptr, sizeof(MonitorStats) * kNumRegions,
                                                         PROT_READ | PROT_WRITE, MAP_SHARED, stats_fd, 0);
        if (stats_array != MAP_FAILED) {
            stats_array[region_id].monitor_id = region_id;
            // Monitor-i监控的上游STMin: Region 0上游是Proc0(0ms), Region i上游是SubB(i-1)
            stats_array[region_id].stmin = (region_id == 0) ? 0 : kForwarderSTMin[region_id - 1];
            stats_array[region_id].msg_count = msg_count;
            stats_array[region_id].avg_latency = avg_latency;
            stats_array[region_id].p50_latency = p50_latency;
            stats_array[region_id].p99_latency = p99_latency;
            stats_array[region_id].avg_interval = avg_interval / 1000.0;
            stats_array[region_id].p50_interval = p50_interval / 1000.0;
            stats_array[region_id].p99_interval = p99_interval / 1000.0;
            stats_array[region_id].valid = true;
            munmap(stats_array, sizeof(MonitorStats) * kNumRegions);
        }
        close(stats_fd);
    }
    
    std::cout << "[Monitor-" << region_id << "] 完成，共监控 " 
             << msg_count << " 条消息" << std::endl;
}

//=============================================================================
// 主函数
//=============================================================================
int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "IPC链式传递示例 - SHRINK模式" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "区域数量: " << kNumRegions << std::endl;
    std::cout << "转发器STMin: ";
    for (int i = 0; i < kNumRegions; ++i) {
        std::cout << kForwarderSTMin[i] << "ms ";
    }
    std::cout << "\n监控器STMin: ";
    for (int i = 0; i < kNumRegions; ++i) {
        std::cout << kMonitorSTMin[i] << "ms ";
    }
    std::cout << "\n进程架构: 1个初始发布者 + " << kNumRegions 
             << "个转发器 + " << kNumRegions << "个监控器" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    // 清理旧的共享内存
    for (int i = 0; i < kNumRegions; ++i) {
        std::string cleanup = "rm -f /dev/shm" + GetShmPath(i);
        system(cleanup.c_str());
    }
    shm_unlink("/lightap_chain_stats");
    
    // 创建统计共享内存
    int stats_fd = shm_open("/lightap_chain_stats", O_CREAT | O_RDWR, 0666);
    if (stats_fd < 0) {
        std::cerr << "创建统计共享内存失败" << std::endl;
        return 1;
    }
    ftruncate(stats_fd, sizeof(MonitorStats) * kNumRegions);
    MonitorStats* stats_array = (MonitorStats*)mmap(nullptr, sizeof(MonitorStats) * kNumRegions,
                                                     PROT_READ | PROT_WRITE, MAP_SHARED, stats_fd, 0);
    if (stats_array == MAP_FAILED) {
        std::cerr << "映射统计共享内存失败" << std::endl;
        close(stats_fd);
        return 1;
    }
    // 初始化统计数据
    for (int i = 0; i < kNumRegions; ++i) {
        stats_array[i].valid = false;
    }
    munmap(stats_array, sizeof(MonitorStats) * kNumRegions);
    close(stats_fd);
    
    std::vector<pid_t> child_pids;
    
    // Fork进程0: 初始发布者
    pid_t pid0 = fork();
    if (pid0 == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        RunInitialPublisher();
        exit(0);
    }
    child_pids.push_back(pid0);
    
    // Fork进程1-4: 链式转发器 (每个区域独立进程)
    for (int region = 0; region < kNumRegions; ++region) {
        pid_t pid = fork();
        if (pid == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            RunForwarderNode(region);
            exit(0);
        }
        child_pids.push_back(pid);
    }
    
    // Fork进程5-8: 监控订阅者 (每个区域独立进程)
    for (int region = 0; region < kNumRegions; ++region) {
        pid_t pid = fork();
        if (pid == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            RunMonitorNode(region);
            exit(0);
        }
        child_pids.push_back(pid);
    }
    
    // 主进程等待所有子进程
    std::cout << "[Main] 已启动 " << child_pids.size() << " 个子进程" << std::endl;
    std::cout << "[Main] 等待所有子进程完成..." << std::endl;
    
    // 等待所有子进程
    int status;
    for (auto pid : child_pids) {
        waitpid(pid, &status, 0);
    }
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "IPC链式传递示例完成" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // 读取并打印统计汇总表格
    stats_fd = shm_open("/lightap_chain_stats", O_RDONLY, 0666);
    if (stats_fd >= 0) {
        stats_array = (MonitorStats*)mmap(nullptr, sizeof(MonitorStats) * kNumRegions,
                                          PROT_READ, MAP_SHARED, stats_fd, 0);
        if (stats_array != MAP_FAILED) {
            std::cout << "\n╔═════════════════════════════════════════════════════════════════════════════════════════════════════════════╗" << std::endl;
            std::cout << "║                              IPC链式传递 - 延时统计汇总表                                                  ║" << std::endl;
            std::cout << "╠═══════════╦═══════╦═════════╦═══════════╦══════════╦══════════╦═══════════╦══════════╦══════════╣" << std::endl;
            std::cout << "║  Monitor  ║ STMin ║ 消息数  ║ 平均延时  ║ P50延时  ║ P99延时  ║ 平均间隔  ║ P50间隔  ║ P99间隔  ║" << std::endl;
            std::cout << "╠═══════════╬═══════╬═════════╬═══════════╬══════════╬══════════╬═══════════╬══════════╬══════════╣" << std::endl;
            
            for (int i = 0; i < kNumRegions; ++i) {
                if (stats_array[i].valid) {
                    char line[300];
                    std::snprintf(line, sizeof(line),
                                 "║ Monitor-%d ║ %3ums ║ %5u条 ║ %7.2f us ║ %6llu us ║ %6llu us ║ %7.2f ms ║ %6.2f ms ║ %6.2f ms ║",
                                 i,
                                 stats_array[i].stmin,
                                 stats_array[i].msg_count,
                                 stats_array[i].avg_latency,
                                 (unsigned long long)stats_array[i].p50_latency,
                                 (unsigned long long)stats_array[i].p99_latency,
                                 stats_array[i].avg_interval,
                                 stats_array[i].p50_interval,
                                 stats_array[i].p99_interval);
                    std::cout << line << std::endl;
                }
            }
            
            std::cout << "╚═══════════╩═══════╩═════════╩═══════════╩══════════╩══════════╩═══════════╩══════════╩══════════╝" << std::endl;
            
            // 添加说明
            std::cout << "\n说明：" << std::endl;
            std::cout << "- STMin列：上游发布者的STMin配置（Monitor监控的根本目的）" << std::endl;
            std::cout << "  * Monitor-0监控Proc0（上游STMin=0ms，原始发送速率0.1ms）" << std::endl;
            std::cout << "  * Monitor-i监控SubB(i-1)的转发速率（上游STMin依次为1/5/10/20ms）" << std::endl;
            std::cout << "- Monitor自身STMin均为0ms（不限流），完整观察上游实际发送速率" << std::endl;
            std::cout << "- 平均间隔：Monitor实际观察到的消息到达间隔，应接近上游STMin配置" << std::endl;
            std::cout << "- 消息衰减：STMin逐级增大，30秒内通过的消息数逐级减少" << std::endl;
            std::cout << "- 链尾SubB4(STMin=50ms)仅接收不转发，无下游Monitor监控" << std::endl;
            munmap(stats_array, sizeof(MonitorStats) * kNumRegions);
        }
        close(stats_fd);
    }
    
    // 清理共享内存
    shm_unlink("/lightap_chain_stats");
    for (int i = 0; i < kNumRegions; ++i) {
        std::string cleanup = "rm -f /dev/shm" + GetShmPath(i);
        system(cleanup.c_str());
    }
    
    return 0;
}
