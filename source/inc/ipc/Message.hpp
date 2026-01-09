/**
 * @file        Message.hpp
 * @author      LightAP Core Team
 * @brief       Base class for all IPC messages
 * @date        2026-01-09
 * @details     Provides a common base for type-safe message passing
 * @copyright   Copyright (c) 2026
 */
#ifndef LAP_CORE_IPC_MESSAGE_HPP
#define LAP_CORE_IPC_MESSAGE_HPP

#include "CTypedef.hpp"

namespace lap
{
namespace core
{
namespace ipc
{
    /**
     * @brief Base class for all IPC messages
     * @details All messages sent through Publisher/Subscriber should inherit from this
     * 
     * Usage:
     * @code
     * struct MyMessage : public Message {
     *     UInt64 sequence;
     *     UInt32 data;
     *     char payload[1024];
     * };
     * @endcode
     */
    class Message
    {
    public:
        Message() noexcept = default;
        virtual ~Message() noexcept = default;
        
        // Allow copy and move
        Message(const Message&) noexcept = default;
        Message& operator=(const Message&) noexcept = default;
        Message(Message&&) noexcept = default;
        Message& operator=(Message&&) noexcept = default;
        
        /**
         * @brief Get message type ID (override in derived classes)
         * @return Type ID (default 0)
         */
        virtual UInt32 GetTypeId() const noexcept { return 0; }
        
        /**
         * @brief Get message size (override in derived classes if needed)
         * @return Size in bytes
         */
        virtual UInt64 GetSize() const noexcept { return sizeof(Message); }
    };
    
}  // namespace ipc
}  // namespace core
}  // namespace lap

#endif  // LAP_CORE_IPC_MESSAGE_HPP
