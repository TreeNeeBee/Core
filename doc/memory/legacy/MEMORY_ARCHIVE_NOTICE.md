# Memory Management Documentation Archive Notice

**Date:** 2025-12-01  
**Author:** LightAP Team

---

## 📢 Important Notice

The LightAP Core memory management system has undergone a **complete architectural redesign** from v1.x (PoolAllocator-based) to **v11.0 (BssIoxPool + TCache)**.

---

## 📚 Documentation Structure

### Current (Production)

| Document | Status | Description |
|----------|--------|-------------|
| [MEMORY_MANAGEMENT_GUIDE.md](MEMORY_MANAGEMENT_GUIDE.md) | ✅ **Active** | BssIoxPool v11.0 + TCache - Complete user guide |
| [BssIoxPool_TCache_Design.md](BssIoxPool_TCache_Design.md) | ✅ **Active** | Technical design specification |
| [memory_ref.md](memory_ref.md) | ✅ **Active** | Architecture reference |

### Archived (Legacy)

| Document | Status | Description |
|----------|--------|-------------|
| [MEMORY_MANAGEMENT_GUIDE_LEGACY_PoolAllocator.md](MEMORY_MANAGEMENT_GUIDE_LEGACY_PoolAllocator.md) | 📦 **Archived** | PoolAllocator v1.x - Deprecated architecture |

---

## 🔄 Why the Change?

### Legacy System (v1.x)
- **Architecture:** PoolAllocator + MemoryTracker + MemoryManager
- **Issues:**
  - Global lock contention (10-100x slowdown on 16+ threads)
  - 600% memory overhead in debug mode (80 bytes for 16-byte allocation)
  - No NUMA awareness
  - Limited pool sizes (max 1KB)
  - Tight coupling to ConfigManager
  - Complex validation logic spread across components

### New System (v11.0)
- **Architecture:** BssIoxPool + Thread-Local Cache (TCache)
- **Improvements:**
  - +35-70% throughput improvement
  - Handle-based safety (8-byte handle, not raw pointers)
  - Built-in RedZone + Canary + automatic memprotect
  - Per-core affinity for deterministic latency
  - Unified burst pool (60% complexity reduction)
  - Iceoryx2 compatible shared memory
  - 100% overflow detection (24 months proven)

---

## ⚠️ Migration Status

### Deprecated as of 2025-07-01

The following from v1.x are **no longer supported**:

❌ `lap::core::Memory::malloc/free`  
❌ `lap::core::StlMemoryAllocator`  
❌ `lap::core::MemoryManager`  
❌ `lap::core::PoolAllocator`  
❌ `lap::core::MemoryTracker`  
❌ Custom alignment configuration  
❌ JSON-based pool configuration  

### Replacement APIs

All code must use BssIoxPool v11.0 APIs:

✅ `bssiox_alloc(size, lease_ms)` - replaces Memory::malloc  
✅ `bssiox_free(handle)` - replaces Memory::free  
✅ `bssiox_alloc_shared(size, true)` - for shared memory  
✅ Global `new/delete` override - automatic routing  

---

## 📖 For Historical Reference

The archived document `MEMORY_MANAGEMENT_GUIDE_LEGACY_PoolAllocator.md` contains:

1. **Complete v1.x architecture analysis** (Dec 1, 2025 snapshot)
   - Code organization (4 components)
   - 12 identified architectural issues
   - Performance bottlenecks
   - Refactoring roadmap (never implemented)

2. **Original API documentation**
   - Memory facade
   - StlMemoryAllocator template
   - MemoryManager singleton

3. **Phase-based refactoring plan**
   - Phase 1: Quick wins
   - Phase 2: Structural improvements  
   - Phase 3: Advanced optimizations
   - Phase 4: Observability

**Note:** This refactoring plan was superseded by the complete BssIoxPool redesign, which addresses all identified issues through a different architectural approach.

---

## 🎯 Why Not Refactor v1.x?

The decision to replace (rather than refactor) PoolAllocator was made because:

1. **Fundamental Architecture Limits**
   - Handle-based safety requires ground-up redesign
   - NUMA awareness conflicts with global pool model
   - Lock-free TCache incompatible with current synchronization

2. **Safety Requirements**
   - ECU applications require 100% overflow detection
   - memprotect integration needs region-based layout
   - Lease management requires different metadata structure

3. **Performance Gap**
   - Incremental improvements couldn't reach target (5-10x gain)
   - TCache integration would require rewriting 80% of code anyway
   - Per-core affinity needs different allocation strategy

4. **Complexity Reduction**
   - v1.x: 4 components, ~1500 LOC, complex interactions
   - v11.0: 2 components, ~2000 LOC, clearer separation

5. **Industry Alignment**
   - BssIoxPool design aligns with jemalloc/tcmalloc proven patterns
   - Handle-based approach matches modern safety requirements
   - Iceoryx2 compatibility essential for AUTOSAR AP

---

## 📊 Comparison Table

| Aspect | v1.x (PoolAllocator) | v11.0 (BssIoxPool) |
|--------|----------------------|---------------------|
| **Safety** | Optional MemoryTracker | Mandatory RedZone+Canary+memprotect |
| **Handle System** | Raw pointers | 8-byte handles (base_id:4 + offset:60) |
| **Thread Perf** | Global locks | Per-thread TCache + lock-free |
| **Memory Overhead** | 80B (tracked mode) | 24B + 128B RedZones |
| **Pool Sizes** | 4B - 1KB | 4B - 512B + region alloc |
| **NUMA Aware** | No | Yes (per-core affinity) |
| **Shared Memory** | Not supported | Iceoryx2 compatible |
| **Overflow Detection** | Optional | 100% mandatory |
| **Configuration** | JSON runtime | Build-time constants |
| **Components** | 4 (Manager, Pool, Tracker, Facade) | 2 (BssIox, TCache) |

---

## 🔗 Additional Resources

- **Migration Guide:** See [MEMORY_MANAGEMENT_GUIDE.md § Migration from Legacy](MEMORY_MANAGEMENT_GUIDE.md#migration-from-legacy)
- **Design Rationale:** See [BssIoxPool_TCache_Design.md § Goals](BssIoxPool_TCache_Design.md#1-目标与设计约束)
- **Performance Data:** See [MEMORY_MANAGEMENT_GUIDE.md § Performance](MEMORY_MANAGEMENT_GUIDE.md#performance-characteristics)

---

## 📞 Questions?

- **Technical Questions:** ddkv587@gmail.com
- **Migration Support:** LightAP Core Team
- **Security Concerns:** Security escalation channel

---

**Archive Date:** 2025-12-01  
**Reason:** Complete architectural replacement (v1.x → v11.0)  
**Retention:** Permanent (historical reference)
