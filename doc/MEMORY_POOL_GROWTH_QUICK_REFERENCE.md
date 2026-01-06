# 内存池自动增长配置快速参考

## 问题诊断

**原问题**：内存池耗尽后没有自动增长
**根本原因**：`pool_enable` 已弃用，应使用编译时配置

## 关键代码位置

| 文件 | 用途 |
|------|------|
| `modules/Core/source/inc/memory/MemoryPoolConfig.hpp` | **编译时配置**（新） |
| `modules/Core/build/config/memory.json` | 参考配置（已弃用 `pool_enable`） |
| `modules/Core/CMakeLists.txt` | 构建选项 `ENABLE_MEMORY_POOL` |
| `modules/Core/source/src/memory/CPoolAllocator.cpp` | 自动增长实现 |

## 工作原理

### 自动增长条件

当需要分配内存但池中无可用单元时：

```
allocUnit() 被调用
    ↓
pool->freeList 为空？
    ├─ 是 → addPoolBlock() 尝试增长
    │        ↓
    │      currentCount < maxCount (或 maxCount == 0)？
    │        ├─ 是 → 增长成功 ✓
    │        └─ 否 → 增长失败，fallback 到 mmap
    └─ 否 → 直接分配
```

### 增长参数解释

以下配置为例：
```cpp
{ .unitSize = 16, .initialCount = 100, .maxBlocks = 1000, .growthRate = 50 }
```

| 参数 | 值 | 含义 |
|------|-----|------|
| `unitSize` | 16 | 每个单元 16 字节 |
| `initialCount` | 100 | 启动时预分配 100 个单元 |
| `maxBlocks` | 1000 | 最多保有 1000 个单元 |
| `growthRate` | 50 | 每次增长时新增 50 个单元 |

**增长过程**：
- 初始状态：1600 字节（100 × 16）
- 第 1 次增长：2400 字节（150 × 16）
- 第 2 次增长：3200 字节（200 × 16）
- ...
- 达到上限：16000 字节（1000 × 16）

## 配置步骤

### 1. 编辑编译时配置

文件：`modules/Core/source/inc/memory/MemoryPoolConfig.hpp`

```cpp
namespace memory_config {
    // 关键参数
    constexpr UInt32 MEMORY_ALIGNMENT = 8;
    constexpr Bool ENABLE_MEMORY_TRACKING = false;
    constexpr UInt32 MAX_POOL_COUNT = 16;
    
    // 池配置
    constexpr PoolConfigEntry DEFAULT_POOL_CONFIGS[] = {
        { .unitSize = 16,  .initialCount = 100, .maxBlocks = 1000, .growthRate = 50 },
        { .unitSize = 32,  .initialCount = 100, .maxBlocks = 1000, .growthRate = 50 },
        // ... 更多
    };
}
```

### 2. 构建时启用内存池

```bash
# 方式 A：CMake 命令行
cmake -DENABLE_MEMORY_POOL=ON ..

# 方式 B：编辑 CMakeLists.txt
set(ENABLE_MEMORY_POOL ON)
```

### 3. 重新编译

```bash
cd /workspace/LightAP/modules/Core/build
cmake ..
make -j$(nproc)
```

## 常见问题

### Q1：如何验证内存池已启用？
```bash
# 查看编译定义
strings build/liblap_core.a | grep LAP_ENABLE_MEMORY_POOL

# 或查看编译日志
cmake .. 2>&1 | grep "LAP_ENABLE_MEMORY_POOL"
```

### Q2：为什么还在使用 mmap（内存溢出）？
可能原因：
1. `maxBlocks` 设置过小
2. `growthRate` 设置过小  
3. `initialCount` 不足
4. 未启用内存池（检查编译定义）

**解决**：增加这些值并重新编译

### Q3：内存池是否支持无限增长？
可以。设置 `maxBlocks = 0`：

```cpp
{ .unitSize = 16, .initialCount = 100, .maxBlocks = 0, .growthRate = 50 }
```

### Q4：如何禁用某个池？
设置 `growthRate = 0` 并使用较小的 `initialCount`：

```cpp
{ .unitSize = 256, .initialCount = 0, .maxBlocks = 0, .growthRate = 0 }
```

## 验证自动增长

### 运行时检查
```cpp
#include "memory/CMemoryManager.hpp"

using namespace lap::core;
MemoryManager& mgr = MemoryManager::getInstance();

// 获取内存统计
MemoryStats stats = mgr.getMemoryStats();
std::cout << "Total pool memory: " << stats.totalPoolMemory << " bytes\n";
std::cout << "Current alloc: " << stats.currentAllocSize << " bytes\n";
std::cout << "Pool count: " << stats.poolCount << "\n";
```

### 查看日志
```bash
# 如果启用了 INNER_CORE_LOG，查看增长时的日志
grep -i "pool" logfile.txt
```

## 性能考虑

### 预分配 vs 动态增长

| 场景 | 推荐配置 |
|------|---------|
| 实时系统 | 大 `initialCount`，小 `growthRate`，小 `maxBlocks` |
| Web 服务器 | 中等 `initialCount`，中等 `growthRate`，大或无限 `maxBlocks` |
| 内存受限 | 小 `initialCount`，小 `growthRate`，小 `maxBlocks` |
| 高吞吐量 | 较大 `growthRate` 减少增长次数 |

## 迁移检查表

- [ ] 移除旧的 `pool_enable` 配置引用
- [ ] 在 `MemoryPoolConfig.hpp` 中设置合适的池参数
- [ ] 编译时启用 `ENABLE_MEMORY_POOL`
- [ ] 验证编译成功且定义被正确应用
- [ ] 测试内存分配和增长
- [ ] 更新相关文档

## 文件清单

**需要配置的文件**：
- ✅ `modules/Core/source/inc/memory/MemoryPoolConfig.hpp` - 现已完整
- ✅ `modules/Core/build/config/memory.json` - 已移除弃用字段
- ✅ `modules/Core/CMakeLists.txt` - 构建选项已到位

**参考文档**：
- `modules/Core/doc/MEMORY_POOL_CONFIG_MIGRATION.md` - 详细迁移指南
- `modules/Core/MEMORY_OPTIONS.md` - 内存选项说明
