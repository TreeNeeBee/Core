# jemalloc vs std::malloc 性能对比报告

## 测试环境
- **CPU**: ARM64 (aarch64)
- **系统**: Linux
- **编译器**: GCC 14.2.0 with -O3
- **jemalloc版本**: 5.3.0
- **测试日期**: 2025-12-26

---

## 性能对比结果

### 1. 单线程malloc/free吞吐量 (1KB分配)

| 分配器 | 吞吐量 (K ops/sec) | 平均延迟 (μs) | P99延迟 (μs) | 相对性能 |
|--------|-------------------|--------------|-------------|----------|
| **std::malloc** | 14,438 | 0.03 | 0.04 | 100% (基准) |
| **jemalloc** | 13,676 | 0.03 | 0.08 | **95%** ⬇️ |

**结论**: jemalloc在单线程小对象分配上**略慢** 5%

---

### 2. 多线程malloc/free吞吐量 (1KB, 4线程)

| 分配器 | 吞吐量 (K ops/sec) | 平均延迟 (μs) | P99延迟 (μs) | 相对性能 |
|--------|-------------------|--------------|-------------|----------|
| **std::malloc** | 84,962 | 0.02 | 0.04 | 100% (基准) |
| **jemalloc** | 80,128 | 0.02 | 0.04 | **94%** ⬇️ |

**结论**: jemalloc在多线程场景下**略慢** 6%

---

### 3. 不同大小对象的对比

#### std::malloc

| 大小 | 吞吐量 (K ops/sec) | 延迟 (μs) |
|------|-------------------|----------|
| 1KB  | 14,438            | 0.03     |
| 4KB  | 20,636            | 0.03     |
| 64KB | 22,665            | 0.03     |

#### jemalloc (使用LD_PRELOAD)

| 大小 | 吞吐量 (K ops/sec) | 延迟 (μs) |
|------|-------------------|----------|
| 1KB  | 13,676            | 0.03     |
| 4KB  | 16,720            | 0.03     |
| 64KB | 5,236             | 0.16     |

**关键发现**: 
- ✅ 小对象(1KB-4KB): jemalloc性能接近，略慢5-20%
- ❌ **大对象(64KB)**: jemalloc性能下降**77%** (22,665 → 5,236)

---

## 详细分析

### 为什么jemalloc在这个测试中较慢？

#### 1. **测试特点不适合jemalloc**

我们的benchmark是：
```cpp
void* ptr = malloc(size);
memset(ptr, 0xAB, size);  // 触发真实分配
free(ptr);
```

这是**高频率小对象分配/释放**场景，特点：
- 分配后立即释放
- 无长期持有
- 单线程或低竞争

**std::malloc (glibc)的优势**:
- ✅ Thread-local cache（tcache）非常快
- ✅ 对小对象优化极致
- ✅ 分配/释放在同一线程，fastbin路径极快

**jemalloc的设计目标**:
- 🎯 减少内存碎片（长期运行）
- 🎯 多线程高竞争场景
- 🎯 大量并发分配器线程
- 🎯 更好的内存统计

#### 2. **ARM64架构的影响**

在ARM64上：
- glibc malloc针对ARM做了优化
- jemalloc可能没有ARM特定优化
- 原子操作成本可能不同

#### 3. **64KB大对象慢的原因**

jemalloc的大对象分配策略：
- 使用mmap直接从系统分配
- 每次都走系统调用
- 而glibc会尝试从堆扩展

---

## jemalloc的真正优势场景

### ✅ 适合使用jemalloc的场景

1. **长期运行服务**
   - 减少内存碎片
   - 更稳定的内存占用
   - 例如：数据库、Web服务器

2. **多线程高竞争**
   - 数十个线程同时分配
   - 跨线程频繁传递对象
   - 例如：Redis、RocksDB

3. **内存分析需求**
   - jemalloc提供详细统计
   - 内存泄漏检测
   - Profiling支持

4. **大型应用**
   - Firefox使用jemalloc
   - Facebook内部广泛使用
   - 对长期稳定性要求高

### ❌ 不适合使用jemalloc的场景

1. **短生命周期进程**
   - 快速启动/关闭
   - 本benchmark就是这种情况

2. **单线程密集计算**
   - tcache已经很快
   - jemalloc overhead反而更大

3. **嵌入式/移动设备**
   - jemalloc内存开销较大
   - 启动时间较长

---

## 对SharedMemoryAllocator的影响

### SharedMemoryAllocator测试结果对比

#### 使用std::malloc

| 场景 | 吞吐量 (K ops/sec) | 延迟 (μs) |
|------|-------------------|----------|
| SHM单线程(1KB) | 2,270 | 0.42 |
| SHM多线程(4线程) | 8,156 | 0.42 |

#### 使用jemalloc

| 场景 | 吞吐量 (K ops/sec) | 延迟 (μs) |
|------|-------------------|----------|
| SHM单线程(1KB) | 2,428 | 0.39 |
| SHM多线程(4线程) | 7,944 | 0.46 |

**结论**: 
- ✅ SHM单线程用jemalloc**略快7%** (2270 → 2428)
- ❌ SHM多线程用jemalloc**略慢3%** (8156 → 7944)
- 📊 **差异不大**，因为SHM自己管理池，很少调用系统malloc

---

## 推荐配置

### 对于LightAP项目

#### 推荐：**使用std::malloc（系统默认）**

**理由**:
1. ✅ 性能在benchmark中更好（5-6%）
2. ✅ 零额外依赖
3. ✅ 与系统集成更好
4. ✅ SharedMemoryAllocator自己管理主要内存池

#### 可选：在以下情况启用jemalloc

```bash
# 编译时启用jemalloc
cmake -DLAP_USE_JEMALLOC=ON ..

# 或运行时启用
LD_PRELOAD=/usr/lib/libjemalloc.so.2 ./your_app
```

**适用场景**:
- 长期运行的守护进程
- 需要内存分析
- 多个模块高频分配（非SHM池）

---

## 实验建议

### 更公平的benchmark设计

当前benchmark的问题：
- ❌ 立即释放不真实
- ❌ 没有测试长期碎片
- ❌ 没有测试跨线程传递

**改进方案**:
```cpp
// 1. 模拟真实应用：持有一段时间
std::vector<void*> held;
for (int i = 0; i < 1000; ++i) {
    held.push_back(malloc(random_size()));
    if (held.size() > 100) {
        free(held.front());
        held.erase(held.begin());
    }
}

// 2. 跨线程传递
std::queue<void*> shared_queue;
// Thread 1: 分配并入队
// Thread 2: 出队并释放

// 3. 长期运行测试
for (int hour = 0; hour < 24; ++hour) {
    run_allocation_workload();
    check_memory_fragmentation();
}
```

---

## 结论总结

### 关键发现

| 指标 | std::malloc | jemalloc | 差异 |
|------|------------|----------|------|
| 单线程1KB | 14,438 K/s | 13,676 K/s | jemalloc **慢5%** |
| 多线程1KB | 84,962 K/s | 80,128 K/s | jemalloc **慢6%** |
| 64KB大对象 | 22,665 K/s | 5,236 K/s | jemalloc **慢77%** |
| SHM影响 | 基准 | ±3-7% | **可忽略** |

### 最终建议

**对于LightAP**:
1. ✅ **默认使用std::malloc**
   - 性能更好
   - 零依赖
   
2. ⚙️ **保留jemalloc选项**
   - 通过宏定义支持
   - 用于特定部署场景

3. 🎯 **重点优化SHM**
   - SHM池才是高频路径
   - 系统malloc只是fallback
   - 继续优化lock-free算法

**性能不是唯一标准**，jemalloc的真正价值在于：
- 长期稳定性
- 内存碎片控制
- 调试和分析能力

对于**短生命周期、性能敏感**的场景，std::malloc确实更优。

---

## 附录：测试原始数据

### std::malloc (baseline)
```
[TEST 1] malloc/free (1KB):  14,438 K ops/sec, 0.03μs
[TEST 1] malloc/free (4KB):  20,636 K ops/sec, 0.03μs
[TEST 1] malloc/free (64KB): 22,665 K ops/sec, 0.03μs

[TEST 3] Multi-threaded (4 threads): 84,962 K ops/sec
[TEST 2] SHM (1KB): 2,270 K ops/sec, 0.42μs
```

### jemalloc (LD_PRELOAD)
```
[TEST 1] malloc/free (1KB):  13,676 K ops/sec, 0.03μs
[TEST 1] malloc/free (4KB):  16,720 K ops/sec, 0.03μs
[TEST 1] malloc/free (64KB): 5,236 K ops/sec, 0.16μs

[TEST 3] Multi-threaded (4 threads): 80,128 K ops/sec
[TEST 2] SHM (1KB): 2,428 K ops/sec, 0.39μs
```

**测试完成时间**: 2025-12-26
