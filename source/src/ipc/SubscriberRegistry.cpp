#include "ipc/SubscriberRegistry.hpp"

namespace lap
{
namespace core
{
namespace ipc
{
    Result<UInt32> SubscriberRegistry::AllocateQueueIndex(ControlBlock* ctrl) noexcept
    {
        #if defined(LIGHTAP_IPC_MODE_SHRINK)
            using MaskType = UInt8;
        #elif defined(LIGHTAP_IPC_MODE_NORMAL)
            using MaskType = UInt32;
        #else // EXTEND
            using MaskType = UInt64;
        #endif
        
        MaskType mask = ctrl->registry.registry_control.ready_mask.load(std::memory_order_acquire);
        
        // Find first unset bit (invert mask to find 0s as 1s)
        MaskType m = ~mask;
        if ( m == 0 ) {
            return Result<UInt32>( MakeErrorCode( CoreErrc::kIPCInvalidQueueIndex ) );  // All slots occupied
        }
        
        // Count trailing zeros to find first available slot
        #if defined(LIGHTAP_IPC_MODE_SHRINK)
            int idx = __builtin_ctz(m);  // UInt8 still uses 32-bit ctz
        #elif defined(LIGHTAP_IPC_MODE_NORMAL)
            int idx = __builtin_ctz(m);  // UInt32 version
        #else // EXTEND
            int idx = __builtin_ctzll(m);  // UInt64 version
        #endif

        MaskType new_mask = mask | (static_cast<MaskType>(1U) << idx);
        if ( ctrl->registry.registry_control.ready_mask.compare_exchange_weak(
                    mask, new_mask,
                    std::memory_order_acq_rel, std::memory_order_acquire) ) {
            return Result<UInt32>( static_cast<UInt32>(idx) );
        }

        return Result<UInt32>( MakeErrorCode( CoreErrc::kIPCRetry ) );
    }

    Bool SubscriberRegistry::RegisterSubscriber(ControlBlock* ctrl, UInt32 queue_index) noexcept
    {
        INNER_CORE_LOG("SubscriberRegistry::RegisterSubscriber - Queue Index: %u", queue_index);
        // Validate queue index
        if ( queue_index >= kMaxSubscribers ) {
            return false;
        }
        
        // Get inactive buffer as write buffer
        UInt32 active_idx = ctrl->registry.registry_control.active_snapshot_index.load(std::memory_order_acquire);
        UInt32 write_idx = 1 - active_idx;
        ControlBlock::SubscriberSnapshot* write_snap = &ctrl->registry.snapshots[write_idx];
        
        // Regenerate snapshot from ready_mask (optimized bit traversal)
        write_snap->count = 0;
#if defined(LIGHTAP_IPC_MODE_SHRINK)
        UInt8 m = ctrl->registry.registry_control.ready_mask.load(std::memory_order_acquire);
        while ( m ) {
            int idx = __builtin_ctz( m );  // Count trailing zeros
            write_snap->queue_indices[ write_snap->count++ ] = static_cast< UInt32 >( idx );
            m &= m - 1;  // Clear lowest set bit
        }
#elif defined(LIGHTAP_IPC_MODE_NORMAL)
        UInt32 m = ctrl->registry.registry_control.ready_mask.load(std::memory_order_acquire);
        while ( m ) {
            int idx = __builtin_ctz( m );  // Count trailing zeros
            write_snap->queue_indices[ write_snap->count++ ] = static_cast< UInt32 >( idx );
            m &= m - 1;  // Clear lowest set bit
        }
#else // EXTEND
        UInt64 m = ctrl->registry.registry_control.ready_mask.load(std::memory_order_acquire);
        while ( m ) {
            int idx = __builtin_ctzll( m );  // Count trailing zeros (64-bit)
            write_snap->queue_indices[ write_snap->count++ ] = static_cast< UInt32 >( idx );
            m &= m - 1;  // Clear lowest set bit
        }
#endif
        //write_snap->version++;
        
        // Switch active snapshot to write buffer
        ctrl->registry.registry_control.active_snapshot_index.store( write_idx, std::memory_order_release );
        
        // Update subscriber count
        ctrl->registry.registry_control.subscriber_count.fetch_add( 1, std::memory_order_release );
        
        return true;
    }

    Bool SubscriberRegistry::UnregisterSubscriber(ControlBlock* ctrl, UInt32 queue_index) noexcept
    {
        // Validate queue index
        if (queue_index >= kMaxSubscribers) {
            return false;
        }
        
        #if defined(LIGHTAP_IPC_MODE_SHRINK)
            using MaskType = UInt8;
        #elif defined(LIGHTAP_IPC_MODE_NORMAL)
            using MaskType = UInt32;
        #else // EXTEND
            using MaskType = UInt64;
        #endif
        
        // Try to clear bit in ready_mask (CAS loop)
        while (true) {
            MaskType expected_mask = ctrl->registry.registry_control.ready_mask.load(std::memory_order_acquire);
            
            // Check if not registered
            if ( !(expected_mask & (static_cast<MaskType>(1U) << queue_index)) ) {
                return false;  // Not registered
            }
            
            MaskType new_mask = expected_mask & ~(static_cast<MaskType>(1U) << queue_index);
            if ( ctrl->registry.registry_control.ready_mask.compare_exchange_weak(
                    expected_mask, new_mask,
                    std::memory_order_acq_rel, std::memory_order_acquire) ) {
                break;  // Successfully cleared bit
            }
        }
        
        // Get inactive buffer as write buffer
        UInt32 active_idx = ctrl->registry.registry_control.active_snapshot_index.load(std::memory_order_acquire);
        UInt32 write_idx = 1 - active_idx;
        ControlBlock::SubscriberSnapshot* write_snap = &ctrl->registry.snapshots[write_idx];
        
        // Regenerate snapshot from ready_mask (optimized bit traversal)
        write_snap->count = 0;
#if defined(LIGHTAP_IPC_MODE_SHRINK)
        UInt8 m = ctrl->registry.registry_control.ready_mask.load(std::memory_order_acquire);
        while ( m ) {
            int idx = __builtin_ctz( m );  // Count trailing zeros
            write_snap->queue_indices[ write_snap->count++ ] = static_cast< UInt32 >( idx );
            m &= m - 1;  // Clear lowest set bit
        }
#elif defined(LIGHTAP_IPC_MODE_NORMAL)
        UInt32 m = ctrl->registry.registry_control.ready_mask.load(std::memory_order_acquire);
        while ( m ) {
            int idx = __builtin_ctz( m );  // Count trailing zeros
            write_snap->queue_indices[ write_snap->count++ ] = static_cast< UInt32 >( idx );
            m &= m - 1;  // Clear lowest set bit
        }
#else // EXTEND
        UInt64 m = ctrl->registry.registry_control.ready_mask.load(std::memory_order_acquire);
        while ( m ) {
            int idx = __builtin_ctzll( m );  // Count trailing zeros (64-bit)
            write_snap->queue_indices[ write_snap->count++ ] = static_cast< UInt32 >( idx );
            m &= m - 1;  // Clear lowest set bit
        }
#endif
        write_snap->version++;

        // Switch active snapshot to write buffer
        ctrl->registry.registry_control.active_snapshot_index.store( write_idx, std::memory_order_release );
        
        // Update subscriber count
        ctrl->registry.registry_control.subscriber_count.fetch_sub( 1, std::memory_order_release );
        
        return true;
    }
}
}  // namespace core
}  // namespace lap