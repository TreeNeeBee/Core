# LightAP Core Module

[English](README.md) | [中文](README_CN.md)

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/std/the-standard)
[![AUTOSAR](https://img.shields.io/badge/AUTOSAR-AP%20R25--11-green.svg)](https://www.autosar.org/)
[![Build](https://img.shields.io/badge/build-passing-brightgreen.svg)](../../)
[![License](https://img.shields.io/badge/license-CC%20BY--NC%204.0-green.svg)](LICENSE)

**AUTOSAR Adaptive Platform R25-11 compliant** core module providing AUTOSAR types, zero-copy IPC, configuration management, and synchronization primitives.

**Version:** 1.1.0  
**Last Updated:** 2026-03-02

---

## Key Features

### Zero-Copy IPC
- **iceoryx2-inspired** lock-free shared memory message passing
- **Publisher/Subscriber** with loan-based zero-copy writes
- **Three modes**: SHRINK (4KB), NORMAL (2MB), EXTEND (configurable)
- **< 5µs latency** for 5MB payloads, 90+ FPS sustained
- **SPSC/SPMC/MPSC/MPMC** concurrency patterns

### AUTOSAR AP Types
- `String`, `StringView`, `Vector`, `Map`, `Optional`, `Variant`, `Span`
- `Result<T>`, `ErrorCode`, `ErrorDomain`, `Exception`
- `Future<T>`, `Promise<T>` (with `then`/`WaitFor`)
- `InstanceSpecifier` for path-based identification

### Configuration Management
- JSON format with type-safe template API
- Module isolation, hot reload (IMMEDIATE/RESTART)
- Optional HMAC-SHA256 integrity, environment variable substitution

### Synchronization
- Mutex / RecursiveMutex / Event / Semaphore
- Lock-free SPSC/MPMC queues (`CLockFreeQueue`)

---

## Quick Start

### Prerequisites

- **Compiler**: GCC 7+ / Clang 6+
- **CMake**: 3.10+
- **C++ Standard**: C++17
- **Dependencies**: nlohmann/json (included), Google Test (optional), OpenSSL (optional)

### Build

```bash
cd LightAP
mkdir build && cd build
cmake .. -DENABLE_BUILD_TESTS=ON
cmake --build . -j$(nproc)
ctest --verbose
```

### CMake Integration

```cmake
find_package(lap_core REQUIRED)
target_link_libraries(your_target PRIVATE lap::core)
```

---

## Usage

### Initialization (Required)

```cpp
#include "lap/core/CInitialization.hpp"

int main() {
    auto result = lap::core::Initialize();
    if (!result.HasValue()) return 1;

    // Application code...

    lap::core::Deinitialize();
    return 0;
}
```

### Core Types

```cpp
#include "lap/core/CString.hpp"
#include "lap/core/CResult.hpp"
#include "lap/core/CFuture.hpp"

// String
lap::core::String str = "Hello, LightAP";

// Result<T> error handling
lap::core::Result<int> divide(int a, int b) {
    if (b == 0)
        return lap::core::Result<int>::FromError(lap::core::CoreErrc::kInvalidArgument);
    return lap::core::Result<int>::FromValue(a / b);
}

// Future/Promise async
lap::core::Promise<int> promise;
lap::core::Future<int> future = promise.get_future();
promise.set_value(42);
int value = future.get();
```

### IPC Zero-Copy

```cpp
#include "lap/core/ipc/Publisher.hpp"
#include "lap/core/ipc/Subscriber.hpp"

using namespace lap::core::ipc;

// Publisher — loan & write directly to shared memory
PublisherConfig config;
config.max_chunks = 16;
config.chunk_size = 1920 * 720 * 4;  // 5.3MB
auto pub = Publisher::Create("/camera0", config).Value();

pub.Send([](void* ptr, size_t size) -> size_t {
    GenerateImageData(ptr, size);
    return size;
});

// Subscriber — zero-copy receive
auto sub = Subscriber::Create("/camera0").Value();
auto sample = sub.Receive().Value();
const auto* data = sample.GetPayload<ImageData>();
```

### Configuration

```cpp
#include "lap/core/CConfig.hpp"

auto& config = lap::core::ConfigManager::getInstance();
config.loadFromFile("config.json");

auto port = config.getValue<int>("server.port");
config.setValue("server.maxConnections", 100);
config.saveToFile("config.json");
```

---

## IPC Performance

| IPC Method | 5MB Latency | Throughput | CPU | Zero-Copy |
|------------|-------------|------------|-----|-----------|
| **LightAP IPC** | **< 5µs** | **90+ FPS** | **25%** | ✅ |
| Unix Socket | ~15ms | 60 FPS | 45% | ❌ |
| TCP Socket | ~20ms | 50 FPS | 55% | ❌ |
| Manual SHM | ~8µs | 85 FPS | 30% | ✅ |

### IPC Modes

| Mode | SHM Align | Max Subs | Max Chunks | Queue Cap | Use Case |
|------|-----------|----------|------------|-----------|----------|
| SHRINK | 4KB | 8 | 4 | 16 | Embedded |
| **NORMAL** | 2MB | 32 | 16 | 256 | **Default** |
| EXTEND | 2MB | 128 | 64 | 1024 | High-performance |

```bash
cmake -DLIGHTAP_IPC_MODE_SHRINK=ON ..   # Embedded
cmake -DLIGHTAP_IPC_MODE_EXTEND=ON ..   # High-performance
```

---

## Project Structure

```
source/
├── inc/                    # Public headers
│   ├── CConfig.hpp         # Configuration management
│   ├── CFile.hpp           # File operations
│   ├── CFuture.hpp         # Future<T> async
│   ├── CLockFreeQueue.hpp  # Lock-free queues
│   ├── CPath.hpp           # Path utilities
│   ├── CPromise.hpp        # Promise<T> async
│   ├── CResult.hpp         # Result<T> error handling
│   ├── CString.hpp         # AUTOSAR String
│   ├── CSync.hpp           # Sync primitives
│   ├── CTime.hpp           # Time utilities
│   ├── CTimer.hpp          # Timer management
│   ├── CVariant.hpp        # Variant / Optional
│   ├── ipc/                # IPC subsystem
│   │   ├── Publisher.hpp
│   │   ├── Subscriber.hpp
│   │   ├── Channel.hpp
│   │   └── ...
│   └── ...
├── src/                    # Implementation
│   ├── ipc/                # IPC implementation
│   └── ...
test/
├── unittest/               # Unit tests (25+ test files)
└── examples/               # Example programs
```

---

## AUTOSAR API Reference

| Type | AUTOSAR Ref | Description |
|------|-------------|-------------|
| `String` | SWS_CORE_01001 | Standard string |
| `StringView` | SWS_CORE_01901 | Non-owning string view |
| `Vector<T>` | SWS_CORE_01201 | Dynamic array |
| `Optional<T>` | SWS_CORE_01301 | Optional value |
| `Variant<T...>` | SWS_CORE_01601 | Type-safe union |
| `Span<T>` | SWS_CORE_01901 | Non-owning array view |
| `Result<T>` | SWS_CORE_00701 | Result or error |
| `Future<T>` | SWS_CORE_00321 | Async result |
| `Promise<T>` | SWS_CORE_00341 | Async producer |
| `ErrorCode` | SWS_CORE_00502 | Error code wrapper |
| `ErrorDomain` | SWS_CORE_00110 | Error domain base |

---

## Tests

Unit tests in `test/unittest/` cover all modules:

| Category | Description |
|----------|-------------|
| InitializationTest | Core lifecycle |
| ResultTest | Result<T> error handling |
| FutureTest | Future/Promise async |
| StringViewTest | StringView R24-11 |
| VariantTest | Variant + Optional |
| SpanTest | Span operations |
| ConfigTest | Configuration management |
| SyncTest | Mutex / Event / Semaphore |
| IPCTest | Publisher / Subscriber / Factory |
| FileTest | File I/O operations |
| PathTest | Path utilities |
| TimeTimerTest | Time and timer |

```bash
# Run from top-level build
ctest --verbose

# Run specific test
./core_test --gtest_filter=ResultTest.*
```

---

## Documentation

- [doc/](doc/) — Core module documentation
- [CHANGES.md](CHANGES.md) — Version history

---

## License

**CC BY-NC 4.0** (Creative Commons Attribution-NonCommercial 4.0)

- ✅ Permitted: Education, personal projects, modification (with attribution)
- ❌ Prohibited: Commercial use, production deployment

For commercial licensing: <https://github.com/nicx-next/LightAP>

### Third-Party Licenses

- nlohmann/json: MIT License
- Google Test: BSD 3-Clause License
- OpenSSL: Apache 2.0 License

---

<p align="center">
  <strong>AUTOSAR R25-11 · Zero-Copy IPC · C++17</strong><br>
  <sub>Built for the Adaptive Platform community · CC BY-NC 4.0</sub>
</p>
