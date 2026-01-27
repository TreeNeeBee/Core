/**
 * @brief 测试ControlBlock和ChannelQueue在不同channel数量下的内存布局
 */
#include <iostream>
#include <iomanip>
#include <vector>
#include "ipc/ControlBlock.hpp"

using namespace lap::core::ipc;
using namespace lap::core;

// 测试不同channel数量的配置
struct TestConfig {
    const char* name;
    UInt8 num_channels;
    UInt16 queue_capacity;
};

void PrintSeparator() {
    std::cout << "========================================" << std::endl;
}

void TestConfiguration(const TestConfig& config) {
    PrintSeparator();
    std::cout << "配置: " << config.name << std::endl;
    std::cout << "  Channels: " << (int)config.num_channels << std::endl;
    std::cout << "  Queue Capacity: " << config.queue_capacity << std::endl;
    PrintSeparator();

// ControlBlock基础大小
    std::cout << "\n1. ControlBlock 基础结构:" << std::endl;
    std::cout << "  - header:      " << std::setw(3) << sizeof(ControlBlock::header) << " bytes" << std::endl;
    std::cout << "  - pool_state:  " << std::setw(3) << sizeof(ControlBlock::pool_state) << " bytes" << std::endl;
    std::cout << "  - registry:    " << std::setw(3) << sizeof(ControlBlock::registry) << " bytes" << std::endl;
    std::cout << "  [Total]:       " << std::setw(3) << sizeof(ControlBlock) << " bytes" << std::endl;
    
    // Snapshot内存
    Size snapshot_size = config.num_channels * 2;  // 双缓冲
    std::cout << "\n2. Snapshot 内存 (双缓冲):" << std::endl;
    std::cout << "  - 每个snapshot: " << std::setw(3) << config.num_channels << " bytes" << std::endl;
    std::cout << "  - 总计:        " << std::setw(3) << snapshot_size << " bytes" << std::endl;
    
    // ChannelQueue结构
    std::cout << "\n3. ChannelQueue 结构:" << std::endl;
    std::cout << "  - STmin:         " << sizeof(ChannelQueue::STmin) << " bytes" << std::endl;
    std::cout << "  - active:        " << sizeof(ChannelQueue::active) << " bytes" << std::endl;
    std::cout << "  - in/out:        " << (sizeof(ChannelQueue::in) + sizeof(ChannelQueue::out)) << " bytes" << std::endl;
    std::cout << "  - capacity:      " << sizeof(ChannelQueue::capacity) << " bytes" << std::endl;
    std::cout << "  - head/tail:     " << (sizeof(ChannelQueue::head) + sizeof(ChannelQueue::tail)) << " bytes" << std::endl;
    std::cout << "  - queue_waitset: " << sizeof(ChannelQueue::queue_waitset) << " bytes" << std::endl;
    std::cout << "  [Total]:         " << sizeof(ChannelQueue) << " bytes (aligned to " << kCacheLineSize << ")" << std::endl;
    
    // 每个Queue的buffer大小
    Size buffer_per_queue = config.queue_capacity * sizeof(ChannelQueueValue);
    std::cout << "\n4. 每个Channel的Buffer:" << std::endl;
    std::cout << "  - ChannelQueueValue: " << sizeof(ChannelQueueValue) << " bytes" << std::endl;
    std::cout << "  - Capacity:          " << config.queue_capacity << " entries" << std::endl;
    std::cout << "  - Buffer size:       " << buffer_per_queue << " bytes" << std::endl;
    
    // 单个Channel总大小
    Size single_channel_size = sizeof(ChannelQueue) + buffer_per_queue;
    std::cout << "\n5. 单个Channel总大小:" << std::endl;
    std::cout << "  - ChannelQueue:  " << sizeof(ChannelQueue) << " bytes" << std::endl;
    std::cout << "  - Buffer:        " << buffer_per_queue << " bytes" << std::endl;
    std::cout << "  [Total]:         " << single_channel_size << " bytes" << std::endl;
    
    // 所有Channel内存
    Size all_channels_size = single_channel_size * config.num_channels;
    std::cout << "\n6. 所有Channels内存 (x" << (int)config.num_channels << "):" << std::endl;
    std::cout << "  - Total:         " << all_channels_size << " bytes (" 
              << (all_channels_size / 1024.0) << " KB)" << std::endl;
    
    // 总内存
    Size total_memory = sizeof(ControlBlock) + snapshot_size + all_channels_size;
    std::cout << "\n7. 共享内存总大小:" << std::endl;
    std::cout << "  - ControlBlock:  " << sizeof(ControlBlock) << " bytes" << std::endl;
    std::cout << "  - Snapshots:     " << snapshot_size << " bytes" << std::endl;
    std::cout << "  - Channels:      " << all_channels_size << " bytes" << std::endl;
    PrintSeparator();
    std::cout << "  [TOTAL]:         " << total_memory << " bytes (" 
              << (total_memory / 1024.0) << " KB)" << std::endl;
    PrintSeparator();
    std::cout << std::endl;
}

int main() {
    PrintSeparator();
    std::cout << "Channel内存布局测试 - 不同配置对比" << std::endl;
    PrintSeparator();
    std::cout << "Cache Line Size: " << kCacheLineSize << " bytes" << std::endl;
    std::cout << std::endl;
    
    // 三种典型配置
    std::vector<TestConfig> configs = {
        {"小型系统 (2 channels)",   2,  64},
        {"中型系统 (16 channels)", 16, 256},
        {"大型系统 (30 channels)", 30, 256}
    };
    
    for (const auto& config : configs) {
        TestConfiguration(config);
    }
    
    return 0;
}
