# 双计数器重构进度跟踪

## ✅ Phase 1: 数据结构重构 (已完成)

### 1.1 ✅ CSegmentState类创建
- [x] CSegmentState.hpp
- [x] CSegmentState.cpp
- [x] borrowSample() / releaseSample() 实现

### 1.2 ✅ ChunkHeader改造
- [x] 移除 ref_count字段
- [x] 更新构造函数

### 1.3 ✅ PublisherState改造  
- [x] 增加 loan_counter (AtomicU32)
- [x] 增加 max_loaned_samples配置

### 1.4 ✅ SubscriberState改造
- [x] 增加 borrow_counter (UInt32)
- [x] 增加 max_borrowed_samples配置  
- [x] 增加 completion_queue (MessageQueue)
- [x] 增加 completion_mutex

### 1.5 ✅ SharedMemoryAllocatorConfig扩展
- [x] publisher_max_loaned_samples字段
- [x] subscriber_max_borrowed_samples字段
- [x] 默认配置更新

### 1.6 ✅ SharedMemoryAllocator新增成员
- [x] segment_state_成员
- [x] getDistanceToChunk()辅助方法
- [x] retrieveReturnedSamples()辅助方法
- [x] releaseSampleToPool()辅助方法

---

## 🚧 Phase 2: 核心API重构 (进行中)

### 2.1 initialize() - 初始化SegmentState
**文件**: CSharedMemoryAllocator.cpp:initialize()
**修改点**:
```cpp
// 创建SegmentState
segment_state_ = new CSegmentState(config_.chunk_count);
segment_state_->setPayloadSize(config_.max_chunk_size);

// 初始化Publisher/Subscriber配额
for (uint32_t i = 0; i < config_.max_publishers; ++i) {
    publishers_[i].max_loaned_samples = config_.publisher_max_loaned_samples;
}
for (uint32_t i = 0; i < config_.max_channels; ++i) {
    subscribers_[i].max_borrowed_samples = config_.subscriber_max_borrowed_samples;
}
```

### 2.2 ⏳ loan() - 增加配额检查
**修改位置**: Line 552 (带PublisherHandle的版本)
**关键逻辑**:
```cpp
// 1. 回收已释放样本
retrieveReturnedSamples(publisher_state);

// 2. 检查配额
if (publisher_state->loan_counter >= publisher_state->max_loaned_samples) {
    return Result<void>::FromError(CoreErrc::kExceedsMaxLoans);
}

// 3. 分配chunk (原有逻辑)
// ...

// 4. loan_counter++
publisher_state->loan_counter.fetch_add(1, memory_order_relaxed);
```

### 2.3 ⏳ send() - loan_counter--，sample_ref++
**修改位置**: Line 630
**关键逻辑**:
```cpp
// 1. loan_counter--（Publisher释放所有权）
publisher_state->loan_counter.fetch_sub(1, memory_order_relaxed);

// 2. 广播到所有Subscriber
for (each active subscriber) {
    // sample_reference_counter++（BEFORE enqueue）
    UInt32 distance = getDistanceToChunk(chunk);
    segment_state_->borrowSample(distance);
    
    if (!subscriber->rx_queue.enqueue(chunk)) {
        // 失败回滚
        segment_state_->releaseSample(distance);
    }
}
```

### 2.4 ⏳ receive() - 增加borrow_counter检查
**修改位置**: Line 887
**关键逻辑**:
```cpp
// 1. 检查配额
if (subscriber_state->borrow_counter >= subscriber_state->max_borrowed_samples) {
    return Result<void>::FromError(CoreErrc::kReceiveWouldExceedMaxBorrow);
}

// 2. dequeue (原有逻辑)
ChunkHeader* chunk = subscriber_state->rx_queue.dequeue();

// 3. borrow_counter++
subscriber_state->borrow_counter++;
```

### 2.5 ⏳ release() - 推入completion_queue
**修改位置**: Line 1018
**关键逻辑**:
```cpp
// 1. borrow_counter--
subscriber_state->borrow_counter--;

// 2. 推入completion_queue（延迟回收）
{
    std::lock_guard<std::mutex> lock(subscriber_state->completion_mutex);
    subscriber_state->completion_queue.enqueue(chunk);
}

// ⚠️ 不立即减少sample_reference_counter
// 由retrieveReturnedSamples()批量处理
```

### 2.6 ⏳ retrieveReturnedSamples() - 批量回收
**新增方法**
**关键逻辑**:
```cpp
void retrieveReturnedSamples(PublisherState* publisher) {
    // 遍历所有Subscriber的completion_queue
    for (uint32_t i = 0; i < 64; ++i) {
        SubscriberState* sub = &subscribers_[i];
        if (!sub->active) continue;
        
        std::lock_guard<std::mutex> lock(sub->completion_mutex);
        
        ChunkHeader* chunk;
        while ((chunk = sub->completion_queue.dequeue()) != nullptr) {
            // sample_reference_counter--
            UInt32 distance = getDistanceToChunk(chunk);
            UInt64 old_ref = segment_state_->releaseSample(distance);
            
            // 最后一个引用 → 归还pool
            if (old_ref == 1) {
                returnChunkToPool(chunk);
            }
        }
    }
}
```

### 2.7 ⏳ getDistanceToChunk() - 辅助方法
**新增方法**
```cpp
UInt32 SharedMemoryAllocator::getDistanceToChunk(const ChunkHeader* chunk) const {
    UInt8* pool_start = reinterpret_cast<UInt8*>(chunk_pool_);
    UInt8* chunk_addr = reinterpret_cast<UInt8*>(const_cast<ChunkHeader*>(chunk));
    return static_cast<UInt32>(chunk_addr - pool_start);
}
```

### 2.8 ⏳ releaseSampleToPool() - 辅助方法
**新增方法**
```cpp
bool SharedMemoryAllocator::releaseSampleToPool(ChunkHeader* chunk) {
    UInt32 distance = getDistanceToChunk(chunk);
    UInt64 old_ref = segment_state_->releaseSample(distance);
    
    if (old_ref == 1) {
        // 最后一个引用，归还pool
        ChunkState expected = ChunkState::IN_USE;
        if (chunk->state.compare_exchange_strong(expected, ChunkState::FREE, ...)) {
            free_list_.push(chunk);
            return true;
        }
    }
    return false;
}
```

---

## ⏳ Phase 3: 辅助功能（待开始）

### 3.1 错误码扩展
- [ ] CoreErrc::kExceedsMaxLoans
- [ ] CoreErrc::kReceiveWouldExceedMaxBorrow

### 3.2 统计接口
- [ ] getLoanedSamples(pub)
- [ ] getBorrowedSamples(sub)
- [ ] getSampleReferenceCount(chunk_id)

---

## ⏳ Phase 4: 测试用例（待开始）

### 4.1 Publisher配额测试
- [ ] TEST(SHMDualCounter, PublisherLoanQuotaEnforcement)
- [ ] TEST(SHMDualCounter, PublisherSendReducesLoanCounter)

### 4.2 Subscriber配额测试
- [ ] TEST(SHMDualCounter, SubscriberBorrowQuotaEnforcement)
- [ ] TEST(SHMDualCounter, SubscriberReleaseReducesBorrowCounter)

### 4.3 批量回收测试
- [ ] TEST(SHMDualCounter, DelayedBatchReclaim)
- [ ] TEST(SHMDualCounter, SampleReferenceCounterLifecycle)

---

## 当前状态

**阶段**: Phase 2.1 - 即将实现initialize()和辅助方法
**下一步**: 
1. 实现 initialize() 中的segment_state_创建
2. 实现 getDistanceToChunk()
3. 实现 retrieveReturnedSamples()  
4. 实现 releaseSampleToPool()
5. 重构 loan() - 配额检查
6. 重构 send() - 双计数器逻辑
7. 重构 receive() - 配额检查
8. 重构 release() - completion_queue

**预计完成时间**: Phase 2 预计2-3小时
