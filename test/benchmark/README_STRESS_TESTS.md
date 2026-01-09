# IPC 压力测试工具

本目录包含用于验证 LightAP Core IPC (SPMC) 功能稳定性的多进程压力测试工具。

## 测试组件

### 1. 独立进程可执行文件

- **publisher_process**: 独立的发布者进程
  - 参数: `<service_name> <rate_msg_per_sec> <stats_file>`
  - 功能: 以指定速率发送消息，定期写入统计信息

- **subscriber_process**: 独立的订阅者进程
  - 参数: `<service_name> <subscriber_id> <stats_file>`
  - 功能: 接收消息，跟踪丢失，定期写入统计信息

### 2. 测试脚本

#### 快速验证测试 (`quick_test.sh`)
用于快速验证 IPC 功能，短时间运行。

**用法:**
```bash
cd /workspace/LightAP
./modules/Core/test/benchmark/quick_test.sh [时长(秒)] [订阅者数] [发送速率]

# 示例: 30秒测试，3个订阅者，1000 msg/s
./modules/Core/test/benchmark/quick_test.sh 30 3 1000
```

**输出示例:**
```
========================================
  快速验证测试 - SPMC
========================================
测试配置:
  测试时长: 30 秒
  订阅者数: 3
  发送速率: 1000 msg/s
========================================
...
发布者:
total_sent=15673
total_errors=0
rate=505

订阅者:
  订阅者 0: 接收=15168, 丢失=0
  订阅者 1: 接收=15116, 丢失=0
  订阅者 2: 接收=15064, 丢失=0
  总计: 接收=45348, 丢失=0
  丢失率: 0.0000%
```

#### 过夜压力测试 (`overnight_stress_test.sh`)
用于长时间稳定性测试，支持动态订阅者连接/断连。

**用法:**
```bash
cd /workspace/LightAP
./modules/Core/test/benchmark/overnight_stress_test.sh [时长(秒)] [最大订阅者数] [发送速率]

# 示例1: 8小时测试，100个订阅者，1000 msg/s
./modules/Core/test/benchmark/overnight_stress_test.sh 28800 100 1000

# 示例2: 1小时测试，50个订阅者，500 msg/s
./modules/Core/test/benchmark/overnight_stress_test.sh 3600 50 500

# 示例3: 5分钟快速验证，10个订阅者，1000 msg/s
./modules/Core/test/benchmark/overnight_stress_test.sh 300 10 1000
```

**功能特性:**
- 动态订阅者管理: 订阅者在 30-300 秒随机存活时间后自动退出，新订阅者自动补充
- 定期统计: 每 5 分钟打印一次运行统计
- 详细日志: 所有进程的日志和统计文件保存在 `/tmp/overnight_logs_*` 和 `/tmp/overnight_stats_*`
- 优雅退出: Ctrl+C 可随时终止并打印完整统计

**输出示例:**
```
========================================
  SPMC 过夜压力测试
========================================
测试配置:
  测试时长: 8小时 (480分钟)
  最大订阅者数: 100
  发送速率: 1000 msg/s
  订阅者存活时间: 30-300 秒
  统计间隔: 300 秒
========================================
...
[17:29:21] 最终统计
[17:29:21] ========================================
[17:29:21] 发布者: 发送=31578, 错误=0
[17:29:21] 订阅者: 总创建=11, 完成=11, 总接收=260312, 总丢失=0
[17:29:21] 最终丢失率: 0.0000%
```

## 编译

测试工具已集成到 CMake 构建系统：

```bash
cd /workspace/LightAP/build
cmake ..
make publisher_process subscriber_process -j$(nproc)
```

编译产物位于 `build/modules/Core/`:
- `publisher_process`
- `subscriber_process`

## 测试场景

### 1. 功能验证 (30秒)
```bash
./modules/Core/test/benchmark/quick_test.sh 30 3 1000
```

### 2. 压力测试 (5分钟)
```bash
./modules/Core/test/benchmark/overnight_stress_test.sh 300 10 1000
```

### 3. 过夜稳定性测试 (8小时)
```bash
# 推荐在 tmux/screen 中运行
./modules/Core/test/benchmark/overnight_stress_test.sh 28800 100 1000 > overnight_test.log 2>&1 &
```

### 4. 最大压力测试 (100订阅者)
```bash
./modules/Core/test/benchmark/overnight_stress_test.sh 3600 100 2000
```

## 验收标准

### 正常标准:
- ✅ 消息丢失率 < 0.01%
- ✅ 无进程崩溃或死锁
- ✅ 100个订阅者可同时连接
- ✅ 发送错误数 = 0

### 优秀标准:
- 🌟 消息丢失率 = 0%
- 🌟 8小时无任何错误
- 🌟 动态连接/断连无异常

## 统计文件格式

### 发布者统计 (`publisher.stats`):
```
type=publisher
pid=12345
elapsed_sec=30
total_sent=15673
total_errors=0
rate=505
status=running
```

### 订阅者统计 (`subscriber_*.stats`):
```
type=subscriber
id=0
pid=12346
elapsed_sec=30
total_received=15168
total_lost=0
rate=505
status=running
```

## 日志文件

所有日志和统计文件保存在:
- 日志: `/tmp/{overnight|quick_test}_logs_<timestamp>/`
- 统计: `/tmp/{overnight|quick_test}_stats_<timestamp>/`

## 故障排查

### 问题: 订阅者无法接收消息
- 检查: `/tmp/*/subscriber_*.log`
- 可能原因: 服务名不匹配、共享内存权限问题

### 问题: 发送错误数 > 0
- 检查: `/tmp/*/publisher.log`
- 可能原因: 共享内存满、订阅者队列满

### 问题: 消息丢失率 > 0%
- 检查: 订阅者日志中的 `total_lost` 字段
- 可能原因: 订阅者处理速度慢、发送速率过高

### 问题: 进程崩溃
- 检查: dmesg (可能是段错误)
- 检查: 各进程的 .log 文件最后几行

## 性能基准

基于当前实现的预期性能:
- **吞吐量**: ~1M msg/s (单发布者)
- **延迟**: < 1μs (P99)
- **最大订阅者数**: 100
- **消息丢失率**: 0% (正常负载下)

## 相关文档

- [IPC 性能比较](../../IPC_PERFORMANCE_COMPARISON.md)
- [IPC 优化计划](../../IPC_OPTIMIZATION_PLAN.md)
- [IPC 测试结果](../../IPC_TEST_RESULTS.md)
