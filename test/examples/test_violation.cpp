/**
 * @file        test_violation.cpp
 * @brief       Test CViolation implementation
 * @date        2025-11-12
 */

#include "CViolation.hpp"
#include "CInitialization.hpp"
#include <iostream>

using namespace lap::core;

int main()
{
    // AUTOSAR-compliant initialization
    auto initResult = Initialize();
    if (!initResult.HasValue()) {
        std::cerr << "Failed to initialize Core: " << initResult.Error().Message() << "\n";
        return 1;
    }
    
    std::cout << "=== Testing CViolation Implementation ===" << std::endl;
    
    // Test 1: ViolationTypeToString
    std::cout << "\n[Test 1] ViolationTypeToString():" << std::endl;
    std::cout << "  kPlatformNotInitialized: " 
              << ViolationTypeToString(ViolationType::kPlatformNotInitialized) << std::endl;
    std::cout << "  kInvalidArgument: " 
              << ViolationTypeToString(ViolationType::kInvalidArgument) << std::endl;
    std::cout << "  kConfigurationMissing: " 
              << ViolationTypeToString(ViolationType::kConfigurationMissing) << std::endl;
    
    // Test 2: LAP_ASSERT macro (should pass)
    std::cout << "\n[Test 2] LAP_ASSERT (passing assertion):" << std::endl;
    int* validPtr = new int(42);
    LAP_ASSERT(validPtr != nullptr, "Valid pointer should not trigger violation");
    std::cout << "  ✓ Assertion passed correctly" << std::endl;
    delete validPtr;
    
    // Test 3: Comment out to avoid termination
    std::cout << "\n[Test 3] RaiseViolation (commented out - would terminate):" << std::endl;
    std::cout << "  To test termination, uncomment the following line:" << std::endl;
    std::cout << "  // LAP_RAISE_VIOLATION(ViolationType::kAssertionFailure, \"Test violation\");" << std::endl;
    
    // Uncomment to test actual violation (process will terminate):
    // LAP_RAISE_VIOLATION(ViolationType::kAssertionFailure, "This is a test violation");
    
    std::cout << "\n=== All tests completed successfully ===" << std::endl;
    std::cout << "Note: Actual violation termination test is commented out." << std::endl;
    
    return 0;
}
