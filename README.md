# LightAP Core Module

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/std/the-standard)
[![AUTOSAR](https://img.shields.io/badge/AUTOSAR-AP%20R24--11-orange.svg)](https://www.autosar.org/)
[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)](../../)
[![Tests](https://img.shields.io/badge/tests-395%2F397-brightgreen.svg)](test/)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

**LightAP Core** is an AUTOSAR Adaptive Platform R24-11 compliant module providing memory management, configuration management, error handling, and synchronization primitives.

English | [中文文档](README_CN.md)

---

## ✨ Key Features

### 🧠 Unified Memory Management
- **AUTOSAR Compliant Initialization** - Complete `Initialize()`/`Deinitialize()` lifecycle
- **High-Performance Memory Pools** - Optimized pool allocator for small objects (≤1024 bytes)
- **Global Interception** - Transparent `new`/`delete` override with zero code intrusion
- **Thread-Safe** - Lock-free fast path with minimal contention
- **Memory Tracking** - Built-in leak detection, statistics, and debugging
- **STL Integration** - Seamless `StlMemoryAllocator<T>` support for standard containers
- **Dynamic Alignment** - Runtime configurable alignment (1/4/8 bytes)

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

```cpp
#include "lap/core/CInitialization.hpp"
#include "lap/core/CMemory.hpp"
#include "lap/core/CResult.hpp"

int main() {
    // 1. Initialize (REQUIRED)
    auto result = lap::core::Initialize();
    if (!result.HasValue()) {
        return 1;
    }

    // 2. Use memory pools
    void* ptr = lap::core::Memory::malloc(128);
    lap::core::Memory::free(ptr);

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

## 🧠 Memory Management

### Architecture

- **6 Pool Sizes**: 32, 64, 128, 256, 512, 1024 bytes
- **O(1) Allocation**: Lock-free fast path
- **Thread-Safe**: Concurrent allocation support
- **Automatic Fallback**: System allocator for large objects
- **Leak Detection**: Built-in tracking and reporting

### Performance

**Memory Stress Test** (4 threads × 1000 operations):
```
Total: 4000 operations
Time:  6 ms
Throughput: 666,667 ops/sec
```

**Single Operation Latency** (8-byte allocation):
```
malloc:  123.51 ns
memset:    4.37 ns
read:     15.23 ns
free:     56.26 ns
──────────────────
Total:   199.36 ns
```

### Usage Examples

```cpp
#include "lap/core/CMemory.hpp"

// Direct allocation
void* ptr = lap::core::Memory::malloc(128);
lap::core::Memory::free(ptr);

// Aligned allocation
void* aligned = lap::core::Memory::memalign(16, 128);
lap::core::Memory::free(aligned);

// Global new/delete (automatic pool usage)
MyClass* obj = new MyClass();
delete obj;
```

### STL Integration

```cpp
#include "lap/core/CStlMemoryAllocator.hpp"
#include <vector>
#include <map>

// Vector with pool allocator
std::vector<int, lap::core::StlMemoryAllocator<int>> vec;
vec.push_back(42);

// Map with pool allocator
std::map<int, std::string, 
         std::less<int>,
         lap::core::StlMemoryAllocator<std::pair<const int, std::string>>> map;
map[1] = "one";
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

- **Unit Tests**: 395/397 passing (**99.5%**)
- **Integration Tests**: 13/14 passing (**92.86%**)
- **Total Tests**: 397 test cases

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
| MemoryTest | 83/83 | ✅ 100% |
| ConfigTest | 17/17 | ✅ 100% |
| AbortHandlerTest | 12/12 | ✅ 100% |
| StlMemoryAllocatorTest | 53/53 | ✅ 100% |
| MemoryFacadeTest | 20/22 | ⚠️ 90.9% |

### Running Tests

```bash
cd build/modules/Core

# Run all unit tests
./core_test

# Run specific test suite
./core_test --gtest_filter=ResultTest.*

# Run all integration tests
./run_all_tests.sh

# Individual test programs
./memory_stress_test
./pool_vs_system_benchmark
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

### Memory Management

| Function | Description |
|----------|-------------|
| `Memory::malloc(size)` | Allocate memory |
| `Memory::free(ptr)` | Free memory |
| `Memory::memalign(align, size)` | Aligned allocation |
| `Memory::calloc(n, size)` | Zero-initialized allocation |
| `Memory::realloc(ptr, size)` | Reallocate memory |
| `StlMemoryAllocator<T>` | STL allocator adapter |

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

More examples in [test/examples/](test/examples/) directory:

- `initialization_example.cpp` - Basic initialization
- `memory_example.cpp` - Memory pool usage
- `memory_example_comprehensive.cpp` - Advanced memory features
- `config_example.cpp` - Configuration management
- `config_policy_example.cpp` - Configuration policies
- `check_alignment.cpp` - Alignment verification

---

## 📄 Documentation

- **[API Reference](doc/INDEX.md)** - Complete API documentation
- **[Memory Management Guide](doc/Memory_Management_Guide.md)** - Memory architecture
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

MIT License - See [LICENSE](LICENSE) file

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

### v1.1.0 (Q4 2025)
- [ ] Fix class name registration
- [ ] Complete ara::com integration
- [ ] Enhanced crypto support
- [ ] Performance profiling tools

### v1.2.0 (Q1 2026)
- [ ] ara::exec lifecycle
- [ ] Distributed tracing
- [ ] Cloud-native features

---

**Maintained by**: LightAP Core Team  
**Version**: 1.0.0  
**Release Date**: November 13, 2025  
**Status**: ✅ Production Ready

---

[⬆ Back to Top](#lightap-core-module)
