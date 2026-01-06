/**
 * @file        MemoryPoolConfig.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       Compile-time configuration for memory pool allocator
 * @date        2025-12-26
 * @details     Defines memory pool parameters that should be configured at compile time
 *              instead of using runtime configuration files
 * @copyright   Copyright (c) 2025
 * @version     2.0
 */
#ifndef LAP_CORE_MEMORY_POOL_CONFIG_HPP
#define LAP_CORE_MEMORY_POOL_CONFIG_HPP

#include "CTypedef.hpp"

namespace lap
{
namespace core
{
namespace memory_config
{
    // ========================================================================
    // Compile-Time Memory Pool Configuration
    // ========================================================================
    
    // Enable/disable memory pool at compile time
    // This is controlled by CMake via LAP_ENABLE_MEMORY_POOL define
    // See CMakeLists.txt for build option
    
    // Memory alignment configuration (bytes)
    // Must be a power of 2: 4, 8, 16, 32, etc.
    // Default: 8 for 64-bit systems, 4 for 32-bit systems
    constexpr UInt32 MEMORY_ALIGNMENT = 8;
    
    // Enable memory tracking/checking
    // When enabled, MemoryTracker is initialized to track allocations
    // Can be disabled at runtime even if compiled in
    constexpr Bool ENABLE_MEMORY_TRACKING = true;
    
    // Maximum number of different pool sizes allowed
    constexpr UInt32 MAX_POOL_COUNT = 16;
    
    // Pool size constraints (bytes)
    constexpr UInt32 MIN_POOL_UNIT_SIZE = 8;       // Minimum unit size
    constexpr UInt32 MAX_POOL_UNIT_SIZE = 4096;    // Maximum unit size (larger uses mmap directly)
    
    // ========================================================================
    // Default Pool Configurations
    // ========================================================================
    
    /**
     * @brief Pool configuration entry
     * @details Defines a single memory pool with:
     *          - unitSize: Size of each memory unit in this pool (bytes)
     *          - initialCount: Initial number of units to pre-allocate
     *          - maxBlocks: Maximum total units allowed (0 = unlimited)
     *          - growthRate: Number of units to add when pool expands
     */
    struct PoolConfigEntry {
        UInt32 unitSize;        ///< Size of each unit (bytes)
        UInt32 initialCount;    ///< Initial number of units to allocate
        UInt32 maxBlocks;       ///< Maximum units (0 = unlimited)
        UInt32 growthRate;      ///< Units to add per expansion
    };
    
    // Define default pool configuration
    // These are compile-time defaults that can be overridden in build system
    // Adjust these values based on application needs:
    // - Small allocations: use smaller unit sizes with higher growth rates
    // - Large allocations: use larger unit sizes with capped limits
    
    constexpr PoolConfigEntry DEFAULT_POOL_CONFIGS[] = {
        // Pool 0: 8-byte allocations
        { 8,    200, 0, 100 },
        
        // Pool 1: 16-byte allocations
        { 16,   200, 0, 100 },
        
        // Pool 2: 24-byte allocations
        { 24,   150, 0, 80 },
        
        // Pool 3: 32-byte allocations
        { 32,   150, 0, 80 },
        
        // Pool 4: 64-byte allocations
        { 64,   100, 0, 50 },
        
        // Pool 5: 128-byte allocations
        { 128,  80,  0, 40 },
        
        // Pool 6: 256-byte allocations
        { 256,  60,  0, 30 },
        
        // Pool 7: 512-byte allocations
        { 512,  40,  0, 20 },
        
        // Pool 8: 1024-byte allocations (1 KB)
        { 1024, 30,  0, 15 },
        
        // Pool 9: 2048-byte allocations (2 KB)
        { 2048, 20,  0, 10 },
        
        // Pool 10: 4096-byte allocations (4 KB)
        { 4096, 15,  0, 8 },
    };
    
    // Number of default pool configurations
    constexpr size_t POOL_CONFIG_COUNT = sizeof(DEFAULT_POOL_CONFIGS) / sizeof(DEFAULT_POOL_CONFIGS[0]);
    
    // ========================================================================
    // Custom Configuration Guidelines
    // ========================================================================
    // To customize pool configuration:
    //
    // 1. Modify DEFAULT_POOL_CONFIGS array above with your desired sizes
    // 2. Ensure POOL_CONFIG_COUNT is correct (automatically calculated)
    // 3. Validate constraints:
    //    - All unitSize values must be >= MIN_POOL_UNIT_SIZE and <= MAX_POOL_UNIT_SIZE
    //    - unitSize values should generally be in ascending order for efficiency
    //    - initialCount * number of pools should not exceed available memory
    //
    // Example: For a cache-heavy application:
    //     { .unitSize = 512, .initialCount = 200, .maxBlocks = 2000, .growthRate = 100 }
    //
    // Example: For a memory-constrained system:
    //     { .unitSize = 256, .initialCount = 10,  .maxBlocks = 50,   .growthRate = 5 }

} // namespace memory_config
} // namespace core
} // namespace lap

#endif // LAP_CORE_MEMORY_POOL_CONFIG_HPP
