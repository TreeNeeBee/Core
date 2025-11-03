/**
 * @file test_dynamic_magic.cpp
 * @brief Test to verify runtime XOR mask is different between executions
 */

#include <iostream>
#include <iomanip>
#include "CMemory.hpp"

int main() {
    std::cout << "=== Dynamic Magic Test ===" << std::endl;
    std::cout << "This test verifies that the runtime XOR mask is generated dynamically" << std::endl;
    std::cout << "and differs between executions (due to PID, timestamp, ASLR, etc.)" << std::endl << std::endl;
    
    // Get the runtime mask
    auto mask = lap::core::MemManager::getRuntimeXorMask();
    
    std::cout << "Runtime XOR Mask: 0x" << std::hex << std::setw(16) << std::setfill('0') 
              << mask << std::dec << std::endl;
    
    std::cout << std::endl << "Run this test multiple times to verify the mask changes:" << std::endl;
    std::cout << "  $ for i in {1..5}; do ./test_dynamic_magic | grep 'Runtime XOR Mask'; done" << std::endl;
    std::cout << std::endl << "=== Test Completed ===" << std::endl;
    
    return 0;
}
