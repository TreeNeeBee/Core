/**
 * @file        SharedMemoryManager.hpp
 * @author      LightAP Core Team
 * @brief       POSIX shared memory management
 * @date        2026-01-07
 * @details     Manages shared memory creation, mapping, and lifecycle
 * @copyright   Copyright (c) 2026
 * @note        Based on iceoryx2 design
 */
#ifndef LAP_CORE_IPC_SHARED_MEMORY_MANAGER_HPP
#define LAP_CORE_IPC_SHARED_MEMORY_MANAGER_HPP

#include "IPCTypes.hpp"
#include "ControlBlock.hpp"
#include "CResult.hpp"
#include "CString.hpp"
#include <sys/mman.h>

namespace lap
{
namespace core
{
namespace ipc
{
    /**
     * @brief Configuration for shared memory segment
     */
    struct SharedMemoryConfig
    {
        UInt8  max_channels     = kMaxChannels;             ///< Max queues
        UInt16 channel_capacity = kMaxChannelCapacity;      ///< Queue capacity
        UInt16 max_chunks       = kDefaultChunks;           ///< Maximum chunks
        UInt32 chunk_size       = kDefaultChunkSize;        ///< Chunk size
        IPCType ipc_type        = IPCType::kSPMC;           ///< IPC type
    };

    constexpr static Size kControlBlockSize     = sizeof( ControlBlock );
    constexpr static Size kChannelQueueSize     = sizeof( ChannelQueue ) + DEF_LAP_ALIGN_FORMAT( kMaxChannelCapacity * sizeof( ChannelQueueValue ), kCacheLineSize ); 
    
    constexpr static Size kChannelRegionOffset  = DEF_LAP_ALIGN_FORMAT( kControlBlockSize + sizeof( UInt8 ) * kMaxChannels * 2, kCacheLineSize ); // after ControlBlock + registry snapshots
    constexpr static Size kChunkPoolOffset      = kChannelRegionOffset + kChannelQueueSize * kMaxChannels;

    /**
     * @brief Shared memory segment manager
     * @details Handles POSIX shared memory lifecycle
     * 
     * Usage Pattern:
     * 1. First process: Create() creates new segment
     * 2. Subsequent processes: Create() opens existing segment
     * 3. Last process: Destructor optionally cleans up
     */
    class SharedMemoryManager
    {
    public:
        /**
         * @brief Calculate total size needed for shared memory
         * @param config Configuration
         * @return Total size (aligned to 2MB)
         */
        static Size CalculateTotalSize( const SharedMemoryConfig& config ) noexcept;  

    public:
        /**
         * @brief Constructor
         */
        SharedMemoryManager() noexcept
            : m_iFd(-1)
            , m_pBaseAddr(nullptr)
            , m_iSize(0)
            , m_bRefCountAcquired(false)
        {
        }
        
        /**
         * @brief Destructor - unmaps memory and optionally unlinks
         */
        ~SharedMemoryManager() noexcept
        {
            if ( m_pBaseAddr != nullptr && m_pBaseAddr != MAP_FAILED ) {
                auto* ctrl = GetControlBlock();
                if ( m_bRefCountAcquired && ctrl && ctrl->Validate() ) {
                    auto ref_count = ctrl->header.ref_count.load( std::memory_order_acquire );
                    if ( ref_count <= 1 ) {
                        ctrl->header.ready.store( SHMState::kClosing, std::memory_order_release );
                    }
                }
            }

            cleanup();
        }
        
        // Delete copy/move
        SharedMemoryManager(const SharedMemoryManager&) = delete;
        SharedMemoryManager& operator=(const SharedMemoryManager&) = delete;
        SharedMemoryManager(SharedMemoryManager&&) = delete;
        SharedMemoryManager& operator=(SharedMemoryManager&&) = delete;
        
        /**
         * @brief Create shared memory segment
         * @param shmPath Shared memory path (e.g., "/lightap_ipc_sensor_data")
         * @param config Configuration
         * @return Result with success or error
         * 
         * @details
         * - First caller: Creates and initializes shared memory
         * - Subsequent callers: Opens existing shared memory
         * - Path format: /lightap_ipc_<service_name>
         */
        Result<void> Create(const String& shmPath, 
                           const SharedMemoryConfig& config) noexcept;

        /**
         * @brief Open existing shared memory segment
         * @param shmPath Shared memory path
         * @param config Configuration
         * @return Result with success or error
         */   
        Result<void> Open(const String& shmPath, 
                           const SharedMemoryConfig& config) noexcept;
        
        /**
         * @brief Open existing shared memory segment by file descriptor
         * @param shm_fd Shared memory file descriptor
         * @param config Configuration
         * @return Result with success or error
         */
        Result<void> Open( int shm_fd, 
                           const SharedMemoryConfig& config) noexcept;  

        /**
         * @brief Get base address of shared memory
         * @return Base address pointer
         */
        inline void* GetBaseAddress() const noexcept 
        { 
            return m_pBaseAddr; 
        }
        
        /**
         * @brief Get control block
         * @return Control block pointer
         */
        inline ControlBlock* GetControlBlock() const noexcept
        {
            return static_cast< ControlBlock* >( m_pBaseAddr );
        }

        /**
         * @brief Get channel queue region start address
         * @return Control block pointer
         */
        inline ChannelQueue* GetChannelQueue() const noexcept
        {
            DEF_LAP_ASSERT( m_pBaseAddr, "Shared memory not initialized" );

            return reinterpret_cast< ChannelQueue* >( reinterpret_cast< UInt8* >( m_pBaseAddr ) + kChannelRegionOffset );
        }

        /**
         * @brief Get subscriber queue by index
         * @param queue_index Queue index (0 to max_queues-1)
         * @return Pointer to ChannelQueue in shared memory
         * 
         * @details Memory layout (optimized fixed partitions):
         * - Region 1: ControlBlock @ 0x000000-0x01FFFF (128KB)
         * - Region 2: Queues @ 0x020000-0x0E7FFF (800KB, 100 × 8KB)
         * - Region 2.5: Reserved @ 0x0E8000-0x0FFFFF (96KB)
         * - Region 3: ChunkPool @ 0x100000+ (dynamic)
         */
        ChannelQueue* GetChannelQueue( UInt32 queue_index ) const noexcept
        {
            DEF_LAP_ASSERT( m_pBaseAddr, "Shared memory not initialized" );
            DEF_LAP_ASSERT( queue_index < kMaxChannels, "Queue index out of range" );

            return reinterpret_cast< ChannelQueue* >( reinterpret_cast< UInt8* >( GetChannelQueue() ) + kChannelQueueSize * queue_index );
        }

        /**
         * @brief Get chunk pool region start address
         * @return Control block pointer
         */
        inline void* GetChunkPool() const noexcept
        {
            DEF_LAP_ASSERT( m_pBaseAddr, "Shared memory not initialized" );

            return reinterpret_cast< void* >( reinterpret_cast< UInt8* >( m_pBaseAddr ) + kChunkPoolOffset );
        }
        
        /**
         * @brief Get total size of shared memory
         * @return Size in bytes
         */
        UInt64 GetSize() const noexcept { return m_iSize; }

        /**
         * @brief Get shared memory path
         * @return Shared memory path
         */
        inline String GetShmPath() const noexcept
        {
            return m_strPath;
        }
        
    private:  
        /**
         * @brief Initialize shared memory structures
         * @param config Configuration
         * @return Result
         */
        Result<void> initializeSharedMemory(const SharedMemoryConfig& config) noexcept;
        
        /**
         * @brief cleanup resources
         */
        void cleanup() noexcept;

    private:      
        int m_iFd;                ///< Shared memory file descriptor
        void* m_pBaseAddr;           ///< Base address of mapped memory
        //void* aligned_addr_;        ///< Aligned address for allocation
        Size m_iSize;               ///< Total size of shared memory
        String m_strPath;           ///< Shared memory path
        SharedMemoryConfig m_config; ///< Configuration
        Bool m_bRefCountAcquired;    ///< Whether ref_count was acquired
    };
    
}  // namespace ipc
}  // namespace core
}  // namespace lap

#endif  // LAP_CORE_IPC_SHARED_MEMORY_MANAGER_HPP
