# LightAP IPC 实现路线图

> **文档版本**: 1.0  
> **创建日期**: 2026-01-07  
> **负责团队**: LightAP Core Team  
> **参考架构**: Eclipse iceoryx2

---

## 目录

- [1. 总体规划](#1-总体规划)
- [2. Phase 1: 核心基础设施（4周）](#2-phase-1-核心基础设施4周)
- [3. Phase 2: Pub-Sub核心功能（6周）](#3-phase-2-pub-sub核心功能6周)
- [4. Phase 3: WaitSet机制（2周）](#4-phase-3-waitset机制2周)
- [5. Phase 4: SubscriberRegistry无锁化（2周）](#5-phase-4-subscriberregistry无锁化2周)
- [6. Phase 5: Hook回调机制（2周）](#6-phase-5-hook回调机制2周)
- [7. Phase 6: 测试与验证（2周）](#7-phase-6-测试与验证2周)
- [8. 可选模块（额外开发）](#8-可选模块额外开发)
- [9. 开发里程碑](#9-开发里程碑)
- [10. 风险与缓解](#10-风险与缓解)
- [11. 资源需求](#11-资源需求)
- [12. 下一步行动](#12-下一步行动)

---

## 1. 总体规划

**开发周期**: 18周（约4.5个月）  
**核心模块**: Phase 1-6（必须，共18周）  
**扩展模块**: E2E保护、Heartbeat监控（可选，独立AUTOSAR模块，额外3周）

**开发优先级**:
1. **P0 (Must Have)**: Phase 1-6 核心IPC功能
2. **P1 (Optional)**: E2E保护、Heartbeat监控
3. **P2 (Future)**: Request-Response模式、Event模式

**技术栈**:
- 语言: C++17
- 编译器: GCC 11+ / Clang 14+
- 测试框架: Google Test
- 性能分析: perf、Valgrind、flamegraph

---

## 2. Phase 1: 核心基础设施（4周）

**目标**: 建立共享内存和内存管理基础

**设计文档索引**:
- 📖 [§3. 共享内存管理](IPC_DESIGN_ARCHITECTURE.md#3-共享内存管理)
- 📖 [§3.1 共享内存约束](IPC_DESIGN_ARCHITECTURE.md#31-共享内存约束)
- 📖 [§3.3 内存架构总览](IPC_DESIGN_ARCHITECTURE.md#33-内存架构总览)
- 📖 [§3.4 内存管理接口](IPC_DESIGN_ARCHITECTURE.md#34-内存管理接口)

### 周1-2: 共享内存管理

#### 任务清单
- [ ] **SharedMemoryManager** 实现
  - [ ] POSIX shm_open/shm_unlink API封装
  - [ ] 共享内存创建/打开/关闭
  - [ ] mmap/munmap 内存映射
  - [ ] 魔数验证（0xICE0RYX2）
  - [ ] 版本兼容性检查
- [ ] **ControlBlock** 设计
  - [ ] 元数据结构定义
  - [ ] loan_waitset 初始化
  - [ ] 状态管理（初始化/就绪）
- [ ] **单元测试**
  - [ ] 跨进程共享内存访问测试
  - [ ] 异常情况测试（重复创建、权限错误）

#### 技术细节
```cpp
// 文件: source/ipc/shm/SharedMemoryManager.hpp
class SharedMemoryManager {
public:
    Result<void> CreateSharedMemory(const char* name, size_t size);
    Result<void*> MapMemory(int fd, size_t size);
    Result<void> UnmapMemory(void* addr, size_size);
    Result<void> UnlinkSharedMemory(const char* name);
private:
    int shm_fd_{-1};
    void* base_addr_{nullptr};
};
```

#### 验收标准
- ✅ 跨进程共享内存读写一致性测试通过
- ✅ 权限错误、重复创建等异常处理正确
- ✅ Valgrind无内存泄漏
- ✅ 单元测试覆盖率 > 80%

#### 交付物
- `source/ipc/shm/SharedMemoryManager.hpp`
- `source/ipc/shm/SharedMemoryManager.cpp`
- `source/ipc/shm/ControlBlock.hpp`
- `test/ipc/shm/SharedMemoryManager_test.cpp`
- 单元测试报告

---

### 周3-4: ChunkPool分配器

**设计文档索引**:
- 📖 [§3.3 ChunkPool内存分配策略](IPC_DESIGN_ARCHITECTURE.md#33-chunkpool-内存分配策略)
- 📖 [§3.4 Chunk状态机设计](IPC_DESIGN_ARCHITECTURE.md#34-chunk-状态机设计)
- 📖 [§3.5 双计数器引用计数机制](IPC_DESIGN_ARCHITECTURE.md#35-双计数器引用计数机制)

#### 任务清单
- [ ] **ChunkPoolAllocator** 实现
  - [ ] Free-list索引链表（next_free_index）
  - [ ] 无锁分配（CAS操作）
  - [ ] 无锁释放（CAS循环）
  - [ ] WaitSet集成（loan_waitset）
  - [ ] allocated_count统计
- [ ] **ChunkHeader** 设计
  - [ ] 状态机（kFree/kLoaned/kSent/kReceived）
  - [ ] 引用计数（ref_count）
  - [ ] 2MB对齐支持
- [ ] **性能测试**
  - [ ] Allocate/Deallocate基准测试
  - [ ] 并发分配压力测试
  - [ ] 内存碎片分析

#### 技术细节
```cpp
// 文件: source/ipc/chunk/ChunkPoolAllocator.hpp
class ChunkPoolAllocator {
public:
    Result<UInt32> Allocate();  // 返回chunk_index
    void Deallocate(UInt32 chunk_index);
    bool IsExhausted() const;
private:
    std::atomic<UInt32> free_list_head_{0};
    std::atomic<UInt32> allocated_count_{0};
    WaitSet loan_waitset_;  // 位于ControlBlock
};
```

#### 验收标准
- ✅ Allocate/Deallocate延迟 < 100ns（fast path）
- ✅ 并发测试（4线程，100万次分配）无数据竞争
- ✅ ThreadSanitizer检测无问题
- ✅ 内存碎片率 < 5%

#### 交付物
- `source/ipc/chunk/ChunkPoolAllocator.hpp`
- `source/ipc/chunk/ChunkPoolAllocator.cpp`
- `source/ipc/chunk/ChunkHeader.hpp`
- `test/ipc/chunk/ChunkPoolAllocator_test.cpp`
- 性能基准报告（benchmark.md）

---

## 3. Phase 2: Pub-Sub核心功能（6周）

**目标**: 实现零拷贝发布-订阅机制

**设计文档索引**:
- 📖 [§4. 消息传递模式](IPC_DESIGN_ARCHITECTURE.md#4-消息传递模式)
- 📖 [§4.1 Publish-Subscribe](IPC_DESIGN_ARCHITECTURE.md#41-publish-subscribe-发布-订阅)
- 📖 [§5. 运行时流程详解](IPC_DESIGN_ARCHITECTURE.md#5-运行时流程详解)

### 周5-6: 无锁队列

**设计文档索引**:
- 📖 [§4.3 通用RingBufferBlock模型](IPC_DESIGN_ARCHITECTURE.md#43-通用-ringbufferblock-模型)
- 📖 [§4.4 Subscriber消息队列模型](IPC_DESIGN_ARCHITECTURE.md#44-subscriber-消息队列模型基于-ringbufferblock)

#### 任务清单
- [ ] **RingBufferBlock** 实现
  - [ ] SPSC无锁队列（单生产者单消费者）
  - [ ] 原子操作（head/tail指针）
  - [ ] 容量256（固定）
  - [ ] Enqueue/Dequeue/IsFull/IsEmpty
- [ ] **SubscriberQueue** 设计
  - [ ] event_flags初始化（HAS_DATA/HAS_SPACE）
  - [ ] msg_queue集成
  - [ ] 统计信息（overrun_count等）
- [ ] **单元测试**
  - [ ] 顺序入队出队测试
  - [ ] 队列满/空边界测试
  - [ ] 并发安全性测试

#### 技术细节
```cpp
// 文件: source/ipc/queue/RingBufferBlock.hpp
template <typename T, UInt32 Capacity = 256>
class RingBufferBlock {
public:
    bool Enqueue(T value);
    Optional<T> Dequeue();
    bool IsFull() const;
    bool IsEmpty() const;
private:
    std::atomic<UInt32> head_{0};
    std::atomic<UInt32> tail_{0};
    T buffer_[Capacity];
};
```

#### 验收标准
- ✅ Enqueue/Dequeue延迟 < 50ns
- ✅ SPSC测试通过（1000万次操作，无数据丢失）
- ✅ 队列满/空边界条件正确
- ✅ 代码覆盖率 > 90%

#### 交付物
- `source/ipc/queue/RingBufferBlock.hpp`
- `source/ipc/queue/SubscriberQueue.hpp`
- `test/ipc/queue/RingBufferBlock_test.cpp`
- 队列性能报告

---

### 周7-9: Publisher/Subscriber API
**设计文档索引**:
- 📖 [§4.1 Publish-Subscribe API](IPC_DESIGN_ARCHITECTURE.md#41-publish-subscribe-发布-订阅)
- 📖 [§4.5 Publisher便捷API](IPC_DESIGN_ARCHITECTURE.md#45-publisher-便捷-api)
- 📖 [§5.1 Publisher发送流程](IPC_DESIGN_ARCHITECTURE.md#5-运行时流程详解)
- 📖 [§5.2 Subscriber接收流程](IPC_DESIGN_ARCHITECTURE.md#5-运行时流程详解)
#### 任务清单
- [ ] **Publisher** 实现
  - [ ] Loan() - ChunkPool分配
  - [ ] Send() - 入队到所有Subscriber
  - [ ] SendCopy() - 拷贝便捷API
  - [ ] SendEmplace() - 原地构造
  - [ ] LoanFailurePolicy支持（kError/kWait/kBlock）
  - [ ] QueueFullPolicy支持（kOverwrite/kWait/kBlock/kDrop）
- [ ] **Subscriber** 实现
  - [ ] Receive() - 从队列出队
  - [ ] QueueEmptyPolicy支持（kBlock/kWait/kSkip/kError）
  - [ ] Sample RAII封装
- [ ] **Sample生命周期管理**
  - [ ] 构造函数（获取Chunk）
  - [ ] 析构函数（DecrementRef）
  - [ ] 移动语义（禁止拷贝）
  - [ ] Release()接口

#### 技术细节
```cpp
// 文件: source/ipc/pubsub/Publisher.hpp
template <typename T>
class Publisher {
public:
    Result<Sample<T>> Loan(LoanFailurePolicy policy = kError);
    Result<void> Send(Sample<T>&& sample, QueueFullPolicy policy = kDrop);
    Result<void> SendCopy(const T& data, QueueFullPolicy policy = kDrop);
    
    template <typename... Args>
    Result<void> SendEmplace(QueueFullPolicy policy, Args&&... args);
};

// 文件: source/ipc/pubsub/Subscriber.hpp
template <typename T>
class Subscriber {
public:
    Result<Sample<T>> Receive(QueueEmptyPolicy policy = kBlock);
    Result<Sample<T>> ReceiveWithTimeout(std::chrono::milliseconds timeout);
};
```

#### 验收标准
- ✅ SPSC测试通过（Publisher → Subscriber）
- ✅ SPMC测试通过（1 Publisher → 4 Subscribers）
- ✅ SP0C测试通过（0 Subscribers，不应耗尽ChunkPool）
- ✅ Sample析构自动释放Chunk
- ✅ API文档完整（Doxygen）

#### 交付物
- `source/ipc/pubsub/Publisher.hpp`
- `source/ipc/pubsub/Publisher.cpp`
- `source/ipc/pubsub/Subscriber.hpp`
- `source/ipc/pubsub/Subscriber.cpp`
- `source/ipc/pubsub/Sample.hpp`
- `test/ipc/pubsub/PubSub_test.cpp`
- API使用示例（examples/pubsub_basic.cpp）

---

### 周10: 队列策略实现

**设计文档索引**:
- 📖 [§7.5 异常处理策略](IPC_DESIGN_ARCHITECTURE.md#75-异常处理策略)
- 📖 [§7.5.1 队列满策略](IPC_DESIGN_ARCHITECTURE.md#75-异常处理策略)
- 📖 [§7.5.2 Loan失败策略](IPC_DESIGN_ARCHITECTURE.md#75-异常处理策略)

#### 任务清单
- [ ] **队列满策略**
  - [ ] kOverwrite: Ring Buffer模式
  - [ ] kWait: PollForFlags轮询
  - [ ] kBlock: WaitForFlags阻塞
  - [ ] kDrop: 立即返回错误
- [ ] **队列空策略**
  - [ ] kBlock: 阻塞等待（默认）
  - [ ] kWait: 轮询等待
  - [ ] kSkip: 立即返回
  - [ ] kError: 返回错误
- [ ] **集成测试**
  - [ ] 各策略组合测试
  - [ ] 超时机制验证

#### 技术细节
```cpp
// 策略枚举定义
enum class QueueFullPolicy {
    kOverwrite,  // Ring Buffer模式，覆盖最旧消息
    kWait,       // 轮询等待空间（自旋）
    kBlock,      // futex阻塞等待
    kDrop        // 立即丢弃（默认）
};

enum class QueueEmptyPolicy {
    kBlock,      // futex阻塞等待（默认）
    kWait,       // 轮询等待数据
    kSkip,       // 立即返回Empty
    kError       // 返回错误码
};
```

#### 验收标准
- ✅ kOverwrite策略正确覆盖旧消息
- ✅ kBlock策略在有数据/空间时自动唤醒
- ✅ kWait策略CPU占用率合理（< 10%）
- ✅ 超时机制精度 ± 5ms

#### 交付物
- 策略实现代码（集成在Publisher/Subscriber）
- `doc/QUEUE_POLICIES.md`（策略对比文档）
- `test/ipc/pubsub/QueuePolicy_test.cpp`

---

## 4. Phase 3: WaitSet机制（2周）

**目标**: 实现高性能等待/唤醒机制

**设计文档索引**:
- 📖 [§4.7 WaitSet机制](IPC_DESIGN_ARCHITECTURE.md#4-消息传递模式)
- 📖 [§4.7.1 EventFlag定义](IPC_DESIGN_ARCHITECTURE.md#4-消息传递模式)
- 📖 [§4.7.2 WaitSet Helper API](IPC_DESIGN_ARCHITECTURE.md#4-消息传递模式)
- 📖 [§4.7.3 Linux futex封装](IPC_DESIGN_ARCHITECTURE.md#4-消息传递模式)

### 周11-12: WaitSet实现

#### 任务清单
- [ ] **WaitSetHelper** 实现
  - [ ] WaitForFlags() - futex阻塞等待
  - [ ] PollForFlags() - 轮询检查
  - [ ] SetFlagsAndWake() - 设置标志+唤醒
  - [ ] ClearFlags() - 清除标志
  - [ ] Linux futex系统调用封装
- [ ] **EventFlag** 定义
  - [ ] HAS_DATA (bit 0)
  - [ ] HAS_SPACE (bit 1)
  - [ ] HAS_FREE_CHUNK (bit 2)
- [ ] **性能优化**
  - [ ] wake=false参数（kWait优化）
  - [ ] 快速路径优化（无系统调用）
  - [ ] 虚假唤醒处理

#### 技术细节
```cpp
// 文件: source/ipc/waitset/WaitSetHelper.hpp
class WaitSetHelper {
public:
    static Result<void> WaitForFlags(
        std::atomic<UInt32>* flags,
        UInt32 mask,
        std::chrono::milliseconds timeout
    );
    
    static bool PollForFlags(
        std::atomic<UInt32>* flags,
        UInt32 mask
    );
    
    static void SetFlagsAndWake(
        std::atomic<UInt32>* flags,
        UInt32 mask,
        bool wake = true  // kWait优化
    );
    
    static void ClearFlags(
        std::atomic<UInt32>* flags,
        UInt32 mask
    );
};

// EventFlag位定义
namespace EventFlag {
    constexpr UInt32 HAS_DATA = 0x01;        // bit 0
    constexpr UInt32 HAS_SPACE = 0x02;       // bit 1
    constexpr UInt32 HAS_FREE_CHUNK = 0x04;  // bit 2
}
```

#### 性能目标
- **WaitForFlags（block模式）**: < 1μs（从唤醒到返回）
- **PollForFlags（轮询模式）**: < 10ns（fast path）
- **SetFlagsAndWake（wake=true）**: ~255ns（含futex_wake）
- **SetFlagsAndWake（wake=false）**: ~55ns（无futex调用，4.5x加速）

#### 验收标准
- ✅ 快速路径延迟 < 60ns（wake=false）
- ✅ 唤醒延迟 < 300ns（wake=true）
- ✅ 虚假唤醒处理正确（重新检查条件）
- ✅ 超时机制精确（误差 < 5ms）

#### 交付物
- `source/ipc/waitset/WaitSetHelper.hpp`
- `source/ipc/waitset/WaitSetHelper.cpp`
- `source/ipc/waitset/EventFlag.hpp`
- `test/ipc/waitset/WaitSet_test.cpp`
- WaitSet性能报告（benchmark_waitset.md）

---

## 5. Phase 4: SubscriberRegistry无锁化（2周）

**目标**: 实现无锁Subscriber列表管理

**设计文档索引**:
- 📖 [§4.6 SubscriberRegistry无锁设计](IPC_DESIGN_ARCHITECTURE.md#4-消息传递模式)
- 📖 [§6.2 无锁编程优化](IPC_DESIGN_ARCHITECTURE.md#6-性能优化)
- 📖 [§6.3 缓存优化](IPC_DESIGN_ARCHITECTURE.md#6-性能优化)

### 周13-14: 无锁Registry

#### 任务清单
- [ ] **SubscriberRegistry** 实现
  - [ ] 双缓冲快照机制
  - [ ] 版本号控制
  - [ ] AddSubscriber() - 无锁注册
  - [ ] RemoveSubscriber() - 无锁移除
  - [ ] GetSnapshot() - 读取快照
  - [ ] RCU风格设计
- [ ] **集成测试**
  - [ ] 并发注册/注销测试
  - [ ] Publisher遍历一致性测试
  - [ ] 性能基准测试

#### 技术细节
```cpp
// 文件: source/ipc/registry/SubscriberRegistry.hpp
class SubscriberRegistry {
public:
    void AddSubscriber(SubscriberQueue* queue);
    void RemoveSubscriber(SubscriberQueue* queue);
    Snapshot GetSnapshot() const;  // 读取当前快照
    
private:
    struct SnapshotBlock {
        std::atomic<UInt32> version{0};
        UInt32 count{0};
        SubscriberQueue* queues[MAX_SUBSCRIBERS];
    };
    
    std::atomic<SnapshotBlock*> active_snapshot_{&snapshot_[0]};
    SnapshotBlock snapshot_[2];  // 双缓冲
};
```

#### 验收标准
- ✅ 并发注册/注销测试通过（8线程，10万次操作）
- ✅ Publisher遍历时Subscriber动态变化不崩溃
- ✅ ThreadSanitizer无数据竞争
- ✅ AddSubscriber/RemoveSubscriber延迟 < 500ns

#### 交付物
- `source/ipc/registry/SubscriberRegistry.hpp`
- `source/ipc/registry/SubscriberRegistry.cpp`
- `test/ipc/registry/SubscriberRegistry_test.cpp`
- `doc/LOCKFREE_REGISTRY_DESIGN.md`（并发安全性证明）

---

## 6. Phase 5: Hook回调机制（2周）

**目标**: 提供事件监控和调试能力

**设计文档索引**:
- 📖 [§10.1 Hook回调机制](IPC_DESIGN_ARCHITECTURE.md#10-核心设计确认基于-iceoryx2)
- 📖 [§7.5 异常处理策略](IPC_DESIGN_ARCHITECTURE.md#75-异常处理策略)
- 📖 [§7.4 IPC错误码定义](IPC_DESIGN_ARCHITECTURE.md#74-ipc-错误码定义)

### 周15-16: Hook实现

#### 任务清单
- [ ] **IPCEventHooks** 接口定义
  - [ ] OnLoanFailed() - Loan失败
  - [ ] OnChunkPoolExhausted() - ChunkPool耗尽
  - [ ] OnQueueOverrun() - 队列溢出
  - [ ] OnReceiveTimeout() - 接收超时
  - [ ] OnLoanCountWarning() - Loan计数警告
- [ ] **Hook集成**
  - [ ] Publisher::Loan()集成
  - [ ] Publisher::Send()集成
  - [ ] Subscriber::Receive()集成
- [ ] **示例实现**
  - [ ] 日志Hook
  - [ ] 统计Hook
  - [ ] 告警Hook

#### 技术细节
```cpp
// 文件: source/ipc/hooks/IPCEventHooks.hpp
class IPCEventHooks {
public:
    virtual ~IPCEventHooks() = default;
    
    virtual void OnLoanFailed(
        const char* topic,
        LoanFailurePolicy policy,
        UInt32 allocated_count
    ) {}
    
    virtual void OnChunkPoolExhausted(
        const char* topic,
        UInt32 total_chunks
    ) {}
    
    virtual void OnQueueOverrun(
        const char* topic,
        UInt32 subscriber_id,
        UInt32 dropped_count
    ) {}
    
    virtual void OnReceiveTimeout(
        const char* topic,
        std::chrono::milliseconds timeout
    ) {}
    
    virtual void OnLoanCountWarning(
        const char* topic,
        UInt32 current_count,
        UInt32 threshold
    ) {}
};
```

#### Hook集成点
```cpp
// Publisher::Loan()中集成
Result<Sample<T>> Publisher::Loan(LoanFailurePolicy policy) {
    auto chunk_index = allocator_->Allocate();
    if (!chunk_index) {
        // 触发Hook
        if (hooks_) {
            hooks_->OnLoanFailed(topic_name_, policy, allocated_count);
        }
        
        if (policy == LoanFailurePolicy::kBlock) {
            WaitSetHelper::WaitForFlags(&control_block_->loan_waitset, 
                                       EventFlag::HAS_FREE_CHUNK);
        }
        // ...
    }
}
```

#### 验收标准
- ✅ Hook回调在正确时机触发
- ✅ Hook不影响主路径性能（开销 < 1%）
- ✅ 示例Hook实现可用（日志、统计、告警）
- ✅ Hook文档完整

#### 交付物
- `source/ipc/hooks/IPCEventHooks.hpp`
- `examples/hooks/LoggingHook.cpp`
- `examples/hooks/StatisticsHook.cpp`
- `examples/hooks/AlertHook.cpp`
- `doc/HOOKS_USER_GUIDE.md`（Hook使用指南）

---

## 7. Phase 6: 测试与验证（2周）

**目标**: 全面测试和性能验证

**设计文档索引**:
- 📖 [§8. 测试方案](IPC_DESIGN_ARCHITECTURE.md#8-测试方案)
- 📖 [§8.1 单元测试](IPC_DESIGN_ARCHITECTURE.md#8-测试方案)
- 📖 [§8.2 集成测试](IPC_DESIGN_ARCHITECTURE.md#8-测试方案)
- 📖 [§8.3 性能测试](IPC_DESIGN_ARCHITECTURE.md#8-测试方案)
- 📖 [§9. AUTOSAR合规性](IPC_DESIGN_ARCHITECTURE.md#9-autosar-合规性)

### 周17-18: 综合测试

#### 任务清单
- [ ] **功能测试**
  - [ ] SPSC测试（单发单收）
  - [ ] SPMC测试（单发多收）
  - [ ] SP0C测试（无Subscriber）
  - [ ] 动态连接/断开测试
- [ ] **性能测试**
  - [ ] 端到端延迟测试（目标 < 1μs）
  - [ ] 吞吐量测试（目标 > 1M msg/s）
  - [ ] CPU占用分析
  - [ ] 内存占用分析
- [ ] **压力测试**
  - [ ] 长时间运行测试（24小时）
  - [ ] 内存泄漏检测（Valgrind）
  - [ ] 崩溃恢复测试
- [ ] **AUTOSAR合规性**
  - [ ] API命名规范检查
  - [ ] 错误码映射验证
  - [ ] 文档完整性检查

#### 性能基准

| 测试场景 | 目标 | 测试方法 |
|---------|------|---------|
| **端到端延迟** | < 1μs | Loan → Send → Receive（1KB消息） |
| **吞吐量** | > 1M msg/s | 持续发送（无背压） |
| **CPU占用** | < 5% | 1 Publisher + 4 Subscribers（轻载） |
| **内存占用** | < 100MB | ChunkPool（1000块 × 64KB） |
| **唤醒延迟** | < 300ns | WaitForFlags（futex） |

#### 压力测试场景

1. **长时间运行**:
   - 1 Publisher + 4 Subscribers
   - 持续24小时
   - 检测内存泄漏、性能退化

2. **并发注册/注销**:
   - 8线程动态创建/销毁Subscriber
   - 持续1小时
   - 检测竞态条件、死锁

3. **崩溃恢复**:
   - Publisher/Subscriber进程随机崩溃
   - 验证共享内存自动清理
   - 验证资源正确回收

#### AUTOSAR合规性检查

- [ ] API命名符合AUTOSAR规范（PascalCase、camelCase）
- [ ] 错误码映射到ara::core::ErrorCode
- [ ] 文档包含所有公开API说明
- [ ] 线程安全性文档化

#### 验收标准
- ✅ 所有功能测试通过
- ✅ 性能达到目标指标
- ✅ Valgrind无内存泄漏
- ✅ ThreadSanitizer无数据竞争
- ✅ 代码覆盖率 > 90%

#### 交付物
- `test/integration/SPSC_test.cpp`
- `test/integration/SPMC_test.cpp`
- `test/performance/Latency_benchmark.cpp`
- `test/performance/Throughput_benchmark.cpp`
- `test/stress/LongRunning_test.cpp`
- `doc/TEST_REPORT.md`（测试报告）
- `doc/PERFORMANCE_REPORT.md`（性能基准报告）
- `doc/AUTOSAR_COMPLIANCE_REPORT.md`（合规性报告）

---

## 8. 可选模块（额外开发）

### 可选模块1: E2E保护（AUTOSAR独立模块，2周）

**依赖**: Core IPC完成后（第18周后启动）

**设计文档索引**:
- 📖 [§10.2 E2E保护](IPC_DESIGN_ARCHITECTURE.md#10-核心设计确认基于-iceoryx2)
- 📖 [§7. 安全性设计](IPC_DESIGN_ARCHITECTURE.md#7-安全性设计)

#### 任务清单
- [ ] **E2EProtector** (Publisher端)
  - [ ] CRC32计算（硬件加速）
  - [ ] Routine Counter管理
  - [ ] Timestamp生成
  - [ ] DataID配置
- [ ] **E2EValidator** (Subscriber端)
  - [ ] CRC校验
  - [ ] Counter跳变检测
  - [ ] Timeout检测
  - [ ] 错误统计
- [ ] **配置管理**
  - [ ] E2EProfile配置
  - [ ] 错误阈值配置
- [ ] **集成测试**
  - [ ] CRC错误注入测试
  - [ ] Counter跳变模拟
  - [ ] 性能影响评估（< 5% 开销）

#### 技术细节
```cpp
// 文件: source/e2e/E2EProtector.hpp
class E2EProtector {
public:
    void Protect(void* data, size_t size, E2EHeader* header);
    
private:
    UInt32 CalculateCRC32(const void* data, size_t size);
    UInt16 counter_{0};
    UInt16 data_id_;
};

// 文件: source/e2e/E2EValidator.hpp
class E2EValidator {
public:
    E2EResult Validate(const void* data, size_t size, const E2EHeader& header);
    
private:
    UInt16 expected_counter_{0};
    UInt16 error_count_{0};
    std::chrono::steady_clock::time_point last_received_;
};
```

#### 性能目标
- CRC32计算开销 < 5%（使用硬件加速）
- E2E保护总开销 < 10%

#### 验收标准
- ✅ CRC错误检测率 100%
- ✅ Counter跳变检测准确
- ✅ 性能影响 < 5%
- ✅ 配置灵活（可开关）

#### 交付物
- `source/e2e/E2EProtector.hpp`（独立模块）
- `source/e2e/E2EProtector.cpp`
- `source/e2e/E2EValidator.hpp`
- `source/e2e/E2EValidator.cpp`
- `examples/e2e/E2EUsage.cpp`
- `doc/E2E_USER_GUIDE.md`
- `test/e2e/E2E_test.cpp`
- 性能影响报告

---

### 可选模块2: Heartbeat监控（AUTOSAR独立模块，1周）

**依赖**: Core IPC完成后（第20周启动）

**设计文档索引**:
- 📖 [§7.3 进程崩溃恢复](IPC_DESIGN_ARCHITECTURE.md#7-安全性设计)
- 📖 [§7. 安全性设计](IPC_DESIGN_ARCHITECTURE.md#7-安全性设计)

#### 任务清单
- [ ] **HeartbeatMonitor** 实现
  - [ ] 进程心跳检测
  - [ ] 超时检测（可配置阈值）
  - [ ] 死锁检测
  - [ ] 自动清理机制
- [ ] **集成Hook**
  - [ ] OnProcessTimeout回调
  - [ ] OnProcessRecovered回调
- [ ] **测试**
  - [ ] 进程崩溃模拟
  - [ ] 超时检测验证
  - [ ] 资源回收测试

#### 技术细节
```cpp
// 文件: source/healthmonitor/HeartbeatMonitor.hpp
class HeartbeatMonitor {
public:
    void RegisterProcess(ProcessId pid, std::chrono::milliseconds timeout);
    void UnregisterProcess(ProcessId pid);
    void Heartbeat(ProcessId pid);  // 进程调用更新心跳
    
    void CheckTimeouts();  // 定期检查（后台线程）
    
private:
    struct ProcessInfo {
        std::chrono::steady_clock::time_point last_heartbeat;
        std::chrono::milliseconds timeout;
        bool is_alive;
    };
    
    std::unordered_map<ProcessId, ProcessInfo> processes_;
};
```

#### 验收标准
- ✅ 进程崩溃后资源正确回收
- ✅ 超时检测精度 ± 10ms
- ✅ 死锁检测准确
- ✅ Hook回调正确触发

#### 交付物
- `source/healthmonitor/HeartbeatMonitor.hpp`（独立模块）
- `source/healthmonitor/HeartbeatMonitor.cpp`
- `examples/healthmonitor/MonitorUsage.cpp`
- `doc/HEARTBEAT_USER_GUIDE.md`
- `test/healthmonitor/HeartbeatMonitor_test.cpp`
- 故障恢复指南

---

## 9. 开发里程碑

| 里程碑 | 时间点 | 关键交付物 | 验收标准 |
|--------|--------|-----------|---------|
| **M1: 基础设施就绪** | 第4周 | SharedMemory + ChunkPool | 单元测试通过，性能达标（< 100ns） |
| **M2: Pub-Sub核心完成** | 第10周 | Publisher/Subscriber API | SPSC测试通过，API文档完整 |
| **M3: WaitSet集成** | 第12周 | 完整策略支持 | 所有策略测试通过，性能达标 |
| **M4: 无锁优化完成** | 第14周 | SubscriberRegistry | 并发测试通过，无数据竞争 |
| **M5: 监控能力就绪** | 第16周 | Hook机制 | Hook示例可用，文档完善 |
| **M6: 正式发布** | 第18周 | 完整IPC模块 | 所有测试通过，覆盖率 > 90% |
| **M7: E2E模块**（可选） | 第20周 | E2E保护 | CRC性能开销 < 5% |
| **M8: 监控模块**（可选） | 第21周 | Heartbeat | 故障恢复测试通过 |

---

## 10. 风险与缓解

| 风险 | 影响 | 概率 | 缓解措施 |
|------|------|------|---------|
| **futex性能不达标** | 高 | 低 | 预留kWait轮询策略备选，快速路径优化 |
| **并发测试发现竞态** | 高 | 中 | 增加2周调试缓冲时间，使用ThreadSanitizer |
| **AUTOSAR合规性问题** | 中 | 低 | 每周与规范对标，提前审查 |
| **内存泄漏** | 高 | 低 | 持续Valgrind检测，代码审查 |
| **跨平台兼容性** | 中 | 中 | 初期仅支持Linux，后续扩展（Phase 2） |
| **性能目标未达成** | 高 | 低 | 预留性能优化时间，使用perf分析瓶颈 |
| **人员变动** | 中 | 低 | 知识文档化，代码审查制度 |

---

## 11. 资源需求

### 人员配置
- **开发人员**: 2-3人（全职）
  - 开发者A: SharedMemory + ChunkPool
  - 开发者B: Publisher/Subscriber + WaitSet
  - 开发者C: Registry + Hook + 测试
- **测试人员**: 1人（兼职，第12周后全职）
- **技术 Lead**: 1人（兼职，架构审查）

### 硬件环境
- **开发机**: 
  - x86_64 Linux开发机 × 2（Ubuntu 22.04+）
  - 配置: 16GB RAM, 8-core CPU
- **测试机**: 
  - x86_64 Linux测试机 × 1（性能基准）
  - ARM64 测试机 × 1（可选，跨平台验证）

### 工具链
- **编译器**: GCC 11+ / Clang 14+
- **测试框架**: Google Test 1.12+
- **性能分析**: 
  - Valgrind 3.20+（内存泄漏）
  - perf（性能分析）
  - flamegraph（火焰图）
- **静态分析**:
  - clang-tidy（AUTOSAR规则检查）
  - cppcheck（AUTOSAR addon）
  - SonarQube（代码质量）
- **CI/CD**: GitHub Actions / Jenkins
- **代码审查**: Gerrit / GitHub PR

### 预算估算
- **人力成本**: 18周 × 3人 = 54人周
- **硬件成本**: ~$5000（开发机 + 测试机）
- **工具许可**: 开源工具（$0）
- **总预算**: ~$100K（按$2K/人周估算）

---

## 12. 下一步行动

### 立即启动（第1周）

1. **创建开发分支**
   ```bash
   git checkout -b feature/ipc-core
   ```

2. **搭建CI/CD流水线**
   - [ ] 配置GitHub Actions（编译、测试、覆盖率）
   - [ ] 集成Valgrind内存检测
   - [ ] 集成ThreadSanitizer数据竞争检测
   - [ ] 配置AUTOSAR代码规范检查（clang-tidy）
   - [ ] 配置静态分析（cppcheck + autosar addon）
   - [ ] 配置性能基准自动运行

3. **建立性能基准测试框架**
   - [ ] 集成Google Benchmark
   - [ ] 创建基准测试模板
   - [ ] 配置结果自动上传（Dashboard）

4. **代码仓库组织**
   ```
   modules/Core/
   ├── source/ipc/
   │   ├── shm/           # SharedMemory
   │   ├── chunk/         # ChunkPool
   │   ├── queue/         # RingBuffer
   │   ├── pubsub/        # Publisher/Subscriber
   │   ├── waitset/       # WaitSet
   │   ├── registry/      # SubscriberRegistry
   │   └── hooks/         # Hooks
   ├── test/ipc/
   │   ├── unit/          # 单元测试
   │   ├── integration/   # 集成测试
   │   ├── performance/   # 性能测试
   │   └── stress/        # 压力测试
   ├── examples/
   │   ├── pubsub_basic/  # 基础示例
   │   └── hooks/         # Hook示例
   └── doc/
       ├── IPC_DESIGN_ARCHITECTURE.md
       ├── IPC_IMPLEMENTATION_ROADMAP.md（本文档）
       └── API_REFERENCE.md
   ```

5. **每周评审机制**
   - [ ] 周一: 每周计划会议（定义本周目标）
   - [ ] 周三: 中期同步会议（问题讨论）
   - [ ] 周五: 周报&代码审查（成果验收）

---

### Phase 1 第1周任务分配

#### 开发者A: SharedMemoryManager实现
- [ ] Day 1-2: API设计 + 单元测试框架搭建
- [ ] Day 3-4: SharedMemoryManager实现
- [ ] Day 5: 单元测试 + 代码审查

#### 开发者B: ControlBlock设计
- [ ] Day 1-2: ControlBlock结构设计
- [ ] Day 3-4: loan_waitset集成 + 状态管理
- [ ] Day 5: 集成测试 + 文档

#### 开发者C: 文档和示例准备
- [ ] Day 1-2: CI/CD流水线搭建
- [ ] Day 3-4: 性能基准测试框架
- [ ] Day 5: 示例代码模板 + API文档模板

---

### 沟通与协作

**每日站会**（15分钟）:
- 昨天完成的工作
- 今天计划的工作
- 遇到的阻碍

**代码审查规范**:
- 所有代码必须经过至少1人审查
- 单元测试覆盖率 > 80%
- Valgrind无内存泄漏
- ThreadSanitizer无数据竞争
- **AUTOSAR C++14合规**:
  - clang-tidy无Critical/High违规
  - cppcheck无AUTOSAR规则违反
  - 代码审查检查AUTOSAR清单（见设计文档§9.4.7）

**文档要求**:
- API使用Doxygen注释
- 复杂算法添加设计文档
- 性能优化添加基准对比

---

## 附录: 参考资料

### 技术文档
- [IPC_DESIGN_ARCHITECTURE.md](IPC_DESIGN_ARCHITECTURE.md) - **完整设计文档（必读）**
  - 第3章: 共享内存管理（Phase 1核心）
  - 第4章: 消息传递模式（Phase 2-3核心）
  - 第5章: 运行时流程详解（实现参考）
  - 第6章: 性能优化（Phase 4-6参考）
  - 第7章: 安全性设计（可选模块参考）
  - 第8章: 测试方案（Phase 6参考）
  - 第9章: AUTOSAR合规性（Phase 6参考）
- [iceoryx2 Book](https://ekxide.github.io/iceoryx2-book) - 参考实现
- [AUTOSAR AP SWS_Core](https://www.autosar.org/) - 规范文档

### 性能基准
- iceoryx2: ~600ns延迟, 1M+ msg/s吞吐量
- LightAP目标: < 1μs延迟, 1M+ msg/s吞吐量

### 工具链
- [Google Test](https://github.com/google/googletest)
- [Google Benchmark](https://github.com/google/benchmark)
- [Valgrind](https://valgrind.org/)
- [perf](https://perf.wiki.kernel.org/)

---

**文档维护**: 本文档应随开发进度每周更新  
**联系人**: LightAP Core Team  
**最后更新**: 2026-01-07
