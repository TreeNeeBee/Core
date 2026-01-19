/**
 * @brief 测试ControlBlock在不同模式下的大小
 */
#include <iostream>
#include <iomanip>
#include "ipc/ControlBlock.hpp"

using namespace lap::core::ipc;

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "ControlBlock 结构大小分析" << std::endl;
    std::cout << "========================================" << std::endl;
    
    std::cout << "\n编译模式: ";
#if defined(LIGHTAP_IPC_MODE_SHRINK)
    std::cout << "SHRINK" << std::endl;
    std::cout << "kMaxSubscribers: " << (int)kMaxSubscribers << std::endl;
    std::cout << "kQueueCapacity: " << kQueueCapacity << std::endl;
#elif defined(LIGHTAP_IPC_MODE_NORMAL)
    std::cout << "NORMAL" << std::endl;
    std::cout << "kMaxSubscribers: " << (int)kMaxSubscribers << std::endl;
    std::cout << "kQueueCapacity: " << kQueueCapacity << std::endl;
#else
    std::cout << "EXTEND" << std::endl;
    std::cout << "kMaxSubscribers: " << (int)kMaxSubscribers << std::endl;
    std::cout << "kQueueCapacity: " << kQueueCapacity << std::endl;
#endif

    std::cout << "\n========================================" << std::endl;
    std::cout << "字段大小分析:" << std::endl;
    std::cout << "========================================" << std::endl;
    
    ControlBlock ctrl;
    
    std::cout << "header 结构:" << std::endl;
    std::cout << "  - magic (atomic<UInt32>):      " << sizeof(ctrl.header.magic) << " bytes" << std::endl;
    std::cout << "  - max_chunks (UInt32):         " << sizeof(ctrl.header.max_chunks) << " bytes" << std::endl;
    std::cout << "  - chunk_size (UInt32):         " << sizeof(ctrl.header.chunk_size) << " bytes" << std::endl;
    std::cout << "  - version (atomic<UInt32>):    " << sizeof(ctrl.header.version) << " bytes" << std::endl;
    std::cout << "  - ref_count (atomic<UInt8>):   " << sizeof(ctrl.header.ref_count) << " bytes" << std::endl;
    std::cout << "  - max_subscribers (UInt8):     " << sizeof(ctrl.header.max_subscribers) << " bytes" << std::endl;
    std::cout << "  - queue_capacity (UInt16):     " << sizeof(ctrl.header.queue_capacity) << " bytes" << std::endl;
    std::cout << "  [Total header]:                " << sizeof(ctrl.header) << " bytes (包含padding)" << std::endl;
    
    std::cout << "\npool_state 结构:" << std::endl;
    std::cout << "  - free_list_head:              " << sizeof(ctrl.pool_state.free_list_head) << " bytes" << std::endl;
    std::cout << "  - remain_count:                " << sizeof(ctrl.pool_state.remain_count) << " bytes" << std::endl;
    std::cout << "  [Total pool_state]:            " << sizeof(ctrl.pool_state) << " bytes" << std::endl;
    
    std::cout << "\nregistry 结构:" << std::endl;
    std::cout << "  - registry_control:            " << sizeof(ctrl.registry.registry_control) << " bytes" << std::endl;
    std::cout << "  - snapshots[0]:                " << sizeof(ctrl.registry.snapshots[0]) << " bytes" << std::endl;
    std::cout << "  - snapshots[1]:                " << sizeof(ctrl.registry.snapshots[1]) << " bytes" << std::endl;
    std::cout << "  [Total registry]:              " << sizeof(ctrl.registry) << " bytes" << std::endl;
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "ControlBlock 总大小: " << sizeof(ControlBlock) << " bytes" << std::endl;
    std::cout << "Cache Line Size: " << kCacheLineSize << " bytes" << std::endl;
    std::cout << "========================================" << std::endl;
    
    std::cout << "\n内存布局 (按offset):" << std::endl;
    std::cout << "  header offset:       " << offsetof(ControlBlock, header) << std::endl;
    std::cout << "  pool_state offset:   " << offsetof(ControlBlock, pool_state) << std::endl;
    std::cout << "  registry offset:     " << offsetof(ControlBlock, registry) << std::endl;
    
    std::cout << "\n========================================" << std::endl;
#if defined(LIGHTAP_IPC_MODE_SHRINK)
    if (sizeof(ControlBlock) <= 64) {
        std::cout << "✓ SHRINK模式: ControlBlock满足 <=64 bytes 要求" << std::endl;
    } else {
        std::cout << "✗ SHRINK模式: ControlBlock超出64 bytes限制!" << std::endl;
        std::cout << "  超出: " << (sizeof(ControlBlock) - 64) << " bytes" << std::endl;
    }
#endif
    std::cout << "========================================" << std::endl;
    
    return 0;
}
