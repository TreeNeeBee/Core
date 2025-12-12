# LightAP Core 模块文档中心

欢迎使用 LightAP Core 模块文档。本文档中心提供完整的架构设计、开发指南和参考资料。

## 📋 文档结构

```
doc/
├── README.md                                  # 本文件 - 文档导航中心
├── QUICK_START.md                             # 快速入门指南
├── HMAC_SECRET_CONFIG.md                      # HMAC 密钥配置
├── THIRD_PARTY.md                             # 第三方依赖说明
├── memory/                                    # ⭐ IoxPool 内存管理文档
│   ├── README.md                              # IoxPool 文档索引
│   ├── MEMORY_MANAGEMENT_GUIDE.md             # 核心架构设计（v0.3.0，6300行）
│   ├── IMPLEMENTATION_PLAN_v0.3.0.md          # 30天开发计划（4600行）
│   ├── REFACTORING_PLAN_JEMALLOC.md           # jemalloc 重构方案
│   ├── DESIGN_CONSISTENCY_REPORT.md           # 设计一致性验证
│   ├── CHANGELOG_v0.3.0.md                    # 版本变更日志
│   ├── legacy/                                # 历史版本归档
│   │   ├── README.md
│   │   ├── MEMORY_MANAGEMENT_GUIDE_v0.2.0_TCMalloc_ARCHIVED.md
│   │   └── ...
│   ├── design_history/                        # 设计演进历史
│   │   ├── README.md
│   │   ├── IOXPOOL_V11_DEVELOPMENT_PLAN.md
│   │   └── ...
│   └── reports/                               # 状态报告
│       ├── README.md
│       ├── IOXPOOL_DESIGN_ISSUES_REPORT.md
│       └── ...
└── [其他模块文档...]                          # 未来扩展：日志、持久化等
```

## 🚀 快速导航

### 👨‍💻 新开发者入门

#### Core 模块总览
1. **[QUICK_START.md](./QUICK_START.md)** - Core 模块快速入门

#### IoxPool 内存管理（推荐阅读顺序）
1. **[memory/MEMORY_MANAGEMENT_GUIDE.md](./memory/MEMORY_MANAGEMENT_GUIDE.md)** ⭐ **必读**
   - 6300 行完整设计文档
   - IOX 确定性内存池 v0.3.0 架构
   - 三层架构详细说明
   - 32 个 Size Classes 设计
   - 三大关键约束（禁用 STL/malloc、Burst 可选、Chunk 63 自举）

2. **[memory/IMPLEMENTATION_PLAN_v0.3.0.md](./memory/IMPLEMENTATION_PLAN_v0.3.0.md)**
   - 30 天开发路线图（7 个阶段）
   - 完整代码示例（SizeClass.hpp、ThreadCache.cpp、Extent.cpp）
   - 单元测试框架和性能基准

3. **[memory/DESIGN_CONSISTENCY_REPORT.md](./memory/DESIGN_CONSISTENCY_REPORT.md)**
   - 设计一致性验证报告
   - 已修复的 3 个关键设计缺陷
   - 约束合规性检查（100% 符合）

详见: **[memory/README.md](./memory/README.md)** - IoxPool 完整文档索引

### 🏗️ 架构研究者

- **当前架构**: [memory/MEMORY_MANAGEMENT_GUIDE.md](./memory/MEMORY_MANAGEMENT_GUIDE.md)
- **重构方案**: [memory/REFACTORING_PLAN_JEMALLOC.md](./memory/REFACTORING_PLAN_JEMALLOC.md)
- **设计演进**: [memory/design_history/](./memory/design_history/) - 查看历史设计决策
- **版本对比**: [memory/legacy/](./memory/legacy/) - 对比 v0.1.0、v0.2.0、v0.3.0 差异

### 📊 项目管理

- **开发计划**: [memory/IMPLEMENTATION_PLAN_v0.3.0.md](./memory/IMPLEMENTATION_PLAN_v0.3.0.md)
- **版本变更**: [memory/CHANGELOG_v0.3.0.md](./memory/CHANGELOG_v0.3.0.md)
- **状态报告**: [memory/reports/](./memory/reports/) - 查看各阶段完成情况
- **设计问题**: [memory/reports/IOXPOOL_DESIGN_ISSUES_REPORT.md](./memory/reports/IOXPOOL_DESIGN_ISSUES_REPORT.md)

## 📚 模块文档索引

### IoxPool 内存管理
**路径**: `memory/`  
**状态**: v0.3.0 设计完成，实现中  
**文档**: [memory/README.md](./memory/README.md)

**核心特性**:
- 零外部依赖的确定性内存池
- 三层架构（Regions / TC+Extent+Burst / Adapter）
- 32 个 Size Classes（8B-1024B）
- 10-15ns Fast Path 分配延迟
- +33% 吞吐量提升（相比 v0.2.0）

### [未来扩展]
- **日志与追踪**: `logging/` - 待规划
- **持久化**: `persistency/` - 待规划
- **健康管理**: `health/` - 待规划

## 📊 IoxPool v0.3.0 核心特性速览

### 三大关键约束

#### 1. ⚠️ **禁止使用 STL 和任何可能引起 malloc/free 的 API**
```cpp
// ❌ 禁止
std::vector<void*> blocks;  // 内部调用 malloc
void* ptr = malloc(size);    // 递归死锁

// ✅ 必须使用固定数组
uint32_t block_offsets[128];  // 编译期固定大小
uint8_t allocation_bitmap[512];
```
**原因**: 当 IoxPool 重载全局 `operator new/delete` 时，STL 容器的 `malloc` 会递归调用 IoxPool，导致死锁。

#### 2. 🔧 **Burst Region 是可选配置（编译期可配）**
```cmake
option(IOX_ENABLE_BURST_REGION "Enable Burst Region for >1MB allocations" OFF)
```
- **默认**: 嵌入式系统关闭（节省内存）
- **可选**: 服务器环境开启（支持超大分配）

#### 3. 🚀 **Chunk 63 自举后释放**
```
Resident Region (128 MiB):
┌────────────────────────────────────────────┐
│ Chunk 0: Header (永久)                     │  2 MiB
├────────────────────────────────────────────┤
│ Chunk 1-62: Dynamic Extent Pool           │ 124 MiB
├────────────────────────────────────────────┤
│ Chunk 63: Bootstrap (临时，自举后释放)     │ 2 MiB → 归还
└────────────────────────────────────────────┘
```

### 性能目标（对比 v0.2.0）

| 指标 | v0.2.0 | v0.3.0 | 改进 |
|------|--------|--------|------|
| **Fast Path 延迟** | 20-25ns | 10-15ns | ↓ 40% |
| **吞吐量** | 基准 | +33% | ↑ 33% |
| **外部碎片率** | 5-10% | 3-8% | ↓ 20% |
| **内部碎片率** | 8-12% | 5-8% | ↓ 33% |

详细说明见: [memory/README.md](./memory/README.md)

## 🎯 当前开发状态

### IoxPool 开发进度

**设计阶段** ✅ 已完成
- ✅ v0.3.0 架构设计（6300行）
- ✅ 设计一致性验证（3个关键缺陷修复）
- ✅ 30天实施计划（7个阶段，完整代码示例）
- ✅ 约束合规验证（100% 符合）
- ✅ 文档结构重组

**实现阶段** 🚧 进行中
- [ ] Phase 1: 基础设施（5天）
- [ ] Phase 2: Size Classes（2天）
- [ ] Phase 3: Thread Cache（5天）
- [ ] Phase 4: Dynamic Extent（5天）
- [ ] Phase 5-7: 集成、测试、交付（13天）

**预计完成**: 2025年1月10日（30天周期）

详见: [memory/IMPLEMENTATION_PLAN_v0.3.0.md](./memory/IMPLEMENTATION_PLAN_v0.3.0.md)

## 🔗 相关资源

### 外部参考
- [jemalloc 官方文档](https://jemalloc.net/)
- [TCMalloc 设计文档](https://google.github.io/tcmalloc/)
- [AUTOSAR Adaptive Platform R24-11](https://www.autosar.org/)

### 项目内文档
- [QUICK_START.md](./QUICK_START.md) - Core 模块快速入门
- [THIRD_PARTY.md](./THIRD_PARTY.md) - 第三方依赖说明
- [HMAC_SECRET_CONFIG.md](./HMAC_SECRET_CONFIG.md) - HMAC 配置指南

### 代码仓库
- **Repository**: [TreeNeeBee/LightAP](https://github.com/TreeNeeBee/LightAP)
- **Branch**: github
- **Module Path**: `/modules/Core/`

## 📧 贡献与支持

### 报告问题
- GitHub Issues: https://github.com/TreeNeeBee/LightAP/issues
- 请包含：问题描述、重现步骤、环境信息

### 文档改进
- 提交 Pull Request 到 `modules/Core/doc/`
- 遵循现有文档风格和结构
- 更新相关 README 索引

### 联系方式
- **Maintainer**: LightAP Core Team
- **Repository**: TreeNeeBee/LightAP

---

**最后更新**: 2024-12-11  
**文档版本**: 1.1.0 (IoxPool v0.3.0)  
**维护者**: LightAP Core Team
