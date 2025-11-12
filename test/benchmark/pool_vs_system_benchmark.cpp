#include <iostream>
#include <chrono>
#include <vector>
#include <iomanip>
#include <cstring>
#include <cstdlib>
#include "CMemory.hpp"
#include "CConfig.hpp"
#include "CInitialization.hpp"

using namespace lap::core;
using namespace std::chrono;

// Test configuration
constexpr int WARMUP_ITERATIONS = 1000;
constexpr int TEST_ITERATIONS = 100000;
constexpr int ALLOCATION_SIZES[] = {8, 16, 32, 64, 128, 256, 512, 1024};

struct BenchmarkResult {
    double allocTime_ns;
    double freeTime_ns;
    double totalTime_ns;
    double speedup; // Pool vs System speedup ratio
};

// Benchmark system malloc/free
BenchmarkResult benchmarkSystemMalloc(int size) {
    BenchmarkResult result = {0, 0, 0, 0};
    
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
    result.allocTime_ns = duration_cast<nanoseconds>(end - start).count() / (double)TEST_ITERATIONS;
    
    // Benchmark free
    start = high_resolution_clock::now();
    for (void* ptr : ptrs) {
        std::free(ptr);
    }
    end = high_resolution_clock::now();
    result.freeTime_ns = duration_cast<nanoseconds>(end - start).count() / (double)TEST_ITERATIONS;
    
    result.totalTime_ns = result.allocTime_ns + result.freeTime_ns;
    
    return result;
}

// Benchmark pool allocator
BenchmarkResult benchmarkPoolAllocator(int size) {
    BenchmarkResult result = {0, 0, 0, 0};
    
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
    result.allocTime_ns = duration_cast<nanoseconds>(end - start).count() / (double)TEST_ITERATIONS;
    
    // Benchmark free
    start = high_resolution_clock::now();
    for (void* ptr : ptrs) {
        Memory::free(ptr);
    }
    end = high_resolution_clock::now();
    result.freeTime_ns = duration_cast<nanoseconds>(end - start).count() / (double)TEST_ITERATIONS;
    
    result.totalTime_ns = result.allocTime_ns + result.freeTime_ns;
    
    return result;
}

void printHeader() {
    std::cout << "\n╔══════════════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║         Memory Pool Allocator vs System malloc() Benchmark                  ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════════════════════╝\n\n";
    std::cout << "Configuration:\n";
    std::cout << "  - Warmup iterations: " << WARMUP_ITERATIONS << "\n";
    std::cout << "  - Test iterations: " << TEST_ITERATIONS << "\n";
    std::cout << "  - Testing both pool allocator and system malloc()\n\n";
}

void printResults(int size, const BenchmarkResult& poolResult, const BenchmarkResult& sysResult) {
    double allocSpeedup = sysResult.allocTime_ns / poolResult.allocTime_ns;
    double freeSpeedup = sysResult.freeTime_ns / poolResult.freeTime_ns;
    double totalSpeedup = sysResult.totalTime_ns / poolResult.totalTime_ns;
    
    std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    std::cout << "  Allocation Size: " << std::setw(4) << size << " bytes\n";
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n";
    
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "┌────────────────┬──────────────┬──────────────┬──────────────┬─────────────┐\n";
    std::cout << "│ Operation      │  Pool (ns)   │ System (ns)  │   Speedup    │  Improvement│\n";
    std::cout << "├────────────────┼──────────────┼──────────────┼──────────────┼─────────────┤\n";
    
    auto printRow = [](const char* name, double poolVal, double sysVal, double speedup) {
        double improvement = ((sysVal - poolVal) / sysVal) * 100.0;
        std::cout << "│ " << std::setw(14) << std::left << name 
                  << " │ " << std::setw(12) << std::right << poolVal
                  << " │ " << std::setw(12) << sysVal
                  << " │ " << std::setw(12) << speedup << "x"
                  << " │ ";
        if (improvement >= 0) {
            std::cout << "+" << std::setw(8) << improvement << "% │\n";
        } else {
            std::cout << std::setw(9) << improvement << "% │\n";
        }
    };
    
    printRow("malloc()", poolResult.allocTime_ns, sysResult.allocTime_ns, allocSpeedup);
    printRow("free()", poolResult.freeTime_ns, sysResult.freeTime_ns, freeSpeedup);
    
    std::cout << "├────────────────┼──────────────┼──────────────┼──────────────┼─────────────┤\n";
    printRow("TOTAL", poolResult.totalTime_ns, sysResult.totalTime_ns, totalSpeedup);
    std::cout << "└────────────────┴──────────────┴──────────────┴──────────────┴─────────────┘\n";
    
    // Performance verdict
    if (totalSpeedup >= 2.0) {
        std::cout << "\n  ⚡ Pool allocator is " << std::setprecision(1) << totalSpeedup 
                  << "x FASTER than system malloc!\n";
    } else if (totalSpeedup >= 1.2) {
        std::cout << "\n  ✓ Pool allocator is " << std::setprecision(1) << totalSpeedup 
                  << "x faster than system malloc\n";
    } else if (totalSpeedup >= 0.8) {
        std::cout << "\n  ≈ Pool allocator performs similarly to system malloc\n";
    } else {
        std::cout << "\n  ⚠ System malloc is faster for this size\n";
    }
}

void printSummary(const std::vector<std::pair<int, double>>& speedups) {
    std::cout << "\n\n╔══════════════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                            Performance Summary                               ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════════════════════╝\n\n";
    
    double avgSpeedup = 0.0;
    double maxSpeedup = 0.0;
    double minSpeedup = 1000.0;
    int bestSize = 0, worstSize = 0;
    
    for (const auto& [size, speedup] : speedups) {
        avgSpeedup += speedup;
        if (speedup > maxSpeedup) {
            maxSpeedup = speedup;
            bestSize = size;
        }
        if (speedup < minSpeedup) {
            minSpeedup = speedup;
            worstSize = size;
        }
    }
    avgSpeedup /= speedups.size();
    
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "┌────────────────────────────────┬──────────────┐\n";
    std::cout << "│ Metric                         │    Value     │\n";
    std::cout << "├────────────────────────────────┼──────────────┤\n";
    std::cout << "│ Average speedup                │ " << std::setw(11) << avgSpeedup << "x │\n";
    std::cout << "│ Maximum speedup                │ " << std::setw(11) << maxSpeedup << "x │\n";
    std::cout << "│ Minimum speedup                │ " << std::setw(11) << minSpeedup << "x │\n";
    std::cout << "│ Best performance at            │ " << std::setw(9) << bestSize << " B │\n";
    std::cout << "│ Worst performance at           │ " << std::setw(9) << worstSize << " B │\n";
    std::cout << "└────────────────────────────────┴──────────────┘\n\n";
    
    std::cout << "Key findings:\n";
    std::cout << "  • Pool allocator average speedup: " << avgSpeedup << "x\n";
    std::cout << "  • Best speedup at " << bestSize << " bytes: " << maxSpeedup << "x faster\n";
    
    if (avgSpeedup >= 2.0) {
        std::cout << "\n⚡ Pool allocator provides SIGNIFICANT performance improvement!\n";
        std::cout << "   Recommended for frequent allocations of small to medium sizes.\n";
    } else if (avgSpeedup >= 1.2) {
        std::cout << "\n✓ Pool allocator provides measurable performance benefit.\n";
        std::cout << "  Recommended for allocation-intensive applications.\n";
    } else {
        std::cout << "\n≈ Pool allocator performs comparably to system malloc.\n";
        std::cout << "  Benefits: Leak detection, memory tracking, predictable behavior.\n";
    }
    
    std::cout << "\nAdditional benefits of pool allocator:\n";
    std::cout << "  ✓ Memory leak detection and reporting\n";
    std::cout << "  ✓ Per-thread and per-class allocation tracking\n";
    std::cout << "  ✓ Reduced memory fragmentation\n";
    std::cout << "  ✓ Configurable alignment and pool sizes\n";
    std::cout << "  ✓ Built-in memory corruption detection\n";
}

int main() {
    // AUTOSAR-compliant initialization
    auto initResult = Initialize();
    if (!initResult.HasValue()) {
        std::cerr << "Failed to initialize Core: " << initResult.Error().Message() << "\n";
        return 1;
    }
    
    // Configure memory manager with 8-byte alignment
    ConfigManager& configMgr = ConfigManager::getInstance();
    nlohmann::json config = configMgr.getModuleConfigJson("memory");
    config["align"] = 8;
    configMgr.setModuleConfigJson("memory", config);
    
    // Re-initialize MemoryManager with new config
    MemoryManager::getInstance()->uninitialize();
    MemoryManager::getInstance()->initialize();
    
    printHeader();
    
    std::vector<std::pair<int, double>> speedups;
    
    for (int size : ALLOCATION_SIZES) {
        std::cout << "Testing allocation size: " << std::setw(4) << size << " bytes..." << std::flush;
        
        auto sysResult = benchmarkSystemMalloc(size);
        std::cout << " [System]" << std::flush;
        
        auto poolResult = benchmarkPoolAllocator(size);
        std::cout << " [Pool] ✓\n" << std::flush;
        
        double speedup = sysResult.totalTime_ns / poolResult.totalTime_ns;
        speedups.push_back({size, speedup});
        
        printResults(size, poolResult, sysResult);
    }
    
    printSummary(speedups);
    
    std::cout << "\n";
    
    // AUTOSAR-compliant deinitialization
    auto deinitResult = Deinitialize();
    (void)deinitResult;
    
    return 0;
}
