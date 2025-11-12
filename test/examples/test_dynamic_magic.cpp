/**
 * @file test_dynamic_magic.cpp
 * @brief Test to verify runtime XOR mask is different between executions
 */

#include <iostream>
#include <iomanip>
#include "CInitialization.hpp"
#include "CMemory.hpp"

int main() {
    // AUTOSAR-compliant initialization
    auto initResult = lap::core::Initialize();
    if (!initResult.HasValue()) {
        std::cerr << "Initialization failed!" << std::endl;
        return 1;
    }
    
    std::cout << "=== Dynamic Magic Test ===" << std::endl;
    std::cout << "This test verifies that the runtime XOR mask is generated dynamically" << std::endl;
    std::cout << "and differs between executions (due to PID, timestamp, ASLR, etc.)" << std::endl << std::endl;
    
    // Get the runtime mask
    auto mask = lap::core::MemoryManager::getRuntimeXorMask();
    
    std::cout << "Runtime XOR Mask: 0x" << std::hex << std::setw(16) << std::setfill('0') 
              << mask << std::dec << std::endl;
    
    std::cout << std::endl << "Run this test multiple times to verify the mask changes:" << std::endl;
    std::cout << "  $ for i in {1..5}; do ./test_dynamic_magic | grep 'Runtime XOR Mask'; done" << std::endl;
    std::cout << std::endl << "=== Test Completed ===" << std::endl;
    
    // AUTOSAR-compliant deinitialization
    auto deinitResult = lap::core::Deinitialize();
    (void)deinitResult;
    
    return 0;
}
