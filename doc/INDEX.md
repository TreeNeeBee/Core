# Core模块文档索引

📚 **最后更新**: 2025-12-30

---

## 📖 用户文档

### 主要指南
- [README.md](../README.md) - 项目概览和特性
- [README_CN.md](../README_CN.md) - 中文版README  
- [BUILDING.md](../BUILDING.md) - 编译和构建指南
- [CHANGES.md](../CHANGES.md) - 变更日志
- [RELEASE_NOTES.md](../RELEASE_NOTES.md) - 发布说明
- [RELEASE_NOTES_CN.md](../RELEASE_NOTES_CN.md) - 中文版发布说明

### 配置和使用
- [current/MEMORY_OPTIONS.md](current/MEMORY_OPTIONS.md) - 内存管理配置选项
- [HMAC_SECRET_CONFIG.md](HMAC_SECRET_CONFIG.md) - 配置文件安全加密
- [QUICK_START.md](QUICK_START.md) - 快速开始指南
- [MEMORY_MANAGEMENT_GUIDE.md](MEMORY_MANAGEMENT_GUIDE.md) - 内存管理完整指南

---

## 🔬 当前有效的技术文档

### SharedMemory设计 (iceoryx2架构)
- [LOCK_FREE_ICEORYX2_IMPLEMENTATION.md](LOCK_FREE_ICEORYX2_IMPLEMENTATION.md) - Lock-free iceoryx2实现
- [ICEORYX2_COMPLETE_IMPLEMENTATION.md](ICEORYX2_COMPLETE_IMPLEMENTATION.md) - 完整实现文档
- [SHARED_MEMORY_CONCURRENCY_ANALYSIS.md](SHARED_MEMORY_CONCURRENCY_ANALYSIS.md) - 并发分析
- [SHM_ARCHITECTURE_ANALYSIS.md](SHM_ARCHITECTURE_ANALYSIS.md) - 架构分析

### 性能和优化
- [BENCHMARK_REPORT.md](BENCHMARK_REPORT.md) - 性能基准测试报告
- [ICEORYX2_VS_EPOLL.md](ICEORYX2_VS_EPOLL.md) - iceoryx2 vs epoll对比
- [JEMALLOC_VS_MALLOC_COMPARISON.md](JEMALLOC_VS_MALLOC_COMPARISON.md) - jemalloc性能对比
- [JEMALLOC_VERIFICATION.md](JEMALLOC_VERIFICATION.md) - jemalloc验证报告

### 内存管理
- [MEMORY_POOL_CONFIG_MIGRATION.md](MEMORY_POOL_CONFIG_MIGRATION.md) - 内存池配置迁移
- [MEMORY_POOL_CONFIG_VERIFICATION.md](MEMORY_POOL_CONFIG_VERIFICATION.md) - 配置验证
- [MEMORY_POOL_GROWTH_QUICK_REFERENCE.md](MEMORY_POOL_GROWTH_QUICK_REFERENCE.md) - 快速参考
- [MEMORY_POOL_QUICK_CARD.md](MEMORY_POOL_QUICK_CARD.md) - 速查卡片

### Dual-Counter机制
- [DUAL_COUNTER_ANALYSIS.md](DUAL_COUNTER_ANALYSIS.md) - Dual-counter分析
- [DUAL_COUNTER_PROGRESS.md](DUAL_COUNTER_PROGRESS.md) - 实现进度
- [DUAL_COUNTER_REFACTORING_PLAN.md](DUAL_COUNTER_REFACTORING_PLAN.md) - 重构计划

### AUTOSAR合规
- [AUTOSAR_REFACTORING_PLAN.md](AUTOSAR_REFACTORING_PLAN.md) - AUTOSAR重构计划
- [R24_11_FEATURES_COMPLETION_REPORT.md](R24_11_FEATURES_COMPLETION_REPORT.md) - R24-11特性报告
- [PHASE1_COMPLETION_REPORT.md](PHASE1_COMPLETION_REPORT.md) - Phase 1完成报告
- [AUTOSAR_AP_SWS_Core.pdf](AUTOSAR_AP_SWS_Core.pdf) - AUTOSAR AP标准文档

---

## 📊 测试和验证报告

### 最新报告 (2025-12-30)
- [reports/CODE_CLEANUP_REPORT.md](reports/CODE_CLEANUP_REPORT.md) - 代码清理报告
  - 删除902行冗余代码
  - 移除4个重复测试文件
  - 清理所有DEBUG输出

- [reports/ID_HANDLE_VALIDATION_SUMMARY.md](reports/ID_HANDLE_VALIDATION_SUMMARY.md) - ID-based Handle验证
  - iceoryx2风格的Handle机制
  - 100%测试通过率
  - 解决了指针失效问题

- [reports/TEST_STRUCTURE_CLEANUP.md](reports/TEST_STRUCTURE_CLEANUP.md) - 测试结构梳理
  - 8个核心测试保留
  - 职责清晰分离

### 运行状态
- [current/OVERNIGHT_TEST_STATUS.md](current/OVERNIGHT_TEST_STATUS.md) - 长期稳定性测试状态

---

## 🗄️ 归档文档

### 历史重构文档 (archive/old_refactoring/)

已完成的重构项目（2024-2025）：

#### Dual-Counter架构演进
- [DUAL_COUNTER_REFACTORING_COMPLETION_REPORT.md](archive/old_refactoring/DUAL_COUNTER_REFACTORING_COMPLETION_REPORT.md)
- [DUAL_COUNTER_REFACTORING_SUMMARY.md](archive/old_refactoring/DUAL_COUNTER_REFACTORING_SUMMARY.md)

#### iceoryx2迁移
- [ICEORYX2_ARCHITECTURE_FIX.md](archive/old_refactoring/ICEORYX2_ARCHITECTURE_FIX.md)
- [ICEORYX2_REFACTORING_SUMMARY.md](archive/old_refactoring/ICEORYX2_REFACTORING_SUMMARY.md)

#### Lock-Free优化
- [LOCKFREE_OPTIMIZATION_REPORT.md](archive/old_refactoring/LOCKFREE_OPTIMIZATION_REPORT.md)

#### 内存管理演进
- [MEMORY_REFACTORING.md](archive/old_refactoring/MEMORY_REFACTORING.md)
- [CRITICAL_RESOURCE_ANALYSIS.md](archive/old_refactoring/CRITICAL_RESOURCE_ANALYSIS.md)

这些文档记录了SharedMemoryAllocator从最初的设计到当前**ID-based Handle机制**的完整演进过程。

### 其他归档
- [archive/](archive/) - 更多历史文档和设计草案

---

## 🗂️ 文档组织结构

```
Core/
├── README.md                    # 主文档
├── BUILDING.md                  # 编译指南
├── CHANGES.md                   # 变更日志
├── RELEASE_NOTES.md            # 发布说明
│
└── doc/                         # 文档目录
    ├── INDEX.md                 # 📍 本文档
    │
    ├── current/                 # ✅ 当前有效配置和状态
    │   ├── MEMORY_OPTIONS.md            # 内存配置选项
    │   └── OVERNIGHT_TEST_STATUS.md     # 测试状态
    │
    ├── reports/                 # 📊 测试和清理报告 (2025-12-30)
    │   ├── CODE_CLEANUP_REPORT.md
    │   ├── ID_HANDLE_VALIDATION_SUMMARY.md
    │   └── TEST_STRUCTURE_CLEANUP.md
    │
    ├── archive/                 # 🗄️ 归档文档
    │   └── old_refactoring/     # 历史重构记录
    │       ├── DUAL_COUNTER_*.md
    │       ├── ICEORYX2_*.md
    │       ├── LOCKFREE_*.md
    │       ├── MEMORY_*.md
    │       └── CRITICAL_*.md
    │
    └── [设计文档]               # 当前有效的设计文档
        ├── LOCK_FREE_ICEORYX2_IMPLEMENTATION.md
        ├── SHARED_MEMORY_CONCURRENCY_ANALYSIS.md
        ├── BENCHMARK_REPORT.md
        └── ...
```

---

## 🎯 快速导航

### 我想...

**了解Core模块的基本功能？**  
→ [README.md](../README.md)

**开始编译和使用？**  
→ [BUILDING.md](../BUILDING.md) 和 [QUICK_START.md](QUICK_START.md)

**配置内存管理？**  
→ [current/MEMORY_OPTIONS.md](current/MEMORY_OPTIONS.md)

**了解SharedMemory的设计？**  
→ [LOCK_FREE_ICEORYX2_IMPLEMENTATION.md](LOCK_FREE_ICEORYX2_IMPLEMENTATION.md)

**查看最新的代码变更？**  
→ [reports/CODE_CLEANUP_REPORT.md](reports/CODE_CLEANUP_REPORT.md)

**了解ID-based Handle机制？**  
→ [reports/ID_HANDLE_VALIDATION_SUMMARY.md](reports/ID_HANDLE_VALIDATION_SUMMARY.md)

**查看测试结构？**  
→ [reports/TEST_STRUCTURE_CLEANUP.md](reports/TEST_STRUCTURE_CLEANUP.md)

**了解历史演进？**  
→ [archive/old_refactoring/](archive/old_refactoring/)

**查看长期测试状态？**  
→ [current/OVERNIGHT_TEST_STATUS.md](current/OVERNIGHT_TEST_STATUS.md)

---

## 📝 文档维护指南

### 添加新文档

| 文档类型 | 存放位置 | 命名规范 |
|---------|---------|---------|
| 设计文档 | `doc/` | `FEATURE_NAME.md` |
| 测试报告 | `doc/reports/` | `REPORT_NAME.md` |
| 配置指南 | `doc/current/` | `CONFIG_NAME.md` |
| 归档文档 | `doc/archive/[category]/` | 保持原名 |

### 归档规则

当某个设计被完全替代时：
1. 将文档移至 `archive/[category]/`
2. 在文档开头添加归档说明：
   ```markdown
   > ⚠️ **已归档** (YYYY-MM-DD)
   > 
   > 本设计已被 [新设计](../NEW_DESIGN.md) 替代。
   > 保留此文档仅供历史参考。
   ```
3. 更新本INDEX.md

### 文档分类

- **设计文档**: 架构、实现细节、API设计
- **测试报告**: 验证结果、性能测试、清理报告
- **配置指南**: 使用说明、配置选项、最佳实践
- **归档文档**: 已完成的重构、过时的设计

---

## 📚 参考资料

### 外部标准
- [AUTOSAR Adaptive Platform R24-11](https://www.autosar.org/)
- [iceoryx2](https://github.com/eclipse-iceoryx/iceoryx2) - 零拷贝IPC框架

### 相关工具
- [jemalloc](http://jemalloc.net/) - 内存分配器
- [Google Test](https://github.com/google/googletest) - 单元测试框架

---

*📍 提示: 本文档是Core模块的文档导航入口，定期更新以反映最新的文档结构。*
