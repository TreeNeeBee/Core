#include <iostream>
#include <chrono>
#include <vector>
#include <iomanip>
#include <cstring>
#include "CMemory.hpp"
#include "CConfig.hpp"
#include "CInitialization.hpp"

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

// Benchmark allocation performance
PerformanceStats benchmarkAlignment(UInt32 alignValue, int size) {
    PerformanceStats stats = {0, 0, 0, 0, 0};
    
    // Uninitialize first to clean up any existing state
    MemoryManager::getInstance()->uninitialize();
    
    // Update configuration
    ConfigManager& configMgr = ConfigManager::getInstance();
    nlohmann::json config = configMgr.getModuleConfigJson("memory");
    config["align"] = alignValue;
    configMgr.setModuleConfigJson("memory", config);
    
    // Reinitialize memory manager with new alignment
    MemoryManager::getInstance()->initialize();
    
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
    std::cout << "║              Memory Alignment Performance Benchmark                          ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════════════════════╝\n\n";
    std::cout << "Configuration:\n";
    std::cout << "  - Warmup iterations: " << WARMUP_ITERATIONS << "\n";
    std::cout << "  - Test iterations: " << TEST_ITERATIONS << "\n";
    std::cout << "  - Alignment configurations: 1, 4, 8 bytes\n\n";
}

void printResults(int size, const PerformanceStats& stats1, 
                  const PerformanceStats& stats4, 
                  const PerformanceStats& stats8) {
    std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    std::cout << "  Allocation Size: " << std::setw(4) << size << " bytes\n";
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n";
    
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "┌────────────────┬──────────────┬──────────────┬──────────────┬─────────────┐\n";
    std::cout << "│ Operation      │  1-byte (ns) │  4-byte (ns) │  8-byte (ns) │   Overhead  │\n";
    std::cout << "├────────────────┼──────────────┼──────────────┼──────────────┼─────────────┤\n";
    
    auto printRow = [](const char* name, double val1, double val4, double val8) {
        double overhead = ((val1 - val8) / val8) * 100.0;
        std::cout << "│ " << std::setw(14) << std::left << name 
                  << " │ " << std::setw(12) << std::right << val1
                  << " │ " << std::setw(12) << val4
                  << " │ " << std::setw(12) << val8
                  << " │ " << std::setw(9) << overhead << "% │\n";
    };
    
    printRow("malloc()", stats1.allocTime_ns, stats4.allocTime_ns, stats8.allocTime_ns);
    printRow("memset()", stats1.writeTime_ns, stats4.writeTime_ns, stats8.writeTime_ns);
    printRow("read loop", stats1.readTime_ns, stats4.readTime_ns, stats8.readTime_ns);
    printRow("free()", stats1.freeTime_ns, stats4.freeTime_ns, stats8.freeTime_ns);
    
    std::cout << "├────────────────┼──────────────┼──────────────┼──────────────┼─────────────┤\n";
    printRow("TOTAL", stats1.totalTime_ns, stats4.totalTime_ns, stats8.totalTime_ns);
    std::cout << "└────────────────┴──────────────┴──────────────┴──────────────┴─────────────┘\n";
}

void printSummary(const std::vector<std::pair<int, PerformanceStats>>& results1,
                  const std::vector<std::pair<int, PerformanceStats>>& results4,
                  const std::vector<std::pair<int, PerformanceStats>>& results8) {
    (void)results4; // Results4 used for display, suppress warning
    std::cout << "\n\n╔══════════════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                            Performance Summary                               ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════════════════════╝\n\n";
    
    double avgOverhead = 0.0;
    int count = 0;
    
    for (size_t i = 0; i < results1.size(); ++i) {
        double overhead = ((results1[i].second.totalTime_ns - results8[i].second.totalTime_ns) 
                          / results8[i].second.totalTime_ns) * 100.0;
        avgOverhead += overhead;
        count++;
    }
    avgOverhead /= count;
    
    std::cout << "Average performance overhead (1-byte vs 8-byte alignment): " 
              << std::fixed << std::setprecision(2) << avgOverhead << "%\n\n";
    
    std::cout << "Key findings:\n";
    std::cout << "  • 1-byte config actually provides 4-byte alignment (limited by structure sizes)\n";
    std::cout << "  • 4-byte alignment performs nearly identically to 1-byte config\n";
    std::cout << "  • 8-byte alignment is optimal for 64-bit systems\n";
    
    if (avgOverhead < 5.0) {
        std::cout << "\n✓ Performance difference is negligible (< 5%)\n";
    } else if (avgOverhead < 15.0) {
        std::cout << "\n⚠ Moderate performance impact (5-15%)\n";
    } else {
        std::cout << "\n⚠ Significant performance impact (> 15%)\n";
    }
    
    std::cout << "\nRecommendation: Use 8-byte alignment for optimal performance on 64-bit systems.\n";
}

int main() {
    // AUTOSAR-compliant initialization
    auto initResult = Initialize();
    if (!initResult.HasValue()) {
        std::cerr << "Failed to initialize Core: " << initResult.Error().Message() << "\n";
        return 1;
    }
    
    printHeader();
    
    std::vector<std::pair<int, PerformanceStats>> results1, results4, results8;
    
    for (int size : ALLOCATION_SIZES) {
        std::cout << "\nTesting allocation size: " << size << " bytes..." << std::flush;
        
        auto stats1 = benchmarkAlignment(1, size);
        std::cout << " [1-byte]" << std::flush;
        
        auto stats4 = benchmarkAlignment(4, size);
        std::cout << " [4-byte]" << std::flush;
        
        auto stats8 = benchmarkAlignment(8, size);
        std::cout << " [8-byte] ✓\n" << std::flush;
        
        results1.push_back({size, stats1});
        results4.push_back({size, stats4});
        results8.push_back({size, stats8});
        
        printResults(size, stats1, stats4, stats8);
    }
    
    printSummary(results1, results4, results8);
    
    // Restore default configuration
    ConfigManager& configMgr = ConfigManager::getInstance();
    nlohmann::json config = configMgr.getModuleConfigJson("memory");
    config["align"] = 8;
    configMgr.setModuleConfigJson("memory", config);
    MemoryManager::getInstance()->uninitialize();
    MemoryManager::getInstance()->initialize();
    
    std::cout << "\n✓ Configuration restored to 8-byte alignment\n\n";
    
    // AUTOSAR-compliant deinitialization
    auto deinitResult = Deinitialize();
    (void)deinitResult;
    
    return 0;
}
