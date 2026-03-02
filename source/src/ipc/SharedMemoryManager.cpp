/**
 * @file        SharedMemoryManager.cpp
 * @author      LightAP Core Team
 * @brief       Implementation of SharedMemoryManager
 * @date        2026-01-07
 * @copyright   Copyright (c) 2026
 */

#include "ipc/SharedMemoryManager.hpp"
#include "ipc/ChunkPoolAllocator.hpp"
#include "CCoreErrorDomain.hpp"
#include <cstring>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace lap
{
namespace core
{
namespace ipc
{
    Size SharedMemoryManager::CalculateTotalSize( const SharedMemoryConfig& config ) noexcept
    {
        // Fixed partition layout (optimized):
        // Region 1: ControlBlock
        // Region 1.5: Registry Snapshots
        // Region 2: ChannelQueue[N]
        // Region 3: ChunkPool
        Size total_size = sizeof( ControlBlock ); // ControlBlock region
        total_size += sizeof( UInt8 ) * kMaxChannels * 2; // Registry snapshot buffers (2 × max_channels)
        total_size = DEF_LAP_ALIGN_FORMAT( total_size, kCacheLineSize ); // format to cache line size boundary

        // ChannelQueue region
        total_size += ( sizeof( ChannelQueue ) + DEF_LAP_ALIGN_FORMAT( sizeof( ChannelQueueValue ) * config.channel_capacity, kCacheLineSize ) ) * config.max_channels;             // ChannelQueue region
        
        // ChunkPool region
        total_size += DEF_LAP_ALIGN_FORMAT( sizeof( ChunkHeader ) + config.chunk_size, kCacheLineSize ) * config.max_chunks;
        
        // // Round up to 4K/2MB boundary for better alignment
        // if ( total_size < 0x200000 ) {
        //     total_size = DEF_LAP_ALIGN_FORMAT( total_size, 0x1000 );  // 4KB
        // } else {
        //     total_size = DEF_LAP_ALIGN_FORMAT( total_size, 0x200000 ); // 2MB
        // }

        total_size = DEF_LAP_ALIGN_FORMAT( total_size, kShmAlignment_4K ); // 4KB alignment
        
        return total_size;
    }

    Result<void> SharedMemoryManager::Create(const String& shmPath,
                                             const SharedMemoryConfig& config) noexcept
    {
        INNER_CORE_LOG("[DEBUG] Creating shared memory segment: %s\n", shmPath.c_str() );

        m_strPath = shmPath;
        m_config = config;
        
        // Try to create new shared memory (O_CREAT | O_EXCL)
        m_iFd = shm_open( m_strPath.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666 );
        
        if ( m_iFd >= 0 ) { 
            // Calculate and align total size
            m_iSize = CalculateTotalSize( config );

            // Set size
            if ( ftruncate( m_iFd, static_cast< off_t >( m_iSize ) ) != 0 ) {
                close(m_iFd);
                m_iFd = -1;
                return Result< void >( MakeErrorCode( CoreErrc::kIPCShmCreateFailed ) );
            }
            
            // Map memory
            m_pBaseAddr = mmap( nullptr, m_iSize, PROT_READ | PROT_WRITE,
                            MAP_SHARED, m_iFd, 0 );
            
            if ( m_pBaseAddr == MAP_FAILED ) {
                close( m_iFd );
                m_iFd = -1;
                m_pBaseAddr = nullptr;
                return Result< void >( MakeErrorCode( CoreErrc::kIPCShmMapFailed ) );
            }

            DEF_LAP_ASSERT( reinterpret_cast< UIntPtr >( m_pBaseAddr ) % kCacheLineSize == 0, "Shared memory not aligned properly" );
            
            // Initialize structures
            auto result = initializeSharedMemory( config );
            if ( !result ) {
                cleanup();
                return result;
            }   

            m_bRefCountAcquired = true;
            
            return {};
        } else if ( errno == EEXIST ) {
            return Result< void >( MakeErrorCode( CoreErrc::kIPCShmAlreadyExists ) );
        } else {
            return Result< void >( MakeErrorCode( CoreErrc::kIPCShmCreateFailed ) );
        }
    }

    Result<void> SharedMemoryManager::Open(const String& shmPath, 
                           const SharedMemoryConfig& config) noexcept
    {
        INNER_CORE_LOG("[DEBUG] Opening shared memory segment: %s\n", shmPath.c_str() );

        m_strPath = shmPath;
        m_config = config;

        m_iFd = shm_open(m_strPath.c_str(), O_RDWR, 0666);
        if ( m_iFd < 0 ) {
            return Result< void >( MakeErrorCode( CoreErrc::kIPCShmNotFound ) );
        }
        
        // Get existing size
        struct stat sb;
        if ( fstat( m_iFd, &sb ) != 0 ) {
            close( m_iFd );
            m_iFd = -1;
            return Result< void >( MakeErrorCode( CoreErrc::kIPCShmStatFailed ) );
        }
        
        m_iSize = static_cast< Size >( sb.st_size );
        
        // Map memory
        m_pBaseAddr = mmap(nullptr, m_iSize, PROT_READ | PROT_WRITE,
                        MAP_SHARED, m_iFd, 0);
        
        if ( m_pBaseAddr == MAP_FAILED ) {
            close( m_iFd );
            m_iFd = -1;
            m_pBaseAddr = nullptr;
            return Result< void >( MakeErrorCode( CoreErrc::kIPCShmMapFailed ) );
        }

        DEF_LAP_ASSERT( reinterpret_cast< UIntPtr >( m_pBaseAddr ) % kCacheLineSize == 0, "Shared memory not aligned properly" );
        
        // Validate control block
        auto* ctrl = GetControlBlock();
        if ( !ctrl || !ctrl->Ready() || ctrl->header.type != config.ipc_type ) {
            cleanup();
            return Result< void >( MakeErrorCode( CoreErrc::kIPCShmInvalidMagic ) );
        }

        ctrl->header.ref_count.fetch_add( 1, std::memory_order_acq_rel );
        m_bRefCountAcquired = true;
        
        return {};
    }

    Result<void> SharedMemoryManager::Open( int shm_fd, const SharedMemoryConfig& config) noexcept
    {
        DEF_LAP_ASSERT( shm_fd >= 0, "Invalid shared memory file descriptor" );
        INNER_CORE_LOG("[DEBUG] Opening shared memory segment with fd: %d\n", shm_fd );
        
        m_config = config;
        m_iFd = -1;
        // Get existing size
        struct stat sb;
        if ( fstat( shm_fd, &sb ) != 0 ) {
            close( shm_fd );
            return Result< void >( MakeErrorCode( CoreErrc::kIPCShmStatFailed ) );
        }
   
        m_iSize = static_cast< Size >( sb.st_size );
        
        // Map memory
        m_pBaseAddr = mmap(nullptr, m_iSize, PROT_READ | PROT_WRITE,
                        MAP_SHARED, shm_fd, 0);
        
        if ( m_pBaseAddr == MAP_FAILED ) {
            close( shm_fd );
            m_pBaseAddr = nullptr;
            return Result< void >( MakeErrorCode( CoreErrc::kIPCShmMapFailed ) );
        }

        m_iFd = shm_fd;

        DEF_LAP_ASSERT( reinterpret_cast< UIntPtr >( m_pBaseAddr ) % kCacheLineSize == 0, "Shared memory not aligned properly" );
        
        // Validate control block
        auto* ctrl = GetControlBlock();
        if ( !ctrl || !ctrl->Ready() || ctrl->header.type != config.ipc_type ) {
            cleanup();
            return Result< void >( MakeErrorCode( CoreErrc::kIPCShmInvalidMagic ) );
        }

        ctrl->header.ref_count.fetch_add( 1, std::memory_order_acq_rel );
        m_bRefCountAcquired = true;
        
        return {};  
    } 
    
    Result<void> SharedMemoryManager::initializeSharedMemory(const SharedMemoryConfig& config) noexcept
    {
        INNER_CORE_LOG("[DEBUG] Initializing shared memory structures, size: %zu bytes\n", m_iSize);
        // Zero out entire shared memory
        std::memset(m_pBaseAddr, 0, m_iSize);

        auto* ctrl = GetControlBlock();
        ctrl->Initialize( config.max_chunks,
                        config.max_channels > 0 ? config.max_channels : kMaxChannels,
                        config.chunk_size,
                        config.ipc_type,
                        config.channel_capacity > 0 ? config.channel_capacity : kMaxChannelCapacity );
        
        // Initialize all ChannelQueue slots in Region 2 (0x100000-0x1FFFFF)
        // Each queue is 8KB, allowing up to 128 queues in 1MB
        UInt32 max_channels = ctrl->header.max_channels;
        UInt32 channel_capacity = ctrl->header.channel_capacity;
        
        for ( UInt32 i = 0; i < max_channels; ++i ) {
            auto* channel = GetChannelQueue( i );
            if ( channel ) {
                // Initialize queue structure (sets active=false)
                channel->Initialize( channel_capacity );
                
                // Zero-initialize buffer (inline after queue struct)
                auto buffer = channel->GetBuffer();
                for ( UInt32 j = 0; j < channel_capacity; ++j ) {
                    buffer[j].sequence = 0;
                    buffer[j].chunk_index = kInvalidChunkIndex;
                }
            }
        }
        
        INNER_CORE_LOG("[DEBUG] Shared memory initialization complete\n");
        INNER_CORE_LOG("  - Max subscribers: %u\n", static_cast<UInt32>(kMaxChannels));
        INNER_CORE_LOG("  - Queue capacity: %u per subscriber\n", static_cast<UInt32>(kMaxChannelCapacity));
        INNER_CORE_LOG("  - ControlBlock size: %zu bytes\n", sizeof(ControlBlock));
        INNER_CORE_LOG("  - Total queues initialized: %u\n", config.max_channels);
        
        // ChunkPool initialization will be done in ChunkPoolAllocator
        ctrl->header.ref_count.store( 1, std::memory_order_release );

        ctrl->header.ready.store( SHMState::kReady, std::memory_order_release );
        
        return {};
    }
    
    void SharedMemoryManager::cleanup() noexcept
    {
        Bool should_unlink = false;

        // Check if we should unlink the shared memory
        if ( m_pBaseAddr != nullptr && m_pBaseAddr != MAP_FAILED ) {
            auto* ctrl = GetControlBlock();
            if ( m_bRefCountAcquired && ctrl && ctrl->Validate() ) {
                const UInt32 previous = ctrl->header.ref_count.fetch_sub( 1, std::memory_order_acq_rel );
                if ( previous == 1U ) {
                    should_unlink = true;
                }
            }
            
            munmap(m_pBaseAddr, m_iSize);
            m_pBaseAddr = nullptr;
        }
        
        if ( m_iFd >= 0 ) {
            close( m_iFd );
            m_iFd = -1;
        }
        
        // Unlink only if we're the last process
        if ( should_unlink && !m_strPath.empty() ) {
            shm_unlink(m_strPath.c_str());
        }

        m_bRefCountAcquired = false;
    }
    
}  // namespace ipc
}  // namespace core
}  // namespace lap
