#include <iostream>
#include "CInitialization.hpp"

int main() {
    using namespace lap::core;
    
    std::cout << "Testing lap::core::Initialize and Deinitialize" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    
    // Test 1: Basic initialization
    std::cout << "\n[Test 1] Basic Initialization" << std::endl;
    auto result = Initialize();
    if (result.HasValue()) {
        std::cout << "✓ Initialize() succeeded" << std::endl;
    } else {
        std::cout << "✗ Initialize() failed: " << result.Error().Message().data() << std::endl;
        return 1;
    }
    
    // Test 2: Double initialization (should fail)
    std::cout << "\n[Test 2] Double Initialization (should fail)" << std::endl;
    auto result2 = Initialize();
    if (!result2.HasValue()) {
        std::cout << "✓ Second Initialize() correctly failed" << std::endl;
        std::cout << "   Error: " << result2.Error().Message().data() << std::endl;
    } else {
        std::cout << "✗ Second Initialize() should have failed!" << std::endl;
        return 1;
    }
    
    // Test 3: Deinitialization
    std::cout << "\n[Test 3] Deinitialization" << std::endl;
    auto deinitResult = Deinitialize();
    if (deinitResult.HasValue()) {
        std::cout << "✓ Deinitialize() succeeded" << std::endl;
    } else {
        std::cout << "✗ Deinitialize() failed: " << deinitResult.Error().Message().data() << std::endl;
        return 1;
    }
    
    // Test 4: Re-initialization
    std::cout << "\n[Test 4] Re-initialization" << std::endl;
    auto reinitResult = Initialize();
    if (reinitResult.HasValue()) {
        std::cout << "✓ Re-initialize() succeeded" << std::endl;
    } else {
        std::cout << "✗ Re-initialize() failed: " << reinitResult.Error().Message().data() << std::endl;
        return 1;
    }
    
    // Cleanup
    Deinitialize();
    
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "All tests passed!" << std::endl;
    
    return 0;
}
