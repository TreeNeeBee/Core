# Core 模块文档

欢迎查阅 LightAP Core 模块的技术文档。

## 📍 快速开始

- **文档导航**: [INDEX.md](INDEX.md) — 完整的文档索引和分类
- **模块概览**: [../README.md](../README.md) / [../README_CN.md](../README_CN.md)
- **变更日志**: [../CHANGES.md](../CHANGES.md)

## 📚 文档结构

```
doc/
├── INDEX.md                         # 📍 文档导航入口（从这里开始）
├── README.md                        # 本文件
│
├── IPC_DESIGN_ARCHITECTURE.md       # CoreIPC 零拷贝设计架构
├── BENCHMARK_REPORT.md              # 性能基准测试报告
├── HMAC_SECRET_CONFIG.md            # HMAC 配置安全指南
├── MEMORY_OPTIONS.md                # 内存管理构建选项
├── AUTOSAR_AP_SWS_Core.pdf         # AUTOSAR 标准文档
│
└── archive/                         # 🗄️ 历史文档归档（扁平结构）
    ├── README.md                    # 归档文档说明
    └── *.md                         # 所有归档文档
```

## 🔍 主要技术文档

### IPC 设计
- [IPC_DESIGN_ARCHITECTURE.md](IPC_DESIGN_ARCHITECTURE.md) — CoreIPC 零拷贝设计架构

### 性能分析
- [BENCHMARK_REPORT.md](BENCHMARK_REPORT.md) — 性能基准测试

### 配置管理
- [HMAC_SECRET_CONFIG.md](HMAC_SECRET_CONFIG.md) — 配置文件 HMAC 安全加密
- [MEMORY_OPTIONS.md](MEMORY_OPTIONS.md) — 内存管理构建选项

### AUTOSAR 合规
- [AUTOSAR_AP_SWS_Core.pdf](AUTOSAR_AP_SWS_Core.pdf) — AUTOSAR AP SWS Core 标准

## 🗄️ 历史文档

所有历史文档已归档至 [archive/](archive/) 目录，包括：
- 已完成的重构记录（Dual-Counter、Lock-Free、iceoryx2 等）
- 旧版设计文档（iceoryx2 vs epoll、R24-11 重构计划等）
- 一次性报告（代码清理、测试结构梳理等）

详见：[archive/README.md](archive/README.md)

## 📖 文档规范

### 文档分类

| 类型 | 位置 | 说明 |
|-----|------|------|
| 设计文档 | `doc/*.md` | 当前有效的架构、设计、配置 |
| 归档文档 | `doc/archive/` | 已完成或过时的历史文档 |

### 文档生命周期

1. 新文档创建于 `doc/` 根目录
2. 过时后移至 `doc/archive/`
3. 更新 `INDEX.md` 和 `archive/README.md`

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
