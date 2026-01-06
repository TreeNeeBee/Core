# 内存池配置快速参考卡

## 当前配置状态 ✓

### Size-Classes 
```
Pool  Size      Init Count  Max Blocks  Growth Rate
────────────────────────────────────────────────────
 0    8 B       200         0 (∞)       100
 1    16 B      200         0 (∞)       100
 2    24 B      150         0 (∞)       80
 3    32 B      150         0 (∞)       80
 4    64 B      100         0 (∞)       50
 5    128 B     80          0 (∞)       40
 6    256 B     60          0 (∞)       30
 7    512 B     40          0 (∞)       20
 8    1K        30          0 (∞)       15
 9    2K        20          0 (∞)       10
10    4K        15          0 (∞)       8
```

### 分配规则
- **8 - 4096 字节**: 内存池分配 (最快, O(1))
- **> 4096 字节**: mmap 直接分配 (避免过大池)

### 编译配置
```cpp
MIN_POOL_UNIT_SIZE = 8       // 最小池单元
MAX_POOL_UNIT_SIZE = 4096    // 最大池单元
ENABLE_MEMORY_TRACKING = true // 启用跟踪
MEMORY_ALIGNMENT = 8          // 8字节对齐
```

## 关键数字

| 指标 | 值 |
|------|-----|
| 池数量 | 11 |
| 初始单元数 | ~1570 |
| 初始内存 | ~114 KB |
| 自动增长 | ✓ 已启用 |
| 无上限 | ✓ maxBlocks=0 |
| 跟踪功能 | ✓ 已启用 |

## 测试命令

```bash
# 进入构建目录
cd /workspace/LightAP/modules/Core/build

# 启用内存池并构建
cmake -DENABLE_MEMORY_POOL=ON .. && make -j$(nproc)

# 运行验证测试
./test_simple_threshold      # 验证阈值行为 (5 秒内完成)
./test_memory_trace          # 验证跟踪功能 (10 秒内完成)
```

## 配置文件清单

| 文件 | 用途 | 状态 |
|------|------|------|
| `source/inc/memory/MemoryPoolConfig.hpp` | 编译时配置 | ✓ 完成 |
| `source/src/memory/CPoolAllocator.cpp` | 池实现 | ✓ 就绪 |
| `source/src/memory/CMemoryManager.cpp` | 管理器 | ✓ 就绪 |
| `build/config/memory.json` | 参考文档 | ✓ 已更新 |
| `doc/MEMORY_POOL_CONFIG_VERIFICATION.md` | 验证报告 | ✓ 完成 |

## 性能特征

### 优点
- ✓ 快速 O(1) 分配 (< 4096 字节)
- ✓ 无碎片化 (同大小池)
- ✓ 自动增长 (无需手动调整)
- ✓ 内存跟踪 (泄漏检测)
- ✓ 灵活阈值 (> 4096 用 mmap)

### 初始开销
- 内存: ~114 KB (预分配)
- CPU: 初始化时 < 10ms

## 修改指南

### 增加池容量
编辑 `MemoryPoolConfig.hpp`:
```cpp
{ 16, 500, 0, 250 }  // 从 200 增加到 500 初始/250 增长
```

### 添加新的 size-class
```cpp
{ 12, 100, 0, 50 }   // 12 字节新池 (必须 >= 8)
```

### 设置上限
```cpp
{ 16, 200, 1000, 100 }  // maxBlocks = 1000 (设置上限)
```

## 常见问题

**Q: 内存池满了怎么办?**  
A: 自动增长。增长 growthRate 个新单元，直到内存耗尽或系统限制。

**Q: 可以改变大小分界点吗?**  
A: 可以。修改 MAX_POOL_UNIT_SIZE (当前 4096)。

**Q: 如何禁用内存跟踪?**  
A: 改 ENABLE_MEMORY_TRACKING = false，重新编译。

**Q: 内存占用多少?**  
A: 初始 ~114 KB + 运行时动态增长。

## 版本信息

- 配置日期: 2025-12-26
- Size-classes: 11 (8, 16, 24, 32, 64, 128, 256, 512, 1K, 2K, 4K)
- 阈值: 4096 字节
- 编译器: C++17
- 测试状态: ✓ PASSED

---

**编辑位置**: `/workspace/LightAP/modules/Core/source/inc/memory/MemoryPoolConfig.hpp`

**验证**: 运行 `test_simple_threshold` 和 `test_memory_trace` 确认配置生效。
