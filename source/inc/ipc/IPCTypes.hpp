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
    constexpr UInt16 kIPCVersion = 0x0101;  // v1.0.1
    
    /// Invalid channel index
    constexpr UInt8 kInvalidChannelID = 0xFF;

    /// Invalid chunk index
    constexpr UInt16 kInvalidChunkIndex = 0xFFFF;

    //=========================================================================
    // Default Configuration Values
    //=========================================================================    
    constexpr UInt32 kMaxChannels           = 30;           ///< Max 30 subscribers
    constexpr UInt32 kMaxChannelCapacity    = 256;        ///< 256 slots per channel

    constexpr UInt16 kDefaultChunks         = 1024;         ///< Default maximum chunks
    constexpr UInt32 kDefaultChunkSize      = 1024;         ///< Default chunk size (1KB)

    enum class IPCType : UInt8
    {
        kNone       = 0,    ///< Undefined / placeholder
        kSPMC       = 1,    ///< Sent to multiple Subscribers
        kMPSC       = 2,    ///< Received by multiple Publishers
        kMPMC       = 3     ///< Multiple Publishers and Subscribers
    };
    
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
    enum class ChunkState : UInt8
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
    enum class LoanPolicy
    {
        kBlock,     ///< Block on futex (default)
        kWait,      ///< Busy-wait polling 
        kError      ///< Return error immediately
    };
    
    /// Policy when subscriber queue is full
    enum class PublishPolicy
    {
        kOverwrite, ///< Overwrite oldest message (default)
        kBlock,     ///< Block on futex
        kWait,      ///< Busy-wait polling
        kDrop,      ///< Drop message
        kError      ///< Return error immediately
    };
    
    /// Policy when subscriber queue is empty
    enum class SubscribePolicy
    {
        kBlock,      ///< Block on futex (default)
        kWait,       ///< Busy-wait polling
        kSkip,       ///< Return immediately
        kError       ///< Return error
    };
    
    // ========================================================================
    // QoS (Quality of Service) Policies
    // ========================================================================
    
    /// Reliability policy - message delivery guarantee
    enum class ReliabilityPolicy : UInt8
    {
        kBestEffort = 0,    ///< No guarantee, fast (default for SPMC)
        kReliable   = 1     ///< Guaranteed delivery, may block/retry
    };
    
    /// History policy - how many samples to keep
    enum class HistoryPolicy : UInt8
    {
        kKeepLast   = 0,    ///< Keep last N samples (default)
        kKeepAll    = 1     ///< Keep all samples until consumed
    };
    
    /// Durability policy - data lifecycle
    enum class DurabilityPolicy : UInt8
    {
        kVolatile       = 0,    ///< Data discarded when no subscribers (default)
        kTransientLocal = 1,    ///< Late-joining subscribers get last N samples
        kTransient      = 2,    ///< Persisted beyond process lifetime
        kPersistent     = 3     ///< Fully persistent (requires external storage)
    };
    
    /// Deadline policy - maximum time between messages
    struct DeadlinePolicy
    {
        UInt64 period_ns;       ///< Period in nanoseconds (0 = infinite)
        
        constexpr DeadlinePolicy() noexcept : period_ns(0) {}
        constexpr explicit DeadlinePolicy(UInt64 ns) noexcept : period_ns(ns) {}
        
        static constexpr DeadlinePolicy Infinite() noexcept { return DeadlinePolicy(0); }
        static constexpr DeadlinePolicy FromMilliseconds(UInt32 ms) noexcept 
        { 
            return DeadlinePolicy(static_cast<UInt64>(ms) * 1000000ULL); 
        }
    };
    
    /// Liveliness policy - entity liveness detection
    enum class LivelinessPolicy : UInt8
    {
        kAutomatic      = 0,    ///< Infrastructure automatically asserts liveliness (default)
        kManualByTopic  = 1,    ///< Application manually asserts per topic
        kManualByEntity = 2     ///< Application manually asserts per entity
    };
    
    /// Priority policy - message priority level
    enum class PriorityPolicy : UInt8
    {
        kLowest     = 0,
        kLow        = 1,
        kNormal     = 2,    ///< Default priority
        kHigh       = 3,
        kHighest    = 4,
        kRealtime   = 5     ///< Critical real-time priority
    };
    
    /// Ownership policy - single vs multiple writers
    enum class OwnershipPolicy : UInt8
    {
        kShared     = 0,    ///< Multiple publishers allowed (default)
        kExclusive  = 1     ///< Only one publisher with highest strength
    };
    
    /**
     * @brief Complete QoS configuration
     * @details Aggregates all QoS policies for Publisher/Subscriber
     */
    struct QoSProfile
    {
        ReliabilityPolicy   reliability;    ///< Delivery guarantee
        HistoryPolicy       history;        ///< Sample retention
        UInt16              history_depth;  ///< Depth for kKeepLast (default: 1)
        DurabilityPolicy    durability;     ///< Data lifecycle
        DeadlinePolicy      deadline;       ///< Timing constraint
        LivelinessPolicy    liveliness;     ///< Liveness detection
        UInt64              liveliness_lease_duration_ns;  ///< Lease duration
        PriorityPolicy      priority;       ///< Message priority
        OwnershipPolicy     ownership;      ///< Writer ownership
        UInt8               ownership_strength;  ///< Strength for exclusive ownership
        
        /// Default QoS profile (best-effort, volatile)
        static constexpr QoSProfile Default() noexcept
        {
            return QoSProfile{
                ReliabilityPolicy::kBestEffort,
                HistoryPolicy::kKeepLast,
                1,  // history_depth
                DurabilityPolicy::kVolatile,
                DeadlinePolicy::Infinite(),
                LivelinessPolicy::kAutomatic,
                0,  // liveliness_lease_duration_ns (infinite)
                PriorityPolicy::kNormal,
                OwnershipPolicy::kShared,
                0   // ownership_strength
            };
        }
        
        /// Reliable QoS profile (guaranteed delivery)
        static constexpr QoSProfile Reliable() noexcept
        {
            return QoSProfile{
                ReliabilityPolicy::kReliable,
                HistoryPolicy::kKeepLast,
                10,  // history_depth
                DurabilityPolicy::kTransientLocal,
                DeadlinePolicy::Infinite(),
                LivelinessPolicy::kAutomatic,
                0,
                PriorityPolicy::kNormal,
                OwnershipPolicy::kShared,
                0
            };
        }
        
        /// Real-time QoS profile (low latency, best-effort)
        static constexpr QoSProfile Realtime() noexcept
        {
            return QoSProfile{
                ReliabilityPolicy::kBestEffort,
                HistoryPolicy::kKeepLast,
                1,  // history_depth
                DurabilityPolicy::kVolatile,
                DeadlinePolicy::FromMilliseconds(10),  // 10ms deadline
                LivelinessPolicy::kAutomatic,
                10000000,  // 10ms liveliness lease
                PriorityPolicy::kRealtime,
                OwnershipPolicy::kShared,
                0
            };
        }
    };
    
    // ========================================================================
    // Event Flags (for WaitSet mechanism)
    // ========================================================================
    
    namespace EventFlag
    {
        constexpr UInt32 kNone          = 0x00;  ///< No event
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
