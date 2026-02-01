/**
 * @file        IPCFactory.hpp
 * @author      LightAP Core Team
 * @brief       IPC factory for standardized creation/destruction interfaces
 * @date        2026-02-01
 * @details     Provides unified Create/Destroy helpers for IPC components
 * @copyright   Copyright (c) 2026
 */
#ifndef LAP_CORE_IPC_FACTORY_HPP
#define LAP_CORE_IPC_FACTORY_HPP

#include "ipc/SharedMemoryManager.hpp"
#include "ipc/Publisher.hpp"
#include "ipc/Subscriber.hpp"

namespace lap
{
namespace core
{
namespace ipc
{
    /**
     * @brief IPC factory
     * @details Simplifies external usage with standardized create/destroy methods
     */
    class IPCFactory final
    {
    public:
        /**
         * @brief Create shared memory segment
         * @param shmPath Shared memory path
         * @param config Shared memory configuration
         * @return Result with shared memory manager or error
         */
        static Result< UniqueHandle< SharedMemoryManager > > CreateSHM(
            const String& shmPath,
            const SharedMemoryConfig& config = {} ) noexcept;

        /**
         * @brief Destroy shared memory manager
         * @param shm Shared memory manager handle
         */
        static void DestroySHM( UniqueHandle< SharedMemoryManager >& shm ) noexcept;

        /**
         * @brief Create publisher
         * @param shmPath Shared memory path
         * @param config Publisher configuration
         * @return Result with publisher handle or error
         */
        static Result< UniqueHandle< Publisher > > CreatePublisher(
            const String& shmPath,
            const PublisherConfig& config = {} ) noexcept;

        /**
         * @brief Destroy publisher
         * @param publisher Publisher handle
         */
        static void DestroyPublisher( UniqueHandle< Publisher >& publisher ) noexcept;

        /**
         * @brief Create subscriber
         * @param shmPath Shared memory path
         * @param config Subscriber configuration
         * @return Result with subscriber handle or error
         */
        static Result< UniqueHandle< Subscriber > > CreateSubscriber(
            const String& shmPath,
            const SubscriberConfig& config = {} ) noexcept;

        /**
         * @brief Destroy subscriber
         * @param subscriber Subscriber handle
         */
        static void DestroySubscriber( UniqueHandle< Subscriber >& subscriber ) noexcept;
    };

}  // namespace ipc
}  // namespace core
}  // namespace lap

#endif  // LAP_CORE_IPC_FACTORY_HPP
