/**
 * @file        debug_ipc.cpp
 * @brief       Debug tool to inspect shared memory state
 * @date        2026-01-08
 */

#include <iostream>
#include <iomanip>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include "ipc/ControlBlock.hpp"
#include "ipc/SharedMemoryManager.hpp"

using namespace lap::core;
using namespace lap::core::ipc;

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cout << "Usage: " << argv[0] << " <service_name>" << std::endl;
        return 1;
    }
    
    std::string service_name = argv[1];
    std::string shm_path = "/lightap_ipc_" + service_name;
    
    std::cout << "========== Shared Memory Inspector ==========" << std::endl;
    std::cout << "Service: " << service_name << std::endl;
    std::cout << "Path: /dev/shm" << shm_path << std::endl;
    std::cout << std::endl;
    
    // Open shared memory
    int fd = shm_open(shm_path.c_str(), O_RDONLY, 0666);
    if (fd == -1)
    {
        std::cerr << "Failed to open shared memory: " << strerror(errno) << std::endl;
        return 1;
    }
    
    // Get size
    struct stat st;
    if (fstat(fd, &st) == -1)
    {
        std::cerr << "Failed to get size: " << strerror(errno) << std::endl;
        close(fd);
        return 1;
    }
    
    std::cout << "[1] Shared Memory Info:" << std::endl;
    std::cout << "  Size: " << st.st_size << " bytes (" << (st.st_size / 1024.0 / 1024.0) << " MB)" << std::endl;
    std::cout << std::endl;
    
    // Map memory
    void* addr = mmap(nullptr, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED)
    {
        std::cerr << "Failed to map memory: " << strerror(errno) << std::endl;
        close(fd);
        return 1;
    }
    
    auto* ctrl = static_cast<ControlBlock*>(addr);
    
    // Validate
    std::cout << "[2] ControlBlock Validation:" << std::endl;
    UInt32 magic = ctrl->magic_number.load(std::memory_order_acquire);
    UInt32 version = ctrl->version.load(std::memory_order_acquire);
    
    std::cout << "  Magic: 0x" << std::hex << magic << std::dec;
    if (magic == kIPCMagicNumber)
    {
        std::cout << " ✓" << std::endl;
    }
    else
    {
        std::cout << " ✗ (expected 0x" << std::hex << kIPCMagicNumber << std::dec << ")" << std::endl;
    }
    
    std::cout << "  Version: " << version;
    if (version == kIPCVersion)
    {
        std::cout << " ✓" << std::endl;
    }
    else
    {
        std::cout << " ✗ (expected " << kIPCVersion << ")" << std::endl;
    }
    std::cout << std::endl;
    
    // ChunkPool info
    std::cout << "[3] ChunkPool Status:" << std::endl;
    std::cout << "  Max chunks: " << ctrl->max_chunks << std::endl;
    std::cout << "  Chunk size: " << ctrl->chunk_size << " bytes" << std::endl;
    std::cout << "  Allocated: " << ctrl->allocated_count.load(std::memory_order_acquire) << std::endl;
    std::cout << "  Free list head: " << ctrl->free_list_head.load(std::memory_order_acquire) << std::endl;
    std::cout << "  Initialized: " << (ctrl->is_initialized.load(std::memory_order_acquire) ? "Yes" : "No") << std::endl;
    std::cout << std::endl;
    
    // Subscriber Registry
    std::cout << "[4] SubscriberRegistry:" << std::endl;
    UInt32 active_idx = ctrl->active_snapshot_index.load(std::memory_order_acquire);
    std::cout << "  Active snapshot: " << active_idx << std::endl;
    std::cout << "  Next queue index: " << ctrl->next_queue_index.load(std::memory_order_acquire) << std::endl;
    
    auto& snapshot = ctrl->snapshots[active_idx];
    std::cout << "  Subscriber count: " << snapshot.count << std::endl;
    std::cout << "  Version: " << snapshot.version << std::endl;
    
    if (snapshot.count > 0)
    {
        std::cout << "  Queue indices: [ ";
        for (UInt32 i = 0; i < snapshot.count && i < 10; ++i)
        {
            std::cout << snapshot.queue_indices[i] << " ";
        }
        if (snapshot.count > 10)
        {
            std::cout << "... (" << snapshot.count << " total)";
        }
        std::cout << "]" << std::endl;
    }
    std::cout << std::endl;
    
    // Statistics
    std::cout << "[5] Statistics:" << std::endl;
    std::cout << "  Publishers: " << ctrl->publisher_count.load(std::memory_order_acquire) << std::endl;
    std::cout << "  Subscribers: " << ctrl->subscriber_count.load(std::memory_order_acquire) << std::endl;
    std::cout << "  Total allocations: " << ctrl->total_allocations.load(std::memory_order_acquire) << std::endl;
    std::cout << "  Total deallocations: " << ctrl->total_deallocations.load(std::memory_order_acquire) << std::endl;
    std::cout << std::endl;
    
    // Queue states
    std::cout << "[6] SubscriberQueue States:" << std::endl;
    std::cout << "  Max queues: " << ctrl->max_subscriber_queues << std::endl;
    std::cout << "  Queue capacity: " << ctrl->queue_capacity << std::endl;
    
    // Calculate queue address
    UInt64 ctrl_size = sizeof(ControlBlock);
    UInt64 single_queue_size = ((sizeof(SubscriberQueue) + ctrl->queue_capacity * sizeof(UInt32) + kCacheLineSize - 1) / kCacheLineSize) * kCacheLineSize;
    
    UInt8* queue_base = reinterpret_cast<UInt8*>(addr) + ctrl_size;
    
    UInt32 active_queues = 0;
    for (UInt32 i = 0; i < std::min(ctrl->max_subscriber_queues, 10u); ++i)
    {
        auto* queue = reinterpret_cast<SubscriberQueue*>(queue_base + i * single_queue_size);
        bool is_active = queue->active.load(std::memory_order_acquire);
        
        if (is_active)
        {
            active_queues++;
            UInt32 head = queue->head.load(std::memory_order_acquire);
            UInt32 tail = queue->tail.load(std::memory_order_acquire);
            UInt32 count = (tail >= head) ? (tail - head) : (queue->capacity - head + tail);
            
            std::cout << "  Queue[" << i << "]: ACTIVE"
                      << " | ID=" << queue->subscriber_id
                      << " | head=" << head
                      << " | tail=" << tail
                      << " | count=" << count << "/" << queue->capacity
                      << " | overruns=" << queue->overrun_count.load(std::memory_order_acquire)
                      << std::endl;
        }
    }
    
    if (active_queues == 0)
    {
        std::cout << "  No active queues" << std::endl;
    }
    
    std::cout << std::endl;
    std::cout << "========== Inspection Complete ==========" << std::endl;
    
    // Cleanup
    munmap(addr, st.st_size);
    close(fd);
    
    return 0;
}
