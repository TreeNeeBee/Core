/**
 * @file        IPCConfig.hpp
 * @author      LightAP Core Team
 * @brief       IPC compile-time configuration macros
 * @date        2026-01-13
 * @details     Supports three memory footprint modes: shrink, normal, extend
 * @copyright   Copyright (c) 2026
 */
#ifndef LAP_CORE_IPC_CONFIG_HPP
#define LAP_CORE_IPC_CONFIG_HPP

#include "CTypedef.hpp"

namespace lap
{
namespace core
{
namespace ipc
{
    //=========================================================================
    // Memory Footprint Modes
    //=========================================================================
    
    // Mode selection (define ONE of the following)
    // #define LIGHTAP_IPC_MODE_SHRINK    1  // Ultra-compact: 2 subs, 128 queue, ~4KB total
    // #define LIGHTAP_IPC_MODE_NORMAL    1  // Balanced: 32 subs, 256 queue, ~100KB
    // #define LIGHTAP_IPC_MODE_EXTEND    1  // Extended: 32 subs, 1024 queue, ~400KB
    
    // Default to NORMAL if not specified
    #if !defined(LIGHTAP_IPC_MODE_SHRINK) && !defined(LIGHTAP_IPC_MODE_NORMAL) && !defined(LIGHTAP_IPC_MODE_EXTEND)
        #define LIGHTAP_IPC_MODE_NORMAL 1
    #endif
    
    //=========================================================================
    // Configuration Parameters
    //=========================================================================
    
    #if defined(LIGHTAP_IPC_MODE_SHRINK)
        //---------------------------------------------------------------------
        // SHRINK Mode: Ultra-compact for embedded systems
        //---------------------------------------------------------------------
        constexpr UInt32 kMaxSubscribers = 2;       ///< Max 2 subscribers
        constexpr UInt32 kQueueCapacity = 64;       ///< 64 slots per queue (fixed at 64)
   
        // Memory layout target: 4KB total
        // ControlBlock: 320B (5 cache lines: header + pool + registry + 2×64B snapshots)
        // SubscriberQueue[2]: 512B (2 × 256B)
        // ChunkPool: ~3KB (configurable chunk count)
        
    #elif defined(LIGHTAP_IPC_MODE_NORMAL)
        //---------------------------------------------------------------------
        // NORMAL Mode: High-throughput scenarios
        //---------------------------------------------------------------------
        constexpr UInt32 kMaxSubscribers = 30;      ///< Max 30 subscribers
        constexpr UInt32 kQueueCapacity = 256;      ///< 256 slots per queue
       
        // Memory layout target: ~400KB
        // ControlBlock: 576B
        // SubscriberQueue[32]: ~139KB (32 × 4352B)
        
    #else // LIGHTAP_IPC_MODE_EXTEND
        //---------------------------------------------------------------------
        // EXTEND Mode: Balanced design (default)
        //---------------------------------------------------------------------
        constexpr UInt32 kMaxSubscribers = 62;      ///< Max 62 subscribers
        constexpr UInt32 kQueueCapacity = 1024;     ///< 1024 slots per queue
 
        // Memory layout target: ~100KB
        // ControlBlock: 576B
        // SubscriberQueue[32]: 64KB (32 × 2048B)
        
    #endif
    
    //=========================================================================
    // Default Configuration Values
    //=========================================================================
#ifdef LIGHTAP_IPC_MODE_SHRINK
    constexpr UInt32 kDefaultMaxChunks = 64;        ///< Default maximum chunks
    constexpr UInt64 kDefaultChunkSize = 16;        ///< Default chunk size (16B)
#else       
    constexpr UInt32 kDefaultMaxChunks = 1024;      ///< Default maximum chunks
    constexpr UInt64 kDefaultChunkSize = 1024;      ///< Default chunk size (1KB)
#endif

    //=========================================================================
    // Mode Information
    //=========================================================================
    
    inline const char* GetIPCModeName() noexcept
    {
        #if defined(LIGHTAP_IPC_MODE_SHRINK)
            return "SHRINK";
        #elif defined(LIGHTAP_IPC_MODE_EXTEND)
            return "EXTEND";
        #else
            return "NORMAL";
        #endif
    }
}  // namespace ipc
}  // namespace core
}  // namespace lap

#endif  // LAP_CORE_IPC_CONFIG_HPP
