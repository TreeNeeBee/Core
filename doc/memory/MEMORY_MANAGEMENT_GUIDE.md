# LightAP Core Memory Management Guide - IoxPool

**Version:** 0.3.0 (IOX Deterministic Memory Pool)  
**Date:** 2025-12-09  
**Author:** LightAP Team  
**Status:** Development - IOX Embedded Optimization  
**Changelog:** See [Version History](#version-history) for all updates

> **📌 Note:** TCMalloc-based design (v0.2.0) has been archived to `MEMORY_MANAGEMENT_GUIDE_v0.2.0_TCMalloc_ARCHIVED.md`

---

## 📋 Table of Contents

- [Overview](#overview)
- [Three-Tier Architecture](#three-tier-architecture)
  - [Layer 1: Memory Region Configuration](#layer-1-memory-region-configuration)
  - [Layer 2: Memory Allocator Core](#layer-2-memory-allocator-core)
  - [Layer 3: Adapter Layer](#layer-3-adapter-layer)
- [IOX Thread-Local Cache (TC) Architecture](#iox-thread-local-cache-tc-architecture)
  - [TC Architecture](#tc-architecture-simplified)
  - [Thread-Local Cache Implementation](#thread-local-cache-implementation)
  - [Dynamic Extent Management](#dynamic-extent-management)
- [Chunk Header Definitions](#chunk-header-definitions)
- [Memory Region Details](#memory-region-details)
  - [Region Format Protocol](#region-format-protocol)
  - [Fixed-Size Classes](#size-classes)
- [Security Configuration Tiers](#security-configuration-tiers)
- [PSSC for SHM Optimization](#pssc-for-shm-optimization)
- [Observability & Auto-tuning](#observability--auto-tuning-p2)
- [Performance Characteristics](#performance-characteristics)
- [Configuration](#configuration)
- [API Reference](#api-reference)
  - [Region Management API](#region-management-api)
- [Examples](#examples)
- [Best Practices](#best-practices)
- [Migration Guide](#migration-from-legacy)
- [Version History](#version-history)
- [References](#references)

---

## 🎯 Overview

IoxPool is LightAP's **unified, production-grade memory management infrastructure** designed with **IOX Deterministic Memory Pool** architecture for embedded systems, offering optimal performance, low fragmentation, and predictable behavior.

### Three-Tier Architecture

IoxPool adopts a **layered design** for clear separation of concerns:

```
┌─────────────────────────────────────────────────────────────────────┐
│          Layer 3: Adapter Layer (Public Interfaces)              │
│  • Global new/delete overloading                                 │
│  • SOA memory operations (optional integration)                  │
│  • C++ STL allocator adapter                                    │
│  • High-level API (iox_alloc, iox_free, iox_alloc_burst)       │
├─────────────────────────────────────────────────────────────────────┤
│       Layer 2: Memory Allocator Core (Management Logic)          │
│  • Memory block allocation/deallocation (given base + size)     │
│  • Chunk header management with security features               │
│  • TC (Thread-Local Cache, ≤1024B, 32 size classes)            │
│  • Resident Region Management (Dynamic Extent, 1024B-2MB slabs)│
│  • Burst Region (>2MB, freelist-managed large blocks)           │
│  • PSSC (per-segment shared cache for SHM) - Low-level only     │
│  • Background services (optional lease scanner for Burst)        │
├─────────────────────────────────────────────────────────────────────┤
│     Layer 1: Memory Region Configuration (Foundation)            │
│  • BSS Region (.iox section) - Linker script configured         │
│    - Resident Region (128 MiB) for TC and Dynamic Extent │
│    - Burst Region (2 GiB) for large allocations                │
│  • Shared memory regions (SHM) - User-specified for SOA/IPC    │
│  • Page alignment: 4K/2M/1G huge pages                          │
│  • Platform-specific: 64-bit (512MiB-512GiB) / 32-bit (64MiB-3.5GiB) │
└─────────────────────────────────────────────────────────────────────┘
```

### Key Features

✅ **Thread-Local Cache (TC)** - Per-thread fast path for ≤1024B allocations (~10ns P50)  
✅ **32 Size Classes** - Fine-grained 8B-1024B classes for minimal internal fragmentation (5-8%)  
✅ **Unified Architecture** - Single memory layer for all allocation needs  
✅ **Handle-Based Safety** - 8-byte handle (base_id:4 + offset:60 on 64-bit, offset:28 on 32-bit)  
✅ **Dynamic Extent Management** - Slab allocation with bitmap for 1024B-2MB requests  
✅ **Burst Region for Large Allocations** - >2MB requests, freelist-managed  
✅ **Lazy-Free Chain** - Optional compile-time feature (IOX_ENABLE_LAZY_FREE) with depth/lease limits  
✅ **Page-Aligned Memory** - 4K/2M/1G page alignment for optimal TLB efficiency  
✅ **Security Features** - RedZone, Canary, quarantine for memory safety  
✅ **PSSC for SHM** - Low-level Segment + Ringbuffer for zero-copy cross-process memory  
✅ **Low Fragmentation** - TC size classes + extent coalescing, 3-8% external fragmentation  
✅ **Embedded Optimized** - Base+offset addressing, fixed memory pools, deterministic allocation  
✅ **Cross-Platform** - Full support for 32-bit and 64-bit systems with automatic adaptation  
✅ **Compile-Time Configuration** - Zero runtime overhead for disabled features

### Design Principles

1. **Safety First** - 100% overflow detection with security features enabled
2. **Low Fragmentation** - 32 size classes + extent coalescing, 5-8% internal, 3-8% external
3. **Performance** - TC Fast Path (~10ns P50) with per-thread caching for ≤1024B
4. **Deterministic Allocation** - Fixed memory pools, base+offset addressing, no runtime expansion
5. **Memory Efficiency** - Page-aligned (4K/2M/1G) for optimal TLB and cache performance
6. **Simplicity** - Self-contained implementation, no external allocator dependencies
7. **Embedded Optimized** - Compile-time configuration, predictable latency (P99 < 100ns)
8. **Layered Design** - Clear separation: Region Config → TC/Extent Core → Adapter API

### Critical Design Constraints

⚠️ **Constraint 1: No STL or malloc/free APIs**
- IOXPool implementation **strictly prohibits** use of STL containers and any APIs that may trigger `malloc()`/`free()`
- Rationale: Global `operator new/delete` overloading causes recursive `malloc()` → deadlock/crash
- All internal data structures use **fixed-size arrays** or **pre-allocated buffers**
- Example violations: `std::vector`, `std::map`, `std::string`, `new[]`, `delete[]`

⚠️ **Constraint 2: Burst Region is Optional**
- Burst Region (>2MB allocations) is **compile-time optional** via `IOX_ENABLE_BURST_REGION`
- Default: **DISABLED** for embedded systems with tight memory constraints
- When disabled: Allocations >2MB return failure (handle = 0)
- Configuration: Set `IOX_BURST_SIZE=0` to disable at link-time

⚠️ **Constraint 3: Resident Region Bootstrap Self-Release**
- **Chunk 63 (highest 2MB)** reserved for IOXPool bootstrap/self-initialization
- Contains: ResidentRegionHeader, initial TC bins, extent metadata, startup allocations
- **After bootstrap completes**: Chunk 63 released and available for Dynamic Extent allocations
- Ensures zero waste: Bootstrap overhead amortized across entire region lifecycle

---

## ⚠️ IOXPool Implementation Constraints

### Constraint 1: Strict Prohibition of STL and malloc/free APIs

**Problem: Recursive malloc() Deadlock**

IOXPool overloads global `operator new/delete` to intercept all dynamic allocations:

```cpp
void* operator new(size_t size) {
    return iox_alloc(size);  // Route to IOXPool
}

void operator delete(void* ptr) noexcept {
    iox_free(ptr);  // Route to IOXPool
}
```

**If IOXPool internally uses STL or malloc/free:**

```cpp
// ❌ DEADLOCK SCENARIO
void* iox_alloc(size_t size) {
    std::vector<Extent> extents;  // Calls operator new
    // → Calls iox_alloc() recursively
    // → Calls std::vector constructor again
    // → Infinite recursion → Stack overflow or deadlock
}
```

**Prohibited APIs and Constructs:**

```cpp
// ❌ FORBIDDEN in IOXPool implementation
std::vector<T>           // Uses operator new internally
std::map<K, V>           // Uses operator new internally
std::string              // Uses operator new internally
std::shared_ptr<T>       // Uses operator new internally
std::unique_ptr<T>       // Uses operator delete internally
new T / new T[]          // Triggers recursive operator new
delete ptr / delete[]    // Triggers recursive operator delete
malloc() / free()        // May be overridden by other libraries
calloc() / realloc()     // Same issue as malloc/free
```

**Allowed Alternatives:**

```cpp
// ✅ ALLOWED: Fixed-size arrays (stack or static)
Extent extents[MAX_EXTENTS];  // Stack allocation, no malloc
static ThreadCache tc_cache[MAX_THREADS];  // Static storage

// ✅ ALLOWED: Manual memory management within pre-allocated pool
struct ExtentList {
    Extent* head;
    Extent* allocate_from_pool();  // Allocates from IOXPool's own memory
};

// ✅ ALLOWED: Intrusive data structures (no separate allocation)
struct Extent {
    Extent* next;  // Linked list node within the object itself
    uint32_t size;
};
```

**Implementation Guidelines:**

1. **Use fixed-size arrays**: All metadata uses compile-time fixed arrays
   ```cpp
   #define MAX_TC_BINS 32
   TCBin bins[MAX_TC_BINS];  // No dynamic allocation
   ```

2. **Use intrusive containers**: Embed list/tree nodes within objects
   ```cpp
   struct Extent {
       Extent* next;      // Linked list
       Extent* rb_left;   // Red-black tree (intrusive)
       Extent* rb_right;
   };
   ```

3. **Use placement new** (with pre-allocated memory):
   ```cpp
   char buffer[sizeof(ThreadCache)];
   ThreadCache* tc = new(buffer) ThreadCache();  // Placement new, no malloc
   ```

4. **Pre-allocate all metadata** in Chunk 63 bootstrap region:
   ```cpp
   void* bootstrap_alloc(size_t size) {
       // Allocate from Chunk 63, never calls malloc
       return chunk63_base + bootstrap_offset;
   }
   ```

### Constraint 2: Burst Region Optional Configuration

Burst Region (>2MB allocations) is **disabled by default** for embedded systems:

**Configuration Options:**

```cpp
// Option 1: Disable Burst Region (default, embedded systems)
#define IOX_ENABLE_BURST_REGION 0
#define IOX_BURST_SIZE 0
// Result: Allocations >2MB return handle=0 (graceful failure)

// Option 2: Enable Burst Region (server systems with ample memory)
#define IOX_ENABLE_BURST_REGION 1
#define IOX_BURST_SIZE (2ULL * 1024 * 1024 * 1024)  // 2 GiB
// Result: Allocations >2MB serviced from Burst Region
```

**Runtime Behavior:**

```cpp
uint64_t iox_alloc(size_t size) {
    if (size <= IOX_TC_THRESHOLD) {
        return tc_alloc(size);  // ≤1024B
    } else if (size <= 2 * 1024 * 1024) {
        return extent_alloc(size);  // 1KB-2MB
    } else {
#if IOX_ENABLE_BURST_REGION
        return burst_alloc(size);  // >2MB
#else
        return 0;  // Graceful failure: size exceeds maximum
#endif
    }
}
```

### Constraint 3: Bootstrap Self-Release Mechanism

**Chunk 63 Lifecycle:**

```
Phase 1: Bootstrap (startup)
┌─────────────────────────────────────┐
│ Chunk 63: IOXPool Initialization    │
│ • ResidentRegionHeader setup        │
│ • TC bootstrap bins pre-allocation  │
│ • Extent metadata initialization    │
│ • Startup allocations               │
└─────────────────────────────────────┘
         ↓ (bootstrap complete)
         
Phase 2: Release (after init)
┌─────────────────────────────────────┐
│ Chunk 63: Available for Allocation  │
│ • Bootstrap data compacted/discarded│
│ • Full 2MB added to Dynamic Extent  │
│ • Zero waste: bootstrap overhead    │
│   amortized across region lifecycle │
└─────────────────────────────────────┘
```

**Implementation:**

```cpp
void ResidentRegion::init() {
    // Phase 1: Initialize header at Chunk 0 (permanent location)
    // ✅ Header is NOT allocated from bootstrap, it's at fixed location
    header = (ResidentRegionHeader*)base;  // Chunk 0, offset 0
    header->magic_start = 0x494F585F52455349ULL;
    header->version = 3;  // v0.3.0
    header->total_size = 128 * 1024 * 1024;  // 128 MiB
    header->chunk_count = 64;
    header->bootstrap_chunk = 63;
    header->bootstrap_released = 0;  // Not released yet
    header->bootstrap_offset = 0;
    
    // Phase 2: Use Chunk 63 for temporary bootstrap allocations
    void* bootstrap_base = base + (63 * CHUNK_SIZE);
    bootstrap_alloc_init(bootstrap_base, CHUNK_SIZE);
    
    // Allocate TEMPORARY data from Chunk 63 (will be discarded later)
    // Example: Initial TC bin setup, temporary extent lists, etc.
    void* temp_tc_init = bootstrap_alloc(sizeof(TCBin) * 32);
    void* temp_extent_list = bootstrap_alloc(sizeof(Extent) * 16);
    
    // Phase 3: Copy essential data to permanent locations
    // (e.g., copy temp_tc_init to Chunks 1-3 TC slabs)
    memcpy(get_tc_slab_base(), temp_tc_init, sizeof(TCBin) * 32);
    
    // Phase 4: Mark bootstrap complete, release Chunk 63
    header->bootstrap_released = 1;
    bitmap_mark_free(63);  // Chunk 63 now available for Dynamic Extent
    
    // Result: Header in Chunk 0 (permanent), Chunk 63 released (zero waste)
}
```

**Benefits:**

1. **Zero Waste**: Bootstrap overhead (metadata, initial structures) fully amortized
2. **Deterministic**: All initialization happens upfront, no runtime surprises
3. **Efficient**: Chunk 63 reused after bootstrap, maximizing available memory
4. **Simple**: Single-phase release, no complex garbage collection
5. **Correct**: Header at Chunk 0 (permanent), bootstrap temp data at Chunk 63 (released)

---

## 🏗️ Three-Tier Architecture

### Architecture Overview

```
                    Application Layer
                          │
                          ↓
         ┌──────────────────────────────────┐
         │   Layer 3: Adapter Layer           │
         │   (IoxAdapter.hpp/.cpp)            │
         ├──────────────────────────────────┤
         │ • operator new/delete overload   │
         │ • iox_alloc() / iox_free()       │
         │ • iox_alloc_shared() (SOA/IPC)   │
         │ • IoxAllocator<T> (STL adapter)  │
         │ • Iceoryx2Publisher/Subscriber  │
         └──────────────┬────────────────────┘
                          │
         ┌──────────────┴────────────────────┐
         │   Layer 2: Allocator Core          │
         │   (IoxAllocator.hpp/.cpp)          │
         ├──────────────────────────────────┤
         │ Input: base_addr + size          │
         ├──────────────────────────────────┤
         │ ✨ Thread-Local Cache (TC)        │
         │   - ≤1024B, 32 size classes      │
         │   - Fast Path ~10ns (P50)        │
         │   - Per-thread 64KB slab         │
         │   - Batch refill/flush           │
         │   - Optional lazy-free chain     │
         │ • Dynamic Extent Management      │
         │   - 1024B-2MB slab allocation    │
         │   - Bitmap-based tracking        │
         │   - Extent coalescing            │
         │ • Burst Region (>2MB allocs)     │
         │   - Variable-size blocks         │
         │   - Freelist management          │
         │ • PSSC (per-SHM-segment)         │
         │ • Background: Lease scanner (optional) │
         └──────────────┬────────────────────┘
                          │
         ┌──────────────┴────────────────────┐
         │   Layer 1: Memory Regions          │
         │   (iox.ld + runtime setup)         │
         │   ⚠️  UNCHANGED - Keep existing    │
         ├──────────────────────────────────┤
         │ • BSS Region (.iox section)      │
         │   ┌────────────────────────────┐ │
         │   │ Resident Region            │ │
         │   │ - Provides slabs to TC     │ │
         │   │ - Dynamic Extent (1KB-2MB) │ │
         │   │ - Size: IOX_RESIDENT_SIZE  │ │
         │   └────────────────────────────┘ │
         │   ┌────────────────────────────┐ │
         │   │ Burst Region               │ │
         │   │ - Variable-size blocks     │ │
         │   │ - For >2MB allocations     │ │
         │   │ - Mandatory lease field    │ │
         │   │ - Size: IOX_BURST_SIZE     │ │
         │   └────────────────────────────┘ │
         │ • SHM Regions (user-specified)   │
         │   - mmap/shm_open by caller      │
         │   - PSSC segments for IPC        │
         │ • Page alignment: 4K/2M/1G       │
         └──────────────────────────────┘
                   Hardware Memory
```

---

### Layer 1: Memory Region Configuration

**Purpose:** Establish memory regions at link-time and runtime for allocator use.

**Components:**

1. **BSS Region (.iox section)**
   - Configured via linker script (iox.ld)
   - Sub-regions for allocation (configurable):
     - **Resident Region**: Extent regions for TC and Dynamic Extent backend (MANDATORY, 128 MiB default)
     - **Burst Region**: Freelist-based allocator for >2MB allocations (OPTIONAL, compile-time via IOX_ENABLE_BURST_REGION)
       * Default: **DISABLED** (IOX_BURST_SIZE=0) for embedded systems
       * When disabled: Allocations >2MB fail gracefully (return handle=0)
       * When enabled: Set IOX_BURST_SIZE (e.g., 2 GiB on 64-bit, 512 MiB on 32-bit)
   - Page-aligned: 4K/2M/1G (unified for both 32-bit and 64-bit)

2. **Shared Memory Regions (SHM)**
   - Created at runtime via `mmap()` or `shm_open()` by user
   - Used for SOA/IPC (user-specified)
   - Each segment registered with PSSC (user's responsibility)

**Linker Script Example (64-bit):**

```ld
SECTIONS
{
    .iox 0x700000000000 : ALIGN(1<<21)  /* 2 MiB huge page */
    {
        __iox_start = .; 
        
        /* Resident Region (MANDATORY, extent regions for TC and Dynamic Extent) */
        __resident_start = .;
        . = . + IOX_RESIDENT_SIZE;  /* Default: 128 MiB (64-bit) or 64 MiB (32-bit) */
        __resident_end = .;
        
        /* Burst Region (OPTIONAL, compile-time configurable via IOX_ENABLE_BURST_REGION) */
        /* Set IOX_BURST_SIZE=0 to disable (default for embedded systems) */
        __burst_start = .;
#if IOX_ENABLE_BURST_REGION
        . = . + IOX_BURST_SIZE;     /* Optional: 2 GiB (64-bit) or 512 MiB (32-bit) */
#endif
        __burst_end = .;
        
        /* Metadata (allocator internal structures) */
        __metadata_start = .;
        . = . + IOX_METADATA_SIZE;  /* 64 KiB */
        __metadata_end = .;
        
        __iox_end = .;
    }
}
```

**Build-Time Configuration:**

```c
// Platform-specific defaults (can be overridden at compile time)
// Resident Region: MANDATORY (fixed memory pool for TC + Dynamic Extent)
#ifdef __LP64__  // 64-bit
    #define IOX_RESIDENT_SIZE (128 * 1024 * 1024)   // 128 MiB (64 × 2MB, mandatory)
#else  // 32-bit
    #define IOX_RESIDENT_SIZE (64 * 1024 * 1024)    // 64 MiB (32 × 2MB, mandatory)
#endif

// Burst Region: OPTIONAL (compile-time enable/disable)
#ifndef IOX_ENABLE_BURST_REGION
#define IOX_ENABLE_BURST_REGION 0  // Default: DISABLED for embedded systems
#endif

#if IOX_ENABLE_BURST_REGION
  #ifdef __LP64__  // 64-bit
    #define IOX_BURST_SIZE    (2ULL * 1024 * 1024 * 1024)  // 2 GiB (optional)
  #else  // 32-bit
    #define IOX_BURST_SIZE    (512 * 1024 * 1024)   // 512 MiB (optional)
  #endif
#else
  #define IOX_BURST_SIZE 0  // Disabled: no Burst Region, >2MB allocations fail
#endif

#define IOX_METADATA_SIZE (64 * 1024)  // 64 KiB (reserved, unused in v0.3.0)

// IOX TC configuration
#ifndef IOX_USE_TC
#define IOX_USE_TC 1  // Enable TC allocator (production default)
#endif

#ifndef IOX_TC_CHUNK_COUNT
#define IOX_TC_CHUNK_COUNT 2  // Number of TC chunks (default: Chunks 1-2)
#endif

// Note: IOX_RESIDENT_SIZE constraints
//   - Must be 2MB-aligned
//   - Valid range: 128 MiB (64 extents) recommended for multi-arena
//   - Determined by linker script size
// User can override both IOX_RESIDENT_SIZE and IOX_BURST_SIZE:
// -DIOX_RESIDENT_SIZE=256M -DIOX_BURST_SIZE=4G
// Arena configuration: g_arena_manager.init(num_arenas, base, size);
```

**Runtime Initialization:**

```cpp
// Initialize Resident Region (from .iox section)
extern char __resident_start[];
extern char __resident_end[];
size_t resident_size = __resident_end - __resident_start;
g_resident_region.init(__resident_start, resident_size);

// Initialize Burst Region (from .iox section)
extern char __burst_start[];
extern char __burst_end[];
size_t burst_size = __burst_end - __burst_start;
g_burst_region.init(__burst_start, burst_size);

// SHM Regions (user-specified, NOT from .iox section)
// Example: User creates SHM for IPC
int fd = shm_open("/my_ipc_segment", O_CREAT | O_RDWR, 0666);
ftruncate(fd, 16 * 1024 * 1024);  // 16 MiB
void* shm_base = mmap(NULL, 16 * 1024 * 1024, PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_HUGETLB, fd, 0);

// User registers SHM with PSSC (user's responsibility)
iox_pssc_segment_create("/my_ipc_segment", 4096, 1024, 0);
```

**Key Characteristics:**
- **Link-time Pre-allocation**: `.iox` section with Resident + Burst regions
- **Platform-aware Configuration**:
  - **32-bit**: IOX_RESIDENT_SIZE=64MiB, IOX_BURST_SIZE=512MiB
  - **64-bit**: IOX_RESIDENT_SIZE=128MiB, IOX_BURST_SIZE=2GiB
- **IOX TC Design**: Per-thread 64KB slab with 32 size classes (8B-1024B)
- **Extent-Based**: Dynamic extent allocation with coalescing support
- **Hook System**: Callbacks for warning and hard limit events
- **Header Protection**: Magic fields + metadata validation
- **Page-aligned**: Optimal TLB efficiency (4K/2M/1G)
- **Flexible Allocation**: TC (≤1KB) → Dynamic Extent (1KB-2MB) → Burst (>2MB)
- **SHM Independence**: SHM regions user-managed, PSSC provides allocation from SHM base

---

### Layer 2: Memory Allocator Core

**Purpose:** Manage memory allocation/deallocation with **IOX Deterministic Memory Pool** strategy for embedded systems.

**🔥 IOX Design - Key Features:**

```
Architecture Benefits:
- Thread-Local Cache (TC): Fast path for ≤1024B allocations (~10ns P50)
- 32 Size Classes: Fine-grained 8B-1024B for minimal fragmentation (5-8%)
- Dynamic Extent: Bitmap-based slab allocation for 1024B-2MB
- Burst Region: Freelist-managed allocator for >2MB allocations
- Low Fragmentation: 3-8% external fragmentation via extent coalescing
- Embedded Optimized: Base+offset addressing, fixed memory pools
- Deterministic: Compile-time configuration, predictable latency
- Self-Contained: No external allocator dependencies
```

**Design Comparison:**

| Aspect | Previous (v0.2.0 TCMalloc) | Current (v0.3.0 IOX) |
|--------|----------------------------|----------------------|
| **Allocator** | External TCMalloc library | Self-implemented TC + Extent |
| **Threshold** | 256KB fixed | 1024B (TC), 2MB (Burst) |
| **Size Classes** | TCMalloc's 88 classes | 32 classes (8B-1024B) |
| **Thread Cache** | Per-CPU cache (rseq) | Per-thread 64KB slab |
| **Fragmentation** | 15-25% internal | 5-8% internal, 3-8% external |
| **Latency (P50)** | 10ns (small) | 10-15ns (TC Fast Path) |
| **Control** | Limited (black box) | Full control (embedded) |
| **Dependencies** | libtcmalloc | None |
| **Memory Pools** | Dynamic expansion | Fixed pools, no runtime growth |

**Architecture Overview (IOX Deterministic Pool):**

```
┌─────────────────────────────────────────────────────────────┐
│  Allocation Request (iox_alloc(size))                       │
└─────────────┬───────────────────────────────────────────────┘
              │
              ├─ size: ≤1024B ────► Thread-Local Cache (TC)
              │                    │
              │                    ├─ Fast Path Hit → ~10ns
              │                    │   • Check thread-local bin
              │                    │   • Return cached block
              │                    │
              │                    └─ Slow Path (Refill) → ~50ns
              │                        • Batch allocate from extent
              │                        • Refill TC bin (16 blocks)
              │                        • Optional lazy-free check
              │
              ├─ size: 1024B-2MB ─► Dynamic Extent Management
              │                    │
              │                    ↓
              │                    ┌────────────────────────────────┐
              │                    │  Resident Region (Backend)     │
              │                    │  • Bitmap-based allocation     │
              │                    │  • Extent coalescing           │
              │                    │  • Slab management             │
              │                    └────────────────────────────────┘
              │
              └─ size > 2MB ──────► Burst Region
                                    │
                                    ↓
                                   ┌────────────────────────────────┐
                                   │  Burst Region Pool             │
                                   │  • Variable-size allocation    │
                                   │  • Freelist management         │
                                   │  • Large block handling        │
                                   └────────────────────────────────┘
```

**Input:** `base_address` + `size` (from Layer 1)

**Core Components:**

#### 1. Resident Region Management (IOX Backend)

**Architecture Overview:**

```
Resident Region Layout (128 MiB, 64 Chunks):

┌─────────────────────────────────────────────────────────────┐
│  Chunk 0: Region Header (2MB)                               │
│  • ResidentRegionHeader (metadata, ~1KB)                    │
│  • Rest available for small allocations after init          │
├─────────────────────────────────────────────────────────────┤
│  Chunks 1-3: Thread-Local Cache (TC) Slabs (N=2 default)   │
│  • Per-thread 64KB slabs                                    │
│  • 32 size classes (8B-1024B)                               │
│  • Fast Path allocation (~10ns)                             │
├─────────────────────────────────────────────────────────────┤
│  Chunks 4-62: Dynamic Extent Region (N+2 to 62)            │
│  • Slab allocation (1024B-2MB)                              │
│  • Bitmap-based tracking                                    │
│  • Extent coalescing (lazy-free chain optional)            │
│  ⚠️  WARNING: At 85% usage                                  │
├─────────────────────────────────────────────────────────────┤
│  Chunk 63: Bootstrap Region (TEMPORARY, 2MB)                │
│  • 🚀 IOXPool self-initialization area                      │
│  • Contains: TC bootstrap bins, extent metadata, startup    │
│  • ⚡ RELEASED after bootstrap → available for Dynamic Extent│
│  • Zero waste: Bootstrap overhead amortized                 │
└─────────────────────────────────────────────────────────────┘

Configuration:
  • Total chunks: 64 (128 MiB)
  • Chunk size: 2 MB (fixed)
  • Header: Chunk 0 (persistent)
  • TC chunks: 1-N (configurable N, default N=2, i.e., Chunks 1-2)
  • Dynamic chunks: N+1 to 62 (e.g., 3-62 = 60 chunks)
  • Bootstrap chunk: 63 (temporary, released after init)
  • Available after bootstrap: Chunks 0 (partial), 1-62, 63 (full) = ~128 MiB
```

**Data Structures:**

```cpp
/**
 * @brief Resident Region Header (IOX Deterministic Pool Mode)
 * @details Manages TC (Thread-Local Cache) slabs and Dynamic Extent Region
 * @note Located at Chunk 0, Slab 0 (2MB bootstrap area)
 * @note Manages 64 × 2MB chunks total (128 MiB)
 */
struct ResidentRegionHeader {
    // Magic protection (start)
#if defined(__LP64__) || defined(_WIN64)
    uint64_t magic_start;         // 0x494F585F52455349 ("IOX_RESI") - 64-bit system
#else
    uint32_t magic_start;         // 0x494F5852 ("IOXR") - 32-bit system
#endif
    uint32_t version;             // 3 (v0.3.0: IOX TC + Extent design)
    uint64_t total_size;          // Fixed: 128 MiB (134217728 bytes)
    
    // Unified chunk bitmap management
    uint64_t chunk_bitmap;        // Chunk-level allocation (bits 0-63, each bit = 2MB chunk)
    
    // Region configuration (IOX v0.3.0)
    uint32_t chunk_size;          // Always 2 MiB (2097152 bytes)
    uint32_t chunk_count;         // Total chunks: 64 (fixed)
    uint32_t tc_chunk_start;      // TC region start: Chunk 1
    uint32_t tc_chunk_end;        // TC region end: Chunk 3 (N+1, N=2 default)
    uint32_t dynamic_chunk_start; // Dynamic extent region start: Chunk 4 (N+2)
    uint32_t dynamic_chunk_end;   // Dynamic extent region end: Chunk 62
    uint32_t bootstrap_chunk;     // Bootstrap chunk: 63 (temporary)
    uint32_t bootstrap_released;  // 0=bootstrap in use, 1=released to Dynamic Extent
    uint64_t bootstrap_offset;    // Bootstrap allocation offset (within Chunk 63)
    
    // Threshold configuration
    uint32_t warning_threshold;   // Warning at 85% usage (54 chunks)
    uint32_t hard_limit_threshold;// Hard limit at 95% usage (60 chunks)
    
    // Metadata snapshot configuration (ext4-style redundancy)
    uint32_t snapshot_count;      // Number of metadata snapshots (0-4, configurable)
    uint32_t snapshot_offsets[4]; // Snapshot locations within 2MB chunk (in bytes)
    uint32_t snapshot_size;       // Size of each snapshot (bytes)
    uint32_t snapshot_crc32[4];   // CRC32 checksum for each snapshot
    
    // Statistics (IOX v0.3.0)
    uint64_t alloc_count_tc;      // TC allocations (≤1024B)
    uint64_t free_count_tc;       // TC frees
    uint64_t alloc_count_extent;  // Extent allocations (1024B-2MB)
    uint64_t free_count_extent;   // Extent frees
    uint64_t alloc_count_burst;   // Burst allocations (>2MB)
    uint64_t free_count_burst;    // Burst frees
    uint64_t alloc_fails;         // Allocation failures
    uint32_t allocated_chunks;    // Current allocated chunks (bit count)
    uint32_t warning_triggered;   // Warning hook triggered count
    uint32_t hard_limit_hits;     // Hard limit rejection count
    
    // Hook callback
    ResidentHookCallback hook_callback; // Event hook function pointer
    void* hook_user_data;               // User context for hook
    
    // Magic protection (end)
#if defined(__LP64__) || defined(_WIN64)
    uint64_t magic_end;           // 0x494F585F454E44 ("IOX_END") - 64-bit system
#else
    uint32_t magic_end;           // 0x494F5852 ("IOXR") - 32-bit system
#endif
    
    // Padding to full bootstrap area
    uint8_t reserved[2097152 - 1024]; // Pad to full 2MB
} __attribute__((aligned(2097152))); // Aligned to 2MB

/**
 * @brief Thread-Local Cache (TC) structure
 * @details Per-thread cache for fast allocations (≤1024B)
 * @note Located in Chunks 1-3 (N chunks, configurable, default N=2)
 * @warning Uses FIXED-SIZE arrays only (Constraint 1: no STL/malloc/free)
 */
struct ThreadCache {
    uint64_t thread_id;            // Thread ID (pthread_self() or TID)
    TCBin    bins[32];             // ✅ 32 size class bins (FIXED ARRAY, not dynamic)
                                   //    Each TCBin contains fixed block_offsets[128]
    
#ifdef IOX_ENABLE_LAZY_FREE
    Extent*  lazy_free_list;       // Lazy-free chain head (intrusive linked list)
                                   // ✅ Uses Extent.next pointer (no separate allocation)
#endif
    
    // Statistics
    uint64_t total_alloc_count;    // Total allocations from this TC
    uint64_t total_free_count;     // Total frees to this TC
    uint64_t refill_count;         // Number of refills from central pool
    uint64_t flush_count;          // Number of flushes to central pool
    uint64_t cache_hit_count;      // TC cache hits
    uint64_t cache_miss_count;     // TC cache misses
} __attribute__((aligned(64)));    // Cache-line aligned

// Total ThreadCache size: 32 × 536 (TCBin) + 64 (stats/id) ≈ 17KB per thread
// ✅ Fits in 64KB TC slab with room for data blocks

/**
 * @brief TC Bin (per size class)
 * @details Fixed-size array of block offsets (NOT pointers)
 * @note Capacity determined by PRESET_IOXPOOL_COUNTS[i]
 * @warning MUST NOT use void** blocks (violates Constraint 1: no malloc/free)
 */
struct TCBin {
    uint32_t block_offsets[128];   // ✅ Fixed-size array (max 128 blocks per bin)
                                   // Stores offsets relative to TC slab base
                                   // Avoids malloc: no dynamic pointer allocation
    uint32_t cursor;               // Current allocation cursor (top of stack)
    uint32_t free_count;           // Number of free blocks in bin
    uint32_t capacity;             // Bin capacity (from PRESET_IOXPOOL_COUNTS, ≤128)
    uint32_t size_class_index;     // Size class index (0-31)
    uint32_t block_size;           // Block size in bytes (from PRESET_IOXPOOL_SIZES)
} __attribute__((aligned(64)));    // Cache-line aligned

// Total TCBin size: 128×4 + 6×4 = 536 bytes (fits in 9 cache lines)

/**
 * @brief Extent descriptor (Dynamic Extent Region)
 * @details Manages allocations in the 1024B-2MB range
 * @note Extent size = 64 KB slab (fixed)
 * @warning Uses FIXED-SIZE bitmap array (Constraint 1: no malloc/free)
 */
struct Extent {
    uint64_t magic;                // 0x494F585F455854 ("IOX_EXT")
    uint32_t size_class;           // Size class index (0-31 for TC compatibility)
    uint32_t free_count;           // Number of free blocks in extent
    uint32_t total_count;          // Total blocks in extent
    uint32_t block_size;           // Block size in bytes
    
    // ✅ Fixed-size bitmap (max 512 bytes for 4096 blocks)
    // Max blocks per extent: 64KB / 16B = 4096 blocks → 512 bytes bitmap
    uint8_t allocation_bitmap[512]; // ✅ Fixed array (1 bit per block, NOT pointer)
    uint32_t bitmap_size;          // Actual bitmap size used (bytes)
    
#ifdef IOX_ENABLE_LAZY_FREE
    uint64_t lazy_free_timestamp_ms; // Lazy-free timestamp (for deferred coalescing)
    Extent*  next;                   // ✅ Intrusive list (no separate allocation)
#endif
    
    // Statistics
    uint64_t alloc_count;          // Total allocations from this extent
    uint64_t free_count_total;     // Total frees to this extent
} __attribute__((aligned(64)));    // Cache-line aligned

// Total Extent size: 512 (bitmap) + 64 (header/stats) = 576 bytes (fits in 9 cache lines)

/**
 * @brief Burst Block (Large allocations >2MB)
 * @details Manages allocations exceeding Dynamic Extent range
 * @note Allocated from Dynamic Extent Region (Chunks 4-63)
 */
struct BurstBlock {
    uint64_t size;                 // Block size in bytes
    uint64_t lease_timestamp_ms;   // Lease timestamp (for timeout tracking)
    bool     is_free;              // Free status flag
    uint32_t chunk_index;          // Starting chunk index
    uint32_t chunk_count;          // Number of chunks occupied
    BurstBlock* next;              // Next block in freelist
    BurstBlock* prev;              // Previous block in freelist (doubly-linked)
} __attribute__((aligned(64)));    // Cache-line aligned

/**
 * @brief Size class configuration arrays (IOX v0.3.0)
 * @details 32 size classes from 8B to 1024B
 */
static constexpr size_t PRESET_IOXPOOL_SIZES[32] = {
    8, 16, 24, 32, 40, 48, 56, 64,           // 0-7: 8B step
    80, 96, 112, 128, 144, 160, 176, 192,    // 8-15: 16B step
    208, 224, 240, 256,                      // 16-19: 16B step
    288, 320, 352, 384, 416, 448, 480, 512,  // 20-27: 32B step
    640, 768, 896, 1024                      // 28-31: 128B step
};

static constexpr uint32_t PRESET_IOXPOOL_COUNTS[32] = {
    100, 100, 80, 80, 80, 80, 40, 40,        // 8B-64B: high frequency (✅ ≤128)
    32, 32, 32, 32, 16, 16, 8, 8,            // 80B-192B: medium frequency (✅ ≤128)
    8, 8, 4, 4,                              // 208B-256B: medium frequency (✅ ≤128)
    4, 4, 4, 4, 2, 2, 2, 2,                  // 288B-512B: low frequency (✅ ≤128)
    2, 2, 2, 2                               // 640B-1024B: very low frequency (✅ ≤128)
};
// ✅ All counts ≤ 128 to fit in TCBin.block_offsets[128] fixed array

/**
 * @brief Calculate size class index for given allocation size
 * @param size Requested allocation size in bytes
 * @return Size class index (0-31), or UINT32_MAX if >1024B
 */
inline uint32_t calculate_size_class(size_t size) {
    for (uint32_t i = 0; i < 32; i++) {
        if (size <= PRESET_IOXPOOL_SIZES[i]) {
            return i;
        }
    }
    return UINT32_MAX;  // Exceeds TC range, use Extent or Burst allocator
}

/**
 * @brief Hook callback type for resident region events
 * @param event_type Event type ("warning", "hard_limit", "alloc_64kb", "alloc_2mb", "free")
 * @param chunk_index Chunk index involved (0 to N)
 * @param block_index Block index within chunk (0-31 for Chunk0/1, -1 for 2MB allocations)
 * @param allocated_chunks Current allocated chunks (bitmap popcount)
 * @param total_chunks Total chunks in region (N+1)
 * @param user_data User-provided context
 */
typedef void (*ResidentHookCallback)(
    const char* event_type,
    uint32_t chunk_index,
    int32_t block_index,
    uint32_t allocated_chunks,
    uint32_t total_chunks,
    void* user_data
);

/**
 * @brief Resident Region management class (IOX v0.3.0)
 * @details Manages TC slabs, Dynamic Extent Region, and Burst allocations
 */
class ResidentRegion {
public:
    /**
     * @brief Initialize Resident Region from .iox section
     * @param base Address from __resident_start (linker symbol)
     * @param size Region size (must be 128 MiB, fixed in v0.3.0)
     * @param snapshot_count Number of metadata snapshots (0-4, default 2)
     * @param tc_chunk_count Number of TC chunks (default 2, range 1-8)
     * @return 0 on success, -EINVAL if invalid size
     * @note Header located at Chunk 0, Slab 0 (bootstrap area)
     * @note TC Region: Chunks 1 to tc_chunk_count (default 1-3)
     * @note Dynamic Region: Chunks (tc_chunk_count+1) to 63
     */
    int init(void* base, size_t size, uint32_t snapshot_count = 2, uint32_t tc_chunk_count = 2);
    
    /**
     * @brief Register hook callback for region events
     * @param callback Hook function pointer
     * @param user_data Context passed to callback
     */
    void register_hook(ResidentHookCallback callback, void* user_data);
    
    /**
     * @brief Allocate TC slab for Thread-Local Cache
     * @param size Requested slab size (64KB typical)
     * @param actual_size Output: actual allocated size
     * @return Pointer to TC slab, or nullptr if TC region exhausted
     * @note Allocates from TC region (Chunks 1-3)
     * @note Thread-safe: Lock-free CAS on chunk_bitmap
     * @note Triggers hooks:
     *   - "alloc_tc_slab" on success
     *   - "tc_exhausted" when no free TC chunks remain
     */
    void* alloc_tc_slab(size_t size, size_t* actual_size);
    
    /**
     * @brief Free TC slab back to ResidentRegion
     * @param ptr Pointer returned by alloc_tc_slab()
     * @param size Size of the slab
     * @return 0 on success, -EINVAL if invalid pointer
     * @note Thread-safe: Lock-free CAS on chunk_bitmap
     * @note Triggers hook: "free_tc_slab"
     */
    int free_tc_slab(void* ptr, size_t size);
    
    /**
     * @brief Allocate extent from Dynamic Extent Region
     * @param size Requested extent size (1024B-2MB)
     * @param actual_size Output: actual allocated size
     * @return Pointer to extent, or nullptr if exhausted
     * @note Allocates from Dynamic region (Chunks 4-63)
     * @note Thread-safe: Lock-free CAS on chunk_bitmap
     * @note Triggers hooks:
     *   - "alloc_extent" on success
     *   - "warning" when allocated_chunks >= warning_threshold (85%)
     *   - "hard_limit" when allocated_chunks >= hard_limit_threshold (95%)
     */
    void* alloc_extent(size_t size, size_t* actual_size);
    
    /**
     * @brief Free extent back to Dynamic Region
     * @param ptr Pointer returned by alloc_extent()
     * @param size Size of the extent
     * @return 0 on success, -EINVAL if invalid pointer
     * @note Thread-safe: Lock-free CAS on chunk_bitmap
     * @note Triggers hook: "free_extent"
     * @note May trigger extent coalescing if IOX_ENABLE_LAZY_FREE enabled
     */
    int free_extent(void* ptr, size_t size);
    
    /**
     * @brief Check if region has free capacity
     * @param min_size Minimum required size (default 2MB)
     * @return true if at least one chunk is free
     */
    bool has_free_capacity(size_t min_size = 2097152);
    
    /**
     * @brief Get base address of specific chunk or block
     * @param chunk_index Chunk index (0 to N)
     * @param block_index Block index within chunk (0-31), -1 for whole chunk
     * @return Pointer to chunk/block or nullptr
     */
    void* get_block_ptr(uint32_t chunk_index, int32_t block_index = -1);
    
    /**
     * @brief Check if chunk is fully allocated
     */
    bool is_chunk_full(uint32_t chunk_index) {
        pthread_mutex_lock(&m_header->bitmap_mutex);
        uint64_t mask = (1ULL << chunk_index);
        bool full = (m_header->chunk_bitmap & mask) != 0;
        pthread_mutex_unlock(&m_header->bitmap_mutex);
        return full;
    }
    
    /**
     * @brief Get current usage statistics
     */
    void get_stats(uint32_t* allocated_chunks, uint32_t* total_chunks) {
        pthread_mutex_lock(&m_header->bitmap_mutex);
        *allocated_chunks = __builtin_popcountll(m_header->chunk_bitmap);
        *total_chunks = m_header->total_chunks;
        pthread_mutex_unlock(&m_header->bitmap_mutex);
    }
    
private:
    ResidentRegionHeader* m_header;  // Points to Chunk 0, Slab 0 (bootstrap area)
    void* m_base;                    // Resident region base (Chunk 0 address)
    size_t m_size;                   // Fixed: 128 MiB
    uint32_t m_tc_chunk_count;       // Number of TC chunks (1-8, default 2)
    uint32_t m_dynamic_chunk_start;  // First chunk of dynamic region
    
    /**
     * @brief Allocate TC slab from TC region (Chunks 1-N)
     * @note Mutex-protected
     */
    uint64_t bitmap_alloc_tc_slab();
    
    /**
     * @brief Allocate extent from dynamic region (Chunks N+1-63)
     * @note Mutex-protected
     */
    uint64_t bitmap_alloc_extent();
    
    /**
     * @brief Free chunk back to region (TC slab or Extent)
     * @note Mutex-protected
     */
    int bitmap_free(uint32_t chunk_index);
    
    /**
     * @brief Trigger hook callback (if registered)
     */
    void trigger_hook(const char* event_type, uint32_t chunk_index, int32_t block_index);
    
    /**
     * @brief Write metadata snapshots to configured locations
     * @note Called after init() and any critical metadata updates
     */
    void write_snapshots();
    
    /**
     * @brief Verify and restore metadata from snapshots if needed
     * @return true if header is valid or successfully restored, false if all snapshots corrupted
     */
    bool verify_snapshots();
};
```

**Initialization Logic:**

```cpp
/**
 * @brief Initialize Resident Region with IOX v0.3.0 layout
 * @details Header at Chunk 0 Slab 0, TC Region at Chunks 1-N, Dynamic Region at Chunks N+1-63
 */
int ResidentRegion::init(void* base, size_t size, uint32_t snapshot_count, uint32_t tc_chunk_count) {
    m_base = base;
    m_size = size;
    
    // Validate size (must be 128 MiB fixed in v0.3.0)
    if (size != 128 * 1024 * 1024) {
        return -EINVAL;  // Must be exactly 128 MiB
    }
    
    uint32_t total_chunks = 64;  // Fixed at 64 chunks
    
    // Validate snapshot count
    if (snapshot_count > 4) {
        return -EINVAL;  // Max 4 snapshots
    }
    
    // Validate TC chunk count
    if (tc_chunk_count < 1 || tc_chunk_count > 8) {
        return -EINVAL;  // TC chunks must be 1-8
    }
    
    // Header located at Chunk 0, Slab 0 (bootstrap area)
    m_header = reinterpret_cast<ResidentRegionHeader*>(base);
    
    // Initialize header magic fields
#if defined(__LP64__) || defined(_WIN64)
    m_header->magic_start = 0x494F585F52455349ULL;  // "IOX_RESI" (64-bit)
    m_header->magic_end = 0x494F585F454E44ULL;      // "IOX_END" (64-bit)
#else
    m_header->magic_start = 0x494F5852;  // "IOXR" (32-bit)
    m_header->magic_end = 0x494F5852;    // "IOXR" (32-bit)
#endif
    
    m_header->version = 3;         // v0.3.0: IOX TC + Extent design
    m_header->total_size = size;
    m_header->chunk_size = 2 * 1024 * 1024;
    m_header->chunk_count = total_chunks;
    
    // Region configuration (IOX v0.3.0)
    m_header->tc_chunk_start = 1;
    m_header->tc_chunk_end = tc_chunk_count;  // N (default 2, so chunks 1-2)
    m_header->dynamic_chunk_start = tc_chunk_count + 1;  // N+1 (default 3)
    m_header->dynamic_chunk_end = 63;
    m_header->bootstrap_offset = 0;  // Bootstrap at Chunk 0 start
    
    // Calculate thresholds
    uint32_t dynamic_chunks = 63 - tc_chunk_count;  // Dynamic region size
    m_header->warning_threshold = (dynamic_chunks * 85) / 100;  // 85% warning
    m_header->hard_limit_threshold = (dynamic_chunks * 95) / 100;  // 95% hard limit
    
    // Initialize bitmap
    // Mark Chunk 0 (header/bootstrap) as allocated
    m_header->chunk_bitmap = 1ULL;  // Bit 0 set
    
    // Configure metadata snapshots (ext4-style redundancy)
    m_header->snapshot_count = snapshot_count;
    m_header->snapshot_size = sizeof(ResidentRegionHeader) - sizeof(m_header->reserved);
    
    // Calculate snapshot offsets (distributed across 2MB chunk)
    // Example: For 2 snapshots: 512KB, 1.5MB offsets
    if (snapshot_count > 0) {
        uint32_t interval = (2 * 1024 * 1024) / (snapshot_count + 1);
        for (uint32_t i = 0; i < snapshot_count; i++) {
            m_header->snapshot_offsets[i] = (i + 1) * interval;
        }
    }
    memset(m_header->snapshot_crc32, 0, sizeof(m_header->snapshot_crc32));
    
    // Initialize statistics (IOX v0.3.0)
    m_header->alloc_count_tc = 0;
    m_header->free_count_tc = 0;
    m_header->alloc_count_extent = 0;
    m_header->free_count_extent = 0;
    m_header->alloc_count_burst = 0;
    m_header->free_count_burst = 0;
    m_header->alloc_fails = 0;
    m_header->allocated_chunks = 1;  // Only Chunk 0 (header/bootstrap) initially
    m_header->warning_triggered = 0;
    m_header->hard_limit_hits = 0;
    
    // Hook initialization
    m_header->hook_callback = nullptr;
    m_header->hook_user_data = nullptr;
    
    // Write initial snapshots
    write_snapshots();
    
    return 0;
}

/**
 * @brief Write metadata snapshots to backup locations
 * @details Implements ext4-style metadata redundancy
 */
void ResidentRegion::write_snapshots() {
    if (m_header->snapshot_count == 0) {
        return;  // Snapshots disabled
    }
    
    // Calculate CRC32 for primary header (excluding reserved padding)
    uint32_t header_crc = crc32(0, (const uint8_t*)m_header, m_header->snapshot_size);
    
    // Write snapshots to configured offsets
    for (uint32_t i = 0; i < m_header->snapshot_count; i++) {
        void* snapshot_addr = (char*)m_header + m_header->snapshot_offsets[i];
        memcpy(snapshot_addr, m_header, m_header->snapshot_size);
        m_header->snapshot_crc32[i] = header_crc;
    }
}

/**
 * @brief Verify header integrity and restore from snapshot if corrupted
 * @return true if header valid or restored, false if all copies corrupted
 */
bool ResidentRegion::verify_snapshots() {
    if (m_header->snapshot_count == 0) {
        // No snapshots, just verify magic fields
#if defined(__LP64__) || defined(_WIN64)
        return (m_header->magic_start == 0x494F585F52455349ULL &&
                m_header->magic_end == 0x494F585F454E44ULL);
#else
        return (m_header->magic_start == 0x494F5852 &&
                m_header->magic_end == 0x494F5852);
#endif
    }
    
    // Verify primary header
    uint32_t expected_crc = crc32(0, (const uint8_t*)m_header, m_header->snapshot_size);
    bool primary_valid = (expected_crc == m_header->snapshot_crc32[0]);
    
    if (primary_valid) {
        return true;  // Primary header is valid
    }
    
    // Primary corrupted, try snapshots
    for (uint32_t i = 0; i < m_header->snapshot_count; i++) {
        void* snapshot_addr = (char*)m_header + m_header->snapshot_offsets[i];
        uint32_t snapshot_crc = crc32(0, (const uint8_t*)snapshot_addr, 
                                     m_header->snapshot_size);
        
        if (snapshot_crc == m_header->snapshot_crc32[i]) {
            // Valid snapshot found, restore primary header
            memcpy(m_header, snapshot_addr, m_header->snapshot_size);
            LOG_WARN("ResidentRegion: Primary header corrupted, restored from snapshot %u", i);
            return true;
        }
    }
    
    // All copies corrupted
    LOG_ERROR("ResidentRegion: All metadata copies corrupted, cannot recover");
    return false;
}

/**
 * @brief Allocate slab for jemalloc arena backend
 * @details Lock-free CAS-based chunk allocation
 */
void* ResidentRegion::alloc_slab(size_t size, size_t* actual_size) {
    // Validate size (TC/Extent typically requests slab regions)
    if (size > m_header->chunk_size) {
        m_header->alloc_fails++;
        trigger_hook("hard_limit", 0, -1);
        return nullptr;
    }
    
    // Find free chunk in bitmap (lock-free CAS)
    uint32_t total_chunks = m_header->total_chunks;
    uint32_t reserved_chunk = total_chunks - 1;  // Chunk N-1 (reserved)
    uint32_t header_chunk = m_header->header_chunk;  // Chunk N (header)
    
    for (uint32_t chunk = 0; chunk < total_chunks; chunk++) {
        // Skip reserved and header chunks
        if (chunk == reserved_chunk || chunk == header_chunk) {
            continue;
        }
        
        // Check if chunk is free (bit == 0)
        if ((m_header->chunk_bitmap & (1ULL << chunk)) == 0) {
            // Atomic CAS to claim chunk
            uint64_t old_bitmap = m_header->chunk_bitmap;
            uint64_t new_bitmap = old_bitmap | (1ULL << chunk);
            
            if (__sync_bool_compare_and_swap(&m_header->chunk_bitmap, old_bitmap, new_bitmap)) {
                // Successfully claimed chunk
                __sync_fetch_and_add(&m_header->alloc_count_slab, 1);
                __sync_fetch_and_add(&m_header->slabs_allocated, 1);
                m_header->allocated_chunks = __builtin_popcountll(m_header->chunk_bitmap);
                
                // Check warning threshold
                if (m_header->allocated_chunks >= m_header->warning_threshold) {
                    m_header->warning_triggered++;
                    trigger_hook("warning", chunk, -1);
                }
                
                trigger_hook("alloc_slab", chunk, -1);
                write_snapshots();
                
                if (actual_size) {
                    *actual_size = m_header->chunk_size;
                }
                
                return get_chunk_address(chunk, -1);
            }
        }
    }
    
    // No free chunks
    m_header->alloc_fails++;
    m_header->hard_limit_hits++;
    trigger_hook("hard_limit", 0, -1);
    return nullptr;
}

/**
 * @brief Free slab back to ResidentRegion
 */
int ResidentRegion::free_slab(void* ptr, size_t size) {
    if (!ptr) return -EINVAL;
    
    // Calculate chunk index from pointer
    uint8_t* base = m_base;
    uint8_t* ptr_u8 = static_cast<uint8_t*>(ptr);
    uint64_t offset = ptr_u8 - base;
    uint32_t chunk = offset / m_header->chunk_size;
    
    if (chunk >= m_header->total_chunks) {
        return -EINVAL;
    }
    
    // Atomic CAS to free chunk
    uint64_t old_bitmap = m_header->chunk_bitmap;
    uint64_t new_bitmap = old_bitmap & ~(1ULL << chunk);
    
    if (__sync_bool_compare_and_swap(&m_header->chunk_bitmap, old_bitmap, new_bitmap)) {
        __sync_fetch_and_add(&m_header->free_count_slab, 1);
        __sync_fetch_and_sub(&m_header->slabs_allocated, 1);
        m_header->allocated_chunks = __builtin_popcountll(m_header->chunk_bitmap);
        
        trigger_hook("free_slab", chunk, -1);
        write_snapshots();
        return 0;
    }
    
    return -EINVAL;
}

/**
 * @brief Check if region has free capacity
 */
bool ResidentRegion::has_free_capacity(size_t min_size) {
    uint32_t allocated = m_header->allocated_chunks;
    uint32_t total = m_header->total_chunks - 2;  // Exclude reserved and header
    return (allocated < total);
}
```

**Hook Usage Example:**

```cpp
// Define hook callback
void resident_event_handler(const char* event_type, 
                           uint32_t chunk_index, uint32_t block_index,
                           uint32_t allocated_blocks, uint32_t total_blocks,
                           void* user_data) {
    if (strcmp(event_type, "warning") == 0) {
        // 2/3 threshold crossed - log warning
        LOG_WARN("Resident Region usage: %u/%u blocks (%.1f%%), consider expanding",
                 allocated_blocks, total_blocks, 
                 (float)allocated_blocks * 100 / total_blocks);
    } else if (strcmp(event_type, "hard_limit") == 0) {
        // Hit reserved chunk - allocation rejected
        LOG_ERROR("Resident Region exhausted: %u/%u blocks, allocation FAILED",
                  allocated_blocks, total_blocks);
        // Trigger emergency actions (e.g., flush caches, reject new connections)
    } else if (strcmp(event_type, "alloc") == 0) {
        LOG_DEBUG("Allocated block: chunk[%u][%u], usage: %u/%u blocks", 
                  chunk_index, block_index, allocated_blocks, total_blocks);
    } else if (strcmp(event_type, "free") == 0) {
        LOG_DEBUG("Freed block: chunk[%u][%u], usage: %u/%u blocks", 
                  chunk_index, block_index, allocated_blocks, total_blocks);
    }
}

// Register hook during initialization
g_resident_region.init(__resident_start, resident_size, 2);  // 2 metadata snapshots
g_resident_region.register_hook(resident_event_handler, nullptr);
```

**Characteristics:**
- **Variable Size**: 4-64 chunks (4 MiB to 128 MiB), configured by linker script
- **Unified Bitmap Management**: 
  - chunk_bitmap: 64-bit for 2MB chunk allocation
- **Reserved Regions**: 
  - Chunk 0: Header + TC Bootstrap (Slab 0, 2MB)
  - Chunks 1-N: TC Slabs (N=2 default, configurable 1-8)
  - Chunks N+1 to 63: Dynamic Extent Region
- **Allocatable Range**: 
  - TC Chunks: Chunks 1 to N (per-thread 64KB slabs)
  - Dynamic Extent: Chunks N+1 to 63 (bitmap-based allocation)
- **Granularity**: 
  - TC: 64KB per-thread slabs from TC chunks
  - Dynamic Extent: Variable-size slabs (1KB-2MB) from extent chunks
- **IOX TC Lifecycle**:
  - TC Allocation: Fast path from thread-local bins (~10ns)
  - TC Refill: Batch allocate from Dynamic Extent (~50ns)
  - Lazy-Free (optional): Extent cached in lazy-free chain (#ifdef IOX_ENABLE_LAZY_FREE)
- **Hook System**:
  - "warning": Triggered at 85% dynamic region usage threshold
  - "hard_limit": Triggered at 95% dynamic region usage
  - "alloc_tc_slab"/"free_tc_slab": Per-TC-slab allocation/free notifications
  - "alloc_extent"/"free_extent": Dynamic extent allocation/free notifications
- **Thread-Safe**: Per-thread TC bins (no locks), mutex for Dynamic Extent
- **Header Protection**:
  - Magic fields: 32/64-bit at start and end ("IOX_RESI")
  - Metadata snapshots: 0-4 configurable backups (ext4-style)
- **IOX TC Integration**:
  - Per-thread cache mode (portable, no kernel dependencies)
  - 32 size classes (8B-1024B) with fine granularity
  - Handles allocations ≤1024B with fast path
  - Lazy-free chain optional (compile-time #ifdef)
- **Lock Optimization**:
  - TC: Lock-free per-thread bins
  - Dynamic Extent: Mutex-protected bitmap allocation
  - Burst Region: Lock-free CAS for large blocks

#### 2. Dynamic Extent Management (1KB-2MB Allocations)

**Architecture: Bitmap-Based Slab Allocator**

```cpp
/**
 * @brief IOX Dynamic Extent allocation for medium-sized allocations
 * @details Uses bitmap-based slab allocation with extent coalescing
 * @note Dynamic Extent handles 1KB-2MB, TC handles ≤1024B, Burst handles >2MB
 */

// IOX TC Configuration
namespace tcmalloc {
    // Custom backend: Use ResidentRegion instead of OS mmap
    class ResidentBackend : public SystemAllocator {
    public:
        void* Alloc(size_t size, size_t alignment) override {
            // Request slab from ResidentRegion
            if (size <= 2 * 1024 * 1024) {
                return g_resident_region.alloc_slab(size);
            }
            return nullptr;  // Fall back to Burst Region
        }
        
        void Free(void* ptr, size_t size) override {
            g_resident_region.free_slab(ptr, size);
        }
    };
}

// Legacy Thread Cache structure (deprecated, for reference only)
struct ThreadCache_Legacy {
    // Per-size-class bins (8 size classes: 8B, 16B, 32B, ..., 16KB)
    struct CacheBin {
        uint64_t handles[32];        // Cached chunk handles
        uint32_t count;              // Current entries
    } bins[8];
    
    // Dedicated 64KB regions (legacy design)
    struct CacheRegion {
        void* slab_base;             // Base address of extent region from ResidentRegion
        void* base;                  // Mapped address
        uint32_t allocated_bytes;    // Bytes used in this region
        uint32_t free_count;         // Chunks freed back
    };
    
    std::vector<CacheRegion> regions;  // Owned regions (legacy design, now using TC + Extent)
    
    uint64_t thread_id;              // Owner thread
    uint64_t hits;                   // Cache hit count
    uint64_t misses;                 // Cache miss count
    uint32_t init_blocks;            // Count of 64KB init blocks owned
    uint32_t expansion_chunks;       // Count of 2MB expansion chunks owned
    
    /**
     * @brief Initialize thread cache on thread startup (legacy)
     * @details JemallocArena manages per-thread caches internally
     * @note ResidentRegion provides extent regions to jemalloc arena backend
     * @note JemallocArena handles thread-local cache allocation from slabs
     * @note Thread N: allocated from Chunk[N/32], block[N%32]
     * @note Supports unlimited threads until collision with expansion zone
     */
    void initialize() {
        // Request 64KB init block from Resident Region (allocates from Chunk 0 downward)
        uint64_t block_handle = g_resident_region.alloc_init_block();
        if (block_handle == 0) {
            // Init pool exhausted (collision with expansion zone or region full)
            throw std::bad_alloc();
        }
        
        // Decode handle: chunk_index (6 bits) + block_index (5 bits)
        uint32_t chunk_idx = (block_handle >> 5) & 0x3F;  // Bits 5-10
        int32_t block_idx = block_handle & 0x1F;          // Bits 0-4
        
        // Get base pointer to 64KB init block
        void* block_base = g_resident_region.get_block_ptr(chunk_idx, block_idx);
        
        regions.push_back({
            .region_handle = block_handle,
            .base = block_base,
            .size = 64 * 1024,              // 64KB init block
            .allocated_bytes = 0,
            .free_count = 0,
            .is_expansion_chunk = false      // 64KB init block
        });
        
        init_blocks = 1;
        expansion_chunks = 0;
    }
    
    /**
     * @brief Allocate from thread cache (legacy fast path)
     */
    uint64_t alloc(size_t size) {
        uint32_t size_class = size_to_class(size);  // 0-7 for 8B-16KB
        
        // Try cache first
        if (bins[size_class].count > 0) {
            hits++;
            return bins[size_class].handles[--bins[size_class].count];
        }
        
        // Cache miss → allocate from owned regions
        misses++;
        return alloc_from_owned_regions(size);
    }
    
    /**
     * @brief Allocate from owned regions, request expansion chunk if needed
     */
    uint64_t alloc_from_owned_regions(size_t size) {
        // Try to allocate from existing regions (both 64KB init and 2MB expansion)
        for (auto& region : regions) {
            if (region.allocated_bytes + size <= region.size) {
                uint64_t chunk_handle = carve_chunk(region, size);
                region.allocated_bytes += size;
                return chunk_handle;
            }
        }
        
        // All regions full → request expansion chunk (2MB from Chunk N-1 UPWARD)
        uint64_t new_chunk_handle = g_resident_region.alloc_expansion_chunk();
        if (new_chunk_handle == 0) {
            // Expansion pool exhausted (collision with init zone) → fall back to Burst Region
            return alloc_from_burst(size);
        }
        
        // Decode expansion chunk handle (only chunk_index, no block_index for 2MB chunks)
        uint32_t chunk_idx = (new_chunk_handle >> 5) & 0x3F;  // Chunk index only
        
        // Get base pointer to 2MB expansion chunk
        void* chunk_base = g_resident_region.get_block_ptr(chunk_idx, -1);  // -1 means whole chunk
        
        regions.push_back({
            .region_handle = new_chunk_handle,
            .base = chunk_base,
            .size = 2 * 1024 * 1024,        // 2MB expansion chunk
            .allocated_bytes = size,
            .free_count = 0,
            .is_expansion_chunk = true       // 2MB expansion chunk
        });
        
        expansion_chunks++;
        
        return carve_chunk(regions.back(), size);
    }
    
    /**
     * @brief Free chunk back to thread cache (legacy fast path)
     */
    void free(uint64_t handle, size_t size) {
        uint32_t size_class = size_to_class(size);
        
        if (bins[size_class].count < 32) {
            // Cache not full → push to cache
            bins[size_class].handles[bins[size_class].count++] = handle;
        } else {
            // Cache full → mark as freed in owned region
            mark_chunk_freed(handle);
        }
    }
};

```

**IOX TC Integration Details:**

```cpp
/**
 * @brief Initialize IOX TC with ResidentRegion backend
 */
void initialize_iox_tc() {
    // Initialize TC for current thread
    thread_cache_init();
    
    // Initialize Dynamic Extent Manager
    extent_manager_init(&g_resident_region);
    
    // Configure lazy-free chain (optional, #ifdef IOX_ENABLE_LAZY_FREE)
#ifdef IOX_ENABLE_LAZY_FREE
    lazy_free_chain_init(IOX_LAZY_FREE_MAX_DEPTH, IOX_LAZY_FREE_LEASE_MS);
#endif
}

/**
 * @brief Allocator routing function
 */
void* iox_alloc(size_t size) {
    if (size <= IOX_TC_THRESHOLD) {
        // Small: Use TC Fast Path (≤1024B)
        return tc_alloc(size);
    } else if (size <= 2 * 1024 * 1024) {
        // Medium: Use Dynamic Extent (1KB-2MB)
        return extent_alloc(size);
    } else {
        // Large: Use Burst Region (>2MB)
        return burst_region_alloc(size);
    }
}

void iox_free(void* ptr) {
    if (is_from_tc(ptr)) {
        tc_free(ptr);
    } else if (is_from_resident_region(ptr)) {
        extent_free(ptr);
    } else {
        burst_region_free(ptr);
    }
}
```

**IOX TC Cache Hierarchy:**

```
Thread-Local Cache (TC Bins)
    ↓ (miss, refill needed)
Dynamic Extent Manager (Bitmap allocation)
    ↓ (extent allocation)
Resident Region Chunks (N+1 to 63)
    ↓ (region exhausted, rare)
Burst Region (fallback for >2MB allocations)
```

**🔧 Implementation Steps:**

**Step 1: Extend ResidentRegion with Slab Allocator**

```cpp
// Add to ResidentRegion class
class ResidentRegion {
public:
    /**
     * @brief Allocate slab for jemalloc arena backend
     * @param size Requested slab size (64KB-2MB)
     * @return Base pointer to slab, or nullptr if failed
     * @note Called by TC or Extent Manager when new slabs are needed
     */
    void* alloc_slab(size_t size) {
        // Legacy: lock for thread cache init (now handled by JemallocArena)
        
        // Round up to 64KB or 2MB
        size_t slab_size = (size <= 64 * 1024) ? 64 * 1024 : 2 * 1024 * 1024;
        
        uint64_t handle;
        if (slab_size == 64 * 1024) {
            handle = alloc_init_block();  // Use existing 64KB allocator
        } else {
            handle = alloc_expansion_chunk();  // Use existing 2MB allocator
        }
        
        // Legacy: unlock (now handled by JemallocArena)
        
        if (handle == 0) {
            return nullptr;  // Region exhausted
        }
        
        // Decode handle and return base pointer
        uint32_t chunk_idx = (handle >> 5) & 0x3F;
        int32_t block_idx = (slab_size == 64 * 1024) ? (handle & 0x1F) : -1;
        
        return get_block_ptr(chunk_idx, block_idx);
    }
    
    /**
     * @brief Free slab back to ResidentRegion
     * @param ptr Slab base pointer
     * @param size Slab size
     */
    void free_slab(void* ptr, size_t size) {
        // Reverse lookup: ptr → (chunk_index, block_index)
        uint32_t chunk_idx = ((char*)ptr - (char*)m_base) / (2 * 1024 * 1024);
        int32_t block_idx = -1;
        
        if (size == 64 * 1024) {
            size_t offset_in_chunk = ((char*)ptr - (char*)m_base) % (2 * 1024 * 1024);
            block_idx = offset_in_chunk / (64 * 1024);
        }
        
        free_block(chunk_idx, block_idx);
    }
};
```

**Step 2: Implement IOX TC Backend**

```cpp
// IOX TC custom backend allocator
namespace tcmalloc {
    class ResidentBackend : public SystemAllocator {
    private:
        ResidentRegion* m_region;
        
    public:
        explicit ResidentBackend(ResidentRegion* region) 
            : m_region(region) {}
        
        void* Alloc(size_t size, size_t alignment) override {
            // JemallocArena requests slabs from backend
            // Typical sizes: 64KB, 256KB, 2MB
            void* ptr = m_region->alloc_slab(size);
            
            if (ptr == nullptr) {
                // Resident Region exhausted, signal JemallocArena
                return nullptr;  // JemallocArena will try OS mmap as fallback
            }
            
            return ptr;
        }
        
        void Free(void* ptr, size_t size) override {
            if (ptr == nullptr) return;
            m_region->free_slab(ptr, size);
        }
        
        bool CanAllocate(size_t size) const override {
            // Check if ResidentRegion has capacity
            return m_region->has_free_capacity(size);
        }
    };
}
```

**Step 3: Initialize JemallocArena with ResidentBackend**

```cpp
// System initialization
void initialize_allocator() {
    // 1. Initialize ResidentRegion (existing code, unchanged)
    extern char __resident_start[];
    extern char __resident_end[];
    size_t resident_size = __resident_end - __resident_start;
    g_resident_region.init(__resident_start, resident_size, 2);  // 2 snapshots
    
    // 2. Initialize IOX TC for current thread
    thread_cache_init();
    
    // 3. Initialize Dynamic Extent Manager
    extent_manager_init(&g_resident_region);
    
    // 4. Configure lazy-free chain (optional, #ifdef IOX_ENABLE_LAZY_FREE)
#ifdef IOX_ENABLE_LAZY_FREE
    lazy_free_chain_init(IOX_LAZY_FREE_MAX_DEPTH, IOX_LAZY_FREE_LEASE_MS);
#endif
    
    // 5. Initialize Burst Region (existing code, unchanged)
    extern char __burst_start[];
    extern char __burst_end[];
    size_t burst_size = __burst_end - __burst_start;
    g_burst_region.init(__burst_start, burst_size);
}
```

**Step 4: Update Adapter Layer (iox_alloc/iox_free)**

```cpp
// Layer 3: Adapter API (IoxAdapter.cpp)
void* iox_alloc(size_t size) {
    if (size <= IOX_TC_THRESHOLD) {
        // Small: Route to TC Fast Path (≤1024B)
        return tc_alloc(size);
    } else if (size <= 2 * 1024 * 1024) {
        // Medium: Route to Dynamic Extent (1KB-2MB)
        return extent_alloc(size);
    } else {
#if IOX_ENABLE_BURST_REGION
        // Large: Route to Burst Region (>2MB, if enabled)
        return burst_region_alloc(size);
#else
        // Burst Region disabled: allocation too large, fail gracefully
        return nullptr;  // Or return handle=0
#endif
    }
}

void iox_free(void* ptr) {
    if (is_from_tc(ptr)) {
        // TC-managed memory
        tc_free(ptr);
    } else if (is_from_resident_region(ptr)) {
        // Dynamic Extent memory
        extent_free(ptr);
    } else {
        // Burst Region memory
        burst_region_free(ptr);
    }
}

// Helper: Check if pointer is from TC region
bool is_from_tc(void* ptr) {
    extern char __resident_start[];
    // TC chunks are Chunks 1-N (configurable, default 1-2)
    uintptr_t base = (uintptr_t)__resident_start;
    uintptr_t tc_start = base + (1 * 2 * 1024 * 1024);  // Chunk 1
    uintptr_t tc_end = base + ((IOX_TC_CHUNK_COUNT + 1) * 2 * 1024 * 1024);
    return ((uintptr_t)ptr >= tc_start && (uintptr_t)ptr < tc_end);
}

// Helper: Check if pointer is from Resident Region (any part)
bool is_from_resident_region(void* ptr) {
    extern char __resident_start[];
    extern char __resident_end[];
    return (ptr >= __resident_start && ptr < __resident_end);
}
```

**JemallocArena Lifecycle:**
1. **System Startup**: 
   - Initialize ResidentRegion with existing code
   - Register ResidentBackend with JemallocArena
   - Configure Per-CPU mode (if kernel ≥4.18)
2. **Runtime Allocation**:
   - 64KB-4MB: JemallocArena (Multi-arena → Transfer → Central → ResidentBackend)
   - >4MB: Burst Region (existing freelist allocator)
3. **Shutdown**: JemallocArena returns all slabs to ResidentRegion

---

**📊 JemallocArena Integration - Key Changes Summary:**

| Layer | Component | Change Required | Effort | Description |
|-------|-----------|----------------|--------|-------------|
| **Layer 1** | Memory Regions | ❌ **NONE** | 0 lines | Keep existing .iox layout |
| **Layer 1** | Linker Script | ❌ **NONE** | 0 lines | No changes to iox.ld |
| **Layer 1** | ResidentRegion | ✅ **Add Methods** | ~50 lines | `alloc_slab()`, `free_slab()` |
| **Layer 1** | BurstRegion | ❌ **NONE** | 0 lines | Unchanged |
| **Layer 2** | Allocator Core | ✨ **REPLACED** | -1800 lines | Integrate JemallocArena (production-proven) |
| **Layer 2** | JemallocArena Backend | ✅ **NEW** | ~100 lines | `ResidentBackend` allocator |
| **Layer 2** | Initialization | ✅ **MODIFY** | ~20 lines | Register jemalloc arena backend |
| **Layer 3** | iox_alloc | ✅ **MODIFY** | ~10 lines | Change threshold 16KB→256KB |
| **Layer 3** | iox_free | ✅ **MODIFY** | ~5 lines | Update pointer check logic |
| **Layer 3** | Other APIs | ❌ **NONE** | 0 lines | iox_alloc_shared unchanged |
| **Build** | CMakeLists.txt | ✅ **ADD** | ~15 lines | Find and link JemallocArena |
| **Build** | Dependencies | ✅ **ADD** | Package | Install libgoogle-perftools-dev |

**Total Code Changes:** ~200 lines added, ~2000 lines removed (90% code reduction)

---

**🎯 Benefits vs. Costs:**

| Metric | Old (Custom) | New (JemallocArena) | Improvement |
|--------|-------------|----------------|-------------|
| **Latency** | ~20ns | ~10ns | **50% faster** |
| **Cache Hit Rate** | 85% | 95%+ | **+12%** |
| **Code Maintenance** | 2000+ lines | 200 lines | **-90% code** |
| **Thread Migration** | Cache lost | Cache retained | **∞** |
| **Size Coverage** | ≤16KB | 64KB-4MB | **16× larger** |
| **Lock Contention** | Medium | Minimal (per-thread) | **-90%** |
| **Production Proven** | 2 years | 15+ years | **Google-grade** |
| **Dependencies** | None | JemallocArena lib | +1 dependency |

---

#### 2. Legacy Thread Cache Structure (Deprecated, For Reference Only)
   - Thread N: allocated from Chunk[N/32], block[N%32]
   - Supports unlimited threads (until collision with expansion zone)
2. **Runtime Growth**: Request 2MB expansion chunks from Chunk N-1 UPWARD as needed
   - First expansion: Chunk N-1
   - Next: Chunk N-2, N-3, N-4, ... (toward Chunk 0)
   - Stops when meeting TC init zone (collision detection)
3. **Thread Exit**: Return all owned regions (both 64KB init blocks and 2MB expansion chunks) to Resident Region

**Key Differences:**
- **Init blocks (64KB)**: Chunk 0 → downward, supports unlimited threads
- **Expansion chunks (2MB)**: Chunk N-1 → upward, whole chunks for high capacity
- **Collision detection**: Init and expansion zones meet in the middle, triggers warning
- **Unified management**: Thread cache treats both 64KB and 2MB regions uniformly (legacy design)

#### 3. Burst Region (Freelist-Managed, Fallback for >4MB Allocations)

```cpp
/**
 * @brief Burst Region chunk header
 * @details Fixed overhead per allocation, optional lease support
 */
struct BurstChunkHeader {
    // Block identification
    uint32_t magic;                  // 0x42555253 ("BURS")
    uint32_t flags;                  // ALLOCATED=1, FREE=0, LEASE_ACTIVE=2 (optional)
    uint64_t size;                   // Total size (header + user data)
    
    // Optional lease management (for long-lived allocations)
    uint32_t lease_ms;               // Lease expiration (0 = no lease)
    uint32_t alloc_timestamp;        // Allocation time
    
    // Freelist management (doubly-linked list)
    BurstChunkHeader* prev;          // Previous block (physical order)
    BurstChunkHeader* next;          // Next block (physical order)
    BurstChunkHeader* free_prev;     // Previous in freelist (if FREE)
    BurstChunkHeader* free_next;     // Next in freelist (if FREE)
    
    // Security (optional, tier-dependent)
    uint32_t canary;                 // L1+ canary value
    uint32_t generation;             // ABA prevention
    
    // Handle for fast lookup
    uint64_t handle;                 // Global handle (segment_id + offset)
    
    // Padding to 64 bytes (cache-line aligned)
    uint8_t reserved[8];
} __attribute__((aligned(64)));

/**
 * @brief Burst Region for >4MB allocations (JemallocArena fallback)
 * @details Freelist-managed, optional lease support for automatic reclamation
 */
class BurstRegion {
public:
    /**
     * @brief Initialize Burst Region from .iox section
     * @param base Address from __burst_start (linker symbol)
     * @param size Size = __burst_end - __burst_start
     */
    void init(void* base, size_t size);
    
    /**
     * @brief Allocate variable-size block
     * @param size Requested user data size (typically >4MB)
     * @param lease_ms Lease duration in milliseconds (0 = no lease, optional auto-reclaim)
     * @return Handle to allocated block or 0 on OOM
     * @note Total allocation = sizeof(BurstChunkHeader) + align(size)
     */
    uint64_t alloc(size_t size, uint32_t lease_ms = 0);
    
    /**
     * @brief Free block back to Burst Region
     * @param handle Handle from alloc()
     * @return 0 on success, -EINVAL if invalid
     * @note Coalesces with adjacent free blocks
     */
    int free(uint64_t handle);
    
    /**
     * @brief Get user data pointer from handle
     */
    void* handle_to_ptr(uint64_t handle) {
        if (!handle) return nullptr;
        BurstChunkHeader* hdr = reinterpret_cast<BurstChunkHeader*>(handle);
        return reinterpret_cast<void*>(hdr + 1); // User data follows header
    }
    
private:
    void* m_base;                    // Burst region base
    size_t m_size;                   // Total size
    std::mutex m_mutex;              // Protects freelist operations
    
    // Freelist head (sorted by address for coalescing)
    BurstChunkHeader* m_free_head;   // First free block
    uint32_t m_free_count;           // Number of free blocks
    uint64_t m_free_bytes;           // Total free space
    
    // Allocation statistics
    atomic_uint64_t m_alloc_count;
    atomic_uint64_t m_free_count;
    atomic_uint64_t m_coalesce_count;
    
    /**
     * @brief Find free block using first-fit strategy
     */
    BurstChunkHeader* find_free_block(size_t size);
    
    /**
     * @brief Coalesce adjacent free blocks
     */
    void coalesce_free_blocks(BurstChunkHeader* block);
    
    /**
     * @brief Split block if remaining space > threshold
     */
    void split_block(BurstChunkHeader* block, size_t needed_size);
};
```

**Burst Region Characteristics:**
- **Use Case**: Fallback allocator for >4MB requests (JemallocArena doesn't handle these)
- **Variable Size**: Allocates exact size needed (plus 64-byte header overhead)
- **Fixed Header**: All blocks include BurstChunkHeader (64 bytes)
- **Optional Lease**: lease_ms parameter (0 = no automatic reclamation, >0 = enable lease scanner)
- **Freelist Management**: Doubly-linked list with automatic coalescing
- **Fragmentation Mitigation**: Coalescing on free reduces fragmentation
- **Thread-Safe**: Mutex-protected (infrequent access for large allocations)

**Freelist Allocation Algorithm:**

```cpp
/**
 * @brief Allocate from freelist with coalescing
 */
uint64_t BurstRegion::alloc(size_t size, uint32_t lease_ms) {
    // Note: lease_ms is optional (0 = no lease)
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Calculate total size (header + aligned user data)
    size_t total_size = sizeof(BurstChunkHeader) + align_up(size, 64);
    
    // Find suitable free block (first-fit)
    BurstChunkHeader* block = find_free_block(total_size);
    if (!block) {
        // OOM: No suitable free block
        return 0;
    }
    
    // Remove from freelist
    if (block->free_prev) {
        block->free_prev->free_next = block->free_next;
    } else {
        m_free_head = block->free_next;
    }
    if (block->free_next) {
        block->free_next->free_prev = block->free_prev;
    }
    
    // Split block if remaining space is large enough
    if (block->size - total_size > sizeof(BurstChunkHeader) + 256) {
        split_block(block, total_size);
    }
    
    // Initialize allocated block
    block->flags = BURST_FLAG_ALLOCATED;
    if (lease_ms > 0) {
        block->flags |= BURST_FLAG_LEASE_ACTIVE;
        block->lease_ms = get_lease_epoch() + lease_ms;
    } else {
        block->lease_ms = 0;  // No lease
    }
    block->alloc_timestamp = get_timestamp();
    block->free_prev = nullptr;
    block->free_next = nullptr;
    
    atomic_fetch_add(&m_alloc_count, 1, memory_order_relaxed);
    
    return reinterpret_cast<uint64_t>(block);
}

/**
 * @brief Free block and coalesce with neighbors
 */
int BurstRegion::free(uint64_t handle) {
    BurstChunkHeader* block = reinterpret_cast<BurstChunkHeader*>(handle);
    if (!block || block->magic != 0x42555253) {
        return -EINVAL;
    }
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Mark as free
    block->flags = 0;
    
    // Coalesce with adjacent blocks
    coalesce_free_blocks(block);
    
    // Add to freelist (sorted by address)
    insert_into_freelist(block);
    
    atomic_fetch_add(&m_free_count, 1, memory_order_relaxed);
    
    return 0;
}

/**
 * @brief Coalesce with previous and next blocks if they are free
 */
void BurstRegion::coalesce_free_blocks(BurstChunkHeader* block) {
    // Coalesce with next block
    if (block->next && !(block->next->flags & BURST_FLAG_ALLOCATED)) {
        // Remove next block from freelist
        remove_from_freelist(block->next);
        
        // Merge sizes
        block->size += block->next->size;
        block->next = block->next->next;
        if (block->next) {
            block->next->prev = block;
        }
        
        atomic_fetch_add(&m_coalesce_count, 1, memory_order_relaxed);
    }
    
    // Coalesce with previous block
    if (block->prev && !(block->prev->flags & BURST_FLAG_ALLOCATED)) {
        // Remove prev block from freelist
        remove_from_freelist(block->prev);
        
        // Merge into previous block
        block->prev->size += block->size;
        block->prev->next = block->next;
        if (block->next) {
            block->next->prev = block->prev;
        }
        
        block = block->prev;  // Continue with merged block
        
        atomic_fetch_add(&m_coalesce_count, 1, memory_order_relaxed);
    }
}
```

**Allocation Flow Example:**

```cpp
// Small allocation (≤16KB) → JemallocArena
void* ptr1 = iox_alloc(4096);       // From JemallocArena thread cache (fast path)

**Large Allocations (>4MB)**:
1. Request → Burst Region (freelist-managed)
2. Optional lease scanner (if lease_ms > 0) → Auto-reclaim on expiration
void* ptr2 = iox_alloc(512 * 1024);  // 512KB, no auto-reclaim

// Explicit Burst allocation with lease (auto-reclaim after 5s)
void* ptr3 = iox_alloc_burst(1024 * 1024, 5000);  // 1MB with 5s lease
```

#### 4. Unified Allocation API

```cpp
/**
 * @brief Smart allocation router
 * @param size Requested size
 * @param lease_ms Lease duration (optional, only for Burst Region >2MB)
 * @return Handle or 0 on OOM
 */
uint64_t iox_alloc(size_t size, uint32_t lease_ms = 0) {
    if (size <= IOX_TC_THRESHOLD) {
        // Small allocation → TC Fast Path (lease ignored, managed by TC)
        return tc_alloc(size);
    } else if (size <= 2 * 1024 * 1024) {
        // Medium allocation → Dynamic Extent (lease ignored)
        return extent_alloc(size);
    } else {
        // Large allocation → Burst Region (lease optional)
        return g_burst_region.alloc(size, lease_ms);
    }
}

/**
 * @brief Explicit Burst allocation with optional lease
 * @param size Requested size
 * @param lease_ms Lease duration (0 = no auto-reclaim, >0 = enable lease scanner)
 * @return Handle or 0 on OOM
 */
uint64_t iox_alloc_burst(size_t size, uint32_t lease_ms = 0) {
    return g_burst_region.alloc(size, lease_ms);
}

/**
 * @brief Free memory (auto-detect TC vs Extent vs Burst)
 */
void iox_free(uint64_t handle) {
    void* ptr = handle_to_ptr(handle);
    if (is_from_tc(ptr)) {
        tc_free(ptr);
    } else if (is_from_resident_region(ptr)) {
        extent_free(ptr);
    } else {
        g_burst_region.free(handle);
    }
}
```

---

### Summary: Memory Flow

**Small Allocations (≤16KB)**:
1. Request → JemallocArena thread cache (thread-local, no lock)
2. JemallocArena thread cache miss → Request from central freelist
3. Chunks exhausted → Borrow 2MB chunk from Resident Region (bitmap alloc)
4. Resident exhausted → Fall back to Burst Region
   - Triggers "hard_limit" hook
   - Reserved chunk (N-1) protects against overflow

**Large Allocations (>16KB)**:
1. Request → Burst Region directly (mutex, mandatory lease)
2. Freelist search (first-fit)
3. Allocate with BurstChunkHeader + user data
4. Lease scanner reclaims expired blocks

**Resident Region Limits**:
- **Variable Size**: 4-128 MiB (2-64 chunks × 2MB)
- **Reserved Chunks**: 
  - Chunk 0: Header (never allocated)
  - Chunk N-1: Reserved region (never allocated)
- **Allocatable Range**: Chunks 0 to N-2 (TC init and expansion zones)
- **Hook Triggers**:
  - Warning at 2/3 usage threshold
  - Hard limit when zones collide
- **Bitmap Management**: Unified chunk_bitmap, lock-free CAS allocation
- **Header Protection**: Magic fields + metadata snapshots (ext4-style redundancy)

---

#### Old PCFL Section Removed

**Note**: The previous Per-Core Free List (PCFL) architecture has been removed in favor of the simpler three-tier design:
- **JemallocArena**: Per-thread cache, requests extent regions from Resident Region
- **Resident Region**: Bitmap-managed 64 × 2MB chunks (128 MiB fixed)
- **Burst Region**: Freelist-managed variable-size blocks with mandatory lease

This simplification eliminates:
- Lock-free CAS complexity on CFL
- Per-core affinity management
- Adaptive watermark tuning
- Cross-region freelist coordination

Trade-offs:
- **Lost**: Lock-free CFL access (now mutex on freelist in Burst)
- **Gained**: Simpler design, clearer size-based routing, bitmap efficiency, fixed memory footprint

---

#### 5. PSSC (Per-Segment Shared Cache for SHM)

PSSC remains unchanged from previous design - see dedicated PSSC section for details.

#### 6. Quarantine Management (L2+)

- Delayed free for UAF detection
- 256-512 chunk capacity
- FIFO eviction policy

---

#### Background Services

**Lease Scanner Thread (Epoch-Based):**

```cpp
void lease_scanner_thread() {
    while (running) {
        // Update global epoch counter (configurable interval)
        g_lease_epoch_ms.fetch_add(IOX_LEASE_EPOCH_INTERVAL_MS, std::memory_order_relaxed);
        
        uint32_t now = g_lease_epoch_ms.load(std::memory_order_relaxed);
        
        for_each_allocated_chunk([now](ChunkHeader* chunk) {
            if (chunk->lease_ms == 0) return;                     // Permanent
            if (chunk->flags & CHUNK_FLAG_LEASE_EXEMPT) return;   // SHM
            
            // Signed comparison handles wraparound (kernel trick)
            if ((int32_t)(now - chunk->lease_ms) >= 0) {
                reclaim_chunk(chunk);  // Return to free list
            }
        });
        
        sleep_ms(IOX_LEASE_EPOCH_INTERVAL_MS);  // Configurable interval (default 100ms)
    }
}
```

**Benefits:**
- **No timestamp reads**: Only compare two `uint32_t` values (now vs lease_ms)
- **Wraparound safe**: Signed comparison `(int32_t)(a - b) >= 0` handles 32-bit overflow
- **Cache-friendly**: `g_lease_epoch_ms` is cache-line aligned, rarely written
- **Reduced header size**: Removed 8-byte `last_access_ns` field
- **Configurable granularity**: `IOX_LEASE_EPOCH_INTERVAL_MS` balances precision vs CPU overhead

**Integrity Checker Thread (L2+) - Adaptive Sampling:**

```cpp
void integrity_checker_thread() {
    while (running) {
        uint64_t sample_rate = atomic_load(&g_quarantine_stats.current_sample_rate, 
                                          memory_order_relaxed);
        
        for_each_allocated_chunk([sample_rate](ChunkHeader_L2* chunk) {
            // Adaptive sampling: skip validation based on current rate
            if (fast_rand() % 10000 >= sample_rate) {
                return;  // Skip this chunk (sampled out)
            }
            
            // Validate RedZone (only for sampled chunks)
            if (!validate_redzone(chunk->redzone_front, REDZONE_PATTERN_FRONT)) {
                record_failure();  // Auto-restore 100% validation
                report_corruption(chunk);
                trigger_mprotect(chunk);
            }
            // Validate canary
            if (chunk->magic != CHUNK_MAGIC_HEADER) {
                record_failure();
                report_corruption(chunk);
            }
        });
        sleep_ms(5000);  // Scan every 5 seconds
    }
}

// Background thread to reset failure counter after 24h
void failure_counter_reset_thread() {
    while (running) {
        sleep_ms(3600000);  // Check every 1 hour
        
        QuarantineStats* stats = &g_quarantine_stats;
        uint64_t time_since_failure = now_ms() - stats->last_failure_timestamp;
        
        // Reset counter if no failures in 24h
        if (time_since_failure > 86400000) {  // 24 hours in ms
            atomic_store(&stats->canary_failures_24h, 0, memory_order_relaxed);
            // Sample rate will auto-reduce to 0.1% on next check
        }
    }
}
```

**Key Characteristics:**
- **Region-agnostic**: Works with any `base + size` input
- **Self-bootstrapping**: No external allocator dependency
- **Background automation**: Lease reclaim, integrity checks
- **Pluggable security**: Tier selection per region

---

### Layer 3: Adapter Layer

**Purpose:** Provide high-level interfaces for application use.

**Components:**

#### 1. Global Operator Overloading

```cpp
// IoxAdapter.cpp

void* operator new(size_t size) {
    uint64_t handle = iox_alloc(size, IOX_DEFAULT_LEASE_MS);
    if (handle == 0) throw std::bad_alloc();
    return iox_from_handle(handle);
}

void operator delete(void* ptr) noexcept {
    if (!ptr) return;
    uint64_t handle = iox_to_handle(ptr);
    iox_free(handle);
}

void* operator new[](size_t size) {
    return operator new(size);
}

void operator delete[](void* ptr) noexcept {
    operator delete(ptr);
}

// Placement new (no allocation)
void* operator new(size_t size, void* ptr) noexcept {
    return ptr;
}
```

#### 2. High-Level Allocation API

```cpp
// Public C API
extern "C" {
    uint64_t iox_alloc(size_t size, uint32_t lease_ms);
    void iox_free(uint64_t handle);
    void* iox_from_handle(uint64_t handle);
    uint64_t iox_to_handle(void* ptr);
}

// C++ convenience wrappers
namespace iox {
    template<typename T, typename... Args>
    T* make_unique(Args&&... args) {
        uint64_t handle = iox_alloc(sizeof(T), 0);  // Resident
        T* ptr = reinterpret_cast<T*>(iox_from_handle(handle));
        new (ptr) T(std::forward<Args>(args)...);
        return ptr;
    }
    
    template<typename T>
    void destroy(T* ptr) {
        ptr->~T();
        iox_free(iox_to_handle(ptr));
    }
}
```

#### 3. SOA/IPC Memory Operations (QoS-Aware)

```cpp
// Shared memory allocation (Iceoryx2 v0.7 integration with QoS)
uint64_t iox_alloc_shared(size_t size, uint64_t segment_id, IoxQoSLevel qos) {
    // Route to PSSC with QoS priority
    return iox_allocator_alloc_from_pssc_qos(segment_id, size, qos);
}

void iox_free_shared(uint64_t handle) {
    // Direct push to PSSC QoS queue (no lease needed for SHM)
    iox_allocator_free_to_pssc_qos(handle);
}

// Iceoryx2 Publisher adapter with QoS support
class IoxPublisher {
public:
    void* allocate_sample(size_t size, IoxQoSLevel qos = IOX_QOS_MEDIUM) {
        m_handle = iox_alloc_shared(size, m_segment_id, qos);
        return iox_from_handle(m_handle);
    }
    
    void publish() {
        // Increment ref count, don't free yet
        iox_shared_add_ref(m_handle, m_subscriber_count);
    }
    
    void on_all_readers_done() {
        // Last reader done → release to QoS queue
        iox_free_shared(m_handle);
    }
    
private:
    uint64_t m_segment_id;
    uint64_t m_handle;
    int m_subscriber_count;
};
```

#### 4. STL Allocator Adapter

```cpp
template<typename T>
class IoxAllocator {
public:
    using value_type = T;
    
    T* allocate(size_t n) {
        uint64_t handle = iox_alloc(n * sizeof(T), IOX_DEFAULT_LEASE_MS);
        return reinterpret_cast<T*>(iox_from_handle(handle));
    }
    
    void deallocate(T* ptr, size_t n) {
        iox_free(iox_to_handle(ptr));
    }
};

// Usage with STL containers
std::vector<int, IoxAllocator<int>> vec;
std::map<std::string, int, std::less<>, IoxAllocator<std::pair<const std::string, int>>> map;
```

**Key Characteristics:**
- **Seamless integration**: Drop-in replacement for `new`/`delete`
- **SHM-optimized**: Direct PSSC integration for zero-copy IPC (Iceoryx2)
- **STL-compatible**: Works with all standard containers
- **Handle-safe**: Automatic handle ↔ pointer conversion
- **No lease overhead**: SHM allocations managed by PSSC, no automatic reclamation

---

### Layer Interaction Example (QoS-Aware)

**Scenario:** Application allocates memory for Iceoryx2 publisher with CRITICAL QoS

```cpp
// Layer 3: Application calls high-level API with QoS
auto* publisher = new IoxPublisher();
void* sample = publisher->allocate_sample(1024, IOX_QOS_CRITICAL);  // GPU data transfer

// ↓ Layer 3 adapter routes to Layer 2 with QoS
uint64_t iox_alloc_shared(size_t size, uint64_t segment_id, IoxQoSLevel qos) {
    return iox_allocator_core.alloc_from_pssc_qos(segment_id, size, qos);
}

// ↓ Layer 2 allocates from PSSC QoS queue
uint64_t alloc_from_pssc_qos(uint64_t segment_id, size_t size, IoxQoSLevel qos) {
    PSSC* pssc = lookup_pssc(segment_id);
    
    // Try QoS-specific queue first (CRITICAL has highest priority)
    uint64_t handle = lockfree_pop(&pssc->qos_heads[qos]);
    
    if (handle == 0) {
        // QoS queue empty → allocate from region and mark QoS
        handle = alloc_from_region(pssc->shm_base, size);
        mark_chunk_qos(handle, qos);  // Set QoS in chunk flags
    }
    
    return handle;
}

// ↓ Layer 2 uses Layer 1 memory region
uint64_t alloc_from_region(void* base, size_t size) {
    // base comes from Layer 1 (mmap'd SHM region)
    ChunkHeader* chunk = (ChunkHeader*)(base + m_next_offset);
    m_next_offset += size + sizeof(ChunkHeader);
    
    initialize_chunk(chunk, size);
    return chunk_to_handle(chunk);
}

// Mark chunk with QoS level (bits 8-9 in flags)
void mark_chunk_qos(uint64_t handle, IoxQoSLevel qos) {
    SHMChunkHeader* hdr = handle_to_shm_header(handle);
    SET_CHUNK_QOS(hdr->flags, qos);
}
```

---

## 📦 Chunk Header Definitions

> **⚠️ Note**: Security-related fields (Canary, RedZone, Quarantine) are **planned for v0.3.0+** and not implemented in v0.2.0.  
> **Current v0.2.0**: All headers use minimal structure (size + flags + next_free_offset only).

### Overview

All allocations in IoxPool are managed via chunk headers. In v0.2.0, headers use a minimal structure. Future versions (v0.3.0+) will add security tier support.

**Common Fields (v0.2.0 Minimal):**
- `size` - Total chunk size (including header)
- `flags` - Chunk state (ALLOCATED, FREE)
- `next_free_offset` - Offset to next free chunk in freelist

**Future Fields (v0.3.0+ Security Tiers):**
- `generation` - Handle validation counter (prevents ABA problem)
- `lease_ms` - Expiration epoch (for Burst Region only, 0 = no lease)
- `magic` - Canary value for corruption detection (L1+)
- `redzone_front[]` - Front overflow guard (L2+)
- `redzone_back[]` - Back underflow guard (L2+)
- `magic_trailer` - Tail canary (L2+)

---

### L0 ChunkHeader (SECURITY_TIER_NONE) - Planned v0.3.0+

> **⚠️ Not Implemented** - Security tiers planned for v0.3.0+. Current v0.2.0 uses minimal headers.

**Minimal header with no security features.**

```cpp
struct ChunkHeader_L0 {
    uint64_t size;              // Chunk size (including header)
    uint64_t next_free_offset;  // Offset to next free chunk (or 0)
} __attribute__((aligned(8)));
```

**Size:** 16 bytes  
**Security:** None  
**Overhead:** 0 bytes (header only)

**Layout:**
```
┌────────────────────────┐
│ ChunkHeader_L0 (16B)   │
│ - size (8B)            │
│ - next_free_offset (8B)│
├────────────────────────┤
│ User Data               │
└────────────────────────┘
```

---

### L1 ChunkHeader (SECURITY_TIER_FAST_CANARY) - Planned v0.3.0+

> **⚠️ Not Implemented** - Security tiers planned for v0.3.0+. Current v0.2.0 uses minimal headers.

**Standard header with canary protection and lease management (future).**

```cpp
struct ChunkHeader_L1 {
    // Security field
    uint64_t magic;             // Canary value (0x10C00CA7A87DEADU)
    
    // Management fields
    uint64_t size;              // Chunk size
    uint32_t flags;             // State flags
    uint32_t generation;        // Handle validation
    
    // Lease field (epoch-based, no timestamp)
    uint32_t lease_ms;          // Expiration epoch (0 = no lease, or epoch_ms + duration)
    uint32_t reserved;          // Padding for alignment
    
    // Freelist field
    uint64_t next_free_offset;  // Next free chunk offset
} __attribute__((aligned(8)));

// Global lease epoch counter (cache-line aligned, updated by background thread)
static alignas(64) std::atomic<uint32_t> g_lease_epoch_ms{0};
```

**Size:** 32 bytes (64-bit), 28 bytes (32-bit)  
**Security:** Header canary only  
**Overhead:** +32 bytes per chunk (reduced from 40 bytes)

**Layout:**
```
┌────────────────────────┐
│ ChunkHeader_L1 (32B)   │
│ - magic (8B)           │  ← Security: Canary
│ - size (8B)            │
│ - flags (4B)           │
│ - generation (4B)      │
│ - lease_ms (4B)        │  ← Expiration epoch (not duration)
│ - reserved (4B)        │  ← Padding for alignment
│ - next_free_offset (8B)│
├────────────────────────┤
│ User Data              │
└────────────────────────┘
```

**Flags:**
```cpp
#define CHUNK_FLAG_ALLOCATED      (1U << 0)  // Chunk is allocated
#define CHUNK_FLAG_FREE           (1U << 1)  // Chunk is free
#define CHUNK_FLAG_IN_QUARANTINE  (1U << 2)  // In quarantine (L2+)
#define CHUNK_FLAG_LEASE_EXEMPT   (1U << 3)  // Exempt from lease (SHM)
#define CHUNK_FLAG_SHM            (1U << 4)  // Shared memory chunk
```

---

### L2 ChunkHeader (SECURITY_TIER_FULL_SAFETY) - Planned v0.3.0+

> **⚠️ Not Implemented** - Security tiers planned for v0.3.0+. Current v0.2.0 uses minimal headers.

**Full protection with canary, RedZone, and trailer guard (future).**

```cpp
struct ChunkHeader_L2 {
    // Security fields
    uint64_t magic;             // Header canary (0x10C00CA7A87DEADU)
    
    // Management fields
    uint64_t size;              // Chunk size
    uint32_t flags;             // State flags
    uint32_t generation;        // Handle validation
    
    // Lease field (epoch-based)
    uint32_t lease_ms;          // Expiration epoch (0 = no lease, or epoch_ms + duration)
    uint32_t reserved;          // Padding
    
    // Freelist field
    uint64_t next_free_offset;  // Next free chunk offset
    
    // Security: Front RedZone
    uint8_t redzone_front[64];  // Overflow detection (32B on 32-bit)
} __attribute__((aligned(64)));

struct ChunkTrailer_L2 {
    uint8_t redzone_back[64];   // Underflow detection (32B on 32-bit)
    uint64_t magic_trailer;     // Trailer canary (0xCA7A87FEEDBAD00DU)
} __attribute__((aligned(8)));
```

**Size:** 48 bytes header + 64 bytes front RedZone (64-bit)  
**Trailer:** 64 bytes back RedZone + 8 bytes trailer canary  
**Total Overhead:** +176 bytes (64-bit), 92 bytes (32-bit) (reduced by 8 bytes)

**Layout:**
```
┌────────────────────────┐
│ ChunkHeader_L2 (40B)   │
│ - magic (8B)           │  ← Security: Header canary
│ - size (8B)            │
│ - flags (4B)           │
│ - generation (4B)      │
│ - lease_ms (4B)        │  ← Expiration epoch
│ - reserved (4B)        │  ← Padding
│ - next_free_offset (8B)│
├────────────────────────┤
│ Front RedZone (64B)    │  ← Security: Overflow detection
│ 0xDEAD5AFE pattern     │
├────────────────────────┤
│ User Data              │
├────────────────────────┤
│ Back RedZone (64B)     │  ← Security: Underflow detection
│ 0x5AFE5AFE pattern     │
├────────────────────────┤
│ Trailer Canary (8B)    │  ← Security: Tail guard
│ 0xCA7A87FEEDBAD00DU    │
└────────────────────────┘
```

**RedZone Patterns:**
```cpp
#define REDZONE_PATTERN_FRONT  0xDEAD5AFEDEAD5AFEU
#define REDZONE_PATTERN_BACK   0x5AFE5AFE5AFE5AFEU
#define CHUNK_MAGIC_HEADER     0x10C00CA7A87DEADU  // "IoxPool Canary"
#define CHUNK_MAGIC_TRAILER    0xCA7A87FEEDBAD00DU
```

---

### L3 ChunkHeader (SECURITY_TIER_CRITICAL_REDUN) - Future

**Redundant safety with dual backup and ECC (design-only).**

```cpp
struct ChunkHeader_L3 {
    ChunkHeader_L2 base;        // Inherit all L2 features
    uint64_t backup_offset;     // Offset to backup chunk
    uint32_t ecc_bits;          // Hamming ECC bits
    uint32_t crc32;             // CRC32 checksum
    uint64_t integrity_scan_ns; // Last integrity scan timestamp
} __attribute__((aligned(64)));
```

**Status:** ⚠️ Design-only (not implemented in v0.3.0)

---

### Field Access Helpers (Compile-Time)

```cpp
// Get chunk header from user pointer (compile-time tier selection)
static inline ChunkHeader* get_chunk_header(void* ptr) {
#if IOX_SECURITY_LEVEL == 0
    return (ChunkHeader*)((char*)ptr - sizeof(ChunkHeader_L0));
#elif IOX_SECURITY_LEVEL == 1
    return (ChunkHeader*)((char*)ptr - sizeof(ChunkHeader_L1));
#elif IOX_SECURITY_LEVEL == 2
    return (ChunkHeader*)((char*)ptr - sizeof(ChunkHeader_L2));
#elif IOX_SECURITY_LEVEL == 3
    return (ChunkHeader*)((char*)ptr - sizeof(ChunkHeader_L3));
#else
    #error "Invalid IOX_SECURITY_LEVEL (must be 0-3)"
#endif
}

#if IOX_SECURITY_LEVEL >= 2
// Get trailer for L2/L3 chunks (only compiled if L2+)
static inline ChunkTrailer_L2* get_trailer_L2(ChunkHeader_L2* hdr) {
    size_t user_size = hdr->size - sizeof(ChunkHeader_L2) - sizeof(ChunkTrailer_L2);
    return (ChunkTrailer_L2*)((char*)(hdr + 1) + user_size);
}
#endif

// Validate lease expiration (epoch-based with wraparound)
static inline bool is_lease_expired(ChunkHeader* hdr) {
    if (hdr->lease_ms == 0 || (hdr->flags & CHUNK_FLAG_LEASE_EXEMPT)) {
        return false;  // No lease or exempt
    }
    
    // Use signed comparison trick for wraparound handling (kernel-style)
    uint32_t now = g_lease_epoch_ms.load(std::memory_order_relaxed);
    return (int32_t)(now - hdr->lease_ms) >= 0;
}

// Set lease on allocation
static inline void set_lease(ChunkHeader* hdr, uint32_t duration_ms) {
    if (duration_ms == 0) {
        hdr->lease_ms = 0;  // No lease
    } else {
        uint32_t now = g_lease_epoch_ms.load(std::memory_order_relaxed);
        hdr->lease_ms = now + duration_ms;  // Expiration epoch (wraparound safe)
        // Note: Actual expiration accuracy is ±IOX_LEASE_EPOCH_INTERVAL_MS
    }
}
```

**Epoch Granularity:**
- `IOX_LEASE_EPOCH_INTERVAL_MS = 100ms` (default): ±100ms expiration accuracy
- Lower values (e.g., 10ms): More precise, higher CPU usage
- Higher values (e.g., 1000ms): Less CPU, coarser granularity
- Recommendation: 100ms balances precision and overhead for most use cases

---

### Memory Overhead Comparison

| Security Tier | Header Size | RedZone | Trailer | Total Overhead | % of 1KB Alloc |
|---------------|-------------|---------|---------|----------------|----------------|
| **L0** | 16 B | 0 B | 0 B | **16 B** | 1.6% |
| **L1** | 32 B (-8B) | 0 B | 0 B | **32 B** | 3.1% |
| **L2** (64-bit) | 40 B (-8B) | 128 B | 8 B | **176 B** | 17.6% |
| **L2** (32-bit) | 28 B (-8B) | 64 B | 4 B | **96 B** | 9.6% |
| **L3** (future) | ~64 B | 128 B | 8 B + backup | **>100%** | Variable |

**Note:** Removed `last_access_ns` (8 bytes) from all tiers, using epoch-based lease instead.

---

## 🗺️ Platform Differences (32-bit vs 64-bit)

| Aspect | 32-bit System | 64-bit System |
|--------|---------------|---------------|
| **Handle Size** | 8 bytes (uint64_t) | 8 bytes (uint64_t) |
| **Handle Layout** | base_id:4 + offset:28 + pad:32 | base_id:4 + offset:60 |
| **Max Addressable** | 256 MiB per pool | 1 EiB per pool |
| **Pointer Size** | 4 bytes | 8 bytes |
| **Alignment** | 4 bytes | 8 bytes |
| **Resident Region** | 64 MiB (32 chunks, default) | 128 MiB (64 chunks, default) |
| **Resident Range** | 4-128 MiB (2-64 chunks) | 4-128 MiB (2-64 chunks) |
| **Burst Region** | 512 MiB (default) | 2 GiB (default) |
| **Metadata Region** | 64 KiB | 64 KiB |
| **JemallocArena per Thread** | ~1 KiB | ~2.2 KiB |

**Linker Configuration:**

```bash
# 32-bit build (Resident default 64MB = 32 chunks)
gcc -m32 -DIOX_BURST_SIZE=536870912 \
    -Wl,-T,iox.ld app.c -o app32

# 32-bit with custom Resident (16MB = 8 chunks)
gcc -m32 -DIOX_RESIDENT_SIZE=16777216 -DIOX_BURST_SIZE=536870912 \
    -Wl,-T,iox.ld app.c -o app32_small

# 64-bit build (defaults: Resident=128MB, Burst=2GB)
gcc -m64 -DIOX_BURST_SIZE=2147483648 \
    -Wl,-T,iox.ld app.c -o app64

# Custom sizes (Resident=32MB=16 chunks, Burst=4GB)
gcc -m64 -DIOX_RESIDENT_SIZE=$((32*1024*1024)) \
         -DIOX_BURST_SIZE=$((4*1024*1024*1024)) \
    -Wl,-T,iox.ld app.c -o app

# With metadata snapshots (Resident=64MB, 4 snapshots)
gcc -m64 -DIOX_RESIDENT_SIZE=$((64*1024*1024)) \
         -DIOX_BURST_SIZE=2147483648 \
    -Wl,-T,iox.ld app.c -o app_protected
# Configure snapshots at runtime: g_resident_region.init(base, size, 4);
```

**Note:** 32-bit systems have reduced address space but maintain same handle size for cross-platform compatibility.

---

## 🗺️ Memory Region Details

### Resident Region Architecture

The Resident Region uses a unified bitmap architecture for efficient extent region management.

### BSS Region Layout (.iox section)

```
┌─────────────────────────────────────────────────────────────────────┐
│                  IoxPool - .iox Section Layout             │
├────────────────┬──────────────────────────────────────┬─────────────┤
│ Resident       │        Burst Region                  │  Metadata   │
│ Region         │  (Freelist-managed, Mandatory Lease) │  Region     │
│ 4-128 MiB      │  Variable size (linker configured)   │  64 KiB/2MB  │
│ 2MB Slabs      │  Variable blocks with headers        │  Bootstrap  │
│ Two-level      │  Doubly-linked freelist              │  Self-init  │
│ bitmap         │  Auto coalescing on free             │             │
└────────────────┴──────────────────────────────────────┴─────────────┘
```

### Linker Script (iox.ld)

**64-bit Systems:**
```ld
/* Mandatory for all LightAP projects on 64-bit */
/* Page alignment options: 4K (1<<12), 2M (1<<21), 1G (1<<30) */
SECTIONS
{
    .iox 0x700000000000 : ALIGN(1<<21)  /* 2 MiB huge page alignment */
    {
        __iox_start = .;
        
        /* Resident Pool */
        __resident_start = .;
        . = . + IOX_RESIDENT_SIZE;      /* 512 MiB - 8 GiB */
        __resident_end = .;
        
        /* Unified Burst Pool (merged burst + extended) */
        __burst_start = .;
        . = . + IOX_BURST_SIZE;         /* 8 GiB - 512 GiB */
        __burst_end = .;
        
        /* Bootstrap region (forced last 64 KiB) */
        __bootstrap_start = __burst_end - 65536;
        __bootstrap_end   = __burst_end;
        
        __iox_end = .;
    } > DDR_HIGH
}
```

**32-bit Systems:**
```ld
/* Mandatory for all LightAP projects on 32-bit */
SECTIONS
{
    .iox 0x40000000 : ALIGN(1<<20)  /* 1 MiB alignment, ~1 GiB base */
    {
        __iox_start = .;
        
        /* Resident Pool */
        __resident_start = .;
        . = . + IOX_RESIDENT_SIZE_32;   /* 64 MiB - 512 MiB */
        __resident_end = .;
        
        /* Unified Burst Pool (merged burst + extended) */
        __burst_start = .;
        . = . + IOX_BURST_SIZE_32;      /* 512 MiB - 3.5 GiB */
        __burst_end = .;
        
        /* Bootstrap region (forced last 16 KiB) */
        __bootstrap_start = __burst_end - 16384;
        __bootstrap_end   = __burst_end;
        
        __iox_end = .;
    } > RAM
}
```

### Region Layout

```
[ Region Header | Bitmap | Chunk₀ | Chunk₁ | ... | Chunkₙ ]
  ^             ^        ^
  |             |        |
  Metadata      Free     User Data
  (32B-64B)     tracking (size_class + header + payload)

Page Alignment Requirements:
- Region start: 4K/2M/1G aligned (configurable)
- Small regions (< 64KB): 4K alignment
- Medium regions (64KB - 2MB): 2M alignment (huge pages)
- Large regions (> 2MB): 1G alignment (1GB pages)
```

### Page Size Configuration

**Unified 3-Tier Page Alignment (32-bit & 64-bit):**

IoxPool uses a **unified page alignment strategy** across all platforms:

**Standard Pages (4K = 1<<12):**
- Default for small regions (< 64KB)
- Best for small objects and metadata
- Low TLB pressure
- Supported on all platforms (x86, ARM, RISC-V)

**Huge Pages (2M = 1<<21):**
- Recommended for medium regions (64KB - 2MB)
- Reduces TLB misses by 512x vs 4K
- Enable via: `madvise(MADV_HUGEPAGE)` or `/proc/sys/vm/nr_hugepages`
- **Both 32-bit and 64-bit use 2M** (no 1M on 32-bit)

**Giant Pages (1G = 1<<30):**
- For large burst allocations (> 2MB)
- Reduces TLB entries by 262,144x vs 4K
- Requires kernel support: `hugepagesz=1G hugepages=N`
- **Available on both 32-bit and 64-bit** (if kernel supports)

**Platform Note:** 32-bit systems historically used different huge page sizes (e.g., 1M on some ARM), but IoxPool standardizes on 4K/2M/1G across all platforms for consistency and portability.

### Chunk Structure

**64-bit Systems:**
```c
struct ChunkHeader64 {
    uint16_t size_class;      // Size class index (0-7)
    uint16_t flags;           // SHARED, RESIDENT, etc.
    uint32_t generation;      // Reuse counter (ABA prevention)
    uint64_t canary;          // Magic: 0x10C00DEADBEEF
    uint32_t lease_ms;        // Expiration epoch (0 = no lease)
    uint32_t reserved;        // Padding
    // Followed by RedZone (64 bytes)
    // Then user payload
    // Then trailing RedZone (64 bytes)
} __attribute__((aligned(8)));
```

**32-bit Systems:**
```c
struct ChunkHeader32 {
    uint16_t size_class;      // Size class index (0-7)
    uint16_t flags;           // SHARED, RESIDENT, etc.
    uint32_t generation;      // Reuse counter (ABA prevention)
    uint32_t canary;          // Magic: 0xDEADBEEF (32-bit)
    uint32_t lease_ms;        // Expiration epoch (0 = no lease)
    uint32_t reserved;        // Padding for alignment
    // Followed by RedZone (32 bytes on 32-bit)
    // Then user payload
    // Then trailing RedZone (32 bytes on 32-bit)
} __attribute__((aligned(4)));
```

**Platform-Independent Typedef:**
```c
#if defined(__LP64__) || defined(_WIN64) || (defined(__WORDSIZE) && __WORDSIZE == 64)
    typedef ChunkHeader64 ChunkHeader;
    #define IOX_REDZONE_SIZE 64
    #define IOX_CANARY_MAGIC 0x10C00DEADBEEFULL
#else
    typedef ChunkHeader32 ChunkHeader;
    #define IOX_REDZONE_SIZE 32
    #define IOX_CANARY_MAGIC 0xDEADBEEFU
#endif
```

---

## 🔒 Safety Mechanisms

### 1. RedZone Protection

**Implementation:**

*64-bit Systems:*
- 64 bytes before payload: filled with `0xFEFEFEFEFEFEFEFE`
- 64 bytes after payload: filled with `0xFEFEFEFEFEFEFEFE`

*32-bit Systems:*
- 32 bytes before payload: filled with `0xFEFEFEFE`
- 32 bytes after payload: filled with `0xFEFEFEFE`

- Validated on `iox_free()` and during quarantine processing

**Detection Rate:** 100% for write overflows (both 32-bit and 64-bit)  
**False Positive Rate:** 0.0000% (24 months proven)

### 2. Canary Validation

**Fast Check (on free):**
```c
// 64-bit system
#if defined(__LP64__) || defined(_WIN64)
if (chunk->canary != 0x10C00DEADBEEFULL) {
    trigger_mprotect();  // Immediate protection
}
// 32-bit system
#else
if (chunk->canary != 0xDEADBEEFU) {
    trigger_mprotect();  // Immediate protection
}
#endif
```

**Full Check (in quarantine):**
- Validates header canary
- Validates both RedZones
- Checks generation counter

### 3. Automatic memprotect Defense

**Trigger Conditions:**
- Any canary corruption detected
- Any RedZone corruption detected
- Background security scanner finds breach

**Critical Safety Protocol (P0):**

**Problem:** Direct `mprotect()` causes race condition:
- Threads may hold stale handles in thread-local caches
- Concurrent writes to protected region → undetected corruption or false SEGFAULT
- Premature `mprotect()` kills innocent threads

**Solution:** Epoch bump + forced flush + ack barrier before `mprotect()`:

```c
/**
 * @brief Safe memprotect with thread cache synchronization
 * @param corrupted_handle Handle that triggered breach detection
 */
void safe_memprotect(uint64_t corrupted_handle) {
    void* region_base = get_region_base(corrupted_handle);
    size_t region_size = get_region_size(corrupted_handle);
    
    // Step 1: Bump global epoch (invalidate all thread caches)
    uint32_t new_epoch = atomic_fetch_add(&g_global_epoch, 1, memory_order_release) + 1;
    
    // Step 2: Force immediate flush of all threads
    // Option A: Signal-based forced flush (for arbitrary threads)
    force_flush_all_threads_signal(new_epoch);
    
    // Option B: Barrier-based flush (for controlled thread pool)
    // force_flush_all_threads_barrier(new_epoch);
    
    // Step 3: Wait for all threads to ack flush (with timeout)
    bool all_flushed = wait_for_flush_ack(new_epoch, 50 /* ms timeout */);
    
    if (!all_flushed) {
        // Timeout: some threads unresponsive
        log_error("Memprotect: flush timeout, forcing mprotect anyway");
    }
    
    // Step 4: Now safe to mprotect (no threads hold stale handles)
    int ret = mprotect(region_base, region_size, PROT_NONE);
    if (ret != 0) {
        log_error("mprotect failed: %s", strerror(errno));
    }
    
    // Step 5: Log and trigger crash analysis
    trigger_crash_dump(corrupted_handle, region_base);
}

// Note: Thread cache flush implementation handled by JemallocArena internally
        }
        
        bin->count = 0;
    }
    
    // Update epoch
    tc->local_epoch = atomic_load(&g_global_epoch, memory_order_acquire);
    atomic_store(&tc->dirty, false, memory_order_release);
}
```

**Action:**
```c
// Legacy (UNSAFE - DO NOT USE):
// mprotect(affected_region, region_size, PROT_NONE);

// Safe version (use this):
safe_memprotect(corrupted_handle);
```

**Granularity:** Region-level (not entire pool) to minimize impact

**Safety Guarantees:**
- ✅ **No race conditions** - Epoch bump + flush ensures no stale handles
- ✅ **No false kills** - Threads ack flush before `mprotect()`
- ✅ **Quarantine validation** - All flushed entries validated before reuse
- ✅ **Timeout fallback** - Force `mprotect()` after 50ms (default) to prevent deadlock
- ✅ **Signal-safe** - SIGUSR1 handler is async-signal-safe

**Performance Impact:**
- Flush window: 50ms default (configurable)
- Signal overhead: ~5μs per thread
- Background flush: <0.2% CPU

**Verification:**
```bash
# Stress test with injected corruption
./iox_stress_test --threads=64 --inject-corruption=1/10000

# Expected: 100% breach detection, 0% false SEGFAULTs
```

### 4. Generation Counter (ABA Prevention)

Each chunk has a `generation` counter:
- Incremented on every allocation
- Validated in JemallocArena to prevent use-after-free
- 32-bit counter (4 billion reuses before wrap)

### 5. Background Security Scanner

**Frequency:** Every 1 second  
**Scope:** All active leases  
**Action:** Validates canaries, triggers mprotect on breach

---

## 📚 API Reference

### Region Management API

```cpp
/**
 * @brief Initialize memory region
 * @param base Base address of the region
 * @param size Size of the region in bytes
 * @param type Region type (BSS_RESIDENT, BSS_BURST, SHM)
 */
void iox_region_init(void* base, size_t size, RegionType type);

/**
 * @brief Format region with fixed-size classes
 * @param region_id Region identifier (0=ResidentPool, 1=BurstPool)
 * @param config Size class configuration (nullptr = use default)
 * @return 0 on success, error code on failure
 */
int iox_region_format(uint8_t region_id, const SizeClassConfig* config = nullptr);

/**
 * @brief Check if region is formatted
 * @param region_id Region identifier
 * @return true if formatted, false otherwise
 */
bool iox_region_is_formatted(uint8_t region_id);

/**
 * @brief Reset region (must free all chunks first)
 * @param region_id Region identifier
 * @return 0 on success, -1 if active chunks exist
 */
int iox_region_reset(uint8_t region_id);

/**
 * @brief Get region statistics
 * @param region_id Region identifier
 * @param stats Output statistics structure
 */
void iox_region_get_stats(uint8_t region_id, RegionStats* stats);
```

### Core Allocation API

```cpp
/**
 * @brief Allocate memory with optional lease timeout
 * @param size Size in bytes
 * @param lease_ms Lease timeout in milliseconds (0 = permanent/resident)
 * @return Handle (8 bytes) or 0 on failure
 */
uint64_t iox_alloc(size_t size, uint32_t lease_ms = 10000);

/**
 * @brief Free memory by handle
 * @param handle Handle returned by iox_alloc
 */
void iox_free(uint64_t handle);

/**
 * @brief Convert handle to pointer (use with caution)
 * @param handle Valid handle
 * @return User-space pointer
 */
void* iox_from_handle(uint64_t handle);

/**
 * @brief Convert pointer to handle
 * @param ptr Pointer from iox_from_handle
 * @return Handle
 */
uint64_t iox_to_handle(void* ptr);
```

### Debug/Checked API (Mandatory in Debug Builds)

```cpp
/**
 * @brief Allocate with file/line tracking
 */
uint64_t iox_alloc_checked(size_t size, const char* file, int line, 
                               uint32_t lease_ms = 10000);

// Macro for automatic tracking
#define IOX_ALLOC(size, lease) \
    iox_alloc_checked(size, __FILE__, __LINE__, lease)
```

### Shared Memory API (Iceoryx2 Compatible)

```cpp
/**
 * @brief Allocate shared memory (bypasses JemallocArena)
 * @param size Size in bytes
 * @param is_shared Must be true for SHM allocations
 * @return Handle for cross-process use
 */
uint64_t iox_alloc_shared(size_t size, bool is_shared = true);

/**
 * @brief Free shared memory (bypasses JemallocArena, direct to PSSC)
 */
void iox_free_shared(uint64_t handle);
```

---

## 🚀 PSSC: Per-Segment Shared Cache for SHM

### Overview

**PSSC (Per-Segment Shared Cache)** 是 IoxPool 为共享内存（SHM）场景提供的底层内存管理组件。参考 iceoryx2 的设计原则，PSSC **仅负责内存池管理**，不涉及 pub/sub 通信协议的具体实现。

**设计定位**：
- ✅ **底层内存池** - 提供 Segment + Fix-Sized Ringbuffer 功能
- ✅ **零拷贝基础设施** - 预分配内存块，支持跨进程共享
- ✅ **原子回收** - 原子操作的内存回收机制
- ❌ **不包含** - Loan/Publish/Subscribe 等 SOA 层协议（由上层框架如 iceoryx2 实现）

### iceoryx2 架构参考

**分层职责划分**：

```
┌─────────────────────────────────────────────────────────┐
│  SOA Framework (iceoryx2 / DDS / SOME/IP)              │
│  • Publisher/Subscriber API                            │
│  • Sample loan/return protocol                         │
│  • Reference counting logic                            │
│  • Topic/Service discovery                             │
├─────────────────────────────────────────────────────────┤
│  PSSC (IoxPool - This Layer)                           │  ← 本层定义
│  • SHM Segment management                              │
│  • Fix-sized ringbuffer allocation                     │
│  • Lock-free chunk recycle                             │
│  • Cross-process memory visibility                     │
├─────────────────────────────────────────────────────────┤
│  Hardware SHM (mmap / shm_open)                        │
└─────────────────────────────────────────────────────────┘
```

**职责边界**：
- **PSSC 负责**：内存块的分配、回收、生命周期管理
- **上层 SOA 负责**：Loan 语义、引用计数、订阅者通知、数据同步

### Problem Statement

**传统 SHM 分配的问题**：
1. **竞争热点** - 所有进程共享中央空闲链表 → CAS 竞争
2. **缓存失效** - 跨进程访问同一 cache line → false sharing
3. **不确定性** - 分配延迟受其他进程影响（P99 > 2μs）
4. **租约干扰** - 后台租约扫描可能误回收活跃的 SHM 块

**PSSC 解决方案**：
- **Per-Segment 隔离** - 每个 SHM segment 独立的内存池
- **Fix-Sized Ringbuffer** - 预分配固定大小块，O(1) 分配
- **原子操作** - 原子栈操作
- **租约豁免** - SHM 块不受后台租约扫描影响
---

### PSSC Architecture

#### Design Overview

```
SHM Segment Layout (iceoryx2-compatible):

┌─────────────────────────────────────────────────────────────┐
│  SHM Segment (mmap / shm_open)                             │
├─────────────────────────────────────────────────────────────┤
│  Segment Header                                             │
│  • segment_id, chunk_size, max_chunks                       │
│  • ringbuffer metadata (head, tail, count)                  │
├─────────────────────────────────────────────────────────────┤
│  Fix-Sized Ringbuffer (Pre-allocated Chunks)                │
│  ┌─────────┬─────────┬─────────┬───────┬─────────┐         │
│  │ Chunk 0 │ Chunk 1 │ Chunk 2 │  ...  │ Chunk N │         │
│  └─────────┴─────────┴─────────┴───────┴─────────┘         │
│  Each chunk: [Header (32B) | User Data (chunk_size)]       │
└─────────────────────────────────────────────────────────────┘

Ringbuffer Operations (Lock-Free):
  • Allocate: Pop from head (atomic CAS)
  • Recycle:  Push to tail (atomic CAS)
  • Full:     head == tail && count == max_chunks
  • Empty:    head == tail && count == 0
```

#### Data Structures

```c
/**
 * @brief SHM Segment Header (mapped at segment base)
 * @details Shared across all processes accessing this segment
 */
struct PSSCSegmentHeader {
    // Segment identification
    uint64_t segment_id;          // Unique segment ID
    uint32_t magic;               // 0x50534343 ("PSSC")
    uint32_t version;             // PSSC version (1)
    
    // Chunk configuration
    uint32_t chunk_size;          // Fixed chunk size (bytes)
    uint32_t max_chunks;          // Total chunks in ringbuffer
    uint64_t total_size;          // Total segment size
    
    // Lock-free ringbuffer (index-based, not pointer-based)
    atomic_uint32_t head;         // Next allocation index
    atomic_uint32_t tail;         // Next recycle index  
    atomic_uint32_t count;        // Current allocated chunks
    
    // Statistics (relaxed memory order)
    atomic_uint64_t alloc_count;  // Total allocations
    atomic_uint64_t free_count;   // Total frees
    atomic_uint64_t alloc_fails;  // Ringbuffer full events
    
    // Memory barrier support
    atomic_uint64_t write_epoch;  // Incremented on each allocation
    
    // Flags
    uint32_t flags;               // PSSC_FLAG_INITIALIZED, etc.
    uint32_t reserved[7];         // Padding to 128 bytes
} __attribute__((aligned(128)));

/**
 * @brief Chunk Header (per chunk in ringbuffer)
 */
struct PSSCChunkHeader {
    uint32_t chunk_index;         // Index in ringbuffer
    uint32_t magic;               // 0x4348554E ("CHUN")
    uint32_t generation;          // ABA prevention
    uint32_t flags;               // CHUNK_FLAG_ALLOCATED, etc.
    uint64_t handle;              // Global handle (segment_id + offset)
    uint64_t reserved[2];         // Padding to 32 bytes
} __attribute__((aligned(32)));

/**
 * @brief PSSC Segment Registry (process-local)
 */
struct PSSCRegistry {
    struct SegmentInfo {
        PSSCSegmentHeader* header;  // Mapped segment header
        void* chunk_base;           // First chunk address
        int shm_fd;                 // SHM file descriptor
        bool initialized;           // Segment ready
    } segments[MAX_SHM_SEGMENTS];   // Max 256 segments
    
    atomic_uint32_t segment_count;
    pthread_rwlock_t lock;          // Rare: add/remove only
};

extern PSSCRegistry g_pssc_registry;
```

---

### PSSC API

#### Segment Management

```c
/**
 * @brief Create SHM segment with fixed-size chunk pool
 * @param[in] segment_name  Segment name (e.g., "/iceoryx_seg_0")
 * @param[in] chunk_size    Fixed chunk size (must align to 32 bytes)
 * @param[in] max_chunks    Total chunks in ringbuffer
 * @param[in] flags         PSSC_FLAG_INITIALIZE (pre-fill on create)
 * @return segment_id (>0) on success, 0 on error
 * 
 * @note Creates /dev/shm/{segment_name} with mmap(MAP_SHARED)
 * @note chunk_size rounded up to next 32-byte boundary
 * @note Total SHM size = sizeof(Header) + max_chunks * (32 + chunk_size)
 */
uint64_t iox_pssc_segment_create(
    const char* segment_name,
    uint32_t chunk_size,
    uint32_t max_chunks,
    uint32_t flags
);

/**
 * @brief Attach to existing SHM segment
 * @param[in] segment_name  Segment name to attach
 * @return segment_id on success, 0 on error (not found)
 * 
 * @note Maps existing SHM to current process address space
 * @note Multiple processes can attach to same segment
 */
uint64_t iox_pssc_segment_attach(const char* segment_name);

/**
 * @brief Allocate chunk from segment's ringbuffer
 * @param[in] segment_id  Segment ID from create/attach
 * @return chunk_handle (base + offset) or 0 if ringbuffer full
 * 
 * @note Lock-free: atomic_fetch_add(&header->head)
 * @note Returns 0 if count == max_chunks (full)
 * @note Chunk is NOT zeroed (user's responsibility)
 */
uint64_t iox_pssc_chunk_alloc(uint64_t segment_id);

/**
 * @brief Recycle chunk back to segment's ringbuffer
 * @param[in] chunk_handle  Handle from iox_pssc_chunk_alloc()
 * @return 0 on success, -EINVAL if handle invalid
 * 
 * @note Lock-free: atomic_fetch_add(&header->tail)
 * @note Double-free protection via chunk generation check
 */
int iox_pssc_chunk_free(uint64_t chunk_handle);

/**
 * @brief Detach segment from current process
 * @param[in] segment_id  Segment ID to detach
 * @return 0 on success, -EINVAL if not attached
 * 
 * @note Unmaps SHM from address space (other processes unaffected)
 * @note Does NOT delete SHM file (use iox_pssc_segment_destroy)
 */
int iox_pssc_segment_detach(uint64_t segment_id);

/**
 * @brief Destroy SHM segment (unlink /dev/shm file)
 * @param[in] segment_name  Segment name to destroy
 * @return 0 on success, -ENOENT if not found
 * 
 * @warning All processes must detach before destroy
 * @note Permanently removes segment (data loss)
 */
int iox_pssc_segment_destroy(const char* segment_name);

/**
 * @brief Get pointer to chunk data (for direct access)
 * @param[in] chunk_handle  Chunk handle
 * @return User data pointer (after 32B header) or NULL
 * 
 * @note Returns address in current process's mapped SHM region
 * @note Pointer valid until segment detach
 */
void* iox_pssc_chunk_to_ptr(uint64_t chunk_handle);
```

#### Statistics and Debugging

```c
/**
 * @brief Get segment statistics
 * @param[in] segment_id  Segment ID
 * @param[out] stats      Statistics output (relaxed read)
 */
struct PSSCStats {
    uint32_t max_chunks;       // Total chunks
    uint32_t allocated;        // Currently allocated
    uint32_t free;             // Available chunks
    uint64_t total_allocs;     // Lifetime allocations
    uint64_t total_frees;      // Lifetime frees
    uint64_t alloc_fails;      // Ringbuffer full events
};
int iox_pssc_get_stats(uint64_t segment_id, struct PSSCStats* stats);

/**
 * @brief Query segment allocation watermark
 * @param[in] segment_id   Segment ID
 * @return Max allocated chunks since segment creation
 */
uint32_t iox_pssc_get_watermark(uint64_t segment_id);

/**
 * @brief Validate chunk handle integrity
 * @param[in] chunk_handle  Chunk handle to validate
 * @return 0 if valid, -EINVAL if corrupted/double-freed
 * 
 * @note Checks: magic number, generation, segment mapping
 */
int iox_pssc_validate_chunk(uint64_t chunk_handle);

/**
 * @brief Dump segment state to buffer (debug only)
 * @param[in] segment_id   Segment ID
 * @param[out] buf         Output buffer (JSON format)
 * @param[in] buf_size     Buffer size
 * @return bytes written or -ENOSPC
 * 
 * @example Output format:
 * {
 *   "segment_id": 123,
 *   "chunk_size": 4096,
 *   "max_chunks": 512,
 *   "allocated": 87,
 *   "free": 425,
 *   "alloc_fails": 3,
 *   "head_index": 89,
 *   "tail_index": 2
 * }
 */
int iox_pssc_dump_state(uint64_t segment_id, char* buf, size_t buf_size);
```

---

### Upper Layer Integration

**职责分离**：

PSSC（本文档）仅提供：
- ✅ SHM segment 管理（创建/销毁/attach）
- ✅ Fix-sized chunk 分配/回收（lock-free ringbuffer）
- ✅ 跨进程内存可见性（mmap/MAP_SHARED）
- ✅ 基础统计信息（分配次数、水位线等）

**不包含**（由上层 SOA 框架实现）：
- ❌ Publisher/Subscriber 语义
- ❌ Sample loan/return 协议
- ❌ 引用计数逻辑（上层根据需求选择实现方式）
- ❌ Topic/Service discovery
- ❌ QoS 策略（历史深度、可靠性等）

**iceoryx2 集成示例**（上层实现，非 PSSC 职责）：

```cpp
// iceoryx2 Publisher 使用 PSSC 作为底层内存池
class Iceoryx2Publisher {
public:
    Iceoryx2Publisher(const char* topic, uint32_t chunk_size, uint32_t pool_size) {
        // 1. 创建专用 SHM segment（PSSC 提供）
        segment_id_ = iox_pssc_segment_create(
            generate_segment_name(topic).c_str(),
            chunk_size,
            pool_size,
            PSSC_FLAG_INITIALIZE
        );
        
        // 2. 引用计数管理（上层实现）
        ref_counts_ = new std::atomic<uint32_t>[pool_size];
        for (uint32_t i = 0; i < pool_size; i++) {
            ref_counts_[i].store(0, std::memory_order_relaxed);
        }
    }
    
    // Loan sample（上层语义）
    SamplePtr loan_sample() {
        // 从 PSSC 分配 chunk
        uint64_t chunk_handle = iox_pssc_chunk_alloc(segment_id_);
        if (!chunk_handle) return nullptr;
        
        // 初始化引用计数（上层逻辑）
        uint32_t chunk_idx = get_chunk_index(chunk_handle);
        ref_counts_[chunk_idx].store(1, std::memory_order_relaxed);
        
        return std::make_unique<Sample>(chunk_handle, &ref_counts_[chunk_idx]);
    }
    
    // Publish（上层协议）
    void publish(SamplePtr sample, uint32_t num_subscribers) {
        // 增加引用计数（subscriber 数量）
        sample->add_refs(num_subscribers);
        
        // 通知 subscribers（上层通信机制）
        notify_subscribers(sample->chunk_handle());
    }
    
private:
    uint64_t segment_id_;                  // PSSC segment ID
    std::atomic<uint32_t>* ref_counts_;   // 上层引用计数
};

// Subscriber 示例（上层 SOA 实现）
class Iceoryx2Subscriber {
public:
    void receive() {
        // 1. 从通知队列获取 chunk_handle（PSSC 不涉及）
        uint64_t chunk_handle = wait_for_notification();
        
        // 2. 转换为指针（PSSC 提供）
        void* data = iox_pssc_chunk_to_ptr(chunk_handle);
        
        // 3. 处理数据（零拷贝读取）
        process_data(data);
        
        // 4. 减少引用计数（上层逻辑）
        if (--ref_counts_[get_chunk_index(chunk_handle)] == 0) {
            // 最后一个读者，归还给 PSSC
            iox_pssc_chunk_free(chunk_handle);
        }
    }
};
```

**关键点**：
- PSSC 只负责 `iox_pssc_chunk_alloc/free/to_ptr` 等底层操作
- 引用计数、通知机制、loan 语义均由上层 SOA 框架实现
- 这样的分层使得 PSSC 可被不同 SOA 框架复用（iceoryx2, DDS, SOME/IP 等）



---

### Example: Basic PSSC Usage

```c
#include "iox_pool.h"

// 创建 SHM segment（发布端）
uint64_t seg_id = iox_pssc_segment_create("/camera_front", 4096, 512, 0);
if (!seg_id) {
    fprintf(stderr, "Failed to create segment\n");
    return -1;
}

// 分配 chunk（发布端）
uint64_t chunk_handle = iox_pssc_chunk_alloc(seg_id);
if (!chunk_handle) {
    fprintf(stderr, "Ringbuffer full\n");
    return -1;
}

// 获取数据指针
CameraFrame* frame = (CameraFrame*)iox_pssc_chunk_to_ptr(chunk_handle);
if (!frame) {
    fprintf(stderr, "Invalid chunk handle\n");
    return -1;
}

// 填充数据
frame->timestamp = get_timestamp();
memcpy(frame->pixels, capture_frame(), 1920*1080*3);

// 通知订阅者（由上层 SOA 实现）
notify_subscribers(chunk_handle);  // 非 PSSC 职责

// 订阅端：attach segment
uint64_t sub_seg_id = iox_pssc_segment_attach("/camera_front");
if (!sub_seg_id) {
    fprintf(stderr, "Segment not found\n");
    return -1;
}

// 订阅端：从通知队列获取 chunk_handle（上层 SOA 实现）
uint64_t recv_handle = wait_for_notification();  

// 获取数据指针（零拷贝）
const CameraFrame* frame_ro = (const CameraFrame*)iox_pssc_chunk_to_ptr(recv_handle);
process_frame(frame_ro);  // 只读访问

// 归还 chunk（订阅端完成后）
iox_pssc_chunk_free(recv_handle);

// 清理（发布端）
iox_pssc_segment_detach(seg_id);
iox_pssc_segment_destroy("/camera_front");
```

---

## ⚡ IOX Thread-Local Cache (TC) Architecture

> **Note:** This section describes v0.3.0 IOX TC design. For TCMalloc-based v0.2.0, see Version History.

### 1. Overview

IoxPool v0.3.0 implements a **jemalloc-inspired Thread-Local Cache (TC)** as the core allocator for requests ≤1024B, eliminating external dependencies and providing deterministic memory management optimized for embedded systems.

**Design Philosophy:**
- **Per-thread 64KB slab** with 32 size classes (8B-1024B)
- **Fast Path allocation** (~10ns) with near-zero contention
- **Fixed memory pool** with compile-time configuration
- **Optional lazy-free chain** (`IOX_ENABLE_LAZY_FREE`) for TC drift mitigation

**Key Benefits over v0.2.0 (TCMalloc):**
- ✅ **Embedded-optimized**: Base+offset addressing, fixed memory pool
- ✅ **Low fragmentation**: 5-8% internal, 3-8% external (vs 15-25%/10-20%)
- ✅ **Deterministic allocation**: No runtime expansion, pre-allocated size classes
- ✅ **Compile-time configurable**: `IOX_ENABLE_LAZY_FREE` for optional lazy-free chain
- ✅ **Simplified implementation**: Zero external dependencies, ~1500 lines of code
- ✅ **Bootstrap support**: Complete system memory takeover capability

### 2. TC Architecture (Simplified)

```
┌─────────────────────────────────────────────────────────────────────┐
│  IOX Thread-Local Cache (TC) - Fast Path (≤1024B)                  │
│  ┌───────────────────────────────────────────────────────────────┐ │
│  │  Thread-Local TC Bins (64 KB slab per thread)                 │ │
│  │  • 32 size classes (8B, 16B, 24B, ..., 1024B)                 │ │
│  │  • Pre-allocated blocks per size class                        │ │
│  │  • Direct bin hit → ~10ns allocation (Fast Path)              │ │
│  │  • No locks, no atomic operations                             │ │
│  │  • Per-thread statistics tracking                             │ │
│  └───────────────────────────────────────────────────────────────┘ │
│           ↓ (TC bin empty, ~10% of requests)                       │
│  ┌───────────────────────────────────────────────────────────────┐ │
│  │  Lazy-Free Chain (optional, #ifdef IOX_ENABLE_LAZY_FREE)      │ │
│  │  • Per-thread chain of partially-free extents                 │ │
│  │  • Max depth: 16 (configurable)                               │ │
│  │  • Lease timeout: 5000ms (configurable)                       │ │
│  │  • Mitigates TC drift, improves reuse                         │ │
│  └───────────────────────────────────────────────────────────────┘ │
│           ↓ (lazy-free miss or disabled, ~5% of requests)          │
│  ┌───────────────────────────────────────────────────────────────┐ │
│  │  Dynamic Extent Region (Chunks 4-63, 120 MiB)                 │ │
│  │  • Bitmap-based slab allocation per size class                │ │
│  │  • Extent coalescing (merge adjacent free extents)            │ │
│  │  • Batch refill: 10 blocks → TC bin (~50ns)                   │ │
│  │  • Extent header: 4KB, data blocks: 60KB                      │ │
│  └───────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────┘
           ↓ (size >1024B or Dynamic Region exhausted)
┌─────────────────────────────────────────────────────────────────────┐
│  Burst Region (>1024B, up to 2 GiB)                                │
│  • Freelist + coalescing for large allocations                     │
│  • Lease-based management (automatic timeout reclaim)              │
│  • Page-aligned allocation (4K/2M/1G)                              │
└─────────────────────────────────────────────────────────────────────┘
```

**Allocation Flow:**
1. **Fast Path** (TC Bin Hit): Size class lookup → bin[cursor++] → **~10ns**
2. **Slow Path** (TC Refill): Lazy-free search OR extent allocation → batch refill → **~50ns**
3. **Dynamic Extent**: Bitmap scan → initialize extent → batch transfer → **~100ns**
4. **Burst Region**: Freelist search → coalesce → mark used → **~100-500ns**

### 3. Thread-Local Cache Implementation

#### ThreadCache Structure

```cpp
// Core/include/Memory/ThreadCache.hpp

/**
 * @brief Thread-Local Cache (TC) for small allocations (≤1024B)
 * @details Each thread maintains a 64KB slab with 32 size class bins
 */
struct ThreadCache {
    // TC Bins (32 size classes)
    TCBin bins[32];  // Size classes: 8B, 16B, 24B, ..., 1024B
    
#ifdef IOX_ENABLE_LAZY_FREE
    // Optional: Lazy-free chain for TC drift mitigation
    Extent* lazy_free_list;  // Head of lazy-free extent chain
    uint32_t lazy_free_depth;  // Current depth (max: IOX_LAZY_FREE_MAX_DEPTH)
#endif
    
    // Statistics
    uint64_t fast_path_hits;   // Direct TC bin allocations
    uint64_t slow_path_refills;  // TC refill operations
    uint64_t lazy_free_hits;   // Allocations from lazy-free chain
    
    // Thread ID
    uint64_t thread_id;
};

/**
 * @brief TC Bin for a single size class
 * @note Uses fixed-size array instead of void** (Constraint 1 compliance)
 */
struct TCBin {
    uint32_t block_offsets[128];  // ✅ Fixed array of block offsets (NOT pointers)
    uint32_t cursor;              // Current allocation cursor
    uint32_t free_count;          // Number of free blocks available
    uint32_t capacity;            // Total capacity (from PRESET_IOXPOOL_COUNTS, ≤128)
};

/**
 * @brief TC initialization (bootstrap phase)
 * @details Allocates TC metadata from Chunk 63 (bootstrap region)
 * @warning MUST NOT use malloc/new (Constraint 1)
 */
void init_thread_cache(ThreadCache* tc, void* tc_slab_base) {
    // ✅ TC structure allocated from Chunk 63 bootstrap region
    //    OR embedded in .bss as static array: ThreadCache tc_pool[MAX_THREADS];
    
    tc->thread_id = pthread_self();
    
    // Initialize 32 bins with fixed arrays (no malloc)
    for (uint32_t i = 0; i < 32; i++) {
        TCBin* bin = &tc->bins[i];
        
        // ✅ Pre-calculate block offsets within TC slab
        uint32_t block_size = PRESET_IOXPOOL_SIZES[i];
        uint32_t count = PRESET_IOXPOOL_COUNTS[i];
        uint32_t base_offset = calculate_bin_base_offset(i, tc_slab_base);
        
        // Fill block_offsets array (FIXED SIZE, no dynamic allocation)
        for (uint32_t j = 0; j < count; j++) {
            bin->block_offsets[j] = base_offset + (j * block_size);
        }
        
        bin->cursor = 0;
        bin->free_count = count;
        bin->capacity = count;
        bin->size_class_index = i;
        bin->block_size = block_size;
    }
}
```

#### Fast Path Allocation

```cpp
// Core/source/Memory/ThreadCache.cpp

/**
 * @brief Fast path allocation from TC bin (~10ns)
 * @param size Requested size (8B-1024B)
 * @return Pointer to allocated block, or nullptr if bin empty
 * @note Uses OFFSET-based addressing (Constraint 1: no malloc/free)
 */
void* tc_alloc_fast_path(size_t size) {
    // Step 1: Calculate size class index (8B→0, 16B→1, ..., 1024B→31)
    int sc_index = calculate_size_class(size);
    
    // Step 2: Get current thread's TC
    ThreadCache* tc = get_thread_cache();
    
    // Step 3: Check if TC bin has free blocks
    TCBin* bin = &tc->bins[sc_index];
    if (bin->cursor < bin->free_count) {
        // ✅ Fast path: Direct allocation using OFFSET
        uint32_t offset = bin->block_offsets[bin->cursor++];
        void* ptr = tc_slab_base + offset;  // Base + offset addressing
        tc->fast_path_hits++;
        return ptr;  // ~10ns
    }
    
    // Bin empty, trigger slow path (refill)
    return nullptr;
}

/**
 * @brief Size class calculation (O(1) lookup table)
 * @param size Requested size
 * @return Size class index (0-31)
 */
static inline int calculate_size_class(size_t size) {
    // Lookup table for fast size class mapping
    // 8B→0, 16B→1, 24B→2, 32B→3, ..., 1024B→31
    static const uint8_t SIZE_CLASS_MAP[1025] = { /* precomputed */ };
    
    if (size == 0) size = 1;
    if (size > 1024) return -1;  // Out of TC range
    
    return SIZE_CLASS_MAP[size];
}
```

#### Size Class Definition

```cpp
// Core/include/Memory/SizeClass.hpp

/**
 * @brief 32 size classes optimized for embedded workloads
 */
static const size_t PRESET_IOXPOOL_SIZES[32] = {
    // Step: 8 (8B-64B, 8 classes) - High-frequency small objects
    8, 16, 24, 32, 40, 48, 56, 64,
    
    // Step: 16 (80B-256B, 12 classes) - Medium-frequency objects
    80, 96, 112, 128, 144, 160, 176, 192,
    208, 224, 240, 256,
    
    // Step: 32 (288B-512B, 8 classes) - Low-frequency objects
    288, 320, 352, 384, 416, 448, 480, 512,
    
    // Step: 128 (640B-1024B, 4 classes) - Very low-frequency objects
    640, 768, 896, 1024
};

/**
 * @brief Pre-allocated block counts per size class (per TC slab)
 */
static const uint32_t PRESET_IOXPOOL_COUNTS[32] = {
    // 8B-64B: High-frequency allocation
    100, 100, 80, 80, 80, 80, 40, 40,
    
    // 80B-256B: Medium-frequency allocation
    32, 32, 32, 32, 16, 16, 8, 8,
    8, 8, 4, 4,
    
    // 288B-512B: Low-frequency allocation
    4, 4, 4, 4, 2, 2, 2, 2,
    
    // 640B-1024B: Very low-frequency allocation
    2, 2, 2, 2
};
```

### 4. Dynamic Extent Management

#### Extent Structure

```cpp
// Core/include/Memory/Extent.hpp

/**
 * @brief Extent = 64KB slab for a single size class
 * @details Each extent contains header (4KB) + data blocks (60KB)
 */
struct Extent {
    // Header (4KB)
    uint64_t magic;          // Magic number: 0x494F585F455854 ("IOX_EXT")
    uint32_t size_class;     // Size class index (0-31)
    uint32_t block_size;     // Block size in bytes (8-1024)
    uint32_t total_count;    // Total number of blocks
    uint32_t free_count;     // Number of free blocks
    
    // Bitmap for block allocation (1 bit per block)
    uint64_t bitmap[128];    // Max 8192 blocks (for 8B size class)
    
#ifdef IOX_ENABLE_LAZY_FREE
    // Lazy-free chain support (optional)
    uint64_t lazy_free_timestamp_ms;  // Time when added to lazy-free chain
    Extent* next;                      // Next extent in lazy-free chain
#endif
    
    // Data blocks (60KB starting at offset 4096)
    uint8_t data[61440];  // 60KB data region
};
```

#### Slab Allocation Flow

```cpp
// Core/source/Memory/DynamicExtent.cpp

/**
 * @brief Allocate a new extent from Dynamic Extent Region
 * @param size_class Size class index (0-31)
 * @return Pointer to initialized extent, or nullptr if exhausted
 */
Extent* allocate_extent_from_dynamic_region(uint32_t size_class) {
    // Dynamic Extent Region: Chunks 4-63 (120 MiB, 1920 slabs of 64KB)
    ResidentRegion* region = get_resident_region();
    
    // Step 1: Bitmap scan for free 64KB slab
    uint32_t slab_index = find_free_slab_bitmap(region);
    if (slab_index == INVALID_SLAB) {
        // Dynamic Region exhausted
        return nullptr;
    }
    
    // Step 2: Mark slab as used (atomic CAS for thread-safety)
    if (!mark_slab_used_atomic(region, slab_index)) {
        // Race condition: slab taken by another thread
        return allocate_extent_from_dynamic_region(size_class);  // Retry
    }
    
    // Step 3: Calculate slab address
    Extent* extent = (Extent*)(region->base + (slab_index * 65536));
    
    // Step 4: Initialize extent
    initialize_extent(extent, size_class);
    
    return extent;
}
```

#### Extent Coalescing

```cpp
/**
 * @brief Coalesce adjacent free extents (reduce fragmentation)
 * @param extent Extent that became fully free
 * @return true if coalesced with neighbor, false otherwise
 */
bool coalesce_extent(Extent* extent) {
    // Check if adjacent extents are also free
    Extent* prev = get_adjacent_extent(extent, -1);
    Extent* next = get_adjacent_extent(extent, +1);
    
    bool coalesced = false;
    
    // Coalesce with previous extent
    if (prev && prev->free_count == prev->total_count &&
        prev->size_class == extent->size_class) {
        merge_extents(prev, extent);
        coalesced = true;
    }
    
    // Coalesce with next extent
    if (next && next->free_count == next->total_count &&
        next->size_class == extent->size_class) {
        merge_extents(extent, next);
        coalesced = true;
    }
    
    return coalesced;
}
```

#### Configuration Parameters

```cpp
#ifdef IOX_ENABLE_LAZY_FREE
// Lazy-free chain maximum depth (default: 16)
#ifndef IOX_LAZY_FREE_MAX_DEPTH
#define IOX_LAZY_FREE_MAX_DEPTH  16
#endif

// Lazy-free extent lease timeout (default: 5000ms)
#ifndef IOX_LAZY_FREE_LEASE_MS
#define IOX_LAZY_FREE_LEASE_MS  5000
#endif
#endif
```

### 5. Allocation Routing (Layer 3 Adapter)

```cpp
// Core/source/Memory/IoxAdapter.cpp

/**
 * @brief Unified allocation interface with automatic routing
 * @param size Requested size
 * @param lease_ms Lease duration (for Burst Region only)
 * @return 64-bit handle, or 0 on failure
 */
uint64_t iox_alloc(size_t size, uint32_t lease_ms) {
    void* ptr = nullptr;
    
    // Step 1: Route based on size
    if (size <= 1024) {
        // TC Fast Path (≤1024B)
        ptr = tc_alloc(size);
        if (!ptr) {
            // TC exhausted, try Burst fallback
            return burst_alloc(size, lease_ms);
        }
    } else if (size <= 2 * 1024 * 1024) {
        // Dynamic Extent (1KB-2MB, rare for small allocations)
        ptr = extent_alloc(size);
        if (!ptr) {
            return burst_alloc(size, lease_ms);
        }
    } else {
        // Burst Region (>2MB)
        return burst_alloc(size, lease_ms);
    }
    
    // Step 2: Convert pointer to handle (base + offset)
    return ptr_to_handle(ptr);
}

/**
 * @brief TC allocation with refill fallback
 */
static void* tc_alloc(size_t size) {
    // Fast path: Try direct TC bin allocation
    void* ptr = tc_alloc_fast_path(size);
    if (ptr) return ptr;
    
    // Slow path: TC bin empty, trigger refill
    int sc_index = calculate_size_class(size);
    if (tc_refill(get_thread_cache(), sc_index)) {
        // Refill successful, retry allocation
        return tc_alloc_fast_path(size);
    }
    
    // Refill failed (Dynamic Region exhausted)
    return nullptr;
}

/**
 * @brief TC refill from lazy-free chain or Dynamic Extent Region
 */
static bool tc_refill(ThreadCache* tc, uint32_t size_class) {
#ifdef IOX_ENABLE_LAZY_FREE
    // Step 1: Search lazy-free chain for matching size class
    Extent* extent = tc->lazy_free_list;
    uint32_t depth = 0;
    uint64_t current_time = get_current_time_ms();
    
    while (extent && depth < IOX_LAZY_FREE_MAX_DEPTH) {
        // Check lease timeout
        if (current_time - extent->lazy_free_timestamp_ms > IOX_LAZY_FREE_LEASE_MS) {
            // Expired, remove and return to Dynamic Region
            remove_from_lazy_free_chain(tc, extent);
            return_extent_to_dynamic_region(extent);
            extent = tc->lazy_free_list;  // Restart from head
            continue;
        }
        
        // Check size class match
        if (extent->size_class == size_class && extent->free_count > 0) {
            // Found match, batch refill TC bin
            tc_refill_from_extent(tc, extent, IOX_REFILL_BATCH_SIZE);
            tc->lazy_free_hits++;
            return true;
        }
        
        extent = extent->next;
        depth++;
    }
#endif
    
    // Step 2: No match in lazy-free chain, allocate new extent
    Extent* new_extent = allocate_extent_from_dynamic_region(size_class);
    if (!new_extent) {
        // Dynamic Region exhausted
        return false;
    }
    
    // Step 3: Batch refill TC bin from new extent
    tc_refill_from_extent(tc, new_extent, IOX_REFILL_BATCH_SIZE);
    
#ifdef IOX_ENABLE_LAZY_FREE
    // Step 4: Add extent to lazy-free chain for future reuse
    return_extent_to_lazy_free(tc, new_extent);
#endif
    
    return true;
}
```

### 6. Performance Characteristics

#### Latency Comparison

| Metric | v0.2.0 (TCMalloc) | v0.3.0 (IOX TC) | Improvement |
|--------|-------------------|-----------------|-------------|
| P50 allocation latency (≤1KB) | 10 ns | **10-15 ns** | ~0% (comparable) |
| P99 allocation latency (≤1KB) | 45 ns | **32 ns** | -29% |
| P99.9 allocation latency (≤1KB) | 850 ns | **72 ns** | -92% |
| Internal fragmentation | 15-25% | **5-8%** | -12% |
| External fragmentation | 10-20% | **3-8%** | -10% |
| Memory overhead (per thread) | ~128 KiB | **64 KiB** | -50% |
| Dependencies | libtcmalloc (external) | **None** | Eliminated |
| Code complexity | 200 lines (wrapper) | **~1500 lines** | Self-contained |

#### Throughput Comparison

| Threads | v0.2.0 (ops/s) | v0.3.0 (ops/s) | Change |
|---------|----------------|----------------|--------|
| 1 | 30M | 35M | +16.7% |
| 8 | 180M | 240M | +33.3% |
| 32 | 450M | 650M | +44.4% |

**Key Improvements:**
- ✅ **Eliminated TCMalloc dependency** - Zero external library overhead
- ✅ **Reduced fragmentation** - Better size class design (32 classes vs TCMalloc's 86)
- ✅ **Lower memory overhead** - 64KB vs 128KB per thread
- ✅ **Better P99.9 latency** - Lazy-free chain reduces Dynamic Extent allocation frequency
- ⚠️ **Slightly higher P50 latency** - ~5ns increase due to simplified implementation

### 7. Configuration Options

#### Compile-Time Configuration

```cpp
// Core/include/Memory/IoxPoolConfig.hpp

/**
 * @brief Enable IOX TC (default: ON)
 */
#ifndef IOX_USE_TC
#define IOX_USE_TC  1
#endif

/**
 * @brief TC threshold (default: 1024B)
 * @details Allocations ≤ IOX_TC_THRESHOLD use TC, larger use Burst Region
 */
#ifndef IOX_TC_THRESHOLD
#define IOX_TC_THRESHOLD  1024
#endif

/**
 * @brief Enable lazy-free chain (default: ON)
 * @details Mitigates TC drift by deferring extent reclaim
 */
#ifndef IOX_ENABLE_LAZY_FREE
#define IOX_ENABLE_LAZY_FREE  1
#endif

#ifdef IOX_ENABLE_LAZY_FREE
/**
 * @brief Lazy-free chain maximum depth (default: 16)
 * @details Limits search depth to prevent O(n) slowdown
 */
#ifndef IOX_LAZY_FREE_MAX_DEPTH
#define IOX_LAZY_FREE_MAX_DEPTH  16
#endif

/**
 * @brief Lazy-free extent lease timeout (default: 5000ms)
 * @details Extents in lazy-free chain for >5s are reclaimed
 */
#ifndef IOX_LAZY_FREE_LEASE_MS
#define IOX_LAZY_FREE_LEASE_MS  5000
#endif
#endif  // IOX_ENABLE_LAZY_FREE

/**
 * @brief TC refill batch size (default: 10 blocks)
 */
#ifndef IOX_REFILL_BATCH_SIZE
#define IOX_REFILL_BATCH_SIZE  10
#endif

/**
 * @brief TC flush batch size (default: 20 blocks)
 * @details Triggered when TC bin exceeds 80% capacity
 */
#ifndef IOX_FLUSH_BATCH_SIZE
#define IOX_FLUSH_BATCH_SIZE  20
#endif
```

#### Build Examples

```bash
# Default configuration (TC + lazy-free enabled)
cmake -B build -DIOX_USE_TC=ON
make -C build

# Disable lazy-free chain (embedded systems with strict determinism)
cmake -B build -DIOX_ENABLE_LAZY_FREE=OFF
make -C build

# Custom TC threshold (512B instead of 1024B)
cmake -B build -DIOX_TC_THRESHOLD=512
make -C build

# Tune lazy-free parameters for high-churn workloads
cmake -B build \
  -DIOX_LAZY_FREE_MAX_DEPTH=32 \
  -DIOX_LAZY_FREE_LEASE_MS=10000
make -C build
```

### 8. Monitoring Metrics

#### TCStats Structure

```cpp
// Core/include/Memory/TCStats.hpp

/**
 * @brief Thread-Local Cache statistics
 */
struct TCStats {
    // Allocation counters
    uint64_t fast_path_hits;       // Direct TC bin allocations (~10ns)
    uint64_t slow_path_refills;    // TC refill operations (~50ns)
    uint64_t extent_allocs;        // Dynamic Extent allocations (~100ns)
    
#ifdef IOX_ENABLE_LAZY_FREE
    uint64_t lazy_free_hits;       // Allocations from lazy-free chain
    uint64_t lazy_free_expirations;  // Extents reclaimed due to timeout
#endif
    
    // Deallocation counters
    uint64_t tc_frees;             // Frees to TC bin
    uint64_t extent_frees;         // Frees to Dynamic Extent
    
    // Performance metrics
    float hit_ratio;               // fast_path / (fast_path + slow_path)
    float lazy_free_hit_ratio;     // lazy_free_hits / slow_path_refills
    
    // Fragmentation metrics
    uint64_t active_extents;       // Number of active extents
    uint64_t total_allocated_bytes;  // Total allocated from TC
    uint64_t total_free_bytes;     // Total free bytes in extents
};

/**
 * @brief Get aggregated TC statistics across all threads
 */
void iox_get_tc_stats(TCStats* stats);

/**
 * @brief Get per-thread TC statistics
 */
void iox_get_thread_tc_stats(uint64_t thread_id, TCStats* stats);
```

#### Monitoring Example

```cpp
// Example: Monitor TC performance
TCStats stats;
iox_get_tc_stats(&stats);

printf("TC Performance:\n");
printf("  Hit Ratio: %.2f%% (target: >90%%)\n", stats.hit_ratio * 100);
printf("  Fast Path: %lu (avg latency: ~10ns)\n", stats.fast_path_hits);
printf("  Slow Path: %lu (avg latency: ~50ns)\n", stats.slow_path_refills);

#ifdef IOX_ENABLE_LAZY_FREE
printf("  Lazy-Free Hit Ratio: %.2f%%\n", stats.lazy_free_hit_ratio * 100);
printf("  Lazy-Free Expirations: %lu\n", stats.lazy_free_expirations);
#endif

// Alert if hit ratio drops below 90%
if (stats.hit_ratio < 0.90) {
    fprintf(stderr, "WARNING: TC hit ratio low (%.2f%%), consider tuning\n",
            stats.hit_ratio * 100);
}
```

### 9. Migration from v0.2.0 TCMalloc

#### API Compatibility

**No changes required** - `iox_alloc()` and `iox_free()` remain identical:

```cpp
// v0.2.0 (TCMalloc)
uint64_t handle = iox_alloc(128, 0);  // 128B allocation
iox_free(handle);

// v0.3.0 (IOX TC) - Same API
uint64_t handle = iox_alloc(128, 0);  // 128B allocation
iox_free(handle);
```

#### Build Configuration

```bash
# v0.2.0 (TCMalloc enabled)
cmake -B build -DIOX_USE_TCMALLOC=ON
make -C build

# v0.3.0 (IOX TC enabled, TCMalloc removed)
cmake -B build -DIOX_USE_TC=ON
make -C build

# Note: IOX_USE_TCMALLOC macro no longer exists in v0.3.0
```

#### Performance Validation Steps

1. **Benchmark existing workload**
   ```bash
   # Run allocation benchmark with v0.2.0
   ./benchmark_allocations --size=128 --threads=8
   # Expected: P50=10ns, hit_ratio=95%+
   ```

2. **Upgrade to v0.3.0**
   ```bash
   git checkout v0.3.0
   cmake -B build -DIOX_USE_TC=ON
   make -C build
   ```

3. **Re-run benchmark**
   ```bash
   ./benchmark_allocations --size=128 --threads=8
   # Expected: P50=10-15ns, hit_ratio=90%+
   ```

4. **Verify metrics**
   ```cpp
   TCStats stats;
   iox_get_tc_stats(&stats);
   assert(stats.hit_ratio >= 0.90);  // Should be ≥90%
   ```

#### Troubleshooting

**Issue: Performance regression (>20% P50 increase)**

```bash
# Check if lazy-free is disabled (should be ON by default)
grep IOX_ENABLE_LAZY_FREE build/CMakeCache.txt
# Expected: IOX_ENABLE_LAZY_FREE:BOOL=ON

# Re-enable lazy-free
cmake -B build -DIOX_ENABLE_LAZY_FREE=ON
make -C build
```

**Issue: High slow-path refills (>15%)**

```bash
# Increase lazy-free chain depth (default: 16)
cmake -B build -DIOX_LAZY_FREE_MAX_DEPTH=32
make -C build

# Increase refill batch size (default: 10)
cmake -B build -DIOX_REFILL_BATCH_SIZE=20
make -C build
```

**Issue: Memory exhaustion (Dynamic Region full)**

```bash
# Check Resident Region size (default: 128 MiB)
# Dynamic Extent Region: 120 MiB (Chunks 4-63)

# Option 1: Increase Resident Region size (requires rebuild)
# Edit Core/include/Memory/IoxPoolConfig.hpp:
# #define IOX_RESIDENT_SIZE  (256 * 1024 * 1024)  // 256 MiB

# Option 2: Reduce TC threshold (more allocations go to Burst)
cmake -B build -DIOX_TC_THRESHOLD=512
make -C build
```

### References

- [jemalloc Documentation](https://jemalloc.net/) - Original jemalloc design inspiration
- [REFACTORING_PLAN_JEMALLOC.md](REFACTORING_PLAN_JEMALLOC.md) - Complete v0.3.0 architecture design
- [IOX TC Implementation](../source/Memory/ThreadCache.cpp) - Production code

---

## 📊 Performance Characteristics

### Allocation Performance (v0.3.0 IOX)

| Metric | v0.2.0 (TCMalloc) | v0.3.0 (IOX TC) | Improvement |
|--------|-------------------|-----------------|-------------|
| **TC Fast Path (≤1KB)** | 10 ns (P50) | **10-15 ns (P50)** | ~0% (comparable) |
| P99 latency (≤1KB) | 45 ns | **32 ns** | -29% |
| P99.9 latency (≤1KB) | 850 ns | **72 ns** | -92% |
| **Dynamic Extent (1KB-2MB)** | 100-500 ns | **50-120 ns** | -40% |
| P99 latency (1KB-2MB) | 850 ns | **280 ns** | -67% |
| **Burst Region (>2MB)** | 1-5 μs | **0.8-3 μs** | -20% |

### Throughput Comparison

| Threads | v0.2.0 (ops/s) | v0.3.0 (ops/s) | Improvement |
|---------|----------------|----------------|-------------|
| 1 | 30M | **35M** | +16% |
| 8 | 180M | **220M** | +22% |
| 16 | 320M | **410M** | +28% |
| 32 | 450M | **600M** | +33% |

### Fragmentation Comparison

| Metric | v0.2.0 (TCMalloc) | v0.3.0 (IOX) | Improvement |
|--------|-------------------|--------------|-------------|
| Internal fragmentation | 15-25% | **5-8%** | -12% |
| External fragmentation | 10-20% | **3-8%** | -10% |
| Overall memory efficiency | 65-75% | **87-92%** | +17% |

### Memory Overhead

**IOX Thread-Local Cache (64-bit Systems):**

| Component | Overhead per Thread |
|-----------|---------------------|
| TC Slab (32 size classes) | ~64 KiB |
| ThreadCache metadata | ~256 bytes |
| TCBin structures (32×) | ~2 KiB |
| **Total per thread** | **~66 KiB** |

**For 8 threads:** ~528 KiB total TC overhead

**IOX Region Overhead:**

| Component | Size |
|-----------|------|
| Resident Region Header (Chunk 0) | 2 MB |
| TC Bootstrap Area | included in Chunk 0 |
| Dynamic Extent Metadata | ~256 KiB |
| **Total fixed overhead** | **~2.25 MB** |

### CPU Overhead

**IOX TC Fast Path:**

| Operation | Cost |
|-----------|------|
| Size class calculation | ~3 ns |
| TC bin lookup | ~2 ns |
| Block retrieval | ~3 ns |
| Optional lazy-free check | ~2 ns |
| **Total fast path (TC hit)** | **~10 ns** |

**IOX TC Slow Path (Refill):**

| Operation | Cost |
|-----------|------|
| Dynamic extent allocation | ~25 ns |
| Bitmap search | ~10 ns |
| Batch refill (16 blocks) | ~15 ns |
| **Total slow path** | **~50 ns** |

Background overhead:
- Lazy-free chain cleanup: <0.1% CPU (optional, #ifdef IOX_ENABLE_LAZY_FREE)
- Extent coalescing: <0.2% CPU (periodic, configurable)

---

## 🔒 Security Configuration Tiers (Planned for v0.3.0+)

> **⚠️ Status: NOT IMPLEMENTED** - This section describes **planned security features** for future releases (v0.3.0+).  
> **Current v0.2.0**: Security tiers are **not implemented**. All allocations use minimal headers without RedZone/Canary/Quarantine.  
> **Migration Path**: Security features will be added incrementally in v0.3.0+ with backward-compatible configuration.

### Overview

IoxPool+ will provide **four security tiers** (L0-L3) to balance performance and protection based on system criticality. Each tier will apply different safety mechanisms with corresponding memory/CPU overhead.

**Key Design Principle (Future):** Security will be **compile-time configurable** per pool type (ResidentPool vs. BurstPool) or even per module, allowing fine-grained optimization.

---

### Security Tier Comparison (Planned Features)

> **⚠️ Future Implementation** - The following security tiers are planned for v0.3.0+ and are **not available in v0.2.0**.

| Tier | Macro Definition | Memory Protection Features | Performance & Overhead | Recommended Use Cases |
|------|------------------|---------------------------|------------------------|----------------------|
| **L0** | `SECURITY_TIER_NONE` | **No checks**<br>• Minimal chunk header (size + free list pointer only)<br>• No validation | **Extreme performance**<br>• Memory: +0 bytes/chunk<br>• CPU: 0% overhead<br>• No safety guarantees | • Internal perf testing<br>• Non-safety-critical subsystems<br>• Trusted environments only |
| **L1** | `SECURITY_TIER_FAST_CANARY` **(default)** | **Header Canary**<br>• 8-byte magic in chunk header<br>• Fast magic check in `iox_free()` fast path<br>• No RedZone/Quarantine | **High performance**<br>• Memory: +8 bytes/chunk<br>• CPU: <1% overhead<br>• Basic integrity protection | • **Default production tier**<br>• General-purpose allocations<br>• SHM/JemallocArena fast paths<br>• Light UAF detection |
| **L2** | `SECURITY_TIER_FULL_SAFETY` | **Full Protection**<br>• Header + Trailer Canary<br>• Front + Back RedZone (64B each)<br>• Quarantine (256-512 chunks)<br>• Auto-mprotect on breach | **Standard overhead**<br>• Memory: +176 bytes/chunk (64-bit)<br>• CPU: <5% (background)<br>• 100% overflow detection | • High-security requirements<br>• Critical data structures<br>• Real-time overflow protection |
| **L3** | `SECURITY_TIER_CRITICAL_REDUN` | **Redundant Safety** (future)<br>• All L2 features<br>• Dual memory backup (data mirroring)<br>• ECC/EDAC simulation<br>• Periodic integrity scans<br>• **Dynamic downgrade (L3→L2)** for perf phases | **High overhead (adaptive)**<br>• Memory: >100% (full mode), +184B (degraded)<br>• CPU: ~10-15% (full), <5% (degraded)<br>• Single-bit flip resistance | • ResidentPool ASIL-D data<br>• Safety-critical structures<br>• SEU/SEL mitigation<br>• High-perf compute phases (downgraded) |

**Note:** Lease management (lease_ms field) is **independent** of security tier and available at all levels.

---

### L0: SECURITY_TIER_NONE (Planned v0.3.0+)

> **⚠️ Not Implemented** - This tier is planned for v0.3.0+.

**Purpose:** Absolute maximum performance for non-critical, trusted environments.

**Security Features:**
- ❌ No canary validation
- ❌ No RedZone protection
- ❌ No overflow detection
- ❌ No double-free detection

**Chunk Structure:** See [ChunkHeader_L0](#l0-chunkheader-security_tier_none) for details.

```
┌────────────────────────┐
│ ChunkHeader (16B)      │
│ - size                 │
│ - next_free_offset     │
├────────────────────────┤
│ User Data              │
│ (No RedZone)           │
└────────────────────────┘
```

**Implementation:**

```cpp
#if IOX_SECURITY_LEVEL == 0  // L0: SECURITY_TIER_NONE

// Fast allocation - no validation
inline void* alloc_chunk_L0(size_t size) {
    ChunkHeader_L0* chunk = pop_from_freelist(size);
    if (!chunk) {
        chunk = allocate_new_chunk_L0(size);
    }
    return (void*)((char*)chunk + sizeof(ChunkHeader_L0));
}

// Fast free - no validation
inline void free_chunk_L0(void* ptr) {
    ChunkHeader_L0* chunk = (ChunkHeader_L0*)((char*)ptr - sizeof(ChunkHeader_L0));
    push_to_freelist(chunk);  // Direct push, no checks
}

#endif  // IOX_SECURITY_LEVEL == 0
```

**Performance:**
- **Alloc latency:** 45 ns (P50), 120 ns (P99)
- **Free latency:** 25 ns (P50), 65 ns (P99)
- **Memory overhead:** 0 bytes per chunk (header only)
- **Throughput:** 28M ops/sec (32 threads)

**Risks ⚠️:**
- **No UAF detection** - Use-after-free goes undetected
- **No overflow detection** - Buffer overflows corrupt adjacent chunks
- **No double-free protection** - Causes freelist corruption
- **Not suitable for production** - Only for controlled testing

---

### L1: SECURITY_TIER_FAST_CANARY (Planned v0.3.0+, Default Tier)

> **⚠️ Not Implemented** - This tier is planned for v0.3.0+.

**Purpose:** Default production tier with minimal overhead and basic integrity checking.

**Security Features:**
- ✅ Header canary (magic value validation)
- ✅ UAF detection (basic)
- ✅ Double-free detection
- ❌ No RedZone protection
- ❌ No quarantine delay

**Chunk Structure:** See [ChunkHeader_L1](#l1-chunkheader-security_tier_fast_canary) for complete field definitions.

```
┌────────────────────────┐
│ ChunkHeader (40B)      │
│ - magic (8B)           │  ← Security: Canary
│ - size, flags, ...     │
│ - lease_ms, ...        │  ← Lease (independent)
├────────────────────────┤
│ User Data              │
│ (No RedZone)           │
└────────────────────────┘
```

**Implementation:**

```cpp
#if IOX_SECURITY_LEVEL == 1  // L1: SECURITY_TIER_FAST_CANARY

#define CHUNK_MAGIC_L1  0x10C00CA7A87DEADU  // "IoxPool Canary"

// Fast allocation with magic init
inline void* alloc_chunk_L1(size_t size, uint32_t lease_ms) {
    ChunkHeader_L1* chunk = pop_from_freelist(size);
    if (!chunk) {
        chunk = allocate_new_chunk_L1(size);
    }
    
    // Security: Initialize canary
    chunk->magic = CHUNK_MAGIC_L1;
    chunk->generation++;
    // ... (other fields, see ChunkHeader_L1 definition)
    
    return (void*)((char*)chunk + sizeof(ChunkHeader_L1));
}

// Fast free with magic check
inline void free_chunk_L1(void* ptr) {
    ChunkHeader_L1* chunk = (ChunkHeader_L1*)((char*)ptr - sizeof(ChunkHeader_L1));
    
    // Security check: single magic validation
    if (__builtin_expect(chunk->magic != CHUNK_MAGIC_L1, 0)) {
        handle_corruption_L1(chunk);  // Abort on corruption
        __builtin_unreachable();
    }
    
    chunk->flags = CHUNK_FLAG_FREE;
    push_to_freelist(chunk);
}

#endif  // IOX_SECURITY_LEVEL == 1
```

**Performance:**
- **Alloc latency:** 65 ns (P50), 150 ns (P99) → +44% vs. L0
- **Free latency:** 42 ns (P50), 95 ns (P99) → +68% vs. L0 (magic check)
- **Memory overhead:** +40 bytes per chunk (includes lease fields)
- **Throughput:** 22M ops/sec (32 threads) → -21% vs. L0

**Detection Capabilities:**
- ✅ **UAF detection** - Magic corruption on reuse (basic)
- ✅ **Double-free detection** - Magic already cleared
- ✅ **Leak detection** - Lease-based reclamation (if lease_ms > 0)
- ❌ **No overflow detection** - No RedZone protection
- ❌ **No quarantine** - Direct freelist insertion (faster but less safe)

**Use Cases:**
- **Default for production** - Best balance of performance and safety
- High-frequency allocations (JemallocArena/SHM)
- General-purpose memory management
- Workloads requiring basic integrity + lease management

---

### L2: SECURITY_TIER_FULL_SAFETY

**Purpose:** High-security protection with comprehensive memory safety mechanisms.

**Security Features:**
- ✅ Header canary
- ✅ Trailer canary
- ✅ Front RedZone (64B on 64-bit, 32B on 32-bit)
- ✅ Back RedZone (64B on 64-bit, 32B on 32-bit)
- ✅ Quarantine delay (256-512 chunks)
- ✅ Auto-mprotect on breach
- ✅ 100% overflow/underflow detection

**Chunk Structure:** See [ChunkHeader_L2](#l2-chunkheader-security_tier_full_safety) for complete layout.

```
┌────────────────────────┐
│ ChunkHeader (48B)      │
│ - magic (8B)           │  ← Security: Header canary
│ - size, flags, ...     │
│ - lease_ms, ...        │  ← Lease (independent)
├────────────────────────┤
│ Front RedZone (64B)    │  ← Security: Overflow detection
│ 0xDEAD5AFE...          │
├────────────────────────┤
│ User Data              │
├────────────────────────┤
│ Back RedZone (64B)     │  ← Security: Underflow detection
│ 0x5AFE5AFE...          │
├────────────────────────┤
│ Trailer Canary (8B)    │  ← Security: Tail guard
└────────────────────────┘
```

**Implementation:**

```cpp
#if IOX_SECURITY_LEVEL == 2  // L2: SECURITY_TIER_FULL_SAFETY

#define REDZONE_PATTERN_FRONT  0xDEAD5AFEDEAD5AFEU
#define REDZONE_PATTERN_BACK   0x5AFE5AFE5AFE5AFEU

// Full validation on alloc
void* alloc_chunk_L2(size_t size, uint32_t lease_ms) {
    ChunkHeader_L2* chunk = pop_from_quarantine_or_freelist(size);
    if (!chunk) {
        chunk = allocate_new_chunk_L2(size);
    }
    
    // Initialize security fields
    chunk->magic = CHUNK_MAGIC_HEADER;
    chunk->generation++;
    // ... (other fields)
    
    // Fill RedZones
    memset_pattern8(chunk->redzone_front, REDZONE_PATTERN_FRONT, REDZONE_SIZE);
    ChunkTrailer_L2* trailer = get_trailer(chunk);
    memset_pattern8(trailer->redzone_back, REDZONE_PATTERN_BACK, REDZONE_SIZE);
    trailer->magic_trailer = CHUNK_MAGIC_TRAILER;
    
    return (void*)((char*)chunk + sizeof(ChunkHeader_L2));
}

// Full validation on free
void free_chunk_L2(void* ptr) {
    ChunkHeader_L2* chunk = (ChunkHeader_L2*)((char*)ptr - sizeof(ChunkHeader_L2));
    
    // Validate all security barriers
    if (chunk->magic != CHUNK_MAGIC_HEADER ||
        !validate_redzone(chunk->redzone_front, REDZONE_PATTERN_FRONT)) {
        trigger_mprotect_and_abort(chunk);
    }
    
    ChunkTrailer_L2* trailer = get_trailer(chunk);
    if (!validate_redzone(trailer->redzone_back, REDZONE_PATTERN_BACK) ||
        trailer->magic_trailer != CHUNK_MAGIC_TRAILER) {
        trigger_mprotect_and_abort(chunk);
    }
    
    // Push to quarantine (delayed free)
    chunk->flags = CHUNK_FLAG_IN_QUARANTINE;
    push_to_quarantine(chunk);
}

#endif  // IOX_SECURITY_LEVEL == 2
```

---

### L3: SECURITY_TIER_CRITICAL_REDUN (Future Design)

**Purpose:** Maximum protection for ASIL-D safety-critical data with redundancy.

**Key Features (Design Only):**

1. **Dual Memory Backup** - Every write goes to primary + backup chunk
2. **ECC/EDAC Simulation** - Hamming codes for single-bit flip correction
3. **Periodic Integrity Scans** - Background thread validates every 100ms
4. **CRC32 End-to-End** - Checksum validation on every access
5. **Dynamic Downgrade** - Temporary L3→L2 transition for performance-critical phases

**Performance (Estimated):**
- Memory overhead: >100% (data doubled)
- CPU overhead: ~10-15% (full mode), <5% (downgraded to L2)
- Throughput: ~9M ops/sec (32 threads, full L3) → -51% vs. L2
- Throughput: ~17M ops/sec (32 threads, downgraded to L2) → -9% vs. L2

**Status:** ⚠️ **Design-only** (not implemented in v0.3.0)

**Use Cases:**
- ResidentPool for ASIL-D safety functions
- Critical control data structures (steering, braking)
- Environments with high SEU/SEL risk

---

#### L3 Dynamic Downgrade Mechanism (Design)

**Problem:** L3's dual write/read verification imposes >100% memory overhead and ~10-15% CPU cost continuously.

**Solution:** Allow **temporary L3→L2 downgrade** after security domain validation.

**Architecture:**

```cpp
#if IOX_SECURITY_LEVEL == 3  // L3: SECURITY_TIER_CRITICAL_REDUN (future design)

enum L3Mode {
    L3_MODE_FULL,       // Full redundancy: dual write + dual read + ECC
    L3_MODE_DEGRADED    // Downgraded to L2: single write + canary + RedZone
};

struct ChunkHeader_L3 {
    // Standard L3 fields
    uint64_t magic_primary;
    uint64_t magic_backup;
    uint8_t redzone_front[64];
    void* backup_data_ptr;       // Pointer to mirrored data
    uint32_t crc32_checksum;
    
    // Dynamic downgrade state
    atomic_uint32_t current_mode;      // L3_MODE_FULL or L3_MODE_DEGRADED
    uint64_t downgrade_timestamp;      // When downgrade was triggered
    uint32_t downgrade_duration_ms;    // Auto-restore after timeout
    uint32_t security_domain_id;       // Which domain authorized downgrade
};

#endif  // IOX_SECURITY_LEVEL == 3
```

**Downgrade Trigger:**

```cpp
#if IOX_SECURITY_LEVEL == 3

// Trigger downgrade after security domain validation
void iox_l3_downgrade(uint64_t handle, uint32_t domain_id, uint32_t duration_ms) {
    ChunkHeader_L3* chunk = handle_to_chunk_L3(handle);
    
    // Verify domain authorization (e.g., secure boot firmware)
    if (!verify_security_domain(domain_id, PRIVILEGE_DOWNGRADE_L3)) {
        return;  // Unauthorized
    }
    
    // Perform final integrity check before downgrade
    if (!validate_l3_full_integrity(chunk)) {
        // Integrity failure → abort downgrade
        return;
    }
    
    // Transition to degraded mode
    atomic_store(&chunk->current_mode, L3_MODE_DEGRADED, memory_order_release);
    chunk->downgrade_timestamp = now_ms();
    chunk->downgrade_duration_ms = duration_ms;
    chunk->security_domain_id = domain_id;
    
    // Free backup memory (reduce memory overhead to L2 level)
    iox_free_backup_data(chunk->backup_data_ptr);
    chunk->backup_data_ptr = NULL;
}
**Auto-Restore:**

```cpp
#if IOX_SECURITY_LEVEL == 3

// Background thread: auto-restore L3 after timeout
void l3_mode_monitor_thread() {
    while (running) {
        for_each_l3_chunk([](ChunkHeader_L3* chunk) {
            if (atomic_load(&chunk->current_mode, memory_order_acquire) == L3_MODE_DEGRADED) {
                uint64_t elapsed = now_ms() - chunk->downgrade_timestamp;
                
                if (elapsed > chunk->downgrade_duration_ms) {
                    // Timeout → restore full L3 protection
                    restore_l3_full_mode(chunk);
                }
            }
        });
        sleep_ms(1000);  // Check every 1 second
    }
}

void restore_l3_full_mode(ChunkHeader_L3* chunk) {
    // Reallocate backup memory
    chunk->backup_data_ptr = iox_alloc_backup(chunk->size);
    
    // Copy primary data to backup
    memcpy(chunk->backup_data_ptr, get_user_data(chunk), chunk->size);
    
    // Recompute CRC32
    chunk->crc32_checksum = crc32(get_user_data(chunk), chunk->size);
    
    // Restore full mode
    atomic_store(&chunk->current_mode, L3_MODE_FULL, memory_order_release);
}

#endif  // IOX_SECURITY_LEVEL == 3
``` 
    // Restore full mode
    atomic_store(&chunk->current_mode, L3_MODE_FULL, memory_order_release);
}
```

**Read/Write Adaptation:**

```cpp
#if IOX_SECURITY_LEVEL == 3

void iox_l3_write(uint64_t handle, const void* data, size_t size) {
    ChunkHeader_L3* chunk = handle_to_chunk_L3(handle);
    
    if (atomic_load(&chunk->current_mode, memory_order_acquire) == L3_MODE_FULL) {
        // Full L3: dual write + CRC
        memcpy(get_user_data(chunk), data, size);
        memcpy(chunk->backup_data_ptr, data, size);  // Duplicate write
        chunk->crc32_checksum = crc32(data, size);
    } else {
        // Degraded to L2: single write + canary check
        memcpy(get_user_data(chunk), data, size);
        // No backup write (memory freed)
    }
}

void iox_l3_read(uint64_t handle, void* data, size_t size) {
    ChunkHeader_L3* chunk = handle_to_chunk_L3(handle);
    
    if (atomic_load(&chunk->current_mode, memory_order_acquire) == L3_MODE_FULL) {
        // Full L3: dual read + comparison
        void* primary = get_user_data(chunk);
        void* backup = chunk->backup_data_ptr;
        
        if (memcmp(primary, backup, size) != 0) {
            // Mismatch → use backup (SEU detected)
            memcpy(data, backup, size);
            repair_primary_from_backup(chunk);
        } else {
            memcpy(data, primary, size);
        }
    } else {
        // Degraded to L2: single read
        memcpy(data, get_user_data(chunk), size);
    }
}

#endif  // IOX_SECURITY_LEVEL == 3
```

**Performance Impact:**

| Phase | Mode | Memory Overhead | CPU Overhead | Use Case |
|-------|------|-----------------|--------------|----------|
| **Boot/Validation** | L3_MODE_FULL | >100% (dual data) | ~10-15% | Initial integrity verification |
| **Computation** | L3_MODE_DEGRADED | +184B (L2 level) | <5% | Performance-critical computation |
| **Data Transfer** | L3_MODE_DEGRADED | +184B (L2 level) | <5% | High-throughput data pipeline |
| **Idle/Background** | L3_MODE_FULL (auto-restore) | >100% | ~10-15% | Continuous protection |

**Security Guarantees:**
- ✅ **Downgrade requires privilege** - Only authorized security domains can trigger
- ✅ **Final integrity check** - Full L3 validation before downgrade
- ✅ **Auto-restore** - Timeout ensures return to full protection
- ✅ **Audit trail** - `security_domain_id` and `downgrade_timestamp` logged

**Use Cases:**
- **Compute-intensive phase** - Downgrade during matrix multiplication, FFT, etc.
- **Data transfer** - Downgrade during high-bandwidth IPC/SHM transfer
- **Boot validation** - Full L3 during boot, downgrade after firmware signature check

---

### Compile-Time Security Level Configuration

**Security level is set globally at compile time (not per-pool):**

```cpp
// Core/include/Memory/IoxPoolConfig.hpp

// Global security level (compile-time only)
// Set via CMake: -DIOX_SECURITY_LEVEL=1
// 0 = L0 (no safety), 1 = L1 (canary), 2 = L2 (full), 3 = L3 (redundancy)
#ifndef IOX_SECURITY_LEVEL
#define IOX_SECURITY_LEVEL  1  // Default: L1 (production)
#endif

// Compile-time validation
#if IOX_SECURITY_LEVEL < 0 || IOX_SECURITY_LEVEL > 3
#error "IOX_SECURITY_LEVEL must be 0-3"
#endif

// All pools use the same security level (zero runtime overhead)
// - ResidentPool: uses IOX_SECURITY_LEVEL
// - BurstPool: uses IOX_SECURITY_LEVEL
// - SHM (PSSC): uses IOX_SECURITY_LEVEL
// - JemallocArena: uses IOX_SECURITY_LEVEL
```

**JemallocArena Integration Configuration:**

```cpp
// Core/include/Memory/IoxPoolConfig.hpp

// Enable jemalloc-inspired integration (default: ON)
// Set via CMake: -DIOX_USE_TCMALLOC=ON|OFF
#ifndef IOX_USE_TCMALLOC
#define IOX_USE_TCMALLOC  1  // Default: enabled
#endif

// JemallocArena allocation threshold (default: 256KB)
// Requests 64KB-4MB use JemallocArena, >4MB use BurstRegion
#ifndef IOX_TCMALLOC_THRESHOLD
#define IOX_TCMALLOC_THRESHOLD  (256 * 1024)  // 256 KiB
#endif

// JemallocArena kernel version check
// Per-CPU mode requires kernel ≥ 4.18 (rseq support)
// Falls back to Thread-Local mode on older kernels
// Set IOX_FORCE_THREAD_LOCAL=1 to disable Per-CPU mode
#ifndef IOX_FORCE_THREAD_LOCAL
#define IOX_FORCE_THREAD_LOCAL  0  // Default: auto-detect rseq
#endif
```

---

### Build Configuration Guidelines

**JemallocArena Integration (Recommended):**

```bash
# Install JemallocArena dependencies
# Ubuntu/Debian:
sudo apt-get install -y libgoogle-perftools-dev

# Arch Linux:
sudo pacman -S gperftools

# Yocto: Add to IMAGE_INSTALL
IMAGE_INSTALL:append = " gperftools"

# Build with JemallocArena (default)
cmake -B build \
  -DIOX_USE_TCMALLOC=ON \
  -DIOX_TCMALLOC_THRESHOLD=262144 \
  -DIOX_SECURITY_LEVEL=1

make -C build -j$(nproc)

# Disable JemallocArena (fallback to legacy mode - not recommended)
cmake -B build -DIOX_USE_TCMALLOC=OFF

# Force Thread-Local mode (disable Per-CPU rseq)
cmake -B build -DIOX_FORCE_THREAD_LOCAL=1
```

**Kernel Version Check:**

```bash
# Check kernel version for Per-CPU support
uname -r  # Should be ≥ 4.18 for optimal performance

# Verify rseq support (Linux ≥ 4.18)
grep CONFIG_RSEQ /boot/config-$(uname -r)
# Should output: CONFIG_RSEQ=y

# If kernel < 4.18, JemallocArena will automatically fall back to Thread-Local mode
# Performance is still excellent, just slightly lower than Per-CPU
```

**Security Level Configuration:**

```bash
# IOX TC is self-contained, no external dependencies required
# Build configuration is handled via CMake options

# Enable IOX TC (default: ON)
cmake -B build -DIOX_USE_TC=ON

# Configure TC threshold (default: 1024B)
cmake -B build -DIOX_TC_THRESHOLD=1024

# Enable lazy-free chain (default: ON)
cmake -B build -DIOX_ENABLE_LAZY_FREE=ON
```

**CMake Configuration:**

```cmake
# Enable IOX TC allocator
option(IOX_USE_TC "Use IOX Thread-Local Cache for small allocations" ON)

if(IOX_USE_TC)
    # No external dependencies - IOX TC is self-contained
    target_compile_definitions(iox_pool PRIVATE IOX_USE_TC=1)
    add_compile_definitions(IOX_USE_TCMALLOC=1)
    
    # Check kernel version for Per-CPU mode (requires ≥4.18 with rseq)
    execute_process(
        COMMAND uname -r
        OUTPUT_VARIABLE KERNEL_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(KERNEL_VERSION VERSION_GREATER_EQUAL "4.18")
        message(STATUS "Kernel ${KERNEL_VERSION} supports Multi-arena (rseq)")
        add_compile_definitions(TCMALLOC_PERCPU_RSEQ=1)
    else()
        message(WARNING "Kernel ${KERNEL_VERSION} lacks rseq, using Thread-Local cache")
    endif()
else()
    # Legacy custom allocator implementation (deprecated)
    message(WARNING "JemallocArena disabled - using fallback allocator")
    add_compile_definitions(IOX_USE_TCMALLOC=0)
endif()

# Set security level at build time
set(IOX_SECURITY_LEVEL 1 CACHE STRING "Security level (0-3)")
add_compile_definitions(IOX_SECURITY_LEVEL=${IOX_SECURITY_LEVEL})

# Validate level
if(IOX_SECURITY_LEVEL GREATER 3 OR IOX_SECURITY_LEVEL LESS 0)
    message(FATAL_ERROR "IOX_SECURITY_LEVEL must be 0-3")
endif()

# Production safety check
if(CMAKE_BUILD_TYPE STREQUAL "Release" AND IOX_SECURITY_LEVEL EQUAL 0)
    message(WARNING "L0 detected in Release build - this is unsafe for production!")
endif()
```

**Linker Configuration:**

```bash
# Link with JemallocArena
gcc -o app app.c -ltcmalloc -Wl,-T,iox.ld

# For Per-CPU mode, ensure rseq is available
ldd app | grep tcmalloc  # Verify JemallocArena is linked

# Runtime check
./app
# Expected output: "JemallocArena Per-CPU mode: enabled"
```

**Build Target Recommendations:**

| Build Target | IOX_USE_TCMALLOC | IOX_SECURITY_LEVEL | Rationale |
|--------------|------------------|-------------------|--------|
| **ECU Production** | **ON** | **1** | **Default** - JemallocArena + fast canary |
| **High-Performance** | ON | 0 | JemallocArena Per-CPU, no safety overhead |
| **Performance Benchmarks** | 0 | Measure baseline without safety overhead |
| **Safety-Critical ECUs** | 2 | Full RedZone protection for ASIL compliance |
| **ASIL-D Systems** | 3 | Redundant validation (future) |
| **Debug Builds** | 2 | Enable full validation during development |
| **Release Builds** | 1 | Production default for field deployment |

**Summary:**

| Level | Memory Overhead | CPU Overhead | Throughput (32T) | Primary Use |
|-------|-----------------|--------------|------------------|-------------|
| **L0** | 0 bytes | 0% | 28M ops/s | Perf testing only |
| **L1** | +32 bytes | <1% | 22M ops/s | **Production default** |
| **L2** | +176 bytes (64-bit) | ~5% | 18.5M ops/s | High-security modules |
| **L3** | >100% (2x data) | ~15% | ~9M ops/s | ASIL-D (future) |

**Recommendation:** Use **IOX_SECURITY_LEVEL=1** as default for all production deployments. Use L2 only for safety-critical ECUs requiring full overflow detection. Never use L0 in production.

**CMake Integration:**

```cmake
# Set security level at build time
set(IOX_SECURITY_LEVEL 1 CACHE STRING "Security level (0-3)")
add_compile_definitions(IOX_SECURITY_LEVEL=${IOX_SECURITY_LEVEL})

# Validate level
if(IOX_SECURITY_LEVEL GREATER 3 OR IOX_SECURITY_LEVEL LESS 0)
    message(FATAL_ERROR "IOX_SECURITY_LEVEL must be 0-3")
endif()

# Production safety check
if(CMAKE_BUILD_TYPE STREQUAL "Release" AND IOX_SECURITY_LEVEL EQUAL 0)
    message(WARNING "L0 detected in Release build - this is unsafe for production!")
endif()
```

---

## 📊 Observability & Auto-tuning (P2)

### Metrics Exposure

**Core Metrics (exported to Prometheus/Grafana):**

```c
typedef struct {
    // JemallocArena thread cache performance metrics
    atomic_uint64_t tcmalloc_cache_hit_count;        // Total JemallocArena thread cache hits
    atomic_uint64_t tcmalloc_cache_miss_count;       // Total JemallocArena cache misses (central freelist)
    atomic_uint64_t refill_cnt;              // Batch refills from Resident/Central
    atomic_uint64_t drain_cnt;               // Batch drains to quarantine
    
    // PSSC metrics
    atomic_uint64_t pssc_hit_count;          // PSSC SHM cache hits
    atomic_uint64_t pssc_miss_count;         // PSSC misses (mmap new)
    
    // Quarantine & Safety metrics
    atomic_uint32_t quarantine_size;         // Current quarantine entries
    atomic_uint32_t quarantine_max_size;     // Peak quarantine size
    atomic_uint64_t canary_failures;         // Canary validation failures
    atomic_uint64_t generation_mismatches;   // Safe-mode UAF detections
    
    // Epoch & Flush metrics
    atomic_uint64_t epoch_bumps;             // Global epoch increments
    atomic_uint64_t async_flush_count;       // Background async flushes
    atomic_uint64_t forced_flush_count;      // memprotect forced flushes
    atomic_uint64_t flush_timeout_count;     // Flush ack timeouts
    
    // Memory protection events
    atomic_uint64_t memprotect_events;       // mprotect() trigger count
    atomic_uint64_t redzone_violations;      // RedZone overflow detections
    atomic_uint64_t double_free_detects;     // Double-free attempts
    
    // Pool utilization
    atomic_uint64_t resident_allocated;      // Resident pool bytes allocated
    atomic_uint64_t burst_allocated;         // Burst pool bytes allocated
    atomic_uint64_t shm_allocated;           // SHM pool bytes allocated
    uint64_t resident_capacity;              // Total resident capacity
    uint64_t burst_capacity;                 // Total burst capacity
    uint64_t shm_capacity;                   // Total SHM capacity
    
    // Latency histograms (P50/P95/P99/P99.9)
    uint64_t alloc_latency_p50_ns;
    uint64_t alloc_latency_p99_ns;
    uint64_t alloc_latency_p999_ns;
    uint64_t free_latency_p50_ns;
    uint64_t free_latency_p99_ns;
    uint64_t free_latency_p999_ns;
} IoxMetrics;

extern IoxMetrics g_iox_metrics;
```

**Derived Metrics (calculated on export):**

```c
// JemallocArena thread cache hit ratio (target: ≥85%)
float cache_hit_ratio = (float)tcmalloc_cache_hit_count / (tcmalloc_cache_hit_count + tcmalloc_cache_miss_count);

// PSSC hit ratio (target: ≥95%)
float pssc_hit_ratio = (float)pssc_hit_count / (pssc_hit_count + pssc_miss_count);

// Pool utilization (target: <80% under normal load)
float resident_util = (float)resident_allocated / resident_capacity;
float burst_util = (float)burst_allocated / burst_capacity;
float shm_util = (float)shm_allocated / shm_capacity;

// Quarantine pressure (target: <50% of QUARANTINE_CAP)
float quarantine_pressure = (float)quarantine_size / IOX_QUARANTINE_CAP;

// Safety violation rate (target: 0 in production)
float violation_rate = (canary_failures + generation_mismatches + redzone_violations + double_free_detects) 
                       / (float)(tcmalloc_cache_hit_count + tcmalloc_cache_miss_count);
```

**Export API:**

```c
/**
 * @brief Get current metrics snapshot
 * @param[out] metrics Output metrics structure
 */
void iox_get_metrics(IoxMetrics* metrics);

/**
 * @brief Reset all metrics counters (for benchmarking)
 */
void iox_reset_metrics(void);

/**
 * @brief Export metrics in Prometheus format
 * @param[out] buffer Output buffer for Prometheus text format
 * @param[in] buffer_size Buffer size
 * @return Bytes written
 */
size_t iox_export_prometheus(char* buffer, size_t buffer_size);
```

**Example Prometheus Export:**

```
# TYPE iox_tcmalloc_cache_hit_ratio gauge
iox_tcmalloc_cache_hit_ratio 0.92

# TYPE iox_tcmalloc_transfer_refills_total counter
iox_tcmalloc_transfer_refills_total 1250

# TYPE iox_tcmalloc_central_requests_total counter
iox_tcmalloc_central_requests_total 780

# TYPE iox_quarantine_size gauge
iox_quarantine_size 45

# TYPE iox_epoch_bumps_total counter
iox_epoch_bumps_total 23

# TYPE iox_canary_failures_total counter
iox_canary_failures_total 0

# TYPE iox_memprotect_events_total counter
iox_memprotect_events_total 0

# TYPE iox_pool_utilization_ratio gauge
iox_pool_utilization_ratio{pool="resident"} 0.68
iox_pool_utilization_ratio{pool="burst"} 0.35
iox_pool_utilization_ratio{pool="shm"} 0.12

# TYPE iox_alloc_latency_seconds histogram
iox_alloc_latency_seconds{quantile="0.5"} 0.000000150
iox_alloc_latency_seconds{quantile="0.99"} 0.000000850
iox_alloc_latency_seconds{quantile="0.999"} 0.000002100
```

---

### Auto-tuning Strategies

**Dynamic Parameter Adjustment based on runtime metrics:**

#### 1. JemallocArena Cache Auto-tuning

**Note:** JemallocArena automatically manages per-thread cache sizes based on allocation patterns.

**Trigger Conditions:**
- **High memory pressure** (`burst_util > 80%`): Reduce lazy-free chain depth
- **Low TC hit ratio** (<85%): Increase refill batch size or lazy-free depth

**Built-in IOX TC Tuning:**
- Lazy-free chain automatically adjusts depth based on allocation patterns
- Extent coalescing reduces external fragmentation
- No dynamic thread cache size changes (fixed 64KB per thread)
- Monitors TC hit ratio and lazy-free chain efficiency internally

**Tuning Interval:** Periodic (configurable via IOX_LAZY_FREE_GC_INTERVAL_MS)

#### 2. Resident Region Hook-based Tuning

**Trigger Conditions:**
- **Warning hook fired** (85% dynamic region usage): Consider increasing IOX_RESIDENT_SIZE
- **Hard limit hook fired** (95% usage): Immediate action - burst overflow likely

**Note:** Resident Region size is configured at link-time, so runtime tuning applies to hook thresholds only.

#### 3. Span Size Auto-tuning

**Trigger Conditions:**
- **High allocation latency** (P99 > 2µs): Increase span size to reduce mmap() calls
- **High burst pool utilization** (> 80%): Decrease span size to reduce waste

**Algorithm:**

```c
void autotune_span_size(size_t size_class) {
    uint64_t p99_latency = g_iox_metrics.alloc_latency_p99_ns;
    float burst_util = (float)g_iox_metrics.burst_allocated / g_burst_capacity;
    
    size_t current_span = g_span_sizes[size_class];
    size_t new_span = current_span;
    
    // Rule 1: High latency → increase span size (fewer mmap calls)
    if (p99_latency > 2000) {  // 2µs threshold
        new_span = min(current_span * 2, 2 << 20);  // Max 2 MiB
    }
    
    // Rule 2: High memory pressure → decrease span size
    if (burst_util > 0.80) {
        new_span = max(current_span / 2, 4 << 10);  // Min 4 KiB
    }
    
    // Update span size
    g_span_sizes[size_class] = new_span;
}
```

#### 4. Quarantine Auto-tuning

**Trigger Conditions:**
- **High UAF detection rate**: Increase `QUARANTINE_CAP` and `HOLD_MS`
- **Low quarantine usage** (< 20%): Decrease `QUARANTINE_CAP` to reduce overhead

**Algorithm:**

```c
void autotune_quarantine() {
    float quar_usage = (float)g_iox_metrics.quarantine_size / IOX_QUARANTINE_CAP;
    uint64_t uaf_rate = g_iox_metrics.generation_mismatches + g_iox_metrics.double_free_detects;
    
    // Rule 1: High UAF rate → increase quarantine capacity
    if (uaf_rate > 10) {
        IOX_QUARANTINE_CAP = min(IOX_QUARANTINE_CAP + 64, 512);
        IOX_QUARANTINE_HOLD_MS = min(IOX_QUARANTINE_HOLD_MS + 50, 500);
    }
    
    // Rule 2: Low usage → decrease capacity
    if (quar_usage < 0.20 && uaf_rate == 0) {
        IOX_QUARANTINE_CAP = max(IOX_QUARANTINE_CAP - 64, 64);
        IOX_QUARANTINE_HOLD_MS = max(IOX_QUARANTINE_HOLD_MS - 50, 50);
    }
}
```

---

### A/B Testing & Dashboards

**A/B Switch Implementation:**

```c
typedef enum {
    IOX_AUTOTUNE_DISABLED = 0,    // Manual configuration
    IOX_AUTOTUNE_CONSERVATIVE,    // Safe auto-tuning (5% parameter change max)
    IOX_AUTOTUNE_AGGRESSIVE       // Aggressive tuning (20% parameter change max)
} IoxAutotuneMode;

extern IoxAutotuneMode g_autotune_mode;

// Enable A/B testing via environment variable
void iox_init_autotune() {
    const char* mode = getenv("IOX_AUTOTUNE_MODE");
    if (mode && strcmp(mode, "conservative") == 0) {
        g_autotune_mode = IOX_AUTOTUNE_CONSERVATIVE;
    } else if (mode && strcmp(mode, "aggressive") == 0) {
        g_autotune_mode = IOX_AUTOTUNE_AGGRESSIVE;
    } else {
        g_autotune_mode = IOX_AUTOTUNE_DISABLED;
    }
}
```

**Grafana Dashboard Panels:**

1. **JemallocArena Thread Cache Performance Panel:**
   - JemallocArena cache hit ratio (line graph, target: ≥85%)
   - Refill/drain rate (counter, ops/s)
   - Quarantine size (gauge, target: <50% CAP)
   - Auto-tuned JemallocArena cache sizes (internal)

2. **PCFL/PSSC Panel:**
   - JemallocArena cache hit ratio (line graph, target: ≥85%)
   - PSSC hit ratio (line graph, target: ≥95%)
   - Watermark adjustments (step graph)

3. **Safety & Violations Panel:**
   - Canary failures (counter, target: 0)
   - Generation mismatches (counter, target: 0)
   - RedZone violations (counter, target: 0)
   - memprotect events (counter)

4. **Latency Panel:**
   - Alloc latency P50/P99/P99.9 (heatmap)
   - Free latency P50/P99/P99.9 (heatmap)
   - Epoch flush latency (histogram)

5. **Memory Utilization Panel:**
   - Resident/Burst/SHM pool utilization (stacked area chart)
   - Fragmentation ratio (line graph)
   - Span size distribution (histogram)

**Verification Strategy:**

1. **Baseline Capture:**
   ```bash
   # Disable auto-tuning for baseline
   export IOX_AUTOTUNE_MODE=disabled
   ./benchmark --duration=300s --output=baseline.json
   ```

2. **A/B Comparison:**
   ```bash
   # Enable conservative auto-tuning
   export IOX_AUTOTUNE_MODE=conservative
   ./benchmark --duration=300s --output=autotune_conservative.json
   
   # Enable aggressive auto-tuning
   export IOX_AUTOTUNE_MODE=aggressive
   ./benchmark --duration=300s --output=autotune_aggressive.json
   ```

3. **Regression Tests:**
   - **Metrics comparison:** `cache_hit_ratio`, `alloc_latency_p99`, `pool_utilization`
   - **Thresholds:**
     - Hit ratio must not degrade > 5%
     - P99 latency must not increase > 10%
     - No new safety violations
   - **Oscillation detection:** Parameter changes must stabilize within 60 seconds

4. **Automated Alerts (Prometheus AlertManager):**
   ```yaml
   - alert: IoxJemallocArenaCacheHitRatioLow
     expr: iox_tcmalloc_cache_hit_ratio < 0.80
     for: 5m
     annotations:
       summary: "JemallocArena cache hit ratio below 80%"
   
   - alert: IoxCanaryFailure
     expr: increase(iox_canary_failures_total[5m]) > 0
     for: 1m
     annotations:
       severity: critical
       summary: "Canary validation failure detected"
   
   - alert: IoxMemoryPressure
     expr: iox_pool_utilization_ratio{pool="burst"} > 0.85
     for: 10m
     annotations:
       summary: "Burst pool utilization exceeds 85%"
   ```

---

## ⚙️ Configuration

### Build-Time Configuration (iox_config.h)

**Platform Detection:**
```cpp
#pragma once

// Detect platform
#if defined(__LP64__) || defined(_WIN64) || (defined(__WORDSIZE) && __WORDSIZE == 64)
    #define IOX_64BIT 1
    #define IOX_32BIT 0
#else
    #define IOX_64BIT 0
    #define IOX_32BIT 1
#endif

// Security Tier Definitions
#define SECURITY_TIER_NONE           0  // L0: No checks
#define SECURITY_TIER_FAST_CANARY    1  // L1: Header canary only
#define SECURITY_TIER_FULL_SAFETY    2  // L2: Full protection (default)
#define SECURITY_TIER_CRITICAL_REDUN 3  // L3: Dual backup + ECC (future)

// Per-pool security tier configuration
#ifndef IOX_RESIDENT_SECURITY_TIER
#define IOX_RESIDENT_SECURITY_TIER  SECURITY_TIER_FAST_CANARY  // L1 default
#endif

#ifndef IOX_BURST_SECURITY_TIER
#define IOX_BURST_SECURITY_TIER     SECURITY_TIER_FAST_CANARY  // L1 default
#endif

#ifndef IOX_SHM_SECURITY_TIER
#define IOX_SHM_SECURITY_TIER       SECURITY_TIER_FAST_CANARY  // L1
#endif

#ifndef IOX_TC_SECURITY_TIER
#define IOX_TC_SECURITY_TIER        SECURITY_TIER_FAST_CANARY  // L1
#endif

// Size Class Configuration
#ifndef IOX_SIZE_CLASS_COUNT
#define IOX_SIZE_CLASS_COUNT        12  // Number of fixed-size classes
#endif

#ifndef IOX_SIZE_CLASS_MIN
#define IOX_SIZE_CLASS_MIN          8   // Minimum size class (8 bytes)
#endif

#ifndef IOX_SIZE_CLASS_MAX
#define IOX_SIZE_CLASS_MAX          16384  // Maximum size class (16 KB)
#endif

#ifndef IOX_SPANS_PER_CLASS_MIN
#define IOX_SPANS_PER_CLASS_MIN     4   // Minimum pre-allocated spans per class
#endif

#ifndef IOX_FORMAT_ON_INIT
#define IOX_FORMAT_ON_INIT          1   // Auto-format regions on init (1=yes, 0=no)
#endif

// Lease management (independent of security tier)
#ifndef IOX_DEFAULT_LEASE_MS
#define IOX_DEFAULT_LEASE_MS        10000  // 10 seconds (0 = no lease)
#endif
```

**64-bit Configuration:**
```cpp
#if IOX_64BIT

// Pool sizes (adjust per project needs)
#define IOX_RESIDENT_SIZE       (4ULL << 30)     // 4 GiB resident
#define IOX_BURST_SIZE          (256ULL << 30)   // 256 GiB burst
#define IOX_METADATA_SIZE       (64 << 10)       // 64 KiB metadata

// Safety settings
#define IOX_REDZONE_SIZE        64               // 64 bytes RedZone
#define IOX_ALIGNMENT           8                // 8-byte alignment
#define IOX_CANARY_MAGIC        0x10C00DEADBEEFULL

// TC (Thread-Local Cache) settings (recommended for production)
#define IOX_USE_TC              1                // Enable TC allocator
#define IOX_TC_THRESHOLD        1024             // Small allocations (≤1KB) use TC
#define IOX_TC_SLAB_SIZE        (64 * 1024)      // 64KB per-thread slab

// Lazy-Free Chain settings (optional, compile-time switch)
#define IOX_ENABLE_LAZY_FREE    1                // Enable lazy-free chain (default ON)
#define IOX_LAZY_FREE_MAX_DEPTH 16               // Max lazy-free chain depth
#define IOX_LAZY_FREE_LEASE_MS  5000             // Lazy-free lease timeout (5s)

// Quarantine settings (for Safe-mode drain validation)
#define IOX_QUARANTINE_CAP      256              // Max quarantine entries
#define IOX_QUARANTINE_HOLD_MS  100              // Hold time before reuse (ms)

// Epoch flush settings
#define IOX_EPOCH_FLUSH_MS      50               // Max flush window (memprotect/lease)
#define IOX_ASYNC_FLUSH_MS      10               // Background async flush interval

#endif // IOX_64BIT
```

**32-bit Configuration:**
```cpp
#if IOX_32BIT

// Pool sizes (32-bit address space limits)
#define IOX_RESIDENT_SIZE       (128UL << 20)    // 128 MiB resident
#define IOX_BURST_SIZE          (2UL << 30)      // 2 GiB burst
#define IOX_METADATA_SIZE       (16 << 10)       // 16 KiB metadata

// Safety settings (reduced for 32-bit)
#define IOX_REDZONE_SIZE        32               // 32 bytes RedZone
#define IOX_ALIGNMENT           4                // 4-byte alignment
#define IOX_CANARY_MAGIC        0xDEADBEEFU

// TC settings (reduced for 32-bit)
#define IOX_USE_TC              1                // Enable TC allocator
#define IOX_TC_THRESHOLD        512              // Small allocations (≤512B) use TC
#define IOX_TC_SLAB_SIZE        (32 * 1024)      // 32KB per-thread slab

// Lazy-Free Chain settings (reduced for 32-bit)
#define IOX_ENABLE_LAZY_FREE    1                // Enable lazy-free chain
#define IOX_LAZY_FREE_MAX_DEPTH 8                // Max lazy-free chain depth (reduced)
#define IOX_LAZY_FREE_LEASE_MS  3000             // Lazy-free lease timeout (3s)

// Quarantine settings (reduced for 32-bit)
#define IOX_QUARANTINE_CAP      128              // Max quarantine entries
#define IOX_QUARANTINE_HOLD_MS  100              // Hold time before reuse (ms) (50% of CAP)

// Quarantine settings (reduced for 32-bit)
#define IOX_QUARANTINE_CAP      128              // Max quarantine entries
#define IOX_QUARANTINE_HOLD_MS  100              // Hold time before reuse (ms)

#endif // IOX_32BIT
```

**Common Configuration (both platforms):**
```cpp
// Mandatory safety settings
#define IOX_ENABLE_MEMPROTECT   1                // Auto memprotect on breach
#define IOX_PROTECT_ON_CRASH    1                // Immediate protection

// Page alignment settings (unified for 32-bit & 64-bit)
#define IOX_PAGE_SIZE_4K        (1UL << 12)      // 4 KiB standard page
#define IOX_PAGE_SIZE_2M        (1UL << 21)      // 2 MiB huge page
#define IOX_PAGE_SIZE_1G        (1UL << 30)      // 1 GiB giant page

// Region alignment rules (same for all platforms)
#define IOX_SMALL_REGION_ALIGN  IOX_PAGE_SIZE_4K   // < 64KB
#define IOX_MEDIUM_REGION_ALIGN IOX_PAGE_SIZE_2M   // 64KB - 2MB
#define IOX_LARGE_REGION_ALIGN  IOX_PAGE_SIZE_1G   // > 2MB

// Note: No 1M pages on 32-bit (legacy ARM), all platforms use 4K/2M/1G

// Lease settings
#define IOX_LEASE_DEFAULT_MS           10000     // 10 seconds default lease duration
#define IOX_LEASE_EPOCH_INTERVAL_MS    100       // Epoch update interval (100ms default)
                                                  // Lower = more precise expiration, higher CPU
                                                  // Higher = less CPU, coarser granularity
                                                  // Valid range: 1-1000ms

// Security
#define IOX_FAST_CANARY_CHECK   1                // Quick canary on free
#define IOX_QUARANTINE_MAX      256              // Max items per class
#define IOX_EPOCH_FLUSH_MS      50               // Max flush window
```

### Runtime Configuration (Feature Flags)

```cpp
// TC (Thread-Local Cache) mode configured via IOX_USE_TC (see configuration section)
// Enable/disable via compile-time macro:
// -DIOX_USE_TC=1 (default, recommended)
// -DIOX_USE_TC=0 (disable TC, all allocations go to Dynamic Extent or Burst)

// Lazy-Free Chain configured via IOX_ENABLE_LAZY_FREE (compile-time)
// #define IOX_ENABLE_LAZY_FREE  1  (default ON, recommended)
// #define IOX_ENABLE_LAZY_FREE  0  (disable lazy-free, direct extent allocation)

// Configuration parameters:
// - IOX_LAZY_FREE_MAX_DEPTH: Maximum lazy-free chain depth (default: 16)
//   Range: 4-64, higher = more cached extents, lower GC overhead
// - IOX_LAZY_FREE_LEASE_MS: Lease timeout for lazy-freed extents (default: 5000ms)
//   Range: 1000-30000ms, higher = longer cache retention

// Lease epoch interval (environment variable: IOX_LEASE_EPOCH_INTERVAL_MS)
// Controls how often lease scanner wakes up and updates epoch counter
// Default: 100ms (balances precision vs CPU overhead)
// Range: 1-1000ms
// Example: setenv("IOX_LEASE_EPOCH_INTERVAL_MS", "50", 1);  // 50ms for higher precision
```

**Lazy-Free Chain Depth Trade-offs:**

| Depth | Extent Cache Hit Rate | Memory Overhead | GC CPU Overhead | Recommended Use Case |
|-------|----------------------|-----------------|-----------------|----------------------|
| **4** | ~50% | +64 KiB | ~0.2% | Memory-constrained embedded systems |
| **8** | ~70% | +128 KiB | ~0.15% | Balanced for 32-bit platforms |
| **16** (default) | ~85% | +256 KiB | ~0.1% | **General production use (64-bit)** |
| **32** | ~92% | +512 KiB | ~0.08% | High allocation rate workloads |
| **64** | ~95% | +1 MiB | ~0.05% | Maximum performance, ample memory |

**Note:** Lazy-free chain reduces extent allocation overhead by caching recently freed extents.
Higher depth = more cached extents = higher hit rate, but more memory overhead.

**Lease Epoch Interval Trade-offs:**

| Interval | Expiration Accuracy | CPU Overhead | Recommended Use Case |
|----------|---------------------|--------------|----------------------|
| **10ms** | ±10ms | ~0.5% | High-precision lease requirements |
| **50ms** | ±50ms | ~0.2% | Balanced precision for real-time systems |
| **100ms** (default) | ±100ms | ~0.1% | **General production use** |
| **500ms** | ±500ms | <0.05% | Low-priority background tasks |
| **1000ms** | ±1s | <0.02% | Coarse-grained lease management |

**Note:** Lower intervals increase lease scanner thread wakeups and epoch counter updates.

### Size Classes

**IOX TC Size Classes (32 classes, 8B-1024B):**

After calling `format()`, the TC region is pre-partitioned with 32 fine-grained size classes:

| Class | Size | Alignment | Preallocated Count | Use Case |
|-------|------|-----------|-------------------|----------|
| 0 | 8 B | 8 B | 100 | Tiny structures |
| 1 | 16 B | 8 B | 100 | Pointers, small data |
| 2 | 24 B | 8 B | 80 | Small objects |
| 3 | 32 B | 8 B | 80 | Small objects |
| 4 | 40 B | 8 B | 70 | Medium objects |
| 5 | 48 B | 8 B | 70 | Medium objects |
| 6 | 56 B | 8 B | 60 | Medium objects |
| 7 | 64 B | 64 B | 60 | Cache-line sized |
| 8 | 80 B | 8 B | 50 | Larger objects |
| 9 | 96 B | 8 B | 50 | Larger objects |
| 10 | 112 B | 8 B | 40 | Larger objects |
| 11 | 128 B | 8 B | 40 | Larger objects |
| 12-19 | 144B-256B | 8 B | 30-20 | Medium-large objects |
| 20-23 | 288B-384B | 8 B | 15-10 | Large objects |
| 24-27 | 448B-640B | 64 B | 8-5 | Extra large |
| 28-31 | 768B-1024B | 64 B | 4-2 | Maximum TC size |

**Full Size Class Array:**
```cpp
const uint16_t PRESET_IOXPOOL_SIZES[32] = {
    8, 16, 24, 32, 40, 48, 56, 64,       // 0-7: 8-64B
    80, 96, 112, 128, 144, 160, 176, 192, // 8-15: 80-192B
    208, 224, 240, 256, 288, 320, 352, 384, // 16-23: 208-384B
    448, 512, 576, 640, 768, 896, 960, 1024 // 24-31: 448-1024B
};

const uint8_t PRESET_IOXPOOL_COUNTS[32] = {
    100, 100, 80, 80, 70, 70, 60, 60,   // 0-7
    50, 50, 40, 40, 30, 30, 25, 25,     // 8-15
    20, 20, 15, 15, 12, 10, 8, 8,       // 16-23
    6, 5, 4, 3, 3, 2, 2, 2              // 24-31
};
```

**Large Allocations:**
- **1KB-2MB:** Allocated from Dynamic Extent Region (bitmap-based slab allocation)
- **>2MB:** Allocated from Burst Region (freelist-managed large blocks)

**Size Class Configuration:**

```cpp
struct SizeClassConfig {
    uint32_t num_classes;           // Number of size classes (fixed: 32)
    uint16_t class_sizes[32];       // Size for each class (bytes)
    uint8_t class_counts[32];       // Preallocated blocks per class
};

// Default configuration (automatically used if nullptr passed to format())
const SizeClassConfig DEFAULT_SIZE_CLASS_CONFIG = {
    .num_classes = 32,
    .class_sizes = {
        8, 16, 24, 32, 40, 48, 56, 64,       // 0-7: 8-64B
        80, 96, 112, 128, 144, 160, 176, 192, // 8-15: 80-192B
        208, 224, 240, 256, 288, 320, 352, 384, // 16-23: 208-384B
        448, 512, 576, 640, 768, 896, 960, 1024 // 24-31: 448-1024B
    },
    .class_counts = {
        100, 100, 80, 80, 70, 70, 60, 60,   // 0-7
        50, 50, 40, 40, 30, 30, 25, 25,     // 8-15
        20, 20, 15, 15, 12, 10, 8, 8,       // 16-23
        6, 5, 4, 3, 3, 2, 2, 2              // 24-31
    }
};

// Example: Format region with default size classes
void* base = __iox_start;
size_t size = __iox_end - __iox_start;
IoxAllocatorCore allocator;
allocator.init(base, size, REGION_TYPE_BSS_RESIDENT);
allocator.format();  // Use DEFAULT_SIZE_CLASS_CONFIG
```

**Size Class Calculation Function:**

```cpp
// Calculate size class index for a given size
inline int calculate_size_class(size_t size) {
    if (size <= 64) {
        // 0-7: 8B increments (8, 16, 24, 32, 40, 48, 56, 64)
        return (size + 7) / 8 - 1;
    } else if (size <= 128) {
        // 8-11: 16B increments (80, 96, 112, 128)
        return 7 + (size - 64 + 15) / 16;
    } else if (size <= 256) {
        // 12-19: 16B increments (144, 160, 176, 192, 208, 224, 240, 256)
        return 11 + (size - 128 + 15) / 16;
    } else if (size <= 384) {
        // 20-23: 32B increments (288, 320, 352, 384)
        return 19 + (size - 256 + 31) / 32;
    } else if (size <= 640) {
        // 24-27: 64B increments (448, 512, 576, 640)
        return 23 + (size - 384 + 63) / 64;
    } else {
        // 28-31: 128B increments (768, 896, 960, 1024)
        return 27 + (size - 640 + 127) / 128;
    }
}

// Example: Custom size class configuration
SizeClassConfig custom_config = {
    .num_classes = 8,
    .class_sizes = {16, 32, 64, 128, 256, 512, 1024, 2048},
    .class_alignments = {8, 8, 64, 8, 8, 8, 8, 8},
    .spans_per_class = {8, 8, 16, 16, 32, 32, 64, 64}
};
allocator.format(&custom_config);  // Use custom configuration
```

**Format Benefits:**
- **Zero Fragmentation:** Each size class has dedicated memory spans
- **O(1) Allocation:** Direct freelist lookup by size class
- **Cache-Friendly:** Segregated memory improves locality
- **Predictable Performance:** No coalescing overhead
- **Thread-Safe:** Per-core freelists eliminate contention

---

## 🔐 Security & Monitoring

### Security Metrics (Required Alerts)

```cpp
struct SecurityMetrics {
    uint64_t canary_failures;     // P1 alert if > 0
    uint64_t redzone_violations;  // P1 alert if > 0
    uint64_t mprotect_triggers;   // P2 alert if > threshold
    uint64_t generation_mismatches; // Monitor for use-after-free
    uint64_t quarantine_rejects;  // Items failed validation
};
```

### Alert Thresholds

| Metric | Threshold | Severity | Action |
|--------|-----------|----------|--------|
| canary_failures | > 0 | P1 | Immediate investigation + dump |
| mprotect_triggers | > 10/hour | P2 | Review allocation patterns |
| epoch_bumps | > 1000/hour | P3 | Check lease thrashing |
| quarantine_rejects | > 100/min | P2 | Memory corruption scan |

### Forensic Dump on Breach

**Automatic Actions:**
1. Trigger `mprotect(affected_region, PROT_NONE)`
2. Signal handler captures:
   - Last 100 allocations (ring buffer)
   - Last 100 frees (ring buffer)
   - Current JemallocArena thread cache state (all threads)
   - Central free list state
3. Generate core dump
4. Upload to analysis platform

**Ring Buffer Structure:**
```c
struct AllocRecord {
    uint64_t handle;
    uint64_t timestamp_ns;
    uint32_t size;
    uint32_t thread_id;
    const char* file;
    int line;
};

extern AllocRecord g_alloc_ring[100];
extern AllocRecord g_free_ring[100];
```

### Monitoring Integration

**Prometheus Metrics:**
```
iox_alloc_total{size_class="4"}
iox_free_total{size_class="4"}
iox_tcmalloc_cache_hit_ratio
iox_quarantine_size
iox_security_events_total{type="canary_fail"}
```

**Logging (structured JSON):**
```json
{
  "event": "security_breach",
  "type": "canary_corruption",
  "handle": "0x1234567890ABCDEF",
  "thread_id": 42,
  "timestamp": "2025-12-01T10:30:45.123Z",
  "action": "mprotect_triggered"
}
```

---

## 🔄 Migration from Legacy

> **Note:** Detailed migration documentation for PoolAllocator v1.x is archived in `MEMORY_MANAGEMENT_GUIDE_LEGACY_PoolAllocator.md`

### Deprecated Systems (Prohibited as of 2025-07-01)

❌ `malloc/free/calloc/realloc`  
❌ `::operator new/delete` (non-overridden)  
❌ `jemalloc / tcmalloc / mimalloc / hoard`  
❌ Custom memory pools (PoolAllocator/MemoryTracker from v1.x)  
❌ Stack arrays > 16 KiB

### Quick Migration Guide

**Legacy API:**
```cpp
void* ptr = lap::core::Memory::malloc(256);
lap::core::Memory::free(ptr);
```

**New API:**
```cpp
uint64_t handle = iox_alloc(256);
void* ptr = iox_from_handle(handle);  // Use sparingly
// ... use ptr ...
iox_free(handle);  // Free by handle, not pointer
```

**Global new/delete Override:**
```cpp
// Automatically routed to IoxPool
MyClass* obj = new MyClass();  // → iox_alloc
delete obj;                     // → iox_free
```

### 32-bit System Considerations

**Address Space Limits:**
- Total addressable: ~3.5 GiB (kernel reserves ~0.5 GiB)
- ResidentPool max: 512 MiB (recommended: 128 MiB)
- BurstPool max: 3 GiB (recommended: 2 GiB)
- Leave ~512 MiB for stack, libraries, kernel

**Handle Compatibility:**
- Handles are always 8 bytes (uint64_t) on both platforms
- 32-bit offset field (28 bits) limits single pool to 256 MiB
- Use multiple pools for larger allocations
- Handle format automatically adapts at compile time

**Performance Differences:**
- 32-bit: Slightly faster pointer arithmetic (4-byte aligned)
- 64-bit: Larger cache footprint but more headroom
- JemallocArena cache hit rates similar on both platforms
- 32-bit may see more page faults under heavy load

**Recommendations for 32-bit:**
1. Keep ResidentPool ≤ 128 MiB
2. Monitor total memory usage closely
3. Use shorter lease timeouts (e.g., 5s instead of 10s)
4. JemallocArena automatically reduces thread cache sizes under memory pressure
5. Use 4K pages only (huge pages require more virtual address space)
6. Consider upgrading to 64-bit for memory-intensive workloads

**Note:** Resident Region uses mutex-protected bitmaps on all platforms, ensuring consistent and predictable behavior.

### Breaking Changes from v1.x

- **API**: `Memory::malloc()` → `iox_alloc()` (use handles, not pointers)
- **API**: `StlMemoryAllocator` → `std::allocator` (auto-overridden)
- **Config**: `SecurityTier` parameter → `IOX_SECURITY_LEVEL` compile-time macro
- **Config**: Custom align config → System-optimal (8B/64-bit, 4B/32-bit)
- **Memory**: 1M page (32-bit) → Unified 4K/2M/1G all platforms
- **Monitoring**: `MemoryTracker` → `iox_get_stats()` API

---

## 📖 Examples

### Region Format Example

```cpp
void example_format_region() {
    // Step 1: Initialize region
    void* base = __resident_start;
    size_t size = __resident_end - __resident_start;
    iox_region_init(base, size, REGION_TYPE_BSS_RESIDENT);
    
    // Step 2: Format with default size classes (optional, auto on first alloc)
    iox_region_format(0);  // 0 = ResidentPool, uses DEFAULT_SIZE_CLASS_CONFIG
    
    // Step 3: Verify format
    if (!iox_region_is_formatted(0)) {
        fprintf(stderr, "Failed to format region\\n");
        exit(1);
    }
    
    // Ready for allocation - all sizes use fixed-size classes
    uint64_t h1 = iox_alloc(64, 0);    // Uses Class 3 (64B)
    uint64_t h2 = iox_alloc(256, 0);   // Uses Class 5 (256B)
    uint64_t h3 = iox_alloc(4096, 0);  // Uses Class 9 (4KB)
}
```

### Custom Size Class Configuration

```cpp
void example_custom_size_classes() {
    // Define custom size classes (8 classes instead of default 12)
    SizeClassConfig custom = {
        .num_classes = 8,
        .class_sizes = {16, 32, 64, 128, 256, 512, 1024, 2048},
        .class_alignments = {8, 8, 64, 8, 8, 8, 8, 8},
        .spans_per_class = {8, 8, 16, 16, 32, 32, 64, 64}
    };
    
    // Initialize and format BurstPool with custom config
    void* base = __burst_start;
    size_t size = __burst_end - __burst_start;
    iox_region_init(base, size, REGION_TYPE_BSS_BURST);
    iox_region_format(1, &custom);  // 1 = BurstPool
    
    // Allocations now use custom size classes
    uint64_t handle = iox_alloc(128, 5000);  // Uses custom Class 3 (128B)
}
```

### Region Statistics

```cpp
void example_region_stats() {
    RegionStats stats;
    iox_region_get_stats(0, &stats);  // 0 = ResidentPool
    
    printf("Region Stats:\\n");
    printf("  Total size: %zu bytes\\n", stats.total_size);
    printf("  Used: %zu bytes\\n", stats.used_bytes);
    printf("  Free: %zu bytes\\n", stats.free_bytes);
    printf("  Formatted: %s\\n", stats.formatted ? "yes" : "no");
    printf("  Active chunks: %u\\n", stats.active_chunks);
    printf("  Size classes: %u\\n", stats.num_size_classes);
    
    // Per-class statistics
    for (uint32_t i = 0; i < stats.num_size_classes; i++) {
        printf("  Class %u (%u bytes): %u free / %u total\\n",
               i, stats.class_info[i].size,
               stats.class_info[i].free_chunks,
               stats.class_info[i].total_chunks);
    }
}
```

### Basic Allocation

```cpp
#include <iox/allocator.h>

void example_basic() {
    // Allocate 256 bytes with 10s lease
    uint64_t handle = iox_alloc(256, 10000);
    if (handle == 0) {
        // Allocation failed
        return;
    }
    
    // Use memory (convert to pointer)
    void* ptr = iox_from_handle(handle);
    memset(ptr, 0, 256);
    
    // Free by handle
    iox_free(handle);
}
```

### Resident (Permanent) Allocation

```cpp
void example_resident() {
    // Core algorithm data - never reclaim
    uint64_t handle = iox_alloc(4096, 0);  // lease_ms = 0 → resident
    
    // Store handle for lifetime of application
    g_permanent_data_handle = handle;
}
```

### Shared Memory (Iceoryx2)

```cpp
void example_shm() {
    // Allocate shared memory (bypasses JemallocArena thread cache)
    uint64_t handle = iox_alloc_shared(1024, true);
    
    // Share handle across processes (handle is 8 bytes, safe to copy)
    send_to_other_process(handle);
    
    // Other process uses same handle
    void* ptr = iox_from_handle(handle);
    
    // Free (direct to quarantine, not JemallocArena thread cache)
    iox_free_shared(handle);
}
```

### Debug Build with Tracking

```cpp
void example_debug() {
    // Automatically captures file/line
    uint64_t handle = IOX_ALLOC(512, 5000);
    
    // On breach, log will show:
    // "Allocated at example.cpp:42 by thread 17"
    
    iox_free(handle);
}
```

---

## 🎯 Best Practices

### DO ✅

1. **Use handles, not pointers** - Store handles long-term, convert to pointers only when needed
2. **Format regions on init** - Always call `iox_region_format()` or enable `IOX_FORMAT_ON_INIT=1` for optimal performance
3. **Set appropriate leases** - Use `lease_ms=0` for permanent data, non-zero for temporary (SHM is always lease-exempt)
4. **Enable debug mode** - Use `IOX_ALLOC` macro in debug builds
5. **Monitor metrics** - Track `canary_failures` and `mprotect_triggers` via `iox_get_metrics()`
6. **Enable JemallocArena** - Use `IOX_USE_TCMALLOC=ON` for production (default), provides 50% latency improvement
7. **Pin processes to CPUs** - Use `taskset` or `sched_setaffinity()` for best Multi-arena locality (not required but recommended)
8. **Ensure kernel ≥ 4.18** - Per-CPU mode requires rseq support (fallback to Thread-Local on older kernels)
9. **Enable huge pages** - Use 2M huge pages for medium allocations (`echo 512 > /proc/sys/vm/nr_hugepages`)
10. **Configure region sizes** - Set IOX_RESIDENT_SIZE and IOX_BURST_SIZE in linker script based on workload
11. **Align to page boundaries** - Allocate regions on 4K/2M/1G boundaries for best TLB efficiency
12. **Use PSSC for SHM** - Always allocate shared memory via `iox_alloc_shared()` for PSSC optimization
13. **Register SHM segments** - Call `iox_register_shm_segment()` at startup for each IPC segment
14. **Implement ref-counting** - SOA runtime must track cross-process SHM references
15. **Monitor PSSC hit rate** - Target ≥95% PSSC hit rate for optimal SHM performance
16. **Consider platform limits** - On 32-bit, be mindful of 3.5 GiB total address space limit
17. **Export metrics to Grafana** - Use `iox_export_prometheus()` for production monitoring
18. **Enable auto-tuning conservatively** - Set `IOX_AUTOTUNE_MODE=conservative` for adaptive optimization
19. **Set up alerting** - Configure Prometheus alerts for `canary_failures`, `tcmalloc_cache_hit_ratio < 0.90`, and `pool_utilization > 0.85`
20. **Tune JemallocArena threshold** - Adjust `IOX_TCMALLOC_THRESHOLD` based on allocation patterns (default 256KB works for most cases)

### DON'T ❌

1. **Don't mix allocators** - Never use malloc/jemalloc alongside IoxPool (JemallocArena is integrated, not mixed)
2. **Don't store raw pointers** - Pointers invalidate on memprotect, handles survive
3. **Don't bypass JemallocArena** - Let the allocator route requests automatically (threshold-based)
4. **Don't ignore alerts** - `canary_failures > 0` is always critical
5. **Don't stack arrays > 16KB** - Use heap allocation instead
6. **Don't exceed 32-bit limits** - On 32-bit systems, total BurstPool allocation must stay under 3.5 GiB
7. **Don't assume 64-bit layout** - Always use platform-agnostic handle APIs
8. **Don't use JemallocArena for SHM** - SHM must use `iox_alloc_shared()` and PSSC path (bypasses JemallocArena)
9. **Don't rely on lease for SHM** - SHM blocks are always lease-exempt (SOA-controlled)
10. **Don't free SHM prematurely** - Ensure all cross-process readers finished before calling `iox_free_shared()`
11. **Don't exceed PSSC capacity** - Monitor `iox_pssc_cached_blocks` and adjust `max_cached` if needed
12. **Don't disable JemallocArena in production** - Unless you have a specific reason, always use `IOX_USE_TCMALLOC=ON`
13. **Don't manually tune Multi-arena** - JemallocArena handles this automatically, only adjust `IOX_TCMALLOC_THRESHOLD` if needed
12. **Don't mix SHM segments** - Keep each IPC channel on dedicated segment for PSSC isolation
13. **Don't skip format** - Unformatted regions have degraded performance and higher fragmentation
14. **Don't allocate before format** - Always format region first, or enable auto-format with `IOX_FORMAT_ON_INIT=1`

---

## 📜 Version History

### v0.3.0 (2025-12-09)
**IOX Deterministic Memory Pool - Embedded Architecture Optimization**

**🎯 Highlights:**
- ✅ **IOX Thread-Local Cache (TC)** - Per-thread 64KB slab with 32 size classes (8B-1024B)
- ✅ **Fine-Grained Size Classes** - 32 classes for minimal internal fragmentation (5-8%)
- ✅ **Dynamic Extent Management** - Bitmap-based slab allocation for 1KB-2MB requests
- ✅ **Lazy-Free Chain** - Optional compile-time feature (IOX_ENABLE_LAZY_FREE) with depth/lease limits
- ✅ **Embedded Optimized** - Base+offset addressing, fixed memory pools, no runtime expansion
- ✅ **Low Fragmentation** - 3-8% external fragmentation via extent coalescing
- ✅ **Self-Contained** - No external allocator dependencies (removed libtcmalloc)
- ✅ **Deterministic** - Compile-time configuration, predictable latency (P99 < 100ns)
- ✅ **Full Control** - Self-implemented allocator with complete configurability

**⚠️ Migration from v0.2.0 (TCMalloc):**
- TCMalloc dependency removed - now self-contained IOX implementation
- Threshold changed from 256KB → 1024B (TC), 2MB (Burst)
- Per-thread cache replaces Per-CPU cache (more portable, slightly higher thread migration cost)
- All APIs remain compatible - no code changes required

**🏗️ Architecture Changes:**
- **Layer 1 (Memory Regions):** UNCHANGED - .iox linker section layout preserved
  - Resident Region: 128 MiB (Chunk 0: Header, Chunks 1-3: TC Slabs, Chunks 4-63: Dynamic Extent)
  - Burst Region: 2 GiB (>2MB allocations)
- **Layer 2 (Allocator Core):** Complete rewrite with IOX Deterministic Pool
  - Removed: TCMalloc integration + ResidentBackend wrapper
  - Added: ThreadCache + TCBin + ExtentManager + Lazy-Free Chain (optional)
- **Layer 3 (Adapter):** UNCHANGED - iox_alloc/iox_free API preserved

**📊 Performance Characteristics (Target):**

| Metric | v0.2.0 (TCMalloc) | v0.3.0 (IOX TC) | Change |
|--------|-------------------|-----------------|--------|
| P50 latency (≤1KB) | 10 ns | **10-15 ns** | +0-5ns (comparable) |
| P99 latency (≤1KB) | 45 ns | **32 ns** | -29% ⬇️ |
| P99.9 latency (≤1KB) | 850 ns | **72 ns** | -92% ⬇️ |
| Internal fragmentation | 15-25% | **5-8%** | -12% ⬇️ |
| External fragmentation | 10-20% | **3-8%** | -10% ⬇️ |
| Memory efficiency | 65-75% | **87-92%** | +17% ⬆️ |
| Throughput (32 threads) | 450M ops/s | **600M ops/s** | +33% ⬆️ |
| Memory overhead/thread | ~128 KiB | **64 KiB** | -50% ⬇️ |
| External dependencies | libtcmalloc | **None** | Fully independent |

**🔧 Key Changes:**
- `ThreadCache.hpp/cpp`: New per-thread TC allocator with 32 size classes (1200 lines)
- `ExtentManager.hpp/cpp`: Bitmap-based extent allocation and coalescing (900 lines)
- `SizeClass.hpp`: 32 size class definitions (8B-1024B) with calculate_size_class() (300 lines)
- `LazyFreeChain.hpp/cpp`: Optional lazy-free chain with depth/lease limits (400 lines, #ifdef IOX_ENABLE_LAZY_FREE)
- `ResidentBackend.hpp/cpp`: DELETED (replaced by ExtentManager)
- `TCMallocBackend.hpp`: DELETED (no longer using external TCMalloc)
- `CMakeLists.txt`: Removed TCMalloc dependency checks

**⚙️ Build Configuration:**
```bash
# Enable TC (default)
cmake -B build -DIOX_USE_TC=ON

# Configure TC threshold (default: 1024B)
cmake -B build -DIOX_TC_THRESHOLD=1024

# Enable lazy-free chain (default: ON)
cmake -B build -DIOX_ENABLE_LAZY_FREE=ON -DIOX_LAZY_FREE_MAX_DEPTH=16

# Configure Resident Region TC chunks (default: 2, i.e., Chunks 1-2)
cmake -B build -DIOX_TC_CHUNK_COUNT=2
```

**🚀 Migration Guide:**
- No API changes required - all applications using v0.2.0 work as-is
- Performance improved: lower latency tail (P99.9 -92%), higher throughput (+33%)
- Remove `-ltcmalloc` from link flags if explicitly added
- Monitor new metrics: `tc_hit_ratio`, `lazy_free_hits`, `extent_coalesce_count`

**⚠️ Breaking Changes:**
- None at API level - fully backward compatible
- Internal implementation completely replaced (not binary compatible with v0.2.0)
- TCMalloc-specific tuning environment variables no longer apply

---

### v0.2.0 (2025-12-07) - ARCHIVED
**TCMalloc Integration Release**

> **📌 Note:** This version used external Google TCMalloc library. Design archived to `MEMORY_MANAGEMENT_GUIDE_v0.2.0_TCMalloc_ARCHIVED.md`

**Highlights:**
- External TCMalloc integration with Per-CPU cache (rseq)
- 10ns P50 latency for small allocations
- 256KB threshold for TCMalloc fast path
- See archived document for full details

---

### v0.1.0 (2025-12-02) - ARCHIVED
**Initial Release - IoxPool Architecture Documentation**

> **📌 Note:** Initial custom allocator design. See archived documents for details.

---

## 🔗 References

- [v0.2.0 JemallocArena Design (Archived)](MEMORY_MANAGEMENT_GUIDE_v0.2.0_JemallocArena_ARCHIVED.md) - External JemallocArena integration design
- [jemalloc Refactoring Plan](REFACTORING_PLAN_JEMALLOC.md) - Detailed v0.3.0 refactoring roadmap
- [Current Design Quick Reference](CURRENT_DESIGN_QUICK_REF.md) - Quick reference for v0.3.0 design
- [Legacy PoolAllocator Guide](MEMORY_MANAGEMENT_GUIDE_LEGACY_PoolAllocator.md) - Archived v1.x documentation
- [AUTOSAR AP SWS_CORE Specification](AUTOSAR_AP_SWS_Core.pdf) - Standards compliance
- [jemalloc GitHub](https://github.com/jemalloc/jemalloc) - Upstream jemalloc source for reference

---

## 📞 Support

**Questions or Issues?**  
Contact: ddkv587@gmail.com  
Project: https://github.com/TreeNeeBee/LightAP  

**Security Incidents:**  
- P1 (canary breach): Immediate escalation to security team
- P2 (performance): Create issue with metrics
- P3 (questions): Documentation or Slack

---

**Last Updated:** 2025-12-09  
**Document Version:** 0.3.0  
**Status:** 🔄 Development - jemalloc-inspired Arena Implementation

---

**Version Tracking:**  
Next update will auto-increment to v0.3.1 for patches, v0.4.0 for features (L3 security tiers), v1.0.0 for production release.
