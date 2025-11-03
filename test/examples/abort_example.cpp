/**
 * @file        abort_example.cpp
 * @brief       Example usage of AUTOSAR AP compliant Abort functionality
 * @date        2025-11-03
 * @details     Demonstrates various use cases for abort and signal handling
 */

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include "CAbort.hpp"

using namespace lap::core;

// ============================================================================
// Example 1: Basic Abort Handler
// ============================================================================

void MyAbortHandler() noexcept {
    std::cerr << "[MyAbortHandler] Application is aborting!\n";
    std::cerr << "[MyAbortHandler] Performing cleanup...\n";
    
    // Perform critical cleanup here
    // Note: Must be async-signal-safe operations only
    
    std::cerr << "[MyAbortHandler] Cleanup complete, proceeding with abort.\n";
}

void Example1_BasicAbortHandler() {
    std::cout << "\n=== Example 1: Basic Abort Handler ===\n";
    
    // Install custom abort handler
    auto previous = SetAbortHandler(&MyAbortHandler);
    std::cout << "Installed custom abort handler\n";
    
    // Simulate critical error (commented out to allow other examples to run)
    // std::cout << "Triggering abort...\n";
    // Abort("Critical resource allocation failure");
    
    // Restore previous handler
    SetAbortHandler(previous);
    std::cout << "Restored previous abort handler\n";
}

// ============================================================================
// Example 2: Querying Current Handler
// ============================================================================

void Example2_QueryHandler() {
    std::cout << "\n=== Example 2: Query Current Handler ===\n";
    
    auto current = GetAbortHandler();
    if (current == nullptr) {
        std::cout << "No custom abort handler installed (using default)\n";
    } else {
        std::cout << "Custom abort handler is installed at: " 
                  << reinterpret_cast<void*>(current) << "\n";
    }
    
    SetAbortHandler(&MyAbortHandler);
    current = GetAbortHandler();
    std::cout << "After SetAbortHandler, handler is at: " 
              << reinterpret_cast<void*>(current) << "\n";
    
    SetAbortHandler(nullptr);
}

// ============================================================================
// Example 3: Signal Handling
// ============================================================================

void MySIGTERMHandler() noexcept {
    std::cerr << "[MySIGTERMHandler] Received SIGTERM signal\n";
    std::cerr << "[MySIGTERMHandler] Performing graceful shutdown...\n";
}

void MySIGINTHandler() noexcept {
    std::cerr << "[MySIGINTHandler] Received SIGINT signal (Ctrl+C)\n";
    std::cerr << "[MySIGINTHandler] Application interrupted by user\n";
}

void Example3_SignalHandling() {
    std::cout << "\n=== Example 3: Signal Handling ===\n";
    
    // Register the default signal handler
    std::cout << "Registering signal handlers...\n";
    RegisterSignalHandler();
    
    // Install custom handlers for specific signals
    std::cout << "Installing custom SIGTERM handler\n";
    SetSignalSIGTERMHandler(&MySIGTERMHandler);
    
    std::cout << "Installing custom SIGINT handler\n";
    SetSignalSIGINTHandler(&MySIGINTHandler);
    
    // Check if handlers are registered
    std::cout << "SIGTERM handler registered: " 
              << (IsSignalHandlerRegistered(SIGTERM) ? "Yes" : "No") << "\n";
    std::cout << "SIGINT handler registered: " 
              << (IsSignalHandlerRegistered(SIGINT) ? "Yes" : "No") << "\n";
    std::cout << "SIGHUP handler registered: " 
              << (IsSignalHandlerRegistered(SIGHUP) ? "Yes" : "No") << "\n";
    
    // Simulate receiving a signal (commented out)
    // std::cout << "Raising SIGTERM...\n";
    // std::raise(SIGTERM);
    
    // Cleanup
    std::cout << "Unregistering signal handlers\n";
    UnregisterSignalHandlers();
}

// ============================================================================
// Example 4: Signal Name Lookup
// ============================================================================

void Example4_SignalNames() {
    std::cout << "\n=== Example 4: Signal Name Lookup ===\n";
    
    int signals[] = {SIGHUP, SIGINT, SIGQUIT, SIGABRT, SIGFPE, SIGILL, SIGSEGV, SIGTERM};
    
    std::cout << "Standard signal names:\n";
    for (int sig : signals) {
        std::cout << "  Signal " << sig << ": " << GetSignalName(sig) << "\n";
    }
    
    std::cout << "Unknown signal: " << GetSignalName(9999) << "\n";
}

// ============================================================================
// Example 5: Thread-Safe Handler Management
// ============================================================================

void ThreadFunction(int id) {
    for (int i = 0; i < 5; ++i) {
        // Install and remove handler multiple times
        SetAbortHandler(&MyAbortHandler);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        auto current = GetAbortHandler();
        if (current != nullptr) {
            std::cout << "Thread " << id << " iteration " << i 
                      << ": Handler is installed\n";
        }
        
        SetAbortHandler(nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void Example5_ThreadSafety() {
    std::cout << "\n=== Example 5: Thread-Safe Handler Management ===\n";
    
    constexpr int NUM_THREADS = 3;
    std::thread threads[NUM_THREADS];
    
    std::cout << "Starting " << NUM_THREADS << " threads...\n";
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads[i] = std::thread(ThreadFunction, i);
    }
    
    std::cout << "Waiting for threads to complete...\n";
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads[i].join();
    }
    
    std::cout << "All threads completed successfully\n";
}

// ============================================================================
// Example 6: RAII-style Handler Management
// ============================================================================

class ScopedAbortHandler {
public:
    explicit ScopedAbortHandler(AbortHandler handler) 
        : previous_(SetAbortHandler(handler)) {
        std::cout << "[ScopedAbortHandler] Installed handler\n";
    }
    
    ~ScopedAbortHandler() {
        SetAbortHandler(previous_);
        std::cout << "[ScopedAbortHandler] Restored previous handler\n";
    }
    
    // Prevent copying
    ScopedAbortHandler(const ScopedAbortHandler&) = delete;
    ScopedAbortHandler& operator=(const ScopedAbortHandler&) = delete;
    
private:
    AbortHandler previous_;
};

void Example6_RAIIHandler() {
    std::cout << "\n=== Example 6: RAII-style Handler Management ===\n";
    
    std::cout << "Entering scoped block...\n";
    {
        ScopedAbortHandler scoped(&MyAbortHandler);
        std::cout << "Inside scoped block, handler is active\n";
        
        auto current = GetAbortHandler();
        std::cout << "Current handler: " << reinterpret_cast<void*>(current) << "\n";
        
        // Handler will be automatically restored when leaving scope
    }
    std::cout << "Exited scoped block, handler restored\n";
    
    auto current = GetAbortHandler();
    std::cout << "Current handler after scope: " 
              << (current == nullptr ? "nullptr" : "installed") << "\n";
}

// ============================================================================
// Example 7: Comprehensive Application Template
// ============================================================================

class Application {
public:
    Application() {
        std::cout << "[Application] Initializing...\n";
        
        // Install abort handler
        SetAbortHandler(&Application::AbortHandler);
        
        // Register signal handlers
        RegisterSignalHandler();
        SetSignalSIGTERMHandler(&Application::TerminateHandler);
        SetSignalSIGINTHandler(&Application::InterruptHandler);
        
        std::cout << "[Application] Initialization complete\n";
    }
    
    ~Application() {
        std::cout << "[Application] Shutting down...\n";
        
        // Cleanup
        UnregisterSignalHandlers();
        SetAbortHandler(nullptr);
        
        std::cout << "[Application] Shutdown complete\n";
    }
    
    void Run() {
        std::cout << "[Application] Running...\n";
        std::cout << "[Application] Press Ctrl+C to interrupt (signal handling is active)\n";
        std::cout << "[Application] Or wait 3 seconds for normal completion\n";
        
        // Simulate work
        for (int i = 0; i < 3; ++i) {
            std::cout << "[Application] Working... " << (i + 1) << "/3\n";
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        std::cout << "[Application] Work complete\n";
    }
    
private:
    static void AbortHandler() noexcept {
        std::cerr << "[Application::AbortHandler] Fatal error, aborting!\n";
    }
    
    static void TerminateHandler() noexcept {
        std::cerr << "[Application::TerminateHandler] Termination requested\n";
    }
    
    static void InterruptHandler() noexcept {
        std::cerr << "[Application::InterruptHandler] Interrupted by user\n";
    }
};

void Example7_ApplicationTemplate() {
    std::cout << "\n=== Example 7: Comprehensive Application Template ===\n";
    
    Application app;
    app.Run();
}

// ============================================================================
// Main Function
// ============================================================================

int main() {
    std::cout << "========================================\n";
    std::cout << "AUTOSAR AP Abort Functionality Examples\n";
    std::cout << "========================================\n";
    
    try {
        Example1_BasicAbortHandler();
        Example2_QueryHandler();
        Example3_SignalHandling();
        Example4_SignalNames();
        Example5_ThreadSafety();
        Example6_RAIIHandler();
        Example7_ApplicationTemplate();
        
        std::cout << "\n========================================\n";
        std::cout << "All examples completed successfully!\n";
        std::cout << "========================================\n";
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Exception caught: " << e.what() << "\n";
        return 1;
    }
}
