# 内存池配置迁移指南

## 概述

`pool_enable` 已弃用。现在使用编译时配置 **MemoryPoolConfig.hpp** 来管理内存池。

## 问题回顾

之前提问时，您发现内存池耗尽后没有自动增长。根本原因是：
- 配置文件中 `pool_enable` 设置为 `false`
- 即使内存池代码本身正确（具备自动增长逻辑），但功能被禁用了

## 新的配置方式

### 1. 编译时配置文件
位置：`modules/Core/source/inc/memory/MemoryPoolConfig.hpp`

```cpp
namespace memory_config {
    // 开启/关闭：通过 CMake 的 LAP_ENABLE_MEMORY_POOL 宏控制
    constexpr UInt32 MEMORY_ALIGNMENT = 8;           // 内存对齐
    constexpr Bool ENABLE_MEMORY_TRACKING = false;   // 内存跟踪
    constexpr UInt32 MAX_POOL_COUNT = 16;            // 最大池数量
    
    // 默认池配置
    constexpr PoolConfigEntry DEFAULT_POOL_CONFIGS[] = {
        { .unitSize = 16,  .initialCount = 100, .maxBlocks = 1000, .growthRate = 50 },
        { .unitSize = 32,  .initialCount = 100, .maxBlocks = 1000, .growthRate = 50 },
        // ...更多池...
    };
}
```

### 2. CMake 构建选项
位置：`modules/Core/CMakeLists.txt`

```cmake
if(LAP_CORE_MEMORY_POOL)
    target_compile_definitions(lap_core PUBLIC LAP_ENABLE_MEMORY_POOL)
endif()
```

## 自动增长机制

当内存池耗尽时，`CPoolAllocator::allocUnit()` 函数会：

```cpp
void* PoolAllocator::allocUnit(tagMemPool* pool) {
    CMutexGuard lock(&mutex_);
    
    // 如果无可用单元，尝试增长池
    if (!pool->freeList && !addPoolBlock(pool)) {
        INNER_CORE_LOG("Failed to allocate unit from pool");
        return nullptr;  // 增长失败
    }
    // ... 分配单元
}
```

## 配置注意事项

### growthRate（增长率）的含义
- **不是百分比**，而是每次扩展时**新增单元数量**
- 例如：`growthRate = 50` 意味着每次扩展时增加 50 个单元

### maxBlocks（最大块数）
- `0` = 无限制
- `> 0` = 上限，达到后不再增长

### 增长限制条件
在 `addPoolBlock()` 中，增长受以下条件限制：

```cpp
if ((pool->currentCount >= pool->maxCount) && (pool->maxCount != 0)) {
    return false;  // 已达到最大限制，不能增长
}
```

## 如何配置

### 方式 1：修改编译时配置（推荐）

编辑 `MemoryPoolConfig.hpp`：

```cpp
constexpr PoolConfigEntry DEFAULT_POOL_CONFIGS[] = {
    // 更大的初始尺寸和增长率
    { .unitSize = 16,  .initialCount = 200, .maxBlocks = 2000, .growthRate = 100 },
    { .unitSize = 32,  .initialCount = 200, .maxBlocks = 2000, .growthRate = 100 },
    // ...
};
```

然后重新编译。

### 方式 2：保留运行时配置文件

虽然 `pool_enable` 已弃用，但 `memory.json` 可以保留用于文档参考：

位置：`modules/Core/build/config/memory.json`

```json
{
    "pools": [
        {
            "unit_size": 16,
            "initial_count": 100,
            "max_blocks": 1000,
            "growth_rate": 50
        }
    ]
}
```

**注意**：这个文件现在仅用于**参考和文档目的**，实际配置从编译时配置读取。

## 启用内存池

### 方式 1：CMake 命令行
```bash
cmake -DLAP_CORE_MEMORY_POOL=ON ..
```

### 方式 2：CMakeLists.txt
```cmake
set(LAP_CORE_MEMORY_POOL ON)
```

## 故障排查

### 问题：内存池仍未自动增长

**检查清单**：

1. 确认编译时定义了 `LAP_ENABLE_MEMORY_POOL`
   ```bash
   grep -r "LAP_ENABLE_MEMORY_POOL" build/
   ```

2. 检查 `maxBlocks` 配置是否已达到
   ```cpp
   // 在 MemoryPoolConfig.hpp 中
   { .unitSize = 16, .initialCount = 100, .maxBlocks = 1000, .growthRate = 50 }
                                                    ^^^^
                                              检查这个值
   ```

3. 如果 `maxBlocks = 0`（无限制）仍未增长，检查是否有其他错误
   - 查看日志中的 `INNER_CORE_LOG` 消息
   - 检查是否有内存分配失败（mmap 失败）

4. 验证 growthRate > 0
   ```cpp
   if (count == 0) {
       return false;  // growthRate 为 0 会导致增长失败
   }
   ```

## 配置建议

### 高频小对象应用
```cpp
{ .unitSize = 16,  .initialCount = 500, .maxBlocks = 5000, .growthRate = 200 },
{ .unitSize = 32,  .initialCount = 500, .maxBlocks = 5000, .growthRate = 200 },
```

### 内存受限应用
```cpp
{ .unitSize = 16,  .initialCount = 20,  .maxBlocks = 100,  .growthRate = 10 },
{ .unitSize = 32,  .initialCount = 20,  .maxBlocks = 100,  .growthRate = 10 },
```

### 实时系统（预分配为主）
```cpp
{ .unitSize = 16,  .initialCount = 1000, .maxBlocks = 1000, .growthRate = 1 },
// 上限设为 initialCount，防止动态增长延迟
```

## 参考

- 完整实现：[CPoolAllocator.cpp](../source/src/memory/CPoolAllocator.cpp)
- 配置文件：[MemoryPoolConfig.hpp](../source/inc/memory/MemoryPoolConfig.hpp)
- 自动增长逻辑：[CPoolAllocator.cpp#addPoolBlock](../source/src/memory/CPoolAllocator.cpp#L296-L341)
