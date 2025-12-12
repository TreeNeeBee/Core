# IoxPool 重构计划：v0.3.0 架构设计

**日期:** 2025-12-09  
**目标:** 基于 jemalloc 设计理念，实现嵌入式优化的确定性内存池  
**当前版本:** v0.2.0 (TCMalloc 集成)  
**目标版本:** v0.3.0 (jemalloc-inspired + 嵌入式优化)

> **🔥 重要说明:** 本文档为 v0.3.0 架构设计蓝图，将应用到 MEMORY_MANAGEMENT_GUIDE.md 进行完整重构。

---

## 📋 目录

- [架构设计概览](#架构设计概览)
  - [v0.2.0 vs v0.3.0 对比](#v02-vs-v03-对比)
  - [三层架构](#三层架构)
- [🆕 IOX 确定性内存池设计](#iox-确定性内存池设计)
  - [设计目标](#设计目标)
  - [功能完备性](#功能完备性)
  - [确定性设计](#确定性设计)
  - [嵌入式场景适配](#嵌入式场景适配)
  - [Resident Region 详细设计](#resident-region-详细设计)
  - [Thread-Local Storage 设计](#thread-local-storage-设计)
  - [内存分配流程](#内存分配流程)
  - [内存释放流程](#内存释放流程)
- [核心数据结构](#核心数据结构)
- [实现步骤](#实现步骤)
- [预期性能指标](#预期性能指标)
- [风险与缓解](#风险与缓解)
- [应用到主文档](#应用到主文档)

---

## 🎯 架构设计概览

### v0.2.0 vs v0.3.0 对比

| 维度 | v0.2.0 (TCMalloc) | v0.3.0 (IOX 确定性内存池) |
|------|-------------------|---------------------------|
| **分配策略** | TCMalloc (≤256KB) + Burst (>256KB) | TC (≤1024B) + Burst (>1024B) |
| **外部依赖** | Google TCMalloc 库 | 零外部依赖，完全自主实现 |
| **确定性** | 部分确定 (TCMalloc 动态扩展) | 完全确定 (固定内存池，不扩展) |
| **嵌入式适配** | 有限支持 | 深度优化 (base+offset, TLS, 预分配) |
| **Size Classes** | TCMalloc 固定 (86 classes) | 自定义 32 classes (8B-1024B) |
| **碎片率** | 15-25% (内部) + 10-20% (外部) | 5-8% (内部) + 3-8% (外部) |
| **延迟 (P50)** | 10ns (TC) / 100-500ns (Burst) | 10-15ns (TC) / 100ns (Burst) |
| **Bootstrap** | 不支持 | 支持完全自举 |

### 三层架构

### 三层架构

v0.3.0 继承 v0.2.0 的三层架构设计，仅重构 Layer 2 (Allocator Core)：

```
┌─────────────────────────────────────────────────────────────────────┐
│          Layer 3: Adapter Layer (不变)                            │
│  • C++ STL allocator (IoxAllocator<T>)                           │
│  • Global new/delete overloading                                 │
│  • Standard malloc/free/calloc/realloc                           │
│  • IOX 定制 API (iox_alloc/iox_free/iox_alloc_resident)         │
├─────────────────────────────────────────────────────────────────────┤
│       Layer 2: Allocator Core (重构核心)                          │
│  ┌───────────────────────────────────────────────────────────┐   │
│  │ ≤1024B: Thread-Local Cache (TC) - jemalloc-inspired      │   │
│  │   • 32 size classes (8B-1024B)                            │   │
│  │   • Per-thread TC bins (64KB slab)                        │   │
│  │   • Fast Path: ~10ns allocation                           │   │
│  │   • Batch Refill/Flush from Dynamic Extent Region         │   │
│  │   • Optional lazy-free chain (compile-time switch)        │   │
│  └───────────────────────────────────────────────────────────┘   │
│  ┌───────────────────────────────────────────────────────────┐   │
│  │ >1024B: Burst Allocator (保留 v0.2.0 设计)               │   │
│  │   • Freelist + coalescing                                 │   │
│  │   • Lease management (租约超时回收)                        │   │
│  │   • Best-fit search strategy                              │   │
│  └───────────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────────────┤
│     Layer 1: Memory Regions (.iox section, 不变)                 │
│  • Resident Region (128 MiB, 64-bit)                            │
│    - Chunk 0 (2 MiB): Region Header                            │
│    - Chunk 1-3 (6 MiB): TC Region (Thread-Local Cache)         │
│    - Chunk 4-63 (120 MiB): Dynamic Extent Region               │
│  • Burst Region (2 GiB) for large allocations                  │
│  • Custom Region (SHM, 用户管理)                                 │
└─────────────────────────────────────────────────────────────────────┘
```

### 核心变更

1. **移除 TCMalloc 依赖** → 自实现 Thread-Local Cache
2. **256KB 阈值调整为 1024B** → 更精细的分配策略
3. **引入 lazy-free 链** → 缓解 TC 漂移，降低碎片率
4. **完全确定性** → 固定内存池，编译时配置，运行时不扩展

---

## 🆕 IOX 确定性内存池设计

### 设计目标

**核心理念:** 设计一个功能完备的、适用于嵌入式场景的**确定性内存池**。

#### 三大支柱

1. **功能完备** - 提供标准内存分配接口，上层应用无感知
2. **确定性** - 固定大小的内存区域，预分配，不支持动态扩展
3. **嵌入式适配** - 低开销、可预测、适用于资源受限环境

---

### 功能完备性

#### 标准内存分配 API

提供标准内存分配函数，可替换全局 malloc/free，上层应用无感知：

##### 必须实现的核心接口

```cpp
/**
 * @brief 标准内存分配接口 (必须实现)
 */
void* malloc(size_t size);              // 分配指定大小的内存
void  free(void* ptr);                  // 释放已分配的内存
void* calloc(size_t nmemb, size_t size); // 分配并初始化为零 (malloc + memset)
void* realloc(void* ptr, size_t size);   // 调整已分配内存大小 (可能复制数据)
```

##### 可选实现的扩展接口

```cpp
/**
 * @brief 对齐内存分配接口 (可选实现)
 */
void* aligned_alloc(size_t alignment, size_t size);  // C11 标准对齐分配
void* memalign(size_t alignment, size_t size);       // POSIX 对齐分配
int   posix_memalign(void** memptr, size_t alignment, size_t size); // POSIX 标准，返回错误码
void* valloc(size_t size);                           // 页面对齐分配
void* pvalloc(size_t size);                          // 页面对齐分配并初始化为零
size_t malloc_usable_size(void* ptr);                // 返回分配块实际可用大小 (用于优化)
```

#### IOX 定制 API

提供 `iox_` 前缀的 API 接口用于细粒度控制：

```cpp
/**
 * @brief IOX 定制内存分配接口
 */
// 基础分配
void* iox_alloc(size_t size, uint32_t flags);
void  iox_free(void* ptr);

// 区域指定分配
void* iox_alloc_resident(size_t size);  // 从 Resident Region 分配
void* iox_alloc_burst(size_t size);     // 从 Burst Region 分配
void* iox_alloc_custom(size_t size, uint32_t region_id); // 从 Custom Region 分配

// Lease 管理 (仅 Burst Region)
void* iox_alloc_with_lease(size_t size, uint64_t lease_ms);
bool  iox_renew_lease(void* ptr, uint64_t lease_ms);

// 统计和调试
void  iox_stats(iox_stats_t* stats);
void  iox_dump(void);
```

#### STL 分配器适配

```cpp
/**
 * @brief STL 风格内存分配器 (wrap)
 * @details 上层应用无感知，自动使用 IoxPool
 */
template <typename T>
class IoxAllocator {
public:
    using value_type = T;
    
    T* allocate(std::size_t n) {
        return static_cast<T*>(iox_alloc(n * sizeof(T), 0));
    }
    
    void deallocate(T* p, std::size_t n) {
        iox_free(p);
    }
};

// 使用示例
std::vector<int, IoxAllocator<int>> vec;  // 自动使用 IoxPool
std::map<int, string, std::less<int>, IoxAllocator<std::pair<const int, string>>> map;
```

#### 第三方库适配方式 (可选)

```cpp
/**
 * @brief 第三方库内存适配器
 * @details 为第三方库提供统一的内存分配接口
 */
// DDS (FastDDS/Cyclone DDS)
namespace eprosima {
namespace fastdds {
    void* allocate_memory(size_t size) {
        return iox_alloc(size, IOX_FLAG_DDS);
    }
}
}

// ROS2 (rcutils)
void* rcutils_allocate(size_t size, void* state) {
    return iox_alloc(size, IOX_FLAG_ROS2);
}

// Protobuf
void* protobuf_allocate(size_t size) {
    return iox_alloc(size, IOX_FLAG_PROTOBUF);
}
```

---

### 确定性设计

#### 三段内存区域设计

```
┌─────────────────────────────────────────────────────────────┐
│  .iox Section (Linker Script)                               │
├─────────────────────────────────────────────────────────────┤
│  ┌───────────────────────────────────────────────────────┐  │
│  │  Resident Region (固定大小, 常驻高速内存)            │  │
│  │  - 默认: 128 MiB (64-bit) / 64 MiB (32-bit)         │  │
│  │  - 用途: 高频小对象分配 (≤1024B)                    │  │
│  │  - 特性: 不支持扩展，超限则分配失败                  │  │
│  │  - Bootstrap: 高位用于 IOX 自举                     │  │
│  └───────────────────────────────────────────────────────┘  │
│  ┌───────────────────────────────────────────────────────┐  │
│  │  Burst Region (固定大小, 临时扩展内存)               │  │
│  │  - 默认: 2 GiB (64-bit) / 512 MiB (32-bit)          │  │
│  │  - 用途: 大对象分配 (>1024B)                        │  │
│  │  - 特性: 必须带 lease 标识，自动回收                 │  │
│  │  - 不支持扩展，超限则分配失败                        │  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────┐
│  Custom Region (运行时动态创建, 用户指定)                   │
├─────────────────────────────────────────────────────────────┤
│  - 典型场景: SHM 分配用于 SOA (Service-Oriented Arch)      │
│  - 创建方式: mmap / shm_open                                │
│  - 管理方式: 用户负责生命周期管理                            │
│  - IOX 支持: 提供分配接口，但不负责 region 创建/销毁        │
└─────────────────────────────────────────────────────────────┘
```

#### 关键特性

1. **固定大小，链接时分配**
   - Resident 和 Burst 放在 `.iox` section 段
   - 链接时分配固定大小，运行时不可扩展
   - 超过限制则该次内存申请失败（返回 nullptr）

2. **Resident Region 用途**
   - 程序常驻高速内存 (L3 Cache 友好)
   - 高位用于 IOX 自举 (Bootstrap)
   - 启动后所有内存分配均由 IOX 管理，系统分配被屏蔽

3. **Burst Region 特性**
   - 临时扩展内存，用于大对象或临时数据
   - 必须带 `lease` 标识 (租约时间)
   - 租约到期自动回收，防止内存泄漏

4. **Custom Region 场景**
   - 自定义内存区域，典型场景：SHM (Shared Memory) 用于 SOA
   - 运行时动态创建 (mmap/shm_open)
   - 用户负责生命周期管理

5. **Base + Offset 管理**
   - 内部统一使用 `base + offset` 来管理内存地址
   - 屏蔽裸指针操作，提高安全性
   - 跨进程共享时只需传递 offset (SHM 场景)

#### Linker Script 示例

```ld
/* iox.ld - Linker script for IOX memory regions */
SECTIONS
{
    .iox 0x700000000000 : ALIGN(2M)  /* 2 MiB 对齐 */
    {
        __iox_start = .;
        
        /* Resident Region */
        __resident_start = .;
        . = . + 128M;  /* 64-bit: 128 MiB, 32-bit: 64 MiB */
        __resident_end = .;
        
        /* Burst Region */
        __burst_start = .;
        . = . + 2G;    /* 64-bit: 2 GiB, 32-bit: 512 MiB */
        __burst_end = .;
        
        __iox_end = .;
    }
}
```

---

### 嵌入式场景适配

#### 统一地址管理: Base + Offset

```cpp
/**
 * @brief IOX Handle - 统一地址表示
 * @details 内部使用 base + offset 管理内存，屏蔽裸指针
 */
struct IoxHandle {
    uint8_t  region_id : 4;  // Region ID (0-15)
    uint64_t offset    : 60; // Offset within region (64-bit)
    // 32-bit: offset 使用 28 位 (256 MiB 寻址空间)
};

/**
 * @brief 地址转换
 */
void* iox_handle_to_ptr(IoxHandle handle) {
    Region* region = get_region(handle.region_id);
    return region->base + handle.offset;
}

IoxHandle iox_ptr_to_handle(void* ptr) {
    Region* region = find_region_by_addr(ptr);
    return {region->id, ptr - region->base};
}
```

#### 多种内存管理方式

IOX 支持多种内存管理策略，适配不同使用场景：

##### 1. Ring Buffer (环形缓冲区)

```cpp
/**
 * @brief Ring Buffer - 适用于日志、消息队列等场景
 * @details FIFO 特性，固定大小，覆盖写
 */
struct RingBuffer {
    void*    base;
    size_t   size;
    uint32_t head;
    uint32_t tail;
    bool     full;
};

// 使用场景
RingBuffer* log_buffer = iox_create_ringbuffer(1 * 1024 * 1024); // 1 MiB 日志缓冲
```

##### 2. Fixed-Size Block Ring Buffer

```cpp
/**
 * @brief 固定大小块环形缓冲 - 适用于固定大小消息
 * @details 预分配固定大小块，快速分配/释放
 */
struct FixedBlockRingBuffer {
    void*    base;
    size_t   block_size;
    uint32_t block_count;
    uint32_t head;
    uint32_t tail;
};

// 使用场景
FixedBlockRingBuffer* msg_buffer = iox_create_fixed_ringbuffer(256, 1024); // 1024 个 256B 消息块
```

##### 3. Arena + Bin (jemalloc-inspired)

```cpp
/**
 * @brief Arena + Bin - 多 arena 并发分配
 * @details jemalloc 风格，支持多线程并发，低碎片
 */
struct Arena {
    void*    base;
    size_t   size;
    Bin      bins[32];  // 多个 size class bin
    ExtentTree extents; // Red-black tree 管理 extent
};

// 使用场景 (默认分配策略)
void* ptr = malloc(128);  // 自动路由到 Arena + Bin
```

##### 4. Slab + Chunk (Iceoryx2-inspired)

```cpp
/**
 * @brief Slab + Chunk - 带引用计数和 free-list 状态机
 * @details 适用于共享内存场景 (SHM)，支持跨进程引用计数
 */
struct Slab {
    void*         base;
    size_t        chunk_size;
    uint32_t      chunk_count;
    std::atomic<uint32_t>* refcounts;  // 引用计数数组
    FreeList      freelist;            // 空闲链表
};

// 使用场景
void* shm_ptr = iox_alloc_custom(1024, REGION_SHM);  // SHM 分配
```

---

### Resident Region 详细设计

#### 整体布局

```
Resident Region (128 MiB on 64-bit, 最小 4 MiB)
┌─────────────────────────────────────────────────────────────┐
│  Chunk 0 (2 MiB) - Resident Region Header                   │
│  ┌───────────────────────────────────────────────────────┐  │
│  │  Slab 0 (64 KB) - Resident Region 头部                │  │
│  │  - Magic: 0x494F585F52455349 ("IOX_RESI")             │  │
│  │  - Version: 3 (v0.3.0)                                │  │
│  │  - Total Size: 128 MiB                                │  │
│  │  - Chunk Count: 64                                    │  │
│  │  - TC Chunk Range: [1, N+1] (N=2, default)           │  │
│  │  - Dynamic Extent Range: [N+2, 63]                   │  │
│  │  - 可选: 冗余备份 (多个 slab)                         │  │
│  └───────────────────────────────────────────────────────┘  │
│  + Slab 1-31 (Reserved for future use)                     │
└─────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────┐
│  Chunk 1 - Chunk[N+1] (默认 Chunk 1-3, 6 MiB)              │
│  Thread-Local Cache (TC) Region                            │
│  ┌───────────────────────────────────────────────────────┐  │
│  │  Chunk 1, Slab 0 (64 KB) - TC Management Header      │  │
│  │  - Thread ID mapping                                  │  │
│  │  - TC slab allocation bitmap                          │  │
│  │  - Per-thread statistics                              │  │
│  └───────────────────────────────────────────────────────┘  │
│  │  Chunk 1, Slab 1 (64 KB) - Thread 0 TC Bins          │  │
│  │  ┌────────────────────────────────────────────────┐  │  │
│  │  │  [0-4K]: TC Header for Thread 0                │  │  │
│  │  │  - Size class bin pointers                     │  │  │
│  │  │  - Current allocation cursor                   │  │  │
│  │  │  - Statistics (alloc/free count)               │  │  │
│  │  ├────────────────────────────────────────────────┤  │  │
│  │  │  [4K-64K]: TC Bins (60 KB)                     │  │  │
│  │  │  - 32 size classes (8B - 1024B)               │  │  │
│  │  │  - Pre-allocated blocks per size class        │  │  │
│  │  └────────────────────────────────────────────────┘  │  │
│  │  ...                                                  │  │
│  │  Chunk 1-3, Slab 1-31 - More TC Bins                 │  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────┐
│  Chunk[N+2] - Chunk 63 (默认 Chunk 4-63, 120 MiB)          │
│  Dynamic Extent Region                                     │
│  ┌───────────────────────────────────────────────────────┐  │
│  │  Each Slab (64 KB):                                   │  │
│  │  ┌────────────────────────────────────────────────┐  │  │
│  │  │  [0-4K]: Extent Header                         │  │  │
│  │  │  - Magic: 0x494F585F455854 ("IOX_EXT")        │  │  │
│  │  │  - Size class for this slab                   │  │  │
│  │  │  - Allocation bitmap (per-block)              │  │  │
│  │  │  - Free count / Total count                   │  │  │
│  │  │  - Next extent in lazy-free list              │  │  │
│  │  ├────────────────────────────────────────────────┤  │  │
│  │  │  [4K-64K]: Data Blocks (60 KB)                │  │  │
│  │  │  - Divided by size class (8B - 1024B)        │  │  │
│  │  │  - Example: 1024B class → 60 blocks          │  │  │
│  │  └────────────────────────────────────────────────┘  │  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

#### Size Class 设计

参考 jemalloc，精心设计 32 个 size class，覆盖 8B - 1024B：

```python
# Size Class Definition
PRESET_IOXPOOL_SIZES = [
    # Step: 8 (8B - 64B, 8 classes)
    8, 16, 24, 32,
    40, 48, 56, 64,
    
    # Step: 16 (80B - 256B, 12 classes)
    80, 96, 112, 128,
    144, 160, 176, 192,
    208, 224, 240, 256,
    
    # Step: 32 (288B - 512B, 8 classes)
    288, 320, 352, 384,
    416, 448, 480, 512,
    
    # Step: 128 (640B - 1024B, 4 classes)
    640, 768, 896, 1024,
]

# Pre-allocated Count per Size Class (per TC slab)
PRESET_IOXPOOL_COUNTS = [
    # 8B - 64B (高频小对象)
    100, 100, 80, 80,
    80, 80, 40, 40,
    
    # 80B - 256B (中频对象)
    32, 32, 32, 32,
    16, 16, 8, 8,
    8, 8, 4, 4,
    
    # 288B - 512B (低频对象)
    4, 4, 4, 4,
    2, 2, 2, 2,
    
    # 640B - 1024B (极低频对象)
    2, 2, 2, 2
]
```

**设计理由:**
- **小粒度 (8B)**: 覆盖小结构体 (如指针、状态机)
- **渐进式增长**: 平衡内部碎片和 size class 数量
- **预分配数量**: 根据使用频率调整，避免浪费

#### TC Slab 内存布局示例

以 64KB slab 为例，展示 size class 的实际布局：

```
TC Slab (64 KB = 65536 Bytes)
┌─────────────────────────────────────────────────────────────┐
│  [0 - 4096): TC Header (4 KB)                               │
│  - Size class bin pointers [32]                             │
│  - Allocation cursor [32]                                    │
│  - Statistics                                                │
└─────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────┐
│  [4096 - 65536): TC Bins (60 KB = 61440 Bytes)              │
│  ┌───────────────────────────────────────────────────────┐  │
│  │  Size Class 0 (8B × 100) = 800 Bytes                  │  │
│  ├───────────────────────────────────────────────────────┤  │
│  │  Size Class 1 (16B × 100) = 1600 Bytes                │  │
│  ├───────────────────────────────────────────────────────┤  │
│  │  Size Class 2 (24B × 80) = 1920 Bytes                 │  │
│  ├───────────────────────────────────────────────────────┤  │
│  │  Size Class 3 (32B × 80) = 2560 Bytes                 │  │
│  ├───────────────────────────────────────────────────────┤  │
│  │  ... (more size classes)                               │  │
│  ├───────────────────────────────────────────────────────┤  │
│  │  Size Class 31 (1024B × 2) = 2048 Bytes               │  │
│  └───────────────────────────────────────────────────────┘  │
│  Total Used: ~58 KB (实际使用根据 PRESET_IOXPOOL_COUNTS)    │
│  Padding: ~2 KB (对齐和保留)                                │
└─────────────────────────────────────────────────────────────┘
```

---

### Thread-Local Storage 设计

#### TC 设计目标

1. **减少锁竞争** - 每个线程独立的 TC，无锁访问
2. **缓解 TC 漂移** - Batch Refill/Flush 操作
3. **优先使用 Resident** - TC 优先从 Resident Region 分配
4. **支持 Burst 扩展** - Resident 不足时可从 Burst 申请扩展

#### TC 分配策略

```
Thread-Local Cache (TC) 分配流程
┌─────────────────────────────────────────────────────────────┐
│  1. 线程初始化                                              │
│     - 分配 TC slab (64 KB) from Chunk 1-3                  │
│     - 初始化 32 个 size class bins                          │
│     - 预分配 blocks 根据 PRESET_IOXPOOL_COUNTS             │
└─────────────────────────────────────────────────────────────┘
          ↓
┌─────────────────────────────────────────────────────────────┐
│  2. 内存分配请求 (size ≤ 1024B)                            │
│     - 计算 size class index                                 │
│     - 检查 TC bin 是否有空闲 block                          │
│       ├─ 有 → 直接返回 (O(1), ~10ns)                       │
│       └─ 无 → 触发 Refill                                   │
└─────────────────────────────────────────────────────────────┘
          ↓ (Refill)
┌─────────────────────────────────────────────────────────────┐
│  3. TC Refill (批量填充)                                    │
│     - 检查 lazy-free 扩展链                                 │
│       ├─ 有空闲 extent → 初始化并挂载到 TC                 │
│       └─ 无 → 从 Dynamic Extent Region 申请新 slab         │
│     - Batch 填充 (例如: 一次填充 10 个 blocks)             │
│     - 更新 TC bin cursor                                    │
└─────────────────────────────────────────────────────────────┘
          ↓
┌─────────────────────────────────────────────────────────────┐
│  4. TC Flush (批量归还, 当 TC 满时)                         │
│     - 将 TC 中过多的 free blocks 归还到 extent             │
│     - Batch 归还 (例如: 一次归还 20 个 blocks)             │
│     - 更新 extent free count                                │
│     - 如果 extent 完全空闲 → 归还到 lazy-free 链           │
└─────────────────────────────────────────────────────────────┘
```

#### Refill/Flush 参数

```cpp
/**
 * @brief TC Refill/Flush 配置
 */
struct TCConfig {
    // Refill 配置
    uint32_t refill_batch_size;     // 每次 refill 的 block 数量 (默认: 10)
    uint32_t refill_threshold;      // 触发 refill 的阈值 (默认: 当前 bin 空)
    
    // Flush 配置
    uint32_t flush_batch_size;      // 每次 flush 的 block 数量 (默认: 20)
    uint32_t flush_threshold;       // 触发 flush 的阈值 (默认: bin 使用 > 80%)
    
#ifdef IOX_ENABLE_LAZY_FREE  // 编译宏开关，默认打开
    // TC 漂移缓解 - lazy-free 链配置
    bool     enable_lazy_free;      // 启用 lazy-free 链 (默认: true)
    uint32_t lazy_free_max_depth;   // lazy-free 链最大深度 (默认: 16)
    uint64_t lazy_free_lease_ms;    // lazy-free extent 租约时间 (ms, 默认: 5000ms)
    // 说明: 
    //   - max_depth: 限制链表长度，防止搜索过慢
    //   - lease_ms: extent 在 lazy-free 链中的最大保留时间
    //               超时后归还到 Dynamic Region，防止内存长期占用
#endif
};

// 默认配置
static const TCConfig DEFAULT_TC_CONFIG = {
    .refill_batch_size = 10,
    .refill_threshold = 0,
    .flush_batch_size = 20,
    .flush_threshold = 80,  // 80%
#ifdef IOX_ENABLE_LAZY_FREE
    .enable_lazy_free = true,
    .lazy_free_max_depth = 16,
    .lazy_free_lease_ms = 5000,  // 5 秒
#endif
};
```

#### 编译宏开关

```cpp
/**
 * @brief 编译时配置 - lazy-free 链开关
 * @details 在编译时通过宏控制是否启用 lazy-free 链优化
 * 
 * 编译选项:
 *   - 启用 (默认): -DIOX_ENABLE_LAZY_FREE
 *   - 禁用:        不定义该宏
 * 
 * 影响范围:
 *   - TCConfig 结构体是否包含 lazy-free 相关字段
 *   - tc_refill() 是否搜索 lazy-free 链
 *   - extent 完全空闲时是否归还到 lazy-free 链
 *   - 是否启用 lazy-free lease 超时回收机制
 */
#ifndef IOX_ENABLE_LAZY_FREE
#define IOX_ENABLE_LAZY_FREE  // 默认启用
#endif
```

---

### 内存分配流程

#### 快速路径 (≤1024B)

```cpp
/**
 * @brief 内存分配流程 (Fast Path)
 * @param size 分配大小 (8B - 1024B)
 * @return 分配的内存指针，失败返回 nullptr
 */
void* iox_alloc_fast(size_t size) {
    // Step 1: 计算 size class index
    uint32_t size_class = calculate_size_class(size);
    
    // Step 2: 获取当前线程的 TC
    ThreadCache* tc = get_thread_cache();
    
    // Step 3: 检查 TC bin 是否有空闲 block
    TCBin* bin = &tc->bins[size_class];
    if (bin->free_count > 0) {
        // Fast path: 直接从 TC bin 分配 (~10ns)
        void* ptr = bin->blocks[bin->cursor];
        bin->cursor++;
        bin->free_count--;
        return ptr;
    }
    
    // Step 4: TC bin 空，触发 Refill
    if (!tc_refill(tc, size_class)) {
        // Refill 失败，分配失败
        return nullptr;
    }
    
    // Step 5: Refill 成功，重新分配
    void* ptr = bin->blocks[bin->cursor];
    bin->cursor++;
    bin->free_count--;
    return ptr;
}

/**
 * @brief TC Refill - 批量填充 TC bin
 */
bool tc_refill(ThreadCache* tc, uint32_t size_class) {
    TCBin* bin = &tc->bins[size_class];
    
#ifdef IOX_ENABLE_LAZY_FREE
    // Step 1: 检查 lazy-free 扩展链 (仅在编译时启用)
    if (DEFAULT_TC_CONFIG.enable_lazy_free) {
        Extent* extent = tc->lazy_free_list;
        uint32_t depth = 0;
        uint64_t current_time_ms = get_current_time_ms();
        
        while (extent && depth < DEFAULT_TC_CONFIG.lazy_free_max_depth) {
            // 检查 lease 是否超时
            if (current_time_ms - extent->lazy_free_timestamp_ms > DEFAULT_TC_CONFIG.lazy_free_lease_ms) {
                // Lease 超时，归还到 Dynamic Region
                Extent* expired = extent;
                extent = extent->next;
                return_extent_to_dynamic_region(expired);
                depth++;
                continue;
            }
            
            // 检查 size class 匹配且有空闲 blocks
            if (extent->size_class == size_class && extent->free_count > 0) {
                // 找到匹配的 extent，初始化并填充
                tc_refill_from_extent(bin, extent, DEFAULT_TC_CONFIG.refill_batch_size);
                return true;
            }
            
            extent = extent->next;
            depth++;
        }
    }
#endif
    
    // Step 2: lazy-free 链无可用 extent (或未启用)，从 Dynamic Extent Region 申请新 slab
    Extent* extent = allocate_extent_from_dynamic_region(size_class);
    if (!extent) {
        // Dynamic Extent Region 耗尽，分配失败
        return false;
    }
    
    // Step 3: 初始化新 extent 并填充 TC bin
    initialize_extent(extent, size_class);
    tc_refill_from_extent(bin, extent, DEFAULT_TC_CONFIG.refill_batch_size);
    
#ifdef IOX_ENABLE_LAZY_FREE
    // Step 4: 将 extent 挂载到 TC 的 lazy-free 链 (仅在编译时启用)
    if (DEFAULT_TC_CONFIG.enable_lazy_free) {
        extent->lazy_free_timestamp_ms = get_current_time_ms();  // 记录时间戳
        extent->next = tc->lazy_free_list;
        tc->lazy_free_list = extent;
    }
#endif
    
    return true;
}
```

#### 慢速路径 (>1024B)

```cpp
/**
 * @brief 内存分配流程 (Slow Path)
 * @param size 分配大小 (>1024B)
 * @return 分配的内存指针，失败返回 nullptr
 */
void* iox_alloc_slow(size_t size) {
    // 超过 1024B，走 Burst 通道
    return burst_allocator_alloc(size);
}
```

#### 完整分配流程图

```
malloc(size)
    ↓
size ≤ 1024B?
    ├─ Yes → iox_alloc_fast(size)
    │         ↓
    │    TC bin 有空闲?
    │         ├─ Yes → 直接返回 (Fast Path, ~10ns)
    │         └─ No  → tc_refill()
    │                   ↓
    │              lazy-free 链有可用 extent?
    │                   ├─ Yes → 从 extent 填充 TC
    │                   └─ No  → allocate_extent_from_dynamic_region()
    │                             ↓
    │                        Dynamic Region 有空闲 slab?
    │                             ├─ Yes → 初始化 extent，填充 TC
    │                             └─ No  → 返回 nullptr (分配失败)
    │
    └─ No  → iox_alloc_slow(size)
              ↓
         burst_allocator_alloc(size)
              ↓
         Burst Region freelist 搜索
              ├─ 找到合适块 → 标记使用，返回
              └─ 未找到 → 返回 nullptr (分配失败)
```

---

### 内存释放流程

#### 快速路径 (≤1024B)

```cpp
/**
 * @brief 内存释放流程 (Fast Path)
 * @param ptr 要释放的内存指针
 */
void iox_free_fast(void* ptr) {
    // Step 1: 根据地址确定所属 region 和 extent
    Extent* extent = find_extent_by_address(ptr);
    if (!extent) {
        // 无效指针
        return;
    }
    
    // Step 2: 判断是否为 TC extent
    if (is_tc_extent(extent)) {
        // TC extent: 直接归还到 TC bin (Fast Path)
        ThreadCache* tc = get_thread_cache();
        uint32_t size_class = extent->size_class;
        TCBin* bin = &tc->bins[size_class];
        
        // 归还到 TC bin
        bin->blocks[bin->cursor] = ptr;
        bin->cursor--;
        bin->free_count++;
        
        // 检查是否需要 Flush
        if (bin->free_count > DEFAULT_TC_CONFIG.flush_threshold) {
            tc_flush(tc, size_class);
        }
    } else {
        // Dynamic extent: 归还到 extent，更新 free count
        extent_free_block(extent, ptr);
        
        // 检查 extent 是否完全空闲
        if (extent->free_count == extent->total_count) {
#ifdef IOX_ENABLE_LAZY_FREE
            // 完全空闲，归还到 lazy-free 链 (仅在编译时启用)
            if (DEFAULT_TC_CONFIG.enable_lazy_free) {
                return_extent_to_lazy_free(extent);
            } else {
                // lazy-free 禁用，直接归还到 Dynamic Region
                return_extent_to_dynamic_region(extent);
            }
#else
            // lazy-free 未编译，直接归还到 Dynamic Region
            return_extent_to_dynamic_region(extent);
#endif
        }
    }
}

/**
 * @brief TC Flush - 批量归还 TC bin 中的空闲 blocks
 */
void tc_flush(ThreadCache* tc, uint32_t size_class) {
    TCBin* bin = &tc->bins[size_class];
    uint32_t flush_count = DEFAULT_TC_CONFIG.flush_batch_size;
    
    // 批量归还 blocks 到对应的 extent
    for (uint32_t i = 0; i < flush_count && bin->free_count > 0; i++) {
        void* ptr = bin->blocks[bin->cursor];
        Extent* extent = find_extent_by_address(ptr);
        extent_free_block(extent, ptr);
        
        bin->cursor--;
        bin->free_count--;
    }
}
```

#### 慢速路径 (>1024B)

```cpp
/**
 * @brief 内存释放流程 (Slow Path)
 * @param ptr 要释放的内存指针 (>1024B)
 */
void iox_free_slow(void* ptr) {
    // 超过 1024B，走 Burst 通道
    burst_allocator_free(ptr);
}
```

#### 完整释放流程图

```
free(ptr)
    ↓
ptr 是否有效?
    ├─ No  → 返回 (无操作)
    └─ Yes → 查找所属 region
              ↓
         属于 Resident Region?
              ├─ Yes → 查找所属 extent
              │         ↓
              │    是 TC extent?
              │         ├─ Yes → 归还到 TC bin (Fast Path)
              │         │        ↓
              │         │   TC bin 使用率 > 80%?
              │         │        ├─ Yes → tc_flush() (批量归还)
              │         │        └─ No  → 完成
              │         │
              │         └─ No  → 归还到 Dynamic extent
              │                  ↓
              │             extent 完全空闲?
              │                  ├─ Yes → 归还到 lazy-free 链
              │                  └─ No  → 完成
              │
              └─ No  → burst_allocator_free(ptr)
                       ↓
                  标记 block 为 free
                       ↓
                  尝试合并相邻 free blocks
                       ↓
                  完成
```

---

## 📊 IOX 设计与 jemalloc 对比

| 特性 | jemalloc | IOX v0.3.0 (嵌入式优化) |
|------|----------|-------------------------|
| **Arena 数量** | 动态 (CPU 核心数 × 4) | 固定 (编译时配置) |
| **Size Class** | 128+ 类 (8B - 4MB+) | 32 类 (8B - 1024B) |
| **Thread Cache** | TLS, 动态扩展 | 固定 slab (64 KB), 预分配 |
| **Extent 管理** | 红黑树 | 简化链表 + bitmap |
| **大对象分配** | mmap 动态分配 | Burst Region (固定大小) |
| **内存扩展** | 支持动态扩展 | 不支持 (确定性) |
| **碎片率** | <5% (extent 合并) | <8% (TC + lazy-free) |
| **延迟 (P50)** | 20-30ns | 10-15ns (TC fast path) |
| **内存开销** | ~5-10% | <5% (简化元数据) |
| **适用场景** | 通用服务器/桌面 | 嵌入式/ECU/实时系统 |

### IOX 设计优势

1. **确定性** - 固定大小内存池，运行时不扩展，可预测性强
2. **低延迟** - TC fast path ~10ns，适合实时系统
3. **低开销** - 简化元数据，内存开销 <5%
4. **嵌入式友好** - Base + Offset 管理，支持跨进程共享 (SHM)
5. **Bootstrap 支持** - 高位自举，完全接管系统内存分配

---

## 🔧 IOX 实现步骤

### Phase 1: 核心数据结构 (Week 1)

1. **定义 Size Class**
   - 实现 `PRESET_IOXPOOL_SIZES` 和 `PRESET_IOXPOOL_COUNTS`
   - 编写 `calculate_size_class(size)` 函数

2. **实现 Resident Region Header**
   - 定义 `ResidentRegionHeader` 结构体
   - 实现初始化和验证逻辑

3. **实现 TC 数据结构**
   - 定义 `ThreadCache`, `TCBin`, `TCConfig`
   - 实现 TC 初始化

4. **实现 Extent 数据结构**
   - 定义 `Extent`, `ExtentHeader`
   - 实现 extent 初始化和管理

### Phase 2: TC 分配器实现 (Week 2)

5. **实现 TC 分配 (Fast Path)**
   - `iox_alloc_fast(size)` - TC bin 直接分配
   - 性能目标: <15ns

6. **实现 TC Refill**
   - `tc_refill(tc, size_class)` - 批量填充 TC bin
   - lazy-free 链管理

7. **实现 TC Flush**
   - `tc_flush(tc, size_class)` - 批量归还
   - 触发条件和阈值

8. **实现 TC 释放 (Fast Path)**
   - `iox_free_fast(ptr)` - 归还到 TC bin
   - extent 完全空闲检测

### Phase 3: Dynamic Extent 管理 (Week 3)

9. **实现 Dynamic Extent 分配**
   - `allocate_extent_from_dynamic_region(size_class)`
   - Bitmap 管理 slab 分配状态

10. **实现 Extent 初始化**
    - `initialize_extent(extent, size_class)`
    - 切分 blocks，建立 freelist

11. **实现 Extent 释放**
    - `extent_free_block(extent, ptr)`
    - 更新 free count 和 bitmap

12. **实现 lazy-free 链管理 (可选，通过 IOX_ENABLE_LAZY_FREE 控制)**
    - `return_extent_to_lazy_free(extent)` - 归还 extent 到 lazy-free 链
    - 限制 lazy-free 链深度 (max_depth)
    - 实现 lease 超时机制 (lease_ms)
    - 周期性扫描并回收超时 extent

### Phase 4: Burst Allocator (Week 4)

13. **实现 Burst 分配器**
    - `burst_allocator_alloc(size)` - Freelist 搜索
    - Best-fit 策略

14. **实现 Burst 释放**
    - `burst_allocator_free(ptr)` - 归还 + 合并
    - 相邻 block 合并逻辑

15. **实现 Lease 管理**
    - `iox_alloc_with_lease(size, lease_ms)` - 带租约分配
    - 租约到期自动回收

### Phase 5: 标准 API 实现 (Week 5)

16. **实现标准内存 API**
    - `malloc`, `free`, `calloc`, `realloc`
    - 路由到 IOX allocator

17. **实现对齐分配 API**
    - `aligned_alloc`, `memalign`, `posix_memalign`
    - `valloc`, `pvalloc`

18. **实现 STL Allocator**
    - `IoxAllocator<T>` 模板类
    - 与 std::vector, std::map 集成

### Phase 6: 测试与优化 (Week 6)

19. **单元测试**
    - TC 分配/释放测试
    - Extent 管理测试
    - Burst 分配测试
    - 边界条件测试

20. **性能测试**
    - 延迟测试 (P50, P99, P99.9)
    - 吞吐量测试 (单线程/多线程)
    - 碎片率测试

21. **压力测试**
    - 长时间运行测试 (24h+)
    - 内存泄漏检测
    - 极限并发测试

---

## 📊 预期性能指标 (v0.3.0 IOX)

### 延迟对比

| 大小范围 | v0.2.0 (TCMalloc) | v0.3.0 IOX (TC Fast Path) | v0.3.0 IOX (Refill) |
|---------|-------------------|--------------------------|---------------------|
| 8B-64B | 10ns (P50) | **10ns (P50)** | 50ns (P50) |
| 64B-256B | 15ns (P50) | **12ns (P50)** | 55ns (P50) |
| 256B-1024B | 25ns (P50) | **15ns (P50)** | 60ns (P50) |
| >1024B | 100ns (P50) | 100ns (P50) | - |

### 碎片率对比

| 指标 | v0.2.0 | v0.3.0 IOX | 变化 |
|------|--------|------------|------|
| 内部碎片率 | 15-25% | **5-8%** | -10-17% ⬇️ |
| 外部碎片率 | 10-20% | **3-8%** | -7-12% ⬇️ |
| 总内存效率 | 65-75% | **87-95%** | +17-25% ⬆️ |

### 吞吐量对比

| 线程数 | v0.2.0 (ops/s) | v0.3.0 IOX (ops/s) | 变化 |
|--------|----------------|-------------------|------|
| 1 | 30M | **35M** | +16.7% ⬆️ |
| 8 | 180M | **240M** | +33.3% ⬆️ |
| 32 | 450M | **650M** | +44.4% ⬆️ |

### 内存开销

| 组件 | 开销 | 说明 |
|------|------|------|
| Resident Header | 64 KB | Chunk 0, Slab 0 |
| TC Header | 4 KB per thread | 线程数量 × 4 KB |
| TC Bins | 60 KB per thread | 预分配 blocks |
| Extent Headers | 4 KB per slab | Dynamic Region |
| 总开销 (128 MiB) | **<5%** | ~6 MiB |

---

## ⚠️ 风险与缓解

### 风险 1: TC 漂移导致碎片

**风险描述:**
- 线程迁移导致 TC 无法复用，extent 碎片化

**缓解措施:**
- ✅ **Batch Refill/Flush** - 批量操作减少单次开销
- ✅ **Lazy-Free 链 (可选)** - 延迟归还，提高复用率
  - 编译宏开关: `IOX_ENABLE_LAZY_FREE` (默认启用)
  - 深度限制: `lazy_free_max_depth` (默认 16)，防止链过长导致搜索慢
  - Lease 超时: `lazy_free_lease_ms` (默认 5000ms)，防止内存长期占用
- ✅ **可配置开关** - 嵌入式场景可禁用 lazy-free，直接归还到 Dynamic Region

### 风险 2: Dynamic Region 耗尽

**风险描述:**
- 大量并发分配导致 Dynamic Extent Region 耗尽

**缓解措施:**
- ✅ **合理配置 Resident Size** - 根据负载调整 (默认 128 MiB)
- ✅ **监控和告警** - 实时监控 extent 使用率
- ✅ **Burst Fallback** - Dynamic Region 不足时提示使用 Burst

### 风险 3: Burst Region 泄漏

**风险描述:**
- 大对象未及时释放，Burst Region 内存泄漏

**缓解措施:**
- ✅ **强制 Lease** - Burst 分配必须带租约
- ✅ **自动回收** - 租约到期自动释放
- ✅ **泄漏检测** - 周期扫描未释放的 Burst blocks

### 风险 4: 性能回退

**风险描述:**
- 复杂的 Refill/Flush 逻辑导致延迟增加

**缓解措施:**
- ✅ **Fast Path 优化** - TC bin 直接分配 <15ns
- ✅ **Batch 操作** - 减少 Refill/Flush 频率
- ✅ **预分配** - TC 初始化时预分配 blocks

---

## 🎯 重构目标：为何参考 jemalloc？

### jemalloc 核心优势

1. **灵活的 Size Class 分级**
   - 小对象 (8B-14KB): 128 个 size class，间隔 8-2048B
   - 中等对象 (16KB-3.75MB): 按 2MB slab 分配
   - 大对象 (>4MB): 直接 mmap

2. **Arena-based 设计**
   - 多 arena 并发分配，减少锁竞争
   - 每个 arena 独立管理 extent（类似 TCMalloc 的 span）
   - CPU 绑定：每个线程绑定到一个 arena

3. **Thread Cache (tcache)**
   - 类似 TCMalloc 的 Per-CPU cache
   - 但更灵活：可配置 size class、缓存大小
   - 支持动态调整

4. **Extent 管理**
   - Extent = 连续内存区域（类似我们的 slab）
   - 支持分裂 (split) 和合并 (merge)
   - 红黑树管理空闲 extent

5. **低碎片率**
   - Size class 设计减少内部碎片
   - Extent 合并减少外部碎片
   - Active/dirty page tracking

### TCMalloc vs jemalloc 对比

| 特性 | TCMalloc | jemalloc | 推荐 |
```
|------|----------|----------|------|
| **Per-CPU Cache** | ✅ 是 (rseq) | ❌ 否 (thread-local) | TCMalloc |
| **Size Class 灵活性** | ⚠️ 固定 | ✅ 可配置 | jemalloc |
| **Arena 支持** | ❌ 单一 Page Heap | ✅ 多 arena 并发 | jemalloc |
| **碎片率** | ⚠️ 中等 | ✅ 低 | jemalloc |
| **延迟 (P50)** | ✅ 10ns | ⚠️ 20-30ns | TCMalloc |
| **内存效率** | ⚠️ 中等 | ✅ 高 | jemalloc |
| **可控性** | ❌ 黑盒 | ✅ 高度可配置 | jemalloc |
| **代码依赖** | ⚠️ 外部库 | ✅ 可自实现 | jemalloc |

### 重构策略

**选项 A: 完全替换为 jemalloc**
- ❌ 失去 TCMalloc 的 Per-CPU cache 优势
- ✅ 获得更灵活的 size class 和 arena 管理
- ⚠️ 延迟可能增加 10-20ns

**选项 B: 混合设计（推荐）**
- ✅ 保留 TCMalloc 的 Per-CPU cache（≤64KB，高频小对象）
- ✅ 引入 jemalloc-style arena（64KB-4MB，中等对象）
- ✅ 保留 BurstAllocator（>4MB，大对象）
- ✅ 获得两者优势，最小化风险

**选项 C: 自实现 jemalloc-inspired 分配器**
- ✅ 完全控制，无外部依赖
- ✅ 针对 AUTOSAR AP 优化
- ⚠️ 开发工作量大
- ⚠️ 需要充分测试

---

## 🏗️ 重构架构设计（选项 B: 混合设计）

### 新的三层架构

```
┌──────────────────────────────────────────────────────────┐
│  Layer 3: Adapter Layer (不变)                           │
│  - IoxMemAPI::alloc/free                                 │
└──────────────────────────────────────────────────────────┘
                         ↓
┌──────────────────────────────────────────────────────────┐
│  Layer 2: Allocator Core (重构)                          │
│  ┌────────────────────────────────────────────────────┐  │
│  │ ≤64KB: TCMalloc (Per-CPU cache)                    │  │
│  │   - 保留现有 ResidentBackend                        │  │
│  │   - 高频小对象，10ns 延迟                            │  │
│  └────────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────────┐  │
│  │ 64KB-4MB: JemallocArena (jemalloc-inspired)        │  │
│  │   - 多 arena 并发分配                               │  │
│  │   - 灵活 size class (128 classes)                  │  │
│  │   - Extent 管理 (红黑树)                            │  │
│  │   - Thread cache (可配置)                          │  │
│  └────────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────────┐  │
│  │ >4MB: BurstAllocator (保留)                        │  │
│  │   - Freelist + coalescing                          │  │
│  └────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────┘
                         ↓
┌──────────────────────────────────────────────────────────┐
│  Layer 1: Memory Regions (不变)                          │
│  - Resident Region (128 MiB, TCMalloc + JemallocArena)  │
│  - Burst Region (2 GiB, BurstAllocator)                 │
└──────────────────────────────────────────────────────────┘
```

### 新的分配策略

| 大小范围 | 分配器 | 理由 |
|---------|--------|------|
| ≤64KB | TCMalloc | 高频小对象，Per-CPU cache 最优 |
| 64KB-4MB | JemallocArena | 中等对象，灵活 size class，低碎片 |
| >4MB | BurstAllocator | 大对象，简单 freelist 足够 |

---

## 📐 JemallocArena 详细设计

### 1. Size Class 分级

参考 jemalloc，定义 128 个 size class：

```cpp
// Small classes (8B-2KB, 64 classes)
// 间隔：8, 16, 24, 32, ..., 128, 144, ..., 256, 288, ..., 2048
constexpr Size kSmallSizeClasses[] = {
    8, 16, 24, 32, 48, 64, 80, 96, 112, 128,  // 0-9
    144, 160, 176, 192, 208, 224, 240, 256,   // 10-17
    288, 320, 352, 384, 416, 448, 480, 512,   // 18-25
    // ... (total 64 classes up to 2KB)
};

// Medium classes (2KB-64KB, 32 classes)
// 间隔：256B steps
constexpr Size kMediumSizeClasses[] = {
    2048 + 256, 2048 + 512, 2048 + 768, ...  // 2.25KB, 2.5KB, ...
    // ... (total 32 classes up to 64KB)
};

// Large classes (64KB-4MB, 32 classes)
// 间隔：按 2MB slab 对齐
constexpr Size kLargeSizeClasses[] = {
    64*1024, 128*1024, 256*1024, 512*1024, 1024*1024, 2048*1024, 4096*1024
};
```

### 2. Arena 结构

```cpp
/**
 * @brief Arena for jemalloc-style allocation
 * @details Each arena manages a subset of Resident Region
 */
class JemallocArena {
public:
    struct ArenaStats {
        Size total_allocated;   // 总分配字节
        Size total_freed;       // 总释放字节
        Size active_bytes;      // 活跃字节
        Size dirty_bytes;       // 脏页字节（已释放但未归还）
        UInt32 num_extents;     // Extent 数量
    };
    
    /**
     * @brief Allocate from arena
     * @param size_class Size class index (0-127)
     * @return Pointer to allocated block, or nullptr
     */
    void* allocate(UInt32 size_class) noexcept;
    
    /**
     * @brief Deallocate to arena
     * @param ptr Pointer to block
     * @param size_class Size class index
     */
    void deallocate(void* ptr, UInt32 size_class) noexcept;
    
    /**
     * @brief Get arena statistics
     */
    ArenaStats getStats() const noexcept;

private:
    // Extent 管理（红黑树）
    RBTree<Extent> extents_;
    
    // Per-size-class freelist
    FreeList freelists_[128];
    
    // Thread cache (optional)
    ThreadCache* tcache_;
    
    // Spinlock for arena operations
    SpinLock lock_;
};
```

### 3. Extent 管理

```cpp
/**
 * @brief Extent = continuous memory region (类似 jemalloc 的 extent)
 */
struct Extent {
    void*  base;          // Base address
    Size   size;          // Extent size (一般为 64 KB slab)
    UInt32 size_class;    // Size class index (0-31)
    UInt32 free_count;    // Number of free blocks
    UInt32 total_count;   // Total number of blocks
    FreeList freelist;    // Freelist for this extent
    
#ifdef IOX_ENABLE_LAZY_FREE
    // lazy-free 链相关字段 (仅在编译时启用)
    uint64_t lazy_free_timestamp_ms;  // 加入 lazy-free 链的时间戳 (ms)
    Extent*  next;                     // lazy-free 链中的下一个 extent
#endif
};
```

### 4. Thread Cache

```cpp
/**
 * @brief Thread-local cache (类似 TCMalloc 的 Per-CPU cache)
 */
class ThreadCache {
public:
    /**
     * @brief Allocate from thread cache
     * @param size_class Size class index
     * @return Pointer, or nullptr if cache miss
     */
    void* allocate(UInt32 size_class) noexcept;
    
    /**
     * @brief Deallocate to thread cache
     * @param ptr Pointer to block
     * @param size_class Size class index
     */
    void deallocate(void* ptr, UInt32 size_class) noexcept;
    
    /**
     * @brief Flush cache to arena
     */
    void flush() noexcept;

private:
    // Per-size-class cache bins
    struct CacheBin {
        void** entries;       // Cached pointers
        UInt32 count;         // Current count
        UInt32 low_water;     // Refill threshold
        UInt32 high_water;    // Flush threshold
    };
    
    CacheBin bins_[128];      // 128 size classes
    JemallocArena* arena_;    // Bound arena
};
```

---

## 🔧 实现步骤

### Phase 1: 准备工作 (Week 1)

1. **研究 jemalloc 源码**
   - 阅读 jemalloc 核心模块：arena.c, tcache.c, extent.c
   - 理解 size class 计算算法
   - 学习 extent 分裂和合并策略

2. **设计新的 size class 策略**
   - 定义 128 个 size class (8B-4MB)
   - 计算每个 class 的对齐要求
   - 确定 extent size (page 数量)

3. **设计 arena 架构**
   - 确定 arena 数量（建议：CPU 核心数 × 4）
   - 设计线程到 arena 的绑定策略
   - 设计 extent 管理数据结构（红黑树 vs B-tree）

### Phase 2: 核心实现 (Week 2-3)

4. **实现 JemallocArena 类**
   - 实现 extent 分配/释放
   - 实现 freelist 管理
   - 实现 extent 分裂和合并

5. **实现 ThreadCache 类**
   - 实现 cache bin 管理
   - 实现 refill/flush 逻辑
   - 实现线程绑定机制

6. **集成到 IoxAllocator**
   - 修改 IoxMemAPI::alloc() 路由逻辑
   - 实现 Handle ↔ Pointer 映射
   - 保持 Layer 3 API 不变

### Phase 3: 测试与优化 (Week 4)

7. **单元测试**
   - 测试 size class 计算
   - 测试 arena 分配/释放
   - 测试 extent 合并

8. **性能测试**
   - 对比 TCMalloc vs JemallocArena（64KB 以下）
   - 对比 JemallocArena vs BurstAllocator（64KB-4MB）
   - 测试多线程并发性能

9. **碎片率分析**
   - 测量内部碎片率（size class 浪费）
   - 测量外部碎片率（extent 碎片）
   - 对比 v0.2.0 vs v0.3.0

### Phase 4: 文档与发布 (Week 5)

10. **更新文档**
    - 更新 MEMORY_MANAGEMENT_GUIDE.md
    - 编写迁移指南
    - 更新性能指标

11. **发布 v0.3.0**
    - Tag git commit
    - 发布 Release Notes
    - 通知团队

---

## 📊 预期性能指标 (v0.3.0)

### 延迟对比

| 大小范围 | v0.2.0 (TCMalloc) | v0.3.0 (Hybrid) | 变化 |
|---------|-------------------|-----------------|------|
| ≤64KB | 10ns (P50) | 10ns (P50) | 不变 |
| 64KB-4MB | 100-500ns | 30-80ns | -60% ⬇️ |
| >4MB | 1-5μs | 1-5μs | 不变 |

### 碎片率对比

| 指标 | v0.2.0 | v0.3.0 | 变化 |
|------|--------|--------|------|
| 内部碎片率 | 15-25% | 5-10% | -10% ⬇️ |
| 外部碎片率 | 10-20% | 3-8% | -10% ⬇️ |
| 总内存效率 | 65-75% | 85-92% | +15% ⬆️ |

### 吞吐量对比

| 线程数 | v0.2.0 (ops/s) | v0.3.0 (ops/s) | 变化 |
|--------|----------------|----------------|------|
| 1 | 30M | 32M | +6.7% ⬆️ |
| 8 | 180M | 220M | +22% ⬆️ |
| 32 | 450M | 600M | +33% ⬆️ |

---

## ⚠️ 风险与缓解

### 风险 1: 性能回退
- **风险:** JemallocArena 延迟高于 TCMalloc
- **缓解:** 保留 TCMalloc 处理 ≤64KB，只用 JemallocArena 处理 64KB-4MB

### 风险 2: 碎片率增加
- **风险:** 不当的 size class 设计导致内部碎片
- **缓解:** 参考 jemalloc 经过验证的 size class 分布

### 风险 3: 复杂度增加
- **风险:** 多分配器并存，维护成本高
- **缓解:** 清晰的模块划分，充分的单元测试

### 风险 4: 调试困难
- **风险:** Handle 映射到多个分配器，调试复杂
- **缓解:** 完善的日志和调试工具，Handle 包含 region_id

---

## 🎯 成功标准

✅ **功能完整性**
- 所有现有 API 保持兼容
- 所有单元测试通过
- 无内存泄漏

✅ **性能提升**
- 64KB-4MB 分配延迟降低 ≥50%
- 多线程吞吐量提升 ≥20%
- 碎片率降低 ≥10%

✅ **代码质量**
- 代码覆盖率 ≥85%
- 无静态分析警告
- 文档完整

---

## 📦 核心数据结构总结

### 关键结构体定义

```cpp
/**
 * @brief Resident Region Header (Chunk 0, Slab 0)
 */
struct ResidentRegionHeader {
    uint64_t magic;                // 0x494F585F52455349 ("IOX_RESI")
    uint16_t version;              // 3 (v0.3.0)
    uint64_t total_size;           // 128 MiB (64-bit)
    uint32_t chunk_count;          // 64
    uint32_t tc_chunk_start;       // 1
    uint32_t tc_chunk_end;         // 3 (N+1, N=2 default)
    uint32_t dynamic_chunk_start;  // 4 (N+2)
    uint32_t dynamic_chunk_end;    // 63
    uint64_t bootstrap_offset;     // Bootstrap 自举区域偏移
};

/**
 * @brief Thread-Local Cache
 */
struct ThreadCache {
    uint64_t thread_id;            // 线程 ID
    TCBin    bins[32];             // 32 个 size class bins
    
#ifdef IOX_ENABLE_LAZY_FREE
    Extent*  lazy_free_list;       // lazy-free 链表头
#endif
    
    // 统计信息
    uint64_t total_alloc_count;
    uint64_t total_free_count;
    uint64_t refill_count;
    uint64_t flush_count;
};

/**
 * @brief TC Bin (per size class)
 */
struct TCBin {
    void**   blocks;               // 预分配的 block 指针数组
    uint32_t cursor;               // 当前分配游标
    uint32_t free_count;           // 空闲 block 数量
    uint32_t capacity;             // Bin 容量 (PRESET_IOXPOOL_COUNTS[i])
};

/**
 * @brief Extent (64 KB slab)
 */
struct Extent {
    uint64_t magic;                // 0x494F585F455854 ("IOX_EXT")
    uint32_t size_class;           // Size class index (0-31)
    uint32_t free_count;           // 空闲 block 数量
    uint32_t total_count;          // 总 block 数量
    uint8_t* allocation_bitmap;    // 分配位图 (每个 block 1 bit)
    
#ifdef IOX_ENABLE_LAZY_FREE
    uint64_t lazy_free_timestamp_ms;
    Extent*  next;
#endif
};

/**
 * @brief Burst Region Block
 */
struct BurstBlock {
    uint64_t size;                 // Block 大小
    uint64_t lease_timestamp_ms;   // 租约时间戳
    bool     is_free;              // 是否空闲
    BurstBlock* next;              // Freelist 下一个 block
    BurstBlock* prev;              // Freelist 上一个 block
};
```

### Size Class 配置数组

```cpp
// Size classes (32 classes, 8B-1024B)
constexpr size_t PRESET_IOXPOOL_SIZES[32] = {
    8, 16, 24, 32, 40, 48, 56, 64,           // 0-7: 8B step
    80, 96, 112, 128, 144, 160, 176, 192,    // 8-15: 16B step
    208, 224, 240, 256,                      // 16-19: 16B step
    288, 320, 352, 384, 416, 448, 480, 512,  // 20-27: 32B step
    640, 768, 896, 1024                      // 28-31: 128B step
};

// Pre-allocated count per size class (per TC slab)
constexpr uint32_t PRESET_IOXPOOL_COUNTS[32] = {
    100, 100, 80, 80, 80, 80, 40, 40,        // 8B-64B: 高频
    32, 32, 32, 32, 16, 16, 8, 8,            // 80B-192B: 中频
    8, 8, 4, 4,                              // 208B-256B: 中频
    4, 4, 4, 4, 2, 2, 2, 2,                  // 288B-512B: 低频
    2, 2, 2, 2                               // 640B-1024B: 极低频
};

/**
 * @brief 计算 size class index
 */
inline uint32_t calculate_size_class(size_t size) {
    for (uint32_t i = 0; i < 32; i++) {
        if (size <= PRESET_IOXPOOL_SIZES[i]) {
            return i;
        }
    }
    return UINT32_MAX;  // >1024B, 超出 TC 范围
}
```

---

## 📝 应用到主文档指南

### 需要更新的章节

#### 1. **Overview 章节**
- 更新版本号: v0.2.0 → v0.3.0
- 更新核心特性列表，移除 TCMalloc，添加 TC
- 更新性能指标: 碎片率 15-25% → 5-8%

#### 2. **Three-Tier Architecture 章节**
- **Layer 2 完全重写:**
  - 移除: TCMalloc + ResidentBackend
  - 新增: Thread-Local Cache (TC) + Dynamic Extent Region
  - 更新阈值: 256KB → 1024B
- **Layer 1 更新:**
  - Resident Region 布局细化 (Chunk 0/1-3/4-63)
  - 添加 TC Region 和 Dynamic Extent Region 说明

#### 3. **新增章节**
- **Thread-Local Cache (TC) Design**
  - TC 架构和工作原理
  - Size class 设计
  - Refill/Flush 机制
  - lazy-free 链优化
- **Dynamic Extent Management**
  - Extent 分配和初始化
  - Bitmap 管理
  - Extent 回收机制

#### 4. **Memory Region Details 章节**
- 更新 Resident Region 详细布局
- 添加 Chunk 分区说明
- 添加 TC Slab 和 Extent Slab 格式

#### 5. **API Reference 章节**
- 保留所有现有 API (向后兼容)
- 更新内部路由逻辑说明
- 添加 TC 相关配置 API

#### 6. **Performance Characteristics 章节**
- 更新延迟指标: TC Fast Path ~10ns
- 更新碎片率: 内部 5-8%, 外部 3-8%
- 更新吞吐量: +16-44% 提升

#### 7. **Configuration 章节**
- 添加 TC 配置选项
  - `IOX_ENABLE_LAZY_FREE` 编译宏
  - `refill_batch_size`, `flush_batch_size`
  - `lazy_free_max_depth`, `lazy_free_lease_ms`
- 添加 Size Class 配置说明

#### 8. **Version History 章节**
- 添加 v0.3.0 条目:
  - 移除 TCMalloc 依赖
  - 实现 Thread-Local Cache
  - 引入 lazy-free 链优化
  - 降低碎片率和延迟

### 迁移步骤

#### Step 1: 备份现有文档 ✅
```bash
# 已完成
MEMORY_MANAGEMENT_GUIDE_v0.2.0_TCMalloc_ARCHIVED.md
```

#### Step 2: 更新文档头部
```markdown
**Version:** 0.3.0 (IOX Deterministic Memory Pool)  
**Date:** 2025-12-09  
**Status:** Production - Thread-Local Cache + Extent Management  

> **📌 Architecture Change:** v0.3.0 removes TCMalloc dependency and implements 
> self-contained Thread-Local Cache with jemalloc-inspired design.
> Previous TCMalloc-based design (v0.2.0) is archived.
```

#### Step 3: 批量替换术语
```bash
# TCMalloc → TC (Thread-Local Cache)
# ResidentBackend → Resident Region Management
# 256KB threshold → 1024B threshold
# Per-CPU cache → Per-thread cache
# External allocator → Self-contained allocator
```

#### Step 4: 新增核心章节
- 添加 "IOX Deterministic Memory Pool Design"
- 添加 "Thread-Local Cache Architecture"
- 添加 "Dynamic Extent Management"
- 添加 "Lazy-Free Chain Optimization"

#### Step 5: 更新图表和示例
- 三层架构图
- Resident Region 布局图
- TC Slab 内存布局图
- 分配/释放流程图

#### Step 6: 更新代码示例
- malloc/free 实现示例
- TC Refill/Flush 伪代码
- Size class 计算函数
- Extent 管理代码

### 关键迁移点

1. **术语映射表**

| v0.2.0 | v0.3.0 | 说明 |
|--------|--------|------|
| TCMalloc | Thread-Local Cache (TC) | 自实现替换外部库 |
| ResidentBackend | Resident Region Management | 统一管理接口 |
| Per-CPU cache | Per-thread cache | TLS 替代 rseq |
| 256KB threshold | 1024B threshold | 精细化分配策略 |
| Span | Extent | 与 jemalloc 术语对齐 |

2. **性能指标对比表**

| 指标 | v0.2.0 | v0.3.0 | 备注 |
|------|--------|--------|------|
| P50 延迟 (小对象) | 10ns | 10-15ns | TC Fast Path |
| 内部碎片率 | 15-25% | 5-8% | 优化 size class |
| 外部碎片率 | 10-20% | 3-8% | lazy-free + extent 管理 |
| 吞吐量 (8线程) | 180M ops/s | 240M ops/s | +33% |

3. **API 兼容性保证**

```cpp
// Layer 3 API 完全兼容，无需修改用户代码
void* iox_alloc(size_t size, uint32_t flags);  // ✅ 不变
void  iox_free(void* ptr);                      // ✅ 不变
void* iox_alloc_burst(size_t size);             // ✅ 不变

// 内部路由逻辑变更 (用户无感知)
// v0.2.0: size <= 256KB → TCMalloc, else → Burst
// v0.3.0: size <= 1024B → TC, else → Burst
```

---

## 📚 参考资料

### jemalloc 核心文档
- [jemalloc GitHub](https://github.com/jemalloc/jemalloc)
- [jemalloc Paper: A Scalable Concurrent malloc(3) Implementation](http://www.bsdcan.org/2006/papers/jemalloc.pdf)
- [jemalloc tuning guide](https://github.com/jemalloc/jemalloc/wiki/Getting-Started)

### TCMalloc 文档
- [TCMalloc Overview](https://google.github.io/tcmalloc/)
- [TCMalloc Design Doc](https://google.github.io/tcmalloc/design.html)

### 相关论文
- [Hoard: A Scalable Memory Allocator for Multithreaded Applications](https://people.cs.umass.edu/~emery/pubs/berger-asplos2000.pdf)
- [Reconsidering Custom Memory Allocation](https://people.cs.umass.edu/~emery/pubs/berger-oopsla2002.pdf)

---

**文档版本:** 1.0.0 (Final)  
**最后更新:** 2025-12-09  
**状态:** Ready for MEMORY_MANAGEMENT_GUIDE.md Integration
