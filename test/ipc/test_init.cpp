/**
 * @file        test_init.cpp
 * @brief       Test shared memory initialization
 */

#include <iostream>
#include "ipc/SharedMemoryManager.hpp"
#include "ipc/ControlBlock.hpp"

using namespace lap::core;
using namespace lap::core::ipc;

int main()
{
    std::cout << "========== Test Shared Memory Init ==========" << std::endl;
    
    // Clean up
    shm_unlink("/lightap_ipc_test");
    
    // Create
    auto shm = std::make_unique<SharedMemoryManager>();
    SharedMemoryConfig config{};
    config.max_chunks = 16;
    config.chunk_size = 128;
    
    auto result = shm->Create("test", config);
    if (!result) {
        std::cerr << "Failed to create" << std::endl;
        return 1;
    }
    
    std::cout << "IsCreator: " << shm->IsCreator() << std::endl;
    
    // Check Queue[0]
    auto* queue0 = shm->GetSubscriberQueue(0);
    if (queue0) {
        bool active = queue0->active.load(std::memory_order_acquire);
        std::cout << "Queue[0] active: " << active << std::endl;
        std::cout << "Queue[0] capacity: " << queue0->capacity << std::endl;
    } else {
        std::cout << "Queue[0] is nullptr!" << std::endl;
    }
    
    // Check Queue[1]
    auto* queue1 = shm->GetSubscriberQueue(1);
    if (queue1) {
        bool active = queue1->active.load(std::memory_order_acquire);
        std::cout << "Queue[1] active: " << active << std::endl;
    }
    
    std::cout << "========== Test Complete ==========" << std::endl;
    return 0;
}
