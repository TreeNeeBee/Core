# Legacy Documentation Archive

This directory contains archived versions of IOXPool memory management documentation.

---

## 📦 Archived Versions

### 1. MEMORY_MANAGEMENT_GUIDE_v0.2.0_TCMalloc_ARCHIVED.md (5302 lines)

**Version**: 0.2.0 (TCMalloc Integration)  
**Archived**: 2025-12-11  
**Reason**: Replaced by v0.3.0 IOX Deterministic Pool

**Key Features**:
- External TCMalloc dependency (google/tcmalloc)
- Per-CPU cache with rseq kernel support
- 88 size classes (TCMalloc's default)
- 256KB allocation threshold
- ResidentBackend + BurstAllocator architecture

**Why Replaced**:
- ❌ External dependency maintenance burden
- ❌ High fragmentation (15-25% internal, 10-20% external)
- ❌ Limited control over allocator behavior
- ❌ Kernel rseq requirement (Linux-specific)
- ❌ Overkill for embedded workloads

**Reference**: Keep for v0.2.0 vs v0.3.0 comparison and migration context

---

### 2. MEMORY_MANAGEMENT_GUIDE_v0.1.0_LEGACY.md

**Version**: 0.1.0 (Original PoolAllocator)  
**Archived**: [Date Unknown]  
**Reason**: Replaced by v0.2.0 TCMalloc

**Key Features**:
- Simple pool allocator design
- Fixed-size block allocation
- Basic slab management

**Why Replaced**:
- Limited scalability
- High fragmentation for variable-size allocations
- No thread-local caching

---

### 3. MEMORY_MANAGEMENT_GUIDE_LEGACY_PoolAllocator.md

**Description**: Early PoolAllocator design notes

**Content**:
- Original pool allocator concepts
- Fixed-block allocation strategies
- Memory layout proposals

---

### 4. BssIoxPool_TCache_Design.md

**Description**: Early thread-cache design exploration

**Content**:
- Initial TC (Thread-Cache) concepts
- BSS section memory layout
- Pre-v0.2.0 design experiments

**Note**: Ideas from this document influenced v0.3.0 IOX TC design

---

### 5. MEMORY_ARCHIVE_NOTICE.md

**Description**: Explanation of why documents were archived

**Content**:
- Archival rationale
- Version migration guidance
- Historical context

---

## 🔄 Version Evolution

```
v0.1.0 (PoolAllocator)
   ↓ (Scalability & performance needs)
v0.2.0 (TCMalloc Integration)
   ↓ (Dependency & fragmentation issues)
v0.3.0 (IOX Deterministic Pool) ← Current
```

---

## 📖 When to Reference These Documents

### Use Case 1: Performance Comparison

Compare v0.2.0 TCMalloc metrics with v0.3.0 IOX TC:
- Read: `MEMORY_MANAGEMENT_GUIDE_v0.2.0_TCMalloc_ARCHIVED.md`
- Compare with: `../v0.3.0/MEMORY_MANAGEMENT_GUIDE.md`

### Use Case 2: Migration Questions

Understanding what changed from v0.2.0 → v0.3.0:
- Read: `MEMORY_MANAGEMENT_GUIDE_v0.2.0_TCMalloc_ARCHIVED.md`
- Then: `../v0.3.0/REFACTORING_PLAN_JEMALLOC.md`

### Use Case 3: Design Rationale

Why certain decisions were made:
- Read: `BssIoxPool_TCache_Design.md` (early TC ideas)
- Then: `../v0.3.0/MEMORY_MANAGEMENT_GUIDE.md` (Section: IOX TC Architecture)

### Use Case 4: Historical Context

Understanding the evolution:
- Read: All archived versions in chronological order
- Then: `../design_history/` for decision records

---

## ⚠️ Important Notes

### These Documents Are Read-Only

- ❌ Do not modify archived documents
- ❌ Do not use as current reference
- ✅ Use for historical comparison only
- ✅ Cite version number when referencing

### Current Documentation

**Always refer to**: `../v0.3.0/MEMORY_MANAGEMENT_GUIDE.md` for current design

### Version Mapping

| File Name | Version | Status |
|-----------|---------|--------|
| MEMORY_MANAGEMENT_GUIDE_v0.1.0_LEGACY.md | v0.1.0 | 📦 Archived |
| MEMORY_MANAGEMENT_GUIDE_v0.2.0_TCMalloc_ARCHIVED.md | v0.2.0 | 📦 Archived |
| BssIoxPool_TCache_Design.md | Pre-v0.2.0 | 📦 Archived |
| ../v0.3.0/MEMORY_MANAGEMENT_GUIDE.md | v0.3.0 | ✅ **Current** |

---

## 🔗 Related Directories

- **Current Version**: [../v0.3.0/](../v0.3.0/)
- **Design History**: [../design_history/](../design_history/)
- **Reports**: [../reports/](../reports/)
- **Main README**: [../README.md](../README.md)

---

**Archive Date**: 2025-12-11  
**Archived By**: LightAP Core Team  
**Reason**: Replaced by v0.3.0 IOX Deterministic Pool design
