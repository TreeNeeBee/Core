#include "ipc/ChannelRegistry.hpp"
#include "ipc/SharedMemoryManager.hpp"
#include "ipc/WaitSetHelper.hpp"

namespace lap
{
namespace core
{
namespace ipc
{
    Result< UInt8 > ChannelRegistry::RegisterChannel( ControlBlock* ctrl, Bool isReadChannel, UInt8 channel_index ) noexcept
    {
        DEF_LAP_ASSERT( ctrl != nullptr, "ControlBlock pointer is null" );   
        if ( isReadChannel ) {
            return AllocateReadChannel( ctrl, channel_index ).AndThen( [&]( UInt8 index ) {
                return Result< UInt8 >( index );
            } ).OrElse( [&]( const ErrorCode& ec ) {
                return Result< UInt8 >::FromError( ec );
            } );
        } else {
            return AllocateWriteChannel( ctrl, channel_index ).AndThen( [&]( UInt8 index ) {
                return Result< UInt8 >( index );
            } ).OrElse( [&]( const ErrorCode& ec ) {
                return Result< UInt8 >::FromError( ec );
            } );
        }
    }

    Bool ChannelRegistry::UnregisterChannel( ControlBlock* ctrl, Bool isReadChannel, UInt8 channel_index ) noexcept
    {
        DEF_LAP_ASSERT( ctrl != nullptr, "ControlBlock pointer is null" );

        // Validate queue index
        if ( channel_index >= kMaxChannels ) {
            return false;
        }

        INNER_CORE_LOG( "ChannelRegistry::UnregisterChannel - channel Index: %u", channel_index );

        // Try to clear bit in ready_mask (CAS loop)
        if ( isReadChannel ) {
            while (true) {
                UInt64 expected_mask = ctrl->registry.read_mask.load(std::memory_order_acquire);
                
                // Check if not registered
                if ( !(expected_mask & (static_cast<UInt64>(1U) << channel_index)) ) {
                    return false;  // Not registered
                }
                
                UInt64 new_mask = expected_mask & ~(static_cast<UInt64>(1U) << channel_index);
                if ( ctrl->registry.read_mask.compare_exchange_weak(
                        expected_mask, new_mask,
                        std::memory_order_acq_rel, std::memory_order_acquire) ) {
                    break;  // Successfully cleared bit
                }
            }
            ctrl->registry.read_seq.fetch_add( 1, std::memory_order_acq_rel );
            WaitSetHelper::FutexWake( &ctrl->registry.read_seq, INT32_MAX );
        } else {
            while (true) {
                UInt64 expected_mask = ctrl->registry.write_mask.load(std::memory_order_acquire);
                
                // Check if not registered
                if ( !(expected_mask & (static_cast<UInt64>(1U) << channel_index)) ) {
                    return false;  // Not registered
                }
                
                UInt64 new_mask = expected_mask & ~(static_cast<UInt64>(1U) << channel_index);
                if ( ctrl->registry.write_mask.compare_exchange_weak(
                        expected_mask, new_mask,
                        std::memory_order_acq_rel, std::memory_order_acquire) ) {
                    break;  // Successfully cleared bit
                }
            }
            ctrl->registry.write_seq.fetch_add( 1, std::memory_order_acq_rel );
            WaitSetHelper::FutexWake( &ctrl->registry.write_seq, INT32_MAX );
        }

        // return RegenerateSnapshot( ctrl );
        return true;
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

    Result<UInt8> ChannelRegistry::AllocateReadChannel( ControlBlock* ctrl, UInt8 index ) noexcept
    {
        DEF_LAP_ASSERT( ctrl != nullptr, "ControlBlock pointer is null" );

        // Load current read mask
        UInt64 mask = ctrl->registry.read_mask.load(std::memory_order_acquire);
        
        // Check MPSC constraint: only one subscriber allowed
        if ( ( ctrl->GetIPCType() == IPCType::kMPSC ) && ( mask != 0 ) ) {
            return Result< UInt8 >( MakeErrorCode( CoreErrc::kIPCChannelAlreadyInUse ) );
        }

        // Determine target index
        UInt8 target_index = index;
        
        if ( index == 0xFF ) {
            // Auto-allocate: find first available slot
            UInt64 available_mask = ~mask;
            if ( available_mask == 0 ) {
                return Result< UInt8 >( MakeErrorCode( CoreErrc::kIPCInvalidChannelIndex ) );  // All slots occupied
            }
            
            target_index = static_cast< UInt8 >( __builtin_ctz( available_mask ) );
        } else {
            // Specific index requested: validate
            if ( index >= kMaxChannels ) {
                return Result< UInt8 >( MakeErrorCode( CoreErrc::kIPCInvalidChannelIndex ) );
            }
            
            UInt64 bit_mask = static_cast< UInt64 >( 1U ) << index;
            if ( mask & bit_mask ) {
                return Result< UInt8 >( MakeErrorCode( CoreErrc::kIPCChannelAlreadyInUse ) );
            }
        }

        // Try to set bit atomically (CAS loop protection)
        UInt64 bit_to_set = static_cast< UInt64 >( 1U ) << target_index;
        UInt64 old_mask = ctrl->registry.read_mask.fetch_or( bit_to_set, std::memory_order_acq_rel );
        
        if ( old_mask & bit_to_set ) {
            // Another thread set the bit first, retry needed
            return Result< UInt8 >( MakeErrorCode( CoreErrc::kIPCRetry ) );
        }
        
        // Increment sequence number
        ctrl->registry.read_seq.fetch_add( 1, std::memory_order_acq_rel );

        WaitSetHelper::FutexWake( &ctrl->registry.read_seq, INT32_MAX );

        INNER_CORE_LOG( "ChannelRegistry::AllocateReadChannel - Allocated read channel addr: %p, index: %u, read_seq: %d, mask: %ld\n", \
                            reinterpret_cast<void*>(ctrl),
                            target_index, 
                            ctrl->registry.read_seq.load(std::memory_order_acquire), 
                            ctrl->registry.read_mask.load(std::memory_order_acquire) );

        return Result< UInt8 >( target_index );
    }

    Result< UInt8 > ChannelRegistry::AllocateWriteChannel( ControlBlock* ctrl, UInt8 index ) noexcept
    {
        DEF_LAP_ASSERT( ctrl != nullptr, "ControlBlock pointer is null" );

        // Load current write mask
        UInt64 mask = ctrl->registry.write_mask.load(std::memory_order_acquire);
        
        // Check SPMC constraint: only one publisher allowed
        if ( ( ctrl->GetIPCType() == IPCType::kSPMC ) && ( mask != 0 ) ) {
            return Result< UInt8 >( MakeErrorCode( CoreErrc::kIPCChannelAlreadyInUse ) );
        }

        // Determine target index
        UInt8 target_index = index;
        if ( index == 0xFF ) {
            // Auto-allocate: find first available slot
            UInt64 available_mask = ~mask;
            if ( available_mask == 0 ) {
                return Result< UInt8 >( MakeErrorCode( CoreErrc::kIPCInvalidChannelIndex ) );  // All slots occupied
            }
            
            target_index = static_cast< UInt8 >( __builtin_ctz( available_mask ) );
        } else {
            // Specific index requested: validate
            if ( index >= kMaxChannels ) {
                return Result< UInt8 >( MakeErrorCode( CoreErrc::kIPCInvalidChannelIndex ) );
            }
            
            UInt64 bit_mask = static_cast< UInt64 >( 1U ) << index;
            if ( mask & bit_mask ) {
                return Result< UInt8 >( MakeErrorCode( CoreErrc::kIPCChannelAlreadyInUse ) );
            }
        }

        // Try to set bit atomically (CAS loop protection)
        UInt64 bit_to_set = static_cast< UInt64 >( 1U ) << target_index;
        UInt64 old_mask = ctrl->registry.write_mask.fetch_or( bit_to_set, std::memory_order_acq_rel );
        
        if ( old_mask & bit_to_set ) {
            // Another thread set the bit first, retry needed
            return Result< UInt8 >( MakeErrorCode( CoreErrc::kIPCRetry ) );
        }
        
        // Increment sequence number
        ctrl->registry.write_seq.fetch_add( 1, std::memory_order_acq_rel );

        WaitSetHelper::FutexWake( &ctrl->registry.write_seq, INT32_MAX );
     
        return Result< UInt8 >( target_index );
    }
}
}  // namespace core
}  // namespace lap