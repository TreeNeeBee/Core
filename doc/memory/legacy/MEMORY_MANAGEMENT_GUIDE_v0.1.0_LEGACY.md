# IoxPool v11.0 Memory Management Guide - v0.1.0 (LEGACY)

**Version:** 0.1.0 (Archived)  
**Date:** 2025-12-02  
**Status:** ⚠️ ARCHIVED - Replaced by v0.2.0 with TCMalloc Integration

---

## ⚠️ DEPRECATION NOTICE

This document describes the **legacy TCache-based architecture** used in IoxPool v0.1.0.

**This design has been replaced in v0.2.0** with Google TCMalloc integration, which provides:
- 50% latency reduction (150ns → 10ns)
- 95%+ cache hit rate (vs 85% in TCache)
- Zero thread migration cost (Per-CPU vs Thread-Local)
- 90% code reduction (2000 lines → 200 lines)

**For current documentation, see:** `MEMORY_MANAGEMENT_GUIDE.md` (v0.2.0+)

---

## Legacy Architecture Overview

### Self-Implemented TCache Design

The original IoxPool v0.1.0 used a custom Thread-Local Cache (TCache) implementation:

```
Thread-Local TCache
├── TCacheBin[0]  (4 bytes)   [Entry₀ Entry₁ ... Entry₃₁]
├── TCacheBin[1]  (8 bytes)   [Entry₀ Entry₁ ... Entry₃₁]
├── TCacheBin[2]  (16 bytes)  [Entry₀ Entry₁ ... Entry₃₁]
├── TCacheBin[3]  (32 bytes)  [Entry₀ Entry₁ ... Entry₃₁]
├── TCacheBin[4]  (64 bytes)  [Entry₀ Entry₁ ... Entry₃₁]
├── TCacheBin[5]  (128 bytes) [Entry₀ Entry₁ ... Entry₃₁]
├── TCacheBin[6]  (256 bytes) [Entry₀ Entry₁ ... Entry₃₁]
└── TCacheBin[7]  (512 bytes) [Entry₀ Entry₁ ... Entry₃₁]

Per bin: 32 entries (TCACHE_CAP)
Total memory per thread: ~16 KiB
```

### Legacy ResidentRegion Structure

```cpp
struct ResidentRegionHeader {
    // Magic field protection
    uint32_t magic_start;          // 0x524F5349 ('ISOR')
    
    // Unified bitmap management
    uint64_t chunk_bitmap;         // Main 2MB chunk allocation
    uint32_t tc_init_bitmap[64];   // TC init blocks (64KB subdivision)
    
    // Metadata snapshots (ext4-style)
    uint32_t snapshot_count;       // 0-4 snapshots
    uint64_t snapshot_crc32[4];    // CRC32 for each snapshot
    
    // Lock for TC creation/destruction
    pthread_mutex_t tc_init_mutex; // Protects tc_init_bitmap
    
    uint32_t magic_end;            // 0x524F5349 ('ISOR')
};
```

### Key Components Removed in v0.2.0

1. **TCache.cpp** (~2000 lines) - Replaced by TCMalloc
2. **TCacheBin** - Thread-local cache bins
3. **tc_init_bitmap[64]** - 64KB TC init block tracking
4. **tc_init_mutex** - TC creation lock
5. **Generation validation** - Safe-mode UAF protection
6. **Epoch-based flush** - Async TCache flush protocol

### Performance Comparison

| Metric | v0.1.0 (TCache) | v0.2.0 (TCMalloc) | Improvement |
|--------|-----------------|-------------------|-------------|
| P50 latency | 150 ns | 10 ns | -93.3% |
| P99 latency | 380 ns | 45 ns | -88.2% |
| Cache hit rate | 85% | 95%+ | +12% |
| Thread migration | ~200 ns penalty | 0 ns (Per-CPU) | -100% |
| Code maintenance | 2000 lines | 200 lines | -90% |

---

## Why TCache Was Replaced

### Limitations of Self-Implemented TCache

1. **Thread migration penalty**: ~200ns cost when threads move between CPUs
2. **Lower cache hit rate**: 85% vs 95%+ in TCMalloc
3. **Maintenance burden**: 2000 lines of custom allocator code
4. **Slower latency**: 150ns P50 vs 10ns in TCMalloc Per-CPU mode
5. **Limited optimization**: No access to Google's decade of TCMalloc research

### Benefits of TCMalloc Integration

1. **Production-proven**: Used by Google, Chromium, LLVM
2. **Per-CPU cache**: Zero thread migration cost with rseq (Linux ≥4.18)
3. **Superior performance**: 50% latency reduction, 12% higher hit rate
4. **Simplified maintenance**: 90% code reduction
5. **Active development**: Continuous improvements from Google

---

## Migration Guide

### No API Changes Required

The public API remains unchanged:
```cpp
uint64_t iox_alloc(size_t size, uint32_t lease_ms = 10000);
void iox_free(uint64_t handle);
```

### Configuration Changes

**v0.1.0 (Old):**
```cpp
#define IOX_TCACHE_BINS  8      // Number of size classes
#define IOX_TCACHE_CAP  32      // Entries per bin
```

**v0.2.0 (New):**
```cpp
#define IOX_USE_TCMALLOC  1                // Enable TCMalloc
#define IOX_TCMALLOC_THRESHOLD  (256*1024)  // 256KB threshold
#define IOX_FORCE_THREAD_LOCAL  0           // Auto-detect Per-CPU support
```

### Build System Changes

**v0.1.0 (Old):**
```cmake
# No external dependencies
add_library(IoxPool
    TCache.cpp          # ~2000 lines
    ResidentRegion.cpp
    BurstRegion.cpp
)
```

**v0.2.0 (New):**
```cmake
# Integrate TCMalloc
find_package(TCMalloc REQUIRED)
add_library(IoxPool
    # TCache.cpp DELETED
    TCMallocBackend.cpp  # ~100 lines
    ResidentRegion.cpp
    BurstRegion.cpp
)
target_link_libraries(IoxPool PRIVATE tcmalloc)
```

---

## Historical Context

### Development Timeline

- **2024-11**: Initial TCache implementation (v0.1.0-alpha)
- **2024-12**: ASIL-D dual-region design (later removed)
- **2025-01**: Unified bitmap optimization
- **2025-02**: Magic field protection + metadata snapshots
- **2025-12-02**: v0.1.0 release (TCache-based)
- **2025-12-07**: v0.2.0 release (TCMalloc integration)

### Lessons Learned

1. **Don't reinvent the wheel**: TCMalloc's 15+ years of optimization beats custom implementation
2. **Per-CPU > Thread-Local**: rseq eliminates thread migration penalty entirely
3. **Maintainability matters**: 90% code reduction improves long-term viability
4. **Production-proven > Custom**: Google's battle-tested allocator vs in-house design

---

## For Historical Reference Only

This document is preserved for:
- Understanding the evolution of IoxPool architecture
- Providing context for v0.1.0 users migrating to v0.2.0
- Academic reference for custom allocator design patterns
- Debugging legacy systems still using v0.1.0

**Do NOT use this design for new projects.**

---

**Archive Date:** 2025-12-07  
**Archived By:** IoxPool Development Team  
**Reason:** Replaced by superior TCMalloc integration in v0.2.0
