/**
 * @file test_core_classes.cpp
 * @brief Comprehensive test for Core classes with memory tracking
 */

#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include "CMemory.hpp"
#include "CTimer.hpp"
#include "CSync.hpp"
#include "CInitialization.hpp"

using namespace lap::core;

// Test class with memory tracking
class TestDataClass {
public:
    IMP_OPERATOR_NEW(TestDataClass)
    
    TestDataClass(int id, const char* name) : id_(id) {
        std::snprintf(name_, sizeof(name_), "%s", name);
        std::cout << "  TestDataClass(" << id_ << ", " << name_ << ") constructed\n";
    }
    
    ~TestDataClass() {
        std::cout << "  TestDataClass(" << id_ << ", " << name_ << ") destroyed\n";
    }
    
    int getId() const { return id_; }
    const char* getName() const { return name_; }
    
private:
    int id_;
    char name_[64];
};

// Container class with memory tracking
class TestContainer {
public:
    IMP_OPERATOR_NEW(TestContainer)
    
    TestContainer(const char* name) : count_(0) {
        std::snprintf(name_, sizeof(name_), "%s", name);
        std::cout << "  TestContainer(" << name_ << ") constructed\n";
    }
    
    ~TestContainer() {
        std::cout << "  TestContainer(" << name_ << ") destroyed, items: " << count_ << "\n";
    }
    
    void addItem() { ++count_; }
    int getCount() const { return count_; }
    
private:
    char name_[32];
    int count_;
};

// Worker class for threading test
class TestWorker {
public:
    IMP_OPERATOR_NEW(TestWorker)
    
    TestWorker(int id) : id_(id), running_(false) {
        std::cout << "  TestWorker(" << id_ << ") constructed\n";
    }
    
    ~TestWorker() {
        stop();
        std::cout << "  TestWorker(" << id_ << ") destroyed\n";
    }
    
    void start() {
        running_ = true;
        thread_ = std::thread([this]() {
            while (running_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }
    
    void stop() {
        running_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
    }
    
private:
    int id_;
    std::atomic<bool> running_;
    std::thread thread_;
};

void printMemoryStats(const char* label) {
    auto stats = Memory::getMemoryStats();
    std::cout << "\n[" << label << "] Memory Statistics:\n";
    std::cout << "  Current Alloc Size: " << stats.currentAllocSize << " bytes\n";
    std::cout << "  Current Alloc Count: " << stats.currentAllocCount << "\n";
    std::cout << "  Total Pool Memory: " << stats.totalPoolMemory << " bytes\n";
    std::cout << "  Pool Count: " << stats.poolCount << "\n";
    std::cout << "  Thread Count: " << stats.threadCount << "\n\n";
}

int main() {
    std::cout << "=== Core Classes Memory Tracking Test ===\n\n";
    
    // AUTOSAR-compliant initialization (includes MemoryManager)
    auto initResult = Initialize();
    if (!initResult.HasValue()) {
        std::cerr << "Failed to initialize Core: " << initResult.Error().Message() << "\n";
        return 1;
    }
    std::cout << "[Info] Core initialized\n\n";
    
    printMemoryStats("Initial State");
    
    // Test 1: Basic class allocation
    std::cout << "Test 1: Basic Class Allocation\n";
    {
        TestDataClass* obj1 = new TestDataClass(1, "Object1");
        TestDataClass* obj2 = new TestDataClass(2, "Object2");
        
        printMemoryStats("After Allocation");
        
        delete obj1;
        delete obj2;
    }
    printMemoryStats("After Test 1");
    
    // Test 2: Array allocation
    std::cout << "Test 2: Array Allocation\n";
    {
        TestDataClass* arr = new TestDataClass[5]{
            {101, "Array1"},
            {102, "Array2"},
            {103, "Array3"},
            {104, "Array4"},
            {105, "Array5"}
        };
        
        printMemoryStats("After Array Allocation");
        
        delete[] arr;
    }
    printMemoryStats("After Test 2");
    
    // Test 3: Container with multiple items
    std::cout << "Test 3: Container Operations\n";
    {
        TestContainer* container = new TestContainer("MainContainer");
        for (int i = 0; i < 10; ++i) {
            container->addItem();
        }
        
        printMemoryStats("After Container Ops");
        
        delete container;
    }
    printMemoryStats("After Test 3");
    
    // Test 4: Multi-threaded allocation
    std::cout << "Test 4: Multi-threaded Allocation\n";
    {
        constexpr int NUM_WORKERS = 4;
        TestWorker* workers[NUM_WORKERS];
        
        for (int i = 0; i < NUM_WORKERS; ++i) {
            workers[i] = new TestWorker(i);
            workers[i]->start();
        }
        
        printMemoryStats("Workers Running");
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        for (int i = 0; i < NUM_WORKERS; ++i) {
            delete workers[i];
        }
    }
    printMemoryStats("After Test 4");
    
    // Test 5: Mixed allocations
    std::cout << "Test 5: Mixed Allocations\n";
    {
        std::vector<TestDataClass*> objects;
        std::vector<TestContainer*> containers;
        
        for (int i = 0; i < 20; ++i) {
            char name[32];
            std::snprintf(name, sizeof(name), "Obj%d", i);
            objects.push_back(new TestDataClass(200 + i, name));
            
            if (i % 5 == 0) {
                std::snprintf(name, sizeof(name), "Container%d", i/5);
                containers.push_back(new TestContainer(name));
            }
        }
        
        printMemoryStats("After Mixed Allocations");
        
        for (auto* obj : objects) delete obj;
        for (auto* cnt : containers) delete cnt;
    }
    printMemoryStats("After Test 5");
    
    // Test 6: Intentional leak for testing leak detection
    std::cout << "Test 6: Intentional Leak (for leak report demonstration)\n";
    {
        TestDataClass* leaked = new TestDataClass(999, "LeakedObject");
        (void)leaked;  // Suppress unused variable warning
        std::cout << "  Intentionally NOT deleting leaked object...\n";
    }
    printMemoryStats("After Test 6");
    
    // Final state
    std::cout << "=== Final Memory State ===\n";
    MemoryManager::getInstance()->outputState(0);
    
    // AUTOSAR-compliant deinitialization (includes MemoryManager cleanup and leak report)
    auto deinitResult = Deinitialize();
    (void)deinitResult;
    std::cout << "\n[Info] Core deinitialized and configuration saved\n";
    
    std::cout << "\n=== Test Completed ===\n";
    std::cout << "Check memory_leak.log for leak report\n";
    
    return 0;
}
