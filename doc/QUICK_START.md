# Core Memory Management - 快速开始指南

## 功能特性

✅ 内存池管理（4-1024 bytes）  
✅ 自动泄漏检测与报告  
✅ 类级内存追踪（IMP_OPERATOR_NEW）  
✅ 多线程安全（无锁池 + 锁检查器）  
✅ 动态 Magic 防护（PID+时间+ASLR）  
✅ 配置持久化（CRC+HMAC 校验）

## 快速开始

### 1. 编译和测试

```bash
cd modules/Core
cmake .
make -j$(nproc)
./run_all_tests.sh
```

### 2. 在应用中使用

#### 方式 1: 全局内存管理（推荐）

```cpp
#include "Core.hpp"

int main() {
    // 初始化（加载配置）
    Memory::initialize();
    
    // 你的代码 - 所有 new/delete 自动管理
    MyClass* obj = new MyClass();
    delete obj;
    
    // 退出时检测泄漏
    Memory::uninitialize();  // 自动生成 memory_leak.log
    return 0;
}
```

#### 方式 2: 类级追踪（细粒度监控）

```cpp
#include "Core.hpp"

class MyBusinessClass {
public:
    IMP_OPERATOR_NEW(MyBusinessClass)  // 添加这一行
    
    MyBusinessClass() { /* ... */ }
    ~MyBusinessClass() { /* ... */ }
};

int main() {
    Memory::initialize();
    
    MyBusinessClass* obj = new MyBusinessClass();
    // 泄漏报告将显示: class=MyBusinessClass
    
    Memory::uninitialize();
    return 0;
}
```

### 3. 配置文件（memory_config.json）

```json
{
    "enable_check": true,
    "enable_statistics": true,
    "enable_memory_pool": true,
    "pool_config": [
        {"size": 8, "count": 128},
        {"size": 16, "count": 128},
        {"size": 32, "count": 64},
        {"size": 64, "count": 64},
        {"size": 128, "count": 32},
        {"size": 256, "count": 32},
        {"size": 512, "count": 16},
        {"size": 1024, "count": 16}
    ]
}
```

**关键配置项**:
- `enable_check`: true 启用泄漏检测，false 禁用（性能模式）
- `enable_statistics`: 启用统计信息收集
- `pool_config`: 自定义池大小和数量

### 4. 查看内存统计

```cpp
Memory::MemoryStats stats = Memory::getMemoryStats();
printf("当前分配: %zu bytes in %zu objects\n", 
       stats.currentAllocSize, stats.currentAllocCount);
printf("池内存: %zu bytes\n", stats.totalPoolMemory);
printf("峰值: %zu bytes\n", stats.peakAllocSize);
```

### 5. 泄漏报告示例

**memory_leak.log**:
```
[ERROR] LEAK SUMMARY: ==12345== definitely lost: 128 bytes in 2 blocks
[ERROR] Leaked: class=MyBusinessClass, ptr=0x7f8c9c0012a0, size=64, thread=139753982832384
[ERROR] Leaked: class=MyBusinessClass, ptr=0x7f8c9c0012f0, size=64, thread=139753982832384
[INFO] Memory leak report written to: memory_leak.log
```

## 典型使用场景

### 场景 1: 开发阶段 - 启用所有检查

```json
{
    "enable_check": true,
    "enable_statistics": true,
    "enable_memory_pool": true
}
```

运行程序，查看 `memory_leak.log` 检测泄漏。

### 场景 2: 生产环境 - 轻量级监控

```json
{
    "enable_check": false,
    "enable_statistics": true,
    "enable_memory_pool": true
}
```

定期调用 `getMemoryStats()` 监控内存趋势。

### 场景 3: 极致性能 - 禁用检查

```json
{
    "enable_check": false,
    "enable_statistics": false,
    "enable_memory_pool": true
}
```

保留池管理，但关闭检查和统计。

## IMP_OPERATOR_NEW 使用建议

### ✅ 适合使用

- **业务数据类**: 需要追踪生命周期
- **动态资源**: 文件句柄、网络连接
- **长生命周期对象**: 服务类、管理器类

### ❌ 不适合使用

- **单例类**: 永远不释放
- **栈对象**: 主要栈分配的工具类
- **POD 类型**: 简单数据结构
- **模板类**: 会为每个实例化生成追踪代码

## 性能指标

- **池分配**: ~1.5 μs/操作（666K ops/sec）
- **Magic 生成**: 一次性启动开销（<1 ms）
- **泄漏检测**: 仅在 uninitialize 时执行
- **统计收集**: 原子操作，几乎无开销

## 常见问题

### Q1: 如何禁用内存检查？

**A**: 编辑 `memory_config.json` 设置 `enable_check: false`

### Q2: 如何自定义池配置？

**A**: 根据实际分配模式调整 `pool_config` 数组：
```json
{"size": 32, "count": 256}  // 32 字节池，256 个对象
```

### Q3: 泄漏报告在哪里？

**A**: 默认生成 `memory_leak.log`，可通过环境变量 `MEMORY_LEAK_LOG_FILE` 修改路径。

### Q4: 如何与其他内存工具配合？

**A**: 
- **Valgrind**: 兼容，可同时使用
- **AddressSanitizer**: 兼容，建议开发时使用
- **Massif**: 兼容，可分析池使用

### Q5: 配置文件不存在怎么办？

**A**: 使用内置默认配置（所有功能启用），首次退出时自动生成配置文件。

## 测试命令

```bash
# 完整测试套件
./run_all_tests.sh

# 单独运行某个测试
./test_core_classes

# 使用 valgrind 验证
valgrind --leak-check=full ./test_core_classes

# 压力测试
./memory_stress_test
```

## 故障排查

### 问题: 程序崩溃在 Memory::initialize()

**原因**: 可能与全局对象初始化顺序冲突

**解决**: 
1. 确保在 main() 开头调用 initialize()
2. 避免全局对象使用 Memory 管理

### 问题: 泄漏报告没有生成

**检查**:
1. `enable_check` 是否为 true？
2. 是否调用了 `Memory::uninitialize()`？
3. 文件写权限是否正常？

### 问题: 性能下降明显

**优化**:
1. 检查 `enable_check` 是否需要关闭
2. 调整池配置匹配实际分配模式
3. 确认大对象（>1024 bytes）直接走 malloc

## 更多文档

- **完整 API**: `source/inc/CMemory.hpp`
- **实现细节**: `source/src/CMemory.cpp`
- **测试报告**: `doc/IMP_OPERATOR_NEW_TEST_REPORT.md`
- **动态 Magic**: `doc/dynamic_magic.md`
- **设计文档**: `README.md`

## 支持

如有问题，请查看测试用例：
- `test/examples/core_memory_example.cpp` - 基础使用
- `test/examples/test_core_classes.cpp` - 类级追踪
- `test/examples/memory_stress_test.cpp` - 多线程使用
