# Core 模块文档索引

📚 **最后更新**: 2026-03-04

---

## 📖 用户文档

### 主要指南
- [README.md](../README.md) — 模块概览与特性（英文）
- [README_CN.md](../README_CN.md) — 模块概览与特性（中文）
- [CHANGES.md](../CHANGES.md) — 变更日志

### 配置与使用
- [HMAC_SECRET_CONFIG.md](HMAC_SECRET_CONFIG.md) — 配置文件 HMAC 安全加密
- [MEMORY_OPTIONS.md](MEMORY_OPTIONS.md) — 内存管理构建选项（System / Pool / jemalloc）

---

## 🔬 技术文档

### IPC 设计
- [IPC_DESIGN_ARCHITECTURE.md](IPC_DESIGN_ARCHITECTURE.md) — CoreIPC 零拷贝设计架构（详细设计文档）

### 性能分析
- [BENCHMARK_REPORT.md](BENCHMARK_REPORT.md) — 性能基准测试报告

### AUTOSAR 规范
- [AUTOSAR_AP_SWS_Core.pdf](AUTOSAR_AP_SWS_Core.pdf) — AUTOSAR AP SWS Core 标准文档

---

## 🗄️ 归档文档 (archive/)

所有历史文档已归档至 [archive/](archive/) 目录（扁平结构），包括已完成的重构记录、旧设计对比、一次性报告等。

### 旧设计与对比
- [ICEORYX2_VS_EPOLL.md](archive/ICEORYX2_VS_EPOLL.md) — iceoryx2 vs epoll 对比（已过时，IPC 已使用 CoreIPC）
- [AUTOSAR_REFACTORING_PLAN.md](archive/AUTOSAR_REFACTORING_PLAN.md) — AUTOSAR R24-11 重构计划（已过时，项目已升级至 R25-11）

### 历史重构
- [ICEORYX2_ARCHITECTURE_FIX.md](archive/ICEORYX2_ARCHITECTURE_FIX.md) — iceoryx2 架构修复
- [ICEORYX2_REFACTORING_SUMMARY.md](archive/ICEORYX2_REFACTORING_SUMMARY.md) — iceoryx2 重构总结
- [DUAL_COUNTER_REFACTORING_COMPLETION_REPORT.md](archive/DUAL_COUNTER_REFACTORING_COMPLETION_REPORT.md) — Dual-Counter 完成报告
- [DUAL_COUNTER_REFACTORING_SUMMARY.md](archive/DUAL_COUNTER_REFACTORING_SUMMARY.md) — Dual-Counter 总结
- [LOCKFREE_OPTIMIZATION_REPORT.md](archive/LOCKFREE_OPTIMIZATION_REPORT.md) — Lock-Free 优化报告
- [MEMORY_REFACTORING.md](archive/MEMORY_REFACTORING.md) — 内存管理重构
- [CRITICAL_RESOURCE_ANALYSIS.md](archive/CRITICAL_RESOURCE_ANALYSIS.md) — 关键资源分析

### 内存与对齐
- [memory_alignment_audit.md](archive/memory_alignment_audit.md) — 内存对齐审计
- [alignment_optimization_summary.md](archive/alignment_optimization_summary.md) — 对齐优化总结
- [OLD_MEMORY_POOL_QUICK_START.md](archive/OLD_MEMORY_POOL_QUICK_START.md) — 旧版内存池快速指南
- [JEMALLOC_VERIFICATION.md](archive/JEMALLOC_VERIFICATION.md) — jemalloc 验证

### AUTOSAR 合规
- [ERRORDOMAIN_AUTOSAR_COMPLIANCE.md](archive/ERRORDOMAIN_AUTOSAR_COMPLIANCE.md) — ErrorDomain 合规
- [ERROR_DOMAIN_LIFECYCLE_IMPROVEMENT.md](archive/ERROR_DOMAIN_LIFECYCLE_IMPROVEMENT.md) — ErrorDomain 生命周期改进
- [AUTOSAR_UTILITIES_SUMMARY.md](archive/AUTOSAR_UTILITIES_SUMMARY.md) — AUTOSAR 工具类总结

### 集成与测试
- [IMP_OPERATOR_NEW_SUMMARY.md](archive/IMP_OPERATOR_NEW_SUMMARY.md) — IMP_OPERATOR_NEW 集成总结
- [IMP_OPERATOR_NEW_TEST_REPORT.md](archive/IMP_OPERATOR_NEW_TEST_REPORT.md) — IMP_OPERATOR_NEW 测试报告
- [CAbort_Refactoring_Summary.md](archive/CAbort_Refactoring_Summary.md) — CAbort 重构总结

### 完成报告
- [Phase1_COMPLETION_REPORT.md](archive/Phase1_COMPLETION_REPORT.md) — Phase 1 完成报告
- [IMPROVEMENT_PROPOSAL.md](archive/IMPROVEMENT_PROPOSAL.md) — 改进提案
- [CODE_CLEANUP_REPORT.md](archive/CODE_CLEANUP_REPORT.md) — 代码清理报告 (2025-12-30)
- [ID_HANDLE_VALIDATION_SUMMARY.md](archive/ID_HANDLE_VALIDATION_SUMMARY.md) — ID-based Handle 验证
- [TEST_STRUCTURE_CLEANUP.md](archive/TEST_STRUCTURE_CLEANUP.md) — 测试结构梳理
- [OVERNIGHT_TEST_STATUS.md](archive/OVERNIGHT_TEST_STATUS.md) — 8 小时过夜压力测试

详细列表见 [archive/README.md](archive/README.md)

---

## 🗂️ 目录结构

```
Core/
├── README.md / README_CN.md     # 模块文档
├── CHANGES.md                   # 变更日志
│
└── doc/
    ├── INDEX.md                 # 📍 本文档（导航入口）
    ├── README.md                # 文档说明
    │
    ├── IPC_DESIGN_ARCHITECTURE.md   # CoreIPC 零拷贝设计
    ├── BENCHMARK_REPORT.md          # 性能基准测试
    ├── HMAC_SECRET_CONFIG.md        # HMAC 配置指南
    ├── MEMORY_OPTIONS.md            # 内存管理选项
    ├── AUTOSAR_AP_SWS_Core.pdf      # AUTOSAR 标准文档
    │
    └── archive/                 # 🗄️ 历史归档（扁平结构）
        ├── README.md
        └── *.md
```

---

## 🎯 快速导航

| 我想... | 去看 |
|---------|------|
| 了解 Core 模块功能 | [README.md](../README.md) / [README_CN.md](../README_CN.md) |
| 了解 IPC 零拷贝设计 | [IPC_DESIGN_ARCHITECTURE.md](IPC_DESIGN_ARCHITECTURE.md) |
| 查看性能基准测试 | [BENCHMARK_REPORT.md](BENCHMARK_REPORT.md) |
| 了解配置文件加密 | [HMAC_SECRET_CONFIG.md](HMAC_SECRET_CONFIG.md) |
| 了解内存管理选项 | [MEMORY_OPTIONS.md](MEMORY_OPTIONS.md) |
| 了解历史演进 | [archive/README.md](archive/README.md) |

---

## 📝 文档维护指南

### 添加新文档

| 文档类型 | 存放位置 | 命名规范 |
|---------|---------|---------|
| 设计文档 | `doc/` | `FEATURE_NAME.md` |
| 配置指南 | `doc/` | `CONFIG_NAME.md` |
| 归档文档 | `doc/archive/` | 保持原名 |

### 归档规则

当某个文档内容被替代或已过时时：
1. 将文档移至 `archive/`
2. 更新本 INDEX.md
3. 更新 `archive/README.md`

---

*📍 本文档是 Core 模块的文档导航入口。*
