# Core模块文档索引

📚 **最后更新**: 2026-02-02

---

## 📖 用户文档

### 主要指南
- [README.md](../README.md) - 项目概览和特性
- [README_CN.md](../README_CN.md) - 中文版README  
- [CHANGES.md](../CHANGES.md) - 变更日志

### 配置和使用
- [HMAC_SECRET_CONFIG.md](HMAC_SECRET_CONFIG.md) - 配置文件安全加密
- [IPC_DESIGN_ARCHITECTURE.md](IPC_DESIGN_ARCHITECTURE.md) - IPC 零拷贝设计架构 (331KB)

---

## 🔬 当前有效的技术文档

### 性能和优化
- [BENCHMARK_REPORT.md](BENCHMARK_REPORT.md) - 性能基准测试报告 (6.7KB)
- [ICEORYX2_VS_EPOLL.md](ICEORYX2_VS_EPOLL.md) - iceoryx2 vs epoll对比 (4.2KB)

### AUTOSAR合规
- [AUTOSAR_REFACTORING_PLAN.md](AUTOSAR_REFACTORING_PLAN.md) - AUTOSAR重构计划 (12.9KB)
- [AUTOSAR_AP_SWS_Core.pdf](AUTOSAR_AP_SWS_Core.pdf) - AUTOSAR AP标准文档

---

## 📊 测试和验证报告

### 最新报告 (2025-12-30)
- [reports/CODE_CLEANUP_REPORT.md](reports/CODE_CLEANUP_REPORT.md) - 代码清理报告
  - 删除902行冗余代码，移除4个重复测试文件，清理所有DEBUG输出

- [reports/ID_HANDLE_VALIDATION_SUMMARY.md](reports/ID_HANDLE_VALIDATION_SUMMARY.md) - ID-based Handle验证
  - iceoryx2风格的Handle机制，100%测试通过率，解决指针失效问题

- [reports/TEST_STRUCTURE_CLEANUP.md](reports/TEST_STRUCTURE_CLEANUP.md) - 测试结构梳理
  - 8个核心测试保留，职责清晰分离

---

## 🗄️ 归档文档 (archive/)

### 历史重构文档 (old_refactoring/)

已完成的重构项目（2024-2025）：

- [archive/old_refactoring/DUAL_COUNTER_REFACTORING_COMPLETION_REPORT.md](archive/old_refactoring/DUAL_COUNTER_REFACTORING_COMPLETION_REPORT.md) - Dual-Counter完成报告
- [archive/old_refactoring/DUAL_COUNTER_REFACTORING_SUMMARY.md](archive/old_refactoring/DUAL_COUNTER_REFACTORING_SUMMARY.md) - Dual-Counter总结
- [archive/old_refactoring/ICEORYX2_ARCHITECTURE_FIX.md](archive/old_refactoring/ICEORYX2_ARCHITECTURE_FIX.md) - iceoryx2架构修复
- [archive/old_refactoring/ICEORYX2_REFACTORING_SUMMARY.md](archive/old_refactoring/ICEORYX2_REFACTORING_SUMMARY.md) - iceoryx2重构总结
- [archive/old_refactoring/LOCKFREE_OPTIMIZATION_REPORT.md](archive/old_refactoring/LOCKFREE_OPTIMIZATION_REPORT.md) - Lock-Free优化报告
- [archive/old_refactoring/MEMORY_REFACTORING.md](archive/old_refactoring/MEMORY_REFACTORING.md) - 内存管理重构
- [archive/old_refactoring/CRITICAL_RESOURCE_ANALYSIS.md](archive/old_refactoring/CRITICAL_RESOURCE_ANALYSIS.md) - 关键资源分析

### 配置和内存管理
- [archive/memory_alignment_audit.md](archive/memory_alignment_audit.md) - 内存对齐审计 (14KB)
- [archive/alignment_optimization_summary.md](archive/alignment_optimization_summary.md) - 对齐优化总结 (7.1KB)
- [archive/OLD_MEMORY_POOL_QUICK_START.md](archive/OLD_MEMORY_POOL_QUICK_START.md) - 旧版内存池快速指南
- [archive/JEMALLOC_VERIFICATION.md](archive/JEMALLOC_VERIFICATION.md) - jemalloc验证

### AUTOSAR与标准合规
- [archive/ERRORDOMAIN_AUTOSAR_COMPLIANCE.md](archive/ERRORDOMAIN_AUTOSAR_COMPLIANCE.md) - ErrorDomain合规 (9.8KB)
- [archive/ERROR_DOMAIN_LIFECYCLE_IMPROVEMENT.md](archive/ERROR_DOMAIN_LIFECYCLE_IMPROVEMENT.md) - ErrorDomain生命周期改进 (7.0KB)
- [archive/AUTOSAR_UTILITIES_SUMMARY.md](archive/AUTOSAR_UTILITIES_SUMMARY.md) - AUTOSAR工具类总结 (4.6KB)

### 集成和测试
- [archive/IMP_OPERATOR_NEW_TEST_REPORT.md](archive/IMP_OPERATOR_NEW_TEST_REPORT.md) - IMP_OPERATOR_NEW测试报告 (6.9KB)
- [archive/IMP_OPERATOR_NEW_SUMMARY.md](archive/IMP_OPERATOR_NEW_SUMMARY.md) - IMP_OPERATOR_NEW集成总结 (5.6KB)
- [archive/CAbort_Refactoring_Summary.md](archive/CAbort_Refactoring_Summary.md) - CAbort重构总结

### 阶段完成和分析
- [archive/Phase1_COMPLETION_REPORT.md](archive/Phase1_COMPLETION_REPORT.md) - Phase 1完成报告 (11KB)
- [archive/IMPROVEMENT_PROPOSAL.md](archive/IMPROVEMENT_PROPOSAL.md) - 改进提案 (15KB)

### 当前状态监控
- [archive/current/OVERNIGHT_TEST_STATUS.md](archive/current/OVERNIGHT_TEST_STATUS.md) - 长期稳定性测试状态
- [archive/current/MEMORY_OPTIONS.md](archive/current/MEMORY_OPTIONS.md) - 内存选项配置

### 归档说明
完整的归档文档列表和管理规则请参见：[archive/README.md](archive/README.md)

---

## 🗂️ 文档组织结构

```
Core/
├── README.md                    # 主文档
├── README_CN.md                # 中文版
├── CHANGES.md                   # 变更日志
│
└── doc/                         # 文档目录
    ├── INDEX.md                 # 📍 本文档（文档导航入口）
    │
    ├── *.md                     # 当前有效的技术文档
    │   ├── AUTOSAR_REFACTORING_PLAN.md
    │   ├── BENCHMARK_REPORT.md
    │   ├── HMAC_SECRET_CONFIG.md
    │   ├── ICEORYX2_VS_EPOLL.md
    │   └── IPC_DESIGN_ARCHITECTURE.md
    │
    ├── reports/                 # 📊 测试和清理报告
    │   ├── CODE_CLEANUP_REPORT.md
    │   ├── ID_HANDLE_VALIDATION_SUMMARY.md
    │   └── TEST_STRUCTURE_CLEANUP.md
    │
    └── archive/                 # 🗄️ 归档文档
        ├── README.md            # 归档说明
        ├── current/             # 当前监控状态
        ├── old_refactoring/     # 历史重构记录
        └── *.md                 # 其他归档文档
```

---

## 🎯 快速导航

### 我想...

**了解Core模块的基本功能？**  
→ [README.md](../README.md) / [README_CN.md](../README_CN.md)

**了解IPC零拷贝设计？**  
→ [IPC_DESIGN_ARCHITECTURE.md](IPC_DESIGN_ARCHITECTURE.md)

**查看性能基准测试？**  
→ [BENCHMARK_REPORT.md](BENCHMARK_REPORT.md)

**了解最新代码清理？**  
→ [reports/CODE_CLEANUP_REPORT.md](reports/CODE_CLEANUP_REPORT.md)

**了解ID-based Handle机制？**  
→ [reports/ID_HANDLE_VALIDATION_SUMMARY.md](reports/ID_HANDLE_VALIDATION_SUMMARY.md)

**了解配置文件加密？**  
→ [HMAC_SECRET_CONFIG.md](HMAC_SECRET_CONFIG.md)

**查看AUTOSAR重构计划？**  
→ [AUTOSAR_REFACTORING_PLAN.md](AUTOSAR_REFACTORING_PLAN.md)

**了解历史演进？**  
→ [archive/old_refactoring/](archive/old_refactoring/) / [archive/README.md](archive/README.md)

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
