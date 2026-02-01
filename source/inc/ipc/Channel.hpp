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
#include "CTypedef.hpp"
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
            , active_(nullptr)
            , stmin_(nullptr)
            , mutex_(nullptr)
        {
        }
        
        /**
         * @brief Constructor with shared memory references
         * @param head Consumer index pointer
         * @param tail Producer index pointer
         * @param waitset Event flags pointer (optional, can be nullptr)
         * @param buffer Ring buffer storage pointer
         * @param capacity Buffer capacity (must be power of 2)
         * @param active Active state pointer (optional, can be nullptr)
         * @param stmin STMin pointer (optional, can be nullptr)
         * @param mutex Mutex pointer for synchronization (optional, can be nullptr)
         */
        Channel(Atomic<UInt16>* head,
                Atomic<UInt16>* tail,
                Atomic<UInt32>* waitset,
                T* buffer,
                UInt16 capacity,
                Atomic<Bool>* active = nullptr,
                Atomic<UInt16>* stmin = nullptr,
                Atomic<Bool>* mutex = nullptr) noexcept
            : head_(head)
            , tail_(tail)
            , waitset_(waitset)
            , buffer_(buffer)
            , capacity_(capacity)
            , active_(active)
            , stmin_(stmin)
            , mutex_(mutex)
        {
            ;
        }
        
        /**
         * @brief Destructor
         */
        virtual ~Channel() noexcept = default;
        
        // Delete copy, allow move
        Channel(const Channel&) = delete;
        Channel& operator=(const Channel&) = delete;
        
        /**
         * @brief Move constructor
         */
        Channel(Channel&& other) noexcept
            : head_(other.head_)
            , tail_(other.tail_)
            , waitset_(other.waitset_)
            , buffer_(other.buffer_)
            , capacity_(other.capacity_)
            , active_(other.active_)
            , stmin_(other.stmin_)
            , mutex_(other.mutex_)
        {
            other.Reset();
        }
        
        /**
         * @brief Move assignment operator
         */
        Channel& operator=(Channel&& other) noexcept
        {
            if (this != &other) {
                head_ = other.head_;
                tail_ = other.tail_;
                waitset_ = other.waitset_;
                buffer_ = other.buffer_;
                capacity_ = other.capacity_;
                active_ = other.active_;
                stmin_ = other.stmin_;
                mutex_ = other.mutex_;
                
                other.Reset();
            }
            return *this;
        }

        void Reset() noexcept
        {
            head_ = nullptr;
            tail_ = nullptr;
            waitset_ = nullptr;
            buffer_ = nullptr;
            capacity_ = 0;
            active_ = nullptr;
            stmin_ = nullptr;
            mutex_ = nullptr;
        }
        
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
        
        // =====================================================================
        // Virtual Read/Write Interface
        // =====================================================================
        
        /**
         * @brief Write value to channel (pure virtual)
         * @param value Value to write
         * @return Result with void on success or error code
         */
        virtual Result<void> Write(const T& value) noexcept = 0;
        
        /**
         * @brief Write value with policy (pure virtual)
         * @param value Value to write
         * @param policy Write policy
         * @param timeout_ns Timeout in nanoseconds
         * @return Result with void on success or error code
         */
        virtual Result<void> WriteWithPolicy(const T& value, PublishPolicy policy, UInt64 timeout_ns = 0) noexcept = 0;
        
        /**
         * @brief Read value from channel (pure virtual)
         * @return Result with value on success or error code
         */
        virtual Result<T> Read() noexcept = 0;
        
        /**
         * @brief Read value with policy (pure virtual)
         * @param policy Read policy
         * @param timeout_ns Timeout in nanoseconds
         * @return Result with value on success or error code
         */
        virtual Result<T> ReadWithPolicy(SubscribePolicy policy, UInt64 timeout_ns = 0) noexcept = 0;
        
        /**
         * @brief Peek at next value without consuming (pure virtual)
         * @return Optional value
         */
        virtual Optional<T> Peek() const noexcept = 0;
        
        /**
         * @brief Check if channel is active
         * @return true if channel is active
         */
        inline Bool IsActive() const noexcept
        {
            DEF_LAP_ASSERT( active_ != nullptr, "Active state pointer is null" );

            return active_->load(std::memory_order_acquire);
        }
        
        /**
         * @brief Set channel active state
         * @param active Active state to set
         */
        void SetActive(Bool active) noexcept
        {
            DEF_LAP_ASSERT( active_ != nullptr, "Active state pointer is null" );

            active_->store(active, std::memory_order_release);
        }
        
        /**
         * @brief Get STMin value (minimum send interval in microseconds)
         * @return STMin value, or 0 if pointer is null
         */
        inline UInt16 GetSTMin() const noexcept
        {
            DEF_LAP_ASSERT( active_ != nullptr, "Active state pointer is null" );

            return stmin_->load(std::memory_order_acquire);
        }
        
        /**
         * @brief Set STMin value (minimum send interval in microseconds)
         * @param stmin STMin value to set
         */
        void SetSTMin(UInt16 stmin) noexcept
        {
            DEF_LAP_ASSERT( active_ != nullptr, "Active state pointer is null" );

            stmin_->store(stmin, std::memory_order_release);
        }
        
        /**
         * @brief Try to acquire channel lock (non-blocking)
         * @return true if lock acquired, false if already locked
         * @details Uses atomic flag for lock-free synchronization
         */
        inline Bool TryLock() noexcept
        {
            DEF_LAP_ASSERT( mutex_ != nullptr, "Mutex pointer is null" );
            
            Bool expected = false;
            return mutex_->compare_exchange_strong(expected, true, 
                                                  std::memory_order_acquire,
                                                  std::memory_order_relaxed);
        }
        
        /**
         * @brief Release channel lock
         * @details Must be called after successful TryLock
         */
        inline void Unlock() noexcept
        {
            DEF_LAP_ASSERT( mutex_ != nullptr, "Mutex pointer is null" );
            
            mutex_->store(false, std::memory_order_release);
        }

    protected:
        Atomic<UInt16>*     head_;      ///< Consumer index pointer
        Atomic<UInt16>*     tail_;      ///< Producer index pointer
        Atomic<UInt32>*     waitset_;   ///< Event flags pointer
        T*                  buffer_;    ///< Ring buffer storage pointer
        UInt16              capacity_;  ///< Buffer capacity (power of 2)
        Atomic<Bool>*       active_;    ///< Channel active state pointer (in shared memory)
        Atomic<UInt16>*     stmin_;     ///< STMin pointer (minimum send interval in microseconds, in shared memory)
        Atomic<Bool>*       mutex_;     ///< Mutex for channel synchronization (optional, in shared memory)
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
         * @param active Active state pointer (optional)
         * @param stmin STMin pointer (optional)
         */
        WriteChannel(Atomic<UInt16>* head,
                     Atomic<UInt16>* tail,
                     Atomic<UInt32>* waitset,
                     T* buffer,
                     UInt16 capacity,
                     Atomic<Bool>* active = nullptr,
                     Atomic<UInt16>* stmin = nullptr,
                     Atomic<Bool>* mutex = nullptr) noexcept
            : Channel<T>(head, tail, waitset, buffer, capacity, active, stmin, mutex)
        {
        }
        
        /**
         * @brief Destructor
         */
        ~WriteChannel() noexcept override = default;
        
        // Delete copy, allow move
        WriteChannel(const WriteChannel&) = delete;
        WriteChannel& operator=(const WriteChannel&) = delete;
        
        /**
         * @brief Move constructor
         */
        WriteChannel(WriteChannel&& other) noexcept
            : Channel<T>(std::move(other))
        {
        }
        
        /**
         * @brief Move assignment operator
         */
        WriteChannel& operator=(WriteChannel&& other) noexcept
        {
            if (this != &other) {
                Channel<T>::operator=(std::move(other));
            }
            return *this;
        }
        
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
        Result<void> Write(const T& value) noexcept override
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
        Result<void> WriteWithPolicy(const T& value, PublishPolicy policy, UInt64 timeout_ns = 0) noexcept override
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
                            std::chrono::nanoseconds timeout = (timeout_ns > 0) 
                                ? std::chrono::nanoseconds(timeout_ns) 
                                : std::chrono::nanoseconds::zero();  // 0 = infinite
                            
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
                                return Result<void>(MakeErrorCode(CoreErrc::kChannelFull));
                            }
                        }
                        break;
                        
                    case PublishPolicy::kWait:
                        // Busy-wait polling for kHasSpace flag
                        if (this->waitset_ == nullptr) {
                            return Result<void>(MakeErrorCode(CoreErrc::kChannelWaitsetUnavailable));
                        }
                        
                        {
                            std::chrono::nanoseconds timeout = (timeout_ns > 0) 
                                ? std::chrono::nanoseconds(timeout_ns) 
                                : std::chrono::nanoseconds(10000000);  // Default 10ms for polling
                            
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
        
        /**
         * @brief Read operation not supported in WriteChannel
         * @return Error indicating write-only channel
         */
        Result<T> Read() noexcept override
        {
            return Result<T>(MakeErrorCode(CoreErrc::kInvalidArgument));
        }
        
        /**
         * @brief Read with policy not supported in WriteChannel
         * @return Error indicating write-only channel
         */
        Result<T> ReadWithPolicy(SubscribePolicy /*policy*/, UInt64 /*timeout_ns*/ = 0) noexcept override
        {
            return Result<T>(MakeErrorCode(CoreErrc::kInvalidArgument));
        }
        
        /**
         * @brief Peek operation not supported in WriteChannel
         * @return Empty optional
         */
        Optional<T> Peek() const noexcept override
        {
            return {};
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
         * @param active Active state pointer (optional)
         * @param stmin STMin pointer (optional)
         */
        ReadChannel(Atomic<UInt16>* head,
                    Atomic<UInt16>* tail,
                    Atomic<UInt32>* waitset,
                    T* buffer,
                    UInt16 capacity,
                    Atomic<Bool>* active = nullptr,
                    Atomic<UInt16>* stmin = nullptr) noexcept
            : Channel<T>(head, tail, waitset, buffer, capacity, active, stmin)
        {
        }
        
        /**
         * @brief Destructor
         */
        ~ReadChannel() noexcept override = default;
        
        // Delete copy, allow move
        ReadChannel(const ReadChannel&) = delete;
        ReadChannel& operator=(const ReadChannel&) = delete;
        
        /**
         * @brief Move constructor
         */
        ReadChannel(ReadChannel&& other) noexcept
            : Channel<T>(std::move(other))
        {
        }
        
        /**
         * @brief Move assignment operator
         */
        ReadChannel& operator=(ReadChannel&& other) noexcept
        {
            if (this != &other) {
                Channel<T>::operator=(std::move(other));
            }
            return *this;
        }
        
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
        Result<T> Read() noexcept override
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
                UInt32 current_flags = this->waitset_->load(std::memory_order_relaxed);
                this->waitset_->store(current_flags | EventFlag::kHasSpace, std::memory_order_release);
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
        Result<T> ReadWithPolicy(SubscribePolicy policy, UInt64 timeout_ns = 0) noexcept override
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
                            std::chrono::nanoseconds timeout = (timeout_ns > 0) 
                                ? std::chrono::nanoseconds(timeout_ns) 
                                : std::chrono::nanoseconds::zero();  // 0 = infinite
                            
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
                                return Result<T>(MakeErrorCode(CoreErrc::kChannelEmpty));
                            }
                        }
                        break;
                        
                    case SubscribePolicy::kWait:
                        // Busy-wait polling for kHasData flag
                        if (this->waitset_ == nullptr) {
                            return Result<T>(MakeErrorCode(CoreErrc::kChannelWaitsetUnavailable));
                        }
                        
                        {
                            std::chrono::nanoseconds timeout = ( timeout_ns > 0 ) 
                                ? std::chrono::nanoseconds( timeout_ns ) 
                                : std::chrono::nanoseconds( 10000000 );  // Default 10ms for polling
                            
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
        Optional<T> Peek() const noexcept override
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
        
        /**
         * @brief Write operation not supported in ReadChannel
         * @return Error indicating read-only channel
         */
        Result<void> Write(const T& /*value*/) noexcept override
        {
            return Result<void>(MakeErrorCode(CoreErrc::kInvalidArgument));
        }
        
        /**
         * @brief Write with policy not supported in ReadChannel
         * @return Error indicating read-only channel
         */
        Result<void> WriteWithPolicy(const T& /*value*/, PublishPolicy /*policy*/, UInt64 /*timeout_ns*/ = 0) noexcept override
        {
            return Result<void>(MakeErrorCode(CoreErrc::kInvalidArgument));
        }
    };
    
    // =========================================================================
    // Channel Factory
    // =========================================================================
    
    /**
     * @brief Factory class for creating Channel instances
     * @details Provides static factory methods to create WriteChannel and ReadChannel
     *          Returns can be stored as base Channel<T> reference/pointer for polymorphism
     * 
     * Usage:
     * @code
     *   // Create write-only channel
     *   auto write_ch = ChannelFactory<UInt16>::CreateWriteChannel(head, tail, waitset, buffer, capacity);
     *   
     *   // Create read-only channel  
     *   auto read_ch = ChannelFactory<UInt16>::CreateReadChannel(head, tail, waitset, buffer, capacity);
     *   
     *   // Use through base class pointer
     *   auto result = write_ch->Write(42);
     *   
     *   // Move ownership
     *   UniqueHandle<Channel<UInt16>> ch = std::move(write_ch);
     * @endcode
     */
    template<typename T>
    class ChannelFactory final
    {
    public:
        /**
         * @brief Create a write-only channel
         * @param head Consumer index pointer (in shared memory)
         * @param tail Producer index pointer (in shared memory)
         * @param waitset Event flags pointer (optional, can be nullptr)
         * @param buffer Ring buffer storage pointer (in shared memory)
         * @param capacity Buffer capacity (must be power of 2)
         * @return UniqueHandle to Channel<T> (holding WriteChannel)
         * 
         * @details Creates a WriteChannel that:
         * - Can write values via Write() and WriteWithPolicy()
         * - Returns error on Read() operations
         * - Does not own shared memory (only references it)
         */
        static UniqueHandle<Channel<T>> CreateWriteChannel(
            Atomic<UInt16>* head,
            Atomic<UInt16>* tail,
            Atomic<UInt32>* waitset,
            T* buffer,
            UInt16 capacity,
            Atomic<Bool>* active = nullptr,
            Atomic<UInt16>* stmin = nullptr,
            Atomic<Bool>* mutex = nullptr ) noexcept
        {
            return MakeUnique<WriteChannel<T>>(head, tail, waitset, buffer, capacity, active, stmin, mutex);
        }
        
        /**
         * @brief Create a read-only channel
         * @param head Consumer index pointer (in shared memory)
         * @param tail Producer index pointer (in shared memory)
         * @param waitset Event flags pointer (optional, can be nullptr)
         * @param buffer Ring buffer storage pointer (in shared memory)
         * @param capacity Buffer capacity (must be power of 2)
         * @return UniqueHandle to Channel<T> (holding ReadChannel)
         * 
         * @details Creates a ReadChannel that:
         * - Can read values via Read(), ReadWithPolicy(), and Peek()
         * - Returns error on Write() operations
         * - Does not own shared memory (only references it)
         */
        static UniqueHandle<Channel<T>> CreateReadChannel(
            Atomic<UInt16>* head,
            Atomic<UInt16>* tail,
            Atomic<UInt32>* waitset,
            T* buffer,
            UInt16 capacity,
            Atomic<Bool>* active = nullptr,
            Atomic<UInt16>* stmin = nullptr) noexcept
        {
            return MakeUnique<ReadChannel<T>>(head, tail, waitset, buffer, capacity, active, stmin);
        }
        
        /**
         * @brief Create write channel from ChannelQueue
         * @param queue Pointer to ChannelQueue in shared memory
         * @return UniqueHandle to Channel<T> (holding WriteChannel), or nullptr if queue is null
         * 
         * @details Convenience method that extracts fields from ChannelQueue
         * and creates a WriteChannel. Assumes buffer follows ChannelQueue struct.
         */
        static UniqueHandle<Channel<T>> CreateWriteChannelFromQueue(ChannelQueue* queue) noexcept
        {
            if (queue == nullptr) {
                return nullptr;
            }
            
            return MakeUnique<WriteChannel<T>>(
                &queue->head,
                &queue->tail,
                &queue->queue_waitset,
                reinterpret_cast<T*>(queue->GetBuffer()),
                queue->capacity,
                &queue->active,
                &queue->STmin,
                &queue->mutex
            );
        }
        
        /**
         * @brief Create read channel from ChannelQueue
         * @param queue Pointer to ChannelQueue in shared memory
         * @return UniqueHandle to Channel<T> (holding ReadChannel), or nullptr if queue is null
         * 
         * @details Convenience method that extracts fields from ChannelQueue
         * and creates a ReadChannel. Assumes buffer follows ChannelQueue struct.
         */
        static UniqueHandle<Channel<T>> CreateReadChannelFromQueue(ChannelQueue* queue) noexcept
        {
            if (queue == nullptr) {
                return nullptr;
            }
            
            return MakeUnique<ReadChannel<T>>(
                &queue->head,
                &queue->tail,
                &queue->queue_waitset,
                reinterpret_cast<T*>(queue->GetBuffer()),
                queue->capacity,
                &queue->active,
                &queue->STmin
            );
        }
        
    private:
        // Factory class - no instantiation
        ChannelFactory() = delete;
        ~ChannelFactory() = delete;
        ChannelFactory(const ChannelFactory&) = delete;
        ChannelFactory& operator=(const ChannelFactory&) = delete;
    };
    
}  // namespace ipc
}  // namespace core
}  // namespace lap

#endif  // LAP_CORE_IPC_CHANNEL_HPP
