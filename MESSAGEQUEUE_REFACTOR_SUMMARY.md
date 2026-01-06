# MessageQueueBlock重构总结 (2026-01-02)

## 重构方案：方案A - 分离职责

### 核心设计

**NodePool (新增)**
- 职责：Chunk节点分配（loan/free操作）
- 实现：Node-based Treiber stack
- 用途：预留给未来chunk管理使用
- 位置：`CNodePool.hpp`, `CNodePool.cpp`

**MessageQueueBlock (重构)**
- 职责：消息队列（数据队列和消息队列）
- 实现：Fixed-size ringbuffer with CAS operations
- 用途：rx_queue和completion_queue
- 特性：每个队列拥有独立的ringbuffer实例

### 架构变更

**之前（共享节点池）：**
```
queue_node_block_ (一个共享的MessageQueueBlock)
    ├── rx_queue_1 (共享head/tail)
    ├── rx_queue_2 (共享head/tail)
    ├── completion_queue_1 (共享head/tail)
    └── completion_queue_2 (共享head/tail)
```
**问题：** 所有队列共享同一个ringbuffer的head/tail指针

**现在（独立ringbuffer）：**
```
Subscriber 1:
    ├── rx_queue (独立MessageQueueBlock实例)
    └── completion_queue (独立MessageQueueBlock实例)
    
Subscriber 2:
    ├── rx_queue (独立MessageQueueBlock实例)
    └── completion_queue (独立MessageQueueBlock实例)
```
**优势：** 每个队列完全隔离，无共享状态冲突

### 代码变更

1. **新文件：**
   - `CNodePool.hpp/cpp` - Node-based chunk allocator

2. **MessageQueueBlock接口：**
   ```cpp
   // 之前：Node-based
   UInt32 allocateNode();
   void freeNode(UInt32 offset);
   
   // 现在：Ringbuffer
   bool enqueue(void* ptr);
   bool dequeue(void*& ptr);
   ```

3. **MessageQueue结构：**
   ```cpp
   struct MessageQueue {
       MessageQueueBlock* block;  // 独立实例
       void* block_memory;        // 队列自己的内存
       Size block_memory_size;
       
       bool initialize(void* memory, Size size, UInt32 capacity, bool use_shm);
       void cleanup();
   };
   ```

4. **内存分配：**
   - **之前：** 初始化时分配一个大的共享节点池
   - **现在：** createSubscriber时为每个队列分配独立内存

### 性能影响

**内存使用：**
- 每个subscriber需要 2 个ringbuffer（rx + completion）
- 默认capacity=256，每个队列 256 * 8 = 2KB
- 64个subscriber最大：64 * 2 * 2KB = 256KB

**性能优势：**
- ✅ 无缓存行冲突（独立内存）
- ✅ 无共享状态竞争
- ✅ 更好的局部性

### 测试结果

```
[==========] 10 tests from EventTest
[  PASSED  ] 10 tests (142 ms total)
```

**关键日志：**
```
[INFO] MessageQueueBlock: RingBuffer initialized - addr=..., capacity=8, block_size=8, total=64
```
每个subscriber创建时输出2次（rx_queue + completion_queue）

### 未来扩展

NodePool已就绪，可用于：
- Chunk节点的loan/free操作
- 任何需要offset-based allocation的场景
- 跨进程共享的对象池

## 总结

成功实现方案A：
- MessageQueueBlock专注于ringbuffer队列（数据队列/消息队列）
- NodePool独立处理chunk分配（Node-based）
- 每个队列拥有独立内存，完全隔离
- 所有测试通过✅
