#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <random>
#include <cstring>
#include "CMemory.hpp"
#include "CInitialization.hpp"

using namespace lap::core;

// Simulate various allocation patterns
void workerThread(int threadId, int iterations) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> sizeDist(16, 512);
    
    // Register thread name
    UInt32 tid = static_cast<UInt32>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    std::string threadName = "worker-" + std::to_string(threadId);
    MemoryManager::getInstance()->registerThreadName(tid, threadName.c_str());
    
    std::vector<void*> allocations;
    
    for (int i = 0; i < iterations; ++i) {
        // Random allocation size
        size_t size = sizeDist(gen);
        
        // Allocate
        void* p = Memory::malloc(size);
        if (p) {
            allocations.push_back(p);
            
            // Write some data
            std::memset(p, threadId % 256, size);
        }
        
        // Randomly free some allocations
        if (!allocations.empty() && (i % 10 == 0)) {
            size_t idx = gen() % allocations.size();
            Memory::free(allocations[idx]);
            allocations.erase(allocations.begin() + idx);
        }
    }
    
    // Free all remaining allocations
    for (void* p : allocations) {
        Memory::free(p);
    }
    
    std::cout << "Thread " << threadId << " completed " << iterations << " iterations\n";
}

int main() {
    std::cout << "=== Memory Stress Test ===\n\n";
    
    // AUTOSAR-compliant initialization (includes MemoryManager)
    auto initResult = Initialize();
    if (!initResult.HasValue()) {
        std::cerr << "Failed to initialize Core: " << initResult.Error().Message() << "\n";
        return 1;
    }
    std::cout << "[Info] Core initialized\n\n";
    
    const int numThreads = 4;
    const int iterationsPerThread = 1000;
    
    std::cout << "Starting " << numThreads << " threads, "
              << iterationsPerThread << " iterations each...\n\n";
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back(workerThread, i, iterationsPerThread);
    }
    
    // Wait for all threads to complete
    for (auto& t : threads) {
        t.join();
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    std::cout << "\n=== Test Results ===\n";
    std::cout << "Total time: " << duration.count() << " ms\n";
    std::cout << "Total operations: " << (numThreads * iterationsPerThread) << "\n";
    std::cout << "Operations per second: " 
              << (numThreads * iterationsPerThread * 1000.0 / duration.count()) << "\n\n";
    
    // Print memory state
    std::cout << "=== Memory State ===\n";
    MemoryManager::getInstance()->outputState(0);
    
    // AUTOSAR-compliant deinitialization
    auto deinitResult = Deinitialize();
    (void)deinitResult;
    std::cout << "[Info] Core deinitialized and configuration saved\n";
    
    std::cout << "\n=== Test Completed ===\n";
    return 0;
}
