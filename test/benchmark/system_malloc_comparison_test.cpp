#include <iostream>
#include <chrono>
#include <vector>
#include <iomanip>
#include <cstring>
#include <cstdlib>
#include "CMemory.hpp"
#include "CConfig.hpp"

using namespace lap::core;
using namespace std::chrono;

// Test configuration
constexpr int WARMUP_ITERATIONS = 100;
constexpr int TEST_ITERATIONS = 10000;
constexpr int ALLOCATION_SIZES[] = {8, 16, 32, 64, 128, 256, 512, 1024};

struct PerformanceStats {
    double allocTime_ns;
    double freeTime_ns;
    double readTime_ns;
    double writeTime_ns;
    double totalTime_ns;
};

// Benchmark using system malloc/free
PerformanceStats benchmarkSystemMalloc(int size) {
    PerformanceStats stats = {0, 0, 0, 0, 0};
    
    // Warmup
    for (int i = 0; i < WARMUP_ITERATIONS; ++i) {
        void* ptr = std::malloc(size);
        if (ptr) {
            std::memset(ptr, 0xFF, size);
            std::free(ptr);
        }
    }
    
    std::vector<void*> ptrs;
    ptrs.reserve(TEST_ITERATIONS);
    
    // Benchmark allocation
    auto start = high_resolution_clock::now();
    for (int i = 0; i < TEST_ITERATIONS; ++i) {
        void* ptr = std::malloc(size);
        ptrs.push_back(ptr);
    }
    auto end = high_resolution_clock::now();
    stats.allocTime_ns = duration_cast<nanoseconds>(end - start).count() / (double)TEST_ITERATIONS;
    
    // Benchmark write
    start = high_resolution_clock::now();
    for (void* ptr : ptrs) {
        if (ptr) {
            std::memset(ptr, 0xAA, size);
        }
    }
    end = high_resolution_clock::now();
    stats.writeTime_ns = duration_cast<nanoseconds>(end - start).count() / (double)TEST_ITERATIONS;
    
    // Benchmark read
    volatile UInt32 sum = 0;
    start = high_resolution_clock::now();
    for (void* ptr : ptrs) {
        if (ptr) {
            unsigned char* data = static_cast<unsigned char*>(ptr);
            for (int j = 0; j < size; ++j) {
                sum += data[j];
            }
        }
    }
    end = high_resolution_clock::now();
    stats.readTime_ns = duration_cast<nanoseconds>(end - start).count() / (double)TEST_ITERATIONS;
    
    // Benchmark free
    start = high_resolution_clock::now();
    for (void* ptr : ptrs) {
        std::free(ptr);
    }
    end = high_resolution_clock::now();
    stats.freeTime_ns = duration_cast<nanoseconds>(end - start).count() / (double)TEST_ITERATIONS;
    
    stats.totalTime_ns = stats.allocTime_ns + stats.freeTime_ns + 
                         stats.readTime_ns + stats.writeTime_ns;
    
    return stats;
}

// Benchmark using Memory pool allocator
PerformanceStats benchmarkPoolAllocator(int size) {
    PerformanceStats stats = {0, 0, 0, 0, 0};
    
    // Warmup
    for (int i = 0; i < WARMUP_ITERATIONS; ++i) {
        void* ptr = Memory::malloc(size);
        if (ptr) {
            std::memset(ptr, 0xFF, size);
            Memory::free(ptr);
        }
    }
    
    std::vector<void*> ptrs;
    ptrs.reserve(TEST_ITERATIONS);
    
    // Benchmark allocation
    auto start = high_resolution_clock::now();
    for (int i = 0; i < TEST_ITERATIONS; ++i) {
        void* ptr = Memory::malloc(size);
        ptrs.push_back(ptr);
    }
    auto end = high_resolution_clock::now();
    stats.allocTime_ns = duration_cast<nanoseconds>(end - start).count() / (double)TEST_ITERATIONS;
    
    // Benchmark write
    start = high_resolution_clock::now();
    for (void* ptr : ptrs) {
        if (ptr) {
            std::memset(ptr, 0xAA, size);
        }
    }
    end = high_resolution_clock::now();
    stats.writeTime_ns = duration_cast<nanoseconds>(end - start).count() / (double)TEST_ITERATIONS;
    
    // Benchmark read
    volatile UInt32 sum = 0;
    start = high_resolution_clock::now();
    for (void* ptr : ptrs) {
        if (ptr) {
            unsigned char* data = static_cast<unsigned char*>(ptr);
            for (int j = 0; j < size; ++j) {
                sum += data[j];
            }
        }
    }
    end = high_resolution_clock::now();
    stats.readTime_ns = duration_cast<nanoseconds>(end - start).count() / (double)TEST_ITERATIONS;
    
    // Benchmark free
    start = high_resolution_clock::now();
    for (void* ptr : ptrs) {
        Memory::free(ptr);
    }
    end = high_resolution_clock::now();
    stats.freeTime_ns = duration_cast<nanoseconds>(end - start).count() / (double)TEST_ITERATIONS;
    
    stats.totalTime_ns = stats.allocTime_ns + stats.freeTime_ns + 
                         stats.readTime_ns + stats.writeTime_ns;
    
    return stats;
}

void printHeader() {
    std::cout << "\n╔══════════════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║         System malloc vs Pool Allocator Performance Comparison               ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════════════════════╝\n\n";
    std::cout << "Configuration:\n";
    std::cout << "  - Warmup iterations: " << WARMUP_ITERATIONS << "\n";
    std::cout << "  - Test iterations: " << TEST_ITERATIONS << "\n";
    std::cout << "  - Pool allocator alignment: 8 bytes (system default)\n\n";
}

void printResults(int size, const PerformanceStats& sysStats, 
                  const PerformanceStats& poolStats) {
    std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    std::cout << "  Allocation Size: " << std::setw(4) << size << " bytes\n";
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n";
    
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "┌────────────────┬──────────────┬──────────────┬──────────────┬─────────────┐\n";
    std::cout << "│ Operation      │ System (ns)  │  Pool (ns)   │  Speedup     │  Advantage  │\n";
    std::cout << "├────────────────┼──────────────┼──────────────┼──────────────┼─────────────┤\n";
    
    auto printRow = [](const char* name, double sysVal, double poolVal) {
        double speedup = sysVal / poolVal;
        const char* advantage = speedup > 1.0 ? "Pool" : "System";
        std::cout << "│ " << std::setw(14) << std::left << name 
                  << " │ " << std::setw(12) << std::right << sysVal
                  << " │ " << std::setw(12) << poolVal
                  << " │ " << std::setw(12) << speedup << "x"
                  << " │ " << std::setw(11) << std::left << advantage << " │\n";
    };
    
    printRow("malloc()", sysStats.allocTime_ns, poolStats.allocTime_ns);
    printRow("memset()", sysStats.writeTime_ns, poolStats.writeTime_ns);
    printRow("read loop", sysStats.readTime_ns, poolStats.readTime_ns);
    printRow("free()", sysStats.freeTime_ns, poolStats.freeTime_ns);
    
    std::cout << "├────────────────┼──────────────┼──────────────┼──────────────┼─────────────┤\n";
    printRow("TOTAL", sysStats.totalTime_ns, poolStats.totalTime_ns);
    std::cout << "└────────────────┴──────────────┴──────────────┴──────────────┴─────────────┘\n";
    
    // Additional analysis
    double allocSpeedup = sysStats.allocTime_ns / poolStats.allocTime_ns;
    double freeSpeedup = sysStats.freeTime_ns / poolStats.freeTime_ns;
    
    if (allocSpeedup > 1.5) {
        std::cout << "\n  ✓ Pool allocator is significantly faster for allocation (" 
                  << std::fixed << std::setprecision(1) 
                  << ((allocSpeedup - 1.0) * 100.0) << "% faster)\n";
    } else if (allocSpeedup < 0.7) {
        std::cout << "\n  ⚠ System malloc is faster for allocation (" 
                  << std::fixed << std::setprecision(1) 
                  << ((1.0/allocSpeedup - 1.0) * 100.0) << "% faster)\n";
    }
    
    if (freeSpeedup > 1.5) {
        std::cout << "  ✓ Pool allocator is significantly faster for deallocation (" 
                  << std::fixed << std::setprecision(1) 
                  << ((freeSpeedup - 1.0) * 100.0) << "% faster)\n";
    }
}

void printSummary(const std::vector<std::pair<int, PerformanceStats>>& sysResults,
                  const std::vector<std::pair<int, PerformanceStats>>& poolResults) {
    std::cout << "\n\n╔══════════════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                            Performance Summary                               ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════════════════════╝\n\n";
    
    double avgAllocSpeedup = 0.0;
    double avgFreeSpeedup = 0.0;
    double avgTotalSpeedup = 0.0;
    int poolFasterCount = 0;
    
    std::cout << "Size-by-size comparison:\n";
    std::cout << "┌──────────┬──────────────┬──────────────┬──────────────┐\n";
    std::cout << "│   Size   │ Alloc Speedup│ Free Speedup │ Total Speedup│\n";
    std::cout << "├──────────┼──────────────┼──────────────┼──────────────┤\n";
    
    for (size_t i = 0; i < sysResults.size(); ++i) {
        double allocSpeedup = sysResults[i].second.allocTime_ns / poolResults[i].second.allocTime_ns;
        double freeSpeedup = sysResults[i].second.freeTime_ns / poolResults[i].second.freeTime_ns;
        double totalSpeedup = sysResults[i].second.totalTime_ns / poolResults[i].second.totalTime_ns;
        
        avgAllocSpeedup += allocSpeedup;
        avgFreeSpeedup += freeSpeedup;
        avgTotalSpeedup += totalSpeedup;
        
        if (totalSpeedup > 1.0) {
            poolFasterCount++;
        }
        
        std::cout << "│ " << std::setw(8) << std::right << sysResults[i].first << " │ "
                  << std::setw(12) << std::fixed << std::setprecision(2) << allocSpeedup << "x │ "
                  << std::setw(12) << freeSpeedup << "x │ "
                  << std::setw(12) << totalSpeedup << "x │\n";
    }
    std::cout << "└──────────┴──────────────┴──────────────┴──────────────┘\n\n";
    
    int count = sysResults.size();
    avgAllocSpeedup /= count;
    avgFreeSpeedup /= count;
    avgTotalSpeedup /= count;
    
    std::cout << "Average Performance:\n";
    std::cout << "  • Allocation speedup: " << std::fixed << std::setprecision(2) 
              << avgAllocSpeedup << "x\n";
    std::cout << "  • Deallocation speedup: " << avgFreeSpeedup << "x\n";
    std::cout << "  • Overall speedup: " << avgTotalSpeedup << "x\n\n";
    
    std::cout << "Pool allocator was faster in " << poolFasterCount << "/" << count 
              << " test cases\n\n";
    
    std::cout << "Key findings:\n";
    if (avgTotalSpeedup > 1.2) {
        std::cout << "  ✓ Pool allocator provides significant performance advantage (>20% faster)\n";
        std::cout << "  ✓ Especially effective for small to medium allocations (<= 1KB)\n";
        std::cout << "  ✓ Reduces memory fragmentation through pre-allocated pools\n";
    } else if (avgTotalSpeedup > 1.0) {
        std::cout << "  ✓ Pool allocator provides moderate performance advantage\n";
        std::cout << "  • Consider for allocation-heavy workloads\n";
    } else {
        std::cout << "  • System malloc performs comparably or better\n";
        std::cout << "  • Pool allocator still provides benefits:\n";
        std::cout << "    - Leak detection and tracking\n";
        std::cout << "    - Memory usage statistics\n";
        std::cout << "    - Controlled alignment\n";
    }
    
    std::cout << "\nRecommendation:\n";
    if (avgAllocSpeedup > 1.3 && avgFreeSpeedup > 1.3) {
        std::cout << "  ✓ Use pool allocator for high-frequency allocation/deallocation patterns\n";
        std::cout << "  ✓ Ideal for object pools, message buffers, and temporary allocations\n";
    } else {
        std::cout << "  • Pool allocator suitable when additional features are needed:\n";
        std::cout << "    - Memory leak detection\n";
        std::cout << "    - Per-thread allocation tracking\n";
        std::cout << "    - Custom alignment requirements\n";
    }
}

int main() {
    printHeader();
    
    // Initialize memory manager with 8-byte alignment
    ConfigManager& configMgr = ConfigManager::getInstance();
    nlohmann::json config = configMgr.getModuleConfigJson("memory");
    config["align"] = 8;
    configMgr.setModuleConfigJson("memory", config);
    MemManager::getInstance()->initialize();
    
    std::vector<std::pair<int, PerformanceStats>> sysResults, poolResults;
    
    for (int size : ALLOCATION_SIZES) {
        std::cout << "\nTesting allocation size: " << size << " bytes..." << std::flush;
        
        auto sysStats = benchmarkSystemMalloc(size);
        std::cout << " [System malloc]" << std::flush;
        
        auto poolStats = benchmarkPoolAllocator(size);
        std::cout << " [Pool allocator] ✓\n" << std::flush;
        
        sysResults.push_back({size, sysStats});
        poolResults.push_back({size, poolStats});
        
        printResults(size, sysStats, poolStats);
    }
    
    printSummary(sysResults, poolResults);
    
    std::cout << "\n";
    
    return 0;
}
