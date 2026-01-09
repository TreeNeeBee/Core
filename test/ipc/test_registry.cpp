/**
 * Test SubscriberRegistry functionality
 */

#include "ipc/SharedMemoryManager.hpp"
#include "ipc/ControlBlock.hpp"
#include "ipc/SubscriberRegistryOps.hpp"
#include <iostream>

using namespace lap::core::ipc;
using namespace lap::core;

int main()
{
    std::cout << "========== Registry Test ==========" << std::endl;
    
    // Create shared memory directly
    auto shm_result = SharedMemoryManager::Create(
        "test_service",
        4096 * 16,  // 64KB
        true  // creator
    );
    
    if (!shm_result.HasValue())
    {
        std::cerr << "Failed to create shared memory" << std::endl;
        return 1;
    }
    
    auto shm = std::move(shm_result).Value();
    auto* ctrl = const_cast<ControlBlock*>(shm->GetControlBlock());
    
    // Initialize control block
    ctrl->Initialize(16, 1024, 128, 32);
    
    // Check initial snapshot
    std::cout << "\n[1] Initial snapshot (before any subscribers):" << std::endl;
    auto snapshot = GetSubscriberSnapshot(ctrl);
    std::cout << "  count = " << snapshot.count << std::endl;
    std::cout << "  version = " << snapshot.version << std::endl;
    
    // Manually register 3 subscribers
    std::cout << "\n[2] Registering 3 subscribers:" << std::endl;
    for (UInt32 i = 0; i < 3; ++i)
    {
        UInt32 queue_idx = AllocateQueueIndex(ctrl);
        bool registered = RegisterSubscriber(ctrl, queue_idx);
        std::cout << "  Registered subscriber " << i 
                  << " with queue_index=" << queue_idx
                  << " success=" << registered << std::endl;
    }
    
    // Check snapshot after subscribers
    std::cout << "\n[3] Snapshot after creating 3 subscribers:" << std::endl;
    snapshot = GetSubscriberSnapshot(ctrl);
    std::cout << "  count = " << snapshot.count << std::endl;
    std::cout << "  version = " << snapshot.version << std::endl;
    std::cout << "  queue_indices: ";
    for (UInt32 i = 0; i < snapshot.count; ++i)
    {
        std::cout << snapshot.queue_indices[i];
        if (i < snapshot.count - 1) std::cout << ", ";
    }
    std::cout << std::endl;
    
    // Check both buffers
    std::cout << "\n[4] Raw buffer inspection:" << std::endl;
    std::cout << "  active_snapshot_index = " << ctrl->active_snapshot_index.load() << std::endl;
    std::cout << "  write_index = " << ctrl->write_index.load() << std::endl;
    std::cout << "  Buffer 0: count=" << ctrl->snapshots[0].count 
              << ", version=" << ctrl->snapshots[0].version << std::endl;
    std::cout << "  Buffer 1: count=" << ctrl->snapshots[1].count 
              << ", version=" << ctrl->snapshots[1].version << std::endl;
    
    std::cout << "\n========== Test Complete ==========" << std::endl;
    return 0;
}
