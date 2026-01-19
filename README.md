# LightAP Core Module

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/std/the-standard)
[![AUTOSAR](https://img.shields.io/badge/AUTOSAR-AP%20R24--11-orange.svg)](https://www.autosar.org/)
[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)](../../)
[![Tests](https://img.shields.io/badge/tests-250%2B-brightgreen.svg)](test/)
[![License](https://img.shields.io/badge/license-CC%20BY--NC%204.0-green.svg)](LICENSE)

**LightAP Core** is an AUTOSAR Adaptive Platform R24-11 compliant module providing memory management, configuration management, error handling, and synchronization primitives.

English | [中文文档](README_CN.md)

---

## ✨ Key Features

### 🔥 Zero-Copy IPC (Inter-Process Communication)
- **iceoryx2-Inspired Design** - Lock-free zero-copy message passing
- **Three Modes** - SHRINK (4KB), NORMAL (2MB), EXTEND (configurable)
- **Publisher/Subscriber API** - Simple and efficient Pub-Sub pattern
- **Loan-Based Zero-Copy** - Direct write to shared memory
- **Lock-Free Queues** - High-performance ring buffer (RingBufferBlock)
- **SPSC/SPMC/MPSC/MPMC** - Full concurrency pattern support
- **Ultra-Low Latency** - < 5μs message delivery (5MB payload)
- **High Throughput** - 90+ FPS sustained (1920x720x4 images)
- **Atomic Reference Counting** - Safe multi-subscriber message sharing
- **Overwrite/Block Policies** - Flexible queue full handling

### 🏛️ AUTOSAR Adaptive Platform Types
- **Core Types**: `String`, `StringView`, `Vector`, `Map`, `Optional`, `Variant`, `Span`
- **Functional Programming**: `Result<T>`, `ErrorCode`, `ErrorDomain`, `Exception`
- **Async Operations**: `Future<T>`, `Promise<T>` (supports `then`/`WaitFor`)
- **Instance Identification**: `InstanceSpecifier` path and identifier management

### ⚙️ Configuration Management
- **JSON Format** - Human-readable configuration files
- **Type-Safe API** - Template-based type checking
- **Module Isolation** - Separate namespace for each module
- **Hot Reload** - IMMEDIATE/RESTART reload policies
- **HMAC Verification** - Optional HMAC-SHA256 integrity check
- **Environment Variables** - Automatic variable substitution
- **Auto-Save** - RAII-based automatic configuration persistence

### 🔒 Synchronization Primitives
- **Mutex/RecursiveMutex** - Standard and recursive mutex locks
- **Event** - Manual/automatic reset synchronization events
- **Semaphore** - Counting semaphores for resource management
- **Lock-Free Queue** - High-performance SPSC/MPMC queues

### 🛠️ System Utilities
- **File Operations** - POSIX-compatible file I/O
- **Time/Timer** - High-resolution time and timer management
- **Binary Serialization** - Efficient binary data serialization
- **Thread Management** - Thread utilities and helpers

---

## 🚀 Quick Start

### Prerequisites
- **Compiler**: GCC 7+ / Clang 6+ / MSVC 2017+
- **CMake**: 3.16+
- **C++ Standard**: C++17
- **Dependencies**: nlohmann/json (included), Google Test (optional), OpenSSL (optional)

### Build and Install

```bash
# Clone repository
git clone https://github.com/TreeNeeBee/LightAP.git
cd LightAP/modules/Core

# Build
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)

# Test
./core_test
./run_all_tests.sh

# Install (optional)
sudo cmake --install . --prefix /usr/local
```

### Basic Usage

#### Core Module Initialization

```cpp
#include "lap/core/CInitialization.hpp"
#include "lap/core/CResult.hpp"
#include "lap/core/CString.hpp"

int main() {
    // 1. Initialize (REQUIRED)
    auto result = lap::core::Initialize();
    if (!result.HasValue()) {
        return 1;
    }

    // 2. Use AUTOSAR types
    lap::core::String str = "Hello, LightAP";
    lap::core::Vector<int> vec = {1, 2, 3};

    // 3. Use Result<T> for error handling
    lap::core::Result<int> value = lap::core::Result<int>::FromValue(42);
    if (value.HasValue()) {
        std::cout << "Value: " << value.Value() << "\n";
    }

    // 4. Cleanup (REQUIRED)
    lap::core::Deinitialize();
    return 0;
}
```

#### IPC Zero-Copy Communication

```cpp
#include "lap/core/ipc/Publisher.hpp"
#include "lap/core/ipc/Subscriber.hpp"
#include "lap/core/ipc/Message.hpp"

using namespace lap::core::ipc;

// Define message type
class SensorData : public Message {
public:
    int32_t temperature = 0;
    int32_t humidity = 0;
    
    size_t OnMessageSend(void* chunk_ptr, size_t chunk_size) noexcept override {
        std::memcpy(chunk_ptr, this, sizeof(SensorData));
        return sizeof(SensorData);
    }
    
    bool OnMessageReceived(const void* chunk_ptr, size_t chunk_size) noexcept override {
        std::memcpy(this, chunk_ptr, sizeof(SensorData));
        return true;
    }
};

int main() {
    // Publisher
    PublisherConfig config;
    config.max_chunks = 16;
    config.chunk_size = sizeof(SensorData);
    auto pub = Publisher::Create("/sensor_data", config).Value();
    
    // Send with Lambda (zero-copy)
    pub.Send([](void* chunk_ptr, size_t) -> size_t {
        SensorData* data = static_cast<SensorData*>(chunk_ptr);
        data->temperature = 25;
        data->humidity = 60;
        return sizeof(SensorData);
    });
    
    // Subscriber
    auto sub = Subscriber::Create("/sensor_data").Value();
    auto sample = sub.Receive().Value();
    const SensorData* data = sample.GetPayload<SensorData>();
    
    return 0;
}
```

### CMake Integration

```cmake
find_package(lap_core REQUIRED)
target_link_libraries(your_target PRIVATE lap::core)
```

---

## 📚 AUTOSAR Compliance

### Initialization Lifecycle (SWS_CORE_15003/15004)

All applications **MUST** call `Initialize()` at startup and `Deinitialize()` before exit:

```cpp
#include "lap/core/CInitialization.hpp"

int main() {
    auto result = lap::core::Initialize();
    if (!result.HasValue()) {
        return 1;
    }
    
    // Your application code here
    
    lap::core::Deinitialize();
    return 0;
}
```

### Core Types

```cpp
// String Types (SWS_CORE_01xxx)
lap::core::String str = "Hello";
lap::core::StringView view = str;

// Optional (SWS_CORE_01301)
lap::core::Optional<int> opt = 42;
if (opt.has_value()) {
    std::cout << opt.value() << "\n";
}

// Variant (SWS_CORE_01601)
lap::core::Variant<int, std::string> var = 42;
if (lap::core::holds_alternative<int>(var)) {
    int value = lap::core::get<int>(var);
}

// Result (SWS_CORE_00701)
lap::core::Result<int> divide(int a, int b) {
    if (b == 0) {
        return lap::core::Result<int>::FromError(
            lap::core::CoreErrc::kInvalidArgument);
    }
    return lap::core::Result<int>::FromValue(a / b);
}

// Future/Promise (SWS_CORE_00321/00341)
lap::core::Promise<int> promise;
lap::core::Future<int> future = promise.get_future();
promise.set_value(42);
int value = future.get();
```

---

## 🚀 IPC Zero-Copy Examples

```cpp
#include "lap/core/ipc/Publisher.hpp"
#include "lap/core/ipc/Subscriber.hpp"

// Publisher side
auto pub_result = lap::core::Publisher::Create("camera");
auto& publisher = pub_result.Value();

// Loan memory from shared pool (zero-copy)
auto loan_result = publisher.Loan();
if (loan_result.HasValue()) {
    auto& message = loan_result.Value();
    // Write directly to shared memory
    message.SetHeader("image", 5242880);  // 5MB
    message.SetPayload(image_data, image_size);
    publisher.Send(std::move(message));
}

// Subscriber side
auto sub_result = lap::core::Subscriber::Create("camera");
auto& subscriber = sub_result.Value();

auto sample = subscriber.Receive();
if (sample.HasValue()) {
    auto& msg = sample.Value();
    // Access shared memory directly (zero-copy)
    ProcessImage(msg.GetPayload(), msg.GetPayloadSize());
}
```

### AUTOSAR Types

```cpp
#include "lap/core/CString.hpp"
#include "lap/core/CVector.hpp"
#include "lap/core/COptional.hpp"

// Vector
lap::core::Vector<int> vec = {1, 2, 3};
vec.push_back(4);

// String
lap::core::String str = "Hello";
str += " World";

// Optional
lap::core::Optional<int> opt = lap::core::nullopt;
if (!opt.HasValue()) {
    opt = 42;
}
```

---

## 🚀 IPC Performance

### Zero-Copy Message Delivery

**camera_fusion_example** (NORMAL mode, 3 cameras):
```
Configuration:
- Payload Size:    1920x720x4 = 5.3 MB
- Max Chunks:      16
- Queue Capacity:  64
- STMin Throttle:  10ms (100 FPS limit)

Results:
- Publisher FPS:   90-95 FPS (sustained)
- Message Latency: < 5 μs (Loan+Send)
- Receive Latency: < 2 μs (Receive+memcpy)
- CPU Usage:       25-30% (3 Pub + 4 Sub threads)
- Memory Usage:    97 MB (49MB SHM + 48MB heap)
- 8-hour Test:     Stable, 0% frame loss
```

### Performance Comparison

| IPC Method | 5MB Latency | Throughput | CPU | Zero-Copy |
|------------|-------------|------------|-----|----------|
| **LightAP IPC** | **< 5μs** | **90+ FPS** | **25%** | ✅ |
| Unix Socket | ~15ms | 60 FPS | 45% | ❌ |
| TCP Socket | ~20ms | 50 FPS | 55% | ❌ |
| Shared Memory (manual) | ~8μs | 85 FPS | 30% | ✅ |

### IPC Usage Examples

```cpp
using namespace lap::core::ipc;

// Example 1: Small messages (< 1KB)
PublisherConfig config;
config.max_chunks = 256;
config.chunk_size = 1024;
auto pub = Publisher::Create("/small_msg", config).Value();

pub.Send([](void* ptr, size_t) {
    std::memcpy(ptr, "Hello IPC", 10);
    return 10;
});

// Example 2: Large images (5MB)
PublisherConfig img_config;
img_config.max_chunks = 16;
img_config.chunk_size = 1920*720*4;  // 5.3MB
img_config.loan_policy = LoanPolicy::kWait;
auto img_pub = Publisher::Create("/camera0", img_config).Value();

img_pub.Send([](void* ptr, size_t size) -> size_t {
    GenerateImageData(ptr, size);
    return size;
});

// Example 3: Subscriber with timeout
auto sub = Subscriber::Create("/sensor_data").Value();
auto sample = sub.ReceiveWithTimeout(100000000);  // 100ms
if (sample.HasValue()) {
    ProcessData(sample.Value().GetRawPayload());
}
```

### IPC Modes

| Mode | SHM Alignment | Max Subs | Max Chunks | Queue Cap | Use Case |
|------|---------------|----------|------------|-----------|----------|
| **SHRINK** | 4KB | 8 | 4 | 16 | Embedded systems |
| **NORMAL** | 2MB | 32 | 16 | 256 | **Default, balanced** |
| **EXTEND** | 2MB | 128 | 64 | 1024 | High-performance servers |

```bash
# Build with specific IPC mode
cmake -DLIGHTAP_IPC_MODE_SHRINK=ON ..
cmake -DLIGHTAP_IPC_MODE_EXTEND=ON ..
```

---

## ⚙️ Configuration Management

```cpp
#include "lap/core/CConfig.hpp"

// Get configuration instance
auto& config = lap::core::ConfigManager::getInstance();

// Load from file
config.loadFromFile("config.json");

// Get values
auto port = config.getValue<int>("server.port");
auto host = config.getValue<std::string>("server.host");

// Set values
config.setValue("server.maxConnections", 100);

// Save
config.saveToFile("config.json");
```

### Configuration File Format

```json
{
  "server": {
    "port": 8080,
    "host": "localhost",
    "maxConnections": 100
  },
  "logging": {
    "level": "info",
    "file": "/var/log/app.log"
  }
}
```

---

## 🧪 Testing

### Test Coverage

- **Unit Tests**: 242/242 passing (**100%**)
- **IPC Tests**: 8/8 passing (**100%**)
- **Integration Tests**: 13/14 passing (**92.86%**)
- **Total Tests**: 250+ test cases

### Test Categories

| Category | Tests | Status |
|----------|-------|--------|
| InitializationTest | 2/2 | ✅ 100% |
| CoreErrorTest | 4/4 | ✅ 100% |
| StringViewTest | 30/30 | ✅ 100% |
| SpanTest | 26/26 | ✅ 100% |
| VariantTest | 44/44 | ✅ 100% |
| OptionalTest | 36/36 | ✅ 100% |
| ResultTest | 50/50 | ✅ 100% |
| FutureTest | 16/16 | ✅ 100% |
| ConfigTest | 17/17 | ✅ 100% |
| AbortHandlerTest | 12/12 | ✅ 100% |
| **IPCTest** | **8/8** | **✅ 100%** |

### Running Tests

```bash
cd build/modules/Core

# Run all unit tests
./core_test

# Run specific test suite
./core_test --gtest_filter=ResultTest.*

# Run IPC tests
./ipc_test

# Run IPC examples
./camera_fusion_example 5        # 5-second test
./stress_test_shrink            # SHRINK mode stress test
./stress_test_extend            # EXTEND mode stress test
./ipc_chain_example             # Publisher chain demo
./config_example                # Config integration demo

# Run all integration tests
./run_all_tests.sh
```

---

## 📚 API Reference

### Initialization

| Function | Description |
|----------|-------------|
| `Initialize()` | Initialize Core module (REQUIRED) |
| `Deinitialize()` | Cleanup Core module (REQUIRED) |

### Core Types

| Type | AUTOSAR Reference | Description |
|------|-------------------|-------------|
| `String` | SWS_CORE_01001 | Standard string |
| `StringView` | SWS_CORE_01901 | Non-owning string view |
| `Vector<T>` | SWS_CORE_01201 | Dynamic array |
| `Map<K,V>` | SWS_CORE_01201 | Key-value map |
| `Optional<T>` | SWS_CORE_01301 | Optional value |
| `Variant<T...>` | SWS_CORE_01601 | Type-safe union |
| `Span<T>` | SWS_CORE_01901 | Non-owning array view |
| `Result<T>` | SWS_CORE_00701 | Result or error |
| `Future<T>` | SWS_CORE_00321 | Async result |
| `Promise<T>` | SWS_CORE_00341 | Async producer |

### Error Handling

| Type | AUTOSAR Reference | Description |
|------|-------------------|-------------|
| `ErrorCode` | SWS_CORE_00502 | Error code wrapper |
| `ErrorDomain` | SWS_CORE_00110 | Error domain base |
| `CoreErrc` | SWS_CORE_00511 | Core error codes |
| `Exception` | SWS_CORE_00601 | Exception base class |

---

## 🔨 Building

### Build Options

```bash
# Debug build
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Release build
cmake .. -DCMAKE_BUILD_TYPE=Release

# Disable tests
cmake .. -DBUILD_TESTING=OFF

# Shared library
cmake .. -DBUILD_SHARED_LIBS=ON
```

### Build Targets

```bash
cmake --build . --target all          # Build everything
cmake --build . --target lap_core     # Library only
cmake --build . --target core_test    # Tests
cmake --build . --target install      # Install
```

---

## 📖 Examples

More examples in [test/examples/](test/examples/) and [test/ipc/](test/ipc/) directories:

**Core Examples:**
- `initialization_example.cpp` - Basic initialization
- `config_example.cpp` - Configuration management
- `config_policy_example.cpp` - Configuration policies

**IPC Examples:**
- `camera_fusion_example.cpp` - ⭐ **3-camera fusion with zero-copy** (5.3MB images @ 90 FPS)
- `stress_test_shrink.cpp` - SHRINK mode stress test
- `stress_test_extend.cpp` - EXTEND mode stress test
- `ipc_chain_example.cpp` - Multi-hop Publisher chain
- `config_example.cpp` - IPC config integration

---

## 📄 Documentation

- **[API Reference](doc/INDEX.md)** - Complete API documentation
- **[IPC Design Architecture](doc/IPC_DESIGN_ARCHITECTURE.md)** - Zero-copy IPC design
- **[Build Guide](BUILDING.md)** - Detailed build instructions
- **[Release Notes](RELEASE_NOTES.md)** - Version history

---

## 🤝 Contributing

We welcome contributions! Please:

1. Fork the repository
2. Create a feature branch
3. Commit your changes
4. Push to the branch
5. Submit a Pull Request

### Code Style

- Follow AUTOSAR C++ guidelines
- Use C++17 features appropriately
- Add unit tests for new features
- Maintain code coverage

---

## 📄 License

CC BY-NC 4.0 (Creative Commons Attribution-NonCommercial 4.0 International) - See [LICENSE](LICENSE) file

For commercial use or production deployment, please contact the copyright holder to obtain a separate commercial license.

### Third-Party Licenses

- nlohmann/json: MIT License
- Google Test: BSD 3-Clause License
- OpenSSL: Apache 2.0 License

---

## 📞 Support

- **Issues**: [GitHub Issues](https://github.com/TreeNeeBee/LightAP/issues)
- **Discussions**: [GitHub Discussions](https://github.com/TreeNeeBee/LightAP/discussions)
- **Email**: ddkv587@gmail.com
- **GitHub**: https://github.com/TreeNeeBee/LightAP

---

## 🗺️ Roadmap

### v1.1.0 (January 2026) - ✅ COMPLETED
- [x] Zero-copy IPC (Publisher/Subscriber)
- [x] Three IPC modes (SHRINK/NORMAL/EXTEND)
- [x] Lock-free ring buffer queues
- [x] camera_fusion_example (90+ FPS)
- [x] 8-hour stress test validation

### v1.2.0 (Q2 2026)
- [ ] Fix class name registration
- [ ] Complete ara::com integration
- [ ] IPC WaitSet mechanism (futex-based)
- [ ] E2E protection hooks
- [ ] Performance profiling tools

### v1.3.0 (Q3 2026)
- [ ] ara::exec lifecycle
- [ ] Distributed tracing
- [ ] Cloud-native features

---

**Maintained by**: LightAP Core Team  
**Version**: 1.1.0  
**Release Date**: January 19, 2026  
**Status**: ✅ Production Ready (with IPC)

---

[⬆ Back to Top](#lightap-core-module)
