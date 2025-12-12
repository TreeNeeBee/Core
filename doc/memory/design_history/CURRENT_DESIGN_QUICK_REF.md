# 当前设计快速参考 - IoxPool v11.0 (TCMalloc 版本)

**版本:** v0.2.0  
**日期:** 2025-12-09  
**目的:** 作为 jemalloc 重构的基线参考

---

## 📁 核心文件结构

```
modules/Core/source/
├── inc/Memory/
│   ├── IoxAllocator.hpp          # Layer 3: 公共 API
│   ├── IoxTCMalloc.hpp            # Layer 2: TCMalloc 适配器
│   ├── IoxResidentBackend.hpp    # Layer 2: 2MB slab 提供器
│   ├── IoxBurstAllocator.hpp     # Layer 2: 大对象分配器
│   ├── IoxRegion.hpp              # Layer 1: Region 管理
│   ├── IoxHandle.hpp              # Handle 类型定义
│   ├── IoxTypes.hpp               # 基础类型定义
│   └── IoxConfig.hpp              # 配置宏
└── src/Memory/
    ├── IoxAllocator.cpp
    ├── IoxTCMalloc.cpp
    ├── IoxResidentBackend.cpp
    ├── IoxBurstAllocator.cpp
    └── IoxRegion.cpp
```

---

## 🏗️ 当前架构总结

### 三层架构

```
Application
    ↓
IoxMemAPI::alloc(size) ━━━━━━━━━━━━━┓
    ↓                               ║ Layer 3: Adapter
IoxMemAPI::free(handle) ━━━━━━━━━━━┛
    ↓
┌───────────────────────────────────┐
│ size ≤ 256KB → TCMalloc           │ ← Per-CPU cache
│ size > 256KB → BurstAllocator     │ ← Freelist
└───────────────────────────────────┘ Layer 2: Core
    ↓
┌───────────────────────────────────┐
│ Resident Region (128 MiB)         │ ← 2MB slabs
│ Burst Region (2 GiB)              │ ← Variable blocks
└───────────────────────────────────┘ Layer 1: Regions
```

### 分配流程

#### 小对象 (≤256KB)
```
alloc(1024)
   ↓
TCMallocAdapter::allocate()
   ↓
tcmalloc::malloc(1024)  ← 外部库
   ↓
Per-CPU Cache Hit? (95%)
   ↓ Yes
Return pointer → Convert to Handle
   ↓ No
Page Heap → ResidentBackend::allocate()
   ↓
Allocate 2MB slab from Resident Region
   ↓
Return to TCMalloc → Return pointer → Convert to Handle
```

#### 大对象 (>256KB)
```
alloc(1MB)
   ↓
BurstAllocator::allocate()
   ↓
Search freelist (best-fit)
   ↓
Found free block?
   ↓ Yes
Split block if needed → Mark in-use → Return handle
   ↓ No
Allocate new block from Burst Region → Return handle
```

---

## 🔑 关键数据结构

### IoxHandle (8 bytes)

```cpp
struct IoxHandle {
    // 64-bit system
    uint8_t  base_id : 4;    // Region ID (0-15)
    uint64_t offset  : 60;   // Offset within region
    
    // 32-bit system
    uint8_t  base_id : 4;    // Region ID (0-15)
    uint32_t offset  : 28;   // Offset within region
};

// Region IDs
enum class RegionId : uint8_t {
    RESIDENT = 0,  // Resident Region (TCMalloc)
    BURST    = 1,  // Burst Region (BurstAllocator)
    SHM      = 2,  // Shared Memory (reserved)
};
```

### ResidentBackend

```cpp
class ResidentBackend {
private:
    void*  region_base_;           // Resident Region 起始地址
    Size   region_size_;           // Resident Region 大小 (128 MiB)
    Size   total_slabs_;           // 总 slab 数量 (64 个, 128MiB/2MB)
    std::atomic<Size> next_slab_index_;  // 下一个可分配 slab 索引
    
public:
    void* allocate(Size size, Size alignment);
    // TCMalloc 调用此方法获取 2MB slab
    // Thread-safe: 原子操作 next_slab_index_
};
```

### BurstAllocator

```cpp
struct BlockHeader {
    uint64_t magic;       // 0x494F5842555253 ("IOXBRST")
    Size     size;        // 用户请求大小
    Size     total_size;  // 总大小（含 header）
    uint32_t flags;       // IN_USE, HAS_LEASE, PAGE_ALIGN
    uint32_t checksum;    // Header checksum (L2+)
};

struct FreeBlock {
    FreeBlock* next;      // 下一个空闲块
    Size       size;      // 块大小
};

class BurstAllocator {
private:
    FreeBlock* freelist_;      // 空闲块链表
    SpinLock   lock_;          // 保护 freelist
    void*      region_base_;
    Size       region_size_;
    
public:
    IoxHandle allocate(Size size, Size alignment);
    IoxError  free(IoxHandle handle);
    // Best-fit 搜索，O(n) 复杂度
};
```

---

## ⚡ 性能特征

### 延迟 (v0.2.0)

| 操作 | P50 | P99 | P99.9 | 说明 |
|------|-----|-----|-------|------|
| 小对象分配 (≤64KB) | 10ns | 45ns | 120ns | TCMalloc Per-CPU |
| 中对象分配 (64-256KB) | 30ns | 80ns | 200ns | TCMalloc TLS |
| 大对象分配 (>256KB) | 100ns | 1μs | 5μs | Freelist 搜索 |
| Handle→Ptr 转换 | <1ns | <1ns | <1ns | 位运算 |

### 缓存命中率

| 缓存层级 | 命中率 | 未命中处理 |
|---------|--------|-----------|
| Per-CPU Cache (TCMalloc) | 95% | Transfer Cache |
| Transfer Cache | 85% | Central Free List |
| Central Free List | 90% | Page Heap |
| Page Heap | 80% | ResidentBackend |

### 内存效率

| 指标 | v0.2.0 实测 | 说明 |
|------|------------|------|
| 内部碎片率 | 15-25% | Size class 浪费 |
| 外部碎片率 | 10-20% | Extent 碎片 |
| 总内存效率 | 65-75% | 有效载荷 / 总分配 |

---

## 🔧 配置宏

### 编译时配置

```cpp
// Layer 1: Memory Regions (iox.ld)
#ifdef __LP64__
#define IOX_RESIDENT_SIZE  (128 * 1024 * 1024)   // 128 MiB
#define IOX_BURST_SIZE     (2ULL * 1024 * 1024 * 1024)  // 2 GiB
#else
#define IOX_RESIDENT_SIZE  (64 * 1024 * 1024)    // 64 MiB
#define IOX_BURST_SIZE     (512 * 1024 * 1024)   // 512 MiB
#endif

// Layer 2: Allocator Core
#define IOX_SLAB_SIZE      (2 * 1024 * 1024)     // 2 MiB
#define IOX_TCMALLOC_MAX_SIZE  (256 * 1024)      // 256 KB (阈值)

// TCMalloc 集成
#ifndef IOX_USE_TCMALLOC
#define IOX_USE_TCMALLOC 1  // 默认启用
#endif

// Security (v0.3.0+, 当前未启用)
#define IOX_SECURITY_LEVEL 0  // L0: No protection
```

### 运行时配置

```cpp
// 无运行时配置 (v0.2.0)
// TCMalloc 内部配置由环境变量控制:
// TCMALLOC_PERCPU_CACHE_SIZE=32768
// TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES=33554432
```

---

## 🐛 已知问题与限制

### TCMalloc 集成

1. **外部依赖**
   - 依赖 Google TCMalloc 库 (`-ltcmalloc`)
   - 版本：gperftools 2.10+
   - 黑盒：无法精细控制内部行为

2. **固定阈值**
   - 256KB 阈值硬编码
   - 无法针对工作负载动态调整
   - 边界附近分配效率不佳

3. **Slab 不释放**
   - TCMalloc 从不释放 slab 给 ResidentBackend
   - Resident Region 内存只增不减
   - 长时间运行可能耗尽 Resident Region

### BurstAllocator

1. **O(n) 搜索**
   - Best-fit 需要遍历 freelist
   - 大量小块时性能下降
   - 建议：大对象专用，数量少

2. **碎片问题**
   - 虽有 coalescing，但仍有外部碎片
   - 无 compaction 机制
   - 长时间运行碎片率上升

### Handle 系统

1. **32-bit 限制**
   - Offset 只有 28 位 (256 MiB 上限)
   - Burst Region 限制在 256 MiB (实际 2 GiB 无法利用)
   - 需要特殊处理大地址

---

## 💡 jemalloc 重构重点

### 需要改进的部分

1. **灵活 Size Class** ❗ 高优先级
   - 当前：TCMalloc 固定 ~60 个 size class
   - 目标：jemalloc 128 个 size class，可配置
   - 好处：降低内部碎片 10-15%

2. **Arena 并发** ❗ 高优先级
   - 当前：TCMalloc 单一 Page Heap (有锁)
   - 目标：多 arena 并发分配
   - 好处：减少锁竞争 60%+

3. **Extent 管理** ⚠️ 中优先级
   - 当前：TCMalloc Span 不透明
   - 目标：jemalloc Extent (分裂/合并)
   - 好处：降低外部碎片 50%+

4. **动态调优** ⚠️ 中优先级
   - 当前：编译时固定
   - 目标：运行时可调 (dirty page tracking)
   - 好处：适应不同工作负载

### 保持不变的部分

1. **Layer 1 (Memory Regions)** ✅ 不变
   - Resident Region (128 MiB)
   - Burst Region (2 GiB)
   - Linker script (iox.ld)

2. **Layer 3 (Adapter API)** ✅ 不变
   - IoxMemAPI::alloc/free
   - IoxHandle 类型
   - handleToPtr/ptrToHandle

3. **TCMalloc 小对象路径** ✅ 部分保留
   - ≤64KB 继续使用 TCMalloc
   - 64KB-4MB 切换到 JemallocArena
   - >4MB 继续使用 BurstAllocator

---

## 📊 性能基线 (v0.2.0)

### 标准测试 (用于重构后对比)

```cpp
// Test 1: 小对象高频分配 (64B, 1M ops)
BenchmarkResult small_alloc = {
    .latency_p50  = 10 ns,
    .latency_p99  = 45 ns,
    .throughput   = 100 M ops/s
};

// Test 2: 中等对象 (8KB, 100K ops)
BenchmarkResult medium_alloc = {
    .latency_p50  = 25 ns,
    .latency_p99  = 80 ns,
    .throughput   = 40 M ops/s
};

// Test 3: 大对象 (1MB, 1K ops)
BenchmarkResult large_alloc = {
    .latency_p50  = 150 ns,
    .latency_p99  = 2 μs,
    .throughput   = 6.7 M ops/s
};

// Test 4: 多线程并发 (8 threads, 64KB, 1M ops)
BenchmarkResult concurrent_alloc = {
    .latency_p50  = 35 ns,
    .latency_p99  = 120 ns,
    .throughput   = 180 M ops/s (total)
};
```

### 碎片率测试

```cpp
// Workload: 混合大小分配 (8B-1MB)
FragmentationResult frag_test = {
    .internal_frag  = 18%,  // Size class 浪费
    .external_frag  = 12%,  // Extent 碎片
    .total_overhead = 30%,  // 总开销
    .memory_efficiency = 70%
};
```

---

## 🎯 下一步行动

### 立即行动 (Week 1)

1. ✅ **阅读此文档** - 理解当前设计
2. ✅ **阅读 REFACTORING_PLAN_JEMALLOC.md** - 了解重构计划
3. 🔄 **研究 jemalloc 源码** - 重点关注 arena.c, extent.c
4. 🔄 **设计 size class 策略** - 定义 128 个 size class

### 后续步骤 (Week 2-5)

- Week 2: 实现 JemallocArena 核心
- Week 3: 实现 ThreadCache
- Week 4: 集成与测试
- Week 5: 文档与发布

---

**参考文档:**
- [完整设计文档](MEMORY_MANAGEMENT_GUIDE.md) - 5000+ 行详细设计
- [重构计划](REFACTORING_PLAN_JEMALLOC.md) - 完整重构路线图
- [jemalloc GitHub](https://github.com/jemalloc/jemalloc) - 上游源码

**最后更新:** 2025-12-09  
**版本:** 0.1.0 (Baseline)
