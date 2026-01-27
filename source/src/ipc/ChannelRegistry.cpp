#include "ipc/ChannelRegistry.hpp"
#include "ipc/SharedMemoryManager.hpp"

namespace lap
{
namespace core
{
namespace ipc
{
    Result< UInt8 > ChannelRegistry::RegisterChannel( ControlBlock* ctrl, UInt8 channel_index ) noexcept
    {
        DEF_LAP_ASSERT( ctrl != nullptr, "ControlBlock pointer is null" );
   
        auto result = AllocateQueueIndex( ctrl, channel_index );
        
        if ( result ) {
            channel_index = result.Value();
        } else {
            INNER_CORE_LOG( "ChannelRegistry::RegisterChannel - Failed to allocate queue index %s", result.Error().Message().data() );
            return Result< UInt8 >::FromError( result.Error() );  // Allocation failed
        }
        
        // Validate queue index
        if ( channel_index >= kMaxChannels ) {
            return Result< UInt8 >( MakeErrorCode( CoreErrc::kIPCInvalidChannelIndex ) );
        }

        INNER_CORE_LOG( "ChannelRegistry::RegisterChannel - channel Index: %u", channel_index );

        RegenerateSnapshot( ctrl );

        return Result< UInt8 >( channel_index );
    }

    Bool ChannelRegistry::UnregisterChannel( ControlBlock* ctrl, UInt8 channel_index ) noexcept
    {
        DEF_LAP_ASSERT( ctrl != nullptr, "ControlBlock pointer is null" );

        // Validate queue index
        if ( channel_index >= kMaxChannels ) {
            return false;
        }

        INNER_CORE_LOG( "ChannelRegistry::UnregisterChannel - channel Index: %u", channel_index );

        // Try to clear bit in ready_mask (CAS loop)
        while (true) {
            UInt32 expected_mask = ctrl->registry.ready_mask.load(std::memory_order_acquire);
            
            // Check if not registered
            if ( !(expected_mask & (static_cast<UInt32>(1U) << channel_index)) ) {
                return false;  // Not registered
            }
            
            UInt32 new_mask = expected_mask & ~(static_cast<UInt32>(1U) << channel_index);
            if ( ctrl->registry.ready_mask.compare_exchange_weak(
                    expected_mask, new_mask,
                    std::memory_order_acq_rel, std::memory_order_acquire) ) {
                break;  // Successfully cleared bit
            }
        }

        return RegenerateSnapshot( ctrl );
    }

    Bool ChannelRegistry::ActiveChannel( SharedMemoryManager* shm, UInt8 index ) noexcept
    {
        DEF_LAP_ASSERT( shm != nullptr, "SharedMemoryManager pointer is null" );
        DEF_LAP_ASSERT( index < kMaxChannels, "Queue index out of range" );
        
        shm->GetChannelQueue( index )->active.store( true, std::memory_order_release );
        return true;
    }

    Bool ChannelRegistry::DeactiveChannel( SharedMemoryManager* shm, UInt8 index ) noexcept
    {
        DEF_LAP_ASSERT( shm != nullptr, "SharedMemoryManager pointer is null" );
        DEF_LAP_ASSERT( index < kMaxChannels, "Queue index out of range" );

        shm->GetChannelQueue( index )->active.store( false, std::memory_order_release );
        return true;
    }

    Result<UInt8> ChannelRegistry::AllocateQueueIndex( ControlBlock* ctrl, UInt8 index ) noexcept
    {
        UInt32 mask = ctrl->registry.ready_mask.load(std::memory_order_acquire);
        
        if ( index != 0xFF ) {
            // Specific index requested, check if available
            if ( index >= kMaxChannels ) {
                return Result< UInt8 >( MakeErrorCode( CoreErrc::kIPCInvalidChannelIndex ) );  // Already occupied
            }

            UInt32 bitSet = static_cast< UInt32 >( 1U ) << index;
            if ( mask & bitSet ) {
                return Result< UInt8 >( MakeErrorCode( CoreErrc::kIPCChannelAlreadyInUse ) );  // Already occupied
            }
            
            UInt32 old = ctrl->registry.ready_mask.fetch_or( bitSet, std::memory_order_acq_rel );
            if ( old & bitSet ) {
                // Another thread set the bit first
                return Result< UInt8 >( MakeErrorCode( CoreErrc::kIPCChannelAlreadyInUse ) );
            }
            
            return Result< UInt8 >( index );
        } else {
            // Find first unset bit (invert mask to find 0s as 1s)
            UInt32 m = ~mask;
            if ( m == 0 ) {
                return Result< UInt8 >( MakeErrorCode( CoreErrc::kIPCInvalidChannelIndex ) );  // All slots occupied
            }

            int idx = __builtin_ctz( m );  // UInt32 version

            UInt32 bitSet = static_cast< UInt32 >( 1U ) << idx;
            UInt32 old = ctrl->registry.ready_mask.fetch_or( bitSet, std::memory_order_acq_rel );
            if ( old & bitSet ) {
                // Another thread set the bit first, retry
                return Result< UInt8 >( MakeErrorCode( CoreErrc::kIPCRetry ) );
            }

            return Result< UInt8 >( idx );
        }
    }

    Bool ChannelRegistry::RegenerateSnapshot( ControlBlock* ctrl ) noexcept
    {
        DEF_LAP_ASSERT( ctrl != nullptr, "ControlBlock pointer is null" );

        // Get inactive buffer as write buffer
        UInt32 active_idx = ctrl->registry.active_index.load(std::memory_order_acquire);
        UInt32 write_idx = 1 - active_idx;
        auto write_snap = ctrl->registry.GetSnapshot(write_idx);
        
        // Regenerate snapshot from ready_mask (optimized bit traversal)
        UInt8 count = 0;
        UInt32 m = ctrl->registry.ready_mask.load(std::memory_order_acquire);
        while ( m ) {
            int idx = __builtin_ctz( m );  // Count trailing zeros
            write_snap[ count++ ] = static_cast< UInt8 >( idx );
            m &= m - 1;  // Clear lowest set bit
        }
        ctrl->registry.active_count[write_idx] = count;

        // Switch active snapshot to write buffer
        ctrl->registry.active_index.store( write_idx, std::memory_order_release );

        return true;
    }
}
}  // namespace core
}  // namespace lap