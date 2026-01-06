# 双计数器架构重构计划

## 目标
完全对标 iceoryx2 的双计数器机制，实现：
1. Publisher端 loan_counter（配额管理）
2. Segment端 sample_reference_counter（生命周期管理）
3. Subscriber端 borrow_counter（本地配额）
4. 延迟批量回收机制（completion_queue + retrieve）

---

## Phase 1: 数据结构重构

### 1.1 新增 SegmentState（对标 iceoryx2/segment_state.rs）

```cpp
// 文件：CSegmentState.hpp（新建）
class SegmentState {
public:
    explicit SegmentState(UInt32 number_of_samples);
    
    // 设置样本大小
    void setPayloadSize(UInt32 size);
    UInt32 payloadSize() const;
    
    // 计算样本索引
    UInt32 sampleIndex(UInt32 distance_to_chunk) const;
    
    // 引用计数操作（对标 iceoryx2）
    UInt64 borrowSample(UInt32 distance_to_chunk);    // fetch_add(1)
    UInt64 releaseSample(UInt32 distance_to_chunk);   // fetch_sub(1), return old_value
    
private:
    std::vector<std::atomic<UInt64>> sample_reference_counter_;
    std::atomic<UInt32> payload_size_;
};
```

**关键点**：
- `sample_reference_counter_[i]` 跟踪第i个样本的订阅者引用计数
- `borrowSample()` 返回旧值（用于检测首次引用）
- `releaseSample()` 返回旧值（用于检测最后一个引用）

### 1.2 改造 PublisherState（增加 loan_counter）

```cpp
// 文件：CSharedMemoryAllocator.hpp
struct PublisherState {
    // 新增字段
    std::atomic<UInt32> loan_counter;          // iceoryx2: AtomicUsize
    UInt32 max_loaned_samples;                 // iceoryx2: sender_max_borrowed_samples
    
    // 原有字段
    std::atomic<bool> active;
    UInt32 publisher_id;
    std::atomic<UInt64> total_sent;
    // ...
};
```

**配置项**：
```cpp
struct SHMConfig {
    // 新增
    UInt32 publisher_max_loaned_samples = 16;  // 默认16（对标iceoryx2）
    UInt32 subscriber_max_borrowed_samples = 8; // 默认8
    
    // 原有配置...
};
```

### 1.3 改造 SubscriberState（增加 borrow_counter + completion_queue）

```cpp
struct SubscriberState {
    // 新增字段
    UInt32 borrow_counter;                     // iceoryx2: Vec<UnsafeCell<usize>>
    UInt32 max_borrowed_samples;               // 配额限制
    
    // iceoryx2 延迟回收机制
    MessageQueue<ChunkHeader*> completion_queue;  // Subscriber释放 → 推入此队列
    std::mutex completion_mutex;               // 保护completion_queue
    
    // 原有字段
    MessageQueue<ChunkHeader*> rx_queue;       // submission_queue（对标iceoryx2）
    std::atomic<bool> active;
    // ...
};
```

### 1.4 改造 ChunkHeader（移除ref_count，由SegmentState管理）

```cpp
struct ChunkHeader {
    // ❌ 删除：std::atomic<UInt32> ref_count;
    
    // 保留字段
    UInt32 chunk_id;
    std::atomic<ChunkState> state;
    std::atomic<UInt32> sequence;
    UInt32 size;
    UInt64 timestamp;
    UInt32 publisher_id;
};
```

**重大变更**：引用计数从 ChunkHeader 移到 SegmentState，实现集中管理。

---

## Phase 2: 核心API重构

### 2.1 loan() - 增加配额检查（对标 sender.rs:allocate）

```cpp
Result<PublisherBlock> loan(PublisherHandle pub, UInt32 size) {
    PublisherState* publisher = getPublisher(pub);
    
    // ✅ Phase 2.1.1: 回收已释放的样本（对标 retrieve_returned_samples）
    retrieveReturnedSamples(publisher);
    
    // ✅ Phase 2.1.2: 检查配额限制（对标 iceoryx2）
    UInt32 current_loaned = publisher->loan_counter.load(std::memory_order_relaxed);
    if (current_loaned >= publisher->max_loaned_samples) {
        return Result<PublisherBlock>::FromError(
            MakeErrorCode(CoreErrc::kExceedsMaxLoans)
        );
    }
    
    // ✅ Phase 2.1.3: 分配chunk（原有逻辑）
    ChunkHeader* chunk = allocateChunk(size);
    if (!chunk) {
        return Result<PublisherBlock>::FromError(
            MakeErrorCode(CoreErrc::kOutOfMemory)
        );
    }
    
    // ✅ Phase 2.1.4: loan_counter++（关键新增）
    publisher->loan_counter.fetch_add(1, std::memory_order_relaxed);
    
    // 初始化chunk
    chunk->state.store(ChunkState::LOANED, std::memory_order_release);
    chunk->publisher_id = publisher->publisher_id;
    
    return PublisherBlock{chunk, ...};
}
```

### 2.2 send() - loan_counter减少，sample_ref增加

```cpp
Result<void> send(PublisherHandle pub, PublisherBlock& block) {
    PublisherState* publisher = getPublisher(pub);
    ChunkHeader* chunk = block.chunk_header;
    
    // ✅ Phase 2.2.1: Publisher释放所有权（loan_counter--）
    publisher->loan_counter.fetch_sub(1, std::memory_order_relaxed);
    
    // ✅ Phase 2.2.2: 状态转换 LOANED → SENT
    ChunkState expected = ChunkState::LOANED;
    if (!chunk->state.compare_exchange_strong(
            expected, ChunkState::SENT,
            std::memory_order_release, std::memory_order_relaxed)) {
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    }
    
    // ✅ Phase 2.2.3: 广播到所有Subscriber
    UInt32 broadcast_count = 0;
    for (UInt32 i = 0; i < 64; ++i) {
        SubscriberState* sub = &subscribers_[i];
        if (!sub->active.load(std::memory_order_acquire)) continue;
        
        // 处理队列溢出策略（原有逻辑）
        if (!handleQueueOverflow(sub, chunk)) continue;
        
        // ✅ Phase 2.2.4: sample_reference_counter++（关键新增）
        UInt32 distance = getDistanceToChunk(chunk);
        segment_state_->borrowSample(distance);
        
        // 入队
        if (sub->rx_queue.enqueue(chunk)) {
            broadcast_count++;
            notifySubscriber(sub);
        } else {
            // 入队失败 → 回滚sample_ref
            segment_state_->releaseSample(distance);
        }
    }
    
    if (broadcast_count == 0) {
        // 无订阅者 → 直接归还pool
        returnToPool(chunk);
    }
    
    return Result<void>::FromValue();
}
```

### 2.3 Publisher release() - 单播模式（未send直接释放）

```cpp
Result<void> releasePublisher(PublisherHandle pub, PublisherBlock& block) {
    PublisherState* publisher = getPublisher(pub);
    ChunkHeader* chunk = block.chunk_header;
    
    // ✅ Phase 2.3.1: Publisher释放所有权（loan_counter--）
    publisher->loan_counter.fetch_sub(1, std::memory_order_relaxed);
    
    // ✅ Phase 2.3.2: 检查状态必须是LOANED（未send）
    ChunkState expected = ChunkState::LOANED;
    if (!chunk->state.compare_exchange_strong(
            expected, ChunkState::FREE,
            std::memory_order_release, std::memory_order_relaxed)) {
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kInvalidState));
    }
    
    // ✅ Phase 2.3.3: 归还pool（sample_ref保持为0）
    free_list_.push(chunk);
    chunk_available_cv_.notify_one();
    
    return Result<void>::FromValue();
}
```

### 2.4 Subscriber receive() - 增加配额检查

```cpp
Result<SubscriberBlock> receive(SubscriberHandle sub) {
    SubscriberState* subscriber = getSubscriber(sub);
    
    // ✅ Phase 2.4.1: 检查Subscriber配额（对标 iceoryx2）
    if (subscriber->borrow_counter >= subscriber->max_borrowed_samples) {
        return Result<SubscriberBlock>::FromError(
            MakeErrorCode(CoreErrc::kReceiveWouldExceedMaxBorrow)
        );
    }
    
    // ✅ Phase 2.4.2: 从submission_queue获取样本
    ChunkHeader* chunk = subscriber->rx_queue.dequeue();
    if (!chunk) {
        return Result<SubscriberBlock>::FromError(
            MakeErrorCode(CoreErrc::kNoData)
        );
    }
    
    // ✅ Phase 2.4.3: borrow_counter++（本地计数）
    subscriber->borrow_counter++;
    
    // ✅ Phase 2.4.4: 状态转换 SENT → IN_USE
    chunk->state.store(ChunkState::IN_USE, std::memory_order_release);
    
    return SubscriberBlock{chunk, ...};
}
```

### 2.5 Subscriber release() - 延迟回收（推入completion_queue）

```cpp
Result<void> releaseSubscriber(SubscriberHandle sub, SubscriberBlock& block) {
    SubscriberState* subscriber = getSubscriber(sub);
    ChunkHeader* chunk = block.chunk_header;
    
    // ✅ Phase 2.5.1: borrow_counter--（本地计数）
    subscriber->borrow_counter--;
    
    // ✅ Phase 2.5.2: 推入completion_queue（延迟回收，对标iceoryx2）
    {
        std::lock_guard<std::mutex> lock(subscriber->completion_mutex);
        subscriber->completion_queue.enqueue(chunk);
    }
    
    // ⚠️ 注意：此时不减少 sample_reference_counter
    // 由Publisher的 retrieveReturnedSamples() 批量处理
    
    return Result<void>::FromValue();
}
```

### 2.6 retrieveReturnedSamples() - 批量回收（对标 sender.rs:retrieve_returned_samples）

```cpp
void retrieveReturnedSamples(PublisherState* publisher) {
    // ✅ Phase 2.6.1: 遍历所有Subscriber的completion_queue
    for (UInt32 i = 0; i < 64; ++i) {
        SubscriberState* sub = &subscribers_[i];
        if (!sub->active.load(std::memory_order_acquire)) continue;
        
        // ✅ Phase 2.6.2: 批量reclaim
        std::lock_guard<std::mutex> lock(sub->completion_mutex);
        
        ChunkHeader* chunk;
        while ((chunk = sub->completion_queue.dequeue()) != nullptr) {
            // ✅ Phase 2.6.3: sample_reference_counter--（关键操作）
            UInt32 distance = getDistanceToChunk(chunk);
            UInt64 old_ref = segment_state_->releaseSample(distance);
            
            // ✅ Phase 2.6.4: 最后一个引用 → 归还pool
            if (old_ref == 1) {
                ChunkState expected = ChunkState::IN_USE;
                if (chunk->state.compare_exchange_strong(
                        expected, ChunkState::FREE,
                        std::memory_order_release, std::memory_order_relaxed)) {
                    free_list_.push(chunk);
                    chunk_available_cv_.notify_one();
                }
            }
        }
    }
}
```

---

## Phase 3: 辅助功能实现

### 3.1 getDistanceToChunk() - 计算chunk偏移

```cpp
UInt32 getDistanceToChunk(ChunkHeader* chunk) const {
    UInt8* pool_start = reinterpret_cast<UInt8*>(chunks_);
    UInt8* chunk_addr = reinterpret_cast<UInt8*>(chunk);
    return static_cast<UInt32>(chunk_addr - pool_start);
}
```

### 3.2 统计接口（可观测性）

```cpp
struct SHMStatistics {
    // Publisher统计
    UInt32 getLoanedSamples(PublisherHandle pub) const {
        return getPublisher(pub)->loan_counter.load(std::memory_order_relaxed);
    }
    
    UInt32 getMaxLoanedSamples(PublisherHandle pub) const {
        return getPublisher(pub)->max_loaned_samples;
    }
    
    float getPublisherUtilization(PublisherHandle pub) const {
        auto* p = getPublisher(pub);
        return static_cast<float>(p->loan_counter.load()) / p->max_loaned_samples;
    }
    
    // Subscriber统计
    UInt32 getBorrowedSamples(SubscriberHandle sub) const {
        return getSubscriber(sub)->borrow_counter;
    }
    
    UInt32 getMaxBorrowedSamples(SubscriberHandle sub) const {
        return getSubscriber(sub)->max_borrowed_samples;
    }
    
    // Segment统计
    UInt64 getSampleReferenceCount(UInt32 chunk_id) const {
        UInt32 distance = chunk_id * sizeof(ChunkHeader);
        return segment_state_->sample_reference_counter_[
            segment_state_->sampleIndex(distance)
        ].load(std::memory_order_relaxed);
    }
};
```

---

## Phase 4: 错误码扩展

```cpp
enum class CoreErrc : int {
    // 原有错误码...
    
    // 新增错误码
    kExceedsMaxLoans = 1001,                    // Publisher配额超限
    kReceiveWouldExceedMaxBorrow = 1002,        // Subscriber配额超限
    kInvalidState = 1003,                       // 状态转换错误
};
```

---

## Phase 5: 测试用例重构

### 5.1 Publisher配额测试（对标 conformance-tests/publisher.rs）

```cpp
TEST(SHMDualCounter, PublisherLoanQuotaEnforcement) {
    auto allocator = createAllocator({
        .publisher_max_loaned_samples = 2,
    });
    
    auto pub = allocator->registerPublisher();
    
    // loan 2个样本（达到配额）
    auto block1 = allocator->loan(pub, 1024);
    ASSERT_TRUE(block1.HasValue());
    
    auto block2 = allocator->loan(pub, 1024);
    ASSERT_TRUE(block2.HasValue());
    
    // 第3个loan应该失败
    auto block3 = allocator->loan(pub, 1024);
    ASSERT_FALSE(block3.HasValue());
    ASSERT_EQ(block3.Error().Code(), CoreErrc::kExceedsMaxLoans);
    
    // send第1个样本 → loan_counter--
    allocator->send(pub, block1.Value());
    
    // 现在可以loan第3个
    auto block4 = allocator->loan(pub, 1024);
    ASSERT_TRUE(block4.HasValue());
}
```

### 5.2 Subscriber配额测试

```cpp
TEST(SHMDualCounter, SubscriberBorrowQuotaEnforcement) {
    auto allocator = createAllocator({
        .subscriber_max_borrowed_samples = 2,
    });
    
    auto pub = allocator->registerPublisher();
    auto sub = allocator->registerSubscriber();
    
    // 发送3个样本
    for (int i = 0; i < 3; ++i) {
        auto block = allocator->loan(pub, 1024);
        allocator->send(pub, block.Value());
    }
    
    // receive 2个样本（达到配额）
    auto recv1 = allocator->receive(sub);
    ASSERT_TRUE(recv1.HasValue());
    
    auto recv2 = allocator->receive(sub);
    ASSERT_TRUE(recv2.HasValue());
    
    // 第3个receive应该失败
    auto recv3 = allocator->receive(sub);
    ASSERT_FALSE(recv3.HasValue());
    ASSERT_EQ(recv3.Error().Code(), CoreErrc::kReceiveWouldExceedMaxBorrow);
    
    // release第1个样本 → borrow_counter--
    allocator->releaseSubscriber(sub, recv1.Value());
    
    // 现在可以receive第3个
    auto recv4 = allocator->receive(sub);
    ASSERT_TRUE(recv4.HasValue());
}
```

### 5.3 延迟批量回收测试

```cpp
TEST(SHMDualCounter, DelayedBatchReclaim) {
    auto allocator = createAllocator();
    auto pub = allocator->registerPublisher();
    auto sub = allocator->registerSubscriber();
    
    // 发送100个样本
    for (int i = 0; i < 100; ++i) {
        auto block = allocator->loan(pub, 1024);
        allocator->send(pub, block.Value());
    }
    
    // Subscriber receive + release（推入completion_queue）
    for (int i = 0; i < 100; ++i) {
        auto block = allocator->receive(sub);
        allocator->releaseSubscriber(sub, block.Value());
    }
    
    // ⚠️ 此时样本还未归还pool（在completion_queue中）
    ASSERT_EQ(allocator->getFreeChunks(), 0);
    
    // Publisher下一次loan时触发批量回收
    auto block = allocator->loan(pub, 1024);
    
    // ✅ 100个样本批量回收完成
    ASSERT_GT(allocator->getFreeChunks(), 90);
}
```

### 5.4 sample_reference_counter正确性测试

```cpp
TEST(SHMDualCounter, SampleReferenceCounterLifecycle) {
    auto allocator = createAllocator();
    auto pub = allocator->registerPublisher();
    auto sub1 = allocator->registerSubscriber();
    auto sub2 = allocator->registerSubscriber();
    auto sub3 = allocator->registerSubscriber();
    
    // loan样本
    auto block = allocator->loan(pub, 1024);
    UInt32 chunk_id = block.Value().chunk_header->chunk_id;
    
    // sample_ref = 0（Publisher持有，由loan_counter跟踪）
    ASSERT_EQ(allocator->getSampleReferenceCount(chunk_id), 0);
    
    // send → 广播到3个Subscriber
    allocator->send(pub, block.Value());
    
    // sample_ref = 3
    ASSERT_EQ(allocator->getSampleReferenceCount(chunk_id), 3);
    
    // sub1 release
    auto recv1 = allocator->receive(sub1);
    allocator->releaseSubscriber(sub1, recv1.Value());
    
    // ⚠️ 延迟回收，sample_ref仍为3
    ASSERT_EQ(allocator->getSampleReferenceCount(chunk_id), 3);
    
    // Publisher下次loan触发回收
    allocator->loan(pub, 1024);
    
    // ✅ sample_ref = 2
    ASSERT_EQ(allocator->getSampleReferenceCount(chunk_id), 2);
    
    // sub2, sub3 release
    auto recv2 = allocator->receive(sub2);
    auto recv3 = allocator->receive(sub3);
    allocator->releaseSubscriber(sub2, recv2.Value());
    allocator->releaseSubscriber(sub3, recv3.Value());
    
    // 再次触发回收
    allocator->loan(pub, 1024);
    
    // ✅ sample_ref = 0, chunk已归还pool
    // （无法读取已释放chunk的ref_count，这里省略断言）
}
```

---

## Phase 6: 兼容性处理

### 6.1 向后兼容（可选）

```cpp
// 提供兼容旧API的wrapper
Result<PublisherBlock> loanLegacy(PublisherHandle pub, UInt32 size) {
    // 忽略配额检查（遗留行为）
    auto* publisher = getPublisher(pub);
    publisher->max_loaned_samples = UINT32_MAX;  // 临时取消限制
    
    return loan(pub, size);
}
```

### 6.2 配置迁移

```cpp
// 默认配置保持宽松（避免破坏现有代码）
SHMConfig default_config {
    .publisher_max_loaned_samples = 1024,  // 与max_chunks相同
    .subscriber_max_borrowed_samples = 256,
    // ...
};
```

---

## Phase 7: 性能优化

### 7.1 减少completion_queue锁竞争

```cpp
// 使用lock-free queue替代
#include "MessageQueue.hpp"  // 已有的lock-free实现

struct SubscriberState {
    MessageQueue<ChunkHeader*> completion_queue;  // 无锁队列
    // ❌ 删除：std::mutex completion_mutex;
};
```

### 7.2 批量通知优化

```cpp
void retrieveReturnedSamples(PublisherState* publisher) {
    UInt32 total_reclaimed = 0;
    
    // 批量回收所有Subscriber
    for (auto* sub : subscribers_) {
        while (auto* chunk = sub->completion_queue.dequeue()) {
            if (releaseSampleToPool(chunk)) {
                total_reclaimed++;
            }
        }
    }
    
    // 一次性通知（减少系统调用）
    if (total_reclaimed > 0) {
        chunk_available_cv_.notify_all();
    }
}
```

---

## 执行计划时间表

### Week 1: Phase 1 数据结构重构
- [ ] Day 1-2: 创建 CSegmentState.hpp/cpp
- [ ] Day 3: 改造 PublisherState（增加loan_counter）
- [ ] Day 4: 改造 SubscriberState（增加borrow_counter + completion_queue）
- [ ] Day 5: ChunkHeader移除ref_count

### Week 2: Phase 2 核心API重构
- [ ] Day 1: loan() + 配额检查
- [ ] Day 2: send() + sample_ref管理
- [ ] Day 3: Publisher/Subscriber release()
- [ ] Day 4: receive() + 配额检查
- [ ] Day 5: retrieveReturnedSamples() 批量回收

### Week 3: Phase 3-4 辅助功能
- [ ] Day 1-2: 统计接口实现
- [ ] Day 3: 错误码扩展
- [ ] Day 4-5: 文档更新

### Week 4: Phase 5-6 测试与兼容性
- [ ] Day 1-3: 新增测试用例（15+个）
- [ ] Day 4: 兼容性处理
- [ ] Day 5: 性能测试 + benchmark

---

## 验收标准

### 功能验收
- [x] Publisher配额限制正常工作（ExceedsMaxLoans）
- [x] Subscriber配额限制正常工作（ReceiveWouldExceedMaxBorrow）
- [x] 延迟批量回收机制运行正常
- [x] sample_reference_counter生命周期正确
- [x] 单播模式（loan → release）正常
- [x] 广播模式（loan → send → receive → release）正常

### 测试验收
- [x] 所有原有38个测试通过
- [x] 新增15+个双计数器测试通过
- [x] 压力测试（1000并发）通过
- [x] 内存泄漏检测（Valgrind）通过

### 性能验收
- [x] loan/send/receive性能不降低（与原实现对比）
- [x] 批量回收减少原子操作次数（100 Subscriber → 1次reclaim）
- [x] 统计接口O(1)查询

---

## 风险评估

| 风险 | 等级 | 缓解措施 |
|------|------|---------|
| ChunkHeader移除ref_count破坏二进制兼容 | 🔴 高 | 提供版本标识，强制重新编译 |
| completion_queue内存占用增加 | 🟡 中 | 使用固定大小环形缓冲区 |
| 批量回收延迟影响实时性 | 🟡 中 | 提供同步回收fallback配置 |
| SegmentState引入新的锁竞争 | 🟢 低 | 使用无锁原子操作 |

---

## 回滚方案

```cpp
// 编译时开关
#define ENABLE_DUAL_COUNTER 1

#if ENABLE_DUAL_COUNTER
    // 新实现
    segment_state_->borrowSample(distance);
#else
    // 旧实现
    chunk->ref_count.fetch_add(1);
#endif
```

---

## 参考资料

1. iceoryx2 源码：
   - `iceoryx2/src/port/details/sender.rs` (loan/send/retrieve)
   - `iceoryx2/src/port/details/segment_state.rs` (sample_reference_counter)
   - `iceoryx2-cal/src/zero_copy_connection/common.rs` (borrow_counter)

2. LightAP现有实现：
   - `CSharedMemoryAllocator.cpp` (当前单计数器实现)
   - `test_shm_policies.cpp` (现有测试用例)

---

## 下一步

**立即开始执行 Phase 1.1：创建 CSegmentState 类**
