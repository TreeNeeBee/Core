# LightAP Core 模块

[English](README.md) | [中文](README_CN.md)

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/std/the-standard)
[![AUTOSAR](https://img.shields.io/badge/AUTOSAR-AP%20R25--11-green.svg)](https://www.autosar.org/)
[![Build](https://img.shields.io/badge/build-passing-brightgreen.svg)](../../)
[![License](https://img.shields.io/badge/license-CC%20BY--NC%204.0-green.svg)](LICENSE)

**AUTOSAR Adaptive Platform R25-11 兼容**的核心模块，提供 AUTOSAR 类型、零拷贝 IPC、配置管理和同步原语。

**版本：** 1.1.0  
**最后更新：** 2026-03-02

---

## 核心特性

### 零拷贝 IPC
- **CoreIPC** — 无锁共享内存消息传递
- **Publisher/Subscriber** — 基于 loan 的零拷贝写入
- **三种模式**：SHRINK (4KB)、NORMAL (2MB)、EXTEND (可配置)
- **< 5µs 延迟**（5MB 负载），持续 90+ FPS
- **SPSC/SPMC/MPSC/MPMC** 并发模式

### AUTOSAR AP 类型
- `String`、`StringView`、`Vector`、`Map`、`Optional`、`Variant`、`Span`
- `Result<T>`、`ErrorCode`、`ErrorDomain`、`Exception`
- `Future<T>`、`Promise<T>`（支持 `then`/`WaitFor`）
- `InstanceSpecifier` 路径标识管理

### 配置管理
- JSON 格式，类型安全模板 API
- 模块隔离，热重载（IMMEDIATE/RESTART）
- 可选 HMAC-SHA256 完整性校验，环境变量替换

### 同步原语
- Mutex / RecursiveMutex / Event / Semaphore
- 无锁 SPSC/MPMC 队列（`CLockFreeQueue`）

---

## 快速开始

### 前置要求

- **编译器**：GCC 7+ / Clang 6+
- **CMake**：3.10+
- **C++ 标准**：C++17
- **依赖**：nlohmann/json（已包含）、Google Test（可选）、OpenSSL（可选）

### 构建

```bash
cd LightAP
mkdir build && cd build
cmake .. -DENABLE_BUILD_TESTS=ON
cmake --build . -j$(nproc)
ctest --verbose
```

### CMake 集成

```cmake
find_package(lap_core REQUIRED)
target_link_libraries(your_target PRIVATE lap::core)
```

---

## 使用示例

### 初始化（必须）

```cpp
#include "lap/core/CInitialization.hpp"

int main() {
    auto result = lap::core::Initialize();
    if (!result.HasValue()) return 1;

    // 应用代码...

    lap::core::Deinitialize();
    return 0;
}
```

### 核心类型

```cpp
#include "lap/core/CString.hpp"
#include "lap/core/CResult.hpp"
#include "lap/core/CFuture.hpp"

// String
lap::core::String str = "Hello, LightAP";

// Result<T> 错误处理
lap::core::Result<int> divide(int a, int b) {
    if (b == 0)
        return lap::core::Result<int>::FromError(lap::core::CoreErrc::kInvalidArgument);
    return lap::core::Result<int>::FromValue(a / b);
}

// Future/Promise 异步
lap::core::Promise<int> promise;
lap::core::Future<int> future = promise.get_future();
promise.set_value(42);
int value = future.get();
```

### IPC 零拷贝

```cpp
#include "lap/core/ipc/Publisher.hpp"
#include "lap/core/ipc/Subscriber.hpp"

using namespace lap::core::ipc;

// Publisher — loan 并直接写入共享内存
PublisherConfig config;
config.max_chunks = 16;
config.chunk_size = 1920 * 720 * 4;  // 5.3MB
auto pub = Publisher::Create("/camera0", config).Value();

pub.Send([](void* ptr, size_t size) -> size_t {
    GenerateImageData(ptr, size);
    return size;
});

// Subscriber — 零拷贝接收
auto sub = Subscriber::Create("/camera0").Value();
auto sample = sub.Receive().Value();
const auto* data = sample.GetPayload<ImageData>();
```

### 配置管理

```cpp
#include "lap/core/CConfig.hpp"

auto& config = lap::core::ConfigManager::getInstance();
config.loadFromFile("config.json");

auto port = config.getValue<int>("server.port");
config.setValue("server.maxConnections", 100);
config.saveToFile("config.json");
```

---

## IPC 性能

| IPC 方式 | 5MB 延迟 | 吞吐量 | CPU | 零拷贝 |
|----------|----------|--------|-----|--------|
| **LightAP IPC** | **< 5µs** | **90+ FPS** | **25%** | ✅ |
| Unix Socket | ~15ms | 60 FPS | 45% | ❌ |
| TCP Socket | ~20ms | 50 FPS | 55% | ❌ |
| 手动 SHM | ~8µs | 85 FPS | 30% | ✅ |

### IPC 模式

| 模式 | SHM 对齐 | 最大订阅者 | 最大 Chunk | 队列容量 | 适用场景 |
|------|----------|-----------|-----------|---------|---------|
| SHRINK | 4KB | 8 | 4 | 16 | 嵌入式 |
| **NORMAL** | 2MB | 32 | 16 | 256 | **默认** |
| EXTEND | 2MB | 128 | 64 | 1024 | 高性能 |

```bash
cmake -DLIGHTAP_IPC_MODE_SHRINK=ON ..   # 嵌入式
cmake -DLIGHTAP_IPC_MODE_EXTEND=ON ..   # 高性能
```

---

## 项目结构

```
source/
├── inc/                    # 公共头文件
│   ├── CConfig.hpp         # 配置管理
│   ├── CFile.hpp           # 文件操作
│   ├── CFuture.hpp         # Future<T> 异步
│   ├── CLockFreeQueue.hpp  # 无锁队列
│   ├── CPath.hpp           # 路径工具
│   ├── CPromise.hpp        # Promise<T> 异步
│   ├── CResult.hpp         # Result<T> 错误处理
│   ├── CString.hpp         # AUTOSAR String
│   ├── CSync.hpp           # 同步原语
│   ├── CTime.hpp           # 时间工具
│   ├── CTimer.hpp          # 定时器管理
│   ├── CVariant.hpp        # Variant / Optional
│   ├── ipc/                # IPC 子系统
│   │   ├── Publisher.hpp
│   │   ├── Subscriber.hpp
│   │   ├── Channel.hpp
│   │   └── ...
│   └── ...
├── src/                    # 实现源码
│   ├── ipc/                # IPC 实现
│   └── ...
test/
├── unittest/               # 单元测试（25+ 测试文件）
└── examples/               # 示例程序
```

---

## AUTOSAR API 参考

| 类型 | AUTOSAR 参考 | 说明 |
|------|-------------|------|
| `String` | SWS_CORE_01001 | 标准字符串 |
| `StringView` | SWS_CORE_01901 | 非拥有字符串视图 |
| `Vector<T>` | SWS_CORE_01201 | 动态数组 |
| `Optional<T>` | SWS_CORE_01301 | 可选值 |
| `Variant<T...>` | SWS_CORE_01601 | 类型安全联合体 |
| `Span<T>` | SWS_CORE_01901 | 非拥有数组视图 |
| `Result<T>` | SWS_CORE_00701 | 结果或错误 |
| `Future<T>` | SWS_CORE_00321 | 异步结果 |
| `Promise<T>` | SWS_CORE_00341 | 异步生产者 |
| `ErrorCode` | SWS_CORE_00502 | 错误码封装 |
| `ErrorDomain` | SWS_CORE_00110 | 错误域基类 |

---

## 测试

`test/unittest/` 下的单元测试覆盖所有模块：

| 类别 | 说明 |
|------|------|
| InitializationTest | Core 生命周期 |
| ResultTest | Result<T> 错误处理 |
| FutureTest | Future/Promise 异步 |
| StringViewTest | StringView R24-11 |
| VariantTest | Variant + Optional |
| SpanTest | Span 操作 |
| ConfigTest | 配置管理 |
| SyncTest | Mutex / Event / Semaphore |
| IPCTest | Publisher / Subscriber / Factory |
| FileTest | 文件 I/O |
| PathTest | 路径工具 |
| TimeTimerTest | 时间和定时器 |

```bash
# 从顶层构建目录运行
ctest --verbose

# 运行特定测试
./core_test --gtest_filter=ResultTest.*
```

---

## 文档

- [doc/](doc/) — Core 模块文档
- [CHANGES.md](CHANGES.md) — 版本历史

---

## 许可证

**CC BY-NC 4.0**（知识共享 署名-非商业性使用 4.0）

- ✅ 允许：教育、个人项目、修改（需署名）
- ❌ 禁止：商业使用、生产部署

商业授权：<https://github.com/nicx-next/LightAP>

### 第三方许可

- nlohmann/json：MIT License
- Google Test：BSD 3-Clause License
- OpenSSL：Apache 2.0 License

---

<p align="center">
  <strong>AUTOSAR R25-11 · 零拷贝 IPC · C++17</strong><br>
  <sub>为自适应平台社区而生 · CC BY-NC 4.0</sub>
</p>
