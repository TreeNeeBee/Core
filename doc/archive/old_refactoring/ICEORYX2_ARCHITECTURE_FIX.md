# iceoryx2 架构修正方案

## 问题诊断

### 当前实现（错误）
```
Publisher 1 ──> [Queue 1] ──┐
Publisher 2 ──> [Queue 2] ──┤ Round-robin dequeue ──> Subscriber 1
Publisher 3 ──> [Queue 3] ──┘                     └──> Subscriber 2
```

**问题**:
- 每个Publisher有自己的队列
- 多个Subscriber竞争从Publisher队列dequeue
- 一条消息只能被一个Subscriber接收（工作队列模式）
- **这不是发布-订阅模式，而是工作队列模式**

### 正确的iceoryx2设计
```
                  ┌──> [Sub 1 Queue] ──> Subscriber 1
Publisher 1 ──┬───┤
              │   └──> [Sub 2 Queue] ──> Subscriber 2
              │
Publisher 2 ──┤   ┌──> [Sub 1 Queue] ──> Subscriber 1
              └───┤
                  └──> [Sub 2 Queue] ──> Subscriber 2
```

**关键特性**:
1. **每个Subscriber有自己的接收队列**
2. **Publisher send时，广播到所有Subscriber队列**
3. **引用计数管理消息生命周期**
4. **只有所有Subscriber都release后，消息才回收**

---

## 架构重构方案

### 1. 数据结构调整

#### 修改：SubscriberState
```cpp
struct SubscriberState {
    std::atomic<bool> active{false};
    UInt32 subscriber_id{0};
    
    // 核心改动：每个订阅者有自己的接收队列
    MessageQueue rx_queue;  // 接收队列（多个Publisher推送到这里）
    
    // 统计信息
    std::atomic<uint64_t> total_received{0};
    std::atomic<uint64_t> total_released{0};
};
```

#### 修改：PublisherState
```cpp
struct PublisherState {
    std::atomic<bool> active{false};
    UInt32 publisher_id{0};
    
    // 移除 msg_queue（不再需要Publisher侧队列）
    
    // 统计信息
    std::atomic<uint64_t> total_sent{0};
};
```

#### 修改：ChunkHeader
```cpp
struct ChunkHeader {
    std::atomic<ChunkState> state;
    std::atomic<uint32_t>   ref_count;  // 引用计数（关键！）
    std::atomic<uint64_t>   sequence;
    Size                    payload_size;
    UInt64                  chunk_id;
    UInt32                  publisher_id;
    void*                   user_payload;
    ChunkHeader*            next_free;   // FREE状态时使用
    
    // 改动：每个Subscriber有独立的next指针数组
    // 或者使用链表的链表结构
    // 简化方案：每个chunk在多个subscriber队列中，使用引用计数
    ChunkHeader*            next_msg;    // 在Subscriber队列中使用
    
    // 新增：订阅者掩码（哪些订阅者已接收）
    std::atomic<uint64_t>   subscriber_mask{0};  // bit i = subscriber i received
};
```

### 2. 核心API重构

#### send() - 广播到所有Subscriber
```cpp
Result<void> SharedMemoryAllocator::send(const PublisherHandle& publisher, 
                                          SharedMemoryMemoryBlock& block) noexcept {
    ChunkHeader* chunk = static_cast<ChunkHeader*>(block.chunk_header);
    
    // 1. 获取当前活跃的订阅者数量
    uint32_t active_subs = active_subscribers_.load(std::memory_order_acquire);
    if (active_subs == 0) {
        // 无订阅者，直接回收
        releaseChunkToPool(chunk);
        return Result<void>::FromValue();
    }
    
    // 2. 设置初始引用计数 = 活跃订阅者数量
    chunk->ref_count.store(active_subs, std::memory_order_release);
    
    // 3. 转换状态：LOANED -> SENT
    ChunkState expected = ChunkState::LOANED;
    if (!chunk->state.compare_exchange_strong(expected, ChunkState::SENT, 
                                               std::memory_order_release,
                                               std::memory_order_relaxed)) {
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    }
    
    // 4. 广播：将消息推送到所有活跃Subscriber的队列中
    for (uint32_t i = 0; i < 64; ++i) {
        SubscriberState* sub = &subscribers_[i];
        if (sub->active.load(std::memory_order_acquire)) {
            // 将chunk推送到subscriber的接收队列
            // 注意：这里需要为每个subscriber创建独立的队列节点
            // 或者使用引用同一个chunk，通过ref_count管理
            sub->rx_queue.enqueue(chunk);
        }
    }
    
    return Result<void>::FromValue();
}
```

#### receive() - 从Subscriber自己的队列读取
```cpp
Result<void> SharedMemoryAllocator::receive(const SubscriberHandle& subscriber, 
                                             SharedMemoryMemoryBlock& block) noexcept {
    SubscriberState* sub = static_cast<SubscriberState*>(subscriber.internal_state);
    
    // 1. 从自己的接收队列dequeue
    ChunkHeader* chunk = sub->rx_queue.dequeue();
    if (!chunk) {
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kWouldBlock));
    }
    
    // 2. 转换状态：SENT -> IN_USE
    ChunkState expected = ChunkState::SENT;
    if (!chunk->state.compare_exchange_strong(expected, ChunkState::IN_USE,
                                               std::memory_order_acquire,
                                               std::memory_order_relaxed)) {
        // CAS失败，chunk已被其他操作处理
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kWouldBlock));
    }
    
    // 3. 填充block描述符
    block.ptr = chunk->user_payload;
    block.size = chunk->payload_size;
    block.chunk_id = chunk->chunk_id;
    block.chunk_header = chunk;
    block.is_loaned = false;
    block.owner_id = subscriber.subscriber_id;
    
    return Result<void>::FromValue();
}
```

#### release() - 引用计数递减
```cpp
Result<void> SharedMemoryAllocator::release(const SubscriberHandle& subscriber, 
                                             SharedMemoryMemoryBlock& block) noexcept {
    ChunkHeader* chunk = static_cast<ChunkHeader*>(block.chunk_header);
    
    // 1. 原子递减引用计数
    uint32_t prev_count = chunk->ref_count.fetch_sub(1, std::memory_order_acq_rel);
    
    // 2. 如果这是最后一个引用，回收chunk
    if (prev_count == 1) {
        // 最后一个subscriber释放了，chunk可以回收
        ChunkState expected = ChunkState::IN_USE;
        if (chunk->state.compare_exchange_strong(expected, ChunkState::FREE,
                                                  std::memory_order_release,
                                                  std::memory_order_relaxed)) {
            // 返回到free list
            pushChunkToFreeList(chunk);
        }
    }
    
    // 3. 清空block描述符
    block.ptr = nullptr;
    block.chunk_header = nullptr;
    block.is_loaned = false;
    
    return Result<void>::FromValue();
}
```

---

## 关键改动总结

### 队列位置
- **旧**: `PublisherState::msg_queue` ❌
- **新**: `SubscriberState::rx_queue` ✅

### send()行为
- **旧**: 推送到Publisher队列（单个队列）❌
- **新**: 广播到所有Subscriber队列（多个队列）✅

### receive()行为
- **旧**: Round-robin从多个Publisher队列dequeue ❌
- **新**: 从Subscriber自己的队列dequeue ✅

### 引用计数
- **旧**: 不使用ref_count（每条消息只有一个接收者）❌
- **新**: ref_count = 活跃订阅者数量，每次release递减 ✅

---

## 实现复杂度

### 挑战1：多队列enqueue
```cpp
// send()需要向N个subscriber队列enqueue
// 问题：如果某个enqueue失败怎么办？
for (each subscriber) {
    sub->rx_queue.enqueue(chunk);  // 可能失败
}
```

**解决方案**：
- 队列容量足够大（避免满）
- 或者使用无界队列（动态分配节点）

### 挑战2：队列节点内存管理
当前MessageQueue使用`ChunkHeader::next_msg`作为链表指针。
如果一个chunk在多个subscriber队列中，需要：

**方案A**: 每个subscriber队列独立的节点
```cpp
struct QueueNode {
    ChunkHeader* chunk;
    QueueNode* next;
};
```

**方案B**: Chunk本身在多个队列中（需要多个next指针）
```cpp
struct ChunkHeader {
    ChunkHeader* next_msg[64];  // 每个subscriber一个
};
```

**推荐方案A**（更简单，内存开销可控）

---

## 测试期望修正

### SOA Scenario
```cpp
// 旧期望（错误）
assert(logger_received + analytics_received == total_published);

// 新期望（正确）
assert(logger_received == total_published);      // logger收到全部
assert(analytics_received == total_published);   // analytics也收到全部
```

### Multi-topic
```cpp
// 旧期望（错误）
assert(sub_all + sub_ab + sub_bc == total_published);

// 新期望（正确）
assert(sub_all == total_published);  // 每个subscriber都收到全部
assert(sub_ab == total_published);
assert(sub_bc == total_published);
```

---

## 迁移步骤

1. ✅ **理解问题**（当前阶段）
2. ⬜ **重构数据结构**
   - 移动MessageQueue到SubscriberState
   - 删除PublisherState::msg_queue
   - 添加引用计数逻辑

3. ⬜ **重构send()**
   - 广播到所有subscriber队列
   - 设置ref_count = active_subscribers

4. ⬜ **重构receive()**
   - 从subscriber自己的队列读取
   - 简化逻辑（不再需要round-robin）

5. ⬜ **重构release()**
   - 引用计数递减
   - 只有ref_count==0时回收

6. ⬜ **更新测试**
   - 修正所有断言
   - 验证广播行为

---

## 参考：iceoryx2真实设计

iceoryx2使用：
- **Sample Pool**: 共享内存chunk池
- **Subscriber Queues**: 每个subscriber有FIFO队列
- **Sample Headers**: 存储引用计数
- **Zero-Copy**: 所有subscriber读取同一个chunk
- **Reference Counting**: 最后一个subscriber释放时回收

当前错误实现更像是**AMQP工作队列**（多个worker竞争消费），而不是**发布-订阅**。

---

**决定**：是否需要立即重构？还是保持当前实现（工作队列模式）并明确文档说明？
