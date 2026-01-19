/**
 * @file        Message.hpp
 * @author      LightAP Core Team
 * @brief       Base class for IPC message interpreter (view/codec pattern)
 * @date        2026-01-09
 * @details     Message objects are NOT stored in shared memory.
 *              They are interpreters/views that reference chunk data.
 *              Similar to serialization/deserialization or Protobuf pattern.
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
     * @brief Base class for IPC message interpreter
     * 
     * @details Message objects live in each process's heap/stack (NOT in shared memory).
     *          They interpret chunk data via OnMessageSend/OnMessageReceived callbacks.
     *          
     *          Design pattern:
     *          - Publisher: Create Message -> Set data -> SendMessage() -> OnMessageSend(chunk_ptr) writes to chunk
     *          - Subscriber: ReceiveMessage() -> OnMessageReceived(chunk_ptr) reads from chunk -> Use data
     *          
     *          This design allows:
     *          - Virtual functions work correctly (each process has its own vtable)
     *          - Type-safe message handling with callbacks
     *          - Zero-copy for large payloads (Message only references chunk data)
     * 
     * Usage:
     * @code
     * class MyMessage : public Message {
     * public:
     *     MyMessage() : sequence(0), data(0) {}
     *     
     *     void SetData(uint64_t seq, uint64_t val) {
     *         sequence = seq;
     *         data = val;
     *     }
     *     
     *     void OnMessageSend(void* const chunk_ptr) noexcept override {
     *         auto* p = static_cast<uint8_t*>(chunk_ptr);
     *         std::memcpy(p, &sequence, sizeof(sequence));
     *         std::memcpy(p + 8, &data, sizeof(data));
     *     }
     *     
     *     void OnMessageReceived(const void* const chunk_ptr) noexcept override {
     *         auto* p = static_cast<const uint8_t*>(chunk_ptr);
     *         std::memcpy(&sequence, p, sizeof(sequence));
     *         std::memcpy(&data, p + 8, sizeof(data));
     *     }
     *     
     * private:
     *     UInt64 sequence;
     *     UInt64 data;
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
         * @return Type ID for message discrimination
         */
        virtual UInt32 GetTypeId() const noexcept { return 0; }
 
        /**
         * @brief Lifecycle callback - write data to chunk before sending
         * @param chunk_ptr Pointer to chunk memory (from Sample.Get())
         * @note Called by Publisher::SendMessage(). Override to write message data to chunk.
         */
        virtual void OnMessageSend( void* const, Size ) noexcept {}
        
        /**
         * @brief Lifecycle callback - read data from chunk after receiving
         * @param chunk_ptr Pointer to chunk memory (from Sample.Get())
         * @note Called by Subscriber::ReceiveMessage(). Override to read message data from chunk.
         */
        virtual void OnMessageReceived( const void* const, Size ) noexcept {}
        
        /**
         * @brief Callback when message is dropped (queue full)
         */
        virtual void OnMessageDropped() noexcept {}
        
        /**
         * @brief Callback when send fails
         */
        virtual void OnMessageFailed() noexcept {}
    };
    
}  // namespace ipc
}  // namespace core
}  // namespace lap

#endif  // LAP_CORE_IPC_MESSAGE_HPP
