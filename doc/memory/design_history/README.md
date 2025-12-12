# Design History & Evolution

This directory contains documents tracking the evolution of IOXPool architecture decisions and development plans.

---

## 📜 Documents in This Directory

### 1. AUTOSAR_REFACTORING_PLAN.md

**Purpose**: AUTOSAR AP R24-11 compliance roadmap

**Content**:
- Standards compliance requirements
- Refactoring tasks for R24-11 alignment
- API surface updates
- ErrorDomain improvements
- Memory management modernization plan

**Context**: This document guided the transition from legacy designs to modern AUTOSAR-compliant architecture

**Date**: [Original creation date]

---

### 2. IOXPOOL_V11_DEVELOPMENT_PLAN.md

**Purpose**: Original development plan for IoxPool v11.0

**Content**:
- Initial architecture vision (v11.0)
- Feature roadmap
- Implementation timeline
- Resource allocation

**Context**: This was the original plan that eventually evolved into v0.2.0 (TCMalloc) and then v0.3.0 (IOX TC)

**Historical Significance**: Shows early design thinking before TCMalloc integration

**Date**: [Original creation date]

---

### 3. CURRENT_DESIGN_QUICK_REF.md

**Purpose**: Quick reference for v0.2.0 (TCMalloc) architecture

**Content**:
- v0.2.0 architecture summary
- Key components (ResidentBackend, BurstAllocator)
- API reference
- Configuration options

**Context**: Created during v0.2.0 era as a developer quick reference. Now superseded by v0.3.0.

**Status**: ⚠️ Outdated - Refers to v0.2.0 design

**Current Equivalent**: `../v0.3.0/MEMORY_MANAGEMENT_GUIDE.md` (Overview section)

---

## 🔄 Design Evolution Timeline

```
2024-XX-XX: v11.0 Development Plan Created
    │       (IOXPOOL_V11_DEVELOPMENT_PLAN.md)
    ↓
2024-XX-XX: AUTOSAR R24-11 Refactoring Initiated
    │       (AUTOSAR_REFACTORING_PLAN.md)
    ↓
2025-XX-XX: v0.2.0 TCMalloc Integration Completed
    │       (CURRENT_DESIGN_QUICK_REF.md created)
    ↓
2025-11-12: Memory Refactoring Begins
    │       (Decision to remove TCMalloc dependency)
    ↓
2025-12-XX: v0.3.0 IOX TC Design Finalized
    │       (All documents in ../v0.3.0/ created)
    ↓
2025-12-11: Documentation Reorganization
            (This archive created)
```

---

## 📖 How to Use These Documents

### Understanding Design Decisions

**Question**: Why did we move from TCMalloc to IOX TC?

**Answer Path**:
1. Read `IOXPOOL_V11_DEVELOPMENT_PLAN.md` - Original vision
2. Read `../legacy/MEMORY_MANAGEMENT_GUIDE_v0.2.0_TCMalloc_ARCHIVED.md` - TCMalloc implementation
3. Read `../reports/MEMORY_REFACTORING_SUMMARY_2025-11-12.md` - Reasons for change
4. Read `../v0.3.0/REFACTORING_PLAN_JEMALLOC.md` - New approach

### Tracing Feature Evolution

**Example**: Thread-Local Cache (TC) design evolution

1. **Early Ideas**: `../legacy/BssIoxPool_TCache_Design.md` (initial TC concepts)
2. **v0.2.0**: `CURRENT_DESIGN_QUICK_REF.md` (TCMalloc Per-CPU cache)
3. **v0.3.0**: `../v0.3.0/MEMORY_MANAGEMENT_GUIDE.md` (IOX TC architecture)

### Understanding AUTOSAR Compliance

**Reference**: `AUTOSAR_REFACTORING_PLAN.md`

**Key Sections**:
- ErrorDomain modernization
- Memory management alignment
- API surface standardization
- R24-11 specific requirements

---

## ⚠️ Important Notes

### These Are Historical Documents

- ❌ Do not use as current design reference
- ❌ Do not implement features from these plans without verification
- ✅ Use for understanding design evolution
- ✅ Use for understanding "why" decisions were made

### Current Design Authority

**Always refer to**: `../v0.3.0/MEMORY_MANAGEMENT_GUIDE.md`

### Version Context

| Document | Relates to Version | Status |
|----------|-------------------|--------|
| IOXPOOL_V11_DEVELOPMENT_PLAN.md | Pre-v0.2.0 | 📜 Historical |
| CURRENT_DESIGN_QUICK_REF.md | v0.2.0 | 📜 Historical |
| AUTOSAR_REFACTORING_PLAN.md | All versions | 📜 Reference |
| ../v0.3.0/MEMORY_MANAGEMENT_GUIDE.md | v0.3.0 | ✅ **Current** |

---

## 🔗 Related Directories

- **Current Version**: [../v0.3.0/](../v0.3.0/) - Current v0.3.0 design
- **Legacy Docs**: [../legacy/](../legacy/) - Archived implementation docs
- **Reports**: [../reports/](../reports/) - Status and completion reports
- **Main README**: [../README.md](../README.md) - Documentation index

---

## 📚 Research Use Cases

### Academic/Research

These documents provide a case study of:
- Memory allocator evolution
- Technical debt management
- Dependency removal strategies
- Performance optimization decisions

### Training/Onboarding

New team members can trace the design evolution:
1. Start: `IOXPOOL_V11_DEVELOPMENT_PLAN.md` (original vision)
2. Middle: `CURRENT_DESIGN_QUICK_REF.md` (intermediate state)
3. Current: `../v0.3.0/MEMORY_MANAGEMENT_GUIDE.md` (final design)

---

**Archive Date**: 2025-12-11  
**Purpose**: Historical reference and design evolution tracking  
**Maintained**: Read-only, no updates
