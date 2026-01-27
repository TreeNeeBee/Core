/**
 * @file        Channel.hpp
 * @author      LightAP Core Team
 * @brief       Lock-free SPSC channel (stream-like design)
 * @date        2026-01-26
 * @details     Refactored from RingBufferBlock with iostream-style API
 *              Channel, ReadChannel, WriteChannel for shared memory queue
 *              Integrated with WaitSetHelper for blocking/polling support
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR C++14 compliant, based on iceoryx2 design
 */
#ifndef LAP_CORE_IPC_CHANNEL_HPP
#define LAP_CORE_IPC_CHANNEL_HPP

#include "IPCTypes.hpp"
#include "COptional.hpp"
#include "WaitSetHelper.hpp"
#include "CResult.hpp"
#include "CCoreErrorDomain.hpp"
#include <atomic>

namespace lap
{
namespace core
{
namespace ipc
{
    /**
     * @brief Base channel class (similar to std::ios)
     * @tparam T Value type stored in channel
     * @details Holds references to shared memory fields, does not own data
     * 
     * Design principles:
     * - Template-based for any value type
     * - No data ownership (all fields are pointers to shared memory)
     * - Lightweight (only stores references)
     * - Can be copied (shallow copy of references)
     * - Pure Read/Write functionality only
     * - Supports waitset-based blocking strategies
     */
    template<typename T>
    class Channel
    {
    public:
        /**
         * @brief Default constructor (invalid channel)
         */
        Channel() noexcept
            : head_(nullptr)
            , tail_(nullptr)
            , waitset_(nullptr)
            , buffer_(nullptr)
            , capacity_(0)
        {
        }
        
        /**
         * @brief Constructor with shared memory references
         * @param head Consumer index pointer
         * @param tail Producer index pointer
         * @param waitset Event flags pointer (optional, can be nullptr)
         * @param buffer Ring buffer storage pointer
         * @param capacity Buffer capacity (must be power of 2)
         */
        Channel(std::atomic<UInt16>* head,
                std::atomic<UInt16>* tail,
                std::atomic<UInt32>* waitset,
                T* buffer,
                UInt16 capacity) noexcept
            : head_(head)
            , tail_(tail)
            , waitset_(waitset)
            , buffer_(buffer)
            , capacity_(capacity)
        {
        }
        
        /**
         * @brief Destructor
         */
        virtual ~Channel() noexcept = default;
        
        // Allow copy and move (shallow copy of references)
        Channel(const Channel&) noexcept = default;
        Channel& operator=(const Channel&) noexcept = default;
        Channel(Channel&&) noexcept = default;
        Channel& operator=(Channel&&) noexcept = default;
        
        /**
         * @brief Check if channel is valid
         * @return true if channel has valid state
         */
        Bool IsValid() const noexcept
        {
            return head_ != nullptr && 
                   tail_ != nullptr && 
                   buffer_ != nullptr &&
                   capacity_ > 0;
        }
        
        /**
         * @brief Check if channel is empty
         * @return true if empty
         */
        Bool IsEmpty() const noexcept
        {
            if (!IsValid()) {
                return true;
            }
            
            UInt16 head = head_->load(std::memory_order_relaxed);
            UInt16 tail = tail_->load(std::memory_order_acquire);
            
            return head == tail;
        }
        
        /**
         * @brief Check if channel is full
         * @return true if full
         */
        Bool IsFull() const noexcept
        {
            if (!IsValid()) {
                return true;
            }
            
            UInt16 tail = tail_->load(std::memory_order_relaxed);
            UInt16 next_tail = static_cast<UInt16>((tail + 1) & (capacity_ - 1));
            UInt16 head = head_->load(std::memory_order_acquire);
            
            return next_tail == head;
        }
        
        /**
         * @brief Get current size (approximate)
         * @return Number of elements in channel
         * @note This is approximate due to concurrent access
         */
        UInt32 Size() const noexcept
        {
            if (!IsValid()) {
                return 0;
            }
            
            UInt16 head = head_->load(std::memory_order_relaxed);
            UInt16 tail = tail_->load(std::memory_order_relaxed);
            
            return static_cast<UInt32>((tail - head) & (capacity_ - 1));
        }
        
        /**
         * @brief Get channel capacity
         * @return Maximum capacity
         */
        UInt16 GetCapacity() const noexcept
        {
            return capacity_;
        }
        
        /**
         * @brief Get waitset flags
         * @return Current event flags
         */
        UInt32 GetWaitsetFlags() const noexcept
        {
            if (!IsValid() || waitset_ == nullptr) {
                return EventFlag::kNone;
            }
            return waitset_->load(std::memory_order_acquire);
        }
        
        /**
         * @brief Set waitset flags
         * @param flags Event flags to set
         */
        void SetWaitsetFlags(UInt32 flags) noexcept
        {
            if (IsValid() && waitset_ != nullptr) {
                waitset_->store(flags, std::memory_order_release);
            }
        }
        
    protected:
        std::atomic<UInt16>*  head_;      ///< Consumer index pointer
        std::atomic<UInt16>*  tail_;      ///< Producer index pointer
        std::atomic<UInt32>*  waitset_;   ///< Event flags pointer
        T*                    buffer_;    ///< Ring buffer storage pointer
        UInt16                capacity_;  ///< Buffer capacity (power of 2)
    };
    
    /**
     * @brief Write-only channel (similar to std::ostream)
     * @tparam T Value type stored in channel
     * @details Used by Publisher to write to subscriber queues
     * 
     * Ownership: Does not own shared memory, only references it
     * Supports PublishPolicy-based strategies for queue full scenarios
     */
    template<typename T>
    class WriteChannel : public Channel<T>
    {
    public:
        /**
         * @brief Default constructor
         */
        WriteChannel() noexcept = default;
        
        /**
         * @brief Constructor with shared memory references
         * @param head Consumer index pointer
         * @param tail Producer index pointer
         * @param waitset Event flags pointer (optional)
         * @param buffer Ring buffer storage pointer
         * @param capacity Buffer capacity (must be power of 2)
         */
        WriteChannel(std::atomic<UInt16>* head,
                     std::atomic<UInt16>* tail,
                     std::atomic<UInt32>* waitset,
                     T* buffer,
                     UInt16 capacity) noexcept
            : Channel<T>(head, tail, waitset, buffer, capacity)
        {
        }
        
        /**
         * @brief Destructor
         */
        ~WriteChannel() noexcept override = default;
        
        // Allow copy and move
        WriteChannel(const WriteChannel&) noexcept = default;
        WriteChannel& operator=(const WriteChannel&) noexcept = default;
        WriteChannel(WriteChannel&&) noexcept = default;
        WriteChannel& operator=(WriteChannel&&) noexcept = default;
        
        /**
         * @brief Write value to channel (producer operation)
         * @param value Value to write
         * @return Result with void on success or error code
         * 
         * @details Lock-free SPSC producer operation
         * - Relaxed load of tail (producer-owned)
         * - Acquire load of head (consumer-owned)
         * - Release store of tail (visibility to consumer)
         * 
         * Possible errors:
         * - kChannelInvalid: Channel not properly initialized
         * - kChannelFull: Queue is full, cannot write
         */
        Result<void> Write(const T& value) noexcept
        {
            if (!this->IsValid()) {
                return Result<void>(MakeErrorCode(CoreErrc::kChannelInvalid));
            }
            
            UInt16 tail = this->tail_->load(std::memory_order_relaxed);
            UInt16 next_tail = static_cast<UInt16>((tail + 1) & (this->capacity_ - 1));
            
            // Check if full
            UInt16 head = this->head_->load(std::memory_order_acquire);
            if (next_tail == head) {
                return Result<void>(MakeErrorCode(CoreErrc::kChannelFull));
            }
            
            // Write data
            this->buffer_[tail] = value;
            
            // Update tail with release semantics (consumer visibility)
            this->tail_->store(next_tail, std::memory_order_release);
            
            // Set HasData flag for waitset
            if (this->waitset_ != nullptr) {
                UInt32 flags = this->waitset_->load(std::memory_order_relaxed);
                this->waitset_->store(flags | EventFlag::kHasData, std::memory_order_release);
            }
            
            return Result<void>();
        }
        
        /**
         * @brief Write value with policy (supports blocking/overwrite strategies)
         * @param value Value to write
         * @param policy Write policy for queue full scenario
         * @param timeout_ns Timeout in nanoseconds (0 = infinite for kBlock, ignored for others)
         * @return Result with void on success or error code
         * 
         * @details Implements PublishPolicy strategies:
         * - kOverwrite: Overwrite oldest value (advance head)
         * - kDrop: Drop new value and return error
         * - kError: Return error immediately if full
         * - kBlock: Block on futex until space available (with timeout)
         * - kWait: Busy-wait polling until space available (with timeout)
         * 
         * Possible errors:
         * - kChannelInvalid: Channel not properly initialized
         * - kChannelFull: Queue is full (for kDrop/kError policies)
         * - kChannelTimeout: Operation timed out (for kBlock/kWait policies)
         * - kChannelWaitsetUnavailable: Waitset required but not available
         * - kChannelSpuriousWakeup: Wakeup occurred but queue still full
         */
        Result<void> WriteWithPolicy(const T& value, PublishPolicy policy, UInt64 timeout_ns = 0) noexcept
        {
            if (!this->IsValid()) {
                return Result<void>(MakeErrorCode(CoreErrc::kChannelInvalid));
            }
            
            // Try to write once first
            UInt16 tail = this->tail_->load(std::memory_order_relaxed);
            UInt16 next_tail = static_cast<UInt16>((tail + 1) & (this->capacity_ - 1));
            UInt16 head = this->head_->load(std::memory_order_acquire);
            
            // Check if full
            if (next_tail == head) {
                // Handle queue full based on policy
                switch (policy) {
                    case PublishPolicy::kOverwrite:
                        // Overwrite oldest: advance head by one
                        {
                            UInt16 new_head = static_cast<UInt16>((head + 1) & (this->capacity_ - 1));
                            this->head_->store(new_head, std::memory_order_release);
                        }
                        break;
                        
                    case PublishPolicy::kDrop:
                    case PublishPolicy::kError:
                        return Result<void>(MakeErrorCode(CoreErrc::kChannelFull));
                        
                    case PublishPolicy::kBlock:
                        // Block on futex until kHasSpace flag is set
                        if (this->waitset_ == nullptr) {
                            return Result<void>(MakeErrorCode(CoreErrc::kChannelWaitsetUnavailable));
                        }
                        
                        {
                            Duration timeout = (timeout_ns > 0) 
                                ? Duration(timeout_ns) 
                                : Duration::zero();  // 0 = infinite
                            
                            auto result = WaitSetHelper::WaitForFlags(
                                this->waitset_,
                                EventFlag::kHasSpace,
                                timeout
                            );
                            
                            if (!result) {
                                return Result<void>(MakeErrorCode(CoreErrc::kChannelTimeout));
                            }
                            
                            // Retry write after wakeup
                            tail = this->tail_->load(std::memory_order_relaxed);
                            next_tail = static_cast<UInt16>((tail + 1) & (this->capacity_ - 1));
                            head = this->head_->load(std::memory_order_acquire);
                            
                            if (next_tail == head) {
                                return Result<void>(MakeErrorCode(CoreErrc::kChannelSpuriousWakeup));
                            }
                        }
                        break;
                        
                    case PublishPolicy::kWait:
                        // Busy-wait polling for kHasSpace flag
                        if (this->waitset_ == nullptr) {
                            return Result<void>(MakeErrorCode(CoreErrc::kChannelWaitsetUnavailable));
                        }
                        
                        {
                            Duration timeout = (timeout_ns > 0) 
                                ? Duration(timeout_ns) 
                                : Duration(10000000);  // Default 10ms for polling
                            
                            Bool success = WaitSetHelper::PollForFlags(
                                this->waitset_,
                                EventFlag::kHasSpace,
                                timeout
                            );
                            
                            if (!success) {
                                return Result<void>(MakeErrorCode(CoreErrc::kChannelTimeout));
                            }
                            
                            // Retry write after polling success
                            tail = this->tail_->load(std::memory_order_relaxed);
                            next_tail = static_cast<UInt16>((tail + 1) & (this->capacity_ - 1));
                            head = this->head_->load(std::memory_order_acquire);
                            
                            if (next_tail == head) {
                                return Result<void>(MakeErrorCode(CoreErrc::kChannelFull));
                            }
                        }
                        break;
                }
            }
            
            // Write data
            this->buffer_[tail] = value;
            this->tail_->store(next_tail, std::memory_order_release);
            
            // Set HasData flag and wake waiters
            if (this->waitset_ != nullptr) {
                WaitSetHelper::SetFlagsAndWake(this->waitset_, EventFlag::kHasData);
            }
            
            return Result<void>();
        }
    };
    
    /**
     * @brief Read-only channel (similar to std::istream)
     * @tparam T Value type stored in channel
     * @details Used by Subscriber to read from its queue
     * 
     * Ownership: Does not own shared memory, only references it
     * Supports SubscribePolicy-based strategies for queue empty scenarios
     */
    template<typename T>
    class ReadChannel : public Channel<T>
    {
    public:
        /**
         * @brief Default constructor
         */
        ReadChannel() noexcept = default;
        
        /**
         * @brief Constructor with shared memory references
         * @param head Consumer index pointer
         * @param tail Producer index pointer
         * @param waitset Event flags pointer (optional)
         * @param buffer Ring buffer storage pointer
         * @param capacity Buffer capacity (must be power of 2)
         */
        ReadChannel(std::atomic<UInt16>* head,
                    std::atomic<UInt16>* tail,
                    std::atomic<UInt32>* waitset,
                    T* buffer,
                    UInt16 capacity) noexcept
            : Channel<T>(head, tail, waitset, buffer, capacity)
        {
        }
        
        /**
         * @brief Destructor
         */
        ~ReadChannel() noexcept override = default;
        
        // Allow copy and move
        ReadChannel(const ReadChannel&) noexcept = default;
        ReadChannel& operator=(const ReadChannel&) noexcept = default;
        ReadChannel(ReadChannel&&) noexcept = default;
        ReadChannel& operator=(ReadChannel&&) noexcept = default;
        
        /**
         * @brief Read value from channel (consumer operation)
         * @return Result with value on success or error code
         * 
         * @details Lock-free SPSC consumer operation
         * - Relaxed load of head (consumer-owned)
         * - Acquire load of tail (producer-owned)
         * - Release store of head (visibility to producer)
         * 
         * Possible errors:
         * - kChannelInvalid: Channel not properly initialized
         * - kChannelEmpty: Queue is empty, no data available
         */
        Result<T> Read() noexcept
        {
            if (!this->IsValid()) {
                return Result<T>(MakeErrorCode(CoreErrc::kChannelInvalid));
            }
            
            UInt16 head = this->head_->load(std::memory_order_relaxed);
            
            // Check if empty
            UInt16 tail = this->tail_->load(std::memory_order_acquire);
            if (head == tail) {
                // Clear HasData flag if empty
                if (this->waitset_ != nullptr) {
                    UInt32 flags = this->waitset_->load(std::memory_order_relaxed);
                    this->waitset_->store(flags & ~EventFlag::kHasData, std::memory_order_release);
                }
                return Result<T>(MakeErrorCode(CoreErrc::kChannelEmpty));
            }
            
            // Read data
            T value = this->buffer_[head];
            
            // Update head with release semantics (producer visibility)
            UInt16 next_head = static_cast<UInt16>((head + 1) & (this->capacity_ - 1));
            this->head_->store(next_head, std::memory_order_release);
            
            // Set HasSpace flag for waitset
            if (this->waitset_ != nullptr) {
                UInt32 flags = this->waitset_->load(std::memory_order_relaxed);
                this->waitset_->store(flags | EventFlag::kHasSpace, std::memory_order_release);
            }
            
            return Result<T>(std::move(value));
        }
        
        /**
         * @brief Read value with policy (supports blocking/skip strategies)
         * @param policy Read policy for queue empty scenario
         * @param timeout_ns Timeout in nanoseconds (0 = infinite for kBlock, ignored for others)
         * @return Result with value on success or error code
         * 
         * @details Implements SubscribePolicy strategies:
         * - kSkip: Return kChannelEmpty immediately if queue empty
         * - kError: Same as kSkip, return kChannelEmpty
         * - kBlock: Block on futex until data available (with timeout)
         * - kWait: Busy-wait polling until data available (with timeout)
         * 
         * Possible errors:
         * - kChannelInvalid: Channel not properly initialized
         * - kChannelEmpty: Queue is empty (for kSkip/kError policies)
         * - kChannelTimeout: Operation timed out (for kBlock/kWait policies)
         * - kChannelWaitsetUnavailable: Waitset required but not available
         * - kChannelSpuriousWakeup: Wakeup occurred but queue still empty
         */
        Result<T> ReadWithPolicy(SubscribePolicy policy, UInt64 timeout_ns = 0) noexcept
        {
            if (!this->IsValid()) {
                return Result<T>(MakeErrorCode(CoreErrc::kChannelInvalid));
            }
            
            // Try to read once first
            UInt16 head = this->head_->load(std::memory_order_relaxed);
            UInt16 tail = this->tail_->load(std::memory_order_acquire);
            
            // Check if empty
            if (head == tail) {
                // Handle queue empty based on policy
                switch (policy) {
                    case SubscribePolicy::kSkip:
                    case SubscribePolicy::kError:
                        // Clear HasData flag
                        if (this->waitset_ != nullptr) {
                            WaitSetHelper::ClearFlags(this->waitset_, EventFlag::kHasData);
                        }
                        return Result<T>(MakeErrorCode(CoreErrc::kChannelEmpty));
                        
                    case SubscribePolicy::kBlock:
                        // Block on futex until kHasData flag is set
                        if (this->waitset_ == nullptr) {
                            return Result<T>(MakeErrorCode(CoreErrc::kChannelWaitsetUnavailable));
                        }
                        
                        {
                            Duration timeout = (timeout_ns > 0) 
                                ? Duration(timeout_ns) 
                                : Duration::zero();  // 0 = infinite
                            
                            auto result = WaitSetHelper::WaitForFlags(
                                this->waitset_,
                                EventFlag::kHasData,
                                timeout
                            );
                            
                            if (!result) {
                                return Result<T>(MakeErrorCode(CoreErrc::kChannelTimeout));
                            }
                            
                            // Retry read after wakeup
                            head = this->head_->load(std::memory_order_relaxed);
                            tail = this->tail_->load(std::memory_order_acquire);
                            
                            if (head == tail) {
                                return Result<T>(MakeErrorCode(CoreErrc::kChannelSpuriousWakeup));
                            }
                        }
                        break;
                        
                    case SubscribePolicy::kWait:
                        // Busy-wait polling for kHasData flag
                        if (this->waitset_ == nullptr) {
                            return Result<T>(MakeErrorCode(CoreErrc::kChannelWaitsetUnavailable));
                        }
                        
                        {
                            Duration timeout = (timeout_ns > 0) 
                                ? Duration(timeout_ns) 
                                : Duration(10000000);  // Default 10ms for polling
                            
                            Bool success = WaitSetHelper::PollForFlags(
                                this->waitset_,
                                EventFlag::kHasData,
                                timeout
                            );
                            
                            if (!success) {
                                return Result<T>(MakeErrorCode(CoreErrc::kChannelTimeout));
                            }
                            
                            // Retry read after polling success
                            head = this->head_->load(std::memory_order_relaxed);
                            tail = this->tail_->load(std::memory_order_acquire);
                            
                            if (head == tail) {
                                return Result<T>(MakeErrorCode(CoreErrc::kChannelEmpty));
                            }
                        }
                        break;
                }
            }
            
            // Read data
            T value = this->buffer_[head];
            UInt16 next_head = static_cast<UInt16>((head + 1) & (this->capacity_ - 1));
            this->head_->store(next_head, std::memory_order_release);
            
            // Set HasSpace flag and wake waiters
            if (this->waitset_ != nullptr) {
                WaitSetHelper::SetFlagsAndWake(this->waitset_, EventFlag::kHasSpace);
                
                // Clear HasData if now empty
                if (next_head == tail) {
                    WaitSetHelper::ClearFlags(this->waitset_, EventFlag::kHasData);
                }
            }
            
            return Result<T>(std::move(value));
        }
        
        /**
         * @brief Peek at next value without consuming
         * @return Optional value (empty if channel empty)
         */
        Optional<T> Peek() const noexcept
        {
            if (!this->IsValid()) {
                return {};
            }
            
            UInt16 head = this->head_->load(std::memory_order_relaxed);
            UInt16 tail = this->tail_->load(std::memory_order_acquire);
            
            if (head == tail) {
                return {};  // Channel empty
            }
            
            return this->buffer_[head];
        }
    };
    
}  // namespace ipc
}  // namespace core
}  // namespace lap

#endif  // LAP_CORE_IPC_CHANNEL_HPP
