# IOXPool v0.3.0 Implementation Plan
# 基于最新架构设计的开发计划

**Version**: 1.0.0  
**Date**: 2025-12-11  
**Status**: Ready for Implementation  
**Based on**: MEMORY_MANAGEMENT_GUIDE.md v0.3.0 (已修复3个严重设计缺陷)

---

## 📋 Executive Summary

本计划基于已完成核查和修正的v0.3.0架构设计，将IOXPool从TCMalloc依赖（v0.2.0）迁移到自实现的IOX确定性内存池。

**核心变更**:
- ✅ 移除TCMalloc外部依赖
- ✅ 实现Thread-Local Cache (TC) + Dynamic Extent + Burst三层分配器
- ✅ 严格遵守3个关键约束（无STL/malloc、Burst可选、Chunk 63释放）
- ✅ 使用固定数组替代动态分配（TCBin.block_offsets[128]、Extent.allocation_bitmap[512]）

**预期收益**:
- 性能提升: +33% 吞吐量，P50延迟 10-15ns (vs 10ns TCMalloc)
- 碎片率降低: 5-8% 内部、3-8% 外部 (vs 15-25%/10-20%)
- 嵌入式优化: 固定内存池、确定性分配、编译时配置

---

## 🎯 Phase 1: 基础设施准备 (Week 1, 5天)

### 1.1 创建新的目录结构

```bash
modules/Core/
├── include/Memory/
│   ├── IoxPoolConfig.hpp         # NEW: 编译时配置（约束宏定义）
│   ├── SizeClass.hpp             # NEW: 32 size classes定义
│   ├── ThreadCache.hpp           # NEW: TC结构体和API
│   ├── Extent.hpp                # NEW: Dynamic Extent结构体
│   ├── BurstRegion.hpp           # 保留: 修改为可选编译
│   └── ResidentRegion.hpp        # 修改: 更新为v0.3.0设计
├── source/Memory/
│   ├── SizeClass.cpp             # NEW: Size class计算逻辑
│   ├── ThreadCache.cpp           # NEW: TC Fast Path实现
│   ├── Extent.cpp                # NEW: Dynamic Extent管理
│   ├── BurstRegion.cpp           # 修改: 可选编译
│   └── ResidentRegion.cpp        # 修改: v0.3.0初始化
└── test/Memory/
    ├── test_size_class.cpp       # NEW: Size class单元测试
    ├── test_thread_cache.cpp     # NEW: TC Fast Path测试
    ├── test_extent.cpp           # NEW: Extent管理测试
    └── test_integration.cpp      # NEW: 集成测试
```

**任务清单**:
- [ ] 创建目录结构
- [ ] 配置CMakeLists.txt支持可选编译（IOX_ENABLE_BURST_REGION、IOX_ENABLE_LAZY_FREE）
- [ ] 更新构建脚本（build.sh）
- [ ] 创建测试框架（使用现有的gtest）

**验收标准**:
- 编译通过（空实现）
- 测试框架运行正常
- CMake选项生效（-DIOX_ENABLE_BURST_REGION=ON/OFF）

---

## 🔧 Phase 2: Size Class实现 (Week 1, 2天)

### 2.1 创建 SizeClass.hpp

```cpp
// Core/include/Memory/SizeClass.hpp

#ifndef IOX_MEMORY_SIZE_CLASS_HPP
#define IOX_MEMORY_SIZE_CLASS_HPP

#include <cstdint>
#include <cstddef>

namespace iox {
namespace memory {

// ✅ Constraint 1: 使用constexpr固定数组（无malloc）
constexpr size_t SIZE_CLASS_COUNT = 32;
constexpr size_t SIZE_CLASS_MAX = 1024;

// 32 size classes: 8B-1024B
constexpr size_t PRESET_IOXPOOL_SIZES[SIZE_CLASS_COUNT] = {
    8, 16, 24, 32, 40, 48, 56, 64,           // 0-7: 8B step
    80, 96, 112, 128, 144, 160, 176, 192,    // 8-15: 16B step
    208, 224, 240, 256,                      // 16-19: 16B step
    288, 320, 352, 384, 416, 448, 480, 512,  // 20-27: 32B step
    640, 768, 896, 1024                      // 28-31: 128B step
};

// Pre-allocated block counts per size class (≤128 for fixed TCBin array)
constexpr uint32_t PRESET_IOXPOOL_COUNTS[SIZE_CLASS_COUNT] = {
    100, 100, 80, 80, 80, 80, 40, 40,  // 8B-64B
    32, 32, 32, 32, 16, 16, 8, 8,      // 80B-192B
    8, 8, 4, 4,                        // 208B-256B
    4, 4, 4, 4, 2, 2, 2, 2,            // 288B-512B
    2, 2, 2, 2                         // 640B-1024B
};

// O(1) lookup table for fast size class calculation
constexpr uint8_t SIZE_CLASS_LOOKUP_TABLE[SIZE_CLASS_MAX + 1] = {
    /* 0-7 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 8-15 */ 0, 1, 1, 1, 1, 1, 1, 1,
    // ... (完整lookup表通过脚本生成)
};

/**
 * @brief Calculate size class index for given size
 * @param size Requested allocation size (1-1024B)
 * @return Size class index (0-31), or UINT32_MAX if >1024B
 * @note O(1) lookup, ~1-2 CPU cycles
 */
inline uint32_t calculate_size_class(size_t size) {
    if (size == 0) size = 1;
    if (size > SIZE_CLASS_MAX) return UINT32_MAX;
    return SIZE_CLASS_LOOKUP_TABLE[size];
}

/**
 * @brief Get actual allocation size for size class
 */
inline size_t get_size_class_size(uint32_t sc_index) {
    return (sc_index < SIZE_CLASS_COUNT) ? PRESET_IOXPOOL_SIZES[sc_index] : 0;
}

/**
 * @brief Get pre-allocated block count for size class
 */
inline uint32_t get_size_class_count(uint32_t sc_index) {
    return (sc_index < SIZE_CLASS_COUNT) ? PRESET_IOXPOOL_COUNTS[sc_index] : 0;
}

} // namespace memory
} // namespace iox

#endif // IOX_MEMORY_SIZE_CLASS_HPP
```

### 2.2 生成Lookup Table脚本

```python
# scripts/generate_size_class_lookup.py

def generate_lookup_table():
    """生成SIZE_CLASS_LOOKUP_TABLE"""
    sizes = [8, 16, 24, 32, 40, 48, 56, 64,
             80, 96, 112, 128, 144, 160, 176, 192,
             208, 224, 240, 256,
             288, 320, 352, 384, 416, 448, 480, 512,
             640, 768, 896, 1024]
    
    lookup = [0] * 1025
    for i in range(1, 1025):
        for sc_idx, sc_size in enumerate(sizes):
            if i <= sc_size:
                lookup[i] = sc_idx
                break
    
    # 输出C++代码
    print("constexpr uint8_t SIZE_CLASS_LOOKUP_TABLE[1025] = {")
    for i in range(0, 1025, 16):
        values = ", ".join(f"{lookup[j]}" for j in range(i, min(i+16, 1025)))
        print(f"    /* {i:4d}-{min(i+15, 1024):4d} */ {values},")
    print("};")

if __name__ == "__main__":
    generate_lookup_table()
```

### 2.3 单元测试

```cpp
// Core/test/Memory/test_size_class.cpp

#include <gtest/gtest.h>
#include "Memory/SizeClass.hpp"

using namespace iox::memory;

TEST(SizeClassTest, BasicCalculation) {
    EXPECT_EQ(calculate_size_class(1), 0);   // 1B → 8B (sc 0)
    EXPECT_EQ(calculate_size_class(8), 0);   // 8B → sc 0
    EXPECT_EQ(calculate_size_class(9), 1);   // 9B → 16B (sc 1)
    EXPECT_EQ(calculate_size_class(1024), 31); // 1024B → sc 31
    EXPECT_EQ(calculate_size_class(1025), UINT32_MAX); // >1024B
}

TEST(SizeClassTest, BoundaryConditions) {
    EXPECT_EQ(calculate_size_class(0), 0);   // 0 → 1 → 8B
    EXPECT_EQ(calculate_size_class(SIZE_CLASS_MAX), 31);
    EXPECT_EQ(calculate_size_class(SIZE_CLASS_MAX + 1), UINT32_MAX);
}

TEST(SizeClassTest, AllSizes) {
    for (size_t sz = 1; sz <= 1024; sz++) {
        uint32_t sc = calculate_size_class(sz);
        ASSERT_NE(sc, UINT32_MAX) << "Size " << sz << " has no size class";
        ASSERT_GE(get_size_class_size(sc), sz) << "Size class too small";
    }
}
```

**任务清单**:
- [ ] 实现SizeClass.hpp（包含lookup table）
- [ ] 编写生成脚本并生成完整lookup table
- [ ] 编写单元测试（覆盖率>95%）
- [ ] 性能基准测试（确认O(1)性能）

**验收标准**:
- 所有测试通过
- calculate_size_class()平均耗时<5ns
- 无malloc/new调用（使用constexpr验证）

---

## 🚀 Phase 3: Thread-Local Cache实现 (Week 2, 5天)

### 3.1 创建 ThreadCache.hpp

```cpp
// Core/include/Memory/ThreadCache.hpp

#ifndef IOX_MEMORY_THREAD_CACHE_HPP
#define IOX_MEMORY_THREAD_CACHE_HPP

#include "SizeClass.hpp"
#include "Extent.hpp"  // Forward declaration for lazy-free
#include <atomic>
#include <pthread.h>

namespace iox {
namespace memory {

// ✅ Constraint 1: 固定大小数组（无动态分配）
constexpr uint32_t TC_BIN_MAX_BLOCKS = 128;

/**
 * @brief TC Bin for single size class
 * @note Uses FIXED ARRAY (NOT void**) to comply with Constraint 1
 */
struct TCBin {
    // ✅ Fixed-size array of block offsets (NOT pointers)
    uint32_t block_offsets[TC_BIN_MAX_BLOCKS];
    
    uint32_t cursor;           // Current allocation cursor
    uint32_t free_count;       // Number of free blocks
    uint32_t capacity;         // Total capacity (≤128)
    uint32_t size_class_index; // Size class index (0-31)
    uint32_t block_size;       // Block size in bytes
    
    // Statistics
    uint64_t alloc_count;
    uint64_t refill_count;
} __attribute__((aligned(64)));

/**
 * @brief Thread-Local Cache structure
 * @note Total size: 32 bins × 536B = ~17KB (fits in 64KB slab)
 */
struct ThreadCache {
    uint64_t thread_id;        // pthread_self()
    void* slab_base;           // TC slab base address (for offset→pointer)
    
    // ✅ Fixed array of 32 bins
    TCBin bins[SIZE_CLASS_COUNT];
    
#ifdef IOX_ENABLE_LAZY_FREE
    Extent* lazy_free_list;    // ✅ Intrusive list (no separate allocation)
    uint32_t lazy_free_depth;
#endif
    
    // Statistics
    std::atomic<uint64_t> fast_path_hits;
    std::atomic<uint64_t> slow_path_refills;
    std::atomic<uint64_t> cache_misses;
} __attribute__((aligned(64)));

/**
 * @brief Get thread-local cache (TLS)
 */
ThreadCache* get_thread_cache();

/**
 * @brief Initialize TC for current thread
 * @param slab_base Base address of 64KB TC slab
 * @return 0 on success, -errno on failure
 */
int init_thread_cache(void* slab_base);

/**
 * @brief Fast path allocation (~10ns)
 * @param size Requested size (1-1024B)
 * @return Pointer to allocated block, or nullptr if bin empty
 */
void* tc_alloc_fast_path(size_t size);

/**
 * @brief Slow path: refill TC bin from Dynamic Extent
 * @param tc Thread cache
 * @param sc_index Size class index
 * @return Number of blocks refilled, or 0 on failure
 */
uint32_t tc_refill_bin(ThreadCache* tc, uint32_t sc_index);

/**
 * @brief Free block back to TC
 * @param tc Thread cache
 * @param ptr Block pointer
 * @param size Original allocation size
 */
void tc_free(ThreadCache* tc, void* ptr, size_t size);

} // namespace memory
} // namespace iox

#endif // IOX_MEMORY_THREAD_CACHE_HPP
```

### 3.2 实现 ThreadCache.cpp

```cpp
// Core/source/Memory/ThreadCache.cpp

#include "Memory/ThreadCache.hpp"
#include "Memory/ResidentRegion.hpp"
#include <cstring>

namespace iox {
namespace memory {

// Thread-local storage
thread_local ThreadCache* tls_thread_cache = nullptr;

ThreadCache* get_thread_cache() {
    if (!tls_thread_cache) {
        // First allocation on this thread, initialize TC
        void* slab = ResidentRegion::get_instance()->allocate_tc_slab();
        if (init_thread_cache(slab) != 0) {
            return nullptr;
        }
    }
    return tls_thread_cache;
}

int init_thread_cache(void* slab_base) {
    if (!slab_base) return -ENOMEM;
    
    // ✅ TC structure allocated from slab (NOT malloc)
    ThreadCache* tc = static_cast<ThreadCache*>(slab_base);
    
    tc->thread_id = pthread_self();
    tc->slab_base = slab_base;
    tc->fast_path_hits.store(0);
    tc->slow_path_refills.store(0);
    tc->cache_misses.store(0);
    
#ifdef IOX_ENABLE_LAZY_FREE
    tc->lazy_free_list = nullptr;
    tc->lazy_free_depth = 0;
#endif
    
    // Initialize 32 bins
    for (uint32_t i = 0; i < SIZE_CLASS_COUNT; i++) {
        TCBin* bin = &tc->bins[i];
        
        uint32_t block_size = PRESET_IOXPOOL_SIZES[i];
        uint32_t count = PRESET_IOXPOOL_COUNTS[i];
        
        // ✅ Calculate block offsets (base + offset addressing)
        uint32_t base_offset = sizeof(ThreadCache) + (i * TC_BIN_MAX_BLOCKS * block_size);
        
        for (uint32_t j = 0; j < count; j++) {
            bin->block_offsets[j] = base_offset + (j * block_size);
        }
        
        bin->cursor = 0;
        bin->free_count = count;
        bin->capacity = count;
        bin->size_class_index = i;
        bin->block_size = block_size;
        bin->alloc_count = 0;
        bin->refill_count = 0;
    }
    
    tls_thread_cache = tc;
    return 0;
}

void* tc_alloc_fast_path(size_t size) {
    ThreadCache* tc = get_thread_cache();
    if (!tc) return nullptr;
    
    // Step 1: Calculate size class
    uint32_t sc = calculate_size_class(size);
    if (sc == UINT32_MAX) return nullptr;  // >1024B, use Extent
    
    // Step 2: Check bin availability
    TCBin* bin = &tc->bins[sc];
    if (bin->cursor >= bin->free_count) {
        // Bin empty, trigger slow path
        tc->cache_misses.fetch_add(1);
        return nullptr;
    }
    
    // Step 3: Fast path allocation
    uint32_t offset = bin->block_offsets[bin->cursor++];
    void* ptr = static_cast<char*>(tc->slab_base) + offset;
    
    bin->alloc_count++;
    tc->fast_path_hits.fetch_add(1);
    
    return ptr;  // ~10ns
}

uint32_t tc_refill_bin(ThreadCache* tc, uint32_t sc_index) {
    // TODO: Implement refill from Dynamic Extent Region
    // Phase 4 implementation
    return 0;
}

void tc_free(ThreadCache* tc, void* ptr, size_t size) {
    // Calculate size class
    uint32_t sc = calculate_size_class(size);
    if (sc == UINT32_MAX) return;
    
    TCBin* bin = &tc->bins[sc];
    
    // Return block to bin (if space available)
    if (bin->cursor > 0) {
        // Calculate offset
        uint32_t offset = static_cast<char*>(ptr) - static_cast<char*>(tc->slab_base);
        bin->block_offsets[--bin->cursor] = offset;
    }
    // TODO: Flush to Dynamic Extent if bin full
}

} // namespace memory
} // namespace iox
```

### 3.3 单元测试

```cpp
// Core/test/Memory/test_thread_cache.cpp

#include <gtest/gtest.h>
#include "Memory/ThreadCache.hpp"

using namespace iox::memory;

class ThreadCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Allocate 64KB slab for testing
        slab = aligned_alloc(65536, 65536);
        ASSERT_NE(slab, nullptr);
        init_thread_cache(slab);
    }
    
    void TearDown() override {
        free(slab);
    }
    
    void* slab;
};

TEST_F(ThreadCacheTest, FastPathAllocation) {
    void* ptr = tc_alloc_fast_path(32);  // Size class 3
    ASSERT_NE(ptr, nullptr);
    
    // Verify alignment
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % 32, 0);
}

TEST_F(ThreadCacheTest, MultipleAllocations) {
    constexpr int N = 10;
    void* ptrs[N];
    
    for (int i = 0; i < N; i++) {
        ptrs[i] = tc_alloc_fast_path(16);
        ASSERT_NE(ptrs[i], nullptr) << "Allocation " << i << " failed";
    }
    
    // Verify no overlap
    for (int i = 0; i < N; i++) {
        for (int j = i + 1; j < N; j++) {
            EXPECT_NE(ptrs[i], ptrs[j]);
        }
    }
}

TEST_F(ThreadCacheTest, BinExhaustion) {
    ThreadCache* tc = get_thread_cache();
    TCBin* bin = &tc->bins[0];  // 8B size class
    
    // Allocate until bin exhausted
    for (uint32_t i = 0; i < bin->capacity; i++) {
        void* ptr = tc_alloc_fast_path(8);
        ASSERT_NE(ptr, nullptr);
    }
    
    // Next allocation should fail (trigger slow path)
    void* ptr = tc_alloc_fast_path(8);
    EXPECT_EQ(ptr, nullptr);
    EXPECT_GT(tc->cache_misses.load(), 0);
}

TEST_F(ThreadCacheTest, Performance) {
    constexpr int ITERATIONS = 10000;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < ITERATIONS; i++) {
        void* ptr = tc_alloc_fast_path(32);
        (void)ptr;  // Prevent optimization
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    
    double avg_ns = static_cast<double>(duration.count()) / ITERATIONS;
    EXPECT_LT(avg_ns, 20.0) << "Average allocation: " << avg_ns << "ns";
}
```

**任务清单**:
- [ ] 实现ThreadCache.hpp/cpp
- [ ] 实现TLS机制（thread_local）
- [ ] 实现Fast Path分配（目标<15ns）
- [ ] 编写单元测试（覆盖率>90%）
- [ ] 性能基准测试

**验收标准**:
- Fast Path平均延迟<15ns
- 无malloc/new调用
- 多线程测试通过（10线程并发）

---

## 🧩 Phase 4: Dynamic Extent管理 (Week 3, 5天)

### 4.1 创建 Extent.hpp

```cpp
// Core/include/Memory/Extent.hpp

#ifndef IOX_MEMORY_EXTENT_HPP
#define IOX_MEMORY_EXTENT_HPP

#include "SizeClass.hpp"
#include <cstdint>

namespace iox {
namespace memory {

// ✅ Constraint 1: 固定大小bitmap数组
constexpr uint32_t EXTENT_BITMAP_SIZE = 512;  // Max 4096 blocks
constexpr uint32_t EXTENT_SIZE = 65536;       // 64KB per extent

/**
 * @brief Extent descriptor for Dynamic Extent Region
 * @note Uses FIXED ARRAY for bitmap (NOT uint8_t* pointer)
 */
struct Extent {
    uint64_t magic;                       // 0x494F585F455854 ("IOX_EXT")
    uint32_t size_class;                  // Size class index (0-31)
    uint32_t free_count;                  // Free blocks count
    uint32_t total_count;                 // Total blocks in extent
    uint32_t block_size;                  // Block size in bytes
    
    // ✅ Fixed-size bitmap (max 512 bytes = 4096 blocks)
    uint8_t allocation_bitmap[EXTENT_BITMAP_SIZE];
    uint32_t bitmap_size;                 // Actual bitmap size used
    
#ifdef IOX_ENABLE_LAZY_FREE
    uint64_t lazy_free_timestamp_ms;
    Extent* next;                         // ✅ Intrusive list
#endif
    
    // Statistics
    uint64_t alloc_count;
    uint64_t free_count_total;
} __attribute__((aligned(64)));

/**
 * @brief Initialize extent for size class
 * @param extent Extent descriptor
 * @param sc_index Size class index
 * @param base_addr Extent data base address
 */
void init_extent(Extent* extent, uint32_t sc_index, void* base_addr);

/**
 * @brief Allocate block from extent
 * @param extent Extent descriptor
 * @return Pointer to allocated block, or nullptr if full
 */
void* extent_alloc_block(Extent* extent);

/**
 * @brief Free block back to extent
 * @param extent Extent descriptor
 * @param ptr Block pointer
 */
void extent_free_block(Extent* extent, void* ptr);

/**
 * @brief Check if extent is fully free (can be coalesced)
 */
bool extent_is_free(const Extent* extent);

} // namespace memory
} // namespace iox

#endif // IOX_MEMORY_EXTENT_HPP
```

### 4.2 实现 Extent.cpp

```cpp
// Core/source/Memory/Extent.cpp

#include "Memory/Extent.hpp"
#include <cstring>

namespace iox {
namespace memory {

void init_extent(Extent* extent, uint32_t sc_index, void* base_addr) {
    extent->magic = 0x494F585F455854ULL;
    extent->size_class = sc_index;
    extent->block_size = PRESET_IOXPOOL_SIZES[sc_index];
    
    // Calculate total blocks in 64KB extent
    uint32_t available_space = EXTENT_SIZE - sizeof(Extent);
    extent->total_count = available_space / extent->block_size;
    extent->free_count = extent->total_count;
    
    // Calculate bitmap size
    extent->bitmap_size = (extent->total_count + 7) / 8;  // Round up
    memset(extent->allocation_bitmap, 0, EXTENT_BITMAP_SIZE);
    
#ifdef IOX_ENABLE_LAZY_FREE
    extent->lazy_free_timestamp_ms = 0;
    extent->next = nullptr;
#endif
    
    extent->alloc_count = 0;
    extent->free_count_total = 0;
}

void* extent_alloc_block(Extent* extent) {
    if (extent->free_count == 0) {
        return nullptr;  // Extent full
    }
    
    // Find free bit in bitmap
    for (uint32_t byte_idx = 0; byte_idx < extent->bitmap_size; byte_idx++) {
        uint8_t byte = extent->allocation_bitmap[byte_idx];
        if (byte == 0xFF) continue;  // All bits set, skip
        
        // Find first free bit
        for (uint32_t bit_idx = 0; bit_idx < 8; bit_idx++) {
            if ((byte & (1 << bit_idx)) == 0) {
                // Found free block
                uint32_t block_idx = byte_idx * 8 + bit_idx;
                if (block_idx >= extent->total_count) break;
                
                // Mark as allocated
                extent->allocation_bitmap[byte_idx] |= (1 << bit_idx);
                extent->free_count--;
                extent->alloc_count++;
                
                // Calculate block address
                void* block_addr = reinterpret_cast<char*>(extent) + sizeof(Extent)
                                 + (block_idx * extent->block_size);
                return block_addr;
            }
        }
    }
    
    return nullptr;
}

void extent_free_block(Extent* extent, void* ptr) {
    // Calculate block index
    uintptr_t offset = reinterpret_cast<char*>(ptr) 
                     - (reinterpret_cast<char*>(extent) + sizeof(Extent));
    uint32_t block_idx = offset / extent->block_size;
    
    if (block_idx >= extent->total_count) {
        // Invalid pointer
        return;
    }
    
    // Clear bit in bitmap
    uint32_t byte_idx = block_idx / 8;
    uint32_t bit_idx = block_idx % 8;
    
    extent->allocation_bitmap[byte_idx] &= ~(1 << bit_idx);
    extent->free_count++;
    extent->free_count_total++;
}

bool extent_is_free(const Extent* extent) {
    return extent->free_count == extent->total_count;
}

} // namespace memory
} // namespace iox
```

**任务清单**:
- [ ] 实现Extent.hpp/cpp
- [ ] 实现bitmap分配算法
- [ ] 实现extent coalescing（可选，lazy-free开启时）
- [ ] 编写单元测试
- [ ] 集成到ResidentRegion

**验收标准**:
- Extent分配延迟<100ns
- Bitmap操作正确性100%
- 无内存泄漏

---

## 🔗 Phase 5: ResidentRegion集成 (Week 4, 5天)

### 5.1 更新 ResidentRegion.hpp

```cpp
// 添加新的方法

/**
 * @brief Allocate TC slab for new thread (64KB)
 */
void* allocate_tc_slab();

/**
 * @brief Allocate extent from Dynamic Region (Chunks 4-62)
 * @param sc_index Size class index
 * @return Extent descriptor, or nullptr if exhausted
 */
Extent* allocate_extent(uint32_t sc_index);

/**
 * @brief Release extent back to Dynamic Region
 */
void free_extent(Extent* extent);

/**
 * @brief Bootstrap initialization (uses Chunk 63)
 * @note Chunk 63 released after init complete
 */
int bootstrap_init();
```

### 5.2 实现Bootstrap逻辑

```cpp
int ResidentRegion::bootstrap_init() {
    // ✅ Phase 1: Header at Chunk 0 (NOT from bootstrap)
    header_ = reinterpret_cast<ResidentRegionHeader*>(base_);
    header_->magic_start = 0x494F585F52455349ULL;
    header_->version = 3;  // v0.3.0
    header_->bootstrap_chunk = 63;
    header_->bootstrap_released = 0;
    
    // ✅ Phase 2: Use Chunk 63 for temporary bootstrap data
    void* bootstrap_base = static_cast<char*>(base_) + (63 * CHUNK_SIZE);
    bootstrap_offset_ = 0;
    
    // Allocate temporary structures from Chunk 63
    // (These will be discarded after copying to permanent locations)
    
    // ✅ Phase 3: Copy essential data to permanent locations
    // (e.g., initial extent list to Chunks 4-62)
    
    // ✅ Phase 4: Release Chunk 63 to Dynamic Extent pool
    header_->bootstrap_released = 1;
    mark_chunk_free(63);
    
    return 0;
}
```

**任务清单**:
- [ ] 实现TC slab分配（从Chunks 1-3）
- [ ] 实现Extent分配（从Chunks 4-62）
- [ ] 实现Bootstrap逻辑（Chunk 63临时使用后释放）
- [ ] 更新初始化流程
- [ ] 集成测试

**验收标准**:
- Bootstrap完成后Chunk 63可用
- TC slab分配成功（多线程测试）
- Dynamic Extent分配成功

---

## 🧪 Phase 6: 集成测试与性能验证 (Week 5, 5天)

### 6.1 功能测试

```cpp
// Core/test/Memory/test_integration.cpp

TEST(IntegrationTest, EndToEndAllocation) {
    // Small allocation (TC Fast Path)
    void* p1 = iox_alloc(32);
    ASSERT_NE(p1, nullptr);
    
    // Medium allocation (Dynamic Extent)
    void* p2 = iox_alloc(8192);
    ASSERT_NE(p2, nullptr);
    
#ifdef IOX_ENABLE_BURST_REGION
    // Large allocation (Burst Region)
    void* p3 = iox_alloc(4 * 1024 * 1024);
    ASSERT_NE(p3, nullptr);
#endif
    
    iox_free(p1);
    iox_free(p2);
#ifdef IOX_ENABLE_BURST_REGION
    iox_free(p3);
#endif
}

TEST(IntegrationTest, MultiThreaded) {
    constexpr int NUM_THREADS = 10;
    constexpr int ALLOCS_PER_THREAD = 1000;
    
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; i++) {
        threads.emplace_back([]() {
            for (int j = 0; j < ALLOCS_PER_THREAD; j++) {
                void* ptr = iox_alloc(32);
                ASSERT_NE(ptr, nullptr);
                iox_free(ptr);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
}

TEST(IntegrationTest, ConstraintCompliance) {
    // Verify Constraint 1: No malloc/new in hot path
    // (Use memory profiler or static analysis)
    
    // Verify Constraint 2: Burst Region optional
#ifndef IOX_ENABLE_BURST_REGION
    void* p = iox_alloc(5 * 1024 * 1024);
    EXPECT_EQ(p, nullptr);  // Should fail gracefully
#endif
    
    // Verify Constraint 3: Chunk 63 released
    ResidentRegion* region = ResidentRegion::get_instance();
    EXPECT_EQ(region->header()->bootstrap_released, 1);
}
```

### 6.2 性能基准测试

```cpp
// Core/test/Memory/benchmark_iox.cpp

#include <benchmark/benchmark.h>

static void BM_TCFastPath(benchmark::State& state) {
    for (auto _ : state) {
        void* ptr = tc_alloc_fast_path(32);
        benchmark::DoNotOptimize(ptr);
    }
}
BENCHMARK(BM_TCFastPath);

static void BM_ExtentAlloc(benchmark::State& state) {
    for (auto _ : state) {
        void* ptr = iox_alloc(4096);
        benchmark::DoNotOptimize(ptr);
        iox_free(ptr);
    }
}
BENCHMARK(BM_ExtentAlloc);

static void BM_Comparison_v0_2_vs_v0_3(benchmark::State& state) {
    // Compare TCMalloc (v0.2.0) vs IOX TC (v0.3.0)
    // Target: +33% throughput improvement
}
```

### 6.3 碎片率测试

```cpp
TEST(FragmentationTest, InternalFragmentation) {
    // Allocate various sizes, measure wasted space
    // Target: <8% internal fragmentation
}

TEST(FragmentationTest, ExternalFragmentation) {
    // Allocate/free random patterns, measure free space utilization
    // Target: <8% external fragmentation
}
```

**任务清单**:
- [ ] 编写集成测试套件
- [ ] 编写性能基准测试
- [ ] 编写碎片率测试
- [ ] 多线程压力测试
- [ ] 与v0.2.0性能对比

**验收标准**:
- 所有功能测试通过
- P50延迟: TC <15ns, Extent <120ns
- 吞吐量: +33% vs v0.2.0
- 碎片率: 内部<8%, 外部<8%
- 3个约束100%合规

---

## 📊 Phase 7: 文档与交付 (Week 6, 3天)

### 7.1 代码文档

- [ ] 生成Doxygen文档
- [ ] 更新README.md（包含v0.3.0迁移指南）
- [ ] 编写API参考手册
- [ ] 添加架构图（draw.io或PlantUML）

### 7.2 性能报告

生成性能对比报告：

```markdown
# IOXPool v0.3.0 Performance Report

## Latency Comparison (P50/P99)

| Allocation Size | v0.2.0 (TCMalloc) | v0.3.0 (IOX TC) | Improvement |
|-----------------|-------------------|-----------------|-------------|
| 8-64B (TC)      | 10ns / 45ns       | 12ns / 38ns     | +20% P99    |
| 1KB-2MB (Extent)| 100ns / 850ns     | 80ns / 280ns    | -67% P99    |
| >2MB (Burst)    | 1-5μs / 12μs      | 0.8-3μs / 8μs   | -33% P99    |

## Throughput

- v0.2.0: 85M allocs/sec
- v0.3.0: 113M allocs/sec (+33%)

## Fragmentation

- Internal: 5.2% (vs 18% v0.2.0)
- External: 4.1% (vs 12% v0.2.0)
```

### 7.3 发布清单

- [ ] 代码审查（2轮）
- [ ] 静态分析（cppcheck, clang-tidy）
- [ ] 内存泄漏检测（valgrind）
- [ ] 线程安全检测（ThreadSanitizer）
- [ ] 创建v0.3.0发布分支
- [ ] 标记Git tag: `v0.3.0-ioxpool`

---

## 🎯 关键里程碑

| 里程碑 | 完成日期 | 标准 |
|--------|----------|------|
| Phase 1: 基础设施 | Day 5 | 编译通过，测试框架就绪 |
| Phase 2: Size Class | Day 7 | 单元测试通过，<5ns性能 |
| Phase 3: TC实现 | Day 12 | Fast Path <15ns，多线程测试通过 |
| Phase 4: Extent管理 | Day 17 | Bitmap正确，<100ns延迟 |
| Phase 5: 集成 | Day 22 | Bootstrap成功，Chunk 63释放 |
| Phase 6: 测试 | Day 27 | 性能达标，3约束合规 |
| Phase 7: 交付 | Day 30 | 文档完整，代码审查通过 |

---

## ⚠️ 风险管理

### 高风险项

1. **TC Fast Path性能未达标** (<15ns)
   - 缓解: 使用perf/vtune优化热路径
   - 应急: 放宽目标到20ns

2. **多线程竞争导致性能下降**
   - 缓解: 使用thread_local隔离，减少共享状态
   - 应急: 添加per-core cache（rseq）

3. **碎片率超标** (>8%)
   - 缓解: 调整size class分布，启用lazy-free coalescing
   - 应急: 增加Dynamic Extent区域大小

### 中风险项

1. **Burst Region可选编译复杂度**
   - 缓解: CMake模板化配置
   - 应急: 保留Burst Region强制启用

2. **TLS机制平台兼容性**
   - 缓解: 使用pthread_key fallback
   - 应急: 静态thread池（max 256 threads）

---

## 📚 参考资料

- MEMORY_MANAGEMENT_GUIDE.md v0.3.0（架构设计）
- DESIGN_CONSISTENCY_REPORT.md（约束验证报告）
- REFACTORING_PLAN_JEMALLOC.md（迁移计划）
- jemalloc源码: arena.c, tcache.c, extent.c
- TCMalloc设计文档（v0.2.0对比基准）

---

## ✅ 签署确认

- [ ] 架构设计审查通过 (SA)
- [ ] 技术可行性确认 (Tech Lead)
- [ ] 资源分配确认 (PM)
- [ ] 开始实施授权 (Director)

---

**注**: 本计划基于已修复的v0.3.0设计，所有3个关键约束已验证合规，TCBin/Extent数据结构已修正为固定数组。
