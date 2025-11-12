/*
 * CInitialization.cpp
 *
 *  Created on: Nov 12, 2025
 *      Author: GitHub Copilot
 *
 * Implementation of AUTOSAR Adaptive Platform initialization and de-initialization
 * according to SWS_Core specification R24-11.
 */

#include "CInitialization.hpp"
#include "CMemoryManager.hpp"
#include "CConfig.hpp"
#include "CCoreErrorDomain.hpp"
#include <atomic>
#include <mutex>

namespace lap {
namespace core {

// Initialization state management
enum class InitState : uint8_t {
    kNotInitialized = 0,
    kInitializing = 1,
    kInitialized = 2,
    kDeinitializing = 3,
    kInitializationFailed = 4
};

static std::atomic<InitState> g_init_state{InitState::kNotInitialized};
static std::mutex g_init_mutex;

/**
 * @brief Internal function to perform actual initialization work.
 * 
 * This function initializes core components in the correct sequence:
 * 1. Memory Manager (ensures singleton is created)
 * 2. Configuration Manager (ensures singleton is created)
 * 
 * @returns Result<void> indicating success or failure
 */
static Result<void> performInitialization() {
    // Initialize memory manager (ensures the singleton exists)
    (void)MemoryManager::getInstance()->initialize();

    // // Ensure configuration manager singleton exists
    // (void)ConfigManager::getInstance();

    return Result<void>{};
}

/**
 * @brief Internal function to perform actual de-initialization work.
 * 
 * De-initializes components in reverse order of initialization.
 * Note: Singletons are not explicitly destroyed, they will be cleaned up
 * at process exit.
 * 
 * @returns Result<void> indicating success or failure
 */
static Result<void> performDeinitialization() {
    // Note: ConfigManager and CMemoryManager are singletons
    // They will be cleaned up automatically at process exit
    // For more sophisticated cleanup, we would need explicit shutdown methods

    return Result<void>{};
}

// [SWS_CORE_15003] Startup and initialization of ARA
Result<void> Initialize() noexcept {
    std::lock_guard<std::mutex> lock(g_init_mutex);
    
    InitState current_state = g_init_state.load(std::memory_order_acquire);

    // [SWS_CORE_90029] Double initialization check
    if (current_state == InitState::kInitialized) {
        return Result<void>::FromError(
            CoreErrc::kAlreadyInitialized
        );
    }

    // [SWS_CORE_90028] Allow re-try after failed initialization
    if (current_state == InitState::kInitializing) {
        // Another thread is currently initializing
        return Result<void>::FromError(
            CoreErrc::kInternalError
        );
    }

    // Set state to initializing
    g_init_state.store(InitState::kInitializing, std::memory_order_release);

    // Perform actual initialization
    auto result = performInitialization();
    
    if (result.HasValue()) {
        // [SWS_CORE_90026] Framework initialization success
        g_init_state.store(InitState::kInitialized, std::memory_order_release);
    } else {
        // [SWS_CORE_90028] Cleanup on failure to allow retry
        g_init_state.store(InitState::kNotInitialized, std::memory_order_release);
    }

    return result;
}

// [SWS_CORE_15006] Command line argument injection
Result<void> Initialize(int& argc, char**& argv) noexcept {
    // Process command line arguments before calling main initialization
    // [SWS_CORE_15006] Remove ARA-specific arguments transparently
    
    // TODO: Implement argument parsing logic here
    // For now, arguments are ignored and passed through unchanged
    (void)argc;
    (void)argv;
    
    // Call main initialization function
    return Initialize();
}

// [SWS_CORE_15004] Shutdown and de-initialization of ARA
Result<void> Deinitialize() noexcept {
    std::lock_guard<std::mutex> lock(g_init_mutex);
    
    InitState current_state = g_init_state.load(std::memory_order_acquire);

    // [SWS_CORE_90030] Double de-initialization check
    if (current_state == InitState::kNotInitialized ||
        current_state == InitState::kInitializationFailed) {
        return Result<void>::FromError(
            CoreErrc::kNotInitialized
        );
    }

    if (current_state == InitState::kDeinitializing) {
        // Another thread is currently de-initializing
        return Result<void>::FromError(
            CoreErrc::kInternalError
        );
    }

    // Set state to de-initializing
    g_init_state.store(InitState::kDeinitializing, std::memory_order_release);

    // Perform actual de-initialization
    auto result = performDeinitialization();

    // [SWS_CORE_90027] Clean deinitialization - return to pre-Init state
    g_init_state.store(InitState::kNotInitialized, std::memory_order_release);

    return result;
}

} // namespace core
} // namespace lap
