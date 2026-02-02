# Core模块文档

欢迎查阅 LightAP Core 模块的技术文档。

## 📍 快速开始

- **文档导航**: [INDEX.md](INDEX.md) - 完整的文档索引和分类
- **模块概览**: [../README.md](../README.md) / [../README_CN.md](../README_CN.md)
- **变更日志**: [../CHANGES.md](../CHANGES.md)

## 📚 文档结构

```
doc/
├── INDEX.md                    # 📍 文档导航入口（从这里开始）
├── README.md                   # 本文件
│
├── *.md                        # 当前有效的技术文档
│   ├── AUTOSAR_REFACTORING_PLAN.md
│   ├── BENCHMARK_REPORT.md
│   ├── HMAC_SECRET_CONFIG.md
│   ├── ICEORYX2_VS_EPOLL.md
│   └── IPC_DESIGN_ARCHITECTURE.md
│
├── reports/                    # 📊 测试报告和清理记录
│   ├── CODE_CLEANUP_REPORT.md
│   ├── ID_HANDLE_VALIDATION_SUMMARY.md
│   └── TEST_STRUCTURE_CLEANUP.md
│
└── archive/                    # 🗄️ 历史文档归档
    ├── README.md               # 归档文档说明
    ├── old_refactoring/        # 历史重构记录
    ├── current/                # 当前监控状态
    └── *.md                    # 其他归档文档
```

## 🔍 主要技术文档

### IPC 设计
- [IPC_DESIGN_ARCHITECTURE.md](IPC_DESIGN_ARCHITECTURE.md) - 零拷贝IPC设计架构（331KB详细设计）

### 性能分析
- [BENCHMARK_REPORT.md](BENCHMARK_REPORT.md) - 性能基准测试
- [ICEORYX2_VS_EPOLL.md](ICEORYX2_VS_EPOLL.md) - iceoryx2与epoll对比

### 配置管理
- [HMAC_SECRET_CONFIG.md](HMAC_SECRET_CONFIG.md) - 配置文件安全加密

### AUTOSAR合规
- [AUTOSAR_REFACTORING_PLAN.md](AUTOSAR_REFACTORING_PLAN.md) - AUTOSAR标准重构计划

## 📊 最新报告

最新的测试和验证报告（2025-12-30）：

- [reports/CODE_CLEANUP_REPORT.md](reports/CODE_CLEANUP_REPORT.md)
- [reports/ID_HANDLE_VALIDATION_SUMMARY.md](reports/ID_HANDLE_VALIDATION_SUMMARY.md)
- [reports/TEST_STRUCTURE_CLEANUP.md](reports/TEST_STRUCTURE_CLEANUP.md)

## 🗄️ 历史文档

所有历史文档已归档至 [archive/](archive/) 目录，包括：
- 历史重构记录
- 已完成的优化项目
- 旧版设计文档

详见：[archive/README.md](archive/README.md)

## 📖 文档规范

### 文档分类

| 类型 | 位置 | 说明 |
|-----|------|------|
| 设计文档 | `doc/*.md` | 当前有效的架构、实现、API设计 |
| 测试报告 | `doc/reports/` | 验证结果、性能测试、清理报告 |
| 归档文档 | `doc/archive/` | 历史文档、已完成的重构 |

### 文档生命周期

1. **活跃文档** - 当前有效的设计和实现文档
2. **报告文档** - 已完成的测试和验证报告
3. **归档文档** - 历史参考，不再主动维护

### 更新文档

更新文档后，请同步更新：
1. [INDEX.md](INDEX.md) - 主文档索引
2. 文档的"最后更新"时间戳
3. 如有必要，更新 [archive/README.md](archive/README.md)

## 🔗 外部参考

- [AUTOSAR Adaptive Platform](https://www.autosar.org/)
- [iceoryx2](https://github.com/eclipse-iceoryx/iceoryx2) - 零拷贝IPC框架
- [Google Test](https://github.com/google/googletest) - 单元测试框架

---

📍 **提示**: 浏览完整文档目录请查看 [INDEX.md](INDEX.md)
