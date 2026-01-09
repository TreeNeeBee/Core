/**
 * @file        devzero_stress_test.cpp
 * @brief       /dev/zero to /dev/null stress test (multi-process)
 * @details     1 publisher reads from /dev/zero, N subscribers write to /dev/null
 * @usage       ./devzero_stress_test <mode> <duration> <num_subs> <msg_size> [sub_id]
 * @example     ./devzero_stress_test pub 60 10 4096       # Publisher
 *              ./devzero_stress_test sub 60 10 4096 0     # Subscriber #0
 *              ./devzero_stress_test monitor 60 10 4096   # Monitor
 */

#include "ipc/Publisher.hpp"
#include "ipc/Subscriber.hpp"
#include "ipc/Message.hpp"
#include "IPCTypes.hpp"
#include "CResult.hpp"
#include "CString.hpp"

#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/mman.h>

using namespace lap::core;
using namespace lap::core::ipc;

// Global flags
static std::atomic<bool> g_running{true};
static std::atomic<bool> g_publisher_ready{false};

// Statistics
struct Stats {
    std::atomic<uint64_t> sent{0};
    std::atomic<uint64_t> received{0};
    std::atomic<uint64_t> send_errors{0};
    std::atomic<uint64_t> recv_errors{0};
};

// Shared memory for statistics (to be shared across processes)
static Stats* g_stats_ptr = nullptr;
static int g_stats_shm_fd = -1;
static const char* kStatsShm = "/lightap_devzero_stats";

// Configuration
struct Config {
    uint32_t duration_sec = 60;
    uint32_t num_subscribers = 10;
    uint32_t msg_size = 4096;
    uint32_t send_rate = 0;  // 0 = unlimited
    String service_name = "devzero_stress";
};

static Config g_config;

// Initialize shared memory for statistics
bool init_stats_shm() {
    // Create shared memory for stats
    shm_unlink(kStatsShm);  // Clean up any existing
    g_stats_shm_fd = shm_open(kStatsShm, O_CREAT | O_RDWR, 0666);
    if (g_stats_shm_fd < 0) {
        std::cerr << "Failed to create stats shared memory\n";
        return false;
    }
    
    if (ftruncate(g_stats_shm_fd, sizeof(Stats)) != 0) {
        close(g_stats_shm_fd);
        return false;
    }
    
    g_stats_ptr = static_cast<Stats*>(mmap(nullptr, sizeof(Stats), 
                                           PROT_READ | PROT_WRITE, 
                                           MAP_SHARED, g_stats_shm_fd, 0));
    if (g_stats_ptr == MAP_FAILED) {
        close(g_stats_shm_fd);
        return false;
    }
    
    // Initialize atomics
    new (&g_stats_ptr->sent) std::atomic<uint64_t>{0};
    new (&g_stats_ptr->received) std::atomic<uint64_t>{0};
    new (&g_stats_ptr->send_errors) std::atomic<uint64_t>{0};
    new (&g_stats_ptr->recv_errors) std::atomic<uint64_t>{0};
    
    return true;
}

// Attach to existing stats shared memory
bool attach_stats_shm() {
    g_stats_shm_fd = shm_open(kStatsShm, O_RDWR, 0666);
    if (g_stats_shm_fd < 0) {
        std::cerr << "Failed to open stats shared memory\n";
        return false;
    }
    
    g_stats_ptr = static_cast<Stats*>(mmap(nullptr, sizeof(Stats), 
                                           PROT_READ | PROT_WRITE, 
                                           MAP_SHARED, g_stats_shm_fd, 0));
    if (g_stats_ptr == MAP_FAILED) {
        close(g_stats_shm_fd);
        return false;
    }
    
    return true;
}

// Cleanup stats shared memory
void cleanup_stats_shm() {
    if (g_stats_ptr != nullptr && g_stats_ptr != MAP_FAILED) {
        munmap(g_stats_ptr, sizeof(Stats));
        g_stats_ptr = nullptr;
    }
    if (g_stats_shm_fd >= 0) {
        close(g_stats_shm_fd);
        g_stats_shm_fd = -1;
    }
    shm_unlink(kStatsShm);
}

// Signal handler
void signal_handler(int signum) {
    std::cout << "\n[信号] 收到信号 " << signum << ", 准备退出...\n";
    g_running = false;
}

// Define test message inheriting from Message base class
struct TestMessage : public Message {
    uint64_t sequence;
    uint64_t timestamp_ns;
    uint32_t data_size;
    uint32_t _padding;  // Alignment
    char data[4096];  // Fixed size for now
    
    // Default constructor
    TestMessage() noexcept : sequence(0), timestamp_ns(0), data_size(0), _padding(0) {
        std::memset(data, 0, sizeof(data));
    }
    
    // Constructor for placement new
    TestMessage(uint64_t seq, uint64_t ts, uint32_t size, const char* buf) noexcept 
        : sequence(seq), timestamp_ns(ts), data_size(size), _padding(0)
    {
        if (buf && size <= sizeof(data)) {
            std::memcpy(data, buf, size);
        } else {
            std::memset(data, 0, sizeof(data));
        }
    }
    
    // 重载GetSize以返回实际大小
    UInt32 GetTypeId() const noexcept override { return 1; }
    UInt64 GetSize() const noexcept override { return sizeof(TestMessage); }
};

/**
 * @brief Publisher thread - reads from /dev/zero and sends via SendEmplace
 */
void publisher_thread() {    // Attach to stats shared memory
    if (!attach_stats_shm()) {
        std::cerr << "[发布者] 无法连接统计共享内存\n";
        return;
    }
        std::cout << "[发布者] 启动\n"
              << "  服务名: " << g_config.service_name.c_str() << "\n"
              << "  消息大小: " << sizeof(TestMessage) << " 字节\n"
              << "  发送速率: " << (g_config.send_rate > 0 ? std::to_string(g_config.send_rate) + " msg/s" : "无限制") << "\n"
              << "  PID: " << getpid() << "\n";
    
    // Open /dev/zero
    int zero_fd = open("/dev/zero", O_RDONLY);
    if (zero_fd < 0) {
        std::cerr << "[发布者] 错误: 无法打开 /dev/zero\n";
        return;
    }
    
    // Create publisher using Message base class (pre-instantiated)
    // Note: Must explicitly set chunk_size to TestMessage size since we use placement new
    PublisherConfig pub_config;
    pub_config.max_chunks = 256;  // Large pool to avoid exhaustion at high rate
    pub_config.chunk_size = sizeof(TestMessage);  // CRITICAL: Must match actual message size
    pub_config.loan_policy = LoanFailurePolicy::kError;
    
    std::cout << "  sizeof(Message)=" << sizeof(Message) << ", sizeof(TestMessage)=" << sizeof(TestMessage) << "\n";
    std::cout << "  pub_config.chunk_size=" << pub_config.chunk_size << ", max_chunks=" << pub_config.max_chunks << "\n";
    
    auto pub_result = Publisher<Message>::Create(g_config.service_name, pub_config);
    if (!pub_result.HasValue()) {
        std::cerr << "[发布者] 创建失败\n";
        close(zero_fd);
        return;
    }
    
    auto publisher = std::move(pub_result).Value();
    std::cout << "[发布者] 创建成功，开始发送...\n";
    
    // Signal ready
    g_publisher_ready = true;
    
    // Allocate buffer for reading from /dev/zero
    std::vector<char> zero_buffer(g_config.msg_size);
    
    uint64_t sequence = 0;
    auto start_time = std::chrono::steady_clock::now();
    auto last_rate_check = start_time;
    uint64_t msgs_in_window = 0;
    
    // Run for configured duration
    while (true) {
        auto now_time = std::chrono::steady_clock::now();
        auto elapsed_time = std::chrono::duration_cast<std::chrono::seconds>(now_time - start_time).count();
        if (elapsed_time >= g_config.duration_sec || !g_running) {
            break;
        }
        // Rate limiting
        if (g_config.send_rate > 0) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_rate_check).count();
            
            if (elapsed >= 1000) {
                // Reset window
                last_rate_check = now;
                msgs_in_window = 0;
            } else if (msgs_in_window >= g_config.send_rate) {
                // Wait until next window
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
        }
        
        // Read from /dev/zero
        ssize_t bytes_read = read(zero_fd, zero_buffer.data(), g_config.msg_size);
        if (bytes_read < 0) {
            std::cerr << "[发布者] 读取 /dev/zero 失败\n";
            g_stats_ptr->send_errors++;
            continue;
        }
        
        // Get current timestamp
        auto now = std::chrono::high_resolution_clock::now();
        uint64_t timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count();
        
        // Loan a chunk
        auto loan_result = publisher.Loan();
        if (!loan_result.HasValue()) {
            if (g_stats_ptr->send_errors.load() < 5) {  // Only print first few errors
                auto error = loan_result.Error();
                std::cerr << "[发布者] Loan失败, 错误=" << error.Value() 
                          << ", 消息=" << error.Message() 
                          << ", 错误数=" << g_stats_ptr->send_errors.load() << "\n";
            }
            g_stats_ptr->send_errors++;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        
        auto sample = std::move(loan_result).Value();
        
        // Use placement new to construct TestMessage in-place
        auto* msg_ptr = const_cast<Message*>(sample.Get());
        new (msg_ptr) TestMessage(sequence++, timestamp_ns, static_cast<uint32_t>(g_config.msg_size), zero_buffer.data());
        
        // Send the sample
        auto send_result = publisher.Send(std::move(sample));
        if (!send_result.HasValue()) {
            g_stats_ptr->send_errors++;
            continue;
        }
        
        g_stats_ptr->sent++;
        msgs_in_window++;
        
        // Add small delay to reduce send rate and allow subscribers to keep up
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
    
    close(zero_fd);
    
    // Print final stats
    auto end_time = std::chrono::steady_clock::now();
    auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count();
    
    std::cout << "\n[发布者] 停止发送\n"
              << "  总发送: " << g_stats_ptr->sent.load() << " 消息\n"
              << "  错误数: " << g_stats_ptr->send_errors.load() << "\n"
              << "  运行时长: " << elapsed_sec << " 秒\n"
              << "  平均速率: " << (elapsed_sec > 0 ? g_stats_ptr->sent.load() / elapsed_sec : 0) << " msg/s\n";
}

/**
 * @brief Subscriber thread - receives and writes to /dev/null
 */
void subscriber_thread(uint32_t subscriber_id) {
    // Attach to stats shared memory
    if (!attach_stats_shm()) {
        std::cerr << "[订阅者#" << subscriber_id << "] 无法连接统计共享内存\n";
        return;
    }
    
    // Wait a bit for publisher to be ready (multi-process, can't use g_publisher_ready)
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    std::cout << "[订阅者#" << subscriber_id << "] 启动, PID=" << getpid() << "\n";
    
    // Open /dev/null
    int null_fd = open("/dev/null", O_WRONLY);
    if (null_fd < 0) {
        std::cerr << "[订阅者#" << subscriber_id << "] 错误: 无法打开 /dev/null\n";
        return;
    }
    
    // Create subscriber with queue capacity
    SubscriberConfig sub_config;
    sub_config.queue_capacity = 1024;  // Queue capacity limit
    
    auto sub_result = Subscriber<Message>::Create(g_config.service_name, sub_config);
    if (!sub_result.HasValue()) {
        std::cerr << "[订阅者#" << subscriber_id << "] 创建失败\n";
        close(null_fd);
        return;
    }
    
    auto subscriber = std::move(sub_result).Value();
    
    uint64_t local_received = 0;
    uint64_t local_errors = 0;
    auto start_time = std::chrono::steady_clock::now();
    
    // Run for configured duration
    while (true) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
        if (elapsed >= g_config.duration_sec || !g_running) {
            break;
        }
        auto result = subscriber.Receive();
        
        if (!result.HasValue()) {
            // No data, continue immediately for high-throughput
            local_errors++;
            g_stats_ptr->recv_errors++;
            continue;
        }
        
        auto sample = std::move(result).Value();
        const auto* msg = static_cast<const TestMessage*>(sample.Get());
        
        // Write to /dev/null
        ssize_t written = write(null_fd, msg->data, msg->data_size);
        if (written < 0) {
            std::cerr << "[订阅者#" << subscriber_id << "] 写入 /dev/null 失败\n";
            local_errors++;
        }
        
        local_received++;
        g_stats_ptr->received++;
    }
    
    close(null_fd);
    
    std::cout << "[订阅者#" << subscriber_id << "] 停止\n"
              << "  接收: " << local_received << " 消息\n"
              << "  错误: " << local_errors << "\n";
}

/**
 * @brief Statistics monitor thread
 */
void monitor_thread() {
    // Attach to stats shared memory
    if (!attach_stats_shm()) {
        std::cerr << "[监控] 无法连接统计共享内存\n";
        return;
    }
    
    auto start_time = std::chrono::steady_clock::now();
    auto last_print = start_time;
    uint64_t last_sent = 0;
    uint64_t last_received = 0;
    
    // Run for configured duration
    while (true) {
        auto now_check = std::chrono::steady_clock::now();
        auto elapsed_check = std::chrono::duration_cast<std::chrono::seconds>(now_check - start_time).count();
        if (elapsed_check >= g_config.duration_sec || !g_running) {
            break;
        }
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed_total = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
        auto elapsed_window = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_print).count();
        
        uint64_t current_sent = g_stats_ptr->sent.load();
        uint64_t current_received = g_stats_ptr->received.load();
        
        uint64_t sent_delta = current_sent - last_sent;
        uint64_t recv_delta = current_received - last_received;
        
        double send_rate = (elapsed_window > 0) ? (sent_delta * 1000.0 / elapsed_window) : 0.0;
        double recv_rate = (elapsed_window > 0) ? (recv_delta * 1000.0 / elapsed_window) : 0.0;
        
        std::cout << "\n=== 运行统计 [" << elapsed_total << "s] ===\n"
                  << "发送: " << current_sent << " (" << static_cast<uint64_t>(send_rate) << " msg/s)\n"
                  << "接收: " << current_received << " (" << static_cast<uint64_t>(recv_rate) << " msg/s)\n"
                  << "发送错误: " << g_stats_ptr->send_errors.load() << "\n"
                  << "接收错误: " << g_stats_ptr->recv_errors.load() << "\n"
                  << "扇出比率: " << (current_sent > 0 ? static_cast<double>(current_received) / current_sent : 0.0) << "\n";
        std::cout.flush();  // Force flush to ensure output is visible immediately
        
        last_sent = current_sent;
        last_received = current_received;
        last_print = now;
        
        // Sleep before next iteration
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

/**
 * @brief Parse command line arguments
 */
bool parse_arguments(int argc, char* argv[], String& mode, uint32_t& sub_id) {
    if (argc < 5) {
        std::cout << "用法: " << argv[0] << " <mode> <测试时长(秒)> <订阅者数量> <消息大小(字节)> [sub_id] [发送速率]\n";
        std::cout << "模式:\n";
        std::cout << "  pub     - 发布者进程\n";
        std::cout << "  sub     - 订阅者进程 (需要sub_id参数)\n";
        std::cout << "  monitor - 监控进程\n";
        std::cout << "示例:\n";
        std::cout << "  " << argv[0] << " pub 60 10 4096       # 发布者, 60秒, 10订阅者, 4KB\n";
        std::cout << "  " << argv[0] << " sub 60 10 4096 0     # 订阅者#0\n";
        std::cout << "  " << argv[0] << " monitor 60 10 4096   # 监控进程\n";
        return false;
    }
    
    mode = argv[1];
    g_config.duration_sec = std::atoi(argv[2]);
    g_config.num_subscribers = std::atoi(argv[3]);
    g_config.msg_size = std::atoi(argv[4]);
    
    if (mode == "sub") {
        if (argc < 6) {
            std::cerr << "错误: sub模式需要提供sub_id参数\n";
            return false;
        }
        sub_id = std::atoi(argv[5]);
    }
    
    if (argc >= 7) {
        g_config.send_rate = std::atoi(argv[6]);
    } else if (argc >= 6 && mode != "sub") {
        g_config.send_rate = std::atoi(argv[5]);
    }
    
    // Validation
    if (g_config.duration_sec == 0 || g_config.duration_sec > 86400) {
        std::cerr << "错误: 测试时长必须在 1-86400 秒之间\n";
        return false;
    }
    
    if (g_config.num_subscribers == 0 || g_config.num_subscribers > 1000) {
        std::cerr << "错误: 订阅者数量必须在 1-1000 之间\n";
        return false;
    }
    
    if (g_config.msg_size < 64 || g_config.msg_size > 1024 * 1024) {
        std::cerr << "错误: 消息大小必须在 64 - 1MB 之间\n";
        return false;
    }
    
    return true;
}

/**
 * @brief Main entry point
 */
int main(int argc, char* argv[]) {
    String mode;
    uint32_t sub_id = 0;
    
    // Parse arguments
    if (!parse_arguments(argc, argv, mode, sub_id)) {
        return 1;
    }
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Attach to shared memory for statistics
    if (mode == "pub") {
        // Publisher creates the stats shared memory
        if (!init_stats_shm()) {
            std::cerr << "[" << mode << "] 初始化统计共享内存失败\n";
            return 1;
        }
    } else {
        // Sub/Monitor attach to existing stats shared memory
        if (!attach_stats_shm()) {
            std::cerr << "[" << mode << "] 连接统计共享内存失败\n";
            return 1;
        }
    }
    
    auto start_time = std::chrono::steady_clock::now();
    
    // Run appropriate role
    if (mode == "pub") {
        publisher_thread();
    } else if (mode == "sub") {
        subscriber_thread(sub_id);
    } else if (mode == "monitor") {
        monitor_thread();
    } else {
        std::cerr << "错误: 未知模式 '" << mode << "'\n";
        cleanup_stats_shm();
        return 1;
    }
    
    // Cleanup (only for publisher)
    if (mode == "pub") {
        auto end_time = std::chrono::steady_clock::now();
        auto total_elapsed = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count();
        
        // Small delay to let subscribers finish
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        uint64_t total_sent = g_stats_ptr->sent.load();
        uint64_t total_received = g_stats_ptr->received.load();
        uint64_t total_send_errors = g_stats_ptr->send_errors.load();
        uint64_t total_recv_errors = g_stats_ptr->recv_errors.load();
        
        std::cout << "\n========================================\n"
                  << "最终统计\n"
                  << "========================================\n"
                  << "运行时长: " << total_elapsed << " 秒\n"
                  << "发送消息: " << total_sent << "\n"
                  << "接收消息: " << total_received << "\n"
                  << "发送错误: " << total_send_errors << "\n"
                  << "接收错误: " << total_recv_errors << "\n"
                  << "平均发送速率: " << (total_elapsed > 0 ? total_sent / total_elapsed : 0) << " msg/s\n"
                  << "平均接收速率: " << (total_elapsed > 0 ? total_received / total_elapsed : 0) << " msg/s\n"
                  << "扇出比率: " << (total_sent > 0 ? static_cast<double>(total_received) / total_sent : 0.0) << "\n"
                  << "数据量发送: " << (total_sent * g_config.msg_size / 1024.0 / 1024.0) << " MB\n"
                  << "数据量接收: " << (total_received * g_config.msg_size / 1024.0 / 1024.0) << " MB\n"
                  << "========================================\n";
        
        cleanup_stats_shm();
    }
    
    return 0;
}
