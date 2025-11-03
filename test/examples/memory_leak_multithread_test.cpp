/**
 * @file        memory_leak_multithread_test.cpp
 * @brief       Multi-threaded memory leak detection test
 * @details     Tests memory management under concurrent allocation/deallocation
 *              across multiple threads to detect race conditions and leaks
 */

#include "CMemory.hpp"
#include "CConfig.hpp"
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <random>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <functional>

using namespace lap::core;

// Test configuration
constexpr int NUM_THREADS = 8;
constexpr int ITERATIONS_PER_THREAD = 1000;
constexpr int MAX_ALLOC_SIZE = 4096;
constexpr int NUM_ALLOC_SIZES = 5;

// Thread-safe statistics
std::atomic<uint64_t> totalAllocations{0};
std::atomic<uint64_t> totalDeallocations{0};
std::atomic<uint64_t> totalBytesAllocated{0};
std::atomic<uint64_t> totalBytesFreed{0};

// Test classes with different allocation patterns
class TestSmall {
public:
    IMP_OPERATOR_NEW(TestSmall)
    
    TestSmall() : data_{42} {}
    ~TestSmall() { data_ = 0; }
private:
    int data_;
    char padding_[28]; // Total 32 bytes
};

class TestMedium {
public:
    IMP_OPERATOR_NEW(TestMedium)
    
    TestMedium() { 
        for (int i = 0; i < 64; ++i) data_[i] = i; 
    }
    ~TestMedium() { 
        for (int i = 0; i < 64; ++i) data_[i] = 0; 
    }
private:
    int data_[64]; // 256 bytes
};

class TestLarge {
public:
    IMP_OPERATOR_NEW(TestLarge)
    
    TestLarge() { 
        for (int i = 0; i < 256; ++i) data_[i] = i; 
    }
    ~TestLarge() { 
        for (int i = 0; i < 256; ++i) data_[i] = 0; 
    }
private:
    int data_[256]; // 1024 bytes
};

class TestError {
public:
    IMP_OPERATOR_NEW(TestError)
    
    TestError(int code = 0) : code_(code) {}
    int getCode() const { return code_; }
private:
    int code_;
    char padding_[20]; // Total 24 bytes
};

class TestStatus {
public:
    IMP_OPERATOR_NEW(TestStatus)
    
    TestStatus(bool success = true, int value = 0) 
        : success_(success), value_(value) {}
    bool isSuccess() const { return success_; }
    int getValue() const { return value_; }
private:
    bool success_;
    int value_;
    char padding_[32]; // Total ~40 bytes
};

// Worker thread function - Pattern 1: Allocate and free immediately
void workerPattern1(int threadId) {
    // Register thread name
    std::ostringstream oss;
    oss << "Worker-P1-" << threadId;
    MemManager::getInstance()->registerThreadName(
        static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id())),
        oss.str()
    );
    
    std::random_device rd;
    std::mt19937 gen(rd() + threadId);
    std::uniform_int_distribution<> sizeDist(0, NUM_ALLOC_SIZES - 1);
    
    for (int i = 0; i < ITERATIONS_PER_THREAD; ++i) {
        int sizeType = sizeDist(gen);
        
        switch (sizeType) {
            case 0: {
                TestSmall* obj = new TestSmall();
                totalAllocations++;
                totalBytesAllocated += sizeof(TestSmall);
                delete obj;
                totalDeallocations++;
                totalBytesFreed += sizeof(TestSmall);
                break;
            }
            case 1: {
                TestMedium* obj = new TestMedium();
                totalAllocations++;
                totalBytesAllocated += sizeof(TestMedium);
                delete obj;
                totalDeallocations++;
                totalBytesFreed += sizeof(TestMedium);
                break;
            }
            case 2: {
                TestLarge* obj = new TestLarge();
                totalAllocations++;
                totalBytesAllocated += sizeof(TestLarge);
                delete obj;
                totalDeallocations++;
                totalBytesFreed += sizeof(TestLarge);
                break;
            }
            case 3: {
                TestError* obj = new TestError(i);
                totalAllocations++;
                totalBytesAllocated += sizeof(TestError);
                delete obj;
                totalDeallocations++;
                totalBytesFreed += sizeof(TestError);
                break;
            }
            case 4: {
                TestStatus* obj = new TestStatus(true, i);
                totalAllocations++;
                totalBytesAllocated += sizeof(TestStatus);
                delete obj;
                totalDeallocations++;
                totalBytesFreed += sizeof(TestStatus);
                break;
            }
        }
    }
}

// Worker thread function - Pattern 2: Batch allocate then batch free
void workerPattern2(int threadId) {
    // Register thread name
    std::ostringstream oss;
    oss << "Worker-P2-" << threadId;
    MemManager::getInstance()->registerThreadName(
        static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id())),
        oss.str()
    );
    
    std::random_device rd;
    std::mt19937 gen(rd() + threadId);
    std::uniform_int_distribution<> batchSize(10, 50);
    
    std::vector<void*> allocated;
    std::vector<int> types;
    
    for (int batch = 0; batch < ITERATIONS_PER_THREAD / 20; ++batch) {
        // Allocate batch
        int size = batchSize(gen);
        allocated.clear();
        types.clear();
        
        for (int i = 0; i < size; ++i) {
            int type = i % NUM_ALLOC_SIZES;
            types.push_back(type);
            
            switch (type) {
                case 0: {
                    TestSmall* obj = new TestSmall();
                    allocated.push_back(obj);
                    totalAllocations++;
                    totalBytesAllocated += sizeof(TestSmall);
                    break;
                }
                case 1: {
                    TestMedium* obj = new TestMedium();
                    allocated.push_back(obj);
                    totalAllocations++;
                    totalBytesAllocated += sizeof(TestMedium);
                    break;
                }
                case 2: {
                    TestLarge* obj = new TestLarge();
                    allocated.push_back(obj);
                    totalAllocations++;
                    totalBytesAllocated += sizeof(TestLarge);
                    break;
                }
                case 3: {
                    TestError* obj = new TestError(i);
                    allocated.push_back(obj);
                    totalAllocations++;
                    totalBytesAllocated += sizeof(TestError);
                    break;
                }
                case 4: {
                    TestStatus* obj = new TestStatus(true, i);
                    allocated.push_back(obj);
                    totalAllocations++;
                    totalBytesAllocated += sizeof(TestStatus);
                    break;
                }
            }
        }
        
        // Free batch in reverse order
        for (int i = size - 1; i >= 0; --i) {
            switch (types[i]) {
                case 0:
                    delete static_cast<TestSmall*>(allocated[i]);
                    totalDeallocations++;
                    totalBytesFreed += sizeof(TestSmall);
                    break;
                case 1:
                    delete static_cast<TestMedium*>(allocated[i]);
                    totalDeallocations++;
                    totalBytesFreed += sizeof(TestMedium);
                    break;
                case 2:
                    delete static_cast<TestLarge*>(allocated[i]);
                    totalDeallocations++;
                    totalBytesFreed += sizeof(TestLarge);
                    break;
                case 3:
                    delete static_cast<TestError*>(allocated[i]);
                    totalDeallocations++;
                    totalBytesFreed += sizeof(TestError);
                    break;
                case 4:
                    delete static_cast<TestStatus*>(allocated[i]);
                    totalDeallocations++;
                    totalBytesFreed += sizeof(TestStatus);
                    break;
            }
        }
    }
}

// Worker thread function - Pattern 3: Mixed with delays
void workerPattern3(int threadId) {
    // Register thread name
    std::ostringstream oss;
    oss << "Worker-P3-" << threadId;
    MemManager::getInstance()->registerThreadName(
        static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id())),
        oss.str()
    );
    
    std::random_device rd;
    std::mt19937 gen(rd() + threadId);
    std::uniform_int_distribution<> sizeDist(0, NUM_ALLOC_SIZES - 1);
    std::uniform_int_distribution<> delayDist(0, 10); // microseconds
    
    for (int i = 0; i < ITERATIONS_PER_THREAD / 2; ++i) {
        int sizeType = sizeDist(gen);
        
        // Allocate and track with proper type
        switch (sizeType) {
            case 0: {
                TestSmall* obj = new TestSmall();
                totalAllocations++;
                totalBytesAllocated += sizeof(TestSmall);
                
                if (delayDist(gen) < 2) {
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                }
                
                delete obj;
                totalDeallocations++;
                totalBytesFreed += sizeof(TestSmall);
                break;
            }
            case 1: {
                TestMedium* obj = new TestMedium();
                totalAllocations++;
                totalBytesAllocated += sizeof(TestMedium);
                
                if (delayDist(gen) < 2) {
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                }
                
                delete obj;
                totalDeallocations++;
                totalBytesFreed += sizeof(TestMedium);
                break;
            }
            case 2: {
                TestLarge* obj = new TestLarge();
                totalAllocations++;
                totalBytesAllocated += sizeof(TestLarge);
                
                if (delayDist(gen) < 2) {
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                }
                
                delete obj;
                totalDeallocations++;
                totalBytesFreed += sizeof(TestLarge);
                break;
            }
            case 3: {
                TestError* obj = new TestError(i);
                totalAllocations++;
                totalBytesAllocated += sizeof(TestError);
                
                if (delayDist(gen) < 2) {
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                }
                
                delete obj;
                totalDeallocations++;
                totalBytesFreed += sizeof(TestError);
                break;
            }
            case 4: {
                TestStatus* obj = new TestStatus(true, i);
                totalAllocations++;
                totalBytesAllocated += sizeof(TestStatus);
                
                if (delayDist(gen) < 2) {
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                }
                
                delete obj;
                totalDeallocations++;
                totalBytesFreed += sizeof(TestStatus);
                break;
            }
        }
    }
}

void printStatistics() {
    std::cout << "\n=== Test Statistics ===" << std::endl;
    std::cout << "Total Allocations:   " << totalAllocations.load() << std::endl;
    std::cout << "Total Deallocations: " << totalDeallocations.load() << std::endl;
    std::cout << "Bytes Allocated:     " << totalBytesAllocated.load() << std::endl;
    std::cout << "Bytes Freed:         " << totalBytesFreed.load() << std::endl;
    
    if (totalAllocations.load() != totalDeallocations.load()) {
        std::cout << "\n[WARNING] Allocation/Deallocation mismatch!" << std::endl;
        std::cout << "Difference: " << (totalAllocations.load() - totalDeallocations.load()) << std::endl;
    } else {
        std::cout << "\n[OK] All allocations freed" << std::endl;
    }
    
    if (totalBytesAllocated.load() != totalBytesFreed.load()) {
        std::cout << "[WARNING] Byte count mismatch!" << std::endl;
        std::cout << "Difference: " << (totalBytesAllocated.load() - totalBytesFreed.load()) << " bytes" << std::endl;
    } else {
        std::cout << "[OK] All bytes accounted for" << std::endl;
    }
}

int main() {
    std::cout << "==== Multi-threaded Memory Leak Test ====" << std::endl;
    std::cout << "Testing memory allocation/deallocation in concurrent environment\n" << std::endl;
    
    // Configure memory module to enable checker BEFORE initialization
    std::string memConfig = R"({"check_enable": true, "pools": []})";
    ConfigManager::getInstance().setModuleConfig("memory", memConfig);
    
    // Initialize memory manager
    MemManager::getInstance()->initialize();
    
    if (!MemManager::getInstance()->hasMemChecker()) {
        std::cout << "\n[NOTE] Memory checker not enabled - leak detection limited" << std::endl;
        std::cout << "[TIP] Create memory_config.json with check_enable:true for full leak tracking\n" << std::endl;
    }
    
    // Test Pattern 1: Immediate free
    std::cout << "\n[Pattern 1] Immediate alloc/free..." << std::flush;
    auto start1 = std::chrono::high_resolution_clock::now();
    {
        std::vector<std::thread> threads;
        for (int i = 0; i < NUM_THREADS; ++i) {
            threads.emplace_back(workerPattern1, i);
        }
        for (auto& t : threads) {
            t.join();
        }
    }
    auto end1 = std::chrono::high_resolution_clock::now();
    auto duration1 = std::chrono::duration_cast<std::chrono::milliseconds>(end1 - start1);
    std::cout << " Done (" << duration1.count() << "ms)" << std::endl;
    
    // Test Pattern 2: Batch operations
    std::cout << "[Pattern 2] Batch alloc/free..." << std::flush;
    auto start2 = std::chrono::high_resolution_clock::now();
    {
        std::vector<std::thread> threads;
        for (int i = 0; i < NUM_THREADS; ++i) {
            threads.emplace_back(workerPattern2, i);
        }
        for (auto& t : threads) {
            t.join();
        }
    }
    auto end2 = std::chrono::high_resolution_clock::now();
    auto duration2 = std::chrono::duration_cast<std::chrono::milliseconds>(end2 - start2);
    std::cout << " Done (" << duration2.count() << "ms)" << std::endl;
    
    // Test Pattern 3: Mixed with delays
    std::cout << "[Pattern 3] Mixed with delays..." << std::flush;
    auto start3 = std::chrono::high_resolution_clock::now();
    {
        std::vector<std::thread> threads;
        for (int i = 0; i < NUM_THREADS; ++i) {
            threads.emplace_back(workerPattern3, i);
        }
        for (auto& t : threads) {
            t.join();
        }
    }
    auto end3 = std::chrono::high_resolution_clock::now();
    auto duration3 = std::chrono::duration_cast<std::chrono::milliseconds>(end3 - start3);
    std::cout << " Done (" << duration3.count() << "ms)" << std::endl;
    
    // Print statistics
    printStatistics();
    
    // Get memory statistics from MemManager
    std::cout << "\n=== MemManager Statistics ===" << std::endl;
    auto memStats = MemManager::getInstance()->getMemoryStats();
    
    std::cout << "Current Alloc Count: " << memStats.currentAllocCount << std::endl;
    std::cout << "Current Alloc Size:  " << memStats.currentAllocSize << " bytes" << std::endl;
    std::cout << "Total Pool Memory:   " << memStats.totalPoolMemory << " bytes" << std::endl;
    std::cout << "Pool Count:          " << memStats.poolCount << std::endl;
    std::cout << "Thread Count:        " << memStats.threadCount << std::endl;
    
    // Check for leaks
    std::cout << "\n=== Leak Detection ===" << std::endl;
    bool hasLeaks = false;
    if (memStats.currentAllocCount > 0) {
        std::cout << "[LEAK] " << memStats.currentAllocCount << " blocks still allocated" << std::endl;
        std::cout << "[LEAK] " << memStats.currentAllocSize << " bytes leaked" << std::endl;
        hasLeaks = true;
        
        // Generate detailed leak report
        std::cout << "\nDetailed leak report:" << std::endl;
        MemManager::getInstance()->outputState();
    } else {
        std::cout << "[OK] No memory leaks detected (current alloc count = 0)" << std::endl;
    }
    
    std::cout << "\n==== Test Complete ====" << std::endl;
    return hasLeaks ? 1 : 0;
}
