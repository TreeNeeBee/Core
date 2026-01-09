/**
 * @file        IPCTypes.hpp
 * @author      LightAP Core Team
 * @brief       IPC fundamental types and constants
 * @date        2026-01-07
 * @details     Based on iceoryx2 design - Zero-Copy Lock-Free IPC
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R24-11 compliant
 */
#ifndef LAP_CORE_IPC_TYPES_HPP
#define LAP_CORE_IPC_TYPES_HPP

#include "CTypedef.hpp"
#include <atomic>
#include <chrono>

namespace lap
{
namespace core
{
namespace ipc
{
    // ========================================================================
    // IPC Constants
    // ========================================================================
    
    /// Magic number for shared memory validation (ICE + version marker)
    constexpr UInt32 kIPCMagicNumber = 0xCE025250;
    
    /// IPC version number
    constexpr UInt32 kIPCVersion = 0x00010000;  // v1.0.0
    
    /// Shared memory alignment (2MB for huge pages)
    constexpr UInt64 kShmAlignment = 2 * 1024 * 1024;
    
    /// Cache line size for alignment
    constexpr UInt64 kCacheLineSize = 64;
    
    /// Page size
    constexpr UInt64 kPageSize = 4096;
    
    /// Memory region sizes (fixed partitions - optimized layout)
    constexpr UInt64 kControlBlockRegionSize = 0x20000;    ///< 128KB for ControlBlock
    constexpr UInt64 kQueueRegionSize = 0xC8000;           ///< 800KB for 100 queues (100 × 8KB)
    constexpr UInt64 kReservedRegionSize = 0x18000;        ///< 96KB reserved for future expansion
    constexpr UInt64 kSingleQueueSize = 8192;              ///< 8KB per queue (page aligned)
    constexpr UInt64 kQueueRegionOffset = kControlBlockRegionSize;  ///< Queue region starts at 128KB
    constexpr UInt64 kChunkPoolOffset = 0x100000;          ///< ChunkPool at 1MB (after ControlBlock + Queues + Reserved)
    
    /// Maximum subscribers per service
    constexpr UInt32 kMaxSubscribers = 100;
    
    /// Default queue capacity per subscriber
    constexpr UInt32 kDefaultQueueCapacity = 256;
    
    /// Default chunk pool size
    constexpr UInt32 kDefaultMaxChunks = 128;
    
    /// Default chunk size (4KB)
    constexpr UInt64 kDefaultChunkSize = 4096;
    
    /// Invalid chunk index
    constexpr UInt32 kInvalidChunkIndex = 0xFFFFFFFF;
    
    // ========================================================================
    // Chunk State Machine
    // ========================================================================
    /*
    ┌─────────┐
    │  kFree  │ ◄──────────────────────────┐
    └────┬────┘                            │
         │ Allocate()                      │ Deallocate()
         │                                 │
         ▼                                 │
    ┌─────────┐                       ┌────┴─────┐
    │ kLoaned │ ──── Send() ────────► │  kSent   │
    └────┬────┘                       └────┬─────┘
         │                                 │
         │ Release()                       │ Receive()
         │ (单播模式)                      │
         └────────────────────────┐        │
                                  │        ▼
                                  │   ┌──────────┐
                                  └──►│kReceived │
                                      └────┬─────┘
                                           │
                                           │ Release Sample
                                           │ (ref_count -> 0)
                                           ▼
                                      (回到 kFree)
    */
    enum class ChunkState : UInt32
    {
        kFree      = 0,  ///< Available in free list
        kLoaned    = 1,  ///< Loaned to Publisher
        kSent      = 2,  ///< Sent to Subscribers
        kReceived  = 3   ///< Received by Subscriber
    };
    
    // ========================================================================
    // Queue Policies
    // ========================================================================
    
    /// Policy when ChunkPool is exhausted
    enum class LoanFailurePolicy
    {
        kError,      ///< Return error immediately
        kWait,       ///< Busy-wait polling
        kBlock       ///< Block on futex (default)
    };
    
    /// Policy when subscriber queue is full
    enum class QueueFullPolicy
    {
        kOverwrite,  ///< Overwrite oldest message (Ring Buffer mode)
        kWait,       ///< Busy-wait polling
        kBlock,      ///< Block on futex
        kDrop        ///< Drop message (default)
    };
    
    /// Policy when subscriber queue is empty
    enum class QueueEmptyPolicy
    {
        kBlock,      ///< Block on futex (default)
        kWait,       ///< Busy-wait polling
        kSkip,       ///< Return immediately
        kError       ///< Return error
    };
    
    // ========================================================================
    // Event Flags (for WaitSet mechanism)
    // ========================================================================
    
    namespace EventFlag
    {
        constexpr UInt32 kHasData       = 0x01;  ///< Queue has data
        constexpr UInt32 kHasSpace      = 0x02;  ///< Queue has space
        constexpr UInt32 kHasFreeChunk  = 0x04;  ///< ChunkPool has free chunks
    }
    
    // ========================================================================
    // Duration Types
    // ========================================================================
    
    using Duration = std::chrono::nanoseconds;
    using TimePoint = std::chrono::steady_clock::time_point;
    
    // ========================================================================
    // Helper Functions
    // ========================================================================
    
    /**
     * @brief Align size to shared memory boundary (2MB)
     * @param size Size to align
     * @return Aligned size
     */
    inline constexpr UInt64 AlignToShmSize(UInt64 size) noexcept
    {
        return (size + kShmAlignment - 1) / kShmAlignment * kShmAlignment;
    }
    
    /**
     * @brief Align size to cache line boundary
     * @param size Size to align
     * @return Aligned size
     */
    inline constexpr UInt64 AlignToCacheLine(UInt64 size) noexcept
    {
        return (size + kCacheLineSize - 1) / kCacheLineSize * kCacheLineSize;
    }
    
    /**
     * @brief Convert ChunkState to string
     * @param state Chunk state
     * @return String representation
     */
    inline const char* ChunkStateToString(ChunkState state) noexcept
    {
        switch (state)
        {
            case ChunkState::kFree:     return "Free";
            case ChunkState::kLoaned:   return "Loaned";
            case ChunkState::kSent:     return "Sent";
            case ChunkState::kReceived: return "Received";
            default:                    return "Unknown";
        }
    }
    
}  // namespace ipc
}  // namespace core
}  // namespace lap

#endif  // LAP_CORE_IPC_TYPES_HPP
