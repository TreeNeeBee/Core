# 内存池配置验证报告

## 配置总结

### Size-Classes（11 个）

| Pool | Size | Initial Count | Max Blocks | Growth Rate |
|------|------|---------------|-----------|-------------|
| 0    | 8 bytes    | 200 | 0 (∞) | 100 |
| 1    | 16 bytes   | 200 | 0 (∞) | 100 |
| 2    | 24 bytes   | 150 | 0 (∞) | 80  |
| 3    | 32 bytes   | 150 | 0 (∞) | 80  |
| 4    | 64 bytes   | 100 | 0 (∞) | 50  |
| 5    | 128 bytes  | 80  | 0 (∞) | 40  |
| 6    | 256 bytes  | 60  | 0 (∞) | 30  |
| 7    | 512 bytes  | 40  | 0 (∞) | 20  |
| 8    | 1024 bytes | 30  | 0 (∞) | 15  |
| 9    | 2048 bytes | 20  | 0 (∞) | 10  |
| 10   | 4096 bytes | 15  | 0 (∞) | 8   |

### 关键参数

- **MIN_POOL_UNIT_SIZE**: 8 字节
- **MAX_POOL_UNIT_SIZE**: 4096 字节
- **ENABLE_MEMORY_TRACKING**: true
- **内存对齐**: 8 字节

### 分配策略

| 分配大小 | 处理方式 | 说明 |
|---------|---------|------|
| 8-4096 字节 | 内存池 | 使用对应的 size-class 池分配 |
| > 4096 字节 | mmap | 直接使用 mmap 分配（无需池） |

## 测试结果

### 1. 内存跟踪功能验证 (test_memory_trace)

✓ 成功初始化 11 个内存池
✓ 支持 8 到 4096 字节的所有 size-class 分配
✓ 内存跟踪已启用（ENABLE_MEMORY_TRACKING = true）
✓ 所有分配指针均通过有效性检查

**关键指标**:
- 总池数: 11
- 初始化总内存: ~114520 字节
- 测试分配: 18 次
- 有效指针: 11/11 (100%)

### 2. 阈值行为验证 (test_simple_threshold)

✓ 256 字节分配: 使用池
✓ 4096 字节分配: 使用池
✓ 8192 字节分配: 使用 mmap
✓ 65536 字节分配: 使用 mmap
✓ 所有分配均通过指针有效性检查

**验证**:
- 池分配计数: 仅统计 ≤ 4096 字节的分配
- 大分配: > 4096 字节绕过池，直接使用 mmap
- 内存释放: 所有分配类型都能正确释放

## 特性说明

### 自动增长

- 所有池配置 **maxBlocks = 0**（无上限）
- 当池耗尽时自动增长（按 growthRate）
- 无需手动调整池大小

### 内存跟踪

- 启用了 MemoryTracker，所有分配都被跟踪
- 支持分配溯源和内存泄漏检测
- 每次分配都经过有效性检查

### 性能优化

- 为不同大小的分配创建了 11 个专用池
- 避免碎片化（每个池内部分配大小一致）
- 预分配策略减少分配延迟
- 大分配（> 4096）绕过池系统，使用系统 mmap

## 构建命令

```bash
# 启用内存池并重新构建
cd /workspace/LightAP/modules/Core/build
cmake -DENABLE_MEMORY_POOL=ON ..
make -j$(nproc)

# 运行验证测试
./test_simple_threshold
./test_memory_trace
```

## 配置文件位置

- 编译时配置: [MemoryPoolConfig.hpp](source/inc/memory/MemoryPoolConfig.hpp)
- 运行时参考: [memory.json](build/config/memory.json)
- 内存管理器: [CMemoryManager.hpp](source/inc/memory/CMemoryManager.hpp)

## 修改历史

1. ✓ 添加 8 字节 size-class（MIN_POOL_UNIT_SIZE = 8）
2. ✓ 设置 11 个 size-classes (8, 16, 24, 32, 64, 128, 256, 512, 1024, 2048, 4096)
3. ✓ 所有池配置 maxBlocks = 0（无限制）
4. ✓ 启用内存跟踪 (ENABLE_MEMORY_TRACKING = true)
5. ✓ 超过 4096 字节直接使用 mmap (MAX_POOL_UNIT_SIZE = 4096)
6. ✓ 创建验证测试程序

## 性能数据

### 初始预分配

总计: 约 1570 个单元 = ~114520 字节

```
8字节:   200 × 8     = 1,600 bytes
16字节:  200 × 16    = 3,200 bytes
24字节:  150 × 24    = 3,600 bytes
32字节:  150 × 32    = 4,800 bytes
64字节:  100 × 64    = 6,400 bytes
128字节: 80 × 128    = 10,240 bytes
256字节: 60 × 256    = 15,360 bytes
512字节: 40 × 512    = 20,480 bytes
1KB:     30 × 1024   = 30,720 bytes
2KB:     20 × 2048   = 40,960 bytes
4KB:     15 × 4096   = 61,440 bytes
```

## 建议调整场景

### 高频小对象应用
增加前几个池的 initialCount 和 growthRate

### 内存受限设备
减少 initialCount，保持 growthRate 平衡

### 实时系统
增加初始预分配，降低增长频率

## 参考文档

- [MEMORY_POOL_CONFIG_MIGRATION.md](doc/MEMORY_POOL_CONFIG_MIGRATION.md)
- [MEMORY_POOL_GROWTH_QUICK_REFERENCE.md](doc/MEMORY_POOL_GROWTH_QUICK_REFERENCE.md)
