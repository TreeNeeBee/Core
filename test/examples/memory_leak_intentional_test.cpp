/**
 * @file        memory_leak_intentional_test.cpp
 * @brief       Intentional memory leak test to verify detection
 * @details     This test intentionally leaks memory to validate the
 *              leak detection mechanism works correctly
 */

#include "CMemory.hpp"
#include "CConfig.hpp"
#include "CInitialization.hpp"
#include <iostream>
#include <thread>
#include <vector>
#include <sstream>
#include <functional>

using namespace lap::core;

// Test classes
class LeakyClass {
public:
    IMP_OPERATOR_NEW(LeakyClass)
    
    LeakyClass(int id) : id_(id) {
        for (int i = 0; i < 100; ++i) data_[i] = id;
    }
    
    int getId() const { return id_; }
    
private:
    int id_;
    int data_[100]; // 400 bytes total
};

class NonLeakyClass {
public:
    IMP_OPERATOR_NEW(NonLeakyClass)
    
    NonLeakyClass(int value) : value_(value) {}
    int getValue() const { return value_; }
    
private:
    int value_;
    char padding_[60]; // 64 bytes total
};

void createLeaksInThread(int threadId, int numLeaks) {
    // Register thread name
    std::ostringstream oss;
    oss << "LeakThread-" << threadId;
    MemoryManager::getInstance()->registerThreadName(
        static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id())),
        oss.str()
    );
    
    std::cout << "[Thread " << threadId << "] Creating " << numLeaks << " intentional leaks..." << std::endl;
    
    for (int i = 0; i < numLeaks; ++i) {
        // Intentionally don't delete these
        LeakyClass* leak = new LeakyClass(threadId * 1000 + i);
        (void)leak; // Suppress unused variable warning
    }
    
    std::cout << "[Thread " << threadId << "] Leaks created" << std::endl;
}

void createAndFreeInThread(int threadId, int numObjects) {
    (void)threadId; // Suppress unused warning
    std::vector<NonLeakyClass*> objects;
    
    // Allocate
    for (int i = 0; i < numObjects; ++i) {
        objects.push_back(new NonLeakyClass(i));
    }
    
    // Free
    for (auto* obj : objects) {
        delete obj;
    }
}

int main() {
    std::cout << "==== Intentional Memory Leak Test ====" << std::endl;
    std::cout << "This test creates intentional leaks to verify detection\n" << std::endl;
    
    // AUTOSAR-compliant initialization (includes MemoryManager and ConfigManager)
    auto initResult = lap::core::Initialize();
    if (!initResult.HasValue()) {
        std::cerr << "Failed to initialize Core: " << initResult.Error().Message() << "\n";
        return 1;
    }
    
    // Note: Memory checker configuration should ideally be set via config.json before
    // the program starts. Here we set it programmatically after init for test purposes.
    // In production, use a proper config file instead.
    std::string memConfig = R"({"check_enable": true, "pools": []})";
    ConfigManager::getInstance().setModuleConfig("memory", memConfig);
    
    // Re-initialize memory checker with new configuration
    // This is a workaround for testing - not recommended in production
    if (MemoryManager::getInstance()->hasMemoryTracker()) {
        MemoryManager::getInstance()->uninitialize();
    }
    MemoryManager::getInstance()->initialize();
    
    // Check if memory checking is enabled
    if (!MemoryManager::getInstance()->hasMemoryTracker()) {
        std::cout << "[WARNING] Memory checker is not enabled!" << std::endl;
        std::cout << "[INFO] To enable leak detection, create config.json with:" << std::endl;
        std::cout << R"({
    "check_enable": true,
    "pools": []
})" << std::endl;
        std::cout << "\n[INFO] Running test anyway to show statistics...\n" << std::endl;
    } else {
        std::cout << "[INFO] Memory checker is enabled\n" << std::endl;
    }
    
    // Phase 1: Normal allocations (should be freed)
    std::cout << "[Phase 1] Normal allocations..." << std::endl;
    {
        std::vector<std::thread> threads;
        for (int i = 0; i < 4; ++i) {
            threads.emplace_back(createAndFreeInThread, i, 100);
        }
        for (auto& t : threads) {
            t.join();
        }
    }
    std::cout << "[Phase 1] Complete\n" << std::endl;
    
    // Check state after phase 1 (should be clean)
    auto stats1 = MemoryManager::getInstance()->getMemoryStats();
    std::cout << "After Phase 1:" << std::endl;
    std::cout << "  Current Alloc Count: " << stats1.currentAllocCount << std::endl;
    std::cout << "  Current Alloc Size:  " << stats1.currentAllocSize << " bytes\n" << std::endl;
    
    // Phase 2: Create intentional leaks
    std::cout << "[Phase 2] Creating intentional leaks..." << std::endl;
    {
        std::vector<std::thread> threads;
        for (int i = 0; i < 4; ++i) {
            threads.emplace_back(createLeaksInThread, i, 5); // 5 leaks per thread = 20 total
        }
        for (auto& t : threads) {
            t.join();
        }
    }
    std::cout << "[Phase 2] Complete\n" << std::endl;
    
    // Check state after phase 2 (should show leaks)
    auto stats2 = MemoryManager::getInstance()->getMemoryStats();
    std::cout << "After Phase 2:" << std::endl;
    std::cout << "  Current Alloc Count: " << stats2.currentAllocCount << std::endl;
    std::cout << "  Current Alloc Size:  " << stats2.currentAllocSize << " bytes" << std::endl;
    
    if (stats2.currentAllocCount > 0) {
        std::cout << "\n[DETECTED] Memory leaks found!" << std::endl;
        std::cout << "Leaked objects: " << stats2.currentAllocCount << std::endl;
        std::cout << "Leaked bytes:   " << stats2.currentAllocSize << std::endl;
        
        // Generate detailed report
        std::cout << "\nDetailed leak report:" << std::endl;
        MemoryManager::getInstance()->outputState();
        
        std::cout << "\n[SUCCESS] Leak detection is working correctly!" << std::endl;
        
        // AUTOSAR-compliant deinitialization
        auto deinitResult = lap::core::Deinitialize();
        (void)deinitResult;
        
        return 0; // Success - we detected the intentional leaks
    } else {
        std::cout << "\n[FAILURE] No leaks detected - leak detection may not be working!" << std::endl;
        
        // AUTOSAR-compliant deinitialization
        auto deinitResult = lap::core::Deinitialize();
        (void)deinitResult;
        
        return 1;
    }
}
