/**
 * @file        ChannelRegistry.hpp
 * @author      LightAP Core Team
 * @brief       Lock-free operations for ChannelRegistry in shared memory
 * @date        2026-01-13
 * @details     Implements double-buffered snapshot mechanism (configurable subscriber count)
 * @copyright   Copyright (c) 2026
 * @note        Based on iceoryx2 lock-free registry design
 */
#ifndef LAP_CORE_IPC_CHANNEL_REGISTRY_OPS_HPP
#define LAP_CORE_IPC_CHANNEL_REGISTRY_OPS_HPP

#include "ControlBlock.hpp"
#include "CCoreErrorDomain.hpp"
#include "CResult.hpp"
#include <atomic>

namespace lap
{
namespace core
{
namespace ipc
{
    // Forward declaration
    class SharedMemoryManager;
    
    class ChannelRegistry final
    {
    public:
        static Result< UInt8 > RegisterReadChannel( ControlBlock* ctrl, UInt8 index = 0xFF ) noexcept
        {
            return RegisterChannel( ctrl, true, index );
        }

        static Bool UnregisterReadChannel( ControlBlock* ctrl, UInt8 index ) noexcept
        {
            return UnregisterChannel( ctrl, true, index );
        }

        static Result< UInt8 > RegisterWriteChannel( ControlBlock* ctrl, UInt8 index = 0xFF ) noexcept
        {
            return RegisterChannel( ctrl, false, index );
        }

        static Bool UnregisterWriteChannel( ControlBlock* ctrl, UInt8 index ) noexcept
        {
            return UnregisterChannel( ctrl, false, index );
        }
   
        /**
        * @brief Mark channel as active
        * @param ctrl Pointer to ControlBlock in shared memory
        * @param index Queue index to activate
        * @return true if successful, false if invalid index
        */
        static Bool ActiveChannel( SharedMemoryManager* shm, UInt8 index ) noexcept;

        /**
        * @brief Mark channel as inactive
        * @param ctrl Pointer to ControlBlock in shared memory
        * @param index Queue index to deactivate
        * @return true if successful, false if invalid index
        */
        static Bool DeactiveChannel( SharedMemoryManager* shm, UInt8 index ) noexcept;
    
    protected:
        ChannelRegistry() = delete;
        ~ChannelRegistry() = default;

        ChannelRegistry(const ChannelRegistry&) = delete;
        ChannelRegistry& operator=(const ChannelRegistry&) = delete;
        ChannelRegistry(ChannelRegistry&&) = delete;
        ChannelRegistry& operator=(ChannelRegistry&&) = delete;

    private:
        /**
        * @brief Allocate a unique queue index for new publisher
        * @param ctrl Pointer to ControlBlock in shared memory
        * @return Unique queue index or kInvalidChunkIndex if full
        * 
        * @details
        * - Finds first available slot from ready_mask using bit operations
        * - Returns kInvalidChunkIndex if all slots occupied (kMaxChannels)
        */
        static Result< UInt8 > AllocateReadChannel( ControlBlock* ctrl, UInt8 index = 0xFF ) noexcept;
    
        /**
        * @brief Allocate a unique queue index for new subscriber
        * @param ctrl Pointer to ControlBlock in shared memory
        * @return Unique queue index or kInvalidChunkIndex if full
        * 
        * @details
        * - Finds first available slot from ready_mask using bit operations
        * - Returns kInvalidChunkIndex if all slots occupied (kMaxChannels)
        */
        static Result< UInt8 > AllocateWriteChannel( ControlBlock* ctrl, UInt8 index = 0xFF ) noexcept;

        /**
        * @brief Register a new subscriber queue
        * @param ctrl Pointer to ControlBlock in shared memory
        * @param index Queue index to register
        * @return true if registered successfully, false if full or already exists
        * 
        * @details
        * - Uses bitmask for O(1) registration
        * - Regenerates snapshot from ready_mask
        * - Lock-free with CAS on ready_mask
        */
        static Result< UInt8 > RegisterChannel( ControlBlock* ctrl, Bool isReadChannel, UInt8 index = 0xFF ) noexcept;
        
        /**
        * @brief Unregister a subscriber queue
        * @param ctrl Pointer to ControlBlock in shared memory
        * @param index Queue index to unregister
        * @return true if unregistered successfully, false if not found
        * 
        * @details
        * - Uses bitmask for O(1) unregistration
        * - Regenerates snapshot from ready_mask
        * - Lock-free with CAS on ready_mask
        */
        static Bool UnregisterChannel( ControlBlock* ctrl, Bool isReadChannel, UInt8 index ) noexcept;
    };
}  // namespace ipc
}  // namespace core
}  // namespace lap

#endif  // LAP_CORE_IPC_SUBSCRIBER_REGISTRY_OPS_HPP
