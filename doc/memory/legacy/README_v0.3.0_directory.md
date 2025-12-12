# v0.3.0 - IOX Deterministic Memory Pool

**Version**: 0.3.0  
**Status**: ✅ Design Complete, Ready for Implementation  
**Date**: 2025-12-11

---

## 📚 Documents in This Directory

### 1. MEMORY_MANAGEMENT_GUIDE.md (6300+ lines)

**Primary architecture specification for IOXPool v0.3.0**

**Key Sections**:
- Overview & Design Principles
- Critical Design Constraints (3 constraints)
- Three-Tier Architecture
- Layer 2 Data Structures (ResidentRegionHeader v3, ThreadCache, TCBin, Extent, BurstBlock)
- IOX Thread-Local Cache (TC) Architecture (9 sections)
- Dynamic Extent Management
- Performance Characteristics
- Configuration Reference
- Version History & Migration

**Status**: ✅ Complete, validated, all design defects fixed

---

### 2. IMPLEMENTATION_PLAN_v0.3.0.md

**30-day development roadmap with complete code examples**

**7 Phases**:
- Phase 1: Infrastructure (5 days) - Directory structure, CMake, tests
- Phase 2: Size Classes (2 days) - 32 size classes + lookup table
- Phase 3: Thread-Local Cache (5 days) - Fast Path implementation
- Phase 4: Dynamic Extent (5 days) - Bitmap allocation
- Phase 5: Integration (5 days) - ResidentRegion, bootstrap
- Phase 6: Testing (5 days) - Benchmarks, validation
- Phase 7: Delivery (3 days) - Documentation, release

**Includes**: Code templates, unit tests, benchmarks, risk management

**Status**: ✅ Ready to execute

---

### 3. REFACTORING_PLAN_JEMALLOC.md (1590 lines)

**Technical migration plan from v0.2.0 (TCMalloc) to v0.3.0 (IOX TC)**

**Content**:
- Detailed comparison (TCMalloc vs IOX TC)
- Design rationale (why jemalloc-inspired)
- 8-chapter migration checklist
- Implementation specifications
- Performance analysis

**Status**: ✅ Complete

---

### 4. DESIGN_CONSISTENCY_REPORT.md

**Comprehensive design validation and constraint verification**

**Findings**:
- ✅ Fixed 3 critical design defects:
  1. TCBin: void** → uint32_t block_offsets[128]
  2. Extent: uint8_t* bitmap → uint8_t allocation_bitmap[512]
  3. Bootstrap logic corrected (Header@Chunk0, Chunk63 temporary)
- ✅ 100% constraint compliance verified
- ✅ Architecture integrity confirmed
- ✅ Implementation feasibility validated

**Status**: ✅ All issues resolved

---

### 5. CHANGELOG_v0.3.0.md

**Version changelog and breaking changes**

**Major Changes**:
- Removed TCMalloc dependency
- New IOX TC architecture (32 size classes, 8B-1024B)
- Fixed-size data structures (Constraint 1 compliance)
- Compile-time Burst Region control
- Performance improvements (+33% throughput)
- Fragmentation reduction (5-8% internal, 3-8% external)

---

## 🎯 Key Design Constraints

### Constraint 1: No STL/malloc/free

**Rationale**: Prevent recursive malloc() deadlock when overloading global operator new/delete

**Solutions**:
- ✅ Use fixed-size arrays (TCBin.block_offsets[128], Extent.allocation_bitmap[512])
- ✅ Use intrusive data structures (Extent.next pointer within struct)
- ✅ Bootstrap allocation from Chunk 63 (no malloc)

### Constraint 2: Burst Region Optional

**Rationale**: Embedded systems may not need >2MB allocations

**Solutions**:
- ✅ Compile-time flag: IOX_ENABLE_BURST_REGION (default: OFF)
- ✅ Graceful failure when disabled (return handle=0)
- ✅ Link-time configuration: IOX_BURST_SIZE

### Constraint 3: Chunk 63 Bootstrap Release

**Rationale**: Zero-waste initialization

**Solutions**:
- ✅ Header at Chunk 0 (permanent)
- ✅ Chunk 63 for temporary bootstrap data
- ✅ Release Chunk 63 after init → available for Dynamic Extent
- ✅ Effective capacity: 128 MiB (all 64 chunks)

---

## 🚀 Architecture Highlights

### Three-Tier Design

```
Layer 3: Adapter API (iox_alloc, iox_free, operator new/delete)
    ↓
Layer 2: Allocator Core (TC ≤1024B | Extent 1KB-2MB | Burst >2MB)
    ↓
Layer 1: Memory Regions (Resident 128MB | Burst 0-2GB)
```

### Performance Targets

| Metric | v0.2.0 (TCMalloc) | v0.3.0 (IOX TC) | Improvement |
|--------|-------------------|-----------------|-------------|
| TC Fast Path (P50) | 10ns | 10-15ns | Maintained |
| Extent (P50) | 100ns | 50-120ns | -40% |
| Throughput | 85M/sec | 113M/sec | +33% |
| Internal Frag | 15-25% | 5-8% | -70% |
| External Frag | 10-20% | 3-8% | -65% |

### Memory Layout

```
Resident Region (128 MiB, 64 chunks × 2MB):
├── Chunk 0: Header (ResidentRegionHeader v3)
├── Chunks 1-3: TC Slabs (per-thread 64KB)
├── Chunks 4-62: Dynamic Extent (bitmap-based slab allocation)
└── Chunk 63: Bootstrap (temporary, released after init)

Burst Region (optional, 0-2GB):
└── Freelist-managed large blocks (>2MB)
```

---

## 📖 Reading Order for Developers

1. **Start**: MEMORY_MANAGEMENT_GUIDE.md - Overview & Design Principles
2. **Understand Constraints**: Section "⚠️ IOXPool Implementation Constraints"
3. **Learn Architecture**: Section "Three-Tier Architecture"
4. **Study Data Structures**: Section "Layer 2: Memory Allocator Core"
5. **Deep Dive TC**: Section "⚡ IOX Thread-Local Cache (TC) Architecture"
6. **Plan Implementation**: IMPLEMENTATION_PLAN_v0.3.0.md
7. **Verify Understanding**: DESIGN_CONSISTENCY_REPORT.md

---

## 🔗 Related Documents

- **Previous Version**: [../legacy/MEMORY_MANAGEMENT_GUIDE_v0.2.0_TCMalloc_ARCHIVED.md](../legacy/MEMORY_MANAGEMENT_GUIDE_v0.2.0_TCMalloc_ARCHIVED.md)
- **Design History**: [../design_history/](../design_history/)
- **Reports**: [../reports/](../reports/)

---

**Status**: Ready for Phase 1 implementation (Infrastructure setup)  
**Next Action**: Execute IMPLEMENTATION_PLAN_v0.3.0.md Phase 1 (Days 1-5)
