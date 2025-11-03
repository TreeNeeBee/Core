/**
 * @file        test_memory_allocator_debug.cpp
 * @brief       Debug version - verify MemoryAllocator actually calls Memory::malloc
 * @date        2025-11-02
 */

#include <iostream>
#include <vector>
#include "CMemory.hpp"
#include "CMemoryAllocator.hpp"
#include "CConfig.hpp"

using namespace lap::core;

// Custom wrapper to trace allocations
template <typename T>
class TracingAllocator : public MemoryAllocator<T> {
public:
    using typename MemoryAllocator<T>::pointer;
    using typename MemoryAllocator<T>::size_type;
    
    template <class U>
    struct rebind { using other = TracingAllocator<U>; };
    
    constexpr TracingAllocator() noexcept = default;
    template <class U>
    constexpr TracingAllocator(const TracingAllocator<U>&) noexcept {}
    
    [[nodiscard]] pointer allocate(size_type n) {
        std::cout << "  [ALLOC] Requesting " << n << " elements of " << sizeof(T) 
                  << " bytes each = " << (n * sizeof(T)) << " total bytes\n";
        pointer p = MemoryAllocator<T>::allocate(n);
        std::cout << "  [ALLOC] Got pointer: " << static_cast<void*>(p) << "\n";
        
        // Verify pointer using Memory::checkPtr
        int checkResult = Memory::checkPtr(static_cast<void*>(p), "TracingAllocator");
        std::cout << "  [CHECK] checkPtr result: " << checkResult << "\n";
        
        return p;
    }
    
    void deallocate(pointer p, size_type n) noexcept {
        std::cout << "  [FREE] Deallocating " << n << " elements at " << static_cast<void*>(p) << "\n";
        MemoryAllocator<T>::deallocate(p, n);
    }
};

int main() {
    std::cout << "=== Debug Test: Verifying MemoryAllocator Usage ===\n\n";
    
    // Initialize
    ConfigManager::getInstance().initialize("config.json");
    
    auto initialStats = Memory::getMemoryStats();
    std::cout << "Initial state:\n";
    std::cout << "  Pool count: " << initialStats.poolCount << "\n";
    std::cout << "  Allocated blocks: " << initialStats.currentAllocCount << "\n";
    std::cout << "  Allocated size: " << initialStats.currentAllocSize << " bytes\n\n";
    
    // Test 1: Simple vector with tracing
    {
        std::cout << "--- Creating vector<int, TracingAllocator<int>> ---\n";
        std::vector<int, TracingAllocator<int>> vec;
        
        std::cout << "\nPushing 5 elements...\n";
        for (int i = 0; i < 5; ++i) {
            std::cout << "Push #" << (i+1) << ":\n";
            vec.push_back(i * 10);
        }
        
        auto midStats = Memory::getMemoryStats();
        std::cout << "\nAfter pushes:\n";
        std::cout << "  Allocated blocks: " << midStats.currentAllocCount << "\n";
        std::cout << "  Allocated size: " << midStats.currentAllocSize << " bytes\n";
        
        std::cout << "\nVector contents: ";
        for (const auto& v : vec) {
            std::cout << v << " ";
        }
        std::cout << "\n\nDestroying vector...\n";
    }
    
    auto finalStats = Memory::getMemoryStats();
    std::cout << "\nAfter destruction:\n";
    std::cout << "  Allocated blocks: " << finalStats.currentAllocCount << "\n";
    std::cout << "  Allocated size: " << finalStats.currentAllocSize << " bytes\n";
    
    // Test 2: Direct Memory::malloc call for comparison
    std::cout << "\n--- Direct Memory::malloc test ---\n";
    void* ptr1 = Memory::malloc(64);
    std::cout << "malloc(64) returned: " << ptr1 << "\n";
    
    int check1 = Memory::checkPtr(ptr1, "Direct malloc");
    std::cout << "checkPtr result: " << check1 << "\n";
    
    auto afterMalloc = Memory::getMemoryStats();
    std::cout << "After malloc:\n";
    std::cout << "  Allocated blocks: " << afterMalloc.currentAllocCount << "\n";
    std::cout << "  Allocated size: " << afterMalloc.currentAllocSize << " bytes\n";
    
    Memory::free(ptr1);
    auto afterFree = Memory::getMemoryStats();
    std::cout << "After free:\n";
    std::cout << "  Allocated blocks: " << afterFree.currentAllocCount << "\n";
    std::cout << "  Allocated size: " << afterFree.currentAllocSize << " bytes\n";
    
    std::cout << "\n=== Test Complete ===\n";
    
    return 0;
}
