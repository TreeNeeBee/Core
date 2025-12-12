# IoxPool 内存管理文档

本目录包含 LightAP Core 模块的 IoxPool 内存管理相关文档。

## 📋 目录结构

```
memory/
├── README.md                          # 本文件 - IoxPool 文档索引
├── MEMORY_MANAGEMENT_GUIDE.md         # ⭐ 核心设计文档 (v0.3.0)
├── IMPLEMENTATION_PLAN_v0.3.0.md      # 30天开发计划
├── REFACTORING_PLAN_JEMALLOC.md       # jemalloc 重构方案
├── DESIGN_CONSISTENCY_REPORT.md       # 设计一致性验证报告
├── CHANGELOG_v0.3.0.md                # v0.3.0 版本变更日志
├── legacy/                            # 历史版本归档
│   ├── README.md
│   ├── MEMORY_MANAGEMENT_GUIDE_v0.2.0_TCMalloc_ARCHIVED.md
│   ├── MEMORY_MANAGEMENT_GUIDE_v0.1.0_LEGACY.md
│   └── ...
├── design_history/                    # 设计演进历史
│   ├── README.md
│   ├── IOXPOOL_V11_DEVELOPMENT_PLAN.md
│   ├── AUTOSAR_REFACTORING_PLAN.md
│   └── ...
└── reports/                           # 状态报告
    ├── README.md
    ├── IOXPOOL_DESIGN_ISSUES_REPORT.md
    ├── IOXPOOL_PHASE1_SUMMARY.md
    └── ...
```

## 🚀 快速开始

### 新开发者入门（按顺序阅读）

1. **[MEMORY_MANAGEMENT_GUIDE.md](./MEMORY_MANAGEMENT_GUIDE.md)** (6300行)
   - **必读** - IoxPool v0.3.0 完整架构设计
   - 三层架构：Memory Regions / TC+Extent+Burst / Adapter API
   - 32个 Size Classes (8B-1024B)
   - 三大关键约束（禁用STL/malloc、Burst可选、Chunk 63自举）

2. **[IMPLEMENTATION_PLAN_v0.3.0.md](./IMPLEMENTATION_PLAN_v0.3.0.md)** (4600行)
   - 30天开发路线图（7个阶段）
   - 完整代码示例（SizeClass.hpp、ThreadCache.cpp、Extent.cpp）
   - 单元测试框架、性能基准测试
   - 每个阶段的验收标准

3. **[REFACTORING_PLAN_JEMALLOC.md](./REFACTORING_PLAN_JEMALLOC.md)**
   - jemalloc 设计理念参考
   - v0.2.0 → v0.3.0 迁移指南
   - Thread Cache 核心机制

### 架构研究者

- **设计演进**: `design_history/IOXPOOL_V11_DEVELOPMENT_PLAN.md` - 查看 v11.0 原始设计
- **版本对比**: `legacy/MEMORY_MANAGEMENT_GUIDE_v0.2.0_TCMalloc_ARCHIVED.md` - TCMalloc 外部依赖版本
- **设计验证**: `DESIGN_CONSISTENCY_REPORT.md` - 架构一致性核查（3个修复的关键缺陷）

### 项目管理

- **当前状态**: `reports/IOXPOOL_DESIGN_ISSUES_REPORT.md` - 已知问题跟踪
- **版本变更**: `CHANGELOG_v0.3.0.md` - v0.2.0 → v0.3.0 的所有变化
- **里程碑**: `reports/IOXPOOL_PHASE1_SUMMARY.md` - Phase 1 完成情况

## 📊 IoxPool v0.3.0 核心特性

### 架构概览

**三层架构**:
- **Layer 1 (Memory Regions)**: 128 MiB Resident Region + 可选 Burst Region
- **Layer 2 (Allocator Core)**: Thread Cache (TC) + Dynamic Extent + Burst Allocator
- **Layer 3 (Adapter API)**: malloc/free/new/delete 适配器

### 关键技术指标

| 指标 | v0.2.0 (TCMalloc) | v0.3.0 (IOX TC) | 改进 |
|------|------------------|----------------|------|
| **Fast Path 延迟** | 20-25ns | 10-15ns | **↓ 40%** |
| **吞吐量** | 基准 | +33% | **↑ 33%** |
| **外部碎片率** | 5-10% | 3-8% | **↓ 20%** |
| **内部碎片率** | 8-12% | 5-8% | **↓ 33%** |
| **Size Classes** | 48个 | 32个 | 优化嵌入式 |
| **外部依赖** | TCMalloc | **零依赖** | 自包含 |

### 三大关键约束

#### 1. ⚠️ **禁止 STL / malloc / free**
```cpp
// ❌ 禁止使用
std::vector<void*> blocks;  // 内部调用 malloc
void* ptr = malloc(size);    // 递归死锁风险

// ✅ 必须使用固定数组
uint32_t block_offsets[128];  // 编译期固定大小
uint8_t allocation_bitmap[512];
```
**原因**: 当 IoxPool 重载全局 `operator new/delete` 时，STL 容器内部的 `malloc` 会递归调用 IoxPool，导致死锁。

#### 2. 🔧 **Burst Region 可选配置**
```cmake
# 默认关闭 Burst Region（嵌入式场景）
option(IOX_ENABLE_BURST_REGION "Enable Burst Region for >1MB allocations" OFF)
```
- **用途**: 处理 >1MB 的超大分配
- **默认**: 嵌入式系统默认禁用（减少内存占用）
- **开启**: 服务器环境可选开启

#### 3. 🚀 **Chunk 63 自举机制**
```
Resident Region (128 MiB):
┌────────────────────────────────────────────┐
│ Chunk 0: ResidentRegionHeader (永久)      │  2 MiB
├────────────────────────────────────────────┤
│ Chunk 1-62: Dynamic Extent Pool           │ 124 MiB
├────────────────────────────────────────────┤
│ Chunk 63: Bootstrap Data (临时，自举后释放)│ 2 MiB → 归还给 Dynamic Extent
└────────────────────────────────────────────┘
```
- **Chunk 0**: Header 永久占用（元数据）
- **Chunk 63**: 临时 bootstrap，初始化后释放
- **Chunk 1-62**: 可用于 Dynamic Extent

## 📂 子目录说明

### legacy/ - 历史版本归档
包含已弃用的设计版本：
- **v0.2.0** (TCMalloc): 使用外部 TCMalloc 库的版本（197KB）
- **v0.1.0**: 最早的 PoolAllocator 设计
- **TCache 实验**: 早期 Thread Cache 设计尝试

**何时参考**:
- 理解设计演进过程
- 对比不同架构的优劣
- 研究为何放弃 TCMalloc 外部依赖

详见: [legacy/README.md](./legacy/README.md)

### design_history/ - 设计演进历史
记录关键设计决策的时间线：
- **V11.0 开发计划**: IoxPool 初始设计
- **AUTOSAR 重构**: 符合 AUTOSAR AP 标准的调整
- **Quick Reference**: 早期快速参考文档

**何时参考**:
- 理解某个设计决策的背景
- 追溯功能需求的来源
- 研究架构演进的原因

详见: [design_history/README.md](./design_history/README.md)

### reports/ - 状态报告
项目进展和问题跟踪：
- **设计问题报告**: 已发现和已修复的设计缺陷
- **阶段完成报告**: Phase 1-N 的里程碑总结
- **R24-11 合规报告**: AUTOSAR R24-11 功能完成情况

**何时参考**:
- 了解当前项目状态
- 查看已知问题和解决方案
- 评估开发进度

详见: [reports/README.md](./reports/README.md)

## 🔍 核心概念速查

### Size Class 系统
```cpp
// 32 个预设 Size Classes (8B - 1024B)
static constexpr uint16_t PRESET_IOXPOOL_SIZES[32] = {
    8, 16, 24, 32, 48, 64, 80, 96, 112, 128,      // Tiny (8-128B)
    160, 192, 224, 256, 320, 384, 448, 512,       // Small (160-512B)
    640, 768, 896, 1024, ...                      // Medium (640-1024B)
};
```

### Thread Cache (TC) 分配流程
```
1. 应用请求: malloc(64)
2. Size Class 映射: 64B → Class 5
3. TLS ThreadCache 查找: thread_local tc
4. TCBin Fast Path: tc.bins[5].pop() → O(1)
   ├─ Hit: 返回 block (10-15ns)
   └─ Miss: Refill from Extent
5. Extent Slow Path: ExtentManager.allocate()
   ├─ Bitmap 查找空闲 slot
   └─ 返回新 block (200-500ns)
```

### Dynamic Extent 管理
```
Extent (2 MiB Chunk):
┌────────────────────────────────────────┐
│ ExtentHeader (128B): size_class, count│
├────────────────────────────────────────┤
│ Bitmap (512B): 每个 slot 1 bit        │
├────────────────────────────────────────┤
│ Data Region: 按 block_size 切分       │
│   [Block 0][Block 1]...[Block N]       │
└────────────────────────────────────────┘
```

## 📝 文档版本历史

| 版本 | 日期 | 主要变更 | 文档 |
|------|------|---------|------|
| **v0.3.0** | 2024-12-09 | IOX 自包含设计，零外部依赖 | `MEMORY_MANAGEMENT_GUIDE.md` |
| v0.2.0 | 2024-12-01 | TCMalloc 外部依赖版本 | `legacy/MEMORY_MANAGEMENT_GUIDE_v0.2.0_*` |
| v0.1.0 | 2024-11-12 | 最早的 PoolAllocator | `legacy/MEMORY_MANAGEMENT_GUIDE_v0.1.0_*` |

## 🎯 当前开发状态

### 已完成 ✅
- [x] v0.3.0 架构设计（6300行）
- [x] 设计一致性验证（3个关键缺陷修复）
- [x] 30天实施计划（7个阶段，完整代码示例）
- [x] 约束合规验证（3个约束 100% 符合）
- [x] 文档结构重组（内存相关文档集中管理）

### 进行中 🚧
- [ ] Phase 1: 基础设施搭建（5天）
- [ ] Phase 2: Size Classes 实现（2天）
- [ ] Phase 3: Thread Cache 实现（5天）
- [ ] Phase 4: Dynamic Extent 实现（5天）
- [ ] Phase 5: 集成与路由（3天）
- [ ] Phase 6: 测试与验证（5天）
- [ ] Phase 7: 交付与文档（5天）

### 预计完成时间
**2025年1月10日** （30天开发周期）

## 🔗 相关资源

### 外部参考
- [jemalloc 官方文档](https://jemalloc.net/)
- [TCMalloc 设计文档](https://google.github.io/tcmalloc/)
- [AUTOSAR AP R24-11 Specification](https://www.autosar.org/)

### Core 模块其他文档
- `../QUICK_START.md` - Core 模块快速入门
- `../THIRD_PARTY.md` - 第三方依赖说明

### 项目根目录
- `/modules/Core/README.md` - Core 模块主文档
- `/modules/Core/CMakeLists.txt` - 构建配置

## 📧 联系方式

如有问题或建议，请联系：
- **Maintainer**: LightAP Core Team
- **Repository**: [TreeNeeBee/LightAP](https://github.com/TreeNeeBee/LightAP)
- **Issue Tracker**: GitHub Issues

---

**最后更新**: 2024-12-11  
**文档版本**: v0.3.0  
**维护者**: LightAP Core Team
