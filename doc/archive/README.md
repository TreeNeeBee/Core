# 文档归档

本目录保存 Core 模块的历史技术文档，采用**扁平结构**（无子目录）。

## 用途

归档文档作为：
- **历史记录**：实现决策与设计演进
- **参考资料**：已完成工作的技术细节
- **知识库**：理解当前架构的上下文

## 归档文档清单

### 旧设计与对比
| 文件 | 说明 |
|------|------|
| `ICEORYX2_VS_EPOLL.md` | iceoryx2 vs epoll 对比（IPC 已使用 CoreIPC，此文档已过时） |
| `AUTOSAR_REFACTORING_PLAN.md` | AUTOSAR R24-11 重构计划（项目已升级至 R25-11） |

### 历史重构记录
| 文件 | 说明 |
|------|------|
| `ICEORYX2_ARCHITECTURE_FIX.md` | iceoryx2 架构修复 |
| `ICEORYX2_REFACTORING_SUMMARY.md` | iceoryx2 重构总结 |
| `DUAL_COUNTER_REFACTORING_COMPLETION_REPORT.md` | Dual-Counter 重构完成报告 |
| `DUAL_COUNTER_REFACTORING_SUMMARY.md` | Dual-Counter 架构总结 |
| `LOCKFREE_OPTIMIZATION_REPORT.md` | Lock-Free 优化报告 |
| `MEMORY_REFACTORING.md` | 内存管理重构 |
| `CRITICAL_RESOURCE_ANALYSIS.md` | 关键资源分析 |

### 内存与对齐
| 文件 | 说明 |
|------|------|
| `memory_alignment_audit.md` | 内存对齐审计 |
| `alignment_optimization_summary.md` | 对齐优化总结 |
| `OLD_MEMORY_POOL_QUICK_START.md` | 旧版内存池快速指南 |
| `JEMALLOC_VERIFICATION.md` | jemalloc 验证 |

### AUTOSAR 合规
| 文件 | 说明 |
|------|------|
| `ERRORDOMAIN_AUTOSAR_COMPLIANCE.md` | ErrorDomain AUTOSAR 合规分析 |
| `ERROR_DOMAIN_LIFECYCLE_IMPROVEMENT.md` | ErrorDomain 生命周期改进 |
| `AUTOSAR_UTILITIES_SUMMARY.md` | Optional, Variant, Result, Span 优化总结 |

### 集成与测试
| 文件 | 说明 |
|------|------|
| `IMP_OPERATOR_NEW_SUMMARY.md` | IMP_OPERATOR_NEW 集成总结 |
| `IMP_OPERATOR_NEW_TEST_REPORT.md` | IMP_OPERATOR_NEW 测试报告 |
| `CAbort_Refactoring_Summary.md` | CAbort 重构总结 |

### 完成报告
| 文件 | 说明 |
|------|------|
| `Phase1_COMPLETION_REPORT.md` | Phase 1 完成报告（C++17 升级） |
| `IMPROVEMENT_PROPOSAL.md` | 代码分析与优化提案 |
| `CODE_CLEANUP_REPORT.md` | 代码清理报告 (2025-12-30) |
| `ID_HANDLE_VALIDATION_SUMMARY.md` | ID-based Handle 验证总结 |
| `TEST_STRUCTURE_CLEANUP.md` | 测试结构梳理报告 |
| `OVERNIGHT_TEST_STATUS.md` | 8 小时过夜压力测试记录 |

## 归档策略

### 归档条件
- ✅ 功能已稳定，文档已在其他地方体现
- ✅ 一次性分析/审计任务已完成
- ✅ 设计被替换（如 iceoryx2 → CoreIPC）
- ✅ 规范版本已升级（如 R24-11 → R25-11）

### 删除条件
- ❌ 内容与现有文档完全重复
- ❌ 无历史参考价值的临时产物

## 当前文档

当前有效文档位于上层目录：
- [../INDEX.md](../INDEX.md) — 📍 文档导航入口
- [../IPC_DESIGN_ARCHITECTURE.md](../IPC_DESIGN_ARCHITECTURE.md) — CoreIPC 设计
- [../BENCHMARK_REPORT.md](../BENCHMARK_REPORT.md) — 性能基准
- [../HMAC_SECRET_CONFIG.md](../HMAC_SECRET_CONFIG.md) — HMAC 配置
- [../MEMORY_OPTIONS.md](../MEMORY_OPTIONS.md) — 内存管理选项

---

**归档文档数**: 28 篇  
**最后更新**: 2026-03-04  
**结构**: 扁平目录（无子文件夹）
