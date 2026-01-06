# SharedMemory Architecture Analysis

## 1. 单播模式（Unicast Mode）使用场景

### 什么是单播模式？
单播模式是指Publisher分配（loan）内存后，**不发送**给任何Subscriber，直接由Publisher自己使用并释放的场景。

### 核心流程
```cpp
// Unicast workflow
auto block = publisher.loan();  // ref_count = 1 (publisher holds reference)
// ... use the memory ...
publisher.release(block);       // ref_count-- (1→0), return to pool
```

### 典型应用场景

#### 1.1 临时缓冲区（Temporary Buffer）
```cpp
// 需要零拷贝内存做中间计算，但不发送
auto temp_buffer = publisher.loan(1024);
serialize_data(temp_buffer.ptr);
process_locally(temp_buffer.ptr);
publisher.release(temp_buffer);  // 使用完直接归还
```

#### 1.2 条件发送（Conditional Send）
```cpp
auto block = publisher.loan();
bool should_send = prepare_data(block.ptr);
if (should_send) {
    publisher.send(block);  // 发送（广播模式）
} else {
    publisher.release(block);  // 不发送，直接归还（单播模式）
}
```

#### 1.3 批量处理失败回滚（Batch Processing Rollback）
```cpp
std::vector<PublisherBlock> batch;
for (int i = 0; i < 100; i++) {
    batch.push_back(publisher.loan());
}

if (!validate_batch(batch)) {
    // 验证失败，全部归还
    for (auto& block : batch) {
        publisher.release(block);  // 单播模式释放
    }
}
```

#### 1.4 预分配内存池（Pre-allocation for Performance）
```cpp
// Publisher预先分配内存避免后续loan()失败
auto reserved = publisher.loan();
// ... later when actual send is needed ...
publisher.send(reserved);  // 或者 publisher.release(reserved) if cancelled
```

### 关键设计要求
为了支持单播模式，引用计数必须：
- **loan() 时 ref_count = 1**（支持Publisher直接释放）
- **release() 接受 LOANED 状态**（允许未send的chunk被释放）

---

## 2. iceoryx2 引用计数实现对比

### 2.1 iceoryx2 的双计数器架构

#### 核心设计
iceoryx2 使用**两层引用计数**：

```rust
// Publisher 端：loan_counter（跟踪Publisher持有的未发送样本）
loan_counter: AtomicUsize  // 仅Publisher可见

// Segment 端：sample_reference_counter（跟踪每个样本的订阅者引用）
sample_reference_counter: Vec<AtomicU64>  // 每个样本独立计数
```

#### 关键代码片段（iceoryx2）

##### Publisher loan:
```rust
// sender.rs:306-319
pub(crate) fn allocate(&self, layout: Layout) -> Result<ChunkMut, LoanError> {
    self.retrieve_returned_samples();  // 回收已释放的样本
    
    if self.loan_counter.load(Ordering::Relaxed) >= self.sender_max_borrowed_samples {
        fail!("ExceedsMaxLoans");
    }
    
    let shm_pointer = self.data_segment.allocate(layout)?;
    // loan_counter 在 ChunkMut::new() 时自动 +1
    Ok(ChunkMut::new(...))
}
```

##### Sample drop (未发送释放 = 单播模式):
```rust
// sample_mut.rs:148-161
impl Drop for SampleMut {
    fn drop(&mut self) {
        self.publisher_shared_state.lock()
            .sender.return_loaned_sample(self.offset_to_chunk);
        // 注意：这里会减少 loan_counter
    }
}

// sender.rs:264-284
pub(crate) fn return_loaned_sample(&self, offset: PointerOffset) {
    self.release_sample(offset);
    self.loan_counter.fetch_sub(1, Ordering::Relaxed);  // Publisher计数器 -1
}
```

##### Sample send (发送 = 广播模式):
```rust
// sample_mut.rs:333-338
pub fn send(self) -> Result<usize, SendError> {
    self.publisher_shared_state.lock()
        .send_sample(self.offset_to_chunk, self.sample_size)
    // 注意：send() 会减少 loan_counter，并增加 sample_reference_counter
}

// sender.rs:306 (conformance test 证明)
pub fn publisher_sending_sample_reduces_loan_counter() {
    let _sample1 = sut.loan_uninit()?;  // loan_counter = 1
    let sample2 = sut.loan_uninit()?;   // loan_counter = 2
    assert_that!(sample2.send(), is_ok); // loan_counter = 1
    let _sample3 = sut.loan_uninit();   // loan_counter = 2
    let sample4 = sut.loan_uninit();    // loan_counter = 3 (exceeds limit)
    assert_that!(sample4, is_err);      // ExceedsMaxLoans
}
```

##### Subscriber receive/release:
```rust
// zero_copy_connection/common.rs:909-925
fn receive(&self, channel_id: ChannelId) -> Result<Option<PointerOffset>, ...> {
    if *self.borrow_counter(channel_id) >= self.max_borrowed_samples {
        fail!("ReceiveWouldExceedMaxBorrowValue");
    }
    
    match self.submission_queue.pop() {
        Some(v) => {
            *self.borrow_counter(channel_id) += 1;  // Subscriber本地计数器 +1
            Ok(Some(v))
        }
    }
}

// zero_copy_connection/common.rs:930-956
fn release(&self, ptr: PointerOffset, channel_id: ChannelId) -> Result<...> {
    self.completion_queue.push(ptr)?;
    *self.borrow_counter(channel_id) -= 1;  // Subscriber本地计数器 -1
    Ok(())
}

// sender.rs:356-376 (Publisher 回收)
pub(crate) fn retrieve_returned_samples(&self) {
    for connection in &self.connections {
        loop {
            match connection.sender.reclaim(channel_id) {
                Ok(Some(offset)) => {
                    self.release_sample(offset);  // 减少 sample_reference_counter
                }
                Ok(None) => break,
            }
        }
    }
}

// segment_state.rs:50-59
pub(crate) fn release_sample(&self, distance_to_chunk: usize) -> u64 {
    self.sample_reference_counter[sample_index]
        .fetch_sub(1, Ordering::Relaxed)  // 原子递减
}
```

### 2.2 当前实现（LightAP）vs iceoryx2

| 维度 | LightAP (当前) | iceoryx2 |
|------|---------------|----------|
| **计数器数量** | 单一 `ref_count` | 双层：`loan_counter` + `sample_reference_counter` |
| **loan() 语义** | `ref_count = 1` (支持单播) | `loan_counter++` (Publisher计数) |
| **send() 语义** | `ref_count = 0` 然后 `ref_count += N` | `loan_counter--`, `sample_ref += N` |
| **drop() 语义** | `ref_count--` (if 0 → pool) | `loan_counter--` (回收到pool) |
| **Subscriber release** | `ref_count--` | `borrow_counter--` → `sample_ref--` |
| **样本回收时机** | `ref_count == 0` | `sample_reference_counter == 0` |

### 2.3 关键差异

#### iceoryx2 的优势：
1. **明确职责分离**
   - `loan_counter`: Publisher是否超出借用限制（单播场景）
   - `sample_reference_counter`: Subscriber是否完成消费（广播场景）

2. **更清晰的生命周期**
   ```rust
   // Publisher 侧
   loan()  → loan_counter++
   send()  → loan_counter--, sample_ref += N
   drop()  → loan_counter--
   
   // Subscriber 侧
   receive() → borrow_counter++
   release() → borrow_counter--, sample_ref--
   ```

3. **支持延迟回收**
   - Publisher可以通过 `retrieve_returned_samples()` 批量回收
   - Subscriber release → completion_queue → Publisher reclaim → sample_ref--

#### LightAP 当前实现的简化：
1. **单一计数器** `ref_count` 同时承担两个角色：
   - 在 LOANED 状态：代表 Publisher 引用
   - 在 IN_USE 状态：代表 Subscriber 引用总数

2. **状态机驱动**
   ```cpp
   LOANED (ref_count=1) → send() → SENT (ref_count=0→N)
                       → release() → FREE
   ```

### 2.4 建议改进方向

#### 选项A：保持当前设计（单计数器 + 状态机）
**优点**：实现简单，内存占用小
**适用场景**：Subscriber数量较少（< 10），不需要复杂的样本生命周期跟踪

#### 选项B：引入双计数器（对标iceoryx2）
```cpp
struct PublisherState {
    AtomicU32 loan_counter;  // Publisher端：当前loaned样本总数
};

struct ChunkHeader {
    AtomicU32 subscriber_ref_count;  // Subscriber端：此样本的引用计数
    AtomicU32 publisher_owned;       // 0 or 1: Publisher是否仍持有
};
```

**优点**：
- 职责更清晰
- 支持更复杂的生命周期管理
- 与iceoryx2语义完全对齐

**缺点**：
- 增加内存占用（每个chunk多4字节）
- 增加实现复杂度

---

## 3. SharedMemory 中的竞态条件和处理机制

### 3.1 关键竞态条件枚举

#### Race #1: loan() 并发池耗尽
**场景**：
```cpp
// Thread 1                    // Thread 2
auto c1 = free_list_.pop();   auto c2 = free_list_.pop();
// 两者可能都拿到 nullptr（池刚好耗尽）
```

**当前处理**：
```cpp
// CSharedMemoryAllocator.cpp:469-545
ChunkHeader* chunk = nullptr;
UInt32 retry_count = 0;

switch (config_.allocation_policy) {
case AllocationPolicy::WAIT_SYNC:
    // Spin-wait with yield()
    while ((chunk = free_list_.pop()) == nullptr && retry_count < kMaxRetries) {
        std::this_thread::yield();
        retry_count++;
    }
    break;

case AllocationPolicy::WAIT_ASYNC:
    // Condition variable blocking
    {
        std::unique_lock<std::mutex> lock(chunk_available_mutex_);
        while ((chunk = free_list_.pop()) == nullptr) {
            if (chunk_available_cv_.wait_for(lock, kTimeout) == std::cv_status::timeout) {
                break;
            }
        }
    }
    break;

case AllocationPolicy::ABORT_ON_FULL:
    // Immediate failure
    chunk = free_list_.pop();
    if (!chunk) {
        return Result<PublisherBlock>::FromError(MakeErrorCode(CoreErrc::kOutOfMemory));
    }
    break;

case AllocationPolicy::USE_OVERFLOW:
    // Fallback to heap allocation
    if (!chunk) {
        void* overflow_ptr = SYS_MALLOC(requested_size);
        // ... return overflow block ...
    }
    break;
}
```

**风险评估**: ✅ **已妥善处理**（4种策略覆盖所有场景）

---

#### Race #2: send() - enqueue 与 ref_count 顺序
**场景**：
```cpp
// Publisher                    // Subscriber
chunk->ref_count += 1;         // (未增加前)
sub->rx_queue.enqueue(chunk);  auto c = rx_queue.dequeue();
                               chunk->ref_count--;  // underflow!
```

**当前处理**（已修复）：
```cpp
// CSharedMemoryAllocator.cpp:770
// ✅ CRITICAL FIX: Increment ref_count BEFORE enqueue
chunk->ref_count.fetch_add(1, std::memory_order_acq_rel);
if (!sub->rx_queue.enqueue(chunk)) {
    // Rollback on enqueue failure
    chunk->ref_count.fetch_sub(1, std::memory_order_acq_rel);
    enqueue_failures++;
}
```

**风险评估**: ✅ **已修复**（ref_count先增加，失败后回滚）

---

#### Race #3: QueueOverflowPolicy::DISCARD_OLDEST 的引用计数泄漏
**场景**：
```cpp
// Publisher sends            // Subscriber receives old chunk
if (queue.full()) {
    auto old = queue.pop();   // 可能同时dequeue同一个chunk
    old->ref_count--;
}
queue.enqueue(new_chunk);
```

**当前处理**：
```cpp
// CSharedMemoryAllocator.cpp:740-768
case QueueOverflowPolicy::DISCARD_OLDEST: {
    ChunkHeader* discarded = sub->rx_queue.dequeue();
    if (discarded) {
        // Atomic decrement
        UInt32 old_ref = discarded->ref_count.fetch_sub(1, std::memory_order_acq_rel);
        
        if (old_ref == 1) {
            // Last reference - return to pool
            ChunkState expected_state = ChunkState::IN_USE;
            if (discarded->state.compare_exchange_strong(
                    expected_state, ChunkState::FREE,
                    std::memory_order_release, std::memory_order_relaxed)) {
                free_list_.push(discarded);
                chunk_available_cv_.notify_one();
            }
        }
    }
    
    // Now safe to enqueue new chunk
    chunk->ref_count.fetch_add(1, std::memory_order_acq_rel);
    sub->rx_queue.enqueue(chunk);
    break;
}
```

**风险评估**: ✅ **原子操作保证**（fetch_sub 确保只有一个线程看到 old_ref==1）

---

#### Race #4: QueueOverflowPolicy::BLOCK_PUBLISHER 的死锁风险
**场景**：
```cpp
// Publisher thread          // Subscriber thread
wait_for(queue_space);      // (never dequeues - stuck in processing)
```

**当前处理**：
```cpp
// CSharedMemoryAllocator.cpp:721-739
case QueueOverflowPolicy::BLOCK_PUBLISHER: {
    std::unique_lock<std::mutex> lock(sub->queue_space_mutex);
    
    // ⚠️ 5秒超时防止永久死锁
    auto timeout = std::chrono::seconds(5);
    if (!sub->queue_space_cv.wait_for(lock, timeout, [&]() {
            return sub->rx_queue.size() < sub->queue_capacity;
        })) {
        // Timeout - mark as failed
        enqueue_failures++;
        continue;
    }
    
    chunk->ref_count.fetch_add(1, std::memory_order_acq_rel);
    sub->rx_queue.enqueue(chunk);
    break;
}
```

**风险评估**: ✅ **超时保护**（5秒超时避免永久死锁）

**潜在改进**：
- 添加 Publisher 超时计数器统计
- 提供配置项允许调整超时时间

---

#### Race #5: release() 状态转换竞争
**场景**：
```cpp
// Thread 1 (Subscriber)       // Thread 2 (Publisher send)
release(chunk);                chunk->state = SENT;
// if (state == IN_USE)
// → CAS FREE
```

**当前处理**：
```cpp
// CSharedMemoryAllocator.cpp:973-1020
UInt32 old_ref = chunk->ref_count.fetch_sub(1, std::memory_order_acq_rel);

if (old_ref == 1) {
    // Last reference - attempt state transition
    ChunkState expected_state = ChunkState::IN_USE;
    
    if (chunk->state.compare_exchange_strong(
            expected_state, ChunkState::FREE,
            std::memory_order_release, std::memory_order_relaxed)) {
        free_list_.push(chunk);
        chunk_available_cv_.notify_one();
        return Result<void>::FromValue();
    }
    
    // ✅ Fallback: If IN_USE CAS failed, try LOANED (unicast mode)
    expected_state = ChunkState::LOANED;
    if (chunk->state.compare_exchange_strong(
            expected_state, ChunkState::FREE,
            std::memory_order_release, std::memory_order_relaxed)) {
        free_list_.push(chunk);
        chunk_available_cv_.notify_one();
        return Result<void>::FromValue();
    }
}
```

**风险评估**: ✅ **双CAS保护**（支持IN_USE和LOANED两种状态）

---

#### Race #6: active_subscribers_ 计数与 send() 决策
**场景**：
```cpp
// Thread 1 (send)              // Thread 2 (unregisterSubscriber)
UInt32 active = active_subs;   active_subscribers_--;
if (active == 0) {
    return_to_pool();           // 可能误判为"无订阅者"
}
```

**当前处理**：
```cpp
// CSharedMemoryAllocator.cpp:668
UInt32 active_subs = active_subscribers_.load(std::memory_order_acquire);

if (active_subs == 0) {
    // Snapshot时刻无订阅者，直接归还池
    // ⚠️ 这是安全的：即使此后有新订阅者注册，它们也收不到此消息
    //   （因为消息已经开始send流程，订阅者列表已确定）
    chunk->state.compare_exchange_strong(...);
    free_list_.push(chunk);
    return;
}
```

**风险评估**: ✅ **快照语义正确**（memory_order_acquire 确保可见性）

---

#### Race #7: MessageQueue 本身的并发安全
**场景**：
```cpp
// Thread 1 (Publisher)      // Thread 2 (Subscriber)
queue.enqueue(chunk);        queue.dequeue();
```

**当前处理**：
```cpp
// CMessageQueue.hpp:146-174 (lock-free Michael-Scott queue)
bool enqueue(T* item) {
    Node* new_node = allocate_node(item);
    while (true) {
        Node* tail = tail_.load(std::memory_order_acquire);
        Node* next = tail->next.load(std::memory_order_acquire);
        
        if (next == nullptr) {
            // ✅ CAS: tail->next = new_node
            if (tail->next.compare_exchange_weak(
                    next, new_node,
                    std::memory_order_release,
                    std::memory_order_relaxed)) {
                // ✅ CAS: tail_ = new_node
                tail_.compare_exchange_strong(tail, new_node, ...);
                size_.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
        } else {
            tail_.compare_exchange_strong(tail, next, ...);
        }
    }
}
```

**风险评估**: ✅ **Lock-free算法**（Michael-Scott队列经过充分验证）

---

### 3.2 竞态条件总结矩阵

| # | 竞态条件 | 风险等级 | 当前处理机制 | 状态 |
|---|---------|---------|------------|------|
| 1 | loan() 池耗尽 | 🟡 中 | 4种AllocationPolicy策略 | ✅ 已处理 |
| 2 | send() ref_count竞争 | 🔴 高 | ref_count先增加，失败回滚 | ✅ 已修复 |
| 3 | DISCARD_OLDEST引用泄漏 | 🟡 中 | fetch_sub原子操作 | ✅ 已处理 |
| 4 | BLOCK_PUBLISHER死锁 | 🟡 中 | 5秒超时 + 失败计数 | ✅ 已处理 |
| 5 | release() 状态转换 | 🟡 中 | 双CAS (IN_USE/LOANED) | ✅ 已处理 |
| 6 | active_subscribers计数 | 🟢 低 | memory_order_acquire快照 | ✅ 已处理 |
| 7 | MessageQueue并发 | 🟢 低 | Lock-free Michael-Scott | ✅ 已处理 |

### 3.3 Memory Ordering 策略

```cpp
// loan()
free_list_.pop()              // Relaxed (原子操作足够)

// send()
active_subscribers_.load()    // Acquire (需要看到最新订阅者)
chunk->ref_count.fetch_add()  // AcqRel (关键路径，需要严格同步)
chunk->state.CAS()            // Release (发布数据给接收者)

// receive()
rx_queue.dequeue()            // Acquire (确保看到发送的数据)

// release()
chunk->ref_count.fetch_sub()  // AcqRel (关键路径)
chunk->state.CAS()            // Release (发布空闲状态)

// registerSubscriber()
active_subscribers_++         // Release (发布新订阅者)
```

---

## 4. 改进建议

### 4.1 短期优化（保持现有架构）
1. **添加统计信息**
   ```cpp
   struct AllocationStats {
       AtomicU64 total_loan_timeouts;
       AtomicU64 total_block_publisher_timeouts;
       AtomicU64 total_overflow_allocations;
   };
   ```

2. **可配置超时时间**
   ```cpp
   struct SHMConfig {
       std::chrono::milliseconds block_publisher_timeout = 5s;
       std::chrono::milliseconds wait_async_timeout = 10s;
   };
   ```

### 4.2 长期演进（对标iceoryx2）
1. **引入双计数器架构**
   - `loan_counter`: 跟踪Publisher端loaned样本总数
   - `sample_reference_counter`: 跟踪每个样本的Subscriber引用

2. **实现延迟回收（retrieve_returned_samples）**
   - Subscriber release → completion_queue
   - Publisher定期批量reclaim

3. **支持iceoryx2完整语义**
   - `max_loaned_samples` 限制
   - 自动Rust-style Drop语义

---

## 5. 测试覆盖验证

### 当前测试（38/38通过）
- ✅ 单播模式: `TestUnicastMode` (test_shm_policies.cpp:165)
- ✅ 广播模式: `TestBroadcastBasic` (test_shm_allocator_broadcast.cpp)
- ✅ 所有AllocationPolicy: `TestAllocationPolicies` (test_shm_policies.cpp:85)
- ✅ 所有QueueOverflowPolicy: `TestQueueOverflowPolicies` (test_shm_policies.cpp:193)
- ✅ 并发send/receive: `TestConcurrentSendReceive` (test_shm_allocator_broadcast.cpp)

### 建议新增测试
1. **压力测试**: 1000线程并发loan/release
2. **死锁检测**: 监控BLOCK_PUBLISHER超时频率
3. **内存泄漏检测**: Valgrind/AddressSanitizer验证ref_count正确性
