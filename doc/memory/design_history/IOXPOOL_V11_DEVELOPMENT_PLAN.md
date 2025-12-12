# IoxPool v11.0 Development Plan - Clean Slate Implementation

**Version:** 1.0  
**Date:** 2025-12-07  
**Status:** Active Development  
**Approach:** Fresh implementation (no backward compatibility)

---

## 🎯 Executive Summary

This document outlines the **ground-up implementation** of IoxPool v11.0, LightAP's next-generation memory management system based on:
- **TCMalloc** (Google's proven Per-CPU allocator)
- **Region-based architecture** (zero fragmentation)
- **Handle-based safety** (no raw pointer exposure)
- **4-tier security** (L0-L3 configurable protection)

**Key Decision:** Legacy PoolAllocator/MemoryTracker archived (no migration burden).

---

## 📊 Architecture Overview

### Three-Layer Design

```
┌─────────────────────────────────────────────────────────────────┐
│  Layer 3: Adapter Layer (Public Interfaces)                    │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ IoxMemory.hpp     - C++ facade (iox_alloc/iox_free)     │  │
│  │ IoxHandle.hpp     - Handle ↔ Pointer conversion         │  │
│  │ IoxAllocator<T>   - STL allocator adapter              │  │
│  │ operator new/delete - Global overrides                  │  │
│  └──────────────────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│  Layer 2: Allocator Core (Management Logic)                    │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ IoxAllocatorCore.hpp - Main allocator logic            │  │
│  │ TCMallocBackend.hpp  - Per-CPU cache (≤256KB)          │  │
│  │ ResidentBackend.hpp  - 2MB slab provider for TCMalloc  │  │
│  │ BurstAllocator.hpp   - Large blocks (>256KB)           │  │
│  │ IoxMonitor.hpp       - Statistics & security           │  │
│  └──────────────────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│  Layer 1: Region Configuration (Foundation)                     │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │ IoxRegion.hpp     - Region initialization             │  │
│  │ iox_linker.ld     - .iox section definition            │  │
│  │ IoxPageAlign.hpp  - Huge page support (4K/2M/1G)      │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘

Shared:
  IoxTypes.hpp   - Core type definitions (IoxHandle, Size, etc.)
  IoxConfig.hpp  - Compile-time configuration
```

---

## 📁 New File Structure

```
modules/Core/source/
├── inc/
│   ├── Memory/                          # NEW: All IoxPool headers
│   │   ├── IoxTypes.hpp                 # Core types and constants
│   │   ├── IoxConfig.hpp                # Configuration macros
│   │   ├── IoxHandle.hpp                # Handle system
│   │   ├── IoxRegion.hpp                # Layer 1: Region management
│   │   ├── IoxPageAlign.hpp             # Huge page utilities
│   │   ├── IoxAllocatorCore.hpp         # Layer 2: Main allocator
│   │   ├── TCMallocBackend.hpp          # TCMalloc integration
│   │   ├── ResidentBackend.hpp          # Resident region backend
│   │   ├── BurstAllocator.hpp           # Burst region allocator
│   │   ├── IoxMonitor.hpp               # Statistics & monitoring
│   │   ├── IoxSecurity.hpp              # Security tiers (L0-L3)
│   │   └── IoxMemory.hpp                # Layer 3: Public API facade
│   ├── CMemory.hpp                      # DEPRECATED: Compatibility stub
│   └── archive/
│       ├── legacy_v1/                   # Archived v1.x implementation
│       │   ├── MemoryManager.hpp
│       │   ├── PoolAllocator.hpp
│       │   └── MemoryTracker.hpp
│       └── LEGACY_MIGRATION.md          # Archive documentation
├── src/
│   ├── Memory/                          # NEW: All IoxPool implementations
│   │   ├── IoxRegion.cpp
│   │   ├── IoxAllocatorCore.cpp
│   │   ├── TCMallocBackend.cpp
│   │   ├── ResidentBackend.cpp
│   │   ├── BurstAllocator.cpp
│   │   ├── IoxMonitor.cpp
│   │   ├── IoxSecurity.cpp
│   │   └── IoxMemory.cpp
│   └── archive/
│       └── legacy_v1/                   # Archived implementations
│           ├── MemoryManager.cpp
│           └── MemoryTracker.cpp
└── linker/
    └── iox_regions.ld                   # NEW: Linker script for .iox section
```

---

## 🗓️ Development Phases

### Phase 1: Foundation (Week 1-2)

#### 1.1 Core Type System
**Files:** `IoxTypes.hpp`, `IoxConfig.hpp`

```cpp
// IoxTypes.hpp - Platform-agnostic types
namespace iox {

// Handle definition (64-bit: 4-bit base_id + 60-bit offset)
#if IOX_ARCH_64BIT
struct IoxHandle {
    uint64_t value;  // [63:60] base_id, [59:0] offset
};
#else
struct IoxHandle {
    uint32_t value;  // [31:28] base_id, [27:0] offset
};
#endif

// Configuration
struct IoxPoolConfig {
    size_t resident_size;     // ResidentRegion size (default: 64MB)
    size_t burst_size;        // BurstRegion size (default: 128MB)
    bool enable_huge_pages;   // Use 2MB pages
    SecurityTier security;    // L0/L1/L2/L3
};

} // namespace iox
```

**Tasks:**
- [ ] Define `IoxHandle` structure (32/64-bit variants)
- [ ] Define `IoxPoolConfig` with sane defaults
- [ ] Create `IoxError` enum for error codes
- [ ] Implement compile-time platform detection

**Success Criteria:** Compiles on x86_64 and armv7

---

#### 1.2 Region Configuration
**Files:** `IoxRegion.hpp/.cpp`, `iox_regions.ld`

```cpp
// IoxRegion.hpp - Memory region management
namespace iox {

class RegionManager {
public:
    static RegionManager& instance();
    
    // Initialize regions from .iox section
    IoxError initialize(const IoxPoolConfig& config);
    
    // Query region info
    const RegionInfo& getResidentRegion() const;
    const RegionInfo& getBurstRegion() const;
    
    // Convert between handle and pointer
    void* handleToPtr(IoxHandle h) const;
    IoxHandle ptrToHandle(void* ptr) const;
    
private:
    RegionInfo resident_region_;  // Fixed base from linker
    RegionInfo burst_region_;     // Fixed base from linker
};

} // namespace iox
```

**Linker Script (iox_regions.ld):**
```ld
SECTIONS {
    .iox_resident (NOLOAD) : ALIGN(2M) {
        __iox_resident_start = .;
        . = . + 64M;  /* Default resident size */
        __iox_resident_end = .;
    }
    
    .iox_burst (NOLOAD) : ALIGN(2M) {
        __iox_burst_start = .;
        . = . + 128M;  /* Default burst size */
        __iox_burst_end = .;
    }
}
```

**Tasks:**
- [ ] Implement `RegionManager::initialize()`
- [ ] Add linker script to CMakeLists.txt
- [ ] Implement handle ↔ pointer conversion (inline, <1ns)
- [ ] Add boundary checks (debug builds)
- [ ] Test on 4K/2M/1G page sizes

**Success Criteria:** 
- `handleToPtr()` completes in <1ns
- Handle validation catches out-of-bounds

---

### Phase 2: Handle System (Week 2-3)

#### 2.1 Handle Operations
**Files:** `IoxHandle.hpp` (inline-heavy)

```cpp
// IoxHandle.hpp - Fast handle conversion
namespace iox {

// Encode pointer to handle
inline IoxHandle ptrToHandle(void* ptr, uint8_t base_id) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    IoxHandle h;
#if IOX_ARCH_64BIT
    h.value = (static_cast<uint64_t>(base_id) << 60) | (addr & 0x0FFFFFFFFFFFFFFFULL);
#else
    h.value = (static_cast<uint32_t>(base_id) << 28) | (addr & 0x0FFFFFFFU);
#endif
    return h;
}

// Decode handle to pointer
inline void* handleToPtr(IoxHandle h, void* base) {
#if IOX_ARCH_64BIT
    uint64_t offset = h.value & 0x0FFFFFFFFFFFFFFFULL;
#else
    uint32_t offset = h.value & 0x0FFFFFFFU;
#endif
    return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(base) + offset);
}

// Extract base_id
inline uint8_t getBaseId(IoxHandle h) {
#if IOX_ARCH_64BIT
    return static_cast<uint8_t>(h.value >> 60);
#else
    return static_cast<uint8_t>(h.value >> 28);
#endif
}

} // namespace iox
```

**Tasks:**
- [ ] Implement handle encoding/decoding
- [ ] Add null handle constant (`IOX_HANDLE_NULL`)
- [ ] Implement `isValidHandle()` with bounds checking
- [ ] Optimize for branch prediction (likely valid)
- [ ] Benchmark handle operations (<1ns target)

**Success Criteria:**
- Handle conversion: <1ns (confirmed via benchmark)
- No false positives in boundary checks

---

### Phase 3: TCMalloc Integration (Week 3-5)

#### 3.1 Dependency Setup
**Files:** `CMakeLists.txt`, `FindTCMalloc.cmake`

```cmake
# CMakeLists.txt additions
find_package(TCMalloc REQUIRED)
target_link_libraries(lap_core PRIVATE TCMalloc::tcmalloc_minimal)

# Enable Per-CPU cache if kernel ≥4.18
if(KERNEL_VERSION VERSION_GREATER_EQUAL "4.18")
    target_compile_definitions(lap_core PRIVATE IOX_ENABLE_PERCPU_CACHE)
endif()
```

**Tasks:**
- [ ] Add gperftools/tcmalloc to dependencies
- [ ] Create `FindTCMalloc.cmake` module
- [ ] Detect kernel rseq support (runtime check)
- [ ] Add compile flag for Per-CPU vs. TLS fallback

---

#### 3.2 Resident Backend
**Files:** `ResidentBackend.hpp/.cpp`

```cpp
// ResidentBackend.hpp - Provides 2MB slabs to TCMalloc
namespace iox {

class ResidentBackend {
public:
    explicit ResidentBackend(void* base, size_t size);
    
    // Called by TCMalloc PageHeap
    void* allocateSlab(size_t slab_size);  // 64KB-2MB
    void deallocateSlab(void* slab);
    
    // Statistics
    size_t getTotalSlabs() const;
    size_t getUsedSlabs() const;
    
private:
    void* base_;
    size_t total_size_;
    Bitmap slab_bitmap_;  // 1 bit per 2MB slab
    std::atomic<size_t> used_slabs_{0};
};

} // namespace iox
```

**Tasks:**
- [ ] Implement unified bitmap (1 bit per 2MB slab)
- [ ] Implement `allocateSlab()` with first-fit
- [ ] Add coalescing on `deallocateSlab()`
- [ ] Thread-safety via atomic operations
- [ ] Zero fragmentation guarantee

**Success Criteria:**
- Slab allocation: <100ns
- Zero internal fragmentation

---

#### 3.3 TCMalloc Backend
**Files:** `TCMallocBackend.hpp/.cpp`

```cpp
// TCMallocBackend.hpp - TCMalloc integration layer
namespace iox {

class TCMallocBackend {
public:
    IoxError initialize(ResidentBackend* backend);
    
    // Main allocation paths
    void* alloc(size_t size);       // ≤256KB fast path
    void free(void* ptr);
    
    // Size classes (TCMalloc standard)
    static constexpr size_t MAX_SIZE = 256 * 1024;  // 256KB
    
private:
    ResidentBackend* backend_;
    
    // Hook into TCMalloc's SystemAlloc
    static void* systemAlloc(size_t size, size_t alignment);
};

} // namespace iox
```

**Tasks:**
- [ ] Override TCMalloc's `SystemAlloc()` to use ResidentBackend
- [ ] Configure size classes (8, 16, 32, ..., 256KB)
- [ ] Enable Per-CPU cache via `TC_PERCPU_TCMALLOC_ENABLED`
- [ ] Implement fast path (<10ns for thread cache hits)
- [ ] Add telemetry hooks

**Success Criteria:**
- Thread cache hit: <10ns
- Central freelist hit: <50ns
- PageHeap allocation: <100ns

---

### Phase 4: Burst Region (Week 5-6)

#### 4.1 Burst Allocator
**Files:** `BurstAllocator.hpp/.cpp`

```cpp
// BurstAllocator.hpp - Large block allocator (>256KB)
namespace iox {

class BurstAllocator {
public:
    explicit BurstAllocator(void* base, size_t size);
    
    // Allocation with optional lease
    IoxHandle alloc(size_t size, uint32_t lease_ms = 0);
    void free(IoxHandle handle);
    
    // Lease management (background thread)
    void startLeaseScanner();
    void stopLeaseScanner();
    
private:
    struct ChunkHeader {
        size_t size;
        uint32_t lease_expiry;  // 0 = no lease
        uint32_t canary;
        ChunkHeader* next;
        ChunkHeader* prev;
    };
    
    FreeList freelist_;
    std::thread lease_scanner_;
};

} // namespace iox
```

**Tasks:**
- [ ] Implement freelist with best-fit
- [ ] Add chunk header (size + lease + canary)
- [ ] Implement coalescing (merge adjacent free chunks)
- [ ] Background lease scanner (optional, disabled by default)
- [ ] Add statistics (fragmentation ratio)

**Success Criteria:**
- Allocation: <100ns
- Coalescing efficiency: >90%
- Lease reclamation: <1ms latency

---

### Phase 5: Core Allocator (Week 6-7)

#### 5.1 Main Allocator
**Files:** `IoxAllocatorCore.hpp/.cpp`

```cpp
// IoxAllocatorCore.hpp - Main allocator logic
namespace iox {

class IoxAllocatorCore {
public:
    static IoxAllocatorCore& instance();
    
    // Main API
    IoxHandle alloc(size_t size, uint32_t lease_ms = 0);
    void free(IoxHandle handle);
    
    // Handle conversion
    void* handleToPtr(IoxHandle h) const;
    IoxHandle ptrToHandle(void* ptr) const;
    
    // Statistics
    IoxStats getStats() const;
    
private:
    RegionManager& regions_;
    TCMallocBackend tcmalloc_;
    BurstAllocator burst_;
    
    // Route allocation based on size
    IoxHandle allocSmall(size_t size);   // ≤256KB → TCMalloc
    IoxHandle allocLarge(size_t size);   // >256KB → Burst
};

} // namespace iox
```

**Allocation Logic:**
```cpp
IoxHandle IoxAllocatorCore::alloc(size_t size, uint32_t lease_ms) {
    if (size <= TCMallocBackend::MAX_SIZE) {
        return allocSmall(size);  // TCMalloc path
    } else {
        return allocLarge(size);  // Burst path
    }
}
```

**Tasks:**
- [ ] Implement routing logic (size-based)
- [ ] Integrate TCMalloc and Burst allocators
- [ ] Add error handling (OOM)
- [ ] Implement statistics collection
- [ ] Add thread-safety for global state

**Success Criteria:**
- Routing overhead: <2ns
- Statistics: lock-free reads

---

### Phase 6: Security & Monitoring (Week 7-9)

#### 6.1 Security Tiers
**Files:** `IoxSecurity.hpp/.cpp`

```cpp
// IoxSecurity.hpp - 4-tier security system
namespace iox {

enum class SecurityTier {
    L0_NONE,           // No checks
    L1_FAST_CANARY,    // Header canary only (default)
    L2_FULL_SAFETY,    // RedZone + Quarantine
    L3_CRITICAL_REDUN  // Data mirroring (future)
};

// L1: Fast canary (8 bytes overhead)
struct ChunkHeaderL1 {
    uint64_t canary;
    size_t size;
};

// L2: Full safety (176 bytes overhead)
struct ChunkHeaderL2 {
    uint64_t header_canary;
    size_t size;
    uint8_t redzone_front[64];
    // ... user data ...
    uint8_t redzone_back[64];
    uint64_t trailer_canary;
};

} // namespace iox
```

**Tasks:**
- [ ] Implement L1 canary checks (fast path)
- [ ] Implement L2 RedZone validation
- [ ] Add quarantine for L2 (256-512 chunks)
- [ ] Auto-mprotect on breach detection
- [ ] Compile-time tier selection

**Success Criteria:**
- L1 overhead: <1% CPU, +8B/chunk
- L2 overhead: <5% CPU, +176B/chunk
- Detection rate: 100% (no false positives)

---

#### 6.2 Monitoring System
**Files:** `IoxMonitor.hpp/.cpp`

```cpp
// IoxMonitor.hpp - Statistics and telemetry
namespace iox {

struct IoxStats {
    // Allocation stats
    uint64_t total_allocs;
    uint64_t total_frees;
    uint64_t active_allocs;
    size_t total_allocated_bytes;
    
    // Performance
    uint64_t tcmalloc_hits;
    uint64_t burst_allocs;
    uint64_t lease_reclaims;
    
    // Security
    uint64_t canary_failures;
    uint64_t quarantine_size;
};

class IoxMonitor {
public:
    static IoxMonitor& instance();
    
    // Update stats (lock-free)
    void recordAlloc(size_t size, bool is_burst);
    void recordFree(IoxHandle h);
    void recordCanaryFailure();
    
    // Query stats
    IoxStats getSnapshot() const;
    
    // Prometheus export
    std::string exportPrometheus() const;
};

} // namespace iox
```

**Tasks:**
- [ ] Implement lock-free counters (atomic)
- [ ] Add Prometheus metric export
- [ ] Implement periodic snapshot
- [ ] Add JSON stats endpoint

**Success Criteria:**
- Stats update: <5ns overhead
- Prometheus export: <1ms

---

### Phase 7: Public API (Week 9-10)

#### 7.1 C++ Facade
**Files:** `IoxMemory.hpp`

```cpp
// IoxMemory.hpp - User-facing API
namespace iox {

// Core allocation API
inline IoxHandle iox_alloc(size_t size, uint32_t lease_ms = 0) {
    return IoxAllocatorCore::instance().alloc(size, lease_ms);
}

inline void iox_free(IoxHandle handle) {
    IoxAllocatorCore::instance().free(handle);
}

// Convenience helpers
inline void* iox_handle_to_ptr(IoxHandle h) {
    return IoxAllocatorCore::instance().handleToPtr(h);
}

inline IoxHandle iox_ptr_to_handle(void* ptr) {
    return IoxAllocatorCore::instance().ptrToHandle(ptr);
}

// STL allocator adapter
template <typename T>
class IoxAllocator {
public:
    using value_type = T;
    
    T* allocate(size_t n) {
        IoxHandle h = iox_alloc(n * sizeof(T));
        return static_cast<T*>(iox_handle_to_ptr(h));
    }
    
    void deallocate(T* ptr, size_t) {
        iox_free(iox_ptr_to_handle(ptr));
    }
};

// Global operator overrides (optional)
#ifdef IOX_OVERRIDE_GLOBAL_NEW
void* operator new(size_t size);
void operator delete(void* ptr) noexcept;
#endif

} // namespace iox
```

**Tasks:**
- [ ] Implement global new/delete overrides
- [ ] Create STL allocator adapter
- [ ] Add convenience macros
- [ ] Write API documentation

---

## 🧪 Testing Strategy

### Unit Tests (Per Phase)

```cpp
// test/unit/test_iox_handle.cpp
TEST(IoxHandle, EncodeDecode) {
    void* ptr = reinterpret_cast<void*>(0x12345678);
    IoxHandle h = iox::ptrToHandle(ptr, 1);
    void* decoded = iox::handleToPtr(h, nullptr);
    EXPECT_EQ(ptr, decoded);
}

// test/unit/test_iox_allocator.cpp
TEST(IoxAllocator, SmallAlloc) {
    IoxHandle h = iox_alloc(128);
    EXPECT_NE(h.value, IOX_HANDLE_NULL.value);
    void* ptr = iox_handle_to_ptr(h);
    EXPECT_NE(ptr, nullptr);
    iox_free(h);
}
```

### Integration Tests

```cpp
// test/integration/test_multithreaded.cpp
TEST(IoxAllocator, MultiThreadStress) {
    constexpr int NUM_THREADS = 32;
    constexpr int ITERATIONS = 10000;
    
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < ITERATIONS; ++j) {
                IoxHandle h = iox_alloc(rand() % 1024);
                iox_free(h);
            }
        });
    }
    for (auto& t : threads) t.join();
    
    IoxStats stats = iox::IoxMonitor::instance().getSnapshot();
    EXPECT_EQ(stats.active_allocs, 0);
}
```

### Performance Benchmarks

```cpp
// test/benchmark/bench_iox_alloc.cpp
void BM_IoxAlloc_Small(benchmark::State& state) {
    for (auto _ : state) {
        IoxHandle h = iox_alloc(128);
        benchmark::DoNotOptimize(h);
        iox_free(h);
    }
}
BENCHMARK(BM_IoxAlloc_Small);

// Target: <10ns for TCMalloc thread cache hits
```

---

## 📈 Success Metrics

| Metric | Target | Measurement |
|--------|--------|-------------|
| Small alloc latency (≤256KB) | <10ns | P50 via benchmark |
| Large alloc latency (>256KB) | <100ns | P50 via benchmark |
| Handle conversion | <1ns | Inline, confirmed via profiler |
| Fragmentation | <5% | Burst region analysis |
| Memory overhead (L1) | +8B/chunk | Static analysis |
| Security detection (L2) | 100% | Fuzz testing |
| Throughput (32 threads) | >10M ops/sec | Stress test |

---

## 🚀 Milestones

| Week | Milestone | Deliverable |
|------|-----------|-------------|
| 1-2 | Foundation | `IoxTypes`, `IoxRegion`, linker script |
| 2-3 | Handle System | `IoxHandle` with <1ns conversion |
| 3-5 | TCMalloc | ResidentBackend + TCMallocBackend |
| 5-6 | Burst Region | BurstAllocator with freelist |
| 6-7 | Core Allocator | IoxAllocatorCore routing logic |
| 7-9 | Security | L1/L2 tiers, monitoring |
| 9-10 | API | Public facade, STL adapter |
| 10 | Testing | Full test coverage + benchmarks |

---

## 🔧 Build Configuration

```cmake
# modules/Core/CMakeLists.txt

# IoxPool compilation flags
option(IOX_ENABLE_SECURITY_L2 "Enable L2 security (RedZone)" OFF)
option(IOX_ENABLE_LEASE "Enable lease management" OFF)
option(IOX_OVERRIDE_GLOBAL_NEW "Override global new/delete" ON)

# Architecture detection
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(IOX_ARCH_64BIT ON)
else()
    set(IOX_ARCH_64BIT OFF)
endif()

# TCMalloc dependency
find_package(TCMalloc REQUIRED)

# IoxPool library
add_library(iox_pool
    src/Memory/IoxRegion.cpp
    src/Memory/IoxAllocatorCore.cpp
    src/Memory/TCMallocBackend.cpp
    src/Memory/ResidentBackend.cpp
    src/Memory/BurstAllocator.cpp
    src/Memory/IoxMonitor.cpp
    src/Memory/IoxSecurity.cpp
    src/Memory/IoxMemory.cpp
)

target_link_libraries(iox_pool
    PRIVATE TCMalloc::tcmalloc_minimal
)

target_include_directories(iox_pool
    PUBLIC inc/Memory
)
```

---

## 📞 Contact & Resources

- **Design Document:** `MEMORY_MANAGEMENT_GUIDE.md`
- **Legacy Archive:** `source/inc/archive/LEGACY_MIGRATION.md`
- **Repository:** TreeNeeBee/LightAP
- **Issues:** GitHub Issues for questions/feedback

---

**Next Steps:** Begin Phase 1 implementation → Create `IoxTypes.hpp` and `IoxRegion.hpp`
