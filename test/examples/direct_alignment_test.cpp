/**
 * @file direct_alignment_test.cpp
 * @brief Direct test to verify alignment configuration
 */

#include <iostream>
#include <iomanip>
#include "CMemory.hpp"
#include "CConfig.hpp"
#include "CInitialization.hpp"
#include <nlohmann/json.hpp>

using namespace lap::core;

void printAlignment(const char* label, void* ptr) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    std::cout << label << ": 0x" << std::hex << std::setw(16) << std::setfill('0') << addr << std::dec;
    
    size_t alignment = 1;
    for (size_t a = 2; a <= 16; a *= 2) {
        if (addr % a == 0) alignment = a;
        else break;
    }
    std::cout << " [" << alignment << "-byte aligned]" << std::endl;
}

int main() {
    std::cout << "=== Direct Alignment Test ===" << std::endl;
    std::cout << "\nSystem info:" << std::endl;
    std::cout << "  Pointer size: " << sizeof(void*) << " bytes" << std::endl;
    std::cout << "  sizeof(size_t): " << sizeof(size_t) << " bytes" << std::endl;
    
    // Read current configuration
    std::cout << "\n--- Reading Configuration ---" << std::endl;
    try {
        ConfigManager& configMgr = ConfigManager::getInstance();
        nlohmann::json config = configMgr.getModuleConfigJson("memory");
        
        if (!config.is_null() && config.contains("align")) {
            std::cout << "Config align value: " << config["align"].get<int>() << std::endl;
        } else {
            std::cout << "No align field in config, using default" << std::endl;
        }
        
        if (config.contains("check_enable")) {
            std::cout << "Config check_enable: " << (config["check_enable"].get<bool>() ? "true" : "false") << std::endl;
        }
    } catch (const std::exception& e) {
        std::cout << "Exception reading config: " << e.what() << std::endl;
    }
    
    // Initialize memory manager
    std::cout << "\n--- Initializing Core ---" << std::endl;
    auto initResult = Initialize();
    if (!initResult.HasValue()) {
        std::cerr << "Failed to initialize Core: " << initResult.Error().Message() << "\n";
        return 1;
    }
    
    // Test allocations
    std::cout << "\n--- Testing Allocations ---" << std::endl;
    
    const size_t testSizes[] = {1, 7, 16, 31, 64, 127, 256};
    
    for (size_t size : testSizes) {
        void* ptr = Memory::malloc(size);
        if (ptr) {
            std::cout << "malloc(" << std::setw(3) << size << ") = ";
            printAlignment("", ptr);
            Memory::free(ptr);
        } else {
            std::cout << "malloc(" << size << ") failed!" << std::endl;
        }
    }
    
    // Test multiple consecutive allocations
    std::cout << "\n--- Consecutive Allocations (17 bytes each) ---" << std::endl;
    void* ptrs[5];
    for (int i = 0; i < 5; ++i) {
        ptrs[i] = Memory::malloc(17);
        std::cout << "Alloc[" << i << "]: ";
        printAlignment("", ptrs[i]);
    }
    
    for (int i = 0; i < 5; ++i) {
        Memory::free(ptrs[i]);
    }
    
    std::cout << "\n=== Test Complete ===" << std::endl;
    
    // AUTOSAR-compliant deinitialization
    auto deinitResult = Deinitialize();
    (void)deinitResult;
    
    return 0;
}
