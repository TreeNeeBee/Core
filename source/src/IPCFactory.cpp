/**
 * @file        IPCFactory.cpp
 * @author      LightAP Core Team
 * @brief       IPC factory implementation
 * @date        2026-02-01
 * @copyright   Copyright (c) 2026
 */

#include "IPCFactory.hpp"

namespace lap
{
namespace core
{
namespace ipc
{
    Result< UniqueHandle< SharedMemoryManager > > IPCFactory::CreateSHM(
        const String& shmPath,
        const SharedMemoryConfig& config ) noexcept
    {
        auto shm = std::make_unique< SharedMemoryManager >();
        auto result = shm->Create( shmPath, config );
        if ( !result ) {
            return Result< UniqueHandle< SharedMemoryManager > >::FromError( result.Error() );
        }
        return Result< UniqueHandle< SharedMemoryManager > >::FromValue( std::move( shm ) );
    }

    void IPCFactory::DestroySHM( UniqueHandle< SharedMemoryManager >& shm ) noexcept
    {
        shm.reset();
    }

    Result< UniqueHandle< Publisher > > IPCFactory::CreatePublisher(
        const String& shmPath,
        const PublisherConfig& config ) noexcept
    {
        auto result = Publisher::Create( shmPath, config );
        if ( !result ) {
            return Result< UniqueHandle< Publisher > >::FromError( result.Error() );
        }
        auto publisher = MakeUnique< Publisher >( std::move( result ).Value() );
        return Result< UniqueHandle< Publisher > >::FromValue( std::move( publisher ) );
    }

    void IPCFactory::DestroyPublisher( UniqueHandle< Publisher >& publisher ) noexcept
    {
        publisher.reset();
    }

    Result< UniqueHandle< Subscriber > > IPCFactory::CreateSubscriber(
        const String& shmPath,
        const SubscriberConfig& config ) noexcept
    {
        auto result = Subscriber::Create( shmPath, config );
        if ( !result ) {
            return Result< UniqueHandle< Subscriber > >::FromError( result.Error() );
        }
        auto subscriber = MakeUnique< Subscriber >( std::move( result ).Value() );
        return Result< UniqueHandle< Subscriber > >::FromValue( std::move( subscriber ) );
    }

    void IPCFactory::DestroySubscriber( UniqueHandle< Subscriber >& subscriber ) noexcept
    {
        subscriber.reset();
    }

}  // namespace ipc
}  // namespace core
}  // namespace lap
