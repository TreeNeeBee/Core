/**
 * @file initialization_example.cpp
 * @brief Example demonstrating lap::core initialization and deinitialization
 * @date 2025-11-12
 * 
 * This example shows the proper way to initialize and deinitialize the
 * AUTOSAR Adaptive Platform Core components in an application.
 */

#include <iostream>
#include "CInitialization.hpp"
#include "CConfig.hpp"
#include "CMemoryManager.hpp"

using namespace lap::core;

void printSeparator() {
    std::cout << "\n" << std::string(60, '=') << "\n\n";
}

void demonstrateBasicUsage() {
    std::cout << "=== Basic Initialization Example ===" << std::endl;
    
    // Initialize the platform
    std::cout << "Calling lap::core::Initialize()..." << std::endl;
    auto initResult = Initialize();
    
    if (initResult.HasValue()) {
        std::cout << "✓ Initialization succeeded!" << std::endl;
        
        // Now we can safely use other ARA components
        std::cout << "\nPlatform is ready. You can now:" << std::endl;
        std::cout << "  - Use memory manager" << std::endl;
        std::cout << "  - Load/save configurations" << std::endl;
        std::cout << "  - Use other ARA services" << std::endl;
        
        // Demonstrate that components are available
        auto* memMgr = MemoryManager::getInstance();
        if (memMgr != nullptr) {
            std::cout << "\n✓ Memory Manager is initialized" << std::endl;
        }
        
        auto& config = ConfigManager::getInstance();
        (void)config; // Suppress unused variable warning
        std::cout << "✓ Configuration Manager is available" << std::endl;
        
    } else {
        std::cerr << "✗ Initialization failed!" << std::endl;
        std::cerr << "  Error: " << initResult.Error().Message().data() << std::endl;
        return;
    }
    
    printSeparator();
    
    // Deinitialize the platform
    std::cout << "Calling lap::core::Deinitialize()..." << std::endl;
    auto deinitResult = Deinitialize();
    
    if (deinitResult.HasValue()) {
        std::cout << "✓ Deinitialization succeeded!" << std::endl;
    } else {
        std::cerr << "✗ Deinitialization failed!" << std::endl;
        std::cerr << "  Error: " << deinitResult.Error().Message().data() << std::endl;
    }
}

void demonstrateCommandLineArgs(int argc, char* argv[]) {
    std::cout << "\n=== Command Line Arguments Example ===" << std::endl;
    
    std::cout << "Received " << argc << " arguments:" << std::endl;
    for (int i = 0; i < argc; ++i) {
        std::cout << "  argv[" << i << "] = " << argv[i] << std::endl;
    }
    
    std::cout << "\nInitializing with command line arguments..." << std::endl;
    auto result = Initialize(argc, argv);
    
    if (result.HasValue()) {
        std::cout << "✓ Initialization with arguments succeeded!" << std::endl;
        
        // Clean up
        Deinitialize();
    } else {
        std::cerr << "✗ Initialization failed!" << std::endl;
        std::cerr << "  Error: " << result.Error().Message().data() << std::endl;
    }
}

void demonstrateErrorHandling() {
    printSeparator();
    std::cout << "=== Error Handling Example ===" << std::endl;
    
    // First initialization
    std::cout << "First initialization..." << std::endl;
    auto result1 = Initialize();
    if (result1.HasValue()) {
        std::cout << "✓ First initialization succeeded" << std::endl;
    }
    
    // Try to initialize again (should fail)
    std::cout << "\nTrying to initialize again (should fail)..." << std::endl;
    auto result2 = Initialize();
    if (!result2.HasValue()) {
        std::cout << "✓ Second initialization correctly failed" << std::endl;
        std::cout << "  Error code: " << result2.Error().Value() << std::endl;
        std::cout << "  Error message: " << result2.Error().Message().data() << std::endl;
        std::cout << "  Error domain: " << result2.Error().Domain().Name() << std::endl;
    } else {
        std::cerr << "✗ Second initialization should have failed!" << std::endl;
    }
    
    // Proper cleanup
    std::cout << "\nCleaning up..." << std::endl;
    Deinitialize();
}

void demonstrateLifecycle() {
    printSeparator();
    std::cout << "=== Complete Lifecycle Example ===" << std::endl;
    
    std::cout << "\n1. Initialize -> Use -> Deinitialize" << std::endl;
    
    auto init1 = Initialize();
    if (init1.HasValue()) {
        std::cout << "   ✓ Phase 1: Initialized" << std::endl;
        
        // Simulate some work
        std::cout << "   ✓ Phase 2: Working..." << std::endl;
        
        auto deinit1 = Deinitialize();
        if (deinit1.HasValue()) {
            std::cout << "   ✓ Phase 3: Deinitialized" << std::endl;
        }
    }
    
    std::cout << "\n2. Re-initialize after proper cleanup" << std::endl;
    auto init2 = Initialize();
    if (init2.HasValue()) {
        std::cout << "   ✓ Re-initialization succeeded!" << std::endl;
        Deinitialize();
    }
}

int main(int argc, char* argv[]) {
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  AUTOSAR Adaptive Platform Core Initialization Example    ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n";
    
    try {
        // Basic usage
        demonstrateBasicUsage();
        
        // Command line arguments
        if (argc > 1) {
            demonstrateCommandLineArgs(argc, argv);
        }
        
        // Error handling
        demonstrateErrorHandling();
        
        // Complete lifecycle
        demonstrateLifecycle();
        
        printSeparator();
        std::cout << "All examples completed successfully!" << std::endl;
        std::cout << "\nKey Takeaways:" << std::endl;
        std::cout << "  1. Always call Initialize() at application startup" << std::endl;
        std::cout << "  2. Always check the Result for errors" << std::endl;
        std::cout << "  3. Call Deinitialize() before application exit" << std::endl;
        std::cout << "  4. Don't call Initialize() multiple times without Deinitialize()" << std::endl;
        std::cout << "\n";
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Exception caught: " << e.what() << std::endl;
        return 1;
    }
}
