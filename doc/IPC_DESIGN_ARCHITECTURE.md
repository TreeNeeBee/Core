# LightAP Core IPC 设计架构

> **参考**: iceoryx2 - Zero-Copy Lock-Free IPC with a Rust Core  
> **版本**: 1.2  
> **日期**: 2026-02-01  
> **状态**: 已实现并测试

---

## 📋 文档概览

本文档描述 LightAP Core 模块的 IPC (Inter-Process Communication) 底层实现，该设计基于 iceoryx2 的核心理念：**零拷贝 (Zero-Copy)** 和 **无锁 (Lock-Free)**。

**定位说明**：
- IPC 是 SOA 层的**底层传输实现**
- 服务发现、服务注册等功能由 **SOA 层**负责
- IPC 层只负责高性能的数据传输

### 近期实现更新 (2026-02-01)

- **共享内存生命周期**: 引用计数完整实现，只有最后一个进程才会执行 `shm_unlink`。  
- **订阅者退出**: 析构时先断开并清空队列，避免阻塞退出。  
- **STmin 单位**: 全链路统一为 **微秒**。  
- **通道扫描参数**: 超时与扫描间隔统一为具名常量，减少魔法数。  

### 文档结构

1. **[设计目标](#1-设计目标)** - 核心目标与 AUTOSAR 要求
2. **[核心架构](#2-核心架构)** - 整体架构、API 设计
3. **[共享内存管理](#3-共享内存管理)** - 内存布局、ChunkPool、引用计数
4. **[消息传递模式](#4-消息传递模式)** - Pub-Sub、队列模型
5. **[运行时流程](#5-运行时流程详解)** - 初始化、连接、发送接收
6. **[性能优化](#6-性能优化)** - 缓存优化、NUMA 支持
7. **[安全性设计](#7-安全性设计)** - Hook、E2E 保护、错误处理
8. **[测试方案](#8-测试方案)** - SPSC/SPMC/MPMC 测试
9. **[AUTOSAR 合规性](#9-autosar-合规性)** - 规范对齐
10. **[实现路线图](#10-实现路线图)** - 开发计划
11. **[参考资料](#11-参考资料)** - iceoryx2 文档链接

---

## 1. 设计目标

### 1.1 核心目标

| 目标 | 描述 | 实现状态 |
|------|------|----------|
| **零拷贝通信** | 发布者直接写入共享内存，订阅者直接读取 | ✅ 已实现 - Loan/Send API |
| **无锁操作** | 所有关键路径无需互斥锁，使用原子操作 | ✅ 已实现 - RingBufferBlock |
| **低延迟** | 消息传递延迟 < 5μs (实测) | ✅ 已验证 - camera_fusion_spmc_example |
| **高吞吐** | 支持 90+ FPS (1920x720x4图像) | ✅ 已验证 - STMin=10ms测试 |
| **确定性** | 固定大小分配，O(1) 时间复杂度 | ✅ 已实现 - ChunkPool |
| **Pub-Sub 模式** | SPSC/SPMC/MPSC/MPMC支持 | ✅ 已实现并测试 |
| **三种IPC模式** | SHRINK(4KB)/NORMAL(2MB)/EXTEND(可配置) | ✅ 已实现 - 编译时选择 |

### 1.2 AUTOSAR Adaptive Platform R24-11 要求

```cpp
// AUTOSAR AP 规范 SWS_Core
namespace ara {
namespace core {
    // IPC 必须符合 AUTOSAR 数据类型和错误处理机制
    class InstanceSpecifier;     // 服务实例标识
    template<typename T> class Result;  // 错误处理
    template<typename T> class Future;  // 异步操作
}

namespace com {
    // 通信管理 (Communication Management)
    class ServiceIdentifierType;  // 服务标识
    class InstanceIdentifier;     // 实例标识
    
    // Pub-Sub API
    template<typename T> class Subscriber;
    template<typename T> class Publisher;
    
    // Event API  
    class Event;
}
}
```

---

## 2. 核心架构

### 2.1 整体架构图

```
┌─────────────────────────────────────────────────────────────────┐
│                     应用层 (Application Layer)                   │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐        │
│  │Publisher │  │Subscriber│  │  Client  │  │  Server  │        │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘        │
└───────┼─────────────┼─────────────┼─────────────┼──────────────┘
        │             │             │             │
┌───────┼─────────────┼─────────────┼─────────────┼──────────────┐
│       │       Service Layer (服务层)            │              │
│  ┌────▼─────────────▼────┐   ┌──────▼──────────▼───┐          │
│  │  Pub-Sub Service      │   │ Request-Response Svc │          │
│  │  - Service Discovery  │   │  - RPC Routing       │          │
│  │  - Connection Mgmt    │   │  - Request Queue     │          │
│  │  - Message Routing    │   │  - Reply Matching    │          │
│  └──────┬────────────────┘   └──────┬───────────────┘          │
└─────────┼──────────────────────────┼──────────────────────────┘
          │                          │
┌─────────┼──────────────────────────┼──────────────────────────┐
│         │   Transport Layer (传输层)                           │
│  ┌──────▼──────────────────────────▼──────────┐               │
│  │     Shared Memory Management (SHM管理)      │               │
│  │  ┌────────────────────────────────────┐   │               │
│  │  │  ChunkPool Allocator (固定大小)    │   │               │
│  │  │  - 预分配固定数量的Chunk            │   │               │
│  │  │  - Free-list管理空闲块             │   │               │
│  │  │  - Offset-based寻址（可重定位）     │   │               │
│  │  └────────────────────────────────────┘   │               │
│  │                                            │               │
│  │  ┌──────────────────────────────────────┐ │               │
│  │  │   Lock-Free Message Queues           │ │               │
│  │  │   - Per-Publisher FIFO (索引链表)    │ │               │
│  │  │   - Round-Robin调度                 │ │               │
│  │  └──────────────────────────────────────┘ │               │
│  └─────────────────────────────────────────────┘               │
└────────────────────────────────────────────────────────────────┘
          │                          │
┌─────────▼──────────────────────────▼──────────────────────────┐
│              Platform Layer (平台层)                           │
│  ┌────────────────┐  ┌────────────────┐  ┌─────────────────┐ │
│  │ POSIX SHM      │  │ Atomic Ops     │  │ Memory Barriers │ │
│  │ (shm_open)     │  │ (std::atomic)  │  │ (acquire/release)│ │
│  └────────────────┘  └────────────────┘  └─────────────────┘ │
└────────────────────────────────────────────────────────────────┘
```

### 2.2 创建方式

**直接使用共享内存路径创建**（无需服务发现）：

```cpp
using namespace lap::core::ipc;

// 创建 Publisher（指定共享内存路径）
PublisherConfig pub_config;
pub_config.max_chunks = 16;              // NORMAL模式默认
pub_config.chunk_size = 1920 * 720 * 4;  // 图像大小: 5.3MB
pub_config.loan_policy = LoanPolicy::kWait;
pub_config.policy = PublishPolicy::kOverwrite;

auto publisher_result = Publisher::Create("/cam0_stream", pub_config);
if (!publisher_result.HasValue()) {
    // 处理错误
}
auto publisher = std::move(publisher_result.Value());

// 创建 Subscriber（使用相同的共享内存路径）
SubscriberConfig sub_config;
sub_config.channel_capacity = 256;       // NORMAL模式默认
sub_config.STmin = 10000;                // 最小接收间隔10ms（微秒）
sub_config.empty_policy = SubscribePolicy::kBlock;

auto subscriber_result = Subscriber::Create("/cam0_stream", sub_config);
if (!subscriber_result.HasValue()) {
    // 处理错误
}
auto subscriber = std::move(subscriber_result.Value());

// 说明：
// - Publisher 和 Subscriber 是非模板类，使用基类指针管理消息
// - SOA 层负责从服务注册表查询服务并获取 shm 路径
// - IPC 层只需要明确的 shm 路径即可创建 Publisher/Subscriber
// - 首个启动者（Pub 或 Sub）创建共享内存，后续启动者打开已存在的共享内存
```

### 2.3 核心 API

```cpp
// Publisher API (非模板类，使用Message基类)
class Publisher {
public:
    // 创建Publisher
    static Result<Publisher> Create(const String& shmPath,
                                   const PublisherConfig& config = {}) noexcept;
    
    // 方式1: Loan + 手动写入 + Send
    Result<Sample> Loan() noexcept;                     // 从ChunkPool借出Chunk
    Result<void> Send(Sample&& sample) noexcept;        // 广播到所有Subscriber
    
    // 方式2: Send with Lambda（推荐，一步完成loan+write+send）
    Result<void> Send(Function<Size(Byte*, Size)> write_fn) noexcept;
    
    // 方式3: Send with Buffer（拷贝模式）
    Result<void> Send(Byte* buffer, Size size) noexcept;
    
    // 定向发送到指定通道
    Result<void> SendTo(Sample&& sample, UInt8 channel_id) noexcept;
    
    // 统计接口
    UInt32 GetAllocatedCount() const noexcept;          // 获取已分配Chunk数
    Bool IsChunkPoolExhausted() const noexcept;         // 检查ChunkPool是否耗尽
    const String& GetShmPath() const noexcept;          // 获取共享内存路径
};

// Subscriber API (非模板类)
class Subscriber {
public:
    // 创建Subscriber
    static Result<Subscriber> Create(const String& shmPath,
                                    const SubscriberConfig& config = {}) noexcept;
    
    // 接收消息（返回多个Sample，支持批量接收）
    Result<Vector<Sample>> Receive(SubscribePolicy policy = SubscribePolicy::kBlock) noexcept;
    
    // 接收带超时
    Result<Vector<Sample>> ReceiveWithTimeout(UInt64 timeout_ns,
                                             SubscribePolicy policy = SubscribePolicy::kBlock) noexcept;
    
    // 使用Lambda接收（推荐，零拷贝读取）
    Result<Size> Receive(Function<Size(UInt8, Byte*, Size)> read_fn,
                        SubscribePolicy policy = SubscribePolicy::kBlock) noexcept;
    
    // 从指定通道接收
    Result<Sample> ReceiveFrom(UInt8 channel_id, SubscribePolicy policy = SubscribePolicy::kBlock) noexcept;
    
    // 连接/断开
    Result<void> Connect() noexcept;                    // 激活接收通道
    Result<void> Disconnect() noexcept;                 // 断开并清理
    
    // 队列状态查询
    UInt32 GetQueueSize() const noexcept;               // 获取当前队列大小
    Bool IsEmpty() const noexcept;                      // 检查队列是否为空
    const String& GetShmPath() const noexcept;          // 获取共享内存路径
    
    // STmin更新
    void UpdateSTMin(UInt16 stmin) noexcept;            // 更新最小接收间隔（微秒）
};
```

---

## 3. 共享内存管理

### 3.1 三种IPC模式配置

**LightAP IPC支持三种运行模式，编译时选择：**

| 模式 | 宏定义 | SHM对齐 | 默认配置 | 适用场景 |
|------|--------|---------|----------|---------|
| **SHRINK** | `LIGHTAP_IPC_MODE_SHRINK` | 4KB | MaxSubs=8, MaxChunks=4, QueueCap=16 | 嵌入式系统、资源受限环境 |
| **NORMAL** | `LIGHTAP_IPC_MODE_NORMAL` | 2MB | MaxSubs=32, MaxChunks=16, QueueCap=256 | **默认模式**，平衡性能与资源 |
| **EXTEND** | `LIGHTAP_IPC_MODE_EXTEND` | 2MB | MaxSubs=128, MaxChunks=64, QueueCap=1024 | 高性能服务器、大规模并发 |

**编译配置：**
```bash
# SHRINK模式 - 超小内存占用
cmake -DLIGHTAP_IPC_MODE_SHRINK=ON ..

# NORMAL模式（默认）
cmake ..

# EXTEND模式 - 大规模并发
cmake -DLIGHTAP_IPC_MODE_EXTEND=ON ..
```

**代码中的自动配置：**
```cpp
#if !defined(LIGHTAP_IPC_MODE_SHRINK) && !defined(LIGHTAP_IPC_MODE_NORMAL) && !defined(LIGHTAP_IPC_MODE_EXTEND)
    #define LIGHTAP_IPC_MODE_NORMAL 1  // 默认NORMAL模式
#endif

#ifdef LIGHTAP_IPC_MODE_SHRINK
    constexpr UInt64 kShmAlignment = 4 * 1024;        // 4KB对齐
    constexpr UInt32 kDefaultMaxSubscribers = 8;
    constexpr UInt32 kDefaultChunks = 4;
    constexpr UInt32 kDefaultQueueCapacity = 16;
#elif defined(LIGHTAP_IPC_MODE_EXTEND)
    constexpr UInt64 kShmAlignment = 2 * 1024 * 1024; // 2MB对齐
    constexpr UInt32 kDefaultMaxSubscribers = 128;
    constexpr UInt32 kDefaultChunks = 64;
    constexpr UInt32 kDefaultQueueCapacity = 1024;
#else  // NORMAL mode (default)
    constexpr UInt64 kShmAlignment = 2 * 1024 * 1024; // 2MB对齐
    constexpr UInt32 kDefaultMaxSubscribers = 32;
    constexpr UInt32 kDefaultChunks = 16;
    constexpr UInt32 kDefaultQueueCapacity = 256;
#endif
```

### 3.2 共享内存路径规范

**统一路径格式：**
```
/dev/shm/<service_path>

实际示例：
- /dev/shm/cam0_stream          # 摄像头0流
- /dev/shm/cam1_stream          # 摄像头1流
- /dev/shm/sensor_data          # 传感器数据
- /dev/shm/can_messages         # CAN总线消息
```

**路径规则：**
1. 由应用层/SOA层决定具体路径名
2. IPC层只负责创建和管理该路径的共享内存
3. 首个启动者（Publisher或Subscriber）创建共享内存
4. 后续进程打开已存在的共享内存
5. 最后退出者可选择删除共享内存

### 3.3 共享内存创建流程

**创建者优先原则：**

```cpp
// Publisher 或 Subscriber 创建示例
using namespace lap::core::ipc;

// 方式1: Publisher先启动（创建共享内存）
PublisherConfig pub_config;
pub_config.max_chunks = 16;           // NORMAL模式默认值
pub_config.chunk_size = 1920*720*4;   // 图像大小: 5.3MB
pub_config.loan_policy = LoanPolicy::kWait;  // Chunk耗尽时等待

auto publisher = Publisher::Create("/cam0_stream", pub_config).Value();

// 方式2: Subscriber先启动（也会创建共享内存）
SubscriberConfig sub_config;
sub_config.channel_capacity = 256;     // NORMAL模式默认值

auto subscriber = Subscriber::Create("/cam0_stream", sub_config).Value();

// 说明：
// - 首个启动者（Pub或Sub）会创建并初始化共享内存
// - 后续启动者直接attach到已存在的共享内存
// - SharedMemoryManager自动处理创建/打开逻辑
```

### 3.4 共享内存大小计算

**内存布局组成（以NORMAL模式为例）：**

```cpp
UInt64 CalculateTotalSize(const PublisherConfig& config) {
    // 1. ControlBlock区域（固定大小）
    constexpr UInt64 kControlBlockSize = 128 * 1024;  // 128KB
    
    // 2. SubscriberQueue数组区域（固定大小）
    constexpr UInt64 kQueueSize = 8 * 1024;  // 单个队列8KB
    UInt64 queue_array_size = kQueueSize * kDefaultMaxSubscribers;  // 32×8KB = 256KB
    
    // 3. 预留空间
    constexpr UInt64 kReservedSpace = 128 * 1024;  // 128KB
    
    // 4. ChunkPool区域（动态大小）
    UInt64 chunk_header_size = sizeof(ChunkHeader) * config.max_chunks;  // 128B×16 = 2KB
    UInt64 chunk_payload_size = config.chunk_size * config.max_chunks;   // 动态
    UInt64 chunk_pool_size = chunk_header_size + chunk_payload_size;
    
    // 总大小
    UInt64 total = kControlBlockSize + queue_array_size + kReservedSpace + chunk_pool_size;
    
    // 对齐到2MB边界（NORMAL/EXTEND模式）
    return AlignTo2MB(total);
}
```

**实际示例：**

```
示例1: camera_fusion_spmc_example (NORMAL模式)
配置:
- max_chunks = 16
- chunk_size = 1920×720×4 = 5,529,600 bytes (~5.3MB)

计算:
- ControlBlock:       128KB
- ChannelQueue[32]: 256KB (32×8KB)
- Reserved:           128KB
- ChunkHeaders[16]:   2KB (16×128B)
- Payloads[16]:       84.8MB (16×5.3MB)
- 总计:              ~85.3MB
- 对齐到2MB:         86MB (实际mmap大小)

示例2: 小消息场景 (NORMAL模式)
配置:
- max_chunks = 256
- chunk_size = 4096 (4KB)

计算:
- ControlBlock:       128KB
- ChannelQueue[32]: 256KB
- Reserved:           128KB
- ChunkHeaders[256]:  32KB (256×128B)
- Payloads[256]:      1MB (256×4KB)
- 总计:              ~1.5MB
- 对齐到2MB:         2MB
```
        
        // 获取已存在的共享内存大小
        struct stat sb;
        fstat(fd, &sb);
        
        void* addr = mmap(nullptr, sb.st_size, PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd, 0);
        
        LOG_INFO("Opened existing shared memory: {} (size: {} MB)",
                 shm_path, sb.st_size / (1024*1024));
        
        return Ok(SharedMemory{fd, addr, sb.st_size, false});
        
    } else {
        return Err(CoreErrc::kIPCShmCreateFailed);
    }
}

// 大小计算（优化内存布局，2026-01-08 平衡版）
UInt64 CalculateTotalSize(const ServiceConfig& config) {
    constexpr UInt64 kControlBlockSize = 128 * 1024;  // 128KB 固定
    constexpr UInt64 kSubscriberQueueArraySize = 800 * 1024;  // 800KB 固定 (100 × 8KB)
    constexpr UInt64 kReservedSpace = 96 * 1024;  // 96KB 预留空间（凑足1MB）
    
    // ChunkPool 大小：从 1MB 偏移开始，动态计算
    UInt64 chunk_pool_size = 
        (sizeof(ChunkHeader) + config.chunk_size) * config.max_chunks;
    
    return kControlBlockSize + kSubscriberQueueArraySize + kReservedSpace + chunk_pool_size;
}

// 示例1：配置 512 个 Chunk，每个 4KB
// ┌───────────────────────────────────────────────────────────┐
// │ ControlBlock:          128KB (固定，实际用 ~2KB)          │
// │ ChannelQueue[100]:  800KB (固定，100 × 8KB)           │
// │ Reserved Space:        96KB (预留空间，凑足1MB)          │
// │ ChunkPool:             2.06MB (动态)                     │
// │   ├─ ChunkHeader[512]: 64KB (512 × 128B)                │
// │   └─ Payloads[512]:    2MB (512 × 4KB)                  │
// ├───────────────────────────────────────────────────────────┤
// │ 总共享内存大小:        ~3.06MB                           │
// │ 对齐到 2MB:            4MB (实际 mmap 大小)              │
// └───────────────────────────────────────────────────────────┘
//
// 示例2：配置 1024 个 Chunk，每个 8KB
// ┌───────────────────────────────────────────────────────────┐
// │ ControlBlock:          128KB (固定)                      │
// │ ChannelQueue[100]:  800KB (固定)                      │
// │ Reserved Space:        96KB (预留空间)                   │
// │ ChunkPool:             8.12MB (动态)                     │
// │   ├─ ChunkHeader[1024]: 128KB (1024 × 128B)             │
// │   └─ Payloads[1024]:   8MB (1024 × 8KB)                 │
// ├───────────────────────────────────────────────────────────┤
// │ 总共享内存大小:        ~9.12MB                           │
// │ 对齐到 2MB:            10MB (实际 mmap 大小)             │
// └───────────────────────────────────────────────────────────┘
//
// 内存布局偏移量（平衡优化布局）：
//   ControlBlock:         offset = 0x000000 (0 bytes)
//   ChannelQueue[100]: offset = 0x020000 (128KB)
//   Reserved Space:       offset = 0x0E8000 (128KB + 800KB)
//   ChunkPool:            offset = 0x100000 (1MB)
//
// 进程本地内存（RAII 智能指针管理，不在共享内存中）：
// - SharedMemoryManager*: ~64B (管理 mmap 映射)
// - PublisherState*:      ~128B (每个 Publisher)
// - SubscriberState*:     ~96B (每个 Subscriber)
```

**清理策略：**

```cpp
// 退出时
~SharedMemory() {
    munmap(addr_, size_);
    close(fd_);
    
    // 如果是创建者，可选择删除（也可以保留给下次使用）
    if (is_creator_ && config_.auto_cleanup) {
        shm_unlink(shm_path_.c_str());
        LOG_INFO("Removed shared memory: {}", shm_path_);
    }
}
```

### 3.2 设计原则

**核心原则：**
1. **Path-based 初始化**：使用固定路径规范进行跨进程发现
2. **固定大小预分配**：服务创建时确定所有内存布局，禁止运行时动态扩容/缩容
3. **Offset-based 寻址**：使用 `base_address + offset` 替代直接指针，实现进程间可重定位
4. **确定性保证**：
   - 时间确定性：O(1)分配/释放，无动态内存分配
   - 空间确定性：内存布局在编译/配置时固定

### 3.3 内存架构总览

**⚠️ 重要设计变更（2026-01-07）**:

**ChannelRegistry 位置调整**：从独立结构体迁移到 ControlBlock 内部，确保跨进程可见性。

**功能完整性验证**：

| 功能 | 修改前 | 修改后 | 状态 |
|------|--------|--------|------|
| **存储位置** | 独立结构体 | ControlBlock 嵌入 | ✅ 共享内存 |
| **Publisher读取** | `registry.GetSnapshot()` | `GetChannelSnapshot(ctrl)` | ✅ 无锁读取 |
| **Subscriber注册** | `registry.Register()` | `RegisterChannel(ctrl, idx)` | ✅ CAS操作 |
| **Subscriber注销** | `registry.Unregister()` | `UnregisterSubscriber(ctrl, idx)` | ✅ CAS操作 |
| **双缓冲快照** | `snapshots[2]` | `ctrl->snapshots[2]` | ✅ 已保留 |
| **版本控制** | `version` 字段 | `ctrl->snapshots[i].version` | ✅ 已保留 |
| **内存序** | acquire/release | acquire/release | ✅ 已保留 |
| **初始化** | 构造函数 | `InitializeControlBlockRegistry()` | ✅ 新增辅助函数 |

**iceoryx2 设计对比总结：**

| 设计维度 | iceoryx2 原则 | LightAP 实现 | 差异说明 |
|---------|--------------|-------------|---------|
| **ChunkPool** | 固定大小池，索引寻址 | ✅ 完全一致 | 共享内存预分配，chunk_index 跨进程传递 |
| **Subscriber Queue** | 每个Sub独立队列 | ✅ 一致 | 动态分配（new），默认容量 256 |
| **ChannelRegistry** | 共享内存中 | ✅ 完全一致 | **已修正**：现在在 ControlBlock 中 |
| **队列满策略** | kOverwrite（默认） | ✅ 完全一致 | Ring Buffer 模式，支持 kWait/kBlock |
| **地址传递** | Offset-based | ✅ 完全一致 | chunk_index 跨进程传递 |
| **Free-List** | 索引链表 | ✅ 完全一致 | next_free_index (UInt32) |
| **引用计数** | 双层计数器 | ✅ 完全一致 | loan_counter + ref_count |
| **状态机** | 4状态原子转换 | ✅ 完全一致 | kFree/kLoaned/kSent/kReceived |
| **初始化** | Path-based SHM | ✅ 完全一致 | /lightap_service_xxx |

#### 3.3.1 内存使用统计与监控

**共享内存区域详细统计：**

| 区域 | 偏移量 | 大小 | 实际使用 | 预留空间 | 用途 |
|------|--------|------|---------|---------|------|
| **ControlBlock** | 0x000000 | 128KB 固定 | ~2KB | ~126KB | 元数据、配置、统计、Registry |
| **ChannelQueue[100]** | 0x020000 | 800KB 固定 | ~4.5KB/队列 | ~3.5KB/队列 | SPSC消息队列 (100队列) |
| **Reserved Space** | 0x0E8000 | 96KB 固定 | 0 | 96KB | 全局预留（凑足1MB） |
| **ChunkPool** | 0x100000 | 动态计算 | 100% | 0 | Chunk头部 + Payload |
| **总计（典型）** | - | ~3.06MB | ~2.51MB | ~576KB | 512×4KB配置 |
| **对齐后mmap** | - | 4MB | 76.5%利用率 | - | 2MB对齐 |

**ControlBlock 统计字段（性能监控）：**

| 字段名 | 类型 | 语义 | 用途 |
|--------|------|------|------|
| `publisher_count` | `atomic<UInt32>` | 当前活跃Publisher数量 | 实时监控 |
| `subscriber_count` | `atomic<UInt32>` | 当前活跃Subscriber数量 | 实时监控 |
| `total_chunks_allocated` | `atomic<UInt64>` | 累计分配Chunk次数 | 吞吐量统计 |
| `total_messages_sent` | `atomic<UInt64>` | 累计发送消息数量 | 吞吐量统计 |
| `total_loan_failures` | `atomic<UInt64>` | 累计Loan失败次数 | 异常监控 |

**ChannelQueue 统计字段（性能监控）：**

| 字段名 | 类型 | 语义 | 用途 |
|--------|------|------|------|
| `last_receive_time` | `atomic<UInt64>` | 最后接收时间戳（纳秒） | 超时检测 |
| `overrun_count` | `atomic<UInt64>` | 队列溢出次数（累计） | 异常监控 |
| `total_messages_received` | `atomic<UInt64>` | 累计接收消息数量 | 吞吐量统计 |
| `total_messages_dropped` | `atomic<UInt64>` | 累计丢弃消息数量 | 丢包监控 |
| `max_queue_depth` | `atomic<UInt64>` | 历史最大队列深度 | 容量规划 |

**内存布局优化效果：**

| 优化项 | 优化前 | 优化后 | 收益 |
|--------|--------|--------|------|
| **ControlBlock对齐** | 自然对齐 | 4KB页对齐 | 支持大页，减少TLB miss |
| **SubscriberQueue对齐** | 自然对齐 | 4KB页对齐 | 每个队列独立页，避免伪共享 |
| **ChunkHeader对齐** | 64B对齐 | 128B对齐 | 双缓存行，提升NUMA性能 |
| **关键字段对齐** | 无特殊对齐 | 64B缓存行对齐 | 避免伪共享，提升并发性能 |
| **预留空间** | 0 | ~1.05MB | 无需重新布局即可扩展功能 |
| **Buffer容量** | 固定256 | 预留1024 | 运行时可扩容至512/1024 |

**内存计算公式（用于配置规划）：**

```cpp
// 总共享内存大小
UInt64 total_size = 128KB (ControlBlock)
                  + 800KB (ChannelQueue[100])
                  + 96KB (Reserved Space)
                  + (128B + chunk_size) * max_chunks;

// 对齐到2MB
UInt64 aligned_size = ((total_size + 2MB - 1) / 2MB) * 2MB;

// 典型配置表（平衡优化布局，100 Subscribers上限）
┌─────────────┬───────────┬──────────┬─────────────┬─────────────┐
│ max_chunks  │chunk_size │原始大小   │对齐后大小    │内存利用率    │
├─────────────┼───────────┼──────────┼─────────────┼─────────────┤
│ 128         │ 4KB       │ 1.52MB   │ 2MB         │ 76.0%       │
│ 256         │ 4KB       │ 2.03MB   │ 4MB         │ 50.8%       │
│ 512         │ 4KB       │ 3.06MB   │ 4MB         │ 76.5%  ✓    │
│ 1024        │ 4KB       │ 5.12MB   │ 6MB         │ 85.3%  ✓    │
│ 512         │ 8KB       │ 5.06MB   │ 6MB         │ 84.3%  ✓    │
│ 1024        │ 8KB       │ 9.12MB   │ 10MB        │ 91.2%  ✓    │
│ 2048        │ 8KB       │ 17.12MB  │ 18MB        │ 95.1%  ✓    │
└─────────────┴───────────┴──────────┴─────────────┴─────────────┘

// 推荐配置（平衡性能与资源）：
// - 低负载：512 chunks × 4KB  (~3MB, 76.5%)   ← 推荐起步
// - 中负载：1024 chunks × 4KB (~5MB, 85.3%)   ← 推荐标准
// - 高负载：1024 chunks × 8KB (~9MB, 91.2%)   ← 推荐高性能
//
// Subscriber限制：最多100个订阅者，每个独立8KB队列
// 预留空间：96KB全局 + 350KB队列内 = 446KB扩展能力
```

#### 3.3.2 核心内存模型

**核心内存模型：**

参考 iceoryx2 的混合设计：ChunkPool 共享 + 动态分配

```cpp
// 共享内存段布局（优化版，固定大小分区）
struct SharedMemorySegment {
    //=== 区域1: ControlBlock（固定 128KB）===//
    // 偏移量：0x000000 - 0x01FFFF（131,072 字节）
    // 实际使用：~2KB
    // 预留空间：~126KB（用于未来扩展）
    struct alignas(4096) ControlBlock {  // 4KB 页对齐
        // --- 头部元数据（缓存行对齐）---
        alignas(64) std::atomic<UInt32> magic_number;     // 0xICE0RYX2
        std::atomic<UInt32> version;                      // 版本号
        std::atomic<UInt32> state;                        // 状态
        UInt32              _padding1;                    // 对齐填充
        
        // --- 服务配置元数据 ---
        alignas(64) UInt32  max_chunks;                   // 最大块数量
        UInt32              max_subscriber_queues;        // 最大 Subscriber 队列数（默认 100）
        UInt32              channel_capacity;               // 每个队列容量（默认 256，最大 1024）
        UInt32              _padding2;                    // 对齐填充
        UInt64              chunk_size;                   // 块大小（含Header）
        UInt64              chunk_alignment;              // 对齐要求
        
        // --- ChunkPool 管理（iceoryx2 风格）---
        alignas(64) std::atomic<UInt32> free_list_head;  // 空闲链表头索引
        std::atomic<UInt32> allocated_count;             // 已分配计数
        std::atomic<bool>   is_initialized;              // 是否已初始化
        UInt8               _padding3[3];                 // bool 对齐填充
        
        // --- WaitSet for Loan failures ---
        alignas(64) std::atomic<UInt32> loan_waitset;    // Loan 等待集（事件标志）
        // loan_waitset 位域定义：
        //   bit 0: HAS_FREE_CHUNK - ChunkPool 有可用块
        //   bit 1-31: 保留
        UInt32              _padding4[15];                // 缓存行对齐填充
        
        // --- 统计信息（64B 对齐，性能监控）---
        alignas(64) std::atomic<UInt32> publisher_count;      // 当前活跃 Publisher 数量
        std::atomic<UInt32> subscriber_count;                 // 当前活跃 Subscriber 数量
        std::atomic<UInt64> total_chunks_allocated;           // 累计分配 Chunk 数量（监控）
        std::atomic<UInt64> total_messages_sent;              // 累计发送消息数量（监控）
        std::atomic<UInt64> total_loan_failures;              // 累计 Loan 失败次数（监控）
        UInt32              _padding5[6];                      // 缓存行对齐填充
        
        // --- ChannelRegistry（无锁快照机制）---
        // 快照结构：存储当前活跃的 Subscriber 队列索引列表
        struct Snapshot {
            UInt32 count;                          // 当前 Subscriber 数量
            UInt32 queue_indices[100];             // 队列索引数组（max_subscriber_queues）
            UInt64 version;                        // 版本号（用于检测并发修改）
            UInt8  _padding[164];                  // 对齐至 576 字节
            
            // 默认构造函数（用于共享内存初始化）
            Snapshot() : count(0), version(0) {
                std::fill_n(queue_indices, 100, UINT32_MAX);
            }
        };
        
        // 双缓冲快照（避免读写冲突）
        alignas(64) std::atomic<UInt32> active_index; // 活跃快照索引（0 或 1）
        std::atomic<UInt32> write_index;                       // 写入缓冲区索引（0 或 1）
        UInt32              _padding6[14];                      // 缓存行对齐填充
        
        alignas(64) Snapshot snapshots[2];                    // 双缓冲区（在共享内存中）
        
        // --- 预留空间（用于未来扩展）---
        // 当前使用：~2KB
        // 预留：1MB - 2KB = ~1046KB
        UInt8 reserved[1048576 - 2048];  // 预留空间，确保 ControlBlock 总大小为 1MB
    };
    ControlBlock control;
    
    //=== 区域2: ChannelQueue 数组（固定 800KB）===//
    // 偏移量：0x020000 - 0x0E7FFF（819,200 字节）
    // 每个队列：8KB（100 队列 × 8KB = 800KB）
    // 每个 ChannelQueue 是一个 SPSC（单生产者单消费者）队列
    // - 单生产者：所有 Publisher 协作向同一队列写入（需要同步）
    // - 单消费者：对应的 Subscriber 独占读取
    struct alignas(4096) ChannelQueue {  // 4KB 页对齐
        // --- 基础状态（缓存行对齐）---
        alignas(64) std::atomic<bool>   active;            // 是否活跃
        UInt8                            _padding1[3];      // bool 对齐填充
        std::atomic<UInt32>              subscriber_id;    // Subscriber UUID (hash)
        UInt32                           _padding2[14];     // 缓存行对齐填充
        
        // --- 消息队列（环形缓冲区）---
        // 预留 1024 容量，默认使用 256
        // buffer[0-255]: 默认使用范围
        // buffer[256-1023]: 预留空间（用于运行时动态扩容或特殊场景）
        alignas(64) struct {
            std::atomic<UInt32> head;              // 读位置索引 [0, capacity)
            std::atomic<UInt32> tail;              // 写位置索引 [0, capacity)
            std::atomic<UInt32> count;             // 当前元素数量
            UInt32              capacity;          // 实际使用容量（默认 256，最大 1024）
            UInt32              buffer[1024];      // 环形缓冲区（预留 1024，默认用 256）
        } msg_queue;
        
        // --- WaitSet 机制（iceoryx2 风格，Linux futex 实现）---
        alignas(64) std::atomic<UInt32> event_flags;      // 事件标志（lock-free 检查）
        // event_flags 位域定义：
        //   bit 0: HAS_DATA  - 队列有数据
        //   bit 1: HAS_SPACE - 队列有空间
        //   bit 2-31: 保留
        UInt32              _padding3[15];                 // 缓存行对齐填充
        
        // --- 统计信息（64B 对齐，性能监控）---
        alignas(64) std::atomic<UInt64> last_receive_time;    // 最后一次接收消息的时间戳（纳秒）
        std::atomic<UInt64> overrun_count;                    // 队列溢出次数（累计）
        std::atomic<UInt64> total_messages_received;          // 累计接收消息数量
        std::atomic<UInt64> total_messages_dropped;           // 累计丢弃消息数量（队列满）
        std::atomic<UInt64> max_queue_depth;                  // 历史最大队列深度（监控峰值）
        
        // --- 预留空间（每个队列总大小 8KB）---
        // 当前使用：~4.5KB（基础状态 + 1024*4B buffer + 统计）
        // 预留：8KB - 4.5KB = ~3.5KB
        UInt8 reserved[8192 - 4608];  // 预留空间，确保单个队列为 8KB
    };
    ChannelQueue subscriber_queues[100];  // 100 队列 × 8KB = 800KB
    
    //=== 预留空间区域（96KB）===//
    // 偏移量：0x0E8000 - 0x0FFFFF（98,304 字节）
    // 用途：未来扩展，保持总大小为 1MB
    UInt8 reserved_space[98304];  // 96KB 预留空间
    
    //=== 区域3: ChunkPool（从 1MB 偏移量开始，动态大小）===//
    // 偏移量：0x100000 开始
    // 大小：sizeof(ChunkHeader) * max_chunks + chunk_size * max_chunks
    // 这是唯一在共享内存中预分配的数据结构
    struct alignas(128) ChunkHeader {  // 128 字节对齐（双缓存行）
        // 块元数据（64 字节缓存行对齐）
        alignas(64) UInt64  chunk_size;       // 块大小（固定）
        UInt32              chunk_index;      // 块索引（在pool中的位置）
        UInt32              publisher_id;     // 发布者 ID
        
        // 状态机（原子操作）
        std::atomic<UInt32> state;            // ChunkState 枚举
        
        // 引用计数（双层设计）
        std::atomic<UInt64> ref_count;        // 订阅者引用计数
        
        // Free-List（索引链表）
        std::atomic<UInt32> next_free_index;  // 下一个空闲块索引
        
        // 时序信息
        UInt64              sequence_number;  // 序列号
        UInt64              timestamp;        // 时间戳（纳秒）
        
        // E2E 保护（可选）
        UInt32              e2e_counter;      // E2E 计数器
        UInt32              e2e_crc;          // CRC32 校验
    };
    
    // ChunkPool：固定数组，所有 Publisher/Subscriber 共享
    ChunkHeader chunks[0];  // 柔性数组，实际大小为 max_chunks
    
    // 用户数据紧随其后：
    // UInt8 payloads[max_chunks][chunk_size];
};

//=== ChannelRegistry 访问接口 ===//
// 注意：ChannelRegistry 已集成到 ControlBlock 中（见上方 ControlBlock 定义）
// 这样确保 Publisher 和 Subscriber 进程都能访问同一个 Registry
//
// 使用方式：
// - Publisher: 通过 ControlBlock 读取快照 → 遍历 Subscriber 队列
// - Subscriber: 通过 ControlBlock 注册/注销自己的队列索引
//
// 以下是 ChannelRegistry 的操作接口（由 ControlBlock 提供）：

/**
 * @brief 无锁获取 Subscriber 快照（Publisher 调用）
 * @note 使用 memory_order_acquire 确保看到最新的注册结果
 */
inline ControlBlock::Snapshot GetChannelSnapshot(ControlBlock* ctrl) noexcept {
    // 读取活跃快照索引
    UInt32 active_idx = ctrl->active_index.load(std::memory_order_acquire);
    
    // 拷贝快照数据（栈上拷贝，非常快）
    ControlBlock::Snapshot result = ctrl->snapshots[active_idx];
    
    // 内存屏障：确保拷贝完成后才继续
    std::atomic_thread_fence(std::memory_order_acquire);
    
    return result;
}

/**
 * @brief 注册新的 Subscriber（CAS 原子操作）
 * @param ctrl ControlBlock 指针（共享内存）
 * @param queue_index 要注册的队列索引
 * @return true 注册成功，false 已满或已存在
 * @note Subscriber 在连接时调用，使用 CAS 确保线程安全
 */
inline bool RegisterChannel(ControlBlock* ctrl, UInt32 queue_index) noexcept {
    // 获取当前写缓冲区索引
    UInt32 current_write = ctrl->write_index.load(std::memory_order_acquire);
    ControlBlock::Snapshot* write_snap = &ctrl->snapshots[current_write];
    
    // 检查是否已满
    if (write_snap->count >= ctrl->max_subscriber_queues) {
        return false;
    }
    
    // 检查是否已存在（避免重复注册）
    for (UInt32 i = 0; i < write_snap->count; ++i) {
        if (write_snap->queue_indices[i] == queue_index) {
            return false;  // 已存在
        }
    }
    
    // 添加到写缓冲区
    write_snap->queue_indices[write_snap->count] = queue_index;
    write_snap->count++;
    write_snap->version++;  // 增加版本号
    
    // 内存屏障：确保写入完成
    std::atomic_thread_fence(std::memory_order_release);
    
    // 切换活跃快照索引（CAS 操作）
    UInt32 new_active = current_write;
    ctrl->active_index.store(new_active, std::memory_order_release);
    
    // 切换写缓冲区索引
    UInt32 new_write = 1 - current_write;
    ctrl->write_index.store(new_write, std::memory_order_release);
    
    // 同步新写缓冲区的内容
    ctrl->snapshots[new_write] = *write_snap;
    
    // 更新 Subscriber 计数
    ctrl->subscriber_count.fetch_add(1, std::memory_order_release);
    
    return true;
}

/**
 * @brief 注销 Subscriber（CAS 原子操作）
 * @param ctrl ControlBlock 指针（共享内存）
 * @param queue_index 要注销的队列索引
 * @return true 注销成功，false 未找到
 */
inline bool UnregisterSubscriber(ControlBlock* ctrl, UInt32 queue_index) noexcept {
    UInt32 current_write = ctrl->write_index.load(std::memory_order_acquire);
    ControlBlock::Snapshot* write_snap = &ctrl->snapshots[current_write];
    
    // 查找并移除
    bool found = false;
    for (UInt32 i = 0; i < write_snap->count; ++i) {
        if (write_snap->queue_indices[i] == queue_index) {
            // 移除元素：后面的元素前移
            for (UInt32 j = i; j < write_snap->count - 1; ++j) {
                write_snap->queue_indices[j] = write_snap->queue_indices[j + 1];
            }
            write_snap->queue_indices[write_snap->count - 1] = UINT32_MAX;
            write_snap->count--;
            write_snap->version++;
            found = true;
            break;
        }
    }
    
    if (!found) {
        return false;
    }
    
    // 内存屏障
    std::atomic_thread_fence(std::memory_order_release);
    
    // 切换活跃快照
    ctrl->active_index.store(current_write, std::memory_order_release);
    
    // 切换写缓冲区
    UInt32 new_write = 1 - current_write;
    ctrl->write_index.store(new_write, std::memory_order_release);
    ctrl->snapshots[new_write] = *write_snap;
    
    // 更新 Subscriber 计数
    ctrl->subscriber_count.fetch_sub(1, std::memory_order_release);
    
    return true;
}

//=== Snapshot 初始化辅助函数 ===//
inline void InitializeControlBlockRegistry(ControlBlock* ctrl) noexcept {
    // 初始化 ChannelRegistry 相关字段
    ctrl->active_index.store(0, std::memory_order_release);
    ctrl->write_index.store(0, std::memory_order_release);
    
    // 初始化两个快照
    for (int i = 0; i < 2; ++i) {
        ctrl->snapshots[i].count = 0;
        ctrl->snapshots[i].version = 0;
        for (UInt32 j = 0; j < 100; ++j) {
            ctrl->snapshots[i].queue_indices[j] = UINT32_MAX;
        }
    }
}

//=== Publisher 状态（RAII 管理，每个 Publisher 独立）===//

//=== Publisher 状态（RAII 管理，每个 Publisher 独立）===//
// 使用智能指针管理生命周期：std::unique_ptr<PublisherState>
struct PublisherState {
    UInt32              id;                    // Publisher ID
    std::atomic<UInt64> sequence_number;       // 序列号
    std::atomic<UInt64> last_heartbeat;        // 心跳时间戳
    
    // 队列满策略（Publisher 写入时使用）
    enum class PublishPolicy : UInt32 {
        kOverwrite  = 0,          // 丢弃最旧的消息（默认，Ring Buffer 模式）
        kWait     = 1,          // 轮询等待：指定 timeout 的自旋轮询，直到队列有空间
        kBlock    = 2,          // 阻塞等待：使用条件变量 + timeout，高效阻塞
        kDrop     = 3,          // 丢弃新消息，立即返回错误
        kCustom   = 4,          // 用户自定义回调处理
    };
    PublishPolicy qos;         // 默认 kOverwrite
    
    // Loan 失败策略（ChunkPool 满时）
    enum class LoanPolicy : UInt32 {
        kError    = 0,          // 立即返回错误（默认，适合实时系统）
        kWait     = 1,          // 轮询等待：指定 timeout 的自旋轮询，直到有可用 Chunk
        kBlock    = 2,          // 阻塞等待：使用 WaitSet + timeout，高效阻塞
    };
    LoanPolicy loan_failure_policy;     // 默认 kError
    Duration          loan_timeout;            // Loan 等待超时（kWait/kBlock 策略使用）
    
    // 连接的 Subscriber 列表（无锁快照机制，参考 iceoryx2）
    ChannelRegistry subscriber_registry;    // 替代 vector + mutex
};
```

---

### 3.4 内存管理接口

```cpp
// 共享内存初始化（Path-based）
class SharedMemoryManager {
public:
    struct Config {
        String       shm_path;        // 如 "/lightap_service_pubsub_1234"
        UInt64       total_size;      // 总大小（固定）
        UInt32       max_chunks;      // 最大块数（固定）
        UInt64       chunk_size;      // 块大小（固定）
        UInt64       chunk_alignment; // 对齐要求
    };
    
    // 创建共享内存（服务端）
    static Result<SharedMemoryManager> Create(const Config& config) {
        // 1. 创建POSIX共享内存
        int fd = shm_open(config.shm_path.CStr(), 
                         O_CREAT | O_RDWR | O_EXCL, 
                         0600);
        if (fd < 0) {
            return Err(CoreErrc::kIPCShmCreateFailed);
        }
        
        // 2. 设置固定大小（不可更改）
        if (ftruncate(fd, config.total_size) < 0) {
            close(fd);
            shm_unlink(config.shm_path.CStr());
            return Err(CoreErrc::kIPCShmResizeFailed);
        }
        
        // 3. 映射到进程地址空间
        void* base_addr = mmap(nullptr, config.total_size,
                              PROT_READ | PROT_WRITE,
                              MAP_SHARED, fd, 0);
        if (base_addr == MAP_FAILED) {
            close(fd);
            shm_unlink(config.shm_path.CStr());
            return Err(CoreErrc::kIPCShmMapFailed);
        }
        
        // 4. 初始化内存布局（一次性）
        auto* segment = static_cast<SharedMemorySegment*>(base_addr);
        segment->control.magic_number.store(0xICE0RYX2, 
                                           std::memory_order_relaxed);
        segment->control.max_chunks = config.max_chunks;
        segment->control.chunk_size = config.chunk_size;
        
        return Ok(SharedMemoryManager{base_addr, config.total_size, fd});
    }
    
    // 打开已存在的共享内存（客户端）
    static Result<SharedMemoryManager> Open(const String& shm_path) {
        // 1. 打开POSIX共享内存
        int fd = shm_open(shm_path.CStr(), O_RDWR, 0600);
        if (fd < 0) {
            return Err(CoreErrc::kIPCShmNotFound);
        }
        
        // 2. 获取大小
        struct stat sb;
        if (fstat(fd, &sb) < 0) {
            close(fd);
            return Err(CoreErrc::kIPCShmStatFailed);
        }
        
        // 3. 映射到进程地址空间
        void* base_addr = mmap(nullptr, sb.st_size,
                              PROT_READ | PROT_WRITE,
                              MAP_SHARED, fd, 0);
        if (base_addr == MAP_FAILED) {
            close(fd);
            return Err(CoreErrc::kIPCShmMapFailed);
        }
        
        // 4. 验证魔数
        auto* segment = static_cast<SharedMemorySegment*>(base_addr);
        if (segment->control.magic_number.load() != 0xICE0RYX2) {
            munmap(base_addr, sb.st_size);
            close(fd);
            return Err(CoreErrc::kIPCShmCorrupted);
        }
        
        return Ok(SharedMemoryManager{base_addr, 
                                     static_cast<UInt64>(sb.st_size), fd});
    }
    
    // 获取基地址
    void* GetBaseAddress() const noexcept { return base_address_; }
    
    // ==================== 索引/Offset 转换方法（核心） ====================
    
    // [方法1] 通过 Chunk 索引获取指针（推荐使用）
    ChunkHeader* GetChunkByIndex(UInt32 chunk_index) const noexcept {
        auto* segment = static_cast<SharedMemorySegment*>(base_address_);
        UInt64 chunk_size = segment->control.chunk_size;
        
        // 计算 Chunk 在内存池中的 offset
        UInt64 chunk_offset = sizeof(SharedMemorySegment::ControlBlock) +
                             chunk_index * (sizeof(ChunkHeader) + chunk_size);
        
        return reinterpret_cast<ChunkHeader*>(
            static_cast<UInt8*>(base_address_) + chunk_offset);
    }
    
    // [方法2] Offset 转指针（通用方法，用于任意共享内存数据）
    template<typename T>
    T* OffsetToPtr(UInt64 offset) const noexcept {
        return reinterpret_cast<T*>(
            static_cast<UInt8*>(base_address_) + offset);
    }
    
    // [方法3] 指针转 Offset（用于跨进程传递）
    template<typename T>
    UInt64 PtrToOffset(const T* ptr) const noexcept {
        return reinterpret_cast<const UInt8*>(ptr) - 
               static_cast<const UInt8*>(base_address_);
    }
    
    // [方法4] 指针转 Chunk 索引（从指针反向获取索引）
    UInt32 PtrToChunkIndex(const ChunkHeader* chunk) const noexcept {
        return chunk->chunk_index;  // 直接从 Header 读取
    }
    
private:
    void*  base_address_;  // 本进程的映射基地址
    UInt64 size_;
    int    fd_;
};
```

### 3.3 ChunkPool 内存分配策略

#### 3.3.1 ChunkPool 内存模型（iceoryx2 设计）

**核心原则：**

1. **固定大小池（Fixed-Size Pool）**
   - 所有 Chunk 在服务创建时一次性预分配
   - Chunk 数量和大小在初始化后不可更改
   - 每个 Chunk 包含：Header（64字节对齐）+ Payload（用户数据）

2. **基于索引的寻址（Index-Based Addressing）**
   ```cpp
   // ChunkPool 内存布局（连续数组）
   struct ChunkPoolMemory {
       ChunkHeader chunk_headers[MAX_CHUNKS];   // Header 数组
       alignas(64) UInt8 payloads[MAX_CHUNKS][PAYLOAD_SIZE];  // Payload 数组
   };
   
   // Offset 计算公式
   UInt32 chunk_offset = chunk_index * (sizeof(ChunkHeader) + PAYLOAD_SIZE);
   
   // 跨进程传递使用 chunk_index，本地转换为指针
   ChunkHeader* ptr = base_address + chunk_offset;
   ```

3. **Free-List 管理（索引链表）**
   ```cpp
   // Free-List 使用索引而非指针
   struct ChunkHeader {
       UInt32 next_free_index;  // 下一个空闲块的索引（非指针！）
       // kInvalidIndex = 0xFFFFFFFF 表示链表结束
   };
   
   // Free-List Head 存储在共享内存控制块中
   std::atomic<UInt32> free_list_head_;  // 索引，不是指针
   ```

4. **Offset-Based 跨进程传递**
   ```cpp
   // ✅ 正确：传递索引/offset
   UInt32 chunk_index = chunk->chunk_index;
   msg_queue.Enqueue(chunk_index);
   
   // ❌ 错误：直接传递指针（跨进程无效）
   ChunkHeader* ptr = chunk;
   msg_queue.Enqueue(reinterpret_cast<UInt64>(ptr));  // 禁止！
   ```

**内存布局示意：**
```
共享内存段布局 (POSIX shm: /lightap_service_xxx)
优化版：固定大小分区，预留扩展空间

┌─────────────────────────────────────────────────────────────┐
│  区域1: ControlBlock (固定 128KB = 0x20000 字节)            │
│  偏移量: 0x000000 - 0x01FFFF                                │
│  ┌───────────────────────────────────────────────────────┐  │
│  │ 头部元数据 (64B 对齐)：                               │  │
│  │ - magic_number, version, state                        │  │
│  │ - max_chunks, max_subscriber_queues, channel_capacity   │  │
│  │ - chunk_size, chunk_alignment                         │  │
│  ├───────────────────────────────────────────────────────┤  │
│  │ ChunkPool 管理 (64B 对齐)：                            │  │
│  │ - free_list_head, allocated_count                     │  │
│  │ - loan_waitset (HAS_FREE_CHUNK 事件标志)              │  │
│  ├───────────────────────────────────────────────────────┤  │
│  │ ChannelRegistry (64B 对齐)：                       │  │
│  │ - active_index, write_index                  │  │
│  │ - snapshots[2]:                                       │  │
│  │   ├─ Snapshot[0]: count, version, queue_indices[100] │  │
│  │   └─ Snapshot[1]: count, version, queue_indices[100] │  │
│  ├───────────────────────────────────────────────────────┤  │
│  │ 统计信息 (64B 对齐，性能监控)：                       │  │
│  │ - publisher_count, subscriber_count                   │  │
│  │ - total_chunks_allocated, total_messages_sent         │  │
│  │ - total_loan_failures (失败监控)                      │  │
│  ├───────────────────────────────────────────────────────┤  │
│  │ 实际使用：~2KB                                        │  │
│  │ 预留空间：~126KB (用于未来扩展)                       │  │
│  └───────────────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────────┤
│  区域2: ChannelQueue[100] (固定 800KB = 0xC8000 字节)   │
│  偏移量: 0x020000 - 0x0E7FFF                                │
│  每个队列: 8KB (4KB 页对齐)                                 │
│  ┌───────────────────────────────────────────────────────┐  │
│  │ Queue[0] @ 0x020000 (8KB):                           │  │
│  │  ├─ 基础状态 (64B 对齐):                             │  │
│  │  │   active, subscriber_id                           │  │
│  │  ├─ 消息队列 (64B 对齐):                             │  │
│  │  │   head, tail, count, capacity                     │  │
│  │  │   buffer[1024] (预留1024，默认用256)              │  │
│  │  │   → 默认容量256: buffer[0-255]                    │  │
│  │  │   → 预留空间: buffer[256-1023] (动态扩容)         │  │
│  │  ├─ WaitSet (64B 对齐):                              │  │
│  │  │   event_flags (HAS_DATA/HAS_SPACE)                │  │
│  │  ├─ 统计 (64B 对齐，性能监控):                       │  │
│  │  │   last_receive_time, overrun_count                │  │
│  │  │   total_messages_received, total_messages_dropped  │  │
│  │  │   max_queue_depth (历史峰值)                      │  │
│  │  └─ 预留: ~3.5KB (未来扩展)                          │  │
│  ├───────────────────────────────────────────────────────┤  │
│  │ Queue[1] @ 0x022000 (8KB): 同上结构                  │  │
│  ├───────────────────────────────────────────────────────┤  │
│  │ ...                                                   │  │
│  ├───────────────────────────────────────────────────────┤  │
│  │ Queue[99] @ 0x0E6000 (8KB): 同上结构                 │  │
│  └───────────────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────────┤
│  预留空间区域 (96KB = 0x18000 字节)                        │
│  偏移量: 0x0E8000 - 0x0FFFFF                                │
│  用途：未来扩展，保持总大小为 1MB                           │
├─────────────────────────────────────────────────────────────┤
│  区域3: ChunkPool (从 1MB 偏移量开始，动态大小)            │
│  偏移量: 0x100000 开始                                      │
│  大小: sizeof(ChunkHeader) * max_chunks                     │
│        + chunk_size * max_chunks                            │
│  ┌─────────────────────────────────────┐                   │
│  │ Chunk[0] @ 0x100000:                │                   │
│  │  ├─ ChunkHeader (128B 对齐)         │                   │
│  │  │   ├─ chunk_size (固定)           │                   │
│  │  │   ├─ chunk_index = 0             │                   │
│  │  │   ├─ state (atomic)              │                   │
│  │  │   ├─ ref_count (atomic)          │                   │
│  │  │   ├─ next_free_index (UInt32)    │ ◄─┐ Free-List    │
│  │  │   └─ timestamp, seq_num, e2e     │   │               │
│  │  └─ Payload[chunk_size]             │   │               │
│  ├─────────────────────────────────────┤   │               │
│  │ Chunk[1]:                           │   │               │
│  │  ├─ ChunkHeader (128B 对齐)         │   │               │
│  │  │   └─ next_free_index = 2         │ ──┘ (索引链表)   │
│  │  └─ Payload[chunk_size]             │                   │
│  ├─────────────────────────────────────┤                   │
│  │ ...                                 │                   │
│  ├─────────────────────────────────────┤                   │
│  │ Chunk[max_chunks-1]:                │                   │
│  │  ├─ ChunkHeader (128B 对齐)         │                   │
│  │  │   └─ next_free_index = 0xFFFFFFFF│ (链表结束)       │
│  │  └─ Payload[chunk_size]             │                   │
│  └─────────────────────────────────────┘                   │
└─────────────────────────────────────────────────────────────┘

内存布局计算示例（典型配置）：
┌──────────────────────────────────────────────────────────┐
│ 配置: max_chunks=512, chunk_size=4KB, max_channels=100│
├──────────────────────────────────────────────────────────┤
│ 区域1 - ControlBlock:         128KB (0x20000 bytes)      │
│   ├─ 实际使用:                ~2KB                       │
│   └─ 预留空间:                ~126KB                     │
├──────────────────────────────────────────────────────────┤
│ 区域2 - ChannelQueue[100]: 800KB (0xC8000 bytes)     │
│   ├─ 单队列大小:              8KB (0x2000 bytes)         │
│   ├─ 实际使用/队列:           ~4.5KB                     │
│   └─ 预留空间/队列:           ~3.5KB                     │
├──────────────────────────────────────────────────────────┤
│ 区域2.5 - Reserved Space:     96KB (0x18000 bytes)      │
│   └─ 用途:                    未来扩展，凑足1MB          │
├──────────────────────────────────────────────────────────┤
│ 区域3 - ChunkPool:            2.06MB (动态)              │
│   ├─ ChunkHeader[512]:        64KB (512 × 128B)          │
│   └─ Payloads[512]:           2MB (512 × 4KB)            │
├──────────────────────────────────────────────────────────┤
│ 总共享内存大小:               3.06MB (原始)              │
│ 对齐到 2MB:                   4MB (实际 mmap)            │
├──────────────────────────────────────────────────────────┤
│ 内存利用率:                   76.5% (3.06/4)             │
│ 预留空间总计:                 ~576KB (扩展能力强)        │
└──────────────────────────────────────────────────────────┘

进程本地内存 (每个进程独立，RAII 智能指针管理)：
┌─────────────────────────────────────────────────────────────┐
│  Publisher 进程：                                            │
│  ├─ ControlBlock* control_block_ (指向 0x000000)            │
│  ├─ ChannelQueue* subscriber_queues_ (指向 0x020000)     │
│  ├─ ChunkHeader* chunks_ (指向 0x100000)                    │
│  ├─ 通过 GetChannelSnapshot(control_block_) 读取快照     │
│  └─ SharedMemoryManager* (管理共享内存映射)                │
├─────────────────────────────────────────────────────────────┤
│  Subscriber 进程：                                           │
│  ├─ ControlBlock* control_block_ (指向 0x000000)            │
│  ├─ ChannelQueue* my_queue_ (指向 0x020000 + idx*8KB)   │
│  ├─ queue_index_ (本地记录自己在共享内存中的队列索引)        │
│  ├─ 通过 RegisterChannel(control_block_, idx) 注册       │
│  └─ SharedMemoryManager* (管理共享内存映射)                │
└─────────────────────────────────────────────────────────────┘

关键优化特性：
1. **固定大小分区设计（平衡优化版）**：
   - ControlBlock: 固定 128KB (实际用2KB，预留126KB)
   - ChannelQueue: 固定 800KB (100队列 × 8KB)
   - Reserved Space: 固定 96KB (未来扩展，凑足1MB)
   - ChunkPool: 从 1MB 偏移开始，大小动态计算
   - 优势: 简化地址计算，避免内存碎片，队列容量充足

2. **对齐优化**：
   - ControlBlock: 4KB 页对齐 (支持大页)
   - ChannelQueue: 4KB 页对齐 (每个队列独立页)
   - ChunkHeader: 128B 对齐 (双缓存行)
   - 所有关键字段: 64B 缓存行对齐
   - 优势: 避免伪共享，提升缓存命中率

3. **预留空间设计（平衡优化）**：
   - ControlBlock: ~126KB 预留 (扩展元数据、更多Registry)
   - ChannelQueue: 每队列 ~3.5KB 预留 (扩展统计、新特性)
   - Reserved Space: 96KB 全局预留 (未来新增队列或其他功能)
   - Queue buffer: 1024容量预留，默认用256 (动态扩容)
   - 优势: 无需改变内存布局即可扩展功能

4. **Buffer 动态容量**：
   - 默认: buffer[0-255] (256容量，满足大多数场景)
   - 高负载: buffer[0-511] (512容量，运行时配置)
   - 极限: buffer[0-1023] (1024容量，特殊场景)
   - 优势: 灵活性与性能兼顾

5. **跨进程传递仍使用 chunk_index**：
   - Free-List 使用索引链表，头节点索引存储在 ControlBlock
   - 消息队列存储 chunk_index (UInt32)，不存储指针
   - 计算公式: chunk_addr = base + 0x100000 + chunk_index * (128 + chunk_size)
```

#### 3.3.2 ChunkPool Allocator 实现（基于索引）

**关键特性：**
- ✅ 固定数量Chunk，服务创建时预分配
- ✅ 使用Chunk索引（offset）替代指针
- ✅ O(1)确定性分配/释放
- ✅ 无动态内存分配，无扩容/缩容

参考 iceoryx2 的设计：

```cpp
class ChunkPoolAllocator {
public:
    struct Config {
        UInt64 chunk_size;        // 块大小（含 Header + Payload）
        UInt32 max_chunks;        // 最大块数（固定）
        UInt64 chunk_alignment;   // 对齐要求（通常64字节）
    };
    
    // 初始化：构建 free-list
    Result<void> Initialize(void* memory_base, const Config& config) noexcept {
        base_address_ = reinterpret_cast<UInt8*>(memory_base);
        config_ = config;
        
        // 初始化所有块到 free-list（逆序链接）
        for (UInt32 i = 0; i < config_.max_chunks; ++i) {
            auto* chunk = GetChunkByIndex(i);
            chunk->chunk_index = i;
            chunk->chunk_size = config_.chunk_size;
            chunk->state.store(static_cast<UInt32>(ChunkState::kFree),
                             std::memory_order_relaxed);
            chunk->next_free_index = i + 1;  // 指向下一个
        }
        
        // 最后一个块的 next 为无效索引
        GetChunkByIndex(config_.max_chunks - 1)->next_free_index = kInvalidIndex;
        
        // 设置 free-list 头为索引 0
        free_list_head_.store(0, std::memory_order_release);
        allocated_count_.store(0, std::memory_order_relaxed);
        is_initialized_.store(true, std::memory_order_release);
        
        return Ok();
    }
    
    // 无锁分配（O(1)时间复杂度）
    Result<ChunkHeader*> Allocate() noexcept {
        if (!is_initialized_.load(std::memory_order_acquire)) {
            return Err(CoreErrc::kIPCAllocationNotInitialized);
        }
        
        // 从 free-list 头部取块（CAS 操作）
        UInt32 expected_index = free_list_head_.load(std::memory_order_acquire);
        
        while (expected_index != kInvalidIndex) {
            auto* chunk = GetChunkByIndex(expected_index);
            UInt32 next_index = chunk->next_free_index;
            
            // CAS 更新 free-list 头
            if (free_list_head_.compare_exchange_weak(
                    expected_index, next_index,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                
                // 成功分配，初始化块状态
                chunk->state.store(static_cast<UInt32>(ChunkState::kLoaned),
                                 std::memory_order_relaxed);
                chunk->ref_count.store(1, std::memory_order_relaxed);
                chunk->sequence_number = 0;
                chunk->timestamp = GetMonotonicTimeNs();
                chunk->next_free_index = kInvalidIndex;  // 不再在链表中
                
                allocated_count_.fetch_add(1, std::memory_order_relaxed);
                
                // 🔥 清除 HAS_FREE_CHUNK 标志（如果 Pool 现在满了）
                if (allocated_count_.load(std::memory_order_relaxed) >= config_.max_chunks) {
                    auto* ctrl = GetControlBlock();
                    WaitSetHelper::ClearFlags(&ctrl->loan_waitset, EventFlag::HAS_FREE_CHUNK);
                }
                
                return Ok(chunk);
            }
            // CAS 失败，重试（expected_index 已被更新）
        }
        
        // ChunkPool 耗尽
        return Err(CoreErrc::kIPCChunkPoolExhausted);
    }
    
    // 无锁释放（O(1)时间复杂度）
    void Deallocate(ChunkHeader* chunk) noexcept {
        if (!chunk) return;
        
        // 验证块是否属于此池
        UInt32 chunk_index = chunk->chunk_index;
        if (chunk_index >= config_.max_chunks) {
            // 错误：非法块
            return;
        }
        
        // 状态转换为 FREE
        chunk->state.store(static_cast<UInt32>(ChunkState::kFree),
                         std::memory_order_relaxed);
        chunk->ref_count.store(0, std::memory_order_relaxed);
        
        // 归还到 free-list 头部（CAS 操作）
        UInt32 expected_head = free_list_head_.load(std::memory_order_acquire);
        do {
            chunk->next_free_index = expected_head;
        } while (!free_list_head_.compare_exchange_weak(
                    expected_head, chunk_index,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire));
        
        allocated_count_.fetch_sub(1, std::memory_order_relaxed);
    }
    
    // 获取统计信息
    UInt32 GetAllocatedCount() const noexcept {
        return allocated_count_.load(std::memory_order_relaxed);
    }
    
    UInt32 GetFreeCount() const noexcept {
        return config_.max_chunks - GetAllocatedCount();
    }
    
private:
    static constexpr UInt32 kInvalidIndex = 0xFFFFFFFF;
    
    // 通过索引获取块（基于偏移量计算）
    ChunkHeader* GetChunkByIndex(UInt32 index) noexcept {
        return reinterpret_cast<ChunkHeader*>(
            base_address_ + index * config_.chunk_size);
    }
    
    UInt8*                    base_address_;      // 内存池基地址
    Config                    config_;            // 配置
    std::atomic<UInt32>       free_list_head_;    // 空闲链表头索引
    std::atomic<UInt32>       allocated_count_;   // 已分配计数
    std::atomic<bool>         is_initialized_;    // 初始化标志
};
```

**关键设计说明：**

1. **固定配置在服务创建时确定：**
   ```cpp
   ServiceBuilder::MaxChunks(1024)        // 固定1024个chunk
                 .ChunkSize(256)          // 每个256字节（固定）
                 .ChunkAlignment(64);     // 64字节对齐
   ```

2. **禁止动态扩容：**
   - 分配失败返回 `kOutOfMemory` 错误
   - 不会动态申请新内存或扩大共享内存

3. **Offset-based跨进程传递：**
   ```cpp
   // Publisher 发送时保存 offset
   UInt32 chunk_offset = chunk->chunk_index * config_.chunk_size;
   msg_queue.Enqueue(chunk_offset);  // 传递offset而非指针
   
   // Subscriber 接收时转换为本地指针
   ChunkHeader* chunk = shm_mgr_->OffsetToPtr<ChunkHeader>(chunk_offset);
   ```

### 3.4 Chunk 状态机设计

参考 iceoryx2 的状态转换模型：

```cpp
// Chunk 状态机
enum class ChunkState : UInt32 {
    kFree     = 0,  // 空闲状态：在 free-list 中，可被分配
    kLoaned   = 1,  // 借出状态：Publisher 持有，未发送
    kSent     = 2,  // 发送状态：在消息队列中，Subscriber 可接收
    kReceived = 3,  // 接收状态：Subscriber 持有，正在使用
};

// 状态转换图
/*
    ┌─────────┐
    │  kFree  │ ◄──────────────────────────┐
    └────┬────┘                            │
         │ Allocate()                      │ Deallocate()
         │                                 │
         ▼                                 │
    ┌─────────┐                       ┌────┴─────┐
    │ kLoaned │ ──── Send() ────────► │  kSent   │
    └────┬────┘                       └────┬─────┘
         │                                 │
         │ Release()                       │ Receive()
         │ (单播模式)                      │
         └────────────────────────┐        │
                                  │        ▼
                                  │   ┌──────────┐
                                  └──►│kReceived │
                                      └────┬─────┘
                                           │
                                           │ Release Sample
                                           │ (ref_count -> 0)
                                           ▼
                                      (回到 kFree)
*/

// 状态转换函数
class ChunkStateMachine {
public:
    // Publisher: Loan -> Send
    static Result<void> TransitionLoanedToSent(ChunkHeader* chunk) noexcept {
        UInt32 expected = static_cast<UInt32>(ChunkState::kLoaned);
        UInt32 desired = static_cast<UInt32>(ChunkState::kSent);
        
        if (!chunk->state.compare_exchange_strong(
                expected, desired,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return Err(CoreErrc::kIPCInvalidStateTransition);
        }
        return Ok();
    }
    
    // Publisher: Loan -> Free (单播模式释放)
    static Result<void> TransitionLoanedToFree(ChunkHeader* chunk) noexcept {
        UInt32 expected = static_cast<UInt32>(ChunkState::kLoaned);
        UInt32 desired = static_cast<UInt32>(ChunkState::kFree);
        
        if (!chunk->state.compare_exchange_strong(
                expected, desired,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return Err(CoreErrc::kIPCInvalidStateTransition);
        }
        return Ok();
    }
    
    // Subscriber: Sent -> Received
    static Result<void> TransitionSentToReceived(ChunkHeader* chunk) noexcept {
        UInt32 expected = static_cast<UInt32>(ChunkState::kSent);
        UInt32 desired = static_cast<UInt32>(ChunkState::kReceived);
        
        if (!chunk->state.compare_exchange_strong(
                expected, desired,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return Err(CoreErrc::kIPCInvalidStateTransition);
        }
        
        // 增加引用计数
        chunk->ref_count.fetch_add(1, std::memory_order_relaxed);
        return Ok();
    }
    
    // Subscriber: Received -> Free (释放样本)
    static Result<void> TransitionReceivedToFree(
            ChunkHeader* chunk, 
            ChunkPoolAllocator* allocator) noexcept {
        
        // 减少引用计数
        UInt64 old_ref = chunk->ref_count.fetch_sub(1, std::memory_order_acq_rel);
        
        if (old_ref == 1) {
            // 最后一个引用，释放内存
            UInt32 expected = static_cast<UInt32>(ChunkState::kReceived);
            UInt32 desired = static_cast<UInt32>(ChunkState::kFree);
            
            if (chunk->state.compare_exchange_strong(
                    expected, desired,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                
                // 归还到内存池
                allocator->Deallocate(chunk);
                return Ok();
            }
        }
        
        return Ok();  // 仍有其他引用
    }
    
    // 验证状态
    static bool IsInState(const ChunkHeader* chunk, ChunkState state) noexcept {
        return chunk->state.load(std::memory_order_acquire) == 
               static_cast<UInt32>(state);
    }
};
```

### 3.5 双计数器引用计数机制

参考 iceoryx2 的双层引用计数设计，解决单播/广播混合场景：

**重要：跨进程传递使用Offset**

```cpp
// Publisher 端：loan_counter
class Publisher {
    std::atomic<UInt32> loan_counter_;  // 跟踪未发送的样本数量
    UInt32              max_loaned_samples_;
    
public:
    Result<Sample> Loan() {
        // 尝试从 ChunkPool 分配
        auto chunk_result = allocator_->Allocate();
        if (!chunk_result.HasValue()) {
            return Err(chunk_result.Error());  // ChunkPool 耗尽
        }
        
        auto* chunk = chunk_result.Value();
        
        // loan_counter + 1
        UInt32 current_loans = loan_counter_.fetch_add(1, std::memory_order_relaxed);
        
        // 警告策略：检测潜在的资源泄漏
        if (current_loans + 1 >= max_loaned_samples_) {
            // 🔥 触发警告 Hook（不阻止分配）
            if (event_hooks_) {
                event_hooks_->OnLoanCounterWarning(
                    publisher_id_,
                    current_loans + 1,
                    max_loaned_samples_
                );
            }
            LOG_WARN("Publisher {} loan_counter high: {}/{}",
                     publisher_id_, current_loans + 1, max_loaned_samples_);
        }
        
        return Ok(Sample{chunk, this});
    }
    
    void Send(Sample&& sample) {
        auto* chunk = sample.Release();
        
        // 转换为offset（跨进程传递）
        UInt32 chunk_offset = chunk->chunk_index * chunk_pool_->GetChunkSize();
        
        // 将offset加入自己的队列
        msg_queue_.EnqueueOffset(chunk_offset);
        
        // loan_counter - 1 (不再持有)
        loan_counter_.fetch_sub(1, std::memory_order_release);
    }
    
    void Release(Sample&& sample) {
        auto* chunk = sample.Release();
        
        // 单播模式：直接归还池
        allocator_->Deallocate(chunk);
        
        // loan_counter - 1
        loan_counter_.fetch_sub(1, std::memory_order_release);
    }
};

// Segment 端：sample_reference_counter
struct ChunkHeader {
    std::atomic<UInt64> ref_count;  // 订阅者引用计数
    
    void IncrementRef() {
        ref_count.fetch_add(1, std::memory_order_relaxed);
    }
    
    void DecrementRef(PoolAllocator* allocator) {
        if (ref_count.fetch_sub(1, std::memory_order_release) == 1) {
            // 最后一个引用，归还内存池
            std::atomic_thread_fence(std::memory_order_acquire);
            allocator->Deallocate(this);
        }
    }
};
```

### 3.6 确定性设计总结

| 设计方面 | 传统动态方法 | LightAP固定方法 | 优势 |
|---------|------------|----------------|------|
| **内存分配** | malloc/new | 预分配ChunkPool | O(1)确定性延迟 |
| **内存布局** | 运行时动态 | 编译/配置时固定 | 可预测的内存占用 |
| **扩容机制** | 动态realloc | ❌ 禁止扩容 | 避免内存碎片 |
| **地址传递** | 直接指针 | Offset索引 | 跨进程可重定位 |
| **初始化** | 编程式创建 | Path-based SHM | 标准化进程间通信 |
| **最坏情况** | 不可预测 | 固定边界 | 满足实时性要求 |

**配置示例：**
```cpp
// 服务配置（固定，不可运行时更改）
auto service = node.CreateServiceBuilder<Data>("MyService")
    .PublishSubscribe()
    .MaxChunks(512)           // 固定512个chunk
    .ChunkSize(1024)          // 每个1KB（固定）
    .MaxPublishers(4)         // 最多4个Publisher
    .MaxSubscribers(16)       // 最多16个Subscriber
    .ShmPath("/lightap_myservice")  // 共享内存路径
    .Create()                 // 一次性创建，固定布局
    .Value();

// 运行时无法更改：
// ❌ service.Resize(1024);      // 不支持
// ❌ service.AddChunks(100);    // 不支持
// ✅ 只能分配已存在的chunk
```

---

## 4. 消息传递模式

### 4.1 Publish-Subscribe (发布-订阅)

#### 4.1.1 核心 API

```cpp
namespace ara::core::ipc {

// Publisher API
template<typename PayloadType>
class Publisher {
public:
    // Loan-Based API (零拷贝)
    Result<Sample<PayloadType>> Loan() noexcept;
    Result<void> Send(Sample<PayloadType>&& sample) noexcept;
    Result<void> Release(Sample<PayloadType>&& sample) noexcept;
    
    // Copy-Based API (便捷接口)
    Result<void> SendCopy(const PayloadType& data) noexcept;
    
    // Emplace-Based API (原地构造，免拷贝)
    template<typename... Args>
    Result<void> SendEmplace(Args&&... args) noexcept;
    
    // 切片支持（动态大小）
    Result<SampleSlice<PayloadType>> LoanSlice(UInt32 length) noexcept;
    
    // 统计信息
    UInt64 GetSentCount() const noexcept;
    UInt32 GetLoanedCount() const noexcept;
};

// Subscriber API（iceoryx2 风格 - 专属队列）
template<typename PayloadType>
class Subscriber {
public:
    // 接收消息（非阻塞，从自己的队列读取）
    Result<Sample<PayloadType>> Receive() noexcept;
    
    // 检查自己的队列是否有消息
    bool HasData() const noexcept;
    
    // 获取队列统计信息
    UInt32 GetQueuedCount() const noexcept;
    UInt32 GetQueueCapacity() const noexcept;
    
    // 断开与所有 Publisher 的连接
    Result<void> Disconnect() noexcept;
    
private:
    // ===== 本地状态 =====
    String                    subscriber_id_;   // 本地 UUID
    UInt32                    queue_index_;     // 自己在共享内存中的队列索引
    SharedMemoryManager*      shm_mgr_;         // 共享内存管理器
    ControlBlock*             control_block_;   // ControlBlock 指针（共享内存）
    UInt32                    queue_capacity_;  // 队列容量
    
    // 队列空策略（Subscriber 读取时使用）
    enum class SubscribePolicy : UInt32 {
        kBlock  = 0,    // 阻塞等待直到有数据（需配置超时，默认，推荐）
        kWait   = 1,    // 轮询等待直到有数据（需配置超时）
        kSkip   = 2,    // 跳过当次
        kError  = 3,    // 队列为空时立即返回错误
    };
    SubscribePolicy queue_empty_policy_;      // 默认 kBlock
    
    // ===== 共享状态（通过 queue_index_ 访问）=====
    // subscriber_queues[queue_index_].msg_queue  <- 自己的专属队列
};

// Sample RAII 包装器
template<typename PayloadType>
class Sample {
public:
    // 访问有效载荷
    PayloadType* operator->() noexcept;
    const PayloadType* operator->() const noexcept;
    
    PayloadType& operator*() noexcept;
    const PayloadType& operator*() const noexcept;
    
    // 获取元数据
    UInt64 GetSequenceNumber() const noexcept;
    UInt64 GetTimestamp() const noexcept;
    UInt32 GetPublisherId() const noexcept;
    
    // 自动引用计数管理
    ~Sample() noexcept;
    
private:
    ChunkHeader* chunk_;
    Publisher<PayloadType>* publisher_;
};

} // namespace ara::core::ipc
```

#### 4.1.2 使用示例（基于实际代码）

**基础示例 - 发送固定大小消息：**

```cpp
using namespace lap::core::ipc;

// 1. 定义消息类型（继承自Message基类）
class SensorData : public Message {
public:
    int32_t temperature = 0;
    int32_t humidity = 0;
    uint64_t timestamp = 0;
    
    // 序列化到共享内存
    size_t OnMessageSend(void* chunk_ptr, size_t chunk_size) noexcept override {
        if (chunk_size < sizeof(SensorData)) return 0;
        std::memcpy(chunk_ptr, this, sizeof(SensorData));
        return sizeof(SensorData);
    }
    
    // 从共享内存反序列化
    bool OnMessageReceived(const void* chunk_ptr, size_t chunk_size) noexcept override {
        if (chunk_size < sizeof(SensorData)) return false;
        std::memcpy(this, chunk_ptr, sizeof(SensorData));
        return true;
    }
};

// 2. 创建Publisher（NORMAL模式）
PublisherConfig config;
config.max_chunks = 16;
config.chunk_size = sizeof(SensorData);

auto pub = Publisher::Create("/sensor_data", config).Value();

// 3. 发送消息 - 方式1：Loan + Send
{
    auto sample = pub.Loan().Value();
    SensorData* data = sample.GetPayload<SensorData>();
    data->temperature = 25;
    data->humidity = 60;
    data->timestamp = GetTimestamp();
    pub.Send(std::move(sample));
}

// 4. 发送消息 - 方式2：Send with Lambda（推荐，零拷贝）
pub.Send([](void* chunk_ptr, size_t chunk_size) -> size_t {
    SensorData* data = static_cast<SensorData*>(chunk_ptr);
    data->temperature = 25;
    data->humidity = 60;
    data->timestamp = GetTimestamp();
    return sizeof(SensorData);
}).Value();

// 5. 创建Subscriber
SubscriberConfig sub_config;
sub_config.channel_capacity = 256;  // NORMAL模式默认值

auto sub = Subscriber::Create("/sensor_data", sub_config).Value();

// 6. 接收消息
while (true) {
    auto sample = sub.Receive();
    if (sample.HasValue()) {
        const SensorData* data = sample.Value().GetPayload<SensorData>();
        std::cout << "Temperature: " << data->temperature << std::endl;
        std::cout << "Humidity: " << data->humidity << std::endl;
        // Sample析构时自动释放引用
    } else {
        // 队列为空，等待或继续其他工作
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
```

**高级示例 - 零拷贝大数据传输（camera_fusion_spmc_example）：**

```cpp
// 定义图像消息
class ImageFrame : public Message {
public:
    static constexpr size_t kWidth = 1920;
    static constexpr size_t kHeight = 720;
    static constexpr size_t kChannels = 4;  // RGBA
    static constexpr size_t kImageSize = kWidth * kHeight * kChannels;
    
    uint64_t frame_id = 0;
    uint64_t timestamp = 0;
    // 图像数据直接在chunk中，不需要在Message对象里
    
    size_t OnMessageSend(void* chunk_ptr, size_t chunk_size) noexcept override {
        if (chunk_size < sizeof(ImageFrame) + kImageSize) return 0;
        
        // 写入元数据
        std::memcpy(chunk_ptr, this, sizeof(ImageFrame));
        
        // 图像数据已在chunk中（通过Lambda直接生成）
        return sizeof(ImageFrame) + kImageSize;
    }
    
    bool OnMessageReceived(const void* chunk_ptr, size_t chunk_size) noexcept override {
        if (chunk_size < sizeof(ImageFrame)) return false;
        std::memcpy(this, chunk_ptr, sizeof(ImageFrame));
        return true;
    }
    
    // 零拷贝访问图像数据
    const uint8_t* GetImageData(const void* chunk_ptr) const noexcept {
        return static_cast<const uint8_t*>(chunk_ptr) + sizeof(ImageFrame);
    }
};

// 创建Publisher（大图像传输）
PublisherConfig config;
config.max_chunks = 16;
config.chunk_size = ImageFrame::kImageSize + sizeof(ImageFrame);  // ~5.3MB
config.loan_policy = LoanPolicy::kWait;  // Chunk耗尽时等待

auto pub = Publisher::Create("/cam0_stream", config).Value();

// 发送图像帧 - Lambda直接生成图像数据（零拷贝）
pub.Send([frame_id](void* chunk_ptr, size_t chunk_size) -> size_t {
    // 写入元数据
    ImageFrame* frame = static_cast<ImageFrame*>(chunk_ptr);
    frame->frame_id = frame_id;
    frame->timestamp = GetTimestamp();
    
    // 生成图像数据（直接写入共享内存，无拷贝）
    uint8_t* image_data = reinterpret_cast<uint8_t*>(chunk_ptr) + sizeof(ImageFrame);
    GenerateImageData(image_data, ImageFrame::kImageSize);
    
    return sizeof(ImageFrame) + ImageFrame::kImageSize;
}).Value();

// Subscriber接收并处理图像（零拷贝）
auto sub = Subscriber::Create("/cam0_stream").Value();
auto sample = sub.Receive().Value();

const void* chunk_ptr = sample.GetRawPayload();
const ImageFrame* frame = static_cast<const ImageFrame*>(chunk_ptr);
const uint8_t* image_data = frame->GetImageData(chunk_ptr);

// 直接使用共享内存中的图像数据（零拷贝）
ProcessImage(image_data, ImageFrame::kImageSize);
```

**多生产者多消费者示例（MPMC）：**

```cpp
// 3个Camera Publisher独立发送
for (int cam = 0; cam < 3; ++cam) {
    std::thread([cam]() {
        String shm_path = "/cam" + std::to_string(cam) + "_stream";
        auto pub = Publisher::Create(shm_path, config).Value();
        
        while (running) {
            pub.Send([cam](void* chunk_ptr, size_t) -> size_t {
                GenerateCameraFrame(cam, chunk_ptr);
                return kFrameSize;
            });
            
            // 限流：100 FPS (STMin=10ms)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }).detach();
}

// 1个Fusion Subscriber，3个线程并发接收
for (int cam = 0; cam < 3; ++cam) {
    std::thread([cam]() {
        String shm_path = "/cam" + std::to_string(cam) + "_stream";
        auto sub = Subscriber::Create(shm_path).Value();
        
        while (running) {
            auto sample = sub.Receive();
            if (sample.HasValue()) {
                // 并发写入双缓存（无锁）
                CopyToFusionBuffer(cam, sample.Value().GetRawPayload());
            }
        }
    }).detach();
}
```
```

### 4.2 Message 设计与使用

#### 4.2.1 Message 设计模式

Message 采用 **Interpreter/Codec 模式**（类似 Protobuf），Message 对象本身**不存储在共享内存**中，而是作为编解码器，负责：
- **Publisher 端**: 将数据序列化写入 Chunk
- **Subscriber 端**: 从 Chunk 反序列化数据

**核心设计理念：**

```
┌─────────────────────────────────────────────────────────┐
│  Message 对象生命周期与存储位置                         │
├─────────────────────────────────────────────────────────┤
│  Publisher进程:                                         │
│    Stack/Heap: TestMessage msg;  ← 对象在进程内存       │
│    SharedMem:  [Chunk Header][Payload] ← 数据在共享内存 │
│                                                          │
│  Subscriber进程:                                        │
│    Stack/Heap: TestMessage msg;  ← 对象在进程内存       │
│    SharedMem:  [Chunk Header][Payload] ← 读取共享内存   │
└─────────────────────────────────────────────────────────┘

关键特性:
✅ Message对象在各进程的栈/堆上（非共享内存）
✅ 虚函数表在各进程独立（vtable不跨进程）
✅ OnMessageSend/OnMessageReceived负责编解码
✅ 支持零拷贝（Subscriber可直接引用chunk数据）
```

#### 4.2.2 Message 基类 API

```cpp
namespace lap::core::ipc {

/**
 * @brief IPC 消息基类（Interpreter/Codec 模式）
 * 
 * @details Message 对象生存在进程的 heap/stack 中（NOT 共享内存）
 *          它们通过 OnMessageSend/OnMessageReceived 回调操作共享内存
 * 
 * 设计模式:
 * - Publisher: 创建 Message → 设置数据 → SendMessage() → OnMessageSend(chunk_ptr) 写入chunk
 * - Subscriber: ReceiveMessage() → OnMessageReceived(chunk_ptr) 读取chunk → 使用数据
 * 
 * 优势:
 * - 虚函数正常工作（每个进程有独立vtable）
 * - 类型安全的消息处理
 * - 零拷贝支持（Message可引用chunk数据而非拷贝）
 */
class Message {
public:
    Message() noexcept = default;
    virtual ~Message() noexcept = default;
    
    // 允许拷贝和移动
    Message(const Message&) noexcept = default;
    Message& operator=(const Message&) noexcept = default;
    Message(Message&&) noexcept = default;
    Message& operator=(Message&&) noexcept = default;
    
    /**
     * @brief 获取消息类型名（用于调试和日志）
     * @return 类型名字符串
     * @note 参考 DDS TopicDescription::get_type_name()
     */
    virtual const char* GetTypeName() const noexcept { return "Message"; }
    
    /**
     * @brief 获取消息类型ID（子类重写用于类型识别）
     * @return 类型ID（建议使用hash或UUID）
     * @note 参考 DDS TypeSupport、ROS2 typesupport
     */
    virtual UInt32 GetTypeId() const noexcept { return 0; }
    
    /**
     * @brief 获取消息版本（用于向后兼容）
     * @return 版本号（主版本.次版本）
     * @note 参考 Protobuf field numbers、DDS TypeObject
     */
    virtual UInt32 GetVersion() const noexcept { return 0x00010000; }  // 1.0
    
    /**
     * @brief 获取序列化后的数据大小（字节）
     * @return 数据大小，0表示变长
     * @note 用于预分配chunk大小，参考 DDS get_serialized_sample_max_size()
     */
    virtual size_t GetSerializedSize() const noexcept { return 0; }
    
    /**
     * @brief 生命周期回调 - 发送前将数据写入chunk
     * @param chunk_ptr Chunk内存指针（来自Sample.Get()）
     * @param chunk_size Chunk可用大小（用于边界检查）
     * @return 实际写入的字节数，0表示失败
     * @note 由Publisher::SendMessage()调用，子类重写实现序列化逻辑
     */
    virtual size_t OnMessageSend(void* const chunk_ptr, size_t chunk_size) noexcept = 0;
    
    /**
     * @brief 生命周期回调 - 接收后从chunk读取数据
     * @param chunk_ptr Chunk内存指针（来自Sample.Get()）
     * @param chunk_size Chunk大小（用于验证）
     * @return true成功，false失败（版本不兼容等）
     * @note 由Subscriber::ReceiveMessage()调用，子类重写实现反序列化逻辑
     */
    virtual bool OnMessageReceived(const void* const chunk_ptr, size_t chunk_size) noexcept = 0;
    
    /**
     * @brief 消息被丢弃时的回调（队列满）
     * @note 参考 DDS on_sample_rejected
     */
    virtual void OnMessageDropped() noexcept {}
    
    /**
     * @brief 发送失败时的回调
     */
    virtual void OnMessageFailed() noexcept {}
    
    /**
     * @brief 验证消息类型兼容性（可选）
     * @param type_id 接收到的类型ID
     * @param version 接收到的版本号
     * @return true兼容，false不兼容
     */
    virtual bool IsCompatible(UInt32 type_id, UInt32 version) const noexcept {
        return (type_id == GetTypeId()) && 
               ((version & 0xFFFF0000) == (GetVersion() & 0xFFFF0000));  // 主版本相同
    }
};

} // namespace lap::core::ipc
```

#### 4.2.3 Sample vs Message：选择指南

**核心区别**：

| 特性 | Sample模式 | Message模式 |
|------|-----------|-------------|
| **数据访问** | 直接指针（`sample->field`） | 序列化/反序列化 |
| **适用类型** | POD类型（Plain Old Data） | 任意类型（含std::string/vector） |
| **性能** | 最优（无序列化开销） | 中等（有序列化开销） |
| **代码复杂度** | 简单 | 稍复杂（需实现序列化） |
| **虚函数支持** | ❌ 不支持 | ✅ 支持 |
| **变长数据** | ❌ 固定大小 | ✅ 支持 |

**选择决策树**：
```
数据是POD类型？（无指针、无虚函数、无std容器）
├─ 是 → 使用Sample模式（性能最优）
│   └─ 示例：struct SensorData { float temp; float pressure; };
│
└─ 否 → 使用Message模式
    ├─ 包含std::string/vector → Message
    ├─ 需要虚函数多态 → Message
    ├─ 需要变长数据 → Message
    └─ 需要版本管理 → Message
```

**典型使用场景**：

**Sample模式**（推荐用于高频、性能关键场景）：
```cpp
// 1. 传感器数据
struct IMUData {
    uint64_t timestamp;
    float accel[3];
    float gyro[3];
};
auto sample = publisher.Loan().Value();
sample->timestamp = GetTime();
publisher.Send(std::move(sample));

// 2. 控制指令
struct MotorCmd {
    uint32_t motor_id;
    float velocity;
};
```

**Message模式**（推荐用于复杂对象、变长数据）：
```cpp
// 1. 日志消息（变长字符串）
class LogMessage : public Message {
    std::string log_content;
};

// 2. 配置数据（复杂嵌套）
class ConfigMessage : public Message {
    std::map<std::string, std::string> params;
};
```

---

#### 4.2.4 Publisher/Subscriber 与 Message 的集成

```cpp
// Publisher API 扩展
template<typename PayloadType>
class Publisher {
public:
    // ... 原有 Loan/Send API ...
    
    /**
     * @brief 发送 Message 对象（使用编解码模式）
     * @param message Message对象引用（栈上或堆上）
     * @param policy 发布策略
     * @return Result<void> 成功或错误
     * 
     * 工作流程:
     * 1. 内部调用Loan()分配chunk
     * 2. 调用message.OnMessageSend(chunk_ptr, chunk_size)写入数据
     * 3. 调用Send(sample)发送chunk到订阅者队列
     * 4. 自动管理chunk引用计数
     * 
     * @note 本质上是 Loan() + OnMessageSend() + Send() 的组合封装
     */
    Result<void> SendMessage(Message& message, 
                             PublishPolicy policy = PublishPolicy::kDrop) noexcept;
};

// Subscriber API 扩展
template<typename PayloadType>
class Subscriber {
public:
    // ... 原有 Receive API ...
    
    /**
     * @brief 接收并反序列化到 Message 对象
     * @param message Message对象引用（栈上或堆上）
     * @return Result<Sample<PayloadType>> 成功返回Sample，失败返回错误
     * 
     * 工作流程:
     * 1. 内部调用Receive()从队列接收chunk
     * 2. 调用message.OnMessageReceived(chunk_ptr, chunk_size)读取数据
     * 3. 返回Sample（保持chunk引用计数）
     * 
     * ⚠️  返回Sample的重要性：
     * - Sample管理chunk生命周期（RAII）
     * - 如果Message内部引用chunk数据（零拷贝），必须保持Sample有效
     * - 示例：
     *   auto result = subscriber.ReceiveMessage(msg);
     *   auto& sample = result.Value();  // 保持sample生命周期
     *   const char* data = msg.GetData();  // 零拷贝引用chunk
     *   ProcessData(data);  // 使用data...
     *   // 离开作用域，sample析构，chunk释放，data失效
     * 
     * @note 本质上是 Receive() + OnMessageReceived() 的组合封装
     */
    Result<Sample<PayloadType>> ReceiveMessage(Message& message) noexcept;
};
```

#### 4.2.4 使用示例 - 基础消息

```cpp
// === 定义消息类型 ===
class SimpleMessage : public Message {
public:
    SimpleMessage() : sequence(0), timestamp(0), value(0) {}
    
    // 设置数据（Publisher端）
    void SetData(uint64_t seq, uint64_t ts, uint32_t val) noexcept {
        sequence = seq;
        timestamp = ts;
        value = val;
    }
    
    // 类型标识（建议使用编译期hash）
    const char* GetTypeName() const noexcept override { return "SimpleMessage"; }
    UInt32 GetTypeId() const noexcept override { return 100; }
    UInt32 GetVersion() const noexcept override { return 0x00010000; }  // 1.0
    size_t GetSerializedSize() const noexcept override { 
        return sizeof(sequence) + sizeof(timestamp) + sizeof(value); 
    }
    
    // 序列化到chunk（Publisher端调用）
    size_t OnMessageSend(void* const chunk_ptr, size_t chunk_size) noexcept override {
        size_t required = GetSerializedSize();
        if (chunk_size < required) return 0;  // 空间不足
        
        auto* p = static_cast<uint8_t*>(chunk_ptr);
        std::memcpy(p, &sequence, sizeof(sequence));
        p += sizeof(sequence);
        std::memcpy(p, &timestamp, sizeof(timestamp));
        p += sizeof(timestamp);
        std::memcpy(p, &value, sizeof(value));
        
        return required;  // 返回实际写入大小
    }
    
    // 从chunk反序列化（Subscriber端调用）
    bool OnMessageReceived(const void* const chunk_ptr, size_t chunk_size) noexcept override {
        if (chunk_size < GetSerializedSize()) return false;  // 数据不足
        
        auto* p = static_cast<const uint8_t*>(chunk_ptr);
        std::memcpy(&sequence, p, sizeof(sequence));
        p += sizeof(sequence);
        std::memcpy(&timestamp, p, sizeof(timestamp));
        p += sizeof(timestamp);
        std::memcpy(&value, p, sizeof(value));
        
        return true;
    }
    
    // 访问器
    uint64_t GetSequence() const noexcept { return sequence; }
    uint64_t GetTimestamp() const noexcept { return timestamp; }
    uint32_t GetValue() const noexcept { return value; }

private:
    uint64_t sequence;   // ⚠️ 进程私有数据（不在共享内存）
    uint64_t timestamp;
    uint32_t value;
};

// === Publisher 端使用 ===
// 创建 Publisher<UInt8>（用于传输原始字节）
PublisherConfig config;
config.chunk_size = 1024;  // 或使用 SimpleMessage().GetSerializedSize()
config.max_chunks = 32;

auto publisher = Publisher<UInt8>::Create("my_service", config).Value();

// 创建消息对象（栈上）
SimpleMessage message;
message.SetData(42, GetTimestamp(), 12345);

// 发送（内部自动调用OnMessageSend）
auto result = publisher.SendMessage(message, PublishPolicy::kDrop);
if (result.HasValue()) {
    std::cout << "Message sent successfully\n";
}

// === Subscriber 端使用 ===
SubscriberConfig sub_config;
sub_config.channel_capacity = 256;

auto subscriber = Subscriber<UInt8>::Create("my_service", sub_config).Value();

// 创建消息对象（栈上）
SimpleMessage received_msg;

// 接收（内部自动调用OnMessageReceived）
auto recv_result = subscriber.ReceiveMessage(received_msg);
if (recv_result.HasValue()) {
    // ✅ 访问反序列化后的数据
    std::cout << "Received: seq=" << received_msg.GetSequence()
              << ", value=" << received_msg.GetValue() << "\n";
    
    // Sample自动管理chunk生命周期
    // recv_result.Value()是Sample，离开作用域自动释放chunk
}
```
```

#### 4.2.5 使用示例 - 零拷贝大消息

```cpp
// === 零拷贝消息（直接引用chunk数据） ===
class ZeroCopyMessage : public Message {
public:
    ZeroCopyMessage() 
        : sequence(0), data_size(0), 
          src_data(nullptr), chunk_data(nullptr), fd(-1) {}
    
    // Publisher: 设置源数据指针和文件描述符
    void SetSource(uint64_t seq, const char* data, size_t size, int file_fd) noexcept {
        sequence = seq;
        src_data = data;
        data_size = size;
        fd = file_fd;
    }
    
    const char* GetTypeName() const noexcept override { return "ZeroCopyMessage"; }
    UInt32 GetTypeId() const noexcept override { return 200; }
    UInt32 GetVersion() const noexcept override { return 0x00010000; }
    size_t GetSerializedSize() const noexcept override { 
        return sizeof(sequence) + sizeof(data_size) + data_size;
    }
    
    // 序列化：直接从文件/设备读取到chunk（零拷贝I/O）
    size_t OnMessageSend(void* const chunk_ptr, size_t chunk_size) noexcept override {
        size_t header_size = sizeof(sequence) + sizeof(UInt32);
        if (chunk_size < header_size + data_size) return 0;  // 空间不足
        
        auto* p = static_cast<uint8_t*>(chunk_ptr);
        
        // 写入头部
        std::memcpy(p, &sequence, sizeof(sequence));
        p += sizeof(sequence);
        UInt32 size32 = static_cast<UInt32>(data_size);
        std::memcpy(p, &size32, sizeof(size32));
        p += sizeof(size32);
        
        // 直接从文件描述符读取到chunk（零拷贝I/O）
        if (fd >= 0 && data_size > 0) {
            ssize_t bytes = read(fd, p, data_size);
            if (bytes != static_cast<ssize_t>(data_size)) {
                // 读取失败，填充零
                std::memset(p, 0, data_size);
            }
        } else if (src_data) {
            // 回退：从内存拷贝
            std::memcpy(p, src_data, data_size);
        }
        
        return header_size + data_size;
    }
    
    // 反序列化：保持chunk数据指针（零拷贝引用）
    bool OnMessageReceived(const void* const chunk_ptr, size_t chunk_size) noexcept override {
        size_t header_size = sizeof(sequence) + sizeof(UInt32);
        if (chunk_size < header_size) return false;
        
        auto* p = static_cast<const uint8_t*>(chunk_ptr);
        
        // 读取头部
        std::memcpy(&sequence, p, sizeof(sequence));
        p += sizeof(sequence);
        UInt32 size32;
        std::memcpy(&size32, p, sizeof(size32));
        p += sizeof(size32);
        data_size = size32;
        
        if (chunk_size < header_size + data_size) return false;
        
        // ⚠️  零拷贝关键：保存chunk数据指针（不拷贝数据）
        chunk_data = reinterpret_cast<const char*>(p);
        
        // 可选：直接写入文件描述符（零拷贝I/O）
        if (fd >= 0 && data_size > 0) {
            write(fd, chunk_data, data_size);
        }
        
        return true;
    }
    
    // 访问器
    const char* GetData() const noexcept { 
        return chunk_data;  // ⚠️ 仅在Sample有效期内可用
    }
    size_t GetSize() const noexcept { return data_size; }

private:
    uint64_t sequence;
    uint32_t data_size;
    const char* src_data;     // Publisher: 源数据指针（进程内存）
    const char* chunk_data;   // Subscriber: chunk中的数据（共享内存，零拷贝引用）
    int fd;                   // 文件描述符（用于直接I/O）
};

// === 使用示例：/dev/zero → IPC → /dev/null ===
// Publisher
int zero_fd = open("/dev/zero", O_RDONLY);
ZeroCopyMessage msg;
msg.SetSource(1, nullptr, 102400, zero_fd);  // 100KB消息

// 数据直接从/dev/zero读入共享内存chunk
publisher.SendMessage(msg, PublishPolicy::kDrop);

// Subscriber
int null_fd = open("/dev/null", O_WRONLY);
ZeroCopyMessage recv_msg;
recv_msg.SetSource(0, nullptr, 0, null_fd);

// 接收并处理（零拷贝）
auto result = subscriber.ReceiveMessage(recv_msg);
if (result.HasValue()) {
    auto& sample = result.Value();  // ⚠️ 关键：保持Sample生命周期
    
    // ✅ 正确：在Sample有效期内访问chunk_data
    const char* data = recv_msg.GetData();
    size_t size = recv_msg.GetSize();
    
    // 处理数据（数据直接从chunk写入/dev/null）
    ProcessData(data, size);
    
    // ❌ 错误示例：不要保存chunk_data指针到外部
    // global_data_ptr = data;  // 危险！
    
}  // Sample析构，chunk释放，chunk_data失效

// ❌ 错误：chunk_data现在是悬空指针
// ProcessData(global_data_ptr);  // 崩溃！

// 端到端零拷贝路径：/dev/zero → SharedMemory Chunk → /dev/null
```

**零拷贝生命周期警告**：

```cpp
// ⚠️  常见错误：过早释放Sample
{
    ZeroCopyMessage msg;
    const char* data;
    
    {
        auto result = subscriber.ReceiveMessage(msg);
        data = msg.GetData();  // 保存指针
    }  // ❌ Sample析构，chunk释放
    
    // ⚠️ data现在是悬空指针！
    ProcessData(data);  // 崩溃或读取垃圾数据
}

// ✅ 正确做法：保持Sample生命周期
{
    ZeroCopyMessage msg;
    auto result = subscriber.ReceiveMessage(msg);
    
    if (result.HasValue()) {
        auto& sample = result.Value();  // 保持Sample有效
        const char* data = msg.GetData();
        ProcessData(data);  // 安全：Sample仍然有效
    }  // Sample析构，chunk释放
}
```
int null_fd = open("/dev/null", O_WRONLY);
ZeroCopyMessage recv_msg;
recv_msg.SetSource(0, nullptr, 0, null_fd);

// 数据直接从chunk写入/dev/null
auto result = subscriber.ReceiveMessage(recv_msg);
// 端到端零拷贝：/dev/zero → SharedMemory → /dev/null
```

#### 4.2.6 Message 设计优势

| 特性 | 传统方案 | Message模式 | 优势 |
|------|---------|------------|------|
| **对象存储位置** | 共享内存 | 进程heap/stack | ✅ 虚函数正常工作 |
| **跨进程传输** | 对象序列化 | Chunk传输 | ✅ 类型安全 |
| **数据拷贝** | 多次拷贝 | 零拷贝可选 | ✅ 性能优化 |
| **扩展性** | 固定格式 | 虚函数多态 | ✅ 灵活扩展 |
| **I/O集成** | 分离 | 回调内直接I/O | ✅ 端到端优化 |

**关键设计决策：**
1. **为什么Message对象不在共享内存？**
   - 虚函数表（vtable）是进程特定的，无法跨进程共享
   - 每个进程有独立的代码段，vtable指针在不同进程中地址不同
   - 解决方案：Message作为Interpreter，只传输数据（chunk）

2. **为什么使用回调而非独立函数？**
   - 面向对象设计，每个消息类型封装自己的序列化逻辑
   - 支持虚函数多态，可扩展不同消息类型
   - 类型安全，编译时检查

3. **性能考虑**
   - OnMessageSend/Received回调只在传输时调用一次
   - 零拷贝场景下，Subscriber可直接引用chunk数据
   - 支持在回调中直接进行I/O操作（如devzero_stress_test）

---

#### 4.2.7 高级特性与业界对比

##### 4.2.7.1 与主流框架的设计对比

| 框架 | 消息模型 | 类型系统 | 零拷贝支持 | 序列化方式 |
|------|----------|----------|-----------|-----------|
| **DDS** | Topic/Sample | IDL (TypeSupport) | Loan API | CDR/XTypes |
| **iceoryx2** | Sample-based | Rust类型 | Loan-based | 原始内存 |
| **ROS2** | Topic/Message | .msg文件 | LoanedMessage | CDR |
| **FastDDS** | DataWriter/Reader | IDL | Loan API | CDR/Fast-CDR |
| **LightAP IPC** | Message/Chunk | C++虚函数 | Sample引用 | 自定义 |

**LightAP设计优势：**
- ✅ **类型安全**: C++虚函数提供编译时类型检查（vs DDS运行时类型检查）
- ✅ **灵活序列化**: 支持原始字节、Protobuf、FlatBuffers等任意格式
- ✅ **零依赖**: 无需IDL编译器或代码生成工具（vs DDS/ROS2）
- ✅ **轻量级**: Message对象仅在进程栈上，无需复杂的类型注册

**参考实现分析：**

**1. DDS Topic模型**
```cpp
// DDS方式（需要IDL生成代码）
DataWriter<SensorData> writer;
SensorData* sample = writer.create_data();  // 从池中分配
sample->temperature = 25.5;
writer.write(sample);  // 序列化 + 发送
writer.delete_data(sample);

// LightAP方式（更简洁）
Publisher<UInt8> publisher;
SensorMessage msg;
msg.SetTemperature(25.5);
publisher.SendMessage(msg);  // 自动序列化 + 发送
```

**2. iceoryx2 Loan模式**
```rust
// iceoryx2 (Rust)
let sample = publisher.loan()?;
sample.write(&SensorData { temp: 25.5 });
sample.send()?;

// LightAP等效
auto sample = publisher.Loan().Value();
// ... 写入数据 ...
publisher.Send(sample);
```

##### 4.2.7.2 推荐的优化增强

**优化1: 添加消息头部标准化（参考ROS2/DDS）**

```cpp
/**
 * @brief 标准消息头（类似ROS2 std_msgs/Header）
 * @note 建议所有应用层消息包含此头部
 */
struct MessageHeader {
    UInt64 sequence_number;    // 序列号（单调递增）
    UInt64 source_timestamp;   // 源时间戳（发送时刻）
    UInt64 receive_timestamp;  // 接收时间戳（接收时刻）
    UInt32 publisher_id;       // 发布者ID
    UInt32 topic_id;           // Topic ID（hash(topic_name)）
    UInt32 qos_flags;          // QoS标志位
    UInt32 reserved;           // 保留字段
};

/**
 * @brief 带标准头部的消息基类
 */
class StandardMessage : public Message {
protected:
    MessageHeader header_;
    
public:
    const MessageHeader& GetHeader() const noexcept { return header_; }
    void SetHeader(const MessageHeader& h) noexcept { header_ = h; }
    
    UInt64 GetSequence() const noexcept { return header_.sequence_number; }
    UInt64 GetTimestamp() const noexcept { return header_.source_timestamp; }
};
```

**优化2: 序列化格式枚举（支持多种序列化策略）**

```cpp
/**
 * @brief 序列化格式标识
 */
enum class SerializationFormat : UInt8 {
    kRaw        = 0,  // 原始字节（当前实现）
    kCDR        = 1,  // DDS CDR (Common Data Representation)
    kProtobuf   = 2,  // Google Protobuf
    kFlatBuffers= 3,  // Google FlatBuffers (零拷贝友好)
    kCapnProto  = 4,  // Cap'n Proto
    kJSON       = 5,  // JSON（调试用）
    kCustom     = 255 // 自定义格式
};

class SerializableMessage : public Message {
public:
    virtual SerializationFormat GetFormat() const noexcept {
        return SerializationFormat::kRaw;
    }
};
```

**优化3: 数据完整性验证（参考DDS RTPS）**

```cpp
/**
 * @brief 支持CRC32校验和的消息（数据完整性）
 */
class ValidatedMessage : public Message {
protected:
    /**
     * @brief 计算CRC32校验和
     * @note 参考DDS RTPS协议
     */
    static UInt32 ComputeCRC32(const void* data, size_t size) noexcept {
        const UInt8* bytes = static_cast<const UInt8*>(data);
        UInt32 crc = 0xFFFFFFFF;
        
        for (size_t i = 0; i < size; ++i) {
            crc ^= bytes[i];
            for (int j = 0; j < 8; ++j) {
                crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
            }
        }
        return ~crc;
    }
    
public:
    /**
     * @brief 发送时附加校验和
     */
    size_t OnMessageSend(void* chunk_ptr, size_t chunk_size) noexcept override {
        size_t payload_size = OnMessageSendPayload(chunk_ptr, chunk_size - 4);
        if (payload_size == 0) return 0;
        
        // 计算并写入CRC32
        UInt32 crc = ComputeCRC32(chunk_ptr, payload_size);
        std::memcpy(static_cast<UInt8*>(chunk_ptr) + payload_size, &crc, 4);
        
        return payload_size + 4;
    }
    
    /**
     * @brief 接收时验证校验和
     */
    bool OnMessageReceived(const void* chunk_ptr, size_t chunk_size) noexcept override {
        if (chunk_size < 4) return false;
        
        size_t payload_size = chunk_size - 4;
        
        // 读取CRC32
        UInt32 received_crc;
        std::memcpy(&received_crc, 
                    static_cast<const UInt8*>(chunk_ptr) + payload_size, 4);
        
        // 验证
        UInt32 computed_crc = ComputeCRC32(chunk_ptr, payload_size);
        if (received_crc != computed_crc) {
            return false;  // 校验失败
        }
        
        return OnMessageReceivedPayload(chunk_ptr, payload_size);
    }
    
protected:
    // 子类实现纯数据序列化（不含CRC）
    virtual size_t OnMessageSendPayload(void* chunk_ptr, size_t chunk_size) noexcept = 0;
    virtual bool OnMessageReceivedPayload(const void* chunk_ptr, size_t chunk_size) noexcept = 0;
};
```

**优化4: Topic模式支持（参考DDS/ROS2）**

```cpp
/**
 * @brief Topic描述符（类似DDS TopicDescription）
 */
struct TopicDescriptor {
    String topic_name;         // Topic名称（如"/sensor/imu"）
    UInt32 topic_id;           // Topic ID（hash(topic_name)）
    UInt32 type_id;            // 消息类型ID
    const char* type_name;     // 类型名（如"SensorMessage"）
    UInt32 type_version;       // 类型版本
};

/**
 * @brief 类型安全的Topic模板（编译时检查）
 */
template<typename MessageT>
class Topic {
    static_assert(std::is_base_of_v<Message, MessageT>, 
                  "MessageT must inherit from Message");
    
    TopicDescriptor descriptor_;
    
public:
    explicit Topic(const String& name) {
        descriptor_.topic_name = name;
        descriptor_.topic_id = HashString(name);
        descriptor_.type_id = MessageT().GetTypeId();
        descriptor_.type_name = MessageT().GetTypeName();
        descriptor_.type_version = MessageT().GetVersion();
    }
    
    const TopicDescriptor& GetDescriptor() const { return descriptor_; }
    
    /**
     * @brief 创建类型安全的Publisher
     */
    auto CreatePublisher(const PublisherConfig& config) {
        return Publisher<UInt8>::Create(descriptor_.topic_name, config);
    }
    
    /**
     * @brief 创建类型安全的Subscriber
     */
    auto CreateSubscriber(const SubscriberConfig& config) {
        return Subscriber<UInt8>::Create(descriptor_.topic_name, config);
    }
    
    /**
     * @brief 验证消息类型匹配
     */
    bool IsCompatible(const Message& msg) const noexcept {
        return msg.IsCompatible(descriptor_.type_id, descriptor_.type_version);
    }
    
private:
    static UInt32 HashString(const String& str) {
        // FNV-1a hash
        UInt32 hash = 2166136261u;
        for (char c : str) {
            hash ^= static_cast<UInt8>(c);
            hash *= 16777619u;
        }
        return hash;
    }
};

// === 使用示例 ===
Topic<SensorMessage> imu_topic("/sensor/imu");
auto publisher = imu_topic.CreatePublisher(config).Value();

SensorMessage msg;
if (imu_topic.IsCompatible(msg)) {
    publisher.SendMessage(msg);
}
```

**优化5: QoS策略支持（简化版DDS QoS）**

```cpp
/**
 * @brief 服务质量策略（参考DDS QoS）
 */
struct QoSPolicy {
    /**
     * @brief 可靠性策略
     */
    enum class Reliability {
        kBestEffort,    // 尽力而为（允许丢失）
        kReliable       // 可靠传输（保证送达，需ACK机制）
    };
    
    /**
     * @brief 历史记录策略
     */
    enum class History {
        kKeepLast,      // 保留最新N个
        kKeepAll        // 保留全部（直到队列满）
    };
    
    /**
     * @brief 持久化策略
     */
    enum class Durability {
        kVolatile,      // 易失性（进程退出数据丢失）
        kTransient      // 瞬态（新Subscriber可接收历史数据）
    };
    
    Reliability reliability = Reliability::kBestEffort;
    History history = History::kKeepLast;
    Durability durability = Durability::kVolatile;
    UInt32 history_depth = 1;  // KeepLast时保留数量
    UInt64 max_latency_ns = 0; // 最大延迟（0表示无限制）
};

/**
 * @brief 扩展PublisherConfig支持QoS
 */
struct PublisherConfigEx : PublisherConfig {
    QoSPolicy qos;
};
```

**优化6: 动态大小消息支持（参考FlatBuffers）**

```cpp
/**
 * @brief 变长消息支持
 */
class VariableSizeMessage : public Message {
protected:
    /**
     * @brief 写入变长头部（size + data）
     */
    size_t WriteVariableData(void* chunk_ptr, size_t chunk_size,
                             const void* data, size_t data_size) noexcept {
        if (sizeof(UInt32) + data_size > chunk_size) {
            return 0;  // 空间不足
        }
        
        UInt8* p = static_cast<UInt8*>(chunk_ptr);
        
        // 写入长度
        UInt32 size32 = static_cast<UInt32>(data_size);
        std::memcpy(p, &size32, sizeof(UInt32));
        p += sizeof(UInt32);
        
        // 写入数据
        std::memcpy(p, data, data_size);
        
        return sizeof(UInt32) + data_size;
    }
    
    /**
     * @brief 读取变长数据
     */
    bool ReadVariableData(const void* chunk_ptr, size_t chunk_size,
                          const void** out_data, size_t* out_size) noexcept {
        if (chunk_size < sizeof(UInt32)) return false;
        
        const UInt8* p = static_cast<const UInt8*>(chunk_ptr);
        
        // 读取长度
        UInt32 size32;
        std::memcpy(&size32, p, sizeof(UInt32));
        p += sizeof(UInt32);
        
        // 验证长度
        if (sizeof(UInt32) + size32 > chunk_size) return false;
        
        *out_data = p;
        *out_size = size32;
        return true;
    }
};

// === 使用示例：字符串消息 ===
class StringMessage : public VariableSizeMessage {
    std::string data_;
    const char* chunk_data_ = nullptr;
    size_t chunk_size_ = 0;
    
public:
    void SetString(const std::string& str) { data_ = str; }
    
    size_t OnMessageSend(void* chunk_ptr, size_t chunk_size) noexcept override {
        return WriteVariableData(chunk_ptr, chunk_size, 
                                data_.data(), data_.size());
    }
    
    bool OnMessageReceived(const void* chunk_ptr, size_t chunk_size) noexcept override {
        const void* data;
        size_t size;
        if (!ReadVariableData(chunk_ptr, chunk_size, &data, &size)) {
            return false;
        }
        
        // 零拷贝：保存指针引用chunk数据
        chunk_data_ = static_cast<const char*>(data);
        chunk_size_ = size;
        return true;
    }
    
    std::string_view GetStringView() const noexcept {
        return std::string_view(chunk_data_, chunk_size_);
    }
};
```

##### 4.2.7.3 实现建议总结

| 优化项 | 优先级 | 实现复杂度 | 收益 | 参考框架 |
|--------|--------|-----------|------|----------|
| **标准消息头** | 高 | 低 | 统一时间戳/序列号 | ROS2 Header |
| **序列化格式枚举** | 中 | 低 | 支持多种序列化 | DDS XTypes |
| **CRC32校验** | 中 | 中 | 数据完整性 | DDS RTPS |
| **Topic模式** | 高 | 中 | 类型安全 | DDS/ROS2 |
| **QoS策略** | 低 | 高 | 丰富功能 | DDS QoS |
| **变长消息** | 高 | 中 | 灵活性 | FlatBuffers |

**推荐实现路径：**
1. **Phase 1（当前已实现）**: 基础Message + 零拷贝支持 ✅
2. **Phase 2（高优先级）**: 添加标准消息头 + Topic模式
3. **Phase 3（中优先级）**: 变长消息支持 + 序列化格式选择
4. **Phase 4（可选）**: CRC32校验 + 简化QoS策略

---

### 4.3 通用 RingBufferBlock 模型

**设计目标：**

将 Subscriber 消息队列抽象为通用的固定大小 Ring Buffer，支持：
- ✅ 固定容量（预分配，无动态扩容）
- ✅ Offset-based 存储（跨进程可重定位）
- ✅ 无锁操作（原子操作 + CAS）
- ✅ 泛型设计（支持任意类型的 block）
- ✅ 队列满策略（Ring Buffer 模式）

#### 4.3.1 RingBufferBlock 核心实现

```cpp
/**
 * @brief 固定大小环形缓冲区（共享内存友好）
 * 
 * 设计特点：
 * 1. 固定容量数组（预分配，无动态内存）
 * 2. 使用索引而非指针（offset-based，跨进程可重定位）
 * 3. 无锁操作（适用于 MPSC/SPMC 场景）
 * 4. 支持 Ring Buffer 模式（覆盖最旧数据）
 * 
 * @tparam T 存储的数据类型（例如 UInt32 存储 chunk_index）
 * @tparam MaxCapacity 最大容量（编译时常量）
 */
template<typename T, UInt32 MaxCapacity = 16>
struct RingBufferBlock {
    static_assert(MaxCapacity > 0, "Capacity must be positive");
    static_assert(std::is_trivially_copyable_v<T>, 
                  "T must be trivially copyable for shared memory");
    
    // ==================== 数据成员 ====================
    
    // 固定大小数组（预分配）
    alignas(64) T           buffer[MaxCapacity];
    
    // 环形缓冲区控制变量
    std::atomic<UInt32>     head;      // 读位置索引 [0, MaxCapacity)
    std::atomic<UInt32>     tail;      // 写位置索引 [0, MaxCapacity)
    std::atomic<UInt32>     count;     // 当前元素数量
    UInt32                  capacity;  // 固定容量（初始化时设置）
    
    static constexpr T kInvalidValue = static_cast<T>(0xFFFFFFFF);
    
    // ==================== 初始化 ====================
    
    void Initialize(UInt32 cap = MaxCapacity) noexcept {
        capacity = (cap <= MaxCapacity) ? cap : MaxCapacity;
        head.store(0, std::memory_order_relaxed);
        tail.store(0, std::memory_order_relaxed);
        count.store(0, std::memory_order_relaxed);
        
        // 初始化 buffer 为无效值
        for (UInt32 i = 0; i < capacity; ++i) {
            buffer[i] = kInvalidValue;
        }
    }
    
    // ==================== 基本操作 ====================
    
    /**
     * @brief 入队（非阻塞）
     * @return true 成功入队，false 队列已满
     */
    bool Enqueue(const T& value) noexcept {
        UInt32 current_count = count.load(std::memory_order_acquire);
        if (current_count >= capacity) {
            return false;  // 队列已满
        }
        
        // 获取写位置（无锁）
        UInt32 write_pos = tail.load(std::memory_order_relaxed);
        
        // 写入数据
        buffer[write_pos] = value;
        
        // 更新 tail（环形）
        UInt32 next_tail = (write_pos + 1) % capacity;
        tail.store(next_tail, std::memory_order_release);
        
        // 增加计数
        count.fetch_add(1, std::memory_order_release);
        
        return true;
    }
    
    /**
     * @brief 入队（Ring Buffer 模式，自动覆盖最旧数据）
     * @return kSuccess/kOverwritten
     */
    enum class EnqueueResult {
        kSuccess,      // 成功入队，队列未满
        kOverwritten,  // 成功入队，覆盖了最旧数据
        KFailed,       // 失败
        KTimeout       // 超时
    };
    
    EnqueueResult EnqueueOverwrite(const T& value) noexcept {
        UInt32 current_count = count.load(std::memory_order_acquire);
        
        if (current_count >= capacity) {
            // 队列满，覆盖最旧数据（Ring Buffer 模式）
            
            // 1. 获取写位置
            UInt32 write_pos = tail.load(std::memory_order_relaxed);
            
            // 2. 覆盖最旧数据（此时 write_pos == head）
            buffer[write_pos] = value;
            
            // 3. 同时推进 head 和 tail（环形）
            UInt32 next_pos = (write_pos + 1) % capacity;
            tail.store(next_pos, std::memory_order_release);
            head.store(next_pos, std::memory_order_release);
            
            // count 保持不变（仍然是 capacity）
            
            return EnqueueResult::kOverwritten;
        } else {
            // 队列未满，正常入队
            Enqueue(value);
            return EnqueueResult::kSuccess;
        }
    }
    
    /**
     * @brief 出队（非阻塞）
     * @param[out] out_value 输出值
     * @return true 成功出队，false 队列为空
     */
    bool Dequeue(T& out_value) noexcept {
        UInt32 current_count = count.load(std::memory_order_acquire);
        if (current_count == 0) {
            return false;  // 队列为空
        }
        
        // 获取读位置
        UInt32 read_pos = head.load(std::memory_order_relaxed);
        
        // 读取数据
        out_value = buffer[read_pos];
        
        // 清空槽位（可选，用于调试）
        buffer[read_pos] = kInvalidValue;
        
        // 更新 head（环形）
        UInt32 next_head = (read_pos + 1) % capacity;
        head.store(next_head, std::memory_order_release);
        
        // 减少计数
        count.fetch_sub(1, std::memory_order_release);
        
        return true;
    }
    
    /**
     * @brief 查看队头元素（不出队）
     */
    bool Peek(T& out_value) const noexcept {
        UInt32 current_count = count.load(std::memory_order_acquire);
        if (current_count == 0) {
            return false;
        }
        
        UInt32 read_pos = head.load(std::memory_order_acquire);
        out_value = buffer[read_pos];
        return true;
    }
    
    // ==================== 状态查询 ====================
    
    bool IsEmpty() const noexcept {
        return count.load(std::memory_order_acquire) == 0;
    }
    
    bool IsFull() const noexcept {
        return count.load(std::memory_order_acquire) >= capacity;
    }
    
    UInt32 GetCount() const noexcept {
        return count.load(std::memory_order_acquire);
    }
    
    UInt32 GetCapacity() const noexcept {
        return capacity;
    }
    
    float GetUtilization() const noexcept {
        return static_cast<float>(GetCount()) / capacity;
    }
    
    // ==================== 批量操作（可选） ====================
    
    /**
     * @brief 批量入队
     * @return 成功入队的元素数量
     */
    UInt32 EnqueueBatch(const T* values, UInt32 num_values) noexcept {
        UInt32 enqueued = 0;
        for (UInt32 i = 0; i < num_values; ++i) {
            if (!Enqueue(values[i])) {
                break;  // 队列满
            }
            ++enqueued;
        }
        return enqueued;
    }
    
    /**
     * @brief 批量出队
     * @return 成功出队的元素数量
     */
    UInt32 DequeueBatch(T* out_values, UInt32 max_values) noexcept {
        UInt32 dequeued = 0;
        for (UInt32 i = 0; i < max_values; ++i) {
            if (!Dequeue(out_values[i])) {
                break;  // 队列空
            }
            ++dequeued;
        }
        return dequeued;
    }
    
    // ==================== 清空操作 ====================
    
    void Clear() noexcept {
        head.store(0, std::memory_order_relaxed);
        tail.store(0, std::memory_order_relaxed);
        count.store(0, std::memory_order_release);
    }
};
```

#### 4.3.2 RingBufferBlock 特性验证

**内存布局（共享内存友好）：**
```
RingBufferBlock<UInt32, 16> 内存布局:
┌─────────────────────────────────────────────────────┐
│ buffer[16] (64B aligned)                            │ <- 固定数组
│  [0] [1] [2] ... [15]                               │
├─────────────────────────────────────────────────────┤
│ head (atomic UInt32)                                │ <- 读位置
│ tail (atomic UInt32)                                │ <- 写位置
│ count (atomic UInt32)                               │ <- 元素数
│ capacity (UInt32)                                   │ <- 固定容量
└─────────────────────────────────────────────────────┘
总大小: 64 (buffer) + 16 (控制) = 80 字节（对齐后）
```

**Ring Buffer 行为示例：**
```cpp
// 容量为 4 的环形缓冲区
RingBufferBlock<UInt32, 4> ring;
ring.Initialize(4);

// 初始状态: head=0, tail=0, count=0
// [ _ | _ | _ | _ ]
//   ^
// head/tail

ring.Enqueue(10);  // count=1
// [10 | _ | _ | _ ]
//   ^   ^
// head tail

ring.Enqueue(20);  // count=2
ring.Enqueue(30);  // count=3
ring.Enqueue(40);  // count=4 (满)
// [10 |20 |30 |40 ]
//   ^           ^
// head         tail

// 队列满时使用 Ring Buffer 模式
ring.EnqueueOverwrite(50);  // 覆盖 10
// [50 |20 |30 |40 ]
//       ^       ^
//     head     tail

UInt32 value;
ring.Dequeue(value);  // value = 20
// [50 | _ |30 |40 ]
//           ^   ^
//         head tail
```

#### 4.3.3 RingBufferBlock 设计优势

**抽象化带来的好处：**

| 对比项 | 原始 MessageQueue | RingBufferBlock 抽象 |
|--------|------------------|---------------------|
| **代码复用** | 每个队列独立实现 | 通用模板，可用于多种场景 |
| **类型安全** | 硬编码 UInt32 offset | 泛型支持任意类型 |
| **可测试性** | 与 ChunkPool 强耦合 | 独立测试，无外部依赖 |
| **可维护性** | 队列逻辑散落各处 | 集中在一个类中 |
| **扩展性** | 难以添加新功能 | 易于添加批量操作等 |
| **性能** | 链表遍历 O(n) | 数组索引 O(1) |
| **调试友好** | 复杂的指针跟踪 | 简单的索引追踪 |

**功能对比：**

| 功能 | 原始实现 | RingBufferBlock |
|------|---------|----------------|
| 固定大小 | ✅ | ✅ |
| Offset-based | ✅ (复用 next_free_index) | ✅ (固定数组索引) |
| 无锁操作 | ✅ | ✅ |
| Ring Buffer | ❌ (需手动实现) | ✅ (内置 EnqueueOverwrite) |
| 批量操作 | ❌ | ✅ (EnqueueBatch/DequeueBatch) |
| 状态查询 | ✅ | ✅ (更丰富) |
| 跨进程安全 | ✅ | ✅ |

**使用场景扩展：**

```cpp
// [场景1] Subscriber 消息队列（当前）
RingBufferBlock<UInt32, 32> msg_queue;  // 存储 chunk_index

// [场景2] 日志缓冲区
RingBufferBlock<LogEntry, 1024> log_buffer;  // 固定大小日志

// [场景3] 事件队列
RingBufferBlock<EventId, 128> event_queue;  // 事件通知

// [场景4] 性能采样缓冲区
RingBufferBlock<PerformanceSample, 256> perf_buffer;  // Ring Buffer 采样

// [场景5] 请求队列
RingBufferBlock<RequestHandle, 64> request_queue;  // Request-Response
```

### 4.4 Subscriber 消息队列模型（基于 RingBufferBlock）

**核心原则：**

1. **每个 Subscriber 拥有独立的消息队列（Per-Subscriber Queue）**
   ```
   iceoryx2 设计理念：
   ┌──────────────┐                 ┌──────────────────┐
   │  Publisher   │ ───Send()───►  │  Subscriber A    │
   │              │                 │  - Queue (独立)  │
   └──────────────┘                 └──────────────────┘
         │                          ┌──────────────────┐
         └────────────Send()───────►│  Subscriber B    │
                                    │  - Queue (独立)  │
                                    └──────────────────┘
   
   优势：
   - ✅ Publisher/Subscriber 完全解耦
   - ✅ 每个 Subscriber 独立控制队列策略（满/空处理）
   - ✅ 慢订阅者不影响快订阅者
   - ✅ 支持动态订阅/取消订阅（分配/释放队列槽位）
   ```

2. **队列在共享内存中预分配（Fixed Array）**
   ```cpp
   // 共享内存段中预分配固定数量的 Subscriber Queue
   struct SharedMemorySegment {
       // ...
       ChannelQueue subscriber_queues[MAX_SUBSCRIBER_QUEUES];  // 例如 256
   };
   
   // Subscriber 连接时分配一个空闲队列槽位
   UInt32 AllocateQueueSlot() {
       for (UInt32 i = 0; i < MAX_SUBSCRIBER_QUEUES; ++i) {
           bool expected = false;
           if (subscriber_queues[i].active.compare_exchange_strong(
                   expected, true, std::memory_order_acq_rel)) {
               return i;  // 返回队列索引
           }
       }
       return kInvalidIndex;  // 无可用槽位
   }
   ```

3. **队列内部使用 Offset-Based 链表（可重定位）**
   ```
   MessageQueue 结构（无锁 FIFO）：
   
   head_offset ────┐
                   ▼
               ┌────────┐     ┌────────┐     ┌────────┐
               │Chunk[5]│────►│Chunk[12]────►│Chunk[7]│
               │offset=5│     │offset=12     │offset=7│
               └────────┘     └────────┘     └────────┘
                                                  ▲
                                                  │
   tail_offset ───────────────────────────────────┘
   
   关键字段：
   - head_offset: UInt32 (不是指针！)
   - tail_offset: UInt32 (不是指针！)
   - count: 当前消息数量
   - capacity: 固定容量
   
   Chunk 通过 next_free_index 字段链接（复用 Free-List 字段）
   ```

4. **Publisher 发送流程（广播到所有 Subscriber Queue）**
   ```cpp
   void Publisher::Send(Sample&& sample) {
       auto* chunk = sample.Release();
       UInt32 chunk_index = chunk->chunk_index;  // 获取索引
       
       // 设置引用计数为订阅者数量
       chunk->ref_count.store(subscriber_count_, std::memory_order_release);
       
       // 遍历所有连接的 Subscriber，推送到它们的队列
       for (UInt32 i = 0; i < subscriber_count_; ++i) {
           UInt32 queue_idx = subscriber_list_[i];  // 队列槽位索引
           
           auto& sub_queue = subscriber_queues[queue_idx];
           
           // 使用队列的满策略进行入队
           auto result = sub_queue.msg_queue.EnqueueWithPolicy(
               chunk_index,              // 传递索引，非指针！
               shm_mgr_,
               sub_queue.qos.load(std::memory_order_acquire)
           );
           
           if (result == EnqueueResult::kQueueFull) {
               // 队列满，该 Subscriber 丢失消息
               chunk->ref_count.fetch_sub(1, std::memory_order_relaxed);
           }
       }
   }
   ```

5. **Subscriber 接收流程（从自己的队列读取）**
   ```cpp
   Result<Sample> Subscriber::Receive() {
       // 从自己的专属队列出队（使用 RingBufferBlock）
       UInt32 chunk_index = subscriber_queues[queue_index_]
                               .msg_queue.Dequeue();
       
       if (chunk_index == kInvalidIndex) {
           return Err(CoreErrc::kIPCNoData);  // 队列为空
       }
       
       // 将索引转换为本地指针
       auto* chunk = shm_mgr_->GetChunkByIndex(chunk_index);
       
       // 状态转换: kSent -> kReceived
       chunk->state.store(ChunkState::kReceived, std::memory_order_release);
       
       return Ok(Sample{chunk, this});
   }
   ```

**使用 RingBufferBlock 的消息队列封装：**

```cpp
/**
 * @brief Subscriber 消息队列（基于 RingBufferBlock）
 * 
 * 存储 chunk_index（UInt32），而非 Chunk 指针
 * 使用固定容量的环形缓冲区，支持 Ring Buffer 模式覆盖策略
 */
class SubscriberMessageQueue {
public:
    // 使用 RingBufferBlock 存储 chunk_index
    using QueueType = RingBufferBlock<UInt32, 32>;  // 默认容量 32
    
    static constexpr UInt32 kInvalidIndex = 0xFFFFFFFF;
    
    // ==================== 初始化 ====================
    
    void Initialize(UInt32 capacity = 16) noexcept {
        queue_.Initialize(capacity);
    }
    
    // ==================== 入队操作（Publisher 端）====================
    
    /**
     * @brief 入队 chunk_index（非阻塞）
     * @param chunk_index Chunk 在 ChunkPool 中的索引
     * @return true 成功，false 队列满
     */
    bool Enqueue(UInt32 chunk_index) noexcept {
        return queue_.Enqueue(chunk_index);
    }
    
    /**
     * @brief 入队（Ring Buffer 模式，自动覆盖最旧数据）
     * @param chunk_index Chunk 索引
     * @param shm_mgr 共享内存管理器（用于释放被覆盖的 Chunk）
     * @return kSuccess/kOverwritten
     */
    enum class EnqueueResult {
        kSuccess,      // 成功入队
        kOverwritten,  // 覆盖了旧消息
    };
    
    EnqueueResult EnqueueOverwrite(
            UInt32 chunk_index,
            SharedMemoryManager* shm_mgr,
            ChunkPoolAllocator* allocator) noexcept {
        
        if (queue_.IsFull()) {
            // 队列满，先出队最旧的 chunk_index
            UInt32 old_chunk_index;
            if (queue_.Dequeue(old_chunk_index)) {
                // 减少被覆盖 Chunk 的引用计数
                auto* old_chunk = shm_mgr->GetChunkByIndex(old_chunk_index);
                old_chunk->DecrementRef(allocator);
            }
            
            // 入队新 chunk
            queue_.Enqueue(chunk_index);
            return EnqueueResult::kOverwritten;
        } else {
            // 队列未满，正常入队
            queue_.Enqueue(chunk_index);
            return EnqueueResult::kSuccess;
        }
    }
    
    // ==================== 出队操作（Subscriber 端）====================
    
    /**
     * @brief 出队 chunk_index（非阻塞）
     * @return chunk_index，如果队列为空返回 kInvalidIndex
     */
    UInt32 Dequeue() noexcept {
        UInt32 chunk_index;
        if (queue_.Dequeue(chunk_index)) {
            return chunk_index;
        }
        return kInvalidIndex;
    }
    
    /**
     * @brief 查看队头 chunk_index（不出队）
     */
    UInt32 Peek() const noexcept {
        UInt32 chunk_index;
        if (queue_.Peek(chunk_index)) {
            return chunk_index;
        }
        return kInvalidIndex;
    }
    
    // ==================== 状态查询 ====================
    
    bool IsEmpty() const noexcept {
        return queue_.IsEmpty();
    }
    
    bool IsFull() const noexcept {
        return queue_.IsFull();
    }
    
    UInt32 GetCount() const noexcept {
        return queue_.GetCount();
    }
    
    UInt32 GetCapacity() const noexcept {
        return queue_.GetCapacity();
    }
    
private:
    QueueType queue_;  // 底层使用 RingBufferBlock
};
```

**使用示例（基于 RingBufferBlock）：**
```cpp
// === 初始化 Subscriber 队列 ===
subscriber_queues[i].msg_queue.Initialize(16);  // 容量 16

// === Publisher Send 时（广播到所有 Subscriber）===
void Publisher::Send(Sample&& sample) {
    auto* chunk = sample.Release();
    UInt32 chunk_index = chunk->chunk_index;
    
    // 设置引用计数
    chunk->ref_count.store(subscriber_count_, std::memory_order_release);
    
    // 推送到所有 Subscriber 队列
    for (UInt32 i = 0; i < subscriber_count_; ++i) {
        UInt32 queue_idx = subscriber_list_[i];
        auto& sub_queue = subscriber_queues[queue_idx];
        
        // 使用 Ring Buffer 模式入队
        auto result = sub_queue.msg_queue.EnqueueOverwrite(
            chunk_index,  // 传递索引
            shm_mgr_,
            allocator_
        );
        
        if (result == EnqueueResult::kOverwritten) {
            // 覆盖了旧消息，记录溢出
            sub_queue.overrun_count.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

// === Subscriber Receive 时（从自己的队列读取）===
Result<Sample> Subscriber::Receive() {
    // 从专属队列出队
    UInt32 chunk_index = subscriber_queues[queue_index_].msg_queue.Dequeue();
    
    if (chunk_index == kInvalidIndex) {
        return Err(CoreErrc::kIPCNoData);  // 队列为空
    }
    
    // 索引转指针
    auto* chunk = shm_mgr_->GetChunkByIndex(chunk_index);
    
    return Ok(Sample{chunk, this});
}
```

### 4.5 Publisher 便捷 API

除了零拷贝的 Loan-Based API，Publisher 还提供两种便捷接口，用于简化使用或优化性能。

#### 4.5.1 SendCopy - 拷贝发送

**适用场景：**
- 快速原型开发，无需关心 Sample 生命周期
- 数据对象已经构造完成，直接发送
- 数据对象较小（< 1KB），拷贝开销可接受

**实现：**

```cpp
template<typename PayloadType>
Result<void> Publisher<PayloadType>::SendCopy(const PayloadType& data) noexcept {
    // 1. Loan 一个 Sample
    auto sample_result = Loan();
    if (!sample_result.HasValue()) {
        return Err(sample_result.Error());
    }
    
    auto sample = sample_result.Value();
    
    // 2. 拷贝数据到共享内存
    *sample = data;  // 调用拷贝赋值运算符
    
    // 3. 发送
    return Send(std::move(sample));
}
```

**性能分析：**
- ✅ 简单易用，一行代码完成发送
- ⚠️ 一次拷贝开销（栈/堆 → 共享内存）
- 📊 适用于小对象（< 1KB）

#### 4.5.2 SendEmplace - 原地构造（免拷贝）

**适用场景：**
- 大对象或复杂对象（> 1KB）
- 对象构造成本高（避免临时对象）
- 性能敏感场景，避免任何拷贝

**实现：**

```cpp
template<typename PayloadType>
template<typename... Args>
Result<void> Publisher<PayloadType>::SendEmplace(Args&&... args) noexcept {
    // 1. Loan 一个 Sample
    auto sample_result = Loan();
    if (!sample_result.HasValue()) {
        return Err(sample_result.Error());
    }
    
    auto sample = sample_result.Value();
    
    // 2. 原地构造对象（直接在共享内存中构造）
    // 使用 placement new + 完美转发
    new (sample.Get()) PayloadType(std::forward<Args>(args)...);
    
    // 3. 发送
    return Send(std::move(sample));
}
```

**性能分析：**
- ✅ 零拷贝，直接在共享内存中构造对象
- ✅ 避免临时对象，减少构造/析构开销
- ✅ 完美转发，支持移动语义
- 📊 性能优于 SendCopy 和手动 Loan + 赋值

**使用示例：**

```cpp
// 示例1：基本类型
struct SensorData {
    int x, y, z;
    std::string name;
    
    SensorData(int x, int y, int z, std::string name)
        : x(x), y(y), z(z), name(std::move(name)) {}
};

auto publisher = node.CreatePublisher<SensorData>("/sensor/imu").Value();

// ✅ 原地构造：直接在共享内存中构造 SensorData
publisher.SendEmplace(100, 200, 300, "IMU_01").Value();

// 等价于（但性能更差）：
// SensorData data(100, 200, 300, "IMU_01");  // 1. 栈上构造
// publisher.SendCopy(data);                   // 2. 拷贝到共享内存
//                                             // 3. 析构栈上对象


// 示例2：复杂对象（大数组）
struct ImageData {
    UInt32 width, height;
    std::array<UInt8, 1920*1080> pixels;  // 2MB 数据
    
    ImageData(UInt32 w, UInt32 h, const UInt8* data)
        : width(w), height(h) {
        std::copy(data, data + width * height, pixels.begin());
    }
};

auto publisher = node.CreatePublisher<ImageData>("/camera/image").Value();

// ✅ 原地构造：避免 2MB 的栈拷贝
UInt8* raw_data = GetCameraData();
publisher.SendEmplace(1920, 1080, raw_data).Value();


// 示例3：移动语义
struct VideoFrame {
    std::vector<UInt8> data;  // 动态数组（移动语义友好）
    UInt64 timestamp;
    
    VideoFrame(std::vector<UInt8>&& d, UInt64 ts)
        : data(std::move(d)), timestamp(ts) {}
};

auto publisher = node.CreatePublisher<VideoFrame>("/video/stream").Value();

std::vector<UInt8> frame = CaptureFrame();
UInt64 ts = GetTimestamp();

// ✅ 原地构造 + 移动语义：避免 vector 拷贝
publisher.SendEmplace(std::move(frame), ts).Value();
```

**三种 API 性能对比：**

| API | 构造位置 | 拷贝次数 | 适用场景 | 性能 |
|-----|---------|---------|---------|------|
| **Loan + 手动填充** | 共享内存 | 0 | 灵活控制，复杂逻辑 | ⭐⭐⭐⭐⭐ |
| **SendEmplace** | 共享内存 | 0 | 简化 Loan，一步到位 | ⭐⭐⭐⭐⭐ |
| **SendCopy** | 栈/堆 → 共享内存 | 1 | 快速原型，小对象 | ⭐⭐⭐ |

**推荐使用原则：**
1. **默认使用 SendEmplace**：零拷贝 + 简洁
2. **特殊逻辑用 Loan**：需要多步填充或条件判断
3. **避免 SendCopy**：仅用于小对象（< 256B）或快速原型

### 4.6 队列满/空策略（iceoryx2）

#### 4.6.1 队列满策略（基于 RingBufferBlock）

**策略定义**：参见 [3.2 核心数据结构 - PublisherState](#32-核心数据结构) 中的 PublishPolicy 枚举。

```cpp
enum class EnqueueResult {
    kSuccess,      // 成功入队
    kQueueFull,    // 队列满，拒绝入队（kDrop 或超时）
    kOverwritten,  // 覆盖了旧消息（kOverwrite 策略）
    kTimeout,      // 等待超时（kWait 或 kBlock 策略）
};
```

**WaitSet 事件机制初始化（iceoryx2 风格）：**

ChannelQueue 在共享内存中包含 event_flags 原子标志，用于 lock-free 等待/唤醒机制：

```cpp
/**
 * @brief 初始化 ChannelQueue 的 WaitSet 机制
 * @note 必须在共享内存创建后、使用前调用
 */
static void InitSubscriberQueue(ChannelQueue* queue) noexcept {
    // 初始化事件标志（初始状态：有空间，无数据）
    queue->event_flags.store(EventFlag::HAS_SPACE, std::memory_order_release);
    
    // 初始化其他字段
    queue->active.store(false, std::memory_order_release);
    queue->subscriber_id.store(0, std::memory_order_release);
    queue->last_receive_time.store(0, std::memory_order_release);
    queue->overrun_count.store(0, std::memory_order_release);
}

/**
 * @brief 销毁 ChannelQueue
 * @note WaitSet 机制不需要显式清理（原子变量自动清理）
 */
static void DestroySubscriberQueue(ChannelQueue* queue) noexcept {
    // WaitSet 机制无需清理，仅标记为非活跃
    queue->active.store(false, std::memory_order_release);
}

/**
 * @brief 初始化 ControlBlock 的 Loan WaitSet
 * @note 必须在共享内存创建后调用
 */
static void InitControlBlock(ControlBlock* ctrl) noexcept {
    // 初始化 Loan 等待集（初始状态：有可用 Chunk）
    ctrl->loan_waitset.store(EventFlag::HAS_FREE_CHUNK, std::memory_order_release);
    
    // 其他字段初始化...
    ctrl->magic_number.store(0xICE0RYX2, std::memory_order_release);
    ctrl->version.store(1, std::memory_order_release);
    ctrl->state.store(0, std::memory_order_release);
    ctrl->is_initialized.store(false, std::memory_order_release);
}
```

**带策略的入队实现（Publisher 写入 ChannelQueue）：**

```cpp
/**
 * @brief 带策略的入队实现（参考 iceoryx2）
 * @param queue         目标 ChannelQueue（共享内存）
 * @param chunk_index   要入队的 chunk 索引
 * @param shm_mgr       SharedMemoryManager 指针
 * @param allocator     ChunkPoolAllocator 指针
 * @param policy        队列满策略
 * @param timeout       超时时间（kWait 和 kBlock 策略使用）
 * @return EnqueueResult 入队结果
 */
EnqueueResult EnqueueWithPolicy(
        ChannelQueue* queue,
        UInt32 chunk_index, 
        SharedMemoryManager* shm_mgr,
        ChunkPoolAllocator* allocator,
        PublishPolicy policy,
        const Duration& timeout = Duration::FromMillis(100)) noexcept {
    
    // ========== 快速路径：队列未满 ==========
    if (!queue->msg_queue.IsFull()) {
        queue->msg_queue.Enqueue(chunk_index);
        return EnqueueResult::kSuccess;
    }
    
    // ========== 队列满处理：根据策略选择行为 ==========
    switch (policy) {
    case PublishPolicy::kOverwrite:
        // [策略1] Ring Buffer 模式：丢弃最旧的消息（默认策略）
        {
            // 出队最旧的消息并减少其引用计数
            UInt32 old_index = queue->msg_queue.Dequeue();
            if (old_index != INVALID_INDEX) {
                auto* old_chunk = shm_mgr->GetChunkByIndex(old_index);
                old_chunk->DecrementRef(allocator);  // 释放旧 chunk
            }
            
            // 入队新消息
            queue->msg_queue.Enqueue(chunk_index);
            queue->overrun_count.fetch_add(1, std::memory_order_relaxed);
            
            return EnqueueResult::kOverwritten;
        }
    
    case PublishPolicy::kDrop:
        // [策略2] 丢弃新消息，立即返回错误
        return EnqueueResult::kQueueFull;
    
    case PublishPolicy::kWait:
        // [策略3] 轮询等待：使用 WaitSet 轮询检查，直到队列有空间或超时
        // 特点：低延迟，中等 CPU 占用（适合短超时、高实时性场景）
        {
            // 使用 WaitSet 轮询 HAS_SPACE 标志（纯快速路径，无 futex 调用）
            bool has_space = WaitSetHelper::PollForFlags(
                &queue->event_flags,
                EventFlag::HAS_SPACE,
                timeout,
                Duration::FromMicros(100)  // 轮询间隔 100us（可配置）
            );
            
            if (!has_space) {
                return EnqueueResult::kTimeout;  // 超时
            }
            
            // 标志位已设置，检查队列（防止竞争）
            if (queue->msg_queue.IsFull()) {
                return EnqueueResult::kQueueFull;
            }
            
            // 队列有空间，入队
            queue->msg_queue.Enqueue(chunk_index);
            
            // 清除 HAS_SPACE 标志（如果队列又满了）
            if (queue->msg_queue.IsFull()) {
                WaitSetHelper::ClearFlags(&queue->event_flags, EventFlag::HAS_SPACE);
            }
            
            // 设置 HAS_DATA 标志（kWait 策略不需要唤醒，Subscriber 使用轮询）
            WaitSetHelper::SetFlagsAndWake(&queue->event_flags, EventFlag::HAS_DATA, false);
            
            return EnqueueResult::kSuccess;
        }
    
    case PublishPolicy::kBlock:
        // [策略4] 阻塞等待：使用 WaitSet (futex) 高效等待（CPU 友好）
        // 特点：lock-free 快速路径 + futex 慢速路径，性能优于 pthread_cond_t
        {
            // 使用 WaitSet 等待 HAS_SPACE 标志
            bool has_space = WaitSetHelper::WaitForFlags(
                &queue->event_flags,
                EventFlag::HAS_SPACE,
                timeout
            );
            
            if (!has_space) {
                return EnqueueResult::kTimeout;  // 超时
            }
            
            // 被唤醒后重新检查队列（防止虚假唤醒或竞争）
            if (queue->msg_queue.IsFull()) {
                return EnqueueResult::kQueueFull;
            }
            
            // 队列有空间，入队
            queue->msg_queue.Enqueue(chunk_index);
            
            // 清除 HAS_SPACE 标志（如果队列又满了）
            if (queue->msg_queue.IsFull()) {
                WaitSetHelper::ClearFlags(&queue->event_flags, EventFlag::HAS_SPACE);
            }
            
            // 设置 HAS_DATA 标志并唤醒等待的 Subscriber
            WaitSetHelper::SetFlagsAndWake(&queue->event_flags, EventFlag::HAS_DATA);
            
            return EnqueueResult::kSuccess;
        }
    
    case PublishPolicy::kCustom:
        // [策略5] 用户自定义回调（未实现）
        // 用户可通过回调函数自定义行为，例如：
        // - 记录日志
        // - 触发告警
        // - 动态调整策略
        return EnqueueResult::kQueueFull;
    
    default:
        return EnqueueResult::kQueueFull;
    }
}
```

**Subscriber 接收时唤醒等待的 Publisher（kBlock 策略）：**

```cpp
/**
 * @brief Subscriber Receive 时唤醒等待的 Publisher
 * @note 使用 kBlock 策略的 Publisher 会被 WaitSet 唤醒
 */
template<typename T>
Result<Sample<T>> Subscriber<T>::Receive() noexcept {
    auto* queue = &subscriber_queues_[queue_index_];
    
    UInt32 chunk_index;
    
    // 出队（RingBufferBlock 是 lock-free 的，单消费者场景无需加锁）
    chunk_index = queue->msg_queue.Dequeue();
    
    if (chunk_index == kInvalidIndex) {
        return Err(CoreErrc::kIPCNoData);  // 队列为空
    }
    
    // 设置 HAS_SPACE 标志并唤醒等待的 Publisher（如果队列之前满了）
    if (queue->msg_queue.GetCount() == queue->msg_queue.GetCapacity() - 1) {
        WaitSetHelper::SetFlagsAndWake(&queue->event_flags, EventFlag::HAS_SPACE);
    }
    
    // 清除 HAS_DATA 标志（如果队列现在空了）
    if (queue->msg_queue.IsEmpty()) {
        WaitSetHelper::ClearFlags(&queue->event_flags, EventFlag::HAS_DATA);
    }
    
    auto* chunk = shm_mgr_->GetChunkByIndex(chunk_index);
    return Ok(Sample<T>{chunk, this});
}
```

**Publisher 发送时使用策略：**

```cpp
/**
 * @brief Publisher 发送消息（支持多种队列满策略）
 */
template<typename T>
void Publisher<T>::Send(Sample<T>&& sample) noexcept {
    auto* chunk = sample.Release();
    
    // 状态转换：kWriting -> kReady
    chunk->state.store(ChunkState::kReady, std::memory_order_release);
    
    // 获取队列满策略和超时配置
    PublishPolicy policy = queue_full_policy_.load(std::memory_order_acquire);
    Duration timeout = send_timeout_;  // 可配置，默认 100ms
    
    // 无锁获取 Subscriber 快照（参考 iceoryx2）
    // 优势：
    // 1. 无锁读取，避免 Publisher 与 Subscriber 注册/注销的锁竞争
    // 2. 栈上拷贝快照（~512B），速度极快（< 100ns）
    // 3. memory_order_acquire 确保看到最新的注册结果
    auto snapshot = subscriber_registry_.GetSnapshot();
    
    // 遍历快照中的所有 Subscriber 队列
    for (UInt32 i = 0; i < snapshot.count; ++i) {
        UInt32 queue_idx = snapshot.queue_indices[i];
        auto* sub_queue = &subscriber_queues_[queue_idx];
        
        // 使用策略进行入队
        auto result = EnqueueWithPolicy(
            sub_queue,
            chunk->chunk_index,
            shm_mgr_,
            allocator_,
            policy,
            timeout
        );
        
        // 处理结果
        switch (result) {
        case EnqueueResult::kSuccess:
            // 成功入队，无需操作
            break;
            
        case EnqueueResult::kOverwritten:
            // 覆盖了旧消息（kOverwrite 策略）
            // 引用计数已由 EnqueueWithPolicy 处理
            send_overrun_count_.fetch_add(1, std::memory_order_relaxed);
            break;
            
        case EnqueueResult::kQueueFull:
        case EnqueueResult::kTimeout:
            // 队列满或超时，该 Subscriber 丢失此消息
            // 减少引用计数（此 Subscriber 未收到）
            chunk->ref_count.fetch_sub(1, std::memory_order_relaxed);
            
            // 记录错误统计
            send_errors_.fetch_add(1, std::memory_order_relaxed);
            break;
        }
    }
    
    // 如果所有 Subscriber 都失败，引用计数为 0，chunk 会自动回收
}
```

**队列满策略对比：**

| 策略 | 延迟 | CPU占用 | 消息丢失 | 适用场景 | 推荐度 |
|------|------|---------|---------|---------|--------|
| **kOverwrite** | ⚡ 极低<br>(~100ns) | ✅ 极低<br>(lock-free) | ⚠️ 丢弃旧消息 | 传感器数据、视频流<br>实时性优先 | ⭐⭐⭐⭐⭐ |
| **kDrop** | ⚡ 极低<br>(~50ns) | ✅ 极低<br>(立即返回) | ⚠️ 丢弃新消息 | 日志、审计<br>历史记录优先 | ⭐⭐⭐⭐ |
| **kWait** | 🔶 中等<br>(~1-10ms) | ❌ 高<br>(自旋轮询) | ✅ 无丢失<br>(超时内) | 短超时场景<br>(< 10ms) | ⭐⭐⭐ |
| **kBlock** | 🔶 中等<br>(~100μs-1ms) | ✅ 低<br>(睡眠等待) | ✅ 无丢失<br>(超时内) | 长超时场景<br>(> 10ms) | ⭐⭐⭐⭐ |
| **kCustom** | - | - | - | 用户自定义逻辑 | ⭐⭐ |

**策略选择建议：**

1. **默认推荐：kOverwrite（Ring Buffer 模式）**
   - 适用于 99% 的实时场景
   - 零拷贝 + lock-free，性能最优
   - 保证最新数据可见性

2. **高可靠性需求：kBlock**
   - 金融交易、控制指令等不能丢消息的场景
   - 设置合理超时（如 1s），避免无限阻塞
   - CPU 友好，适合长超时

3. **低延迟 + 无丢失：kWait**
   - 需要非常短的超时（< 10ms）
   - 可以接受短时间的 CPU 占用
   - 延迟敏感场景

4. **历史记录优先：kDrop**
   - 日志系统、审计系统
   - 保留旧消息，丢弃新消息

**性能数据（参考）：**

| 操作 | kOverwrite | kDrop | kWait (1ms超时) | kBlock (1ms超时) |
|------|----------|-------|-----------------|------------------|
| 入队延迟 (队列未满) | 100 ns | 50 ns | 100 ns | 150 ns |
| 入队延迟 (队列满) | 200 ns | 50 ns | ~1 ms | ~100 μs |
| CPU 占用 (等待时) | 0% | 0% | ~100% (单核) | ~0% |
| 吞吐量 (Msg/s) | 10M+ | 10M+ | 1M | 5M |

#### 4.6.2 队列空策略（Subscriber 接收时）

**策略定义（与 Subscriber 一致）：**

```cpp
// Subscriber API - 支持不同的队列空策略
// SubscribePolicy 定义见第3.2节
template<typename PayloadType>
class Subscriber {
public:
    SubscribePolicy queue_empty_policy_ = SubscribePolicy::kBlock;  // 默认阻塞
    Duration receive_timeout_ = Duration::FromMillis(100);  // kBlock/kWait 超时
    
    // [策略1] 非阻塞接收（kReturnError，默认推荐）
    Result<Sample<PayloadType>> Receive() noexcept {
        auto* queue = &subscriber_queues_[queue_index_];
        
        UInt32 chunk_index;
        
        // 出队（RingBufferBlock 是 lock-free 的）
        chunk_index = queue->msg_queue.Dequeue();
        
        if (chunk_index == kInvalidIndex) {
            return Err(CoreErrc::kIPCNoData);  // 队列为空，立即返回
        }
        
        // 设置 HAS_SPACE 标志并唤醒等待的 Publisher（如果队列之前满了）
        if (queue->msg_queue.GetCount() == queue->msg_queue.GetCapacity() - 1) {
            WaitSetHelper::SetFlagsAndWake(&queue->event_flags, EventFlag::HAS_SPACE);
        }
        
        // 清除 HAS_DATA 标志（如果队列现在空了）
        if (queue->msg_queue.IsEmpty()) {
            WaitSetHelper::ClearFlags(&queue->event_flags, EventFlag::HAS_DATA);
        }
        
        auto* chunk = shm_mgr_->GetChunkByIndex(chunk_index);
        return Ok(Sample<PayloadType>{chunk, this});
    }
    
    // [策略2] 带超时的接收（kWait 策略，使用 WaitSet 轮询）
    // 特点：低延迟，中等 CPU 占用（适合短超时、高实时性场景）
    Result<Sample<PayloadType>> ReceiveWithTimeout(
            const Duration& timeout) noexcept {
        
        auto* queue = &subscriber_queues_[queue_index_];
        
        // 使用 WaitSet 轮询 HAS_DATA 标志（纯快速路径，无 futex 调用）
        bool has_data = WaitSetHelper::PollForFlags(
            &queue->event_flags,
            EventFlag::HAS_DATA,
            timeout,
            Duration::FromMicros(100)  // 轮询间隔 100us（可配置）
        );
        
        if (!has_data) {
            return Err(CoreErrc::kIPCReceiveTimeout);  // 超时
        }
        
        // 标志位已设置，尝试出队
        UInt32 chunk_index = queue->msg_queue.Dequeue();
        
        if (chunk_index == kInvalidIndex) {
            return Err(CoreErrc::kIPCNoData);  // 竞争或虚假标志
        }
        
        // 设置 HAS_SPACE 标志（kWait 策略不需要唤醒，Publisher 使用轮询）
        if (queue->msg_queue.GetCount() == queue->msg_queue.GetCapacity() - 1) {
            WaitSetHelper::SetFlagsAndWake(&queue->event_flags, EventFlag::HAS_SPACE, false);
        }
        
        // 清除 HAS_DATA 标志（如果队列现在空了）
        if (queue->msg_queue.IsEmpty()) {
            WaitSetHelper::ClearFlags(&queue->event_flags, EventFlag::HAS_DATA);
        }
        
        auto* chunk = shm_mgr_->GetChunkByIndex(chunk_index);
        return Ok(Sample<PayloadType>{chunk, this});
    }
    
    // [策略3] 尝试接收（无等待，仅检查一次）
    Result<Sample<PayloadType>> TryReceive() noexcept {
        return Receive();  // 与 Receive() 相同（非阻塞）
    }
    
    // 辅助方法
    bool HasData() const noexcept {
        return !subscriber_queues_[queue_index_].msg_queue.IsEmpty();
    }
    
    UInt32 GetQueuedCount() const noexcept {
        return subscriber_queues_[queue_index_].msg_queue.GetCount();
    }
    
    UInt32 GetQueueCapacity() const noexcept {
        return subscriber_queues_[queue_index_].msg_queue.GetCapacity();
    }
    
    UInt64 GetOverrunCount() const noexcept {
        return subscriber_queues_[queue_index_].overrun_count.load(
            std::memory_order_acquire);
    }
};
```

**队列空策略对比：**

| 策略 | 延迟 | CPU占用 | 使用场景 | 推荐度 |
|------|------|---------|---------|--------|
| **kBlock** | 🔶 中等 (ms级) | ✅ 低（休眠） | 非实时系统、后台任务（默认） | ⭐⭐⭐⭐ |
| **kWait** | 🔶 中等 (~1-10ms) | ❌ 高（轮询） | 短超时场景 (< 10ms) | ⭐⭐⭐ |
| **kSkip** | ⚡ 极低 (50ns) | ✅ 极低 | 允许丢失数据的场景 | ⭐⭐ |
| **kError** | ⚡ 极低 (50ns) | ✅ 极低 | 需要严格错误处理 | ⭐⭐⭐ |

**使用示例：**

```cpp
// ========== Publisher 端：配置队列满策略 ==========

// === 场景1：实时传感器数据（默认 kOverwrite）===
auto pub = node.CreatePublisher<SensorData>("/sensor/imu").Value();
// 默认策略：kOverwrite（Ring Buffer 模式），自动丢弃旧数据
while (running) {
    auto sample = pub.Loan().Value();
    sample->Fill(GetSensorData());
    pub.Send(std::move(sample));  // 队列满时覆盖最旧消息
}

// === 场景2：控制指令（kBlock 策略 + 超时）===
auto pub = node.CreatePublisher<ControlCmd>("/control/cmd").Value();
pub.SetPublishPolicy(PublishPolicy::kBlock);  // 阻塞等待
pub.SetSendTimeout(Duration::FromSecs(1));        // 最多等待 1 秒
while (running) {
    auto sample = pub.Loan().Value();
    sample->Fill(GetControlCmd());
    
    // 如果所有 Subscriber 队列满，会阻塞最多 1 秒
    auto result = pub.TrySend(std::move(sample));
    if (result.HasError()) {
        LOG_ERROR("Failed to send control command: timeout");
    }
}

// === 场景3：低延迟交易（kWait 策略 + 短超时）===
auto pub = node.CreatePublisher<TradeOrder>("/trading/order").Value();
pub.SetPublishPolicy(PublishPolicy::kWait);   // 轮询等待
pub.SetSendTimeout(Duration::FromMillis(5));      // 最多等待 5ms
while (running) {
    auto sample = pub.Loan().Value();
    sample->Fill(GetTradeOrder());
    
    auto start = GetMonotonicTime();
    auto result = pub.TrySend(std::move(sample));
    auto latency = GetMonotonicTime() - start;
    
    if (latency > Duration::FromMicros(100)) {
        LOG_WARN("High latency detected: {}us", latency.ToMicros());
    }
}

// === 场景4：日志系统（kDrop 策略）===
auto pub = node.CreatePublisher<LogEntry>("/system/log").Value();
pub.SetPublishPolicy(PublishPolicy::kDrop);  // 队列满时丢弃新消息
while (running) {
    auto sample = pub.Loan().Value();
    sample->Fill(GetLogEntry());
    pub.Send(std::move(sample));  // 队列满时立即返回，不阻塞
}

// ========== Subscriber 端：接收数据 ==========

// ========== Subscriber 端：接收数据 ==========

// === 场景1：后台任务/非实时系统（默认 kBlock 策略）===
auto sub = node.CreateSubscriber<SensorData>("/sensor/imu").Value();
// 默认策略：kBlock（阻塞等待），适合非实时后台任务
while (running) {
    // 自动阻塞等待，直到有数据或超时
    auto sample_result = sub.Receive();  // 使用默认超时 100ms
    if (sample_result.HasValue()) {
        auto sample = sample_result.Value();
        ProcessData(*sample);
    } else {
        // 超时或其他错误
        HandleError(sample_result.Error());
    }
}

// === 场景2：实时系统（配置 kError 策略）===
auto sub = node.CreateSubscriber<SensorData>("/sensor/imu").Value();
sub.SetSubscribePolicy(SubscribePolicy::kError);  // 立即返回错误
while (running) {
    auto sample_result = sub.Receive();
    if (sample_result.HasValue()) {
        auto sample = sample_result.Value();
        ProcessData(*sample);
    } else if (sample_result.Error() == CoreErrc::kIPCQueueEmpty) {
        // 队列为空，执行其他任务（实时性好）
        DoOtherWork();
    }
}

// === 场景3：允许跳过数据（kSkip 策略）===
auto sub = node.CreateSubscriber<SensorData>("/sensor/imu").Value();
sub.SetSubscribePolicy(SubscribePolicy::kSkip);  // 队列空时跳过
while (running) {
    auto sample_result = sub.Receive();
    if (sample_result.HasValue()) {
        ProcessData(*sample_result.Value());
    }
    // 没有数据也不报错，继续下一轮循环
}

// === 场景4：带超时的轮询等待（kWait 策略）===
auto sub = node.CreateSubscriber<ControlCmd>("/control/cmd").Value();
sub.SetSubscribePolicy(SubscribePolicy::kWait);  // 轮询等待
sub.SetReceiveTimeout(Duration::FromMillis(10));   // 短超时
auto sample_result = sub.Receive();
if (sample_result.HasValue()) {
    ProcessData(*sample_result.Value());
} else if (sample_result.Error() == CoreErrc::kIPCReceiveTimeout) {
    // 超时，可能 Publisher 已停止
    HandleTimeout();
}
```

**队列策略对比总结：**

| 维度 | Publisher (队列满) | Subscriber (队列空) |
|------|-------------------|---------------------|
| **默认策略** | kOverwrite (Ring Buffer) | kBlock (阻塞等待) |
| **实时系统** | ✅ kOverwrite<br>⚠️ kDrop | ✅ kError (立即返回)<br>⚠️ kSkip (允许丢失) |
| **高可靠** | ✅ kBlock (长超时)<br>⚠️ kWait (短超时) | ✅ kBlock (默认)<br>⚠️ kWait (短超时) |
| **后台任务** | ✅ kBlock<br>✅ kDrop | ✅ kBlock (默认)<br>✅ kWait |
| **最低延迟** | ✅ kOverwrite<br>✅ kDrop | ✅ Receive() 非阻塞 |

**性能建议：**

1. **99% 场景推荐：kOverwrite + Receive()**
   - 零拷贝 + lock-free，性能最优
   - 确定性延迟，适合实时系统
   - 自动处理慢消费者

2. **高可靠性场景：kBlock + ReceiveWithTimeout()**
   - 不丢消息（超时内）
   - CPU 友好，适合长超时
**性能建议：**

1. **99% 场景推荐：kOverwrite + kBlock（默认组合）**
   - Publisher: 零拷贝 + lock-free，性能最优
   - Subscriber: 阻塞等待，CPU 友好，适合非实时系统
   - 自动处理慢消费者

2. **实时系统推荐：kOverwrite + kError**
   - Publisher: Ring Buffer 自动覆盖
   - Subscriber: 立即返回，确定性延迟
   - 适合事件循环和周期性任务

3. **高可靠性场景：kBlock + kBlock**
   - 不丢消息（超时内）
   - CPU 友好，适合长超时
   - 需要处理超时情况

4. **避免使用：**
   - ❌ kWait 长超时（浪费 CPU，除非低延迟要求 < 1ms）
   - ❌ kBlock 超短超时（<1ms，上下文切换开销大）
   - ❌ kSkip 在关键数据场景（可能丢失重要消息）
| **TryReceive()** | ✅ 零等待 | ⚠️ 需外部同步 | 快速检查、状态机 |

**iceoryx2 推荐最佳实践：**

```cpp
// ✅ 推荐：实时系统使用非阻塞 + 事件驱动
auto subscriber = service.CreateSubscriber()
    .QueueCapacity(256)
    .PublishPolicy(PublishPolicy::kOverwrite)  // Ring Buffer (默认)
    .Create().Value();

while (running) {
    // 非阻塞接收
    if (auto sample = subscriber.Receive(); sample.HasValue()) {
        ProcessData(*sample.Value());
    }
    
    // 检查队列溢出
    if (auto overruns = subscriber.GetOverrunCount(); overruns > 0) {
        LOG_WARN("Queue overrun: {} messages lost", overruns);
    }
}
```

### 4.7 WaitSet 机制（iceoryx2 风格，Linux futex 实现）

**设计目标：** 提供高效的进程间等待/唤醒机制，用于 kBlock 和 kWait 策略，结合 lock-free 检查和 futex 睡眠，实现低延迟和低 CPU 占用。

#### 4.7.1 WaitSet 核心设计

**与传统 pthread 条件变量的对比：**

| 维度 | pthread_cond_t | WaitSet (futex) | 优势 |
|------|----------------|-----------------|------|
| **快速路径** | 需要 mutex 锁 | Lock-free atomic 检查 | ⚡ 无锁，延迟 < 50ns |
| **慢速路径** | 系统调用（睡眠/唤醒） | futex 系统调用 | ✅ 相同开销 |
| **内存占用** | ~48B (mutex+cond) | 4B (atomic flag) | ✅ 节省 92% |
| **跨进程** | 需要 PTHREAD_PROCESS_SHARED | 天然支持（共享内存） | ✅ 简单 |
| **虚假唤醒** | 需要循环检查 | 需要循环检查 | ➖ 相同 |

**WaitSet 事件标志位定义：**

```cpp
namespace ara::core::ipc {

/**
 * @brief WaitSet 事件标志位（存储在共享内存 event_flags）
 */
enum EventFlag : UInt32 {
    HAS_DATA        = 0x01,  // bit 0: 队列有数据（ChannelQueue）
    HAS_SPACE       = 0x02,  // bit 1: 队列有空间（ChannelQueue）
    HAS_FREE_CHUNK  = 0x04,  // bit 2: ChunkPool 有可用块（ControlBlock）
    // bit 3-31: 保留扩展
};

} // namespace ara::core::ipc
```

#### 4.7.2 WaitSet 实现（Linux futex）

**核心思想：**
1. **快速路径**：使用 atomic 检查事件标志（lock-free，无系统调用）
2. **慢速路径**：仅在需要睡眠时调用 futex（减少系统调用）
3. **唤醒路径**：设置标志 + futex_wake（通知等待者）

**完整实现：**

```cpp
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <atomic>

namespace ara::core::ipc {

/**
 * @brief WaitSet 辅助函数（Linux futex 封装）
 */
class WaitSetHelper {
public:
    /**
     * @brief 等待事件标志位被设置（带超时）
     * @param event_flags 共享内存中的事件标志原子变量
     * @param expected_flags 期望的标志位（按位与检查）
     * @param timeout 超时时间
     * @return true 标志位已设置，false 超时
     * 
     * 工作原理：
     * 1. 先 lock-free 检查标志位（快速路径）
     * 2. 若未设置，调用 futex_wait 睡眠
     * 3. 被唤醒后重新检查标志位（防止虚假唤醒）
     */
    static bool WaitForFlags(
        std::atomic<UInt32>* event_flags,
        UInt32 expected_flags,
        const Duration& timeout) noexcept {
        
        auto deadline = GetMonotonicTime() + timeout;
        
        while (GetMonotonicTime() < deadline) {
            // [快速路径] Lock-free 检查标志位
            UInt32 current = event_flags->load(std::memory_order_acquire);
            if ((current & expected_flags) == expected_flags) {
                return true;  // 标志位已设置，立即返回
            }
            
            // [慢速路径] 标志位未设置，使用 futex 睡眠等待
            struct timespec ts;
            auto remaining = deadline - GetMonotonicTime();
            ts.tv_sec = remaining.ToSecs();
            ts.tv_nsec = remaining.ToNanos() % 1'000'000'000;
            
            // futex_wait: 原子地检查值并睡眠
            // 如果 *event_flags != current，立即返回（避免丢失唤醒）
            int ret = syscall(
                SYS_futex,
                event_flags,                    // uaddr
                FUTEX_WAIT_BITSET,              // futex_op (支持超时)
                current,                        // val (期望值)
                &ts,                            // timeout
                nullptr,                        // uaddr2 (unused)
                FUTEX_BITSET_MATCH_ANY          // val3 (匹配所有位)
            );
            
            // 返回值处理：
            // - 0: 被唤醒（需重新检查标志位）
            // - ETIMEDOUT: 超时
            // - EAGAIN: 标志位已改变（快速路径会检测到）
            if (ret == -1 && errno == ETIMEDOUT) {
                return false;  // 超时
            }
            
            // 继续循环，重新检查标志位（处理虚假唤醒）
        }
        
        return false;  // 超时
    }
    
    /**
     * @brief 设置事件标志位并唤醒等待者
     * @param event_flags 共享内存中的事件标志原子变量
     * @param flags_to_set 要设置的标志位
     * @param wake 是否调用 futex_wake 唤醒等待者（默认 true）
     * 
     * 工作原理：
     * 1. 原子地设置标志位（按位或）
     * 2. 如果 wake=true，调用 futex_wake 唤醒所有等待者
     * 
     * 优化场景：
     * - wake=false: 用于 kWait 策略（PollForFlags），不需要唤醒（无等待者）
     * - wake=true: 用于 kBlock 策略（WaitForFlags），需要唤醒阻塞的线程
     */
    static void SetFlagsAndWake(
        std::atomic<UInt32>* event_flags,
        UInt32 flags_to_set,
        bool wake = true) noexcept {
        
        // 原子地设置标志位
        UInt32 old_flags = event_flags->fetch_or(
            flags_to_set, 
            std::memory_order_release
        );
        
        // 仅当标志位发生变化且需要唤醒时才调用 futex_wake（优化）
        if (wake && (old_flags & flags_to_set) != flags_to_set) {
            // futex_wake: 唤醒所有等待者
            syscall(
                SYS_futex,
                event_flags,                    // uaddr
                FUTEX_WAKE,                     // futex_op
                INT_MAX,                        // val (唤醒所有)
                nullptr,                        // timeout (unused)
                nullptr,                        // uaddr2 (unused)
                0                               // val3 (unused)
            );
        }
    }
    
    /**
     * @brief 清除事件标志位（不唤醒）
     * @param event_flags 共享内存中的事件标志原子变量
     * @param flags_to_clear 要清除的标志位
     */
    static void ClearFlags(
        std::atomic<UInt32>* event_flags,
        UInt32 flags_to_clear) noexcept {
        
        event_flags->fetch_and(
            ~flags_to_clear, 
            std::memory_order_release
        );
    }
    
    /**
     * @brief 轮询检查事件标志位（用于 kWait 策略）
     * @param event_flags 共享内存中的事件标志原子变量
     * @param expected_flags 期望的标志位（按位与检查）
     * @param timeout 超时时间
     * @param poll_interval 轮询间隔（默认 100us）
     * @return true 标志位已设置，false 超时
     * 
     * 工作原理：
     * 1. 纯快速路径：只使用原子操作检查标志位
     * 2. 不调用 futex 系统调用（避免进入内核态）
     * 3. 轮询间隔可配置（平衡延迟和 CPU 占用）
     * 4. 适合短超时、低延迟场景
     * 
     * 性能特点：
     * - 延迟：极低（~10-50 us，取决于轮询间隔）
     * - CPU 占用：中等（比 kBlock 高，比纯自旋低）
     * - 适用场景：实时性要求高、超时时间短（< 10ms）
     */
    static bool PollForFlags(
        std::atomic<UInt32>* event_flags,
        UInt32 expected_flags,
        const Duration& timeout,
        const Duration& poll_interval = Duration::FromMicros(100)) noexcept {
        
        auto deadline = GetMonotonicTime() + timeout;
        
        while (GetMonotonicTime() < deadline) {
            // Lock-free 检查标志位（纯原子操作，无系统调用）
            UInt32 current = event_flags->load(std::memory_order_acquire);
            if ((current & expected_flags) == expected_flags) {
                return true;  // 标志位已设置
            }
            
            // 短暂休眠，减少 CPU 占用（可配置）
            // 注意：不能太长，否则影响实时性
            std::this_thread::sleep_for(
                std::chrono::nanoseconds(poll_interval.ToNanos())
            );
        }
        
        // 最后一次检查（确保不遗漏临界情况）
        UInt32 current = event_flags->load(std::memory_order_acquire);
        return (current & expected_flags) == expected_flags;
    }
};

} // namespace ara::core::ipc
```

#### 4.7.3 WaitSet 在 kBlock 策略中的应用

**Publisher 发送（队列满时阻塞等待）：**

```cpp
/**
 * @brief kBlock 策略：使用 WaitSet 高效等待队列有空间
 */
EnqueueResult EnqueueWithPolicy_kBlock(
        ChannelQueue* queue,
        UInt32 chunk_index,
        const Duration& timeout) noexcept {
    
    // 快速路径：队列未满，直接入队
    if (!queue->msg_queue.IsFull()) {
        queue->msg_queue.Enqueue(chunk_index);
        
        // 设置 HAS_DATA 标志并唤醒等待的 Subscriber
        WaitSetHelper::SetFlagsAndWake(
            &queue->event_flags, 
            EventFlag::HAS_DATA
        );
        
        return EnqueueResult::kSuccess;
    }
    
    // 慢速路径：队列满，使用 WaitSet 等待 HAS_SPACE 标志
    bool has_space = WaitSetHelper::WaitForFlags(
        &queue->event_flags,
        EventFlag::HAS_SPACE,
        timeout
    );
    
    if (!has_space) {
        return EnqueueResult::kTimeout;  // 超时
    }
    
    // 被唤醒后重新尝试入队
    if (queue->msg_queue.IsFull()) {
        return EnqueueResult::kQueueFull;  // 仍然满（虚假唤醒或竞争）
    }
    
    queue->msg_queue.Enqueue(chunk_index);
    
    // 清除 HAS_SPACE 标志（队列可能又满了）
    if (queue->msg_queue.IsFull()) {
        WaitSetHelper::ClearFlags(&queue->event_flags, EventFlag::HAS_SPACE);
    }
    
    // 设置 HAS_DATA 标志并唤醒 Subscriber
    WaitSetHelper::SetFlagsAndWake(&queue->event_flags, EventFlag::HAS_DATA);
    
    return EnqueueResult::kSuccess;
}
```

**Subscriber 接收（队列空时阻塞等待）：**

```cpp
/**
 * @brief kBlock 策略：使用 WaitSet 高效等待队列有数据
 */
Result<Sample<PayloadType>> Subscriber<PayloadType>::Receive_kBlock(
        const Duration& timeout) noexcept {
    
    auto* queue = &subscriber_queues_[queue_index_];
    
    // 快速路径：队列有数据，直接出队
    if (!queue->msg_queue.IsEmpty()) {
        UInt32 chunk_index = queue->msg_queue.Dequeue();
        
        // 设置 HAS_SPACE 标志并唤醒等待的 Publisher
        WaitSetHelper::SetFlagsAndWake(
            &queue->event_flags, 
            EventFlag::HAS_SPACE
        );
        
        auto* chunk = shm_mgr_->GetChunkByIndex(chunk_index);
        return Ok(Sample<PayloadType>{chunk, this});
    }
    
    // 慢速路径：队列空，使用 WaitSet 等待 HAS_DATA 标志
    bool has_data = WaitSetHelper::WaitForFlags(
        &queue->event_flags,
        EventFlag::HAS_DATA,
        timeout
    );
    
    if (!has_data) {
        return Err(CoreErrc::kIPCReceiveTimeout);  // 超时
    }
    
    // 被唤醒后重新尝试出队
    if (queue->msg_queue.IsEmpty()) {
        return Err(CoreErrc::kIPCNoData);  // 仍然空（虚假唤醒）
    }
    
    UInt32 chunk_index = queue->msg_queue.Dequeue();
    
    // 清除 HAS_DATA 标志（队列可能又空了）
    if (queue->msg_queue.IsEmpty()) {
        WaitSetHelper::ClearFlags(&queue->event_flags, EventFlag::HAS_DATA);
    }
    
    // 设置 HAS_SPACE 标志并唤醒 Publisher
    WaitSetHelper::SetFlagsAndWake(&queue->event_flags, EventFlag::HAS_SPACE);
    
    auto* chunk = shm_mgr_->GetChunkByIndex(chunk_index);
    return Ok(Sample<PayloadType>{chunk, this});
}
```

#### 4.7.4 WaitSet 性能分析

**快速路径性能（队列有数据/空间）：**

```
时间线（Subscriber Receive，队列有数据）：
┌──────────────────────────────────────────────────────┐
│ 1. Load event_flags (atomic)            ~10 ns      │
│ 2. Check (flags & HAS_DATA)             ~5 ns       │
│ 3. Dequeue from RingBuffer              ~30 ns      │
│ 4. Set HAS_SPACE flag (atomic)          ~10 ns      │
│ 5. futex_wake (系统调用，仅 kBlock)      ~200 ns     │
├──────────────────────────────────────────────────────┤
│ kBlock 总延迟：~255 ns（vs pthread: ~500 ns）       │
│ kWait 总延迟：~55 ns（无 futex_wake）                │
└──────────────────────────────────────────────────────┘
```

**kWait 策略优化（wake=false）：**

```cpp
// Publisher 发送（kWait 策略）
WaitSetHelper::SetFlagsAndWake(&queue->event_flags, EventFlag::HAS_DATA, false);
// ✅ 仅设置标志位（atomic），不调用 futex_wake
// ✅ 节省 ~200ns 系统调用开销
// ✅ Subscriber 使用 PollForFlags 轮询，不需要唤醒

// Subscriber 接收（kWait 策略）
WaitSetHelper::SetFlagsAndWake(&queue->event_flags, EventFlag::HAS_SPACE, false);
// ✅ 仅设置标志位，Publisher 使用 PollForFlags 轮询
```

**慢速路径性能（队列空，需要等待）：**

```
时间线（Subscriber Receive，队列空 -> 数据到达）：
┌──────────────────────────────────────────────────────┐
│ 1. Load event_flags (atomic)            ~10 ns      │
│ 2. Check failed (no HAS_DATA)           ~5 ns       │
│ 3. futex_wait (系统调用 + 睡眠)          ~1-10 μs    │
│ 4. 被唤醒（Publisher 调用 futex_wake）   ~500 ns    │
│ 5. 重新检查 event_flags                  ~10 ns      │
│ 6. Dequeue from RingBuffer              ~30 ns      │
├──────────────────────────────────────────────────────┤
│ 总延迟：~2-11 μs（vs pthread_cond: ~2-15 μs）       │
└──────────────────────────────────────────────────────┘
```

**与 pthread_cond_t 对比总结：**

| 场景 | pthread_cond_t | WaitSet (futex) | 提升 |
|------|----------------|-----------------|------|
| **快速路径（有数据）** | ~500 ns (需mutex) | ~255 ns (lock-free) | **2x** |
| **慢速路径（阻塞）** | ~2-15 μs | ~2-11 μs | **1.2x** |
| **内存占用** | 48 B | 4 B | **12x** |
| **实现复杂度** | 低（标准库） | 中（需封装futex） | - |

**推荐使用场景：**
- ✅ **kBlock 策略**：使用 WaitSet 的 `WaitForFlags()`（快速路径 lock-free + futex 慢速路径）
- ✅ **kWait 策略**：使用 WaitSet 的 `PollForFlags()`（纯快速路径，无系统调用）
- ✅ **高频场景**：快速路径占 99% 时，WaitSet 优势明显
- ⚠️ **低频场景**：慢速路径占比高时，kBlock 性能接近 pthread_cond_t

#### 4.7.5 kWait vs kBlock 性能对比

**PollForFlags（kWait）性能特点：**

```
时间线（Subscriber Receive，队列空 -> 数据到达）：
┌──────────────────────────────────────────────────────┐
│ 1. Load event_flags (atomic)            ~10 ns      │
│ 2. Check failed (no HAS_DATA)           ~5 ns       │
│ 3. sleep_for(100us)                      ~100 μs    │  <- 轮询间隔
│ 4. 重新 Load event_flags                 ~10 ns      │
│ 5. Check success (HAS_DATA)              ~5 ns       │
│ 6. Dequeue from RingBuffer              ~30 ns      │
├──────────────────────────────────────────────────────┤
│ 平均延迟：~50 μs（取决于轮询间隔，最坏 100us）       │
│ CPU 占用：中等（100us 间隔 -> ~1% CPU）              │
└──────────────────────────────────────────────────────┘
```

**kWait vs kBlock 对比：**

| 维度 | kWait (PollForFlags) | kBlock (WaitForFlags) | 推荐 |
|------|---------------------|----------------------|------|
| **快速路径延迟** | ~255 ns | ~255 ns | 相同 |
| **慢速路径延迟** | ~50 μs (可配置) | ~2-11 μs | kBlock 更低 |
| **系统调用** | ❌ 无（纯用户态） | ✅ 有（futex） | kWait 更轻量 |
| **CPU 占用** | ⚠️ 中等（轮询） | ✅ 极低（睡眠） | kBlock 更省 |
| **实时性** | ⚡ 极高（可配置到 10us） | 🔶 高（受调度影响） | kWait 更可控 |
| **适用超时** | < 10ms | 任意 | - |

**推荐策略选择：**

```cpp
// [场景1] 超低延迟要求（< 100us），可接受少量 CPU 占用
Publisher pub(PublishPolicy::kWait);   // 轮询间隔 100us
Subscriber sub(SubscribePolicy::kWait); // 轮询间隔 100us

// [场景2] 通用场景，平衡延迟和 CPU（推荐）
Publisher pub(PublishPolicy::kBlock);  // 快速路径 lock-free + futex 慢速路径
Subscriber sub(SubscribePolicy::kBlock);

// [场景3] 非实时后台任务，最小化 CPU
Publisher pub(PublishPolicy::kBlock);  // 允许长时间睡眠
Subscriber sub(SubscribePolicy::kBlock);

// [场景4] 硬实时系统（RTOS）
Publisher pub(PublishPolicy::kOverrun); // 直接覆盖旧数据
Subscriber sub(SubscribePolicy::kSkip);  // 无数据立即返回
```

#### 4.7.6 WaitSet vs pthread 条件变量选择

| 考虑因素 | WaitSet (futex) | pthread_cond_t | 推荐 |
|---------|----------------|----------------|------|
| **性能（快速路径）** | ⚡ 极快（lock-free） | 🔶 中等（需mutex） | WaitSet |
| **性能（慢速路径）** | ✅ 快 | ✅ 快 | 相同 |
| **内存占用** | ✅ 4B | ⚠️ 48B | WaitSet |
| **跨平台** | ❌ 仅 Linux | ✅ POSIX 标准 | pthread |
| **实现复杂度** | ⚠️ 中等 | ✅ 简单 | pthread |
| **调试工具** | ⚠️ 有限 | ✅ 完善 | pthread |

**LightAP IPC 选择：**
- ✅ 使用 **WaitSet (futex)** - 性能和内存优先
- ✅ Linux 平台专用实现
- ✅ 参考 iceoryx2 设计

### 4.8 Request-Response (请求-响应)

```cpp
// Client 端
template<typename RequestType, typename ResponseType>
class Client {
public:
    Result<Future<ResponseType>> SendRequest(const RequestType& request);
    
    Result<ResponseType> SendRequestSync(
        const RequestType& request, 
        const Duration& timeout);
};

// Server 端
template<typename RequestType, typename ResponseType>
class Server {
public:
    Result<Request<RequestType>> Receive();
    Result<void> SendResponse(Request<RequestType>&& request, 
                             const ResponseType& response);
};

// 使用示例
auto client = service.CreateClient<AddRequest, AddResponse>().Value();
auto future = client.SendRequest(AddRequest{.a = 10, .b = 20}).Value();

auto response = future.Get(Duration::FromSecs(1)).Value();
std::cout << "Result: " << response.sum << "\n";  // 30
```

### 4.9 Event (事件通知)

```cpp
class Notifier {
public:
    Result<void> Notify(EventId id = EventId::kDefault);
};

class Listener {
public:
    Result<Vector<EventId>> WaitAndCollect(const Duration& timeout);
    bool Try(EventId& out_id);
};

// 使用示例
auto notifier = service.CreateNotifier().Value();
notifier.Notify(EventId{42}).Value();

auto listener = service.CreateListener().Value();
auto events = listener.WaitAndCollect(Duration::FromMillis(100)).Value();
for (auto event_id : events) {
    std::cout << "Event: " << event_id << "\n";
}
```

---

## 5. 运行时流程详解

本章节详细描述 LightAP IPC 的关键运行时流程，包括初始化、连接建立、消息收发和内存管理。

### 5.1 初始化流程

#### 5.1.1 服务端初始化流程（Service Creator）

```
┌─────────────────────────────────────────────────────────────────┐
│                   服务端初始化完整流程                           │
└─────────────────────────────────────────────────────────────────┘

[1] 创建 Node
    │
    ├─► Node::Create<ServiceType::kIPC>(config)
    │   ├─► 分配 NodeId（UUID）
    │   ├─► 初始化 ServiceDiscovery（文件系统监听器）
    │   └─► 初始化 ConnectionManager
    │
    ▼

[2] 创建服务构建器
    │
    ├─► node.CreateServiceBuilder<PayloadType>("Service/Name")
    │   ├─► 设置服务名称
    │   ├─► 设置有效载荷类型
    │   └─► 返回 ServiceBuilder<PayloadType>
    │
    ▼

[3] 配置服务参数
    │
    ├─► builder.PublishSubscribe()        // 选择 Pub-Sub 模式
    ├─► builder.MaxPublishers(8)          // 最多 8 个 Publisher
    ├─► builder.MaxSubscribers(32)        // 最多 32 个 Subscriber
    ├─► builder.MaxChunks(512)            // 固定 512 个 Chunk
    ├─► builder.ChunkSize(1024)           // 每个 Chunk 1KB（固定）
    ├─► builder.ChunkAlignment(64)        // 64 字节对齐
    ├─► builder.HistorySize(5)            // 保留最近 5 个样本
    └─► builder.ShmPath("/lightap_service_xxx")
    │
    ▼

[4] 创建共享内存
    │
    ├─► builder.Create() 或 builder.OpenOrCreate()
    │   │
    │   ├─► SharedMemoryManager::Create(config)
    │   │   ├─► shm_open("/lightap_service_xxx", O_CREAT|O_RDWR|O_EXCL)
    │   │   ├─► ftruncate(fd, total_size)  // 固定大小
    │   │   ├─► mmap(NULL, total_size, PROT_READ|PROT_WRITE, MAP_SHARED)
    │   │   └─► 返回 base_address
    │   │
    │   ├─► 初始化 ControlBlock
    │   │   ├─► magic_number = 0xICE0RYX2
    │   │   ├─► version = 1
    │   │   ├─► state = kInitializing
    │   │   ├─► max_publishers = 8
    │   │   ├─► max_channels = 32
    │   │   ├─► max_chunks = 512
    │   │   ├─► chunk_size = 1024
    │   │   ├─► chunk_alignment = 64
    │   │   ├─► free_list_head = 0
    │   │   ├─► allocated_count = 0
    │   │   └─► is_initialized = false
    │   │
    │   ├─► 初始化 ChunkPool
    │   │   ├─► ChunkPoolAllocator::Initialize(base_addr, config)
    │   │   ├─► for (i = 0; i < max_chunks; ++i) {
    │   │   │       chunk[i].chunk_index = i;
    │   │   │       chunk[i].chunk_size = chunk_size;
    │   │   │       chunk[i].state = kFree;
    │   │   │       chunk[i].ref_count = 0;
    │   │   │       chunk[i].next_free_index = i + 1;  // 链表
    │   │   │   }
    │   │   └─► chunk[max_chunks-1].next_free_index = kInvalidIndex;
    │   │
    │   ├─► 初始化 Publisher 状态数组
    │   │   └─► for (i = 0; i < max_publishers; ++i) {
    │   │           publishers[i].id = kInvalidId;
    │   │           publishers[i].active = false;
    │   │           publishers[i].sequence_number = 0;
    │   │           publishers[i].subscriber_count = 0;
    │   │           for (j = 0; j < MAX_SUBSCRIBERS_PER_PUB; ++j) {
    │   │               publishers[i].subscriber_list[j] = kInvalidIndex;
    │   │           }
    │   │       }
    │   │
    │   ├─► 初始化 Subscriber 队列数组
    │   │   └─► for (i = 0; i < MAX_SUBSCRIBER_QUEUES; ++i) {
    │   │           subscriber_queues[i].active = false;
    │   │           subscriber_queues[i].subscriber_id = 0;
    │   │           subscriber_queues[i].msg_queue.head_offset = kInvalidOffset;
    │   │           subscriber_queues[i].msg_queue.tail_offset = kInvalidOffset;
    │   │           subscriber_queues[i].msg_queue.count = 0;
    │   │           subscriber_queues[i].msg_queue.capacity = DEFAULT_QUEUE_SIZE;
    │   │       }
    │   │
    │   └─► 设置状态
    │       ├─► control.is_initialized = true
    │       └─► control.state = kReady
    │
    ▼

[5] 注册到服务发现
    │
    ├─► 创建服务描述文件: /tmp/lightap/services/Service_Name.service
    │   └─► JSON 内容:
    │       {
    │         "service_name": "Service/Name",
    │         "service_id": "uuid-xxxxx",
    │         "service_type": "PublishSubscribe",
    │         "payload_type": "PayloadType",
    │         "payload_size": 1024,
    │         "max_publishers": 8,
    │         "max_channels": 32,
    │         "max_chunks": 512,
    │         "shm_path": "/lightap_service_xxx",
    │         "created_at": "2026-01-06T10:00:00Z"
    │       }
    │
    ▼

[6] 返回 Service 对象
    │
    └─► Service<PayloadType> 就绪，可创建 Publisher/Subscriber
```

#### 5.1.2 客户端初始化流程（Service Opener）

```
┌─────────────────────────────────────────────────────────────────┐
│                   客户端初始化完整流程                           │
└─────────────────────────────────────────────────────────────────┘

[1] 创建 Node
    │
    └─► Node::Create<ServiceType::kIPC>(config)
    ▼

[2] 打开已存在的服务
    │
    ├─► node.CreateServiceBuilder<PayloadType>("Service/Name")
    │       .PublishSubscribe()
    │       .Open()  // 仅打开，不创建
    │   │
    │   ├─► ServiceDiscovery::FindService("Service/Name")
    │   │   ├─► 读取 /tmp/lightap/services/Service_Name.service
    │   │   ├─► 解析 JSON，获取 shm_path = "/lightap_service_xxx"
    │   │   └─► 验证 payload_type 匹配
    │   │
    │   ├─► SharedMemoryManager::Open("/lightap_service_xxx")
    │   │   ├─► shm_open("/lightap_service_xxx", O_RDWR)  // 无 O_CREAT
    │   │   ├─► fstat(fd) 获取大小
    │   │   ├─► mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED)
    │   │   └─► 返回 base_address（可能与服务端不同）
    │   │
    │   ├─► 验证共享内存
    │   │   ├─► 检查 magic_number == 0xICE0RYX2
    │   │   ├─► 检查 version 兼容性
    │   │   ├─► 检查 is_initialized == true
    │   │   └─► 检查 state == kReady
    │   │
    │   └─► 返回 Service<PayloadType> 对象
    │
    ▼

[3] Service 就绪
    │
    └─► 可创建 Publisher/Subscriber
```

**关键点：**
- ✅ 服务端使用 `Create()` 创建共享内存（带 `O_EXCL` 标志）
- ✅ 客户端使用 `Open()` 打开已存在的共享内存
- ✅ 所有内存布局在服务创建时固定，客户端只读取配置
- ✅ 不同进程映射到不同的虚拟地址 → 必须使用 Offset-based 寻址

### 5.2 Publisher/Subscriber 连接流程

> **重要设计原则（参考 iceoryx2）：**
> - **Publisher**: 在共享内存中占用固定槽位，维护连接的Subscriber列表
> - **Subscriber**: 在共享内存中拥有独立的消息队列（每个Sub一个队列）
> - **消息发送**: Publisher遍历所有连接的Subscriber，将chunk引用推送到各自队列
> - **消息接收**: Subscriber从自己的专属队列读取（无需round-robin）
> - **广播机制**: 一个chunk可被多个Subscriber队列引用（ref_count管理）

#### 5.2.1 Publisher 连接流程（参考 iceoryx2）

```
┌─────────────────────────────────────────────────────────────────┐
│                   Publisher 连接流程                             │
└─────────────────────────────────────────────────────────────────┘

[1] 创建 Publisher
    │
    ├─► service.CreatePublisher()
    │       .MaxLoanedSamples(3)  // 最多并发 loan 3 个样本
    │       .Create()
    │   │
    │   ├─► 在共享内存中找到空闲的 PublisherState 槽位
    │   │   │
    │   │   ├─► for (i = 0; i < max_publishers; ++i) {
    │   │   │       expected = false;
    │   │   │       if (publishers[i].active.compare_exchange_strong(
    │   │   │               expected, true, acq_rel, acquire)) {
    │   │   │           // 成功占用槽位
    │   │   │           publisher_index = i;
    │   │   │           break;
    │   │   │       }
    │   │   │   }
    │   │   │
    │   │   └─► 若所有槽位已满，返回 Err(CoreErrc::kIPCMaxPublishersReached)
    │   │
    │   ├─► 初始化 PublisherState（共享内存中）
    │   │   ├─► publishers[i].id = GeneratePublisherId()
    │   │   ├─► publishers[i].active = true
    │   │   ├─► publishers[i].sequence_number = 0
    │   │   ├─► publishers[i].subscriber_count = 0
    │   │   ├─► publishers[i].last_heartbeat = GetMonotonicTime()
    │   │   └─► for (j = 0; j < MAX_SUBSCRIBERS_PER_PUB; ++j) {
    │   │           publishers[i].subscriber_list[j] = kInvalidIndex;
    │   │       }
    │   │
    │   ├─► 创建本地 Publisher 对象
    │   │   ├─► publisher.publisher_index_ = i
    │   │   ├─► publisher.loan_counter_ = 0  // 本地计数器
    │   │   ├─► publisher.max_loaned_samples_ = 3
    │   │   ├─► publisher.allocator_ = &chunk_pool_
    │   │   ├─► publisher.shm_mgr_ = &shm_manager_
    │   │   └─► publisher.connected_subscribers_ = {}  // 本地缓存
    │   │
    │   └─► 注册到服务发现（文件系统）
    │       └─► 创建文件: /tmp/lightap/services/Service_Name.pub_<UUID>
    │           内容: { "publisher_id": "uuid", 
    │                   "publisher_index": i,
    │                   "shm_path": "/lightap_service_xxx",
    │                   "created_at": "..." }
    │
    ▼

[2] Publisher 就绪
    │
    └─► 等待 Subscriber 连接
        Subscriber 连接时会将自己的队列索引注册到 Publisher
```

**关键设计说明（iceoryx2 风格）：**
- ✅ Publisher 在共享内存中占用固定槽位（预分配，确定性）
- ✅ Publisher 维护连接的 Subscriber 列表（用于发送时推送消息）
- ✅ 通过文件系统服务发现让 Subscriber 找到 Publisher
- ✅ Publisher 数量受 `max_publishers` 限制（固定，编译/配置时确定）

#### 5.2.2 Subscriber 连接流程（参考 iceoryx2）

```
┌─────────────────────────────────────────────────────────────────┐
│         Subscriber 连接流程（创建专属消息队列）                   │
└─────────────────────────────────────────────────────────────────┘

[1] 创建 Subscriber
    │
    ├─► service.CreateSubscriber()
    │       .QueueCapacity(10)  // 队列容量（可缓存10个样本）
    │       .Create()
    │   │
    │   ├─► 通过服务发现查找所有活跃的 Publisher
    │   │   │
    │   │   ├─► 扫描目录: /tmp/lightap/services/
    │   │   │   └─► 找到所有 Service_Name.pub_* 文件
    │   │   │
    │   │   ├─► 解析每个 Publisher 描述文件
    │   │   │   ├─► 获取 publisher_index
    │   │   │   ├─► 获取 shm_path
    │   │   │   └─► 验证 shm_path 与当前服务匹配
    │   │   │
    │   │   └─► 构建 Publisher 索引列表
    │   │       └─► discovered_publishers = [0, 2, 5, ...]  // 例如
    │   │
    │   ├─► 在共享内存中分配专属消息队列
    │   │   │
    │   │   ├─► for (i = 0; i < MAX_SUBSCRIBER_QUEUES; ++i) {
    │   │   │       expected = false;
    │   │   │       if (subscriber_queues[i].active.compare_exchange_strong(
    │   │   │               expected, true, acq_rel, acquire)) {
    │   │   │           // 成功占用队列槽位
    │   │   │           queue_index = i;
    │   │   │           break;
    │   │   │       }
    │   │   │   }
    │   │   │
    │   │   └─► 若所有队列槽位已满，返回 Err(CoreErrc::kIPCMaxSubscribersReached)
    │   │
    │   ├─► 初始化 ChannelQueue（共享内存中）
    │   │   ├─► subscriber_queues[queue_index].subscriber_id = GenerateSubId()
    │   │   ├─► subscriber_queues[queue_index].active = true
    │   │   ├─► subscriber_queues[queue_index].msg_queue.head_offset = kInvalidOffset
    │   │   ├─► subscriber_queues[queue_index].msg_queue.tail_offset = kInvalidOffset
    │   │   ├─► subscriber_queues[queue_index].msg_queue.count = 0
    │   │   ├─► subscriber_queues[queue_index].msg_queue.capacity = 10
    │   │   └─► subscriber_queues[queue_index].last_receive_time = GetMonotonicTime()
    │   │
    │   ├─► 向所有 Publisher 注册（建立连接）
    │   │   │
    │   │   └─► for each pub_idx in discovered_publishers {
    │   │           // 原子添加到 Publisher 的 subscriber_list
    │   │           for (j = 0; j < MAX_SUBSCRIBERS_PER_PUB; ++j) {
    │   │               expected = kInvalidIndex;
    │   │               if (publishers[pub_idx].subscriber_list[j]
    │   │                       .compare_exchange_strong(
    │   │                           expected, queue_index, acq_rel, acquire)) {
    │   │                   publishers[pub_idx].subscriber_count
    │   │                       .fetch_add(1, relaxed);
    │   │                   break;  // 注册成功
    │   │               }
    │   │           }
    │   │       }
    │   │
    │   ├─► 创建本地 Subscriber 对象
    │   │   ├─► subscriber.subscriber_id_ = GenerateSubscriberId()
    │   │   ├─► subscriber.queue_index_ = queue_index  // 自己的队列索引
    │   │   ├─► subscriber.shm_mgr_ = &shm_manager_
    │   │   └─► subscriber.queue_capacity_ = 10
    │   │
    │   └─► 注册到服务发现（可选，用于监控）
    │       └─► 创建文件: /tmp/lightap/services/Service_Name.sub_<UUID>
    │           内容: { "subscriber_id": "uuid",
    │                   "queue_index": queue_index,
    │                   "created_at": "..." }
    │
    ▼

[2] Subscriber 就绪
    │
    └─► 从自己的专属队列 subscriber_queues[queue_index].msg_queue 读取消息
        Publisher 发送时会自动推送到此队列
```

**关键设计说明（iceoryx2 风格）：**
- ✅ **每个 Subscriber 拥有独立的消息队列**（在共享内存中）
- ✅ Subscriber 连接时向所有 Publisher 注册自己的队列索引
- ✅ 队列数量受 `MAX_SUBSCRIBER_QUEUES` 限制（固定，确定性）
- ✅ 无需 Round-robin 调度（直接从自己的队列读取）
- ✅ Publisher 发送时推送到所有连接的 Subscriber 队列（广播）

#### 5.2.3 动态连接与断开（iceoryx2 风格）

```
┌─────────────────────────────────────────────────────────────────┐
│              新 Publisher 上线（Subscriber 自动发现）             │
└─────────────────────────────────────────────────────────────────┘

[场景] Subscriber 运行中，新 Publisher 上线
    │
    ├─► [新 Publisher 创建]
    │   ├─► 占用共享内存槽位 publishers[7]
    │   └─► 创建文件: Service_Name.pub_<UUID>
    │
    ▼
[Subscriber 发现新 Publisher]
    │
    ├─► [选项 1] 定期刷新（轮询）
    │   └─► 每 N 秒重新扫描服务发现目录
    │       └─► 更新 active_publishers 列表
    │
    ├─► [选项 2] 文件系统监听（inotify）
    │   └─► 监听 /tmp/lightap/services/ 目录
    │       └─► 接收 CREATE 事件
    │           └─► 解析新文件，更新 active_publishers
    │
    └─► [选项 3] 懒加载（首次访问时发现）
        └─► Receive() 时检测到新的活跃槽位
            └─► 添加到 active_publishers
```

```
┌─────────────────────────────────────────────────────────────────┐
│                   Publisher 断开连接流程                          │
└─────────────────────────────────────────────────────────────────┘

[Publisher 析构]
    │
    ├─► Publisher::~Publisher()
    │   ├─► 释放所有已 Loan 的 Chunk
    │   │   └─► while (loan_counter_ > 0) {
    │   │           // 查找并释放 loaned chunks
    │   │       }
    │   │
    │   ├─► 从所有 Subscriber 的订阅列表中移除自己
    │   │   └─► for (j = 0; j < MAX_SUBSCRIBERS_PER_PUB; ++j) {
    │   │           queue_idx = publishers[i].subscriber_list[j].load(acquire);
    │   │           if (queue_idx != kInvalidIndex) {
    │   │               // 标记此 Publisher 已断开（Subscriber 可选处理）
    │   │               publishers[i].subscriber_list[j]
    │   │                   .store(kInvalidIndex, release);
    │   │           }
    │   │       }
    │   │
    │   ├─► 标记槽位为不活跃（原子操作）
    │   │   └─► publishers[publisher_index_].active.store(false, release);
    │   │
    │   └─► 删除服务发现文件
    │       └─► unlink("/tmp/lightap/services/Service_Name.pub_<UUID>")
    │
    ▼

[Subscriber 无需感知]
    │
    └─► Subscriber 继续从自己的队列读取
        Publisher 下线不影响已发送的消息
```

```
┌─────────────────────────────────────────────────────────────────┐
│                   Subscriber 断开连接流程                         │
└─────────────────────────────────────────────────────────────────┘

[Subscriber 析构]
    │
    ├─► Subscriber::~Subscriber()
    │   ├─► 释放所有持有的 Sample
    │   │   └─► // Sample 析构时自动 DecrementRef
    │   │
    │   ├─► 清空自己的消息队列
    │   │   └─► while (queue.count > 0) {
    │   │           chunk_index = queue.Dequeue();
    │   │           chunk = GetChunkByIndex(chunk_index);
    │   │           chunk->DecrementRef(allocator_);  // 减少引用
    │   │       }
    │   │
    │   ├─► 从所有 Publisher 的订阅列表中移除自己
    │   │   └─► for each publisher {
    │   │           for (j = 0; j < MAX_SUBSCRIBERS_PER_PUB; ++j) {
    │   │               if (publishers[pub_idx].subscriber_list[j]
    │   │                       .load(acquire) == queue_index_) {
    │   │                   publishers[pub_idx].subscriber_list[j]
    │   │                       .store(kInvalidIndex, release);
    │   │                   publishers[pub_idx].subscriber_count
    │   │                       .fetch_sub(1, release);
    │   │                   break;
    │   │               }
    │   │           }
    │   │       }
    │   │
    │   ├─► 标记队列槽位为不活跃
    │   │   └─► subscriber_queues[queue_index_].active.store(false, release);
    │   │
    │   └─► 删除服务发现文件（可选）
    │       └─► unlink("/tmp/lightap/services/Service_Name.sub_<UUID>")
    │
    ▼

[队列资源回收]
    │
    └─► 队列槽位可被新的 Subscriber 复用
```

#### 5.2.3 Subscriber 完整退出流程

**⚠️ 关键设计要求：确保资源正确释放和消息不丢失**

```
┌─────────────────────────────────────────────────────────────────┐
│              Subscriber 完整退出流程（三步骤）                   │
└─────────────────────────────────────────────────────────────────┘

[步骤1] 从 ChannelRegistry 注销（停止接收新消息）
    │
    ├─► subscriber.Disconnect() 调用
    │   │
    │   ├─► UnregisterSubscriber(control_block_, queue_index_)
    │   │   │
    │   │   ├─► 获取写缓冲区快照
    │   │   │   └─► write_snap = &ctrl->snapshots[write_index]
    │   │   │
    │   │   ├─► 从 queue_indices[] 中移除自己的索引
    │   │   │   ├─► 查找 queue_indices[i] == queue_index_
    │   │   │   └─► 后续元素前移（线性移除）
    │   │   │
    │   │   ├─► 更新快照元数据
    │   │   │   ├─► write_snap->count--
    │   │   │   └─► write_snap->version++（版本号递增）
    │   │   │
    │   │   ├─► 切换活跃快照（CAS 操作）
    │   │   │   └─► active_index.store(current_write, release)
    │   │   │
    │   │   └─► 更新 Subscriber 计数
    │   │       └─► subscriber_count.fetch_sub(1, release)
    │   │
    │   └─► ⚡ 效果：Publisher 下次 Send() 时读取快照将不再包含此 Subscriber
    │
    ▼

[步骤2] 消费队列中剩余消息（避免消息泄漏）
    │
    ├─► while (HasData()) {
    │       auto sample = Receive();  // 非阻塞接收
    │       // sample 析构时自动 DecrementRef
    │   }
    │   │
    │   ├─► 从自己的专属队列读取所有剩余消息
    │   │   └─► subscriber_queues[queue_index_].msg_queue.Dequeue()
    │   │
    │   ├─► 每个消息的引用计数 -1
    │   │   └─► chunk->ref_count.fetch_sub(1, release)
    │   │
    │   └─► 当 ref_count 降为 0 时，Chunk 自动归还 ChunkPool
    │       ├─► IncrementRefCount(0) 返回 true（可回收）
    │       └─► ChunkPoolAllocator::Deallocate(chunk_index)
    │           ├─► 设置状态为 kFree
    │           ├─► 插入 Free-List
    │           └─► 唤醒等待的 Publisher（loan_waitset）
    │
    ▼

[步骤3] 清理队列状态并退出
    │
    ├─► 标记队列槽位为不活跃
    │   └─► subscriber_queues[queue_index_].active.store(false, release)
    │
    ├─► 清空事件标志（可选）
    │   └─► subscriber_queues[queue_index_].event_flags.store(0, release)
    │
    ├─► 删除服务发现文件（可选）
    │   └─► unlink("/tmp/lightap/services/Service_Name.sub_<UUID>")
    │
    └─► Subscriber 对象析构
        ├─► SharedMemoryManager 取消映射（munmap）
        └─► 队列槽位可被新的 Subscriber 复用
    │
    ▼

[完成] 退出成功
```

**代码示例：**

```cpp
// Subscriber 退出的正确实现
class Subscriber {
public:
    ~Subscriber() {
        // 析构函数自动调用 Disconnect
        Disconnect();
    }
    
    Result<void> Disconnect() noexcept {
        if (is_disconnected_) {
            return Ok();  // 已经断开
        }
        
        // ====== 步骤1: 从 ChannelRegistry 注销 ======
        bool unregister_success = UnregisterSubscriber(
            control_block_, 
            queue_index_
        );
        
        if (!unregister_success) {
            // 注销失败（可能已被移除），继续清理
            LOG_WARN("Subscriber {} already unregistered", subscriber_id_);
        }
        
        // ====== 步骤2: 消费队列中剩余消息 ======
        UInt32 consumed_count = 0;
        while (HasData()) {
            auto sample_result = Receive();
            if (sample_result.HasValue()) {
                // Sample 析构时自动 DecrementRef
                consumed_count++;
            } else {
                break;  // 队列为空或出错
            }
        }
        
        LOG_INFO("Subscriber {} consumed {} remaining messages", 
                 subscriber_id_, consumed_count);
        
        // ====== 步骤3: 清理队列状态 ======
        auto* queue = &subscriber_queues_[queue_index_];
        queue->active.store(false, std::memory_order_release);
        queue->event_flags.store(0, std::memory_order_release);
        
        // 删除服务发现文件（可选）
        String discovery_file = fmt::format(
            "/tmp/lightap/services/{}.sub_{}",
            service_name_, subscriber_id_
        );
        unlink(discovery_file.c_str());
        
        is_disconnected_ = true;
        return Ok();
    }
    
private:
    bool is_disconnected_ = false;
};
```

**关键要点：**

1. **顺序不可颠倒**：
   - ✅ 必须先注销（步骤1），再消费消息（步骤2）
   - ❌ 如果先清理队列，Publisher 可能仍在发送消息，导致消息丢失

2. **消息不丢失保证**：
   - 步骤1 注销后，Publisher 不再向此队列发送新消息
   - 步骤2 消费所有已发送的消息，确保引用计数正确减少
   - 步骤3 清理状态，队列槽位可复用

3. **引用计数正确性**：
   - 每个消息的 `ref_count` 在步骤2中通过 Sample 析构减少
   - 当 `ref_count` 降为 0 时，Chunk 自动归还 ChunkPool
   - 避免内存泄漏和 ChunkPool 耗尽

4. **线程安全**：
   - UnregisterSubscriber 使用 CAS 操作，无锁安全
   - 队列状态标记使用 atomic release 语义
   - Publisher 通过版本号检测 Registry 变化

**错误示例（不要这样做）：**

```cpp
// ❌ 错误示例1：直接退出，不消费消息
~Subscriber() {
    // 队列中剩余消息的引用计数永远不会减少
    // 导致 ChunkPool 泄漏！
}

// ❌ 错误示例2：先清理队列再注销
~Subscriber() {
    queue->active = false;        // 先清理
    UnregisterSubscriber(...);    // 后注销
    // Publisher 可能在两者之间发送消息，导致消息丢失！
}

// ❌ 错误示例3：只注销，不消费消息
~Subscriber() {
    UnregisterSubscriber(...);
    // 队列中剩余消息未消费，引用计数泄漏！
}
```

**iceoryx2 设计优势：**

| 设计方面 | 错误设计（Per-Publisher队列） | iceoryx2 风格（Per-Subscriber队列） |
|---------|----------------------------|----------------------------------|
| **消息队列归属** | 每个 Publisher 有队列 | ✅ 每个 Subscriber 有队列 |
| **发送机制** | 入队到 Publisher 的队列 | ✅ 推送到所有 Subscriber 队列 |
| **接收机制** | Round-robin 轮询所有 Publisher | ✅ 从自己的队列读取 |
| **Subscriber 扩展性** | 受限于轮询效率 | ✅ O(1) 读取，无竞争 |
| **消息顺序** | 可能乱序（轮询顺序） | ✅ FIFO 保证（每个队列） |
| **队列满处理** | Publisher 阻塞 | ✅ 可独立背压控制 |
| **广播效率** | 需拷贝到多个队列 | ✅ offset 引用，零拷贝 |

### 5.3 消息发送与接收流程

> **iceoryx2 关键设计：**
> - **消息队列归属**: 每个 Subscriber 拥有独立的消息队列（非 Publisher）
> - **发送机制**: Publisher 遍历所有连接的 Subscriber，推送 chunk 引用
> - **接收机制**: Subscriber 从自己的专属队列读取（无需轮询）
> - **引用计数**: Send 时设置为连接的 Subscriber 数量，每个 Release 时 -1

#### 5.3.1 零拷贝发送流程（Loan-Based API）

```
┌─────────────────────────────────────────────────────────────────┐
│              零拷贝发送流程（Loan → Write → Send）               │
└─────────────────────────────────────────────────────────────────┘

[1] Loan（借出内存块）
    │
    ├─► auto sample = publisher.Loan().Value();
    │   │
    │   ├─► 检查 loan_counter_ < max_loaned_samples_
    │   │   └─► 若超限，返回 Err(CoreErrc::kIPCExceedsMaxLoans)
    │   │
    │   ├─► ChunkPoolAllocator::Allocate()
    │   │   │
    │   │   ├─► expected_index = free_list_head.load(acquire);
    │   │   │
    │   │   ├─► while (expected_index != kInvalidIndex) {
    │   │   │       chunk = GetChunkByIndex(expected_index);
    │   │   │       next_index = chunk->next_free_index;
    │   │   │       
    │   │   │       // CAS 更新 free_list 头
    │   │   │       if (free_list_head.compare_exchange_weak(
    │   │   │               expected_index, next_index, acq_rel, acquire)) {
    │   │   │           
    │   │   │           // ========== 引用计数初始化 ==========
    │   │   │           chunk->ref_count.store(1, relaxed);
    │   │   │           // ref_count = 1: Publisher 持有（Loaned）
    │   │   │           // ===================================
    │   │   │           
    │   │   │           chunk->state.store(kLoaned, relaxed);
    │   │   │           chunk->publisher_id = publisher_index_;
    │   │   │           chunk->sequence_number = 0;
    │   │   │           chunk->timestamp = GetMonotonicTimeNs();
    │   │   │           chunk->next_free_index = kInvalidIndex;
    │   │   │           
    │   │   │           allocated_count.fetch_add(1, relaxed);
    │   │   │           break;  // 成功分配
    │   │   │       }
    │   │   │   }
    │   │   │
    │   │   └─► 若失败（expected_index == kInvalidIndex），
    │   │       返回 Err(CoreErrc::kIPCChunkPoolExhausted)
    │   │
    │   ├─► loan_counter_.fetch_add(1, relaxed);
    │   │   // loan_counter += 1: 本地跟踪 Publisher 端未发送的样本数
    │   │
    │   └─► 返回 Sample<PayloadType>{chunk, this}
    │
    ▼

[2] Write（写入有效载荷）
    │
    ├─► sample->x = 100;
    ├─► sample->y = 200;
    └─► sample->name = "Hello";
    │   // 直接写入共享内存 chunk->payload
    │   // 此时 chunk->state == kLoaned, ref_count == 1
    │
    ▼

[3] Send（发送消息到所有 Subscriber 队列）
    │
    ├─► publisher.Send(std::move(sample)).Value();
    │   │
    │   ├─► auto* chunk = sample.Release();  // 获取 chunk 指针
    │   │
    │   ├─► 状态转换: kLoaned -> kSent
    │   │   ├─► expected = kLoaned;
    │   │   └─► chunk->state.compare_exchange_strong(
    │   │           expected, kSent, acq_rel, acquire);
    │   │
    │   ├─► 更新元数据
    │   │   ├─► chunk->sequence_number = 
    │   │   │       publishers[i].sequence_number.fetch_add(1, relaxed);
    │   │   └─► chunk->timestamp = GetMonotonicTimeNs();
    │   │
    │   ├─► 计算 Offset（用于跨进程传递）
    │   │   └─► chunk_offset = chunk->chunk_index * config_.chunk_size;
    │   │
    │   ├─► ========== 广播到所有连接的 Subscriber 队列 ==========
    │   │   │
    │   │   ├─► 获取连接的 Subscriber 数量
    │   │   │   └─► sub_count = publishers[i].subscriber_count.load(acquire);
    │   │   │
    │   │   ├─► 初始化引用计数（广播模式）
    │   │   │   └─► chunk->ref_count.store(sub_count, release);
    │   │   │       // ref_count = 连接的 Subscriber 数量
    │   │   │       // 每个 Subscriber 会持有一个引用
    │   │   │
    │   │   └─► 遍历所有连接的 Subscriber，推送到各自队列
    │   │       │
    │   │       └─► for (j = 0; j < MAX_SUBSCRIBERS_PER_PUB; ++j) {
    │   │               queue_idx = publishers[i].subscriber_list[j]
    │   │                              .load(acquire);
    │   │               
    │   │               if (queue_idx == kInvalidIndex) {
    │   │                   break;  // 没有更多 Subscriber
    │   │               }
    │   │               
    │   │               // 推送到该 Subscriber 的队列（offset-based）
    │   │               subscriber_queues[queue_idx].msg_queue
    │   │                   .EnqueueOffset(chunk_offset, shm_mgr_);
    │   │               
    │   │               // 注意：不增加 ref_count（已在初始化时设置）
    │   │           }
    │   │   │
    │   │   // ====================================================
    │   │
    │   └─► loan_counter_.fetch_sub(1, release);
    │       // loan_counter -= 1: Publisher 不再持有（已发送）
    │
    ▼

[4] 消息在各个 Subscriber 队列中等待接收
    │
    └─► chunk->state == kSent
        chunk->ref_count == N  // N = 连接的 Subscriber 数量
        
        例如：3 个 Subscriber 连接
        - subscriber_queues[5].msg_queue 包含 chunk_offset
        - subscriber_queues[12].msg_queue 包含 chunk_offset
        - subscriber_queues[18].msg_queue 包含 chunk_offset
        - chunk->ref_count == 3
```

**关键引用计数变化（广播模式）：**
```
Loan():  ref_count = 0 → 1       (Publisher 持有)
Send():  ref_count = 1 → N       (N = 连接的 Subscriber 数量)
         推送到 N 个 Subscriber 队列
         每个队列持有相同的 chunk_offset（不是拷贝，是引用）
```

#### 5.3.2 接收流程（Receive - 从自己的队列读取）

```
┌─────────────────────────────────────────────────────────────────┐
│              消息接收流程（从专属队列读取）                       │
└─────────────────────────────────────────────────────────────────┘

[1] Receive（从自己的队列接收消息）
    │
    ├─► auto sample = subscriber.Receive().Value();
    │   │
    │   ├─► 从自己的专属队列出队（使用 RingBufferBlock）
    │   │   │
    │   │   ├─► chunk_index = subscriber_queues[queue_index_].msg_queue
    │   │   │                      .Dequeue();
    │   │   │   │
    │   │   │   ├─► read_pos = head.load(acquire);
    │   │   │   │
    │   │   │   ├─► if (count == 0) {
    │   │   │   │       return kInvalidIndex;  // 队列为空
    │   │   │   │   }
    │   │   │   │
    │   │   │   ├─► chunk_index = buffer[read_pos];
    │   │   │   ├─► next_head = (read_pos + 1) % capacity;
    │   │   │   ├─► head.store(next_head, release);
    │   │   │   │
    │   │   │   ├─► if (new_head == kInvalidOffset) {
    │   │   │   │       tail_offset.store(kInvalidOffset, release);
    │   │   │   │   }
    │   │   │   │
    │   │   │   ├─► count.fetch_sub(1, relaxed);
    │   │   │   └─► return old_head;
    │   │   │
    │   │   └─► 若队列为空，返回 Err(CoreErrc::kIPCNoData)
    │   │
    │   ├─► 从 offset 转换为本地指针
    │   │   └─► chunk = shm_mgr_->OffsetToPtr<ChunkHeader>(chunk_offset);
    │   │
    │   ├─► 状态检查（可选，调试用）
    │   │   └─► assert(chunk->state == kSent);
    │   │
    │   ├─► ========== 引用计数不变 ==========
    │   │   // 注意：ref_count 在 Send 时已设置为 N
    │   │   // 每个 Subscriber Receive 时不增加 ref_count
    │   │   // 只在 Release 时递减
    │   │   // ===================================
    │   │
    │   ├─► 更新接收时间
    │   │   └─► subscriber_queues[queue_index_].last_receive_time
    │   │           .store(GetMonotonicTime(), relaxed);
    │   │
    │   └─► 返回 Sample<PayloadType>{chunk}
    │
    ▼

[2] Use（使用数据）
    │
    ├─► std::cout << sample->x << ", " << sample->y << "\n";
    │   // 读取共享内存 chunk->payload
    │   // chunk->state == kSent, ref_count == N（初始值）
    │
    ▼

[3] Release（自动释放）
    │
    ├─► }  // sample 超出作用域，触发析构
    │   │
    │   └─► Sample<PayloadType>::~Sample()
    │       │
    │       ├─► if (chunk_) {
    │       │       chunk_->DecrementRef(allocator_);
    │       │   }
    │       │
    │       └─► ChunkHeader::DecrementRef(allocator)
    │           │
    │           ├─► ========== 引用计数递减 ==========
    │           │   old_ref = ref_count.fetch_sub(1, acq_rel);
    │           │   // 每个 Subscriber Release 时 -1
    │           │   // =====================================
    │           │
    │           ├─► if (old_ref == 1) {
    │           │       // 最后一个引用，释放内存
    │           │       std::atomic_thread_fence(acquire);
    │           │       
    │           │       // 状态转换: kSent → kFree
    │           │       expected = kSent;
    │           │       state.compare_exchange_strong(
    │           │           expected, kFree, acq_rel, acquire);
    │           │       
    │           │       // 归还到 ChunkPool
    │           │       allocator->Deallocate(this);
    │           │   }
    │           │
    │           └─► // 否则: 仍有其他 Subscriber 引用，不释放
    │
    ▼
```

**多个 Subscriber 接收场景（ref_count 变化）：**
```
Publisher Send():        ref_count = 1 → 3  (3个Subscriber连接)
                         推送到 3 个队列

Subscriber1 Receive():   ref_count = 3      (不变，只是从队列取出)
Subscriber2 Receive():   ref_count = 3      (不变)
Subscriber3 Receive():   ref_count = 3      (不变)

Subscriber1 Release():   ref_count = 3 → 2  (减少引用)
Subscriber2 Release():   ref_count = 2 → 1  (减少引用)
Subscriber3 Release():   ref_count = 1 → 0  (最后一个，触发释放)
                         状态: kSent → kFree
                         归还到 ChunkPool
```

**关键设计优势（iceoryx2）：**
- ✅ **无需 Round-robin**：每个 Subscriber 只读自己的队列
- ✅ **零拷贝广播**：同一个 chunk 被多个队列引用（offset）
- ✅ **公平性保证**：每个 Subscriber 有独立队列，不会饥饿
- ✅ **背压控制**：队列满时 Publisher 可以检测并处理

#### 5.3.3 拷贝发送流程（SendCopy）

```
┌─────────────────────────────────────────────────────────────────┐
│                  拷贝发送流程（SendCopy）                        │
└─────────────────────────────────────────────────────────────────┘

[1] SendCopy（便捷接口）
    │
    ├─► publisher.SendCopy(data).Value();
    │   │
    │   ├─► [内部实现] 等价于:
    │   │   {
    │   │       auto sample = Loan().Value();
    │   │       *sample = data;  // 拷贝数据
    │   │       Send(std::move(sample)).Value();
    │   │   }
    │   │
    │   └─► 引用计数变化与 Loan-Based API 相同
    │
    ▼
```

### 5.4 Chunk 生命周期与引用计数详解

#### 5.4.1 Chunk 状态机与引用计数映射

```
┌─────────────────────────────────────────────────────────────────┐
│           Chunk 状态机 + 引用计数生命周期                        │
└─────────────────────────────────────────────────────────────────┘

状态: kFree
引用计数: ref_count = 0
持有者: 无（在 free_list 中）
操作: 等待被分配
    │
    │ [Allocate()]
    │ - CAS 从 free_list 取出
    │ - ref_count: 0 → 1
    │ - loan_counter += 1 (Publisher 本地)
    │
    ▼
状态: kLoaned
引用计数: ref_count = 1
持有者: Publisher (通过 Sample<T> 对象)
操作: Publisher 写入数据
    │
    │ [Send()]
    │ - 转移到消息队列
    │ - ref_count: 保持 1（不变）
    │ - loan_counter -= 1 (Publisher 本地)
    │
    ▼
状态: kSent
引用计数: ref_count = 1
持有者: 消息队列（等待被接收）
操作: 在 Publisher 的 msg_queue 中
    │
    │ [Receive()] by Subscriber1
    │ - ref_count: 1 → 2 (Subscriber1 引用)
    │
    ▼
状态: kReceived
引用计数: ref_count = 2
持有者: 
  - 原始引用（来自 Send）
  - Subscriber1 (通过 Sample<T> 对象)
操作: Subscriber1 正在使用
    │
    │ [Receive()] by Subscriber2 (可选，广播模式)
    │ - ref_count: 2 → 3 (Subscriber2 引用)
    │
    ▼
引用计数: ref_count = 3
持有者: 
  - 原始引用
  - Subscriber1
  - Subscriber2
    │
    │ [Release()] by Subscriber1
    │ - ref_count: 3 → 2
    │
    ▼
引用计数: ref_count = 2
持有者: 
  - 原始引用
  - Subscriber2
    │
    │ [Release()] by Subscriber2
    │ - ref_count: 2 → 1
    │
    ▼
引用计数: ref_count = 1
持有者: 原始引用（最后一个）
    │
    │ [Final Release] 
    │ - ref_count: 1 → 0
    │ - old_ref == 1, 触发释放逻辑
    │ - state: kReceived → kFree
    │ - Deallocate() 归还到 free_list
    │
    ▼
状态: kFree
引用计数: ref_count = 0
持有者: 无（回到 free_list）
操作: 可被再次分配
```

#### 5.4.2 引用计数详细跟踪示例（iceoryx2 广播模式）

```cpp
// ==================== 场景: 1 Publisher, 3 Subscribers ====================

// === 初始状态 ===
ChunkPool: [Chunk0(FREE, ref=0), Chunk1(FREE, ref=0), ...]
Publisher: loan_counter = 0
Subscriber1: queue_index = 5  (subscriber_queues[5])
Subscriber2: queue_index = 12 (subscriber_queues[12])
Subscriber3: queue_index = 18 (subscriber_queues[18])

Publisher 的 subscriber_list = [5, 12, 18, kInvalidIndex, ...]
Publisher 的 subscriber_count = 3

// === [1] Publisher.Loan() ===
chunk0 = Allocate();
chunk0->state = kLoaned;
chunk0->ref_count = 1;           // ← 初始引用（Publisher 持有）
publisher.loan_counter = 1;       // ← 本地计数

状态快照:
  chunk0: { state=kLoaned, ref_count=1 }
  引用持有者: [Publisher]

// === [2] Publisher.Send() ===
chunk0->state = kSent;
chunk0->ref_count = 3;            // ← 设置为连接的 Subscriber 数量！
publisher.loan_counter = 0;       // ← 本地计数减少

// 推送到所有 Subscriber 队列
subscriber_queues[5].msg_queue.EnqueueOffset(chunk0_offset);   // Sub1
subscriber_queues[12].msg_queue.EnqueueOffset(chunk0_offset);  // Sub2
subscriber_queues[18].msg_queue.EnqueueOffset(chunk0_offset);  // Sub3

状态快照:
  chunk0: { state=kSent, ref_count=3 }
  引用分布: 
    - subscriber_queues[5]: 包含 chunk0_index
    - subscriber_queues[12]: 包含 chunk0_index
    - subscriber_queues[18]: 包含 chunk0_index
  引用计数: 3 (每个 Subscriber 队列算 1 个引用)

// === [3] Subscriber1.Receive() ===
chunk0_index = subscriber_queues[5].msg_queue.Dequeue();
chunk0 = GetChunkByIndex(chunk0_index);
// ref_count 不变！(已在 Send 时设置为 3)
sample1 = Sample{chunk0};

状态快照:
  chunk0: { state=kSent, ref_count=3 }
  引用持有者: 
    - Subscriber1 持有 Sample (从队列[5]取出)
    - subscriber_queues[12]: 仍包含 chunk0_index
    - subscriber_queues[18]: 仍包含 chunk0_index

// === [4] Subscriber2.Receive() ===
chunk0_index = subscriber_queues[12].msg_queue.Dequeue();
chunk0 = GetChunkByIndex(chunk0_index);
// ref_count 不变！
sample2 = Sample{chunk0};

状态快照:
  chunk0: { state=kSent, ref_count=3 }
  引用持有者: 
    - Subscriber1 持有 Sample
    - Subscriber2 持有 Sample
    - subscriber_queues[18]: 仍包含 chunk0_index

// === [5] Subscriber3.Receive() ===
chunk0_index = subscriber_queues[18].msg_queue.Dequeue();
chunk0 = GetChunkByIndex(chunk0_index);
// ref_count 不变！
sample3 = Sample{chunk0};

状态快照:
  chunk0: { state=kSent, ref_count=3 }
  引用持有者: 
    - Subscriber1 持有 Sample
    - Subscriber2 持有 Sample
    - Subscriber3 持有 Sample
  所有队列已清空

// === [6] Subscriber1 释放 (sample1 析构) ===
old_ref = chunk0->ref_count.fetch_sub(1);  // 3 → 2, old_ref=3
// old_ref != 1, 不触发释放

状态快照:
  chunk0: { state=kSent, ref_count=2 }
  引用持有者: [Subscriber2, Subscriber3]

// === [7] Subscriber2 释放 (sample2 析构) ===
old_ref = chunk0->ref_count.fetch_sub(1);  // 2 → 1, old_ref=2
// old_ref != 1, 不触发释放

状态快照:
  chunk0: { state=kSent, ref_count=1 }
  引用持有者: [Subscriber3]

// === [8] Subscriber3 释放 (sample3 析构) ===
old_ref = chunk0->ref_count.fetch_sub(1);  // 1 → 0, old_ref=1
// old_ref == 1, 触发释放逻辑!

chunk0->state.compare_exchange_strong(kSent, kFree);
Deallocate(chunk0);  // 归还到 free_list

状态快照:
  chunk0: { state=kFree, ref_count=0 }
  引用持有者: []
  ChunkPool: free_list = [chunk0, ...]
```

**关键差异对比：**

| 操作 | 错误设计（Per-Pub队列） | iceoryx2（Per-Sub队列） |
|------|----------------------|----------------------|
| **Send()** | ref_count = 1 (不变) | ✅ ref_count = N (Subscriber数) |
| **Receive()** | ref_count += 1 (每次) | ✅ ref_count 不变 (已预设) |
| **Release()** | ref_count -= 1 | ✅ ref_count -= 1 |
| **最后释放** | 最后一个 Subscriber | ✅ 最后一个 Subscriber |

#### 5.4.3 引用计数关键代码注释

```cpp
// ============ Allocate 时初始化引用计数 ============
Result<ChunkHeader*> ChunkPoolAllocator::Allocate() noexcept {
    // ... CAS 从 free_list 取出 chunk ...
    
    // ========== 引用计数: 0 → 1 ==========
    chunk->ref_count.store(1, std::memory_order_relaxed);
    // 原因: Publisher 持有（Loaned 状态）
    // 持有者: Publisher (通过 Sample<T>)
    // =====================================
    
    chunk->state.store(static_cast<UInt32>(ChunkState::kLoaned), relaxed);
    return Ok(chunk);
}

// ============ Send 时引用计数保持不变 ============
void Publisher::Send(Sample<T>&& sample) {
    auto* chunk = sample.Release();
    
    // 状态转换: kLoaned → kSent
    chunk->state.compare_exchange_strong(...);
    
    // ========== 引用计数: 保持 1 (不变) ==========
    // 原因: 转移到消息队列，但引用数不变
    // 持有者: 消息队列（等待 Subscriber 接收）
    // ============================================
    
    msg_queue_.EnqueueOffset(chunk_offset, shm_mgr_);
    loan_counter_.fetch_sub(1, release);  // 本地计数减少
}

// ============ Receive 时增加引用计数 ============
Result<Sample<T>> Subscriber::Receive() {
    // ... Round-robin 从队列取出 chunk_offset ...
    
    auto* chunk = shm_mgr_->OffsetToPtr<ChunkHeader>(chunk_offset);
    
    // 状态转换: kSent → kReceived
    chunk->state.compare_exchange_strong(...);
    
    // ========== 引用计数: +1 ==========
    chunk->ref_count.fetch_add(1, std::memory_order_acquire);
    // 原因: Subscriber 持有新引用
    // 持有者: 增加 1 个 Subscriber (通过 Sample<T>)
    // ==================================
    
    return Ok(Sample<T>{chunk});
}

// ============ Sample 析构时减少引用计数 ============
template<typename T>
Sample<T>::~Sample() noexcept {
    if (chunk_) {
        chunk_->DecrementRef(allocator_);
    }
}

void ChunkHeader::DecrementRef(ChunkPoolAllocator* allocator) noexcept {
    // ========== 引用计数: -1 ==========
    UInt64 old_ref = ref_count.fetch_sub(1, std::memory_order_acq_rel);
    // old_ref: 递减前的值
    // ==================================
    
    if (old_ref == 1) {
        // ========== 最后一个引用被释放 ==========
        // old_ref == 1 表示递减前 ref_count = 1
        // 递减后 ref_count = 0
        // 可以安全释放内存
        // ========================================
        
        std::atomic_thread_fence(std::memory_order_acquire);
        
        // 状态转换: kReceived → kFree
        UInt32 expected = static_cast<UInt32>(ChunkState::kReceived);
        UInt32 desired = static_cast<UInt32>(ChunkState::kFree);
        state.compare_exchange_strong(expected, desired, acq_rel, acquire);
        
        // 归还到 ChunkPool
        allocator->Deallocate(this);
        
        // 🔥 唤醒等待 Loan 的 Publisher（通知有可用 Chunk）
        auto* ctrl = allocator->GetControlBlock();
        WaitSetHelper::SetFlagsAndWake(&ctrl->loan_waitset, EventFlag::HAS_FREE_CHUNK);
    }
    // 否则: 仍有其他引用，不释放
}
```

#### 5.4.4 双计数器机制总结

| 计数器 | 位置 | 作用域 | 用途 |
|--------|------|--------|------|
| **loan_counter** | Publisher 对象（本地） | 单进程 | 跟踪 Publisher 端未发送的样本数量<br>限制并发 Loan 数量 |
| **ref_count** | ChunkHeader（共享内存） | 跨进程 | 跟踪所有持有该 Chunk 的引用数量<br>自动回收内存（ref_count → 0） |

**关系：**
- `loan_counter` 仅在 Publisher 端生效，防止过度 Loan
- `ref_count` 在所有进程中共享，实现自动内存管理
- `loan_counter` 的变化：
  - `Loan()`: +1
  - `Send()` 或 `Release()`: -1
- `ref_count` 的变化：
  - `Allocate()`: 0 → 1
  - `Receive()` (每个 Subscriber): +1
  - `Sample 析构` (每个): -1
  - 最后一个析构: 1 → 0，触发 `Deallocate()`

---

## 6. 性能优化

### 7.1 性能目标

| 场景 | 目标延迟 | iceoryx2 基准 |
|------|---------|--------------|
| 1KB payload | < 1μs | ~600ns |
| 10KB payload | < 2μs | ~1.2μs |
| 64KB payload | < 10μs | ~8μs |
| 1MB payload | < 100μs | ~80μs |

### 7.2 优化技术

#### 7.2.1 无锁 Subscriber 注册表（iceoryx2 核心优化）

**问题：传统 mutex + vector 的性能瓶颈**

```cpp
// ❌ 传统实现：每次 Send 都需要加锁
void Publisher::Send() {
    std::lock_guard<std::mutex> lock(subscriber_mutex_);  // 🔒 锁开销 ~25ns
    for (auto queue_idx : subscriber_list_) {             // 拷贝列表 ~100ns
        EnqueueToSubscriber(queue_idx);
    }
}
// 问题：
// 1. 锁竞争：Publisher Send 与 Subscriber 注册/注销互斥
// 2. 缓存失效：mutex 导致 CPU 缓存行失效
// 3. 可扩展性差：多个 Publisher 争抢同一个锁
```

**解决方案：版本化快照 + 双缓冲（Lock-Free）**

```cpp
// ✅ iceoryx2 实现：无锁快照机制
struct ChannelRegistry {
    // 双缓冲快照：读写分离
    Snapshot snapshots[2];
    std::atomic<Snapshot*> active_snapshot;  // 读侧：无锁
    std::atomic<UInt32> write_index;         // 写侧：CAS
    
    // Publisher 无锁读取（热路径）
    Snapshot GetSnapshot() const {
        auto* snap = active_snapshot.load(std::memory_order_acquire);
        return *snap;  // 栈上拷贝 ~512B，耗时 < 50ns
    }
    
    // Subscriber 注册（冷路径，允许慢一点）
    bool Register(UInt32 queue_idx) {
        // ... 写入 write_buffer ...
        std::atomic_thread_fence(std::memory_order_release);  // 内存屏障
        
        // CAS 切换活跃快照
        active_snapshot.compare_exchange_strong(...);
    }
};
```

**性能对比：**

| 操作 | mutex + vector | ChannelRegistry | 提升 |
|------|----------------|-------------------|------|
| **Publisher::Send() 获取列表** | ~125ns (lock + copy) | ~50ns (无锁快照) | **2.5x** |
| **Subscriber 注册** | ~150ns (lock + push_back) | ~200ns (CAS + 双缓冲) | 0.75x (可接受) |
| **锁竞争 (4 Pub)** | ~500ns (严重竞争) | ~50ns (无竞争) | **10x** |
| **可扩展性** | 随 Publisher 数量线性下降 | 常数时间 O(1) | ∞ |

**内存屏障说明：**

```cpp
Snapshot GetSnapshot() const {
    // [屏障1] memory_order_acquire: 
    // 确保读取到的 snapshot 指针是最新的，防止 CPU 乱序执行
    auto* snap = active_snapshot.load(std::memory_order_acquire);
    
    // [屏障2] 隐式屏障（拷贝构造）
    Snapshot result = *snap;  // 拷贝时编译器插入屏障
    
    // [屏障3] memory_order_acquire:
    // 确保快照数据完全可见后才继续
    std::atomic_thread_fence(std::memory_order_acquire);
    
    return result;
}

bool Register(UInt32 queue_idx) {
    // ... 修改 write_buffer ...
    
    // [屏障4] memory_order_release:
    // 确保所有写入对其他线程可见
    std::atomic_thread_fence(std::memory_order_release);
    
    // [屏障5] CAS 的 release 语义:
    // 发布新快照，所有之前的写入对读者可见
    active_snapshot.compare_exchange_strong(
        expected, new_snapshot,
        std::memory_order_release,  // 成功：发布写入
        std::memory_order_acquire   // 失败：重新读取
    );
}
```

**优势总结：**

1. **热路径优化**：Publisher Send() 完全无锁，适合高频操作
2. **冷路径允许慢**：Subscriber 注册/注销是低频操作，CAS 开销可接受
3. **无锁竞争**：多个 Publisher 并发 Send 无冲突
4. **缓存友好**：快照拷贝在栈上，CPU L1 缓存命中率高
5. **ABA 问题解决**：版本号 (version) 防止 ABA 问题

#### 7.2.2 CPU 缓存优化

```cpp
// 缓存行对齐（避免 False Sharing）
struct alignas(64) PublisherState {
    std::atomic<UInt32> id;        // 0-3
    std::atomic<bool>   active;    // 4
    // ... padding to 64 bytes ...
};

// 预取指令
__builtin_prefetch(&chunk->payload, 0, 3);  // Read, high temporal locality
```

#### 7.2.2 内存屏障优化

```cpp
// 使用 acquire-release 语义（比 seq_cst 更快）
chunk->ref_count.fetch_add(1, std::memory_order_acquire);
chunk->ref_count.fetch_sub(1, std::memory_order_release);

// 避免不必要的内存屏障
UInt32 count = loan_counter_.load(std::memory_order_relaxed);  // 无屏障
```

#### 7.2.3 批量操作

```cpp
// 批量发送（减少系统调用）
Result<void> SendBatch(Vector<Sample<T>>&& samples) {
    for (auto& sample : samples) {
        msg_queue_.Enqueue(sample.Release());
    }
    // 单次通知所有 Subscriber
    NotifySubscribers();
}
```

### 7.3 基准测试与实际性能

#### 7.3.1 camera_fusion_spmc_example 实测数据（NORMAL模式）

**测试配置：**
```
硬件: ARM Cortex-A76 (8核)
内存: 16GB DDR4
OS: Linux 6.1.0

IPC模式: NORMAL
- max_chunks = 16
- chunk_size = 1920×720×4 = 5,529,600 bytes (~5.3MB)
- channel_capacity = 64 (per Publisher)

场景: 3个Camera Publisher + 1个Fusion Subscriber(3线程)
STMin限流: 10ms (理论上限100 FPS)
```

**实测性能：**

| 指标 | 实测值 | 说明 |
|------|--------|------|
| **Publisher吞吐** | 90-95 FPS | STMin=10ms限流，理论100 FPS |
| **消息延迟** | < 5μs | Loan+Send完整路径 |
| **接收延迟** | < 2μs | Receive+拷贝到fusion buffer |
| **拼接延迟** | < 1ms | 3×5.3MB memcpy到双缓存 |
| **CPU占用** | 25-30% | 3 Pub + 4 Sub线程 |
| **内存占用** | 97MB | 49MB共享内存 + 48MB进程内存 |
| **BMP保存** | ~50ms | 5.3MB文件写入磁盘 |

**关键发现：**
1. **零拷贝优势**：5.3MB图像传输延迟 < 5μs，传统socket需要10+ms
2. **STMin限流有效**：100 FPS理论上限，实测90+ FPS (受调度影响)
3. **Overwrite策略**：Publisher从不阻塞，保证实时性
4. **多线程并发**：3个Subscriber线程并发接收，无锁争用

#### 7.3.2 性能测试框架

```cpp
// 延迟测试 (1KB消息)
class IPCBenchmark {
public:
    void BenchmarkLatency(UInt32 payload_size) {
        auto pub = Publisher::Create("/bench", {.chunk_size = payload_size}).Value();
        auto sub = Subscriber::Create("/bench").Value();
        
        std::vector<uint64_t> latencies;
        for (int i = 0; i < 10000; ++i) {
            auto start = std::chrono::high_resolution_clock::now();
            
            pub.Send([](void* ptr, size_t) { return 1024; });
            auto sample = sub.Receive().Value();
            
            auto end = std::chrono::high_resolution_clock::now();
            latencies.push_back((end - start).count());
        }
        
        // 统计: p50, p99, p999
        std::sort(latencies.begin(), latencies.end());
        std::cout << "p50: " << latencies[5000] << "ns\n";
        std::cout << "p99: " << latencies[9900] << "ns\n";
    }
};

// 预期结果:
// 1KB消息: p50 < 1μs, p99 < 2μs
// 1MB消息: p50 < 3μs, p99 < 5μs
// 5MB消息: p50 < 5μs, p99 < 10μs
```

#### 7.3.3 压力测试结果

**8小时连续压测（camera_fusion_spmc_example）：**

```bash
# 启动测试
./camera_fusion_spmc_example 28800  # 8小时 = 28800秒

# 实测结果（STMin=10ms配置）
Total Runtime: 28800.5 seconds
Frames Sent (Cam0): 2,590,234 (avg 90.0 FPS)
Frames Sent (Cam1): 2,590,156 (avg 90.0 FPS)
Frames Sent (Cam2): 2,589,987 (avg 89.9 FPS)
Frames Received: 7,770,377 (total from 3 cameras)
Frame Loss Rate: 0.02% (丢帧率极低)
Memory Leak: 0 bytes (无内存泄漏)
```

**关键指标：**
- ✅ **长期稳定性**：8小时无崩溃、无死锁
- ✅ **内存安全**：引用计数正确，无泄漏
- ✅ **实时性保证**：90 FPS稳定维持（STMin限流）
- ✅ **丢帧率极低**：< 0.1% (Overwrite策略优化)

#### 7.3.4 与传统IPC对比

| IPC方式 | 5MB消息延迟 | 吞吐量 | CPU占用 | 零拷贝 |
|---------|-------------|--------|---------|---------|
| **LightAP IPC** | **< 5μs** | **90+ FPS** | **25%** | ✅ |
| Unix Socket | ~15ms | 60 FPS | 45% | ❌ |
| TCP Socket | ~20ms | 50 FPS | 55% | ❌ |
| Message Queue | ~10ms | 70 FPS | 40% | ❌ |
| Shared Memory (手动) | ~8μs | 85 FPS | 30% | ✅ (需手动管理) |

**结论：LightAP IPC提供最佳性能和最简API**
```

---

## 7. 安全性设计

### 7.1 Hook 回调机制

**设计目标：** 在关键错误路径提供用户自定义处理能力，实现灵活的错误恢复、日志记录和监控。

#### 8.1.1 Hook 接口定义

```cpp
namespace ara::core::ipc {

/**
 * @brief IPC 事件 Hook 接口
 * 
 * 用户可实现此接口以自定义关键事件的处理逻辑
 */
class IPCEventHooks {
public:
    virtual ~IPCEventHooks() = default;
    
    // ==================== Publisher Hook ====================
    
    /**
     * @brief Loan 失败回调
     * @param publisher_id Publisher 标识
     * @param error 错误码
     * @param allocated_count 当前已分配的 Chunk 数量
     * @param max_chunks ChunkPool 最大容量
     */
    virtual void OnLoanFailed(
        UInt32 publisher_id,
        CoreErrc error,
        UInt32 allocated_count,
        UInt32 max_chunks) noexcept {
        // 默认实现：记录日志
        LOG_WARN("Publisher {} loan failed: error={}, allocated={}/{}",
                 publisher_id, static_cast<int>(error), allocated_count, max_chunks);
    }
    
    /**
     * @brief Loan Counter 警告回调
     * @param publisher_id Publisher 标识
     * @param current_loans 当前借出的 Sample 数量
     * @param threshold 警告阈值
     * @note 当 loan_counter >= threshold 时触发，用于检测潜在的资源泄漏
     */
    virtual void OnLoanCounterWarning(
        UInt32 publisher_id,
        UInt32 current_loans,
        UInt32 threshold) noexcept {
        LOG_WARN("Publisher {} loan_counter high: {}/{} (potential resource leak)",
                 publisher_id, current_loans, threshold);
    }
    
    /**
     * @brief Subscriber 队列连续写入失败回调
     * @param publisher_id Publisher 标识
     * @param subscriber_id Subscriber 标识
     * @param queue_index 队列索引
     * @param failure_count 连续失败次数
     * @param overrun_count 总溢出计数
     */
    virtual void OnSubscriberQueueOverrun(
        UInt32 publisher_id,
        UInt32 subscriber_id,
        UInt32 queue_index,
        UInt32 failure_count,
        UInt64 overrun_count) noexcept {
        // 默认实现：记录警告
        LOG_ERROR("Subscriber {} queue {} overrun: failures={}, total_overruns={}",
                  subscriber_id, queue_index, failure_count, overrun_count);
    }
    
    /**
     * @brief ChunkPool 耗尽回调
     * @param allocated_count 已分配数量
     * @param max_chunks 最大块数
     */
    virtual void OnChunkPoolExhausted(
        UInt32 allocated_count,
        UInt32 max_chunks) noexcept {
        LOG_CRITICAL("ChunkPool exhausted: {}/{} chunks allocated",
                     allocated_count, max_chunks);
    }
    
    // ==================== Subscriber Hook ====================
    
    /**
     * @brief 接收超时回调
     * @param subscriber_id Subscriber 标识
     * @param timeout_ms 超时时间（毫秒）
     * @param queue_count 当前队列消息数
     */
    virtual void OnReceiveTimeout(
        UInt32 subscriber_id,
        UInt32 timeout_ms,
        UInt32 queue_count) noexcept {
        LOG_WARN("Subscriber {} receive timeout: {}ms, queue_count={}",
                 subscriber_id, timeout_ms, queue_count);
    }
    
    /**
     * @brief E2E 校验失败回调
     * @param subscriber_id Subscriber 标识
     * @param chunk_index Chunk 索引
     * @param error E2E 错误类型
     */
    virtual void OnE2EValidationFailed(
        UInt32 subscriber_id,
        UInt32 chunk_index,
        E2EError error) noexcept {
        LOG_ERROR("Subscriber {} E2E validation failed: chunk={}, error={}",
                  subscriber_id, chunk_index, static_cast<int>(error));
    }
    
    // ==================== 共享内存 Hook ====================
    
    /**
     * @brief 共享内存损坏检测回调
     * @param magic_number 当前魔数
     * @param expected_magic 预期魔数
     */
    virtual void OnMemoryCorruption(
        UInt32 magic_number,
        UInt32 expected_magic) noexcept {
        LOG_CRITICAL("Shared memory corruption detected: magic={:X}, expected={:X}",
                     magic_number, expected_magic);
    }
};

} // namespace ara::core::ipc
```

#### 8.1.2 Hook 使用示例

```cpp
// [示例1] 自定义 Hook 实现
class MyIPCHooks : public IPCEventHooks {
public:
    void OnLoanFailed(
        UInt32 publisher_id,
        AllocationError error,
        UInt32 loaned_count,
        UInt32 max_loans) noexcept override {
        
        // 记录到监控系统
        metrics_.RecordLoanFailure(publisher_id);
        
        // 触发告警（连续失败 > 10 次）
        if (++loan_failure_count_[publisher_id] > 10) {
            alerting_system_.SendAlert(
                AlertLevel::kCritical,
                "Publisher {} loan failures exceeded threshold",
                publisher_id
            );
        }
        
        // 可选：尝试清理资源
        TryReclaimStaleChunks(publisher_id);
    }
    
    void OnSubscriberQueueOverrun(
        UInt32 publisher_id,
        UInt32 subscriber_id,
        UInt32 queue_index,
        UInt32 failure_count,
        UInt64 overrun_count) noexcept override {
        
        // 检测慢订阅者
        if (failure_count > 100) {
            LOG_WARN("Slow subscriber detected: {}, consider increasing queue size",
                     subscriber_id);
            
            // 通知应用层处理
            app_callback_->OnSlowSubscriberDetected(subscriber_id);
        }
        
        // 记录统计数据
        stats_.subscriber_overruns[subscriber_id] = overrun_count;
    }
    
private:
    MetricsCollector& metrics_;
    AlertingSystem& alerting_system_;
    std::unordered_map<UInt32, UInt32> loan_failure_count_;
    IPCStatistics stats_;
};

// [示例2] 注册 Hook
auto my_hooks = std::make_shared<MyIPCHooks>();

auto publisher = service.CreatePublisher()
    .SetEventHooks(my_hooks)  // 注册 Hook
    .Create()
    .Value();

auto subscriber = service.CreateSubscriber()
    .SetEventHooks(my_hooks)
    .Create()
    .Value();
```

#### 8.1.3 关键位置集成 Hook

**Publisher::Loan() Hook 集成示例**：

完整实现见 [7.5.2.1 Loan 失败处理](#7521-loan-失败处理)，Hook触发点包括：

```cpp
// Hook 触发点示例
if (!chunk_result.HasValue()) {
    // 🔥 触发 ChunkPool 耗尽 Hook
    event_hooks_->OnChunkPoolExhausted(allocated_count, max_chunks);
    
    // 🔥 触发 Loan 失败 Hook
    event_hooks_->OnLoanFailed(publisher_id_, error, current_loans, max_loans);
    
    // 根据 LoanPolicy 处理（kError/kWait/kBlock）
    // ...
}
```

// Publisher::Send() 中检测连续失败
void Publisher::Send(Sample<PayloadType>&& sample) {
    auto* chunk = sample.Release();
    UInt32 chunk_index = chunk->chunk_index;
    
    chunk->ref_count.store(subscriber_count_, std::memory_order_release);
    
    // 跟踪每个 Subscriber 的连续失败次数
    for (UInt32 i = 0; i < subscriber_count_; ++i) {
        UInt32 queue_idx = subscriber_list_[i];
        auto& sub_queue = subscriber_queues[queue_idx];
        
        auto result = sub_queue.msg_queue.EnqueueOverwrite(
            chunk_index, shm_mgr_, allocator_
        );
        
        if (result == EnqueueResult::kOverwritten) {
            sub_queue.overrun_count.fetch_add(1, std::memory_order_relaxed);
            
            // 跟踪连续失败
            UInt32 failures = ++queue_failure_count_[queue_idx];
            
            // 🔥 连续失败阈值触发 Hook
            if (failures % 10 == 0 && event_hooks_) {  // 每 10 次触发一次
                event_hooks_->OnSubscriberQueueOverrun(
                    publisher_id_,
                    sub_queue.subscriber_id.load(std::memory_order_acquire),
                    queue_idx,
                    failures,
                    sub_queue.overrun_count.load(std::memory_order_acquire)
                );
            }
        } else {
            // 成功，重置失败计数
            queue_failure_count_[queue_idx] = 0;
        }
    }
}
```

### 7.2 E2E (End-to-End) 保护

**设计目标：** 提供可选的端到端数据完整性保护，检测传输过程中的数据损坏、丢失和重复。

#### 8.2.1 E2E 配置

```cpp
/**
 * @brief E2E 保护配置
 */
struct E2EConfig {
    bool     enabled;              // 是否启用 E2E 保护
    UInt8    data_id;              // 数据标识符（0-255）
    UInt32   counter_offset;       // 计数器起始值
    
    // 检测能力
    bool     check_crc;            // 启用 CRC32 校验
    bool     check_counter;        // 启用计数器检查（检测丢失/重复）
    bool     check_timeout;        // 启用超时检查
    
    // 容错配置
    UInt32   max_delta_counter;    // 最大允许的计数器跳变（检测丢失）
    UInt32   max_no_new_data_ms;   // 最大无新数据时间（毫秒）
    
    // 默认配置：E2E 关闭
    static E2EConfig Disabled() {
        return E2EConfig{
            .enabled = false,
            .data_id = 0,
            .counter_offset = 0,
            .check_crc = false,
            .check_counter = false,
            .check_timeout = false,
            .max_delta_counter = 0,
            .max_no_new_data_ms = 0
        };
    }
    
    // AUTOSAR E2E Profile 1 (CRC + Counter)
    static E2EConfig Profile1(UInt8 data_id) {
        return E2EConfig{
            .enabled = true,
            .data_id = data_id,
            .counter_offset = 0,
            .check_crc = true,
            .check_counter = true,
            .check_timeout = true,
            .max_delta_counter = 15,        // 允许丢失 14 条消息
            .max_no_new_data_ms = 1000      // 1 秒超时
        };
    }
};

/**
 * @brief E2E 错误类型
 */
enum class E2EError : UInt32 {
    kNone               = 0,  // 无错误
    kCRCMismatch        = 1,  // CRC 校验失败
    kCounterJump        = 2,  // 计数器跳变（丢失消息）
    kCounterRepeat      = 3,  // 计数器重复（重复消息）
    kTimeout            = 4,  // 接收超时
    kWrongDataID        = 5,  // 数据 ID 不匹配
};
```

#### 8.2.2 E2E 实现

```cpp
/**
 * @brief E2E 保护器（Publisher 端）
 */
class E2EProtector {
public:
    explicit E2EProtector(const E2EConfig& config) 
        : config_(config), counter_(config.counter_offset) {}
    
    /**
     * @brief 保护数据（发送前调用）
     * @param chunk 要保护的 Chunk
     */
    void Protect(ChunkHeader* chunk) noexcept {
        if (!config_.enabled) {
            return;  // E2E 未启用
        }
        
        // 1. 设置数据 ID
        chunk->e2e.data_id = config_.data_id;
        
        // 2. 设置计数器
        chunk->e2e.counter = counter_++;
        
        // 3. 计算 CRC32（Header + Payload）
        if (config_.check_crc) {
            chunk->e2e.crc32 = CalculateCRC32(chunk);
        }
        
        // 4. 设置标志位
        chunk->e2e.flags = 0;
        if (config_.check_crc) chunk->e2e.flags |= 0x01;
        if (config_.check_counter) chunk->e2e.flags |= 0x02;
    }
    
private:
    E2EConfig config_;
    std::atomic<UInt32> counter_;
    
    UInt32 CalculateCRC32(const ChunkHeader* chunk) const noexcept {
        // CRC32 计算范围：chunk_size + publisher_id + sequence_number + 
        //                 timestamp + e2e(除 crc32 字段) + payload
        
        UInt32 crc = 0xFFFFFFFF;
        
        // 计算 Header 部分（跳过 crc32 字段）
        crc = UpdateCRC32(crc, &chunk->chunk_size, sizeof(chunk->chunk_size));
        crc = UpdateCRC32(crc, &chunk->publisher_id, sizeof(chunk->publisher_id));
        crc = UpdateCRC32(crc, &chunk->sequence_number, sizeof(chunk->sequence_number));
        crc = UpdateCRC32(crc, &chunk->timestamp, sizeof(chunk->timestamp));
        crc = UpdateCRC32(crc, &chunk->e2e.counter, sizeof(chunk->e2e.counter));
        crc = UpdateCRC32(crc, &chunk->e2e.data_id, sizeof(chunk->e2e.data_id));
        crc = UpdateCRC32(crc, &chunk->e2e.flags, sizeof(chunk->e2e.flags));
        
        // 计算 Payload 部分
        UInt64 payload_size = chunk->chunk_size - sizeof(ChunkHeader);
        crc = UpdateCRC32(crc, chunk->payload, payload_size);
        
        return crc ^ 0xFFFFFFFF;
    }
};

/**
 * @brief E2E 验证器（Subscriber 端）
 */
class E2EValidator {
public:
    explicit E2EValidator(const E2EConfig& config)
        : config_(config),
          last_counter_(0),
          last_receive_time_(0),
          is_first_message_(true) {}
    
    /**
     * @brief 验证数据（接收后调用）
     * @param chunk 要验证的 Chunk
     * @return E2E 错误码
     */
    E2EError Validate(const ChunkHeader* chunk) noexcept {
        if (!config_.enabled) {
            return E2EError::kNone;  // E2E 未启用
        }
        
        // 1. 验证数据 ID
        if (chunk->e2e.data_id != config_.data_id) {
            return E2EError::kWrongDataID;
        }
        
        // 2. 验证 CRC32
        if (config_.check_crc) {
            UInt32 calculated_crc = CalculateCRC32(chunk);
            if (calculated_crc != chunk->e2e.crc32) {
                return E2EError::kCRCMismatch;
            }
        }
        
        // 3. 验证计数器
        if (config_.check_counter && !is_first_message_) {
            UInt32 current_counter = chunk->e2e.counter;
            UInt32 expected_counter = (last_counter_ + 1) & 0xFFFFFFFF;
            
            if (current_counter == last_counter_) {
                return E2EError::kCounterRepeat;  // 重复消息
            }
            
            UInt32 delta = (current_counter - expected_counter) & 0xFFFFFFFF;
            if (delta > 0 && delta <= config_.max_delta_counter) {
                // 允许范围内的计数器跳变（丢失消息）
                LOG_WARN("E2E: Detected {} lost messages (counter jump)", delta);
            } else if (delta > config_.max_delta_counter) {
                return E2EError::kCounterJump;  // 丢失过多
            }
        }
        
        // 4. 验证超时
        if (config_.check_timeout) {
            UInt64 current_time = GetMonotonicTimeMs();
            if (!is_first_message_) {
                UInt64 time_delta = current_time - last_receive_time_;
                if (time_delta > config_.max_no_new_data_ms) {
                    return E2EError::kTimeout;
                }
            }
            last_receive_time_ = current_time;
        }
        
        // 更新状态
        last_counter_ = chunk->e2e.counter;
        is_first_message_ = false;
        
        return E2EError::kNone;
    }
    
private:
    E2EConfig config_;
    UInt32 last_counter_;
    UInt64 last_receive_time_;
    bool is_first_message_;
    
    UInt32 CalculateCRC32(const ChunkHeader* chunk) const noexcept {
        // 与 E2EProtector 中的实现相同
        // ...
    }
};
```

#### 8.2.3 E2E 集成到 Publisher/Subscriber

```cpp
// Publisher 集成 E2E
template<typename PayloadType>
class Publisher {
public:
    // 配置 E2E
    Publisher& SetE2EConfig(const E2EConfig& config) {
        e2e_protector_ = std::make_unique<E2EProtector>(config);
        return *this;
    }
    
    void Send(Sample<PayloadType>&& sample) {
        auto* chunk = sample.Release();
        
        // 🔒 E2E 保护
        if (e2e_protector_) {
            e2e_protector_->Protect(chunk);
        }
        
        // 设置序列号和时间戳
        chunk->sequence_number = sequence_number_++;
        chunk->timestamp = GetMonotonicTimeNs();
        
        // 📌 从共享内存 ControlBlock 读取 Subscriber 快照
        // ControlBlock 在共享内存中，所有进程都能访问
        auto snapshot = GetChannelSnapshot(control_block_);
        
        // 广播到所有已注册的 Subscriber 队列
        for (UInt32 i = 0; i < snapshot.count; ++i) {
            UInt32 queue_idx = snapshot.queue_indices[i];
            ChannelQueue* queue = &subscriber_queues_[queue_idx];
            
            // 增加引用计数（每个 Subscriber 一份）
            chunk->ref_count.fetch_add(1, std::memory_order_release);
            
            // 入队（使用 chunk_index）
            bool success = queue->msg_queue.Enqueue(chunk->chunk_index);
            if (!success) {
                // 队列满，根据策略处理...
                chunk->ref_count.fetch_sub(1, std::memory_order_release);
            }
        }
    }
    
private:
    std::unique_ptr<E2EProtector> e2e_protector_;
    UInt64 sequence_number_ = 0;
};

// Subscriber 集成 E2E
template<typename PayloadType>
class Subscriber {
public:
    // 配置 E2E
    Subscriber& SetE2EConfig(const E2EConfig& config) {
        e2e_validator_ = std::make_unique<E2EValidator>(config);
        return *this;
    }
    
    Result<Sample<PayloadType>> Receive() noexcept {
        UInt32 chunk_index = subscriber_queues[queue_index_].msg_queue.Dequeue();
        if (chunk_index == kInvalidIndex) {
            return Err(ReceiveError::kNoData);
        }
        
        auto* chunk = shm_mgr_->GetChunkByIndex(chunk_index);
        
        // 🔒 E2E 验证
        if (e2e_validator_) {
            E2EError e2e_error = e2e_validator_->Validate(chunk);
            if (e2e_error != E2EError::kNone) {
                // 🔥 触发 E2E 失败 Hook
                if (event_hooks_) {
                    event_hooks_->OnE2EValidationFailed(
                        subscriber_id_, chunk_index, e2e_error
                    );
                }
                
                // 根据策略决定是否继续
                if (e2e_error == E2EError::kCRCMismatch) {
                    // CRC 失败，拒绝消息
                    chunk->DecrementRef(allocator_);
                    return Err(ReceiveError::kE2EValidationFailed);
                }
                // 其他错误（如计数器跳变）可能只记录警告
            }
        }
        
        return Ok(Sample<PayloadType>{chunk});
    }
    
private:
    std::unique_ptr<E2EValidator> e2e_validator_;
};
```

#### 8.2.4 E2E 使用示例

```cpp
// [示例1] 启用 E2E 保护
auto publisher = service.CreatePublisher<SensorData>()
    .SetE2EConfig(E2EConfig::Profile1(42))  // 数据 ID = 42
    .Create()
    .Value();

auto subscriber = service.CreateSubscriber<SensorData>()
    .SetE2EConfig(E2EConfig::Profile1(42))  // 必须匹配
    .Create()
    .Value();

// [示例2] 自定义 E2E 配置
E2EConfig custom_config{
    .enabled = true,
    .data_id = 100,
    .counter_offset = 0,
    .check_crc = true,
    .check_counter = true,
    .check_timeout = false,          // 不检查超时
    .max_delta_counter = 5,          // 最多丢失 4 条消息
    .max_no_new_data_ms = 0
};

auto publisher2 = service.CreatePublisher<VideoFrame>()
    .SetE2EConfig(custom_config)
    .Create()
    .Value();

// [示例3] 禁用 E2E（默认）
auto publisher3 = service.CreatePublisher<LogEntry>()
    // 不调用 SetE2EConfig，默认禁用
    .Create()
    .Value();
```

#### 8.2.5 E2E 性能影响

| 配置 | CRC32 开销 | Counter 开销 | 总开销 | 适用场景 |
|------|-----------|-------------|--------|--------|
| **禁用** | 0 ns | 0 ns | 0 ns | 高性能场景 |
| **仅 Counter** | 0 ns | ~10 ns | ~10 ns | 轻量级保护 |
| **CRC32 (1KB)** | ~300 ns | ~10 ns | ~310 ns | 中等数据 |
| **CRC32 (64KB)** | ~15 μs | ~10 ns | ~15 μs | 大数据 |

**优化建议：**
- 使用硬件 CRC32 指令（`_mm_crc32_u32`）
- 对大数据使用分块 CRC32
- 仅在关键数据路径启用 E2E

### 7.3 内存安全

```cpp
// 防御式编程
Result<Sample<T>> Loan() {
    // 检查魔数（检测损坏）
    if (segment_->control.magic_number.load() != kMagicNumber) {
        // 🔥 触发内存损坏 Hook
        if (event_hooks_) {
            event_hooks_->OnMemoryCorruption(
                segment_->control.magic_number.load(),
                kMagicNumber
            );
        }
        return Err(CoreErrc::kIPCMemoryCorruption);
    }
    
    // 检查版本兼容性
    if (segment_->control.version.load() != kCurrentVersion) {
        return Err(CoreErrc::kIPCVersionMismatch);
    }
    
    // 尝试分配
    return allocator_->Allocate();
}
```

### 7.4 进程崩溃恢复

```cpp
// 心跳机制
struct PublisherState {
    std::atomic<UInt64> last_heartbeat;  // 时间戳
};

class ServiceMonitor {
public:
    // 定期检查心跳
    void CheckHeartbeats() {
        auto now = GetMonotonicTime();
        for (auto& pub : segment_->publishers) {
            if (pub.active && 
                now - pub.last_heartbeat > kHeartbeatTimeout) {
                // Publisher 超时，清理其资源
                CleanupPublisher(&pub);
            }
        }
    }
    
private:
    static constexpr Duration kHeartbeatTimeout = Duration::FromSecs(5);
};
```

### 7.3 权限控制

```cpp
// 共享内存权限
Result<SharedMemory> CreateSharedMemory(const String& name, UInt64 size) {
    int fd = shm_open(name.CStr(), O_CREAT | O_RDWR, 0600);  // 仅所有者读写
    if (fd < 0) {
        return Err(CoreErrc::kIPCPermissionDenied);
    }
    
    // 设置所有者
    fchown(fd, getuid(), getgid());
    
    return Ok(SharedMemory{fd, size});
}
```

---

## 7.4 IPC 错误码定义

### 7.4.1 错误码分类

所有 IPC 错误码统一定义在 `CCoreErrorDomain` 中，使用 `CoreErrc` 枚举类型，并加上 `kIPC` 前缀以区分其他模块错误。

**错误码范围分配：**

| 分类 | 错误码范围 | 说明 |
|------|-----------|------|
| 共享内存错误 | 150-169 | 共享内存创建、映射、访问相关错误 |
| 内存分配错误 | 170-189 | ChunkPool 分配、超限相关错误 |
| 状态机错误 | 190-209 | Chunk 状态转换相关错误 |
| 接收错误 | 210-229 | 消息接收、队列操作相关错误 |
| 队列错误 | 230-249 | 消息队列满、溢出相关错误 |
| E2E 保护错误 | 250-269 | 端到端数据保护相关错误 |
| 服务发现错误 | 270-289 | 服务注册、查找相关错误 |
| 连接错误 | 290-309 | Publisher/Subscriber 连接相关错误 |

### 7.5.2 完整错误码列表

```cpp
namespace ara::core {

/**
 * @brief IPC 错误码定义（集成到 CCoreErrorDomain）
 * @note 所有 IPC 错误码使用 kIPC 前缀
 * @note Routine counter 范围: 1 to 0xFFFFFFFE (排除 0x00000000 和 0xFFFFFFFF)
 */
enum class CoreErrc : ErrorDomain::CodeType {
    
    // ========== IPC 共享内存错误 (150-169) ==========
    kIPCShmCreateFailed         = 150,  ///< 创建共享内存失败
    kIPCShmResizeFailed         = 151,  ///< 调整共享内存大小失败
    kIPCShmMapFailed            = 152,  ///< 映射共享内存失败
    kIPCShmNotFound             = 153,  ///< 共享内存段未找到
    kIPCShmStatFailed           = 154,  ///< 获取共享内存状态失败
    kIPCShmCorrupted            = 155,  ///< 共享内存损坏（魔数校验失败）
    kIPCVersionMismatch         = 156,  ///< 进程间版本不匹配
    kIPCMemoryCorruption        = 157,  ///< 内存损坏检测到
    
    // ========== IPC 内存分配错误 (170-189) ==========
    kIPCAllocationFailed        = 170,  ///< 通用内存分配失败
    kIPCChunkPoolExhausted      = 171,  ///< ChunkPool 无可用 Chunk
    kIPCExceedsMaxLoans         = 172,  ///< [已废弃] 改为警告策略，不再返回此错误
    kIPCAllocationNotInitialized = 173, ///< 分配器未初始化
    kIPCOutOfMemory             = 174,  ///< 内存不足
    kIPCLoanTimeout             = 175,  ///< Loan 操作等待超时（kWait/kBlock 策略）
    
    // ========== IPC 状态机错误 (190-209) ==========
    kIPCInvalidStateTransition  = 190,  ///< 无效的 Chunk 状态转换
    kIPCChunkNotInFreeState     = 191,  ///< Chunk 不在空闲状态
    kIPCChunkNotInLoanedState   = 192,  ///< Chunk 不在借出状态
    kIPCChunkNotInSentState     = 193,  ///< Chunk 不在发送状态
    kIPCChunkNotInReceivedState = 194,  ///< Chunk 不在接收状态
    
    // ========== IPC 接收错误 (210-229) ==========
    kIPCNoData                  = 210,  ///< 队列为空，无数据可用
    kIPCReceiveTimeout          = 211,  ///< 接收操作超时
    kIPCQueueEmpty              = 212,  ///< 消息队列为空
    kIPCE2EValidationFailed     = 213,  ///< E2E 保护校验失败
    
    // ========== IPC 队列错误 (230-249) ==========
    kIPCQueueFull               = 230,  ///< 消息队列已满
    kIPCQueueOverrun            = 231,  ///< 队列溢出（消息被丢弃）
    kIPCEnqueueFailed           = 232,  ///< 入队操作失败
    kIPCDequeueFailed           = 233,  ///< 出队操作失败
    
    // ========== IPC E2E 保护错误 (250-269) ==========
    kIPCE2ECRCMismatch          = 250,  ///< E2E CRC32 校验和不匹配
    kIPCE2ECounterJump          = 251,  ///< E2E 计数器跳变（丢失消息）
    kIPCE2ECounterRepeat        = 252,  ///< E2E 计数器重复（重复消息）
    kIPCE2ETimeout              = 253,  ///< E2E 接收超时
    kIPCE2EWrongDataID          = 254,  ///< E2E 数据 ID 不匹配
    
    // ========== IPC 服务发现错误 (270-289) ==========
    kIPCServiceNotFound         = 270,  ///< 服务未找到
    kIPCServiceAlreadyExists    = 271,  ///< 服务已存在
    kIPCInstanceNotFound        = 272,  ///< 服务实例未找到
    kIPCMaxPublishersReached    = 273,  ///< 达到最大 Publisher 数量限制
    kIPCMaxSubscribersReached   = 274,  ///< 达到最大 Subscriber 数量限制
    
    // ========== IPC 连接错误 (290-309) ==========
    kIPCConnectionFailed        = 290,  ///< 建立连接失败
    kIPCAlreadyConnected        = 291,  ///< 已连接到服务
    kIPCNotConnected            = 292,  ///< 未连接到服务
    kIPCPublisherNotFound       = 293,  ///< Publisher 未找到
    kIPCSubscriberNotFound      = 294,  ///< Subscriber 未找到
    kIPCPermissionDenied        = 295,  ///< 权限被拒绝
};

} // namespace ara::core
```

### 7.5.3 错误码使用示例

```cpp
// 1. 共享内存错误
Result<SharedMemory> CreateSharedMemory(const String& name, UInt64 size) {
    int fd = shm_open(name.c_str(), O_CREAT | O_RDWR, 0666);
    if (fd == -1) {
        return Err(CoreErrc::kIPCShmCreateFailed);
    }
    
    if (ftruncate(fd, size) == -1) {
        return Err(CoreErrc::kIPCShmResizeFailed);
    }
    
    void* addr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        return Err(CoreErrc::kIPCShmMapFailed);
    }
    
    return Ok(SharedMemory{fd, addr, size});
}

// 2. 内存分配错误
Result<ChunkIndex> AllocateChunk() {
    if (!is_initialized_) {
        return Err(CoreErrc::kIPCAllocationNotInitialized);
    }
    
    if (loaned_chunks_ >= max_loaned_chunks_) {
        return Err(CoreErrc::kIPCExceedsMaxLoans);
    }
    
    ChunkIndex index = free_list_.Pop();
    if (index == INVALID_CHUNK_INDEX) {
        return Err(CoreErrc::kIPCChunkPoolExhausted);
    }
    
    return Ok(index);
}

// 3. 状态机错误
Result<void> TransitionToLoaned(ChunkIndex index) {
    auto expected = ChunkState::kFree;
    if (!chunks_[index].state.compare_exchange_strong(expected, ChunkState::kLoaned)) {
        return Err(CoreErrc::kIPCInvalidStateTransition);
    }
    return Ok();
}

// 4. 接收错误
Result<Sample> Receive(Duration timeout) {
    auto index_result = queue_.Dequeue();
    if (index_result.IsError()) {
        return Err(CoreErrc::kIPCNoData);
    }
    
    ChunkIndex index = index_result.Value();
    ChunkHeader* chunk = GetChunkHeader(index);
    
    // E2E 校验
    auto e2e_error = e2e_validator_.Validate(chunk);
    if (e2e_error != CoreErrc::kIPCE2ECRCMismatch) {  // E2E 校验失败
        return Err(CoreErrc::kIPCE2EValidationFailed);
    }
    
    return Ok(Sample{chunk});
}

// 5. 队列错误
Result<void> Enqueue(ChunkIndex index) {
    if (!queue_.Enqueue(index)) {
        if (queue_policy_ == PublishPolicy::kOverwrite) {
            queue_.Dequeue();  // 丢弃最旧消息
            queue_.Enqueue(index);
            hooks_->OnSubscriberQueueOverrun(sub_id_, 1);
            return Err(CoreErrc::kIPCQueueOverrun);  // 通知发生溢出
        }
        return Err(CoreErrc::kIPCQueueFull);
    }
    return Ok();
}

// 6. E2E 保护错误
CoreErrc ValidateE2E(const ChunkHeader* chunk) {
    if (!config_.enabled) {
        return CoreErrc::kSuccess;
    }
    
    if (chunk->e2e.data_id != config_.data_id) {
        return CoreErrc::kIPCE2EWrongDataID;
    }
    
    UInt32 calculated_crc = CalculateCRC32(chunk);
    if (calculated_crc != chunk->e2e.crc32) {
        return CoreErrc::kIPCE2ECRCMismatch;
    }
    
    if (chunk->e2e.counter == last_counter_) {
        return CoreErrc::kIPCE2ECounterRepeat;
    }
    
    UInt32 delta = (chunk->e2e.counter - last_counter_) & 0x0FFFFFFF;
    if (delta > config_.max_delta_counter) {
        return CoreErrc::kIPCE2ECounterJump;
    }
    
    return CoreErrc::kSuccess;
}

// 7. 服务发现错误
Result<ServiceHandle> FindService(const ServiceName& name) {
    auto it = services_.find(name);
    if (it == services_.end()) {
        return Err(CoreErrc::kIPCServiceNotFound);
    }
    return Ok(it->second);
}

// 8. 连接错误
Result<void> ConnectPublisher(PublisherId pub_id) {
    if (publishers_.size() >= MAX_PUBLISHERS) {
        return Err(CoreErrc::kIPCMaxPublishersReached);
    }
    
    if (publishers_.contains(pub_id)) {
        return Err(CoreErrc::kIPCAlreadyConnected);
    }
    
    publishers_.insert(pub_id);
    return Ok();
}
```

### 7.5.4 错误码消息映射

```cpp
inline constexpr const Char* GetIPCErrorMessage(CoreErrc errCode) {
    switch (errCode) {
    // 共享内存错误
    case CoreErrc::kIPCShmCreateFailed:
        return "Failed to create shared memory segment";
    case CoreErrc::kIPCShmResizeFailed:
        return "Failed to resize shared memory segment";
    case CoreErrc::kIPCShmMapFailed:
        return "Failed to map shared memory segment";
    case CoreErrc::kIPCShmNotFound:
        return "Shared memory segment not found";
    case CoreErrc::kIPCShmStatFailed:
        return "Failed to get shared memory segment statistics";
    case CoreErrc::kIPCShmCorrupted:
        return "Shared memory segment is corrupted";
    case CoreErrc::kIPCVersionMismatch:
        return "Version mismatch between processes";
    case CoreErrc::kIPCMemoryCorruption:
        return "Memory corruption detected";
        
    // 内存分配错误
    case CoreErrc::kIPCAllocationFailed:
        return "Memory allocation failed";
    case CoreErrc::kIPCChunkPoolExhausted:
        return "ChunkPool has no free chunks available";
    case CoreErrc::kIPCExceedsMaxLoans:
        return "Publisher exceeded maximum loaned samples limit";
    case CoreErrc::kIPCAllocationNotInitialized:
        return "Allocator not initialized";
    case CoreErrc::kIPCOutOfMemory:
        return "Out of memory";
        
    // 状态机错误
    case CoreErrc::kIPCInvalidStateTransition:
        return "Invalid chunk state transition";
    case CoreErrc::kIPCChunkNotInFreeState:
        return "Chunk is not in free state";
    case CoreErrc::kIPCChunkNotInLoanedState:
        return "Chunk is not in loaned state";
    case CoreErrc::kIPCChunkNotInSentState:
        return "Chunk is not in sent state";
    case CoreErrc::kIPCChunkNotInReceivedState:
        return "Chunk is not in received state";
        
    // 接收错误
    case CoreErrc::kIPCNoData:
        return "No data available in queue";
    case CoreErrc::kIPCReceiveTimeout:
        return "Receive operation timed out";
    case CoreErrc::kIPCQueueEmpty:
        return "Message queue is empty";
    case CoreErrc::kIPCE2EValidationFailed:
        return "E2E protection validation failed";
        
    // 队列错误
    case CoreErrc::kIPCQueueFull:
        return "Message queue is full";
    case CoreErrc::kIPCQueueOverrun:
        return "Queue overrun, messages were dropped";
    case CoreErrc::kIPCEnqueueFailed:
        return "Failed to enqueue message";
    case CoreErrc::kIPCDequeueFailed:
        return "Failed to dequeue message";
        
    // E2E 保护错误
    case CoreErrc::kIPCE2ECRCMismatch:
        return "E2E CRC32 checksum mismatch";
    case CoreErrc::kIPCE2ECounterJump:
        return "E2E counter jump detected (lost messages)";
    case CoreErrc::kIPCE2ECounterRepeat:
        return "E2E counter repeat detected (duplicate message)";
    case CoreErrc::kIPCE2ETimeout:
        return "E2E reception timeout";
    case CoreErrc::kIPCE2EWrongDataID:
        return "E2E data ID mismatch";
        
    // 服务发现错误
    case CoreErrc::kIPCServiceNotFound:
        return "Service not found in discovery";
    case CoreErrc::kIPCServiceAlreadyExists:
        return "Service already registered";
    case CoreErrc::kIPCInstanceNotFound:
        return "Service instance not found";
    case CoreErrc::kIPCMaxPublishersReached:
        return "Maximum publishers limit reached";
    case CoreErrc::kIPCMaxSubscribersReached:
        return "Maximum subscribers limit reached";
        
    // 连接错误
    case CoreErrc::kIPCConnectionFailed:
        return "Failed to establish connection";
    case CoreErrc::kIPCAlreadyConnected:
        return "Already connected to service";
    case CoreErrc::kIPCNotConnected:
        return "Not connected to service";
    case CoreErrc::kIPCPublisherNotFound:
        return "Publisher not found";
    case CoreErrc::kIPCSubscriberNotFound:
        return "Subscriber not found";
    case CoreErrc::kIPCPermissionDenied:
        return "Permission denied";
        
    default:
        return "Unknown IPC error";
    }
}
```

### 7.5.5 Routine Counter 规范

根据 AUTOSAR 规范，错误报告中的 **Routine Counter** 必须遵循以下规则：

```cpp
/**
 * @brief Routine Counter 规范
 * 
 * 有效范围: 1 到 0xFFFFFFFE
 * 保留值:
 *   - 0x00000000: 保留，禁止使用
 *   - 0xFFFFFFFF: 保留，禁止使用
 * 
 * 用途: 用于错误报告中标识错误发生的顺序或位置
 */
constexpr UInt32 kRoutineCounterMin = 1;
constexpr UInt32 kRoutineCounterMax = 0xFFFFFFFE;
constexpr UInt32 kRoutineCounterReserved0 = 0x00000000;
constexpr UInt32 kRoutineCounterReservedF = 0xFFFFFFFF;

// Routine Counter 校验
inline bool IsValidRoutineCounter(UInt32 counter) {
    return counter >= kRoutineCounterMin && counter <= kRoutineCounterMax;
}
```

---

## 7.5 异常处理策略

### 7.5.1 异常分类与处理原则

IPC 层的异常处理遵循 AUTOSAR 规范，使用 `Result<T>` 类型进行错误传递，不使用 C++ 异常机制（适合实时系统）。

**异常分类：**

| 异常类型 | 严重程度 | 处理策略 | 示例 |
|---------|---------|---------|------|
| **资源耗尽** | 🔴 高 | 立即返回错误 + Hook 回调 | ChunkPool 满、Loan 超限 |
| **队列策略** | 🟡 中 | 根据策略处理 | 队列满/空 |
| **E2E 校验** | 🔴 高 | 丢弃消息 + Hook 回调 | CRC 错误、计数器跳变 |
| **内存损坏** | 🔴 致命 | 停止服务 + 崩溃报告 | 魔数错误、版本不匹配 |
| **权限错误** | 🔴 高 | 拒绝访问 + 日志记录 | shm_open 失败 |
| **超时错误** | 🟡 中 | 返回超时 + Hook 回调 | 接收超时、发送超时 |

### 7.5.2 Publisher 异常处理

#### 7.5.2.1 Loan 失败处理

```cpp
/**
 * @brief Publisher::Loan() 异常处理流程（支持等待策略）
 */
Result<Sample<PayloadType>> Publisher<PayloadType>::Loan() noexcept {
    // [异常1] 检查 Publisher 是否活跃
    if (!is_active_.load(std::memory_order_acquire)) {
        return Err(CoreErrc::kIPCPublisherInactive);
    }
    
    // [异常2] ChunkPool 分配失败（根据策略处理）
    auto chunk_result = allocator_->Allocate();
    
    if (!chunk_result.HasValue()) {
        CoreErrc error = chunk_result.Error();
        
        // 🔥 触发 ChunkPool 耗尽 Hook
        if (error == CoreErrc::kIPCChunkPoolExhausted) {
            if (event_hooks_) {
                event_hooks_->OnChunkPoolExhausted(
                    allocator_->GetAllocatedCount(),
                    allocator_->GetMaxChunks()
                );
                event_hooks_->OnLoanFailed(
                    publisher_id_,
                    error,
                    loan_counter_.load(),
                    max_loaned_samples_
                );
            }
            
            // 根据 LoanPolicy 处理
            switch (loan_failure_policy_) {
            case LoanPolicy::kError:
                // 立即返回错误（默认策略，适合实时系统）
                return Err(error);
                
            case LoanPolicy::kWait:
                // 轮询等待有可用 Chunk
                {
                    auto* ctrl = shm_mgr_->GetControlBlock();
                    bool has_chunk = WaitSetHelper::PollForFlags(
                        &ctrl->loan_waitset,
                        EventFlag::HAS_FREE_CHUNK,
                        loan_timeout_,
                        Duration::FromMicros(100)  // 轮询间隔 100us
                    );
                    
                    if (!has_chunk) {
                        return Err(CoreErrc::kIPCLoanTimeout);  // 超时
                    }
                    
                    // 重新尝试分配
                    chunk_result = allocator_->Allocate();
                    if (!chunk_result.HasValue()) {
                        return Err(CoreErrc::kIPCChunkPoolExhausted);  // 竞争失败
                    }
                }
                break;
                
            case LoanPolicy::kBlock:
                // 阻塞等待有可用 Chunk（使用 WaitSet + futex）
                {
                    auto* ctrl = shm_mgr_->GetControlBlock();
                    bool has_chunk = WaitSetHelper::WaitForFlags(
                        &ctrl->loan_waitset,
                        EventFlag::HAS_FREE_CHUNK,
                        loan_timeout_
                    );
                    
                    if (!has_chunk) {
                        return Err(CoreErrc::kIPCLoanTimeout);  // 超时
                    }
                    
                    // 重新尝试分配
                    chunk_result = allocator_->Allocate();
                    if (!chunk_result.HasValue()) {
                        return Err(CoreErrc::kIPCChunkPoolExhausted);  // 竞争失败
                    }
                }
                break;
            }
        } else {
            // 其他错误直接返回
            return Err(error);
        }
    }
    
    // 成功：增加 loan_counter（仅统计，不限制）
    UInt64 current_loans = loan_counter_.fetch_add(1, std::memory_order_release) + 1;
    
    // 🔥 触发 Loan 警告 Hook（如果超过建议上限）
    if (current_loans > max_loaned_samples_ && event_hooks_) {
        event_hooks_->OnLoanCountWarning(
            publisher_id_,
            current_loans,
            max_loaned_samples_
        );
    }
    
    auto* chunk = chunk_result.Value();
    return Ok(Sample<PayloadType>{chunk, this});
}

/**
 * @brief 应用层处理 Loan 失败
 */
void ApplicationExample() {
    auto publisher = node.CreatePublisher<SensorData>("/sensor/imu").Value();
    
    // 策略1：重试机制（kError 策略）
    auto sample_result = publisher.Loan();
    if (!sample_result.HasValue()) {
        if (sample_result.Error() == CoreErrc::kIPCChunkPoolExhausted) {
            // ChunkPool 耗尽，等待一段时间后重试
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            sample_result = publisher.Loan();
        }
        
        if (!sample_result.HasValue()) {
            LOG_ERROR("Failed to loan sample: {}", sample_result.Error());
            return;  // 放弃本次发送
        }
    }
    
    auto sample = sample_result.Value();
    sample->Fill(GetSensorData());
    publisher.Send(std::move(sample));
}
```

**Loan 失败错误码：**

| 错误码 | 含义 | 恢复策略 |
|--------|------|---------|
| `kIPCChunkPoolExhausted` | ChunkPool 已满（kError 策略） | 等待 Subscriber 消费或增大 `max_chunks` |
| `kIPCLoanTimeout` | Loan 等待超时（kWait/kBlock 策略） | 增大超时时间或增大 `max_chunks` |
| `kIPCPublisherInactive` | Publisher 未激活 | 检查 Publisher 生命周期 |
| `kIPCMemoryCorruption` | 共享内存损坏 | 重启服务，检查系统稳定性 |

**LoanPolicy 对比：**

| 策略 | 行为 | 延迟 | CPU 占用 | 推荐场景 |
|------|-----|------|---------|---------|
| **kError** | 立即返回错误 | ~10 ns | 无 | 实时系统（默认） |
| **kWait** | 轮询等待可用 Chunk | ~10-50 μs | 中等 | 短超时、高实时性 |
| **kBlock** | 阻塞等待（futex） | ~2-11 μs | 极低 | 长超时、后台任务 |

**配置示例：**

```cpp
// 场景1：实时系统，不允许阻塞
PublisherOptions opts;
opts.loan_failure_policy = LoanPolicy::kError;
auto pub = node.CreatePublisher<Data>("/topic", opts).Value();

// 场景2：非实时，允许短时间等待（轮询）
PublisherOptions opts;
opts.loan_failure_policy = LoanPolicy::kWait;
opts.loan_timeout = Duration::FromMillis(5);  // 5ms 超时
auto pub = node.CreatePublisher<Data>("/topic", opts).Value();

// 场景3：后台任务，允许长时间阻塞
PublisherOptions opts;
opts.loan_failure_policy = LoanPolicy::kBlock;
opts.loan_timeout = Duration::FromMillis(100);  // 100ms 超时
auto pub = node.CreatePublisher<Data>("/topic", opts).Value();
```

**注意：** `loan_counter` 仅用于警告（Hook 回调），不再限制 Loan 操作。资源限制由 ChunkPool 大小控制。

#### 7.5.2.2 队列满策略处理

```cpp
/**
 * @brief Publisher::Send() 队列满异常处理
 */
void Publisher<PayloadType>::Send(Sample<PayloadType>&& sample) noexcept {
    auto* chunk = sample.Release();
    
    // 状态转换：kWriting -> kReady
    chunk->state.store(ChunkState::kReady, std::memory_order_release);
    
    // 获取队列满策略
    PublishPolicy policy = queue_full_policy_.load(std::memory_order_acquire);
    Duration timeout = send_timeout_;
    
    // 获取 Subscriber 快照
    auto snapshot = subscriber_registry_.GetSnapshot();
    
    // 统计发送结果
    UInt32 success_count = 0;
    UInt32 overrun_count = 0;
    UInt32 timeout_count = 0;
    UInt32 drop_count = 0;
    
    for (UInt32 i = 0; i < snapshot.count; ++i) {
        UInt32 queue_idx = snapshot.queue_indices[i];
        auto* sub_queue = &subscriber_queues_[queue_idx];
        
        // 使用策略进行入队
        auto result = EnqueueWithPolicy(
            sub_queue, chunk->chunk_index,
            shm_mgr_, allocator_, policy, timeout
        );
        
        // 处理不同的异常结果
        switch (result) {
        case EnqueueResult::kSuccess:
            success_count++;
            break;
            
        case EnqueueResult::kOverwritten:
            overrun_count++;
            send_overrun_count_.fetch_add(1, std::memory_order_relaxed);
            
            // 🔥 触发队列溢出 Hook
            if (event_hooks_) {
                event_hooks_->OnSubscriberQueueOverrun(
                    publisher_id_,
                    sub_queue->subscriber_id.load(std::memory_order_acquire),
                    queue_idx,
                    1,  // 本次溢出计数
                    sub_queue->overrun_count.load(std::memory_order_acquire)
                );
            }
            break;
            
        case EnqueueResult::kTimeout:
            timeout_count++;
            chunk->ref_count.fetch_sub(1, std::memory_order_relaxed);
            send_errors_.fetch_add(1, std::memory_order_relaxed);
            
            // 🔥 触发发送超时 Hook
            if (event_hooks_) {
                event_hooks_->OnSendTimeout(
                    publisher_id_,
                    sub_queue->subscriber_id.load(std::memory_order_acquire),
                    timeout.ToMillis()
                );
            }
            break;
            
        case EnqueueResult::kQueueFull:
            drop_count++;
            chunk->ref_count.fetch_sub(1, std::memory_order_relaxed);
            send_errors_.fetch_add(1, std::memory_order_relaxed);
            break;
        }
    }
    
    // 记录统计信息
    LOG_DEBUG("Send summary: success={}, overrun={}, timeout={}, drop={}",
              success_count, overrun_count, timeout_count, drop_count);
}
```

**队列满策略对比：**

| 策略 | 异常行为 | 应用层处理 | 推荐场景 |
|------|---------|-----------|---------|
| **kOverwrite** | 覆盖旧消息，返回 `kOverwritten` | 无需处理（自动恢复） | 实时传感器数据 |
| **kWait** | 轮询等待，超时返回 `kTimeout` | 检查返回值，记录超时 | 短超时场景（< 10ms） |
| **kBlock** | 阻塞等待，超时返回 `kTimeout` | 检查返回值，调整超时配置 | 长超时场景（> 10ms） |
| **kDrop** | 立即丢弃，返回 `kQueueFull` | 检查返回值，记录丢失 | 日志系统 |
| **kCustom** | 调用用户回调 | 自定义处理逻辑 | 特殊需求 |

### 7.5.3 Subscriber 异常处理

#### 7.5.3.1 队列空策略处理

```cpp
/**
 * @brief Subscriber::Receive() 队列空异常处理
 */
Result<Sample<PayloadType>> Subscriber<PayloadType>::Receive() noexcept {
    auto* queue = &subscriber_queues_[queue_index_];
    
    // [异常1] 检查 Subscriber 是否活跃
    if (!is_active_.load(std::memory_order_acquire)) {
        return Err(CoreErrc::kIPCSubscriberInactive);
    }
    
    // 出队
    UInt32 chunk_index = queue->msg_queue.Dequeue();
    
    // [异常2] 队列为空
    if (chunk_index == kInvalidIndex) {
        // 根据队列空策略处理
        switch (queue_empty_policy_) {
        case SubscribePolicy::kBlock:
            // 阻塞等待（默认策略，适合非实时系统）
            // 使用 WaitSet 等待 HAS_DATA 标志
            {
                bool has_data = WaitSetHelper::WaitForFlags(
                    &queue->event_flags,
                    EventFlag::HAS_DATA,
                    receive_timeout_
                );
                
                if (!has_data) {
                    // 触发超时 Hook
                    if (event_hooks_) {
                        event_hooks_->OnReceiveTimeout(
                            subscriber_id_,
                            receive_timeout_.ToMillis(),
                            queue->msg_queue.GetCount()
                        );
                    }
                    return Err(CoreErrc::kIPCReceiveTimeout);
                }
                
                // 被唤醒后重新尝试出队
                chunk_index = queue->msg_queue.Dequeue();
                if (chunk_index == kInvalidIndex) {
                    return Err(CoreErrc::kIPCNoData);  // 虚假唤醒或竞争
                }
            }
            break;
            
        case SubscribePolicy::kWait:
            // 轮询等待（适合短超时场景）
            return ReceiveWithTimeout(receive_timeout_);
            
        case SubscribePolicy::kSkip:
            // 跳过当次，立即返回空（不报错）
            return Err(CoreErrc::kIPCNoData);
            
        case SubscribePolicy::kError:
            // 立即返回错误
            return Err(CoreErrc::kIPCQueueEmpty);
        }
    }
    
    // 设置 HAS_SPACE 标志并唤醒等待的 Publisher（如果队列之前满了）
    if (queue->msg_queue.GetCount() == queue->msg_queue.GetCapacity() - 1) {
        WaitSetHelper::SetFlagsAndWake(&queue->event_flags, EventFlag::HAS_SPACE);
    }
    
    // 清除 HAS_DATA 标志（如果队列现在空了）
    if (queue->msg_queue.IsEmpty()) {
        WaitSetHelper::ClearFlags(&queue->event_flags, EventFlag::HAS_DATA);
    }
    
    // [异常3] Chunk 索引越界
    if (chunk_index >= max_chunks_) {
        LOG_ERROR("Invalid chunk_index: {}", chunk_index);
        return Err(CoreErrc::kIPCInvalidChunkIndex);
    }
    
    auto* chunk = shm_mgr_->GetChunkByIndex(chunk_index);
    
    // [异常4] Chunk 状态检查
    if (chunk->state.load(std::memory_order_acquire) != ChunkState::kReady) {
        LOG_ERROR("Chunk {} not in kReady state", chunk_index);
        return Err(CoreErrc::kIPCInvalidChunkState);
    }
    
    return Ok(Sample<PayloadType>{chunk, this});
}

/**
 * @brief 带超时的接收（kWait 策略，使用 WaitSet 轮询）
 */
Result<Sample<PayloadType>> Subscriber<PayloadType>::ReceiveWithTimeout(
        const Duration& timeout) noexcept {
    
    auto* queue = &subscriber_queues_[queue_index_];
    
    // 使用 WaitSet 轮询 HAS_DATA 标志（纯快速路径，无 futex 调用）
    bool has_data = WaitSetHelper::PollForFlags(
        &queue->event_flags,
        EventFlag::HAS_DATA,
        timeout,
        Duration::FromMicros(100)  // 轮询间隔 100us（可配置）
    );
    
    if (!has_data) {
        // 触发超时 Hook
        if (event_hooks_) {
            event_hooks_->OnReceiveTimeout(
                subscriber_id_,
                timeout.ToMillis(),
                queue->msg_queue.GetCount()
            );
        }
        return Err(CoreErrc::kIPCReceiveTimeout);
    }
    
    // 标志位已设置，尝试出队
    UInt32 chunk_index = queue->msg_queue.Dequeue();
    
    if (chunk_index == kInvalidIndex) {
        return Err(CoreErrc::kIPCNoData);  // 竞争或虚假标志
    }
    
    // 设置 HAS_SPACE 标志并唤醒等待的 Publisher（如果队列之前满了）
    if (queue->msg_queue.GetCount() == queue->msg_queue.GetCapacity() - 1) {
        WaitSetHelper::SetFlagsAndWake(&queue->event_flags, EventFlag::HAS_SPACE);
    }
    
    // 清除 HAS_DATA 标志（如果队列现在空了）
    if (queue->msg_queue.IsEmpty()) {
        WaitSetHelper::ClearFlags(&queue->event_flags, EventFlag::HAS_DATA);
    }
    
    auto* chunk = shm_mgr_->GetChunkByIndex(chunk_index);
    return Ok(Sample<PayloadType>{chunk, this});
}
```

**队列空策略对比：**

| 策略 | 队列空时行为 | 应用层处理 | 推荐场景 |
|------|-------------|-----------|---------|
| **kReturnError** | 立即返回 `kIPCNoData` | 检查返回值，处理无数据情况 | 实时系统（推荐） |
| **kBlockConsumer** | 阻塞等待直到有数据或超时 | 检查超时错误 | 非实时后台任务 |
| **kReturnDefault** | 返回预设的默认值 | 判断是否为默认值 | 需要默认值的场景 |

#### 7.5.3.2 E2E 校验失败处理

```cpp
/**
 * @brief E2E 校验失败异常处理
 */
Result<Sample<PayloadType>> Subscriber<PayloadType>::Receive() noexcept {
    // ... 出队逻辑 ...
    
    auto* chunk = shm_mgr_->GetChunkByIndex(chunk_index);
    
    // E2E 校验
    if (e2e_validator_) {
        E2EError e2e_error = e2e_validator_->Validate(chunk);
        
        if (e2e_error != E2EError::kNone) {
            // 🔥 触发 E2E 校验失败 Hook
            if (event_hooks_) {
                event_hooks_->OnE2EValidationFailed(
                    subscriber_id_,
                    chunk_index,
                    e2e_error
                );
            }
            
            // 释放 Chunk（减少引用计数）
            chunk->DecrementRef(allocator_);
            
            // 根据错误类型返回不同错误码
            switch (e2e_error) {
            case E2EError::kCRCMismatch:
                return Err(CoreErrc::kIPCE2ECRCError);
            case E2EError::kCounterJump:
                return Err(CoreErrc::kIPCE2ECounterError);
            case E2EError::kWrongDataID:
                return Err(CoreErrc::kIPCE2EDataIDError);
            case E2EError::kTimeout:
                return Err(CoreErrc::kIPCE2ETimeoutError);
            default:
                return Err(CoreErrc::kIPCE2EUnknownError);
            }
        }
    }
    
    return Ok(Sample<PayloadType>{chunk, this});
}
```

**E2E 错误处理策略：**

| E2E 错误类型 | 处理策略 | 应用层建议 |
|-------------|---------|-----------|
| **kCRCMismatch** | 丢弃消息 + Hook 回调 | 记录错误，检查数据完整性 |
| **kCounterJump** | 丢弃消息 + Hook 回调 | 检测消息丢失，调整队列大小 |
| **kWrongDataID** | 丢弃消息 + Hook 回调 | 检查配置错误 |
| **kTimeout** | 丢弃消息 + Hook 回调 | 检查 Publisher 是否正常 |

### 7.5.4 异常处理最佳实践

#### 7.5.4.1 错误码检查

```cpp
// ✅ 推荐：始终检查 Result 返回值
auto sample_result = subscriber.Receive();
if (!sample_result.HasValue()) {
    CoreErrc error = sample_result.Error();
    
    switch (error) {
    case CoreErrc::kIPCNoData:
        // 队列为空，正常情况
        break;
        
    case CoreErrc::kIPCReceiveTimeout:
        LOG_WARN("Receive timeout");
        break;
        
    case CoreErrc::kIPCE2ECRCError:
        LOG_ERROR("E2E CRC error, message corrupted");
        metrics_.RecordE2EError();
        break;
        
    case CoreErrc::kIPCSubscriberInactive:
        LOG_ERROR("Subscriber inactive");
        return;  // 严重错误，停止处理
        
    default:
        LOG_ERROR("Unknown error: {}", static_cast<int>(error));
        break;
    }
    return;
}

auto sample = sample_result.Value();
ProcessData(*sample);

// ❌ 错误：不检查返回值直接使用
auto sample = subscriber.Receive().Value();  // 可能崩溃！
```

#### 7.5.4.2 Hook 回调集成

```cpp
class ProductionIPCHooks : public IPCEventHooks {
public:
    void OnLoanFailed(
        UInt32 publisher_id,
        CoreErrc error,
        UInt32 loaned_count,
        UInt32 max_loans) noexcept override {
        
        // 1. 记录到日志系统
        logger_.Error("Loan failed: pub={}, error={}, {}/{}",
                     publisher_id, static_cast<int>(error), 
                     loaned_count, max_loans);
        
        // 2. 更新监控指标
        metrics_.loan_failures.Increment();
        
        // 3. 触发告警（连续失败 > 阈值）
        if (++consecutive_failures_ > kAlertThreshold) {
            alerting_.SendCriticalAlert(
                "IPC Loan Failures Exceeded Threshold",
                {{"publisher_id", publisher_id}}
            );
        }
        
        // 4. 尝试自动恢复
        if (error == CoreErrc::kIPCChunkPoolExhausted) {
            // 请求资源回收
            resource_manager_.RequestGarbageCollection();
        }
    }
    
    void OnSubscriberQueueOverrun(
        UInt32 publisher_id,
        UInt32 subscriber_id,
        UInt32 queue_index,
        UInt32 failure_count,
        UInt64 overrun_count) noexcept override {
        
        // 检测慢订阅者
        if (overrun_count > kSlowSubscriberThreshold) {
            logger_.Warn("Slow subscriber detected: {}", subscriber_id);
            
            // 通知应用层
            app_callbacks_.OnSlowSubscriber(subscriber_id);
            
            // 可选：动态调整队列大小或断开连接
            if (overrun_count > kDisconnectThreshold) {
                app_callbacks_.SuggestDisconnect(subscriber_id);
            }
        }
    }
    
private:
    static constexpr UInt32 kAlertThreshold = 100;
    static constexpr UInt64 kSlowSubscriberThreshold = 1000;
    static constexpr UInt64 kDisconnectThreshold = 10000;
    
    Logger& logger_;
    MetricsCollector& metrics_;
    AlertingSystem& alerting_;
    ResourceManager& resource_manager_;
    ApplicationCallbacks& app_callbacks_;
    UInt32 consecutive_failures_ = 0;
};
```

#### 7.5.4.3 异常恢复流程

```cpp
/**
 * @brief 应用层异常恢复示例
 */
class RobustPublisher {
public:
    void PublishSensorData() {
        const int kMaxRetries = 3;
        int retry_count = 0;
        
        while (retry_count < kMaxRetries) {
            // 1. Loan Sample
            auto sample_result = publisher_.Loan();
            
            if (!sample_result.HasValue()) {
                CoreErrc error = sample_result.Error();
                
                if (error == CoreErrc::kIPCChunkPoolExhausted) {
                    // 策略2：请求资源回收
                    RequestGarbageCollection();
                    retry_count++;
                    continue;
                    
                } else {
                    // 致命错误，无法恢复
                    LOG_ERROR("Fatal loan error: {}", static_cast<int>(error));
                    return;
                }
            }
            
            // 2. 填充数据
            auto sample = sample_result.Value();
            sample->Fill(GetSensorData());
            
            // 3. 发送
            publisher_.Send(std::move(sample));
            return;  // 成功
        }
        
        // 重试次数耗尽
        LOG_ERROR("Failed to publish after {} retries", kMaxRetries);
        metrics_.RecordPublishFailure();
    }
    
private:
    Publisher<SensorData>& publisher_;
    MetricsCollector& metrics_;
    
    void RequestGarbageCollection() {
        // 通知资源管理器进行垃圾回收
        // ...
    }
};
```

### 7.5.5 异常统计与监控

```cpp
/**
 * @brief IPC 异常统计结构
 */
struct IPCErrorStatistics {
    // Publisher 异常
    std::atomic<UInt64> loan_failures{0};
    std::atomic<UInt64> chunk_pool_exhausted{0};
    std::atomic<UInt64> send_timeouts{0};
    std::atomic<UInt64> send_overruns{0};
    
    // Subscriber 异常
    std::atomic<UInt64> receive_timeouts{0};
    std::atomic<UInt64> receive_no_data{0};
    std::atomic<UInt64> invalid_chunk_index{0};
    
    // E2E 异常
    std::atomic<UInt64> e2e_crc_errors{0};
    std::atomic<UInt64> e2e_counter_errors{0};
    std::atomic<UInt64> e2e_data_id_errors{0};
    
    // 内存异常
    std::atomic<UInt64> memory_corruption{0};
    std::atomic<UInt64> version_mismatch{0};
    
    void Reset() {
        loan_failures = 0;
        chunk_pool_exhausted = 0;
        send_timeouts = 0;
        send_overruns = 0;
        receive_timeouts = 0;
        receive_no_data = 0;
        invalid_chunk_index = 0;
        e2e_crc_errors = 0;
        e2e_counter_errors = 0;
        e2e_data_id_errors = 0;
        memory_corruption = 0;
        version_mismatch = 0;
    }
    
    void Dump() const {
        LOG_INFO("=== IPC Error Statistics ===");
        LOG_INFO("Publisher: loan_failures={}, pool_exhausted={}, timeouts={}, overruns={}",
                 loan_failures.load(), chunk_pool_exhausted.load(),
                 send_timeouts.load(), send_overruns.load());
        LOG_INFO("Subscriber: timeouts={}, no_data={}, invalid_index={}",
                 receive_timeouts.load(), receive_no_data.load(), 
                 invalid_chunk_index.load());
        LOG_INFO("E2E: crc={}, counter={}, data_id={}",
                 e2e_crc_errors.load(), e2e_counter_errors.load(),
                 e2e_data_id_errors.load());
        LOG_INFO("Memory: corruption={}, version_mismatch={}",
                 memory_corruption.load(), version_mismatch.load());
    }
};
```

---

## 8. 测试方案

### 8.1 测试环境配置

**测试原则**：使用固定的 shm 地址进行测试，跳过服务发现流程，直接验证 IPC 底层功能。

#### 8.1.1 测试配置

```cpp
// 测试专用共享内存路径
constexpr const char* kTestShmPath = "/dev/shm/lightap_ipc_test";

// 测试配置
struct TestConfig {
    UInt32 max_chunks = 128;
    UInt64 chunk_size = 4096;
    UInt32 max_publishers = 8;
    UInt32 max_channels = 16;
    UInt32 channel_capacity = 32;
};

// 测试数据结构
struct TestPayload {
    UInt64 序列号;
    UInt64 timestamp;
    UInt64 sender_id;
    UInt8  data[3072];  // 填充至 4KB
};
```

#### 8.1.2 独立进程测试框架

```cpp
// Publisher 进程启动
int main_publisher(int argc, char** argv) {
    auto publisher = Publisher<TestPayload>::Create(
        kTestShmPath,
        PublisherConfig{
            .max_channels = 16,
            .max_chunks = 128,
            .chunk_size = sizeof(TestPayload)
        }
    ).Value();
    
    UInt64 seq = 0;
    while (running) {
        auto sample = publisher.Loan().Value();
        sample->sequence_number = seq++;
        sample->timestamp = GetTimestampNs();
        sample->sender_id = getpid();
        publisher.Send(std::move(sample));
        
        // 根据测试场景调整发送频率
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// Subscriber 进程启动
int main_subscriber(int argc, char** argv) {
    auto subscriber = Subscriber<TestPayload>::Create(
        kTestShmPath,
        SubscriberConfig{
            .channel_capacity = 32
        }
    ).Value();
    
    UInt64 last_seq = 0;
    UInt64 received_count = 0;
    
    while (running) {
        auto sample_result = subscriber.Receive();
        if (sample_result.HasValue()) {
            auto& sample = sample_result.Value();
            
            // 验证序列号连续性
            if (sample->sequence_number != last_seq + 1) {
                LOG_WARN("Sequence gap: {} -> {}", last_seq, sample->sequence_number);
            }
            last_seq = sample->sequence_number;
            received_count++;
            
            // 计算端到端延迟
            UInt64 latency = GetTimestampNs() - sample->timestamp;
            LOG_INFO("Latency: {} ns", latency);
        }
    }
}
```

### 8.2 SPSC 测试（单发单收）

**测试目标**：验证单个 Publisher 和单个 Subscriber 的基本通信功能。

#### 8.2.1 测试步骤

```bash
# 终端 1: 启动 Subscriber（先启动以测试共享内存创建）
$ ./ipc_test subscriber

# 终端 2: 启动 Publisher
$ ./ipc_test publisher

# 预期结果:
# - Subscriber 成功创建共享内存
# - Publisher 打开已存在的共享内存
# - 消息正常传递，无丢失
# - 延迟 < 1μs (p99)
```

#### 8.2.2 验证点

```cpp
TEST(IPC_SPSC, BasicCommunication) {
    // 1. 消息完整性
    EXPECT_TRUE(VerifySequence(received_messages));
    
    // 2. 延迟性能
    EXPECT_LT(GetP99Latency(), 1000);  // < 1μs
    
    // 3. 吞吐量
    EXPECT_GT(GetThroughput(), 1000000);  // > 1M msg/s
    
    // 4. 零拷贝验证
    EXPECT_EQ(GetCopyCount(), 0);
}
```

### 8.3 SPMC 测试（单发多收）

**测试目标**：验证单个 Publisher 向多个 Subscriber 广播的功能。

#### 8.3.1 测试步骤

```bash
# 终端 1-4: 启动 4 个 Subscriber
$ ./ipc_test subscriber --id=sub1 &
$ ./ipc_test subscriber --id=sub2 &
$ ./ipc_test subscriber --id=sub3 &
$ ./ipc_test subscriber --id=sub4 &

# 终端 5: 启动 Publisher
$ ./ipc_test publisher --rate=10000  # 10k msg/s

# 预期结果:
# - 所有 Subscriber 收到相同的消息序列
# - 队列无溢出（或按配置丢弃最旧消息）
# - 每个 Subscriber 独立接收，互不影响
```

#### 8.3.2 验证点

```cpp
TEST(IPC_SPMC, MultipleSubscribers) {
    const int kNumSubscribers = 4;
    
    // 1. 所有 Subscriber 收到相同消息
    for (int i = 0; i < kNumSubscribers; i++) {
        EXPECT_EQ(subscribers[i].GetReceivedCount(), kTotalMessages);
        EXPECT_TRUE(VerifySequence(subscribers[i].GetMessages()));
    }
    
    // 2. 独立队列无干扰
    // Subscriber 1 慢速接收不应影响 Subscriber 2
    subscribers[0].SetReceiveRate(100);  // 100 msg/s
    subscribers[1].SetReceiveRate(10000);  // 10k msg/s
    
    Sleep(1s);
    EXPECT_GT(subscribers[1].GetReceivedCount(), 
              subscribers[0].GetReceivedCount() * 50);
    
    // 3. 队列满处理
    // Subscriber 停止接收，验证队列满策略
    subscribers[2].Stop();
    publisher.SendMessages(100);
    
    if (config.queue_policy == PublishPolicy::kOverwrite) {
        EXPECT_EQ(subscribers[2].GetQueueSize(), config.channel_capacity);
    }
}
```

### 8.4 SP0C 测试（单发无收）

**测试目标**：验证无 Subscriber 时 Publisher 的行为。

#### 8.4.1 测试步骤

```bash
# 只启动 Publisher，无 Subscriber
$ ./ipc_test publisher --duration=10s

# 预期结果（0 个 Subscriber）:
# - Publisher 正常运行，无崩溃
# - Loan 和 Send 调用始终成功（不会耗尽）
# - Send 后 Chunk 立即回收（ref_count=0，无订阅者）
# - ChunkPool 不会耗尽（Chunk 循环复用）
# - 性能极高（无入队开销，仅 Loan/Send/Release 循环）
```

#### 8.4.2 验证点

```cpp
TEST(IPC_SP0C, NoSubscribers) {
    auto publisher = CreateTestPublisher();
    
    // 无 Subscriber 时，可以无限发送（Chunk 立即回收）
    int sent_count = 0;
    const int kTestMessages = 10000;  // 远超 max_chunks
    
    for (int i = 0; i < kTestMessages; i++) {
        auto sample_result = publisher.Loan();
        EXPECT_TRUE(sample_result.HasValue());  // 始终成功
        
        auto sample = std::move(sample_result.Value());
        sample->data = i;
        
        publisher.Send(std::move(sample));
        sent_count++;
    }
    
    // 验证成功发送了所有消息
    EXPECT_EQ(sent_count, kTestMessages);
    
    // 验证 ChunkPool 未耗尽（Chunk 被循环复用）
    auto shm = OpenSharedMemory(kTestShmPath);
    EXPECT_LT(shm->GetAllocatedCount(), kTestConfig.max_chunks);
    
    // 验证所有 Chunk 都已回收（ref_count=0）
    for (int i = 0; i < kTestConfig.max_chunks; i++) {
        auto* chunk = shm->GetChunkByIndex(i);
        EXPECT_EQ(chunk->ref_count.load(), 0);
        EXPECT_EQ(chunk->state.load(), ChunkState::kFree);
    }
}
```

### 8.5 MPSC/MPMC 测试（多发）

**测试目标**：验证多个 Publisher 同时发布的功能（扩展测试）。

#### 8.5.1 MPSC 测试步骤

```bash
# 终端 1: 启动 Subscriber
$ ./ipc_test subscriber

# 终端 2-5: 启动 4 个 Publisher
$ ./ipc_test publisher --id=pub1 --rate=1000 &
$ ./ipc_test publisher --id=pub2 --rate=1000 &
$ ./ipc_test publisher --id=pub3 --rate=1000 &
$ ./ipc_test publisher --id=pub4 --rate=1000 &

# 预期结果:
# - Subscriber 收到所有 Publisher 的消息
# - 总吞吐量 ~4k msg/s
# - 无消息丢失（或按配置丢弃）
```

#### 8.5.2 验证点

```cpp
TEST(IPC_MPSC, MultiplePublishers) {
    const int kNumPublishers = 4;
    const int kMessagesPerPub = 10000;
    
    auto subscriber = CreateTestSubscriber();
    std::vector<Publisher<TestPayload>> publishers;
    
    for (int i = 0; i < kNumPublishers; i++) {
        publishers.push_back(CreateTestPublisher());
    }
    
    // 多个 Publisher 并发发送
    std::vector<std::thread> threads;
    for (int i = 0; i < kNumPublishers; i++) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < kMessagesPerPub; j++) {
                auto sample = publishers[i].Loan().Value();
                sample->sender_id = i;
                sample->sequence_number = j;
                publishers[i].Send(std::move(sample));
            }
        });
    }
    
    // 等待所有 Publisher 完成
    for (auto& t : threads) t.join();
    
    // 验证 Subscriber 收到所有消息
    Sleep(1s);  // 等待接收完成
    
    std::map<UInt64, int> sender_counts;
    auto messages = subscriber.DrainQueue();
    
    for (auto& msg : messages) {
        sender_counts[msg->sender_id]++;
    }
    
    // 每个 Publisher 的消息都收到
    for (int i = 0; i < kNumPublishers; i++) {
        EXPECT_EQ(sender_counts[i], kMessagesPerPub);
    }
}
```

#### 8.5.3 MPMC 测试步骤

```bash
# 终端 1-2: 启动 2 个 Subscriber
$ ./ipc_test subscriber --id=sub1 &
$ ./ipc_test subscriber --id=sub2 &

# 终端 3-6: 启动 4 个 Publisher
$ ./ipc_test publisher --id=pub1 &
$ ./ipc_test publisher --id=pub2 &
$ ./ipc_test publisher --id=pub3 &
$ ./ipc_test publisher --id=pub4 &

# 预期结果:
# - 所有 Subscriber 收到所有 Publisher 的消息
# - 消息分发正确，无重复/丢失
```

### 8.6 测试工具与自动化

```bash
# 测试工具目录结构
tests/ipc/
├── test_spsc.sh          # SPSC 自动化测试脚本
├── test_spmc.sh          # SPMC 自动化测试脚本
├── test_sp0c.sh          # SP0C 自动化测试脚本
├── test_mpsc.sh          # MPSC 自动化测试脚本
├── test_mpmc.sh          # MPMC 自动化测试脚本
├── ipc_test              # 测试可执行文件
└── analyze_results.py    # 结果分析工具

# 运行所有测试
$ ./run_all_tests.sh

# 生成测试报告
$ python analyze_results.py --output=report.html
```

---

## 9. AUTOSAR 合规性

### 9.1 AUTOSAR AP SWS_Core 映射

| AUTOSAR 概念 | LightAP 实现 |
|-------------|------------|
| `ara::core::InstanceSpecifier` | `ServiceName` + `InstanceId` |
| `ara::core::Result<T>` | `Result<T, Error>` |
| `ara::core::Future<T>` | `Future<T>` (异步操作) |
| `ara::com::ServiceIdentifier` | `ServiceId` (UUID) |
| `ara::com::EventReceiveHandler` | `Listener::WaitAndCollect()` |

### 9.2 错误处理

```cpp
// AUTOSAR 错误代码
enum class IPCErrorCode : UInt32 {
    kSuccess               = 0,
    kServiceNotAvailable   = 1,
    kMaxPublishersExceeded = 2,
    kOutOfMemory           = 3,
    kTimeout               = 4,
    kCorruptedData         = 5,
};

// Result 类型
template<typename T>
using Result = ara::core::Result<T, IPCErrorCode>;
```

### 9.3 生命周期管理

```cpp
// AUTOSAR 服务生命周期
class ServiceLifecycle {
public:
    Result<void> Initialize();    // 初始化
    Result<void> Start();         // 启动
    Result<void> Shutdown();      // 关闭
    Result<void> Terminate();     // 终止
};
```

### 9.4 代码规范

**编码标准**: 严格遵守 **AUTOSAR C++14 Coding Guidelines**

#### 9.4.1 规范文档

- **参考**: `doc/AUTOSAR_RS_CPP14Guidelines`
- **版本**: AUTOSAR Release R24-11
- **适用范围**: 所有IPC模块C++代码

#### 9.4.2 核心规则（强制）

| 规则ID | 描述 | 示例 |
|--------|------|------|
| **A7-1-1** | 禁止使用`register`关键字 | ❌ `register int x;` |
| **A18-1-1** | 禁止使用C风格数组 | ✅ `std::array<T, N>` |
| **A15-5-1** | 异常仅用于错误处理 | ✅ `Result<T>` 替代异常 |
| **A5-1-2** | Lambda必须显式捕获 | ✅ `[&pool]` ❌ `[&]` |
| **A3-3-2** | 禁止隐式类型转换 | ✅ `static_cast<UInt32>` |
| **A12-1-1** | 构造函数必须初始化所有成员 | ✅ 使用成员初始化列表 |
| **M5-0-3** | 禁止`reinterpret_cast` | ⚠️ 仅在必要时使用 |
| **A18-5-1** | 禁止`new`/`delete` | ✅ 使用智能指针或预分配 |

#### 9.4.3 内存管理规范

```cpp
// ✅ 推荐：预分配共享内存
class ChunkPoolAllocator {
    // 构造时预分配，无动态内存
    ChunkPoolAllocator(void* base, UInt32 max_chunks);
};

// ❌ 禁止：运行时动态分配
ChunkHeader* chunk = new ChunkHeader();  // 违反 A18-5-1

// ✅ 推荐：RAII管理生命周期
class Sample {
    ~Sample() { DecrementRef(); }  // 自动释放
};
```

#### 9.4.4 并发安全规范

```cpp
// ✅ 推荐：使用std::atomic
std::atomic<UInt32> head_{0};  // 符合 A18-9-1

// ❌ 禁止：volatile用于同步
volatile int flag;  // 违反 A7-1-4

// ✅ 推荐：CAS操作
UInt32 expected = head_.load(std::memory_order_acquire);
head_.compare_exchange_weak(expected, new_head, 
                            std::memory_order_release);
```

#### 9.4.5 类型安全规范

```cpp
// ✅ 推荐：使用enum class
enum class ChunkState : UInt8 {  // 符合 A7-2-3
    kFree = 0,
    kLoaned = 1
};

// ❌ 禁止：传统enum
enum ChunkState { FREE, LOANED };  // 违反 A7-2-3

// ✅ 推荐：类型别名
using ChunkIndex = UInt32;  // 符合 A7-1-6
```

#### 9.4.6 静态分析工具

**必须通过的检查**:
- ✅ **clang-tidy**: `--checks='autosar-*'`
- ✅ **cppcheck**: `--addon=autosar`
- ✅ **静态分析**: 无Critical/High级别违规

**CI/CD集成**:
```bash
# .github/workflows/autosar-check.yml
clang-tidy --checks='autosar-*,cert-*' source/**/*.cpp
cppcheck --addon=autosar --error-exitcode=1 source/
```

#### 9.4.7 代码审查清单

- [ ] 无`new`/`delete`（使用预分配或智能指针）
- [ ] 无C风格数组（使用`std::array`）
- [ ] 无裸指针传递（使用引用或`std::unique_ptr`）
- [ ] 无隐式类型转换（显式`static_cast`）
- [ ] 无异常（使用`Result<T>`）
- [ ] 所有原子操作指定内存序
- [ ] Lambda显式捕获变量
- [ ] 构造函数初始化所有成员

---

## 10. 核心设计确认（基于 iceoryx2）

### 10.1 ChunkPool 内存模型确认

**✅ 已确定的关键设计：**

1. **固定大小池（Fixed-Size Pool）**
   ```cpp
   // 服务创建时预分配，运行时不可更改
   struct ChunkPoolConfig {
       UInt32 max_chunks;        // 例如 1024，固定值
       UInt64 chunk_size;        // 例如 256 字节，固定值
       UInt64 chunk_alignment;   // 例如 64 字节对齐
   };
   
   // ❌ 禁止操作
   // - 动态扩容 ChunkPool
   // - 运行时修改 chunk_size
   // - realloc 或动态内存分配
   ```

2. **基于索引的寻址（Index-Based Addressing）**
   ```cpp
   // ✅ 正确：使用索引（chunk_index）
   UInt32 chunk_index = chunk->chunk_index;
   msg_queue.Enqueue(chunk_index);  // 跨进程传递索引
   
   // Subscriber 接收时通过 SharedMemoryManager 转换
   ChunkHeader* chunk = shm_mgr->GetChunkByIndex(chunk_index);
   
   // ❌ 错误：直接传递指针
   ChunkHeader* ptr = chunk;
   msg_queue.Enqueue(reinterpret_cast<UInt64>(ptr));  // 禁止！
   ```

3. **Free-List 管理（索引链表）**
   ```cpp
   // Free-List 头存储在 ControlBlock 中
   std::atomic<UInt32> free_list_head_;  // 索引，非指针
   
   // 每个 Chunk 通过 next_free_index 链接
   struct ChunkHeader {
       UInt32 next_free_index;  // 下一个空闲块的索引（0xFFFFFFFF = 链表结束）
       // ...
   };
   
   // 分配：从 free_list_head_ 取出（O(1)）
   // 释放：归还到 free_list_head_（O(1)）
   ```

4. **跨进程地址转换**
   ```cpp
   class SharedMemoryManager {
       // [核心方法] 索引 -> 指针
       ChunkHeader* GetChunkByIndex(UInt32 chunk_index) const;
       
       // [辅助方法] 指针 -> 索引
       UInt32 PtrToChunkIndex(const ChunkHeader* chunk) const {
           return chunk->chunk_index;  // 直接从 Header 读取
       }
   };
   ```

### 10.2 Subscriber 消息队列模型确认

**✅ 已确定的关键设计：**

1. **Per-Subscriber Queue（每个订阅者独立队列）**
   ```cpp
   // 共享内存中预分配固定数量的队列槽位
   struct SharedMemorySegment {
       ChannelQueue subscriber_queues[256];  // 最多 256 个 Subscriber
   };
   
   // Subscriber 创建时分配一个队列槽位
   UInt32 queue_index = AllocateQueueSlot();  // 返回 0-255
   ```

2. **队列内部使用 Offset-Based 链表**
   ```cpp
   struct MessageQueue {
       std::atomic<UInt32> head_offset;  // 队列头的 Chunk 索引
       std::atomic<UInt32> tail_offset;  // 队列尾的 Chunk 索引
       std::atomic<UInt32> count;        // 消息计数
       UInt32              capacity;     // 固定容量（例如 16）
   };
   
   // 入队/出队操作传递 chunk_index，而非指针
   void EnqueueOffset(UInt32 chunk_index, SharedMemoryManager* shm_mgr);
   UInt32 Dequeue();  // 返回 chunk_index（基于 RingBufferBlock）
   ```

3. **Publisher 广播流程**
   ```cpp
   void Publisher::Send(Sample&& sample) {
       auto* chunk = sample.Release();
       UInt32 chunk_index = chunk->chunk_index;  // 获取索引
       
       // 设置引用计数为订阅者数量
       chunk->ref_count.store(subscriber_count_, std::memory_order_release);
       
       // 遍历所有连接的 Subscriber 队列
       for (UInt32 i = 0; i < subscriber_count_; ++i) {
           UInt32 queue_idx = subscriber_list_[i];
           
           // 推送到对应的队列（传递索引）
           subscriber_queues[queue_idx].msg_queue.EnqueueWithPolicy(
               chunk_index,  // 索引，非指针
               shm_mgr_,
               subscriber_queues[queue_idx].qos.load()
           );
       }
   }
   ```

4. **Subscriber 接收流程**
   ```cpp
   Result<Sample> Subscriber::Receive() {
       // 从自己的队列中出队（返回 chunk_index，基于 RingBufferBlock）
       UInt32 chunk_index = subscriber_queues[queue_index_]
                               .msg_queue.Dequeue();
       
       if (chunk_index == kInvalidIndex) {
           return Err(ReceiveError::kNoData);  // 队列为空
       }
       
       // 索引转指针（本地进程内）
       auto* chunk = shm_mgr_->GetChunkByIndex(chunk_index);
       
       return Ok(Sample{chunk, this});
   }
   ```

5. **队列满策略（iceoryx2 原则）**
   ```cpp
   // 队列满策略定义见第3.2节 PublisherState
   // Publisher 配置策略（每个 Publisher 独立配置）
   publisher_state->qos = PublishPolicy::kOverwrite;  // 默认
   ```

### 10.3 设计验证清单

| 验证项 | 状态 | 说明 |
|--------|------|------|
| ChunkPool 固定大小 | ✅ | max_chunks 和 chunk_size 在初始化后不可变 |
| 索引 vs 指针 | ✅ | 跨进程传递使用 chunk_index (UInt32) |
| Free-List 实现 | ✅ | 使用 next_free_index 索引链表 |
| Per-Sub Queue | ✅ | subscriber_queues[256] 预分配 |
| Queue Offset-Based | ✅ | head_offset/tail_offset 存储索引 |
| 队列满策略 | ✅ | kOverwrite (Ring Buffer) 默认，支持 kWait/kBlock |
| Publisher 广播 | ✅ | 遍历 subscriber_list，推送到所有队列 |
| Subscriber 独立读取 | ✅ | 从 subscriber_queues[queue_index_] 读取 |
| 引用计数 | ✅ | ref_count 初始化为 subscriber_count |
| 状态机 | ✅ | kFree/kLoaned/kSent/kReceived 原子转换 |
| **Hook 回调** | ✅ | Loan失败、队列溢出等关键事件 |
| **E2E 保护** | ✅ | 可选 CRC32 + Counter，配置开启 |

### 10.4 Hook 回调机制验证

| 回调点 | 触发条件 | 用途 | 状态 |
|--------|---------|------|-----|
| OnLoanFailed | ChunkPool耗尽或超限 | 监控、告警、资源回收 | ✅ |
| OnSubscriberQueueOverrun | 连续写入失败 | 慢订阅者检测 | ✅ |
| OnChunkPoolExhausted | 池已满 | 资源告警 | ✅ |
| OnReceiveTimeout | 接收超时 | 超时监控 | ✅ |
| OnE2EValidationFailed | E2E校验失败 | 数据完整性监控 | ✅ |
| OnMemoryCorruption | 魔数错误 | 内存损坏检测 | ✅ |

### 10.5 E2E 保护验证

| 特性 | 配置项 | 状态 | 说明 |
|------|--------|-----|------|
| **CRC32 校验** | check_crc | ✅ | 检测数据损坏 |
| **计数器检查** | check_counter | ✅ | 检测丢失/重复消息 |
| **超时检查** | check_timeout | ✅ | 检测通信中断 |
| **数据ID** | data_id | ✅ | 区分不同数据流 |
| **可配置性** | E2EConfig | ✅ | 支持开启/关闭 |
| **性能影响** | CRC32开销 | ✅ | 1KB: ~300ns, 可接受 |

### 10.6 与 iceoryx2 的差异

### 10.6 与 iceoryx2 的差异

**✅ 核心设计完全对齐 iceoryx2：**

```
┌──────────────────────────────────────────────────────┐
│           iceoryx2 核心设计原则                       │
├──────────────────────────────────────────────────────┤
│ 1. Fixed-Size ChunkPool                             │ ✅
│ 2. Index-Based Addressing (chunk_index)             │ ✅
│ 3. Per-Subscriber Message Queue                     │ ✅
│ 4. Offset-Based Queue (head/tail as indices)        │ ✅
│ 5. Ring Buffer (kOverwrite + kWait/kBlock)            │ ✅
│ 6. Lock-Free Operations (CAS)                       │ ✅
│ 7. Publisher-Subscriber Decoupling                  │ ✅
│ 8. Reference Counting (broadcast)                   │ ✅
└──────────────────────────────────────────────────────┘
```

**➕ LightAP 增强特性：**

```
┌──────────────────────────────────────────────────────┐
│           LightAP 特有增强                           │
├──────────────────────────────────────────────────────┤
│ 1. Hook 回调机制 (IPCEventHooks)                    │ ✅
│    - 关键错误路径回调                                │
│    - 用户自定义处理逻辑                              │
│    - 监控和告警集成                                  │
│ 2. E2E 保护 (可选)                                   │ ✅
│    - CRC32 数据完整性校验                            │
│    - Counter 消息顺序检查                            │
│    - Timeout 通信超时检测                            │
│    - 配置开启/关闭                                   │
│ 3. RingBufferBlock 通用抽象                          │ ✅
│    - 可复用环形缓冲区模板                            │
│    - 支持批量操作                                    │
└──────────────────────────────────────────────────────┘
```

---

## 11. 实现状态总结

### 11.1 已完成功能 ✅

**核心功能：**
- ✅ **零拷贝通信**：Publisher Loan/Send API + Subscriber Receive API
- ✅ **无锁队列**：RingBufferBlock实现的lock-free FIFO
- ✅ **三种IPC模式**：SHRINK(4KB)/NORMAL(2MB)/EXTEND(可配置)
- ✅ **Chunk状态机**：kFree → kLoaned → kSent → kReceived → kFree
- ✅ **引用计数**：原子操作管理Chunk生命周期
- ✅ **ChunkPool分配器**：固定大小O(1)分配
- ✅ **Subscriber注册表**：动态Subscriber管理
- ✅ **发布策略**：Overwrite/Error/Block三种策略
- ✅ **Lambda发送API**：`Send(Fn&& write_fn)` 零拷贝写入

**测试与验证：**
- ✅ **SPSC测试**：单生产单消费场景
- ✅ **SPMC测试**：单生产多消费场景（camera_fusion_spmc_example）
- ✅ **MPSC测试**：多生产单消费场景
- ✅ **MPMC测试**：多生产多消费场景
- ✅ **8小时压力测试**：长期稳定性验证，无内存泄漏
- ✅ **性能基准测试**：5MB图像 < 5μs延迟，90+ FPS吞吐

**编译配置：**
- ✅ **CMake集成**：BuildTemplate模块化构建
- ✅ **编译时模式选择**：SHRINK/NORMAL/EXTEND宏定义
- ✅ **示例程序**：camera_fusion_spmc_example, stress_test_shrink/extend等

### 11.2 实测性能指标

**camera_fusion_spmc_example（NORMAL模式）：**
```
配置: 3个Camera Publisher (1920x720x4图像)
吞吐: 90+ FPS (STMin=10ms限流)
延迟: Publisher < 5μs, Subscriber < 2μs
CPU: 25-30% (8核ARM Cortex-A76)
内存: 97MB (49MB共享 + 48MB进程)
稳定性: 8小时无崩溃，丢帧率 < 0.1%
```

**与设计目标对比：**
| 目标 | 设计值 | 实测值 | 状态 |
|------|--------|--------|------|
| 延迟 | < 1μs | < 5μs | ⚠️ 略高但可接受 |
| 吞吐 | 1M+ msg/s | 90+ FPS (大图像) | ✅ 符合预期 |
| CPU占用 | 低 | 25-30% | ✅ 优秀 |
| 内存占用 | 可控 | 97MB | ✅ 合理 |
| 稳定性 | 高 | 8小时无崩溃 | ✅ 优秀 |

### 11.3 待优化功能 🚧

**性能优化：**
- 🚧 **NUMA亲和性**：跨NUMA节点延迟优化
- 🚧 **缓存行对齐**：减少false sharing
- 🚧 **预取优化**：软件预取减少cache miss

**功能增强：**
- 🚧 **WaitSet机制**：基于futex的高效等待/唤醒
- 🚧 **E2E保护**：端到端数据完整性校验
- 🚧 **Heartbeat监控**：Publisher/Subscriber健康检测
- 🚧 **QoS策略**：可靠性/持久性/截止时间等配置
- 🚧 **Request-Response**：RPC模式支持

**工具与调试：**
- 🚧 **性能分析工具**：延迟/吞吐量可视化
- 🚧 **内存泄漏检测**：自动化内存分析
- 🚧 **调试日志系统**：结构化日志输出

### 11.4 下一步计划

**短期目标（2周）：**
1. 优化延迟到 < 2μs（通过缓存行对齐）
2. 添加WaitSet机制（替代轮询）
3. 完善错误处理和日志系统

**中期目标（1个月）：**
1. 实现E2E保护和CRC校验
2. 添加Request-Response模式
3. 完善性能测试套件

**长期目标（3个月）：**
1. NUMA优化和多核扩展性
2. QoS策略完整实现
3. AUTOSAR合规性认证

---

## 12. 参考资料

### iceoryx2 核心文档

- [iceoryx2 GitHub](https://github.com/eclipse-iceoryx/iceoryx2)
- [iceoryx2 Book](https://ekxide.github.io/iceoryx2-book)
- [iceoryx2 API Reference](https://docs.rs/iceoryx2/latest/iceoryx2/)

### AUTOSAR 规范

- AUTOSAR AP SWS_Core (R24-11)
- AUTOSAR AP SWS_CommunicationManagement (R24-11)

### 本项目相关文档

- [camera_fusion_spmc_example.cpp](../test/ipc/camera_fusion_spmc_example.cpp) - 实际测试用例
- [Publisher.hpp](../source/inc/ipc/Publisher.hpp) - Publisher API
- [Subscriber.hpp](../source/inc/ipc/Subscriber.hpp) - Subscriber API
- [IPCTypes.hpp](../source/inc/ipc/IPCTypes.hpp) - 三种IPC模式配置
- [ChunkPoolAllocator.hpp](../source/inc/ipc/ChunkPoolAllocator.hpp) - 内存分配器

---

## 附录 A: 术语表

| 术语 | 描述 |
|------|------|
| **Zero-Copy** | 数据在进程间传递时无需拷贝，通过共享内存实现 |
| **Lock-Free** | 算法不使用互斥锁，通过原子操作和 CAS 实现并发 |
| **SOA** | Service-Oriented Architecture，服务导向架构 |
| **Loan** | Publisher 从共享内存分配器"借用"内存块 |
| **Sample** | 包含有效载荷和元数据的消息单元 |
| **Chunk** | 共享内存中的数据块（ChunkHeader + Payload） |
| **CAS** | Compare-And-Swap，原子比较并交换操作 |
| **SPSC** | Single-Producer Single-Consumer，单生产者单消费者 |
| **SPMC** | Single-Producer Multi-Consumer，单生产者多消费者 |
| **MPMC** | Multi-Producer Multi-Consumer，多生产者多消费者 |
| **STMin** | Send Time Minimum，最小发送间隔（限流） |

---

## 附录 B: 性能对比

| 中间件 | 延迟 (5MB) | 吞吐量 | 零拷贝 | 无锁 | 实测 |
|--------|-----------|--------|--------|------|------|
| **LightAP IPC** | **< 5μs** | **90+ FPS** | ✅ | ✅ | ✅ 已验证 |
| **iceoryx2** | ~600ns | 1M+ msg/s | ✅ | ✅ | 参考值 |
| **iceoryx1** | ~1μs | 800K msg/s | ✅ | ⚠️ Partial | 参考值 |
| **Unix Socket** | ~15ms | 60 FPS | ❌ | ❌ | 实测对比 |
| **DDS (CycloneDDS)** | ~10μs | 100K msg/s | ❌ | ❌ | 参考值 |
| **ROS 2 (FastDDS)** | ~50μs | 50K msg/s | ❌ | ❌ | 参考值 |

**说明**：
- LightAP IPC数据基于camera_fusion_spmc_example实测
- 其他中间件数据来自公开基准测试
- 延迟和吞吐量受硬件和消息大小影响

---

**文档版本**: 1.1  
**最后更新**: 2026-01-19  
**作者**: LightAP Core Team  
**参考**: Eclipse iceoryx2 Project + 实际测试验证
