# LightAP Core Module

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/std/the-standard)
[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)](../../)
[![Tests](https://img.shields.io/badge/tests-294%2F294-brightgreen.svg)](test/)
[![AUTOSAR](https://img.shields.io/badge/AUTOSAR-AP%20R23--11-orange.svg)](https://www.autosar.org/)
[![License](https://img.shields.io/badge/license-Proprietary-red.svg)](LICENSE)

**Core** provides fundamental building blocks for the LightAP middleware platform, offering AUTOSAR Adaptive Platform compliant APIs for memory management, configuration, synchronization, and functional programming utilities.

---

## 🎯 Key Features

### 🧠 Unified Memory Management
- **Global operator interception** - Transparent pool-based allocation for all `new`/`delete` operations
- **High-performance pools** - Optimized for small objects (≤1024 bytes) with configurable size classes
- **Thread-safe** - Lock-free fast paths with minimal contention
- **Memory tracking** - Built-in leak detection, statistics, and debugging support
- **STL integration** - Custom allocators for seamless use with Standard Library containers
- **Dynamic alignment** - Runtime-configurable alignment (1-128 bytes)

### 🏛️ AUTOSAR Adaptive Platform Compliance
- **Core types**: `String`, `Vector`, `Map`, `Optional`, `Variant`, `Span`
- **Functional programming**: `Result<T>`, `ErrorCode`, `ErrorDomain`, `Exception`
- **Asynchronous operations**: `Future<T>`, `Promise<T>`
- **Abort handling**: Full compliance with AUTOSAR SWS_CORE_00051-00054
- **Instance Specifier**: AUTOSAR identifier and path management

### ⚙️ Configuration Management
- **JSON-based** - Human-readable, schema-free configuration
- **Type-safe API** - Strongly-typed getters and setters with validation
- **Metadata support** - `__metadata__` section for security and audit trails
- **Flexible reload policies** - Hot reload with configurable update strategies
- **HMAC security** - Optional cryptographic verification (HMAC-SHA256)
- **Environment integration** - Environment variable substitution and overrides

### 🔄 Synchronization Primitives
- **Mutex** - Standard and recursive locking with RAII support
- **Event** - Manual/auto-reset event signaling for thread coordination
- **Semaphore** - Counting semaphore with timeout and try-acquire
- **Lock-free queue** - High-throughput SPSC/MPMC queue for inter-thread communication

### 🛠️ System Utilities
- **File & Path** - POSIX-compliant file operations with modern C++ interface
- **Time & Timer** - High-resolution clocks, durations, and one-shot/periodic timers
- **Serialization** - Binary serialization for network and storage
- **Thread tools** - Thread naming, affinity, and management helpers

---

## 📦 Quick Start

### Building within LightAP

```bash
cd /path/to/LightAP
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make lap_core -j$(nproc)
```

### Standalone Build (with BuildTemplate)

Core can be built independently using BuildTemplate as a Git submodule:

```bash
# 1. Clone or enter Core repository
cd /path/to/Core

# 2. Initialize BuildTemplate submodule (SSH)
git submodule add git@github.com:TreeNeeBee/BuildTemplate.git BuildTemplate
git submodule update --init --recursive

# 3. Configure and build
mkdir -p _build && cd _build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)

# 4. Run tests
./core_test

# 5. Install (optional)
sudo cmake --install . --prefix /usr/local
```

**BuildTemplate Configuration** (`.gitmodules`):
```ini
[submodule "BuildTemplate"]
    path = BuildTemplate
    url = git@github.com:TreeNeeBee/BuildTemplate.git
    branch = master
```

### Integration with CMake

```cmake
# Find and link lap_core
find_package(lap_core REQUIRED)
target_link_libraries(your_target PRIVATE lap::core)

# Or use direct linking in LightAP context
target_link_libraries(your_target PRIVATE lap_core)
target_include_directories(your_target PRIVATE 
    ${CMAKE_SOURCE_DIR}/modules/Core/source/inc
)
```

---

## 💡 Usage Examples

### Memory Management
```cpp
#include "CMemory.hpp"
using namespace lap::core;

// Automatic pool-based allocation (transparent)
auto* obj = new MyClass();  // Routed through Memory pool
delete obj;                  // Thread-safe deallocation

// STL containers with custom allocator
Vector<int> vec;            // Uses lap_core memory pool
vec.push_back(42);
Map<String, int> map;       // Efficient small-object allocation
```

### Configuration Management
```cpp
#include "CConfig.hpp"
using namespace lap::core;

auto& config = ConfigManager::getInstance();
config.initialize("config.json");

// Type-safe accessors with defaults
int port = config.getInt("server.port", 8080);
String host = config.getString("server.host", "localhost");

// Setters with immediate persistence
config.setString("server.status", "running");
config.save();  // Persist to disk
```

### AUTOSAR Result Pattern
```cpp
#include "CResult.hpp"
using namespace lap::core;

Result<int> divide(int a, int b) {
    if (b == 0) {
        return ErrorCode(ErrorCode::kInvalidArgument, GetCoreErrorDomain());
    }
    return a / b;
}

// Error handling
auto result = divide(10, 2);
if (result.HasValue()) {
    std::cout << "Result: " << result.Value() << std::endl;
} else {
    std::cerr << "Error: " << result.Error().Message() << std::endl;
}

// Monadic composition with LAP_TRY
LAP_TRY(auto x, divide(10, 2));
LAP_TRY(auto y, divide(x, 3));
return y;  // Automatic error propagation
```

### Asynchronous Operations
```cpp
#include "CFuture.hpp"
#include "CPromise.hpp"
using namespace lap::core;

// Producer-consumer with Future/Promise
Promise<int> promise;
Future<int> future = promise.GetFuture();

std::thread worker([&promise]() {
    // Simulate work
    std::this_thread::sleep_for(std::chrono::seconds(1));
    promise.SetValue(42);
});

// Non-blocking wait with timeout
auto status = future.WaitFor(std::chrono::seconds(2));
if (status == FutureStatus::kReady) {
    int value = future.Get();
    std::cout << "Result: " << value << std::endl;
}

worker.join();
```

### Synchronization
```cpp
#include "CSync.hpp"
using namespace lap::core;

Mutex mutex;
Event event;
Semaphore sem(1);

// RAII lock guard
{
    std::lock_guard<Mutex> lock(mutex);
    // Critical section protected
}

// Event signaling
std::thread waiter([&event]() {
    event.wait();  // Blocks until signaled
    std::cout << "Event received!" << std::endl;
});

event.signal();  // Wake up waiter
waiter.join();

// Semaphore with timeout
if (sem.try_acquire_for(std::chrono::milliseconds(100))) {
    // Resource acquired
    sem.release();
}
```

---

## 🧪 Testing

### Run All Unit Tests
```bash
cd _build  # or build/modules/Core
./core_test

# Output: [==========] 294 tests from 80 test suites ran.
#         [  PASSED  ] 294 tests.
```

### Run Filtered Tests
```bash
# Run only memory-related tests
./core_test --gtest_filter="*Memory*"

# Run config tests
./core_test --gtest_filter="ConfigTest.*"

# Exclude leak tests (slow)
./core_test --gtest_filter="-*LeakTest*"
```

### Run Test Scripts
```bash
cd test
./run_all_tests.sh      # Comprehensive test suite
./run_memory_tests.sh   # Memory-specific tests
```

### Run Examples
```bash
./config_example_v4              # Configuration demo
./abort_example                  # AUTOSAR abort handling
./memory_leak_multithread_test   # Multi-threaded stress test
```

### Run Benchmarks
```bash
./alignment_performance_test     # Alignment overhead
./pool_vs_system_benchmark      # Pool vs malloc comparison
./memory_stress_test            # Concurrent allocation stress
```

**Test Coverage**: 294/294 tests passing (100% success rate, 1 intentionally disabled)

---

## 🏗️ Architecture

### Module Structure
```
Core/
├── source/
│   ├── inc/              # Public API headers (installed)
│   │   ├── CMemory.hpp
│   │   ├── CConfig.hpp
│   │   ├── CResult.hpp
│   │   ├── CFuture.hpp
│   │   ├── CSync.hpp
│   │   └── ...
│   └── src/              # Implementation files
│       ├── CMemory.cpp
│       ├── CConfig.cpp
│       └── ...
├── test/
│   ├── unittest/         # Unit tests (GTest)
│   │   ├── test_memory.cpp
│   │   ├── config_test.cpp
│   │   └── ...
│   ├── examples/         # Usage examples
│   │   ├── memory_example.cpp
│   │   ├── config_example_v4.cpp
│   │   └── ...
│   ├── benchmark/        # Performance benchmarks
│   │   ├── pool_vs_system_benchmark.cpp
│   │   └── ...
│   ├── run_all_tests.sh
│   └── run_memory_tests.sh
├── doc/                  # Documentation
│   ├── QUICK_START.md
│   ├── INDEX.md
│   ├── HMAC_SECRET_CONFIG.md
│   ├── THIRD_PARTY.md
│   └── archive/          # Historical design docs
├── BuildTemplate/        # Git submodule (for standalone build)
├── CMakeLists.txt        # Build configuration
└── README.md             # This file
```

### Component Overview

| Component | Description | Key Headers |
|-----------|-------------|-------------|
| **Memory** | Unified memory management with pool allocation | `CMemory.hpp`, `CMemoryAllocator.hpp` |
| **Config** | JSON configuration with HMAC security | `CConfig.hpp` |
| **AUTOSAR Types** | Standard containers (String, Vector, Map, etc.) | `CString.hpp`, `CVector.hpp`, `COptional.hpp`, `CVariant.hpp`, `CSpan.hpp` |
| **AUTOSAR Functional** | Error handling and monadic composition | `CResult.hpp`, `CErrorCode.hpp`, `CErrorDomain.hpp`, `CException.hpp` |
| **Async** | Future/Promise for asynchronous operations | `CFuture.hpp`, `CPromise.hpp` |
| **Sync** | Synchronization primitives | `CSync.hpp`, `CLockFreeQueue.hpp` |
| **File/Path** | File system operations | `CFile.hpp`, `CPath.hpp` |
| **Time** | Timing and duration utilities | `CTime.hpp`, `CTimer.hpp` |
| **Abort** | AUTOSAR abort and signal handling | `CAbort.hpp` |
| **Core** | Initialization and lifecycle management | `CCore.hpp`, `CInitialization.hpp` |

---

## 🔧 Configuration

### Memory Pool Configuration

Create `config.json` in your working directory:

```json
{
  "memory": {
    "pool_count": 5,
    "pool_8_size": 64,      // 8-byte objects, 64 slots
    "pool_16_size": 64,     // 16-byte objects, 64 slots
    "pool_32_size": 32,     // 32-byte objects, 32 slots
    "pool_64_size": 16,     // 64-byte objects, 16 slots
    "pool_128_size": 8,     // 128-byte objects, 8 slots
    "align": 8              // Default alignment (bytes)
  }
}
```

### HMAC Security (Production)

For secure configuration verification in production:

```bash
# Generate a strong secret
export HMAC_SECRET=$(openssl rand -hex 32)

# Run your application
./your_app

# Or set in systemd service
Environment="HMAC_SECRET=your_secret_here"
```

See [doc/HMAC_SECRET_CONFIG.md](doc/HMAC_SECRET_CONFIG.md) for complete setup instructions.

### Environment Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `HMAC_SECRET` | HMAC-SHA256 key for config verification | Built-in (dev only) |
| `CONFIG_PATH` | Custom configuration file path | `./config.json` |

---

## 📊 Performance

### Memory Allocation Benchmarks

| Operation | Pool (small) | System malloc | Speedup |
|-----------|--------------|---------------|---------|
| Allocate 32B | ~15 ns | ~120 ns | **8x** |
| Allocate 128B | ~18 ns | ~140 ns | **7.7x** |
| Allocate+Free | ~25 ns | ~200 ns | **8x** |
| Threaded (4 cores) | ~35 ns | ~450 ns | **12.8x** |

*Measured on Intel Core i7, GCC 12.2, -O3*

### Throughput
- **Single-threaded**: ~40M allocations/sec (pool), ~5M alloc/sec (malloc)
- **Multi-threaded (4 cores)**: ~28M alloc/sec (pool), ~2.2M alloc/sec (malloc)

Run benchmarks for your platform:
```bash
cd _build
./pool_vs_system_benchmark
./alignment_performance_test
```

---

## 🔒 Security

### Configuration Integrity
- **HMAC-SHA256** verification for config files
- **Protected metadata** with `__metadata__` section
- **Environment-based secrets** - No hardcoded keys

### Memory Safety
- **Bounds checking** in debug mode
- **Use-after-free detection** via XOR masking
- **Memory leak detection** with full allocation tracking

### Thread Safety
- **Lock-free fast paths** for allocation hot paths
- **Atomic operations** for ref counting and state management
- **Deadlock-free** synchronization primitives

---

## 📚 Documentation

### Quick Links
- **[Quick Start Guide](doc/QUICK_START.md)** - Get up and running in 5 minutes
- **[API Documentation Index](doc/INDEX.md)** - Complete API reference catalog
- **[Test Guide](test/README.md)** - Building and running tests
- **[AUTOSAR Abort Implementation](doc/CAbort_Refactoring_Summary.md)** - Signal handling details
- **[HMAC Security Setup](doc/HMAC_SECRET_CONFIG.md)** - Production security configuration
- **[Third-party Dependencies](doc/THIRD_PARTY.md)** - License and version info
- **[Design Archive](doc/archive/)** - Historical implementation notes

### API Documentation

All public APIs are documented with Doxygen-style comments in headers:
- `source/inc/*.hpp` - Public API headers with inline documentation
- `doc/*.md` - Detailed guides and tutorials

---

## 🤝 Dependencies

### Build Requirements

| Dependency | Version | Purpose |
|------------|---------|---------|
| **CMake** | ≥ 3.10.2 | Build system |
| **C++ Compiler** | C++17 (GCC 7+, Clang 5+) | Language support |
| **Boost** | ≥ 1.65 | filesystem, regex, system |
| **OpenSSL** | ≥ 1.1.0 | HMAC-SHA256 for config security |
| **zlib** | ≥ 1.2.8 | Compression utilities |

### Test Dependencies (Optional)

| Dependency | Version | Purpose |
|------------|---------|---------|
| **Google Test** | ≥ 1.10.0 | Unit testing framework |

### Runtime Dependencies

**None** - Core is a self-contained library with no runtime dependencies beyond system libraries.

See [doc/THIRD_PARTY.md](doc/THIRD_PARTY.md) for complete license and attribution information.

---

## � Installation

### System-wide Installation

```bash
cd _build
sudo cmake --install . --prefix /usr/local

# Installed files:
# /usr/local/lib/liblap_core.so*
# /usr/local/include/lap/core/*.hpp
# /usr/local/lib/cmake/lap_core/
# /usr/local/bin/lap_config_editor
# /usr/local/share/doc/lap-core/
```

### Using Installed Library

```cmake
# CMakeLists.txt
find_package(lap_core REQUIRED)
target_link_libraries(your_app PRIVATE lap::core)
```

### Custom Installation Prefix

```bash
cmake --install . --prefix /opt/lightap
export CMAKE_PREFIX_PATH=/opt/lightap:$CMAKE_PREFIX_PATH
```

---

## 🐛 Troubleshooting

### Library Not Found at Runtime

If you see `error while loading shared libraries: liblap_core.so.1`:

```bash
# Option 1: Update library cache (system-wide install)
sudo ldconfig

# Option 2: Set LD_LIBRARY_PATH (custom prefix)
export LD_LIBRARY_PATH=/opt/lightap/lib:$LD_LIBRARY_PATH

# Option 3: Use RPATH (build time)
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local
```

### HMAC Warnings in Production

If you see `HMAC_SECRET environment variable not set!`:

```bash
# This is expected in development. For production:
export HMAC_SECRET=$(openssl rand -hex 32)
```

See [HMAC_SECRET_CONFIG.md](doc/HMAC_SECRET_CONFIG.md) for production deployment.

### Build Errors

```bash
# Clean build
rm -rf _build
mkdir _build && cd _build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)

# Verbose build for debugging
cmake --build . -j$(nproc) -- VERBOSE=1
```

---

## 📝 Contributing

### Development Workflow

1. **Clone with submodules**:
   ```bash
   git clone --recursive git@github.com:YourOrg/Core.git
   ```

2. **Create feature branch**:
   ```bash
   git checkout -b feature/your-feature
   ```

3. **Build and test**:
   ```bash
   mkdir _build && cd _build
   cmake .. -DCMAKE_BUILD_TYPE=Debug
   cmake --build . -j$(nproc)
   ./core_test
   ```

4. **Run checks**:
   ```bash
   # All tests must pass
   ./core_test
   
   # No memory leaks
   ./core_test --gtest_filter="*LeakTest*"
   
   # Performance benchmarks
   ./pool_vs_system_benchmark
   ```

### Code Style Guidelines
- **C++17** - Use modern C++ features (auto, constexpr, structured bindings)
- **AUTOSAR naming** - Follow AUTOSAR Adaptive Platform naming conventions
- **RAII** - Use RAII for resource management (no manual new/delete in client code)
- **Thread-safe** - All public APIs must be thread-safe unless explicitly documented
- **Documentation** - Add Doxygen comments for all public APIs

### Testing Requirements
- **Unit tests** - Add GTest tests for all new features (`test/unittest/`)
- **Examples** - Provide usage examples for new APIs (`test/examples/`)
- **Benchmarks** - Add performance tests if applicable (`test/benchmark/`)
- **100% pass rate** - All existing tests must continue to pass

---

## 📄 License

Copyright © 2025 LightAP Project Contributors. All rights reserved.

This software is proprietary and confidential. Unauthorized copying, distribution, or modification is strictly prohibited.

---

## 🔗 Related Modules

| Module | Description |
|--------|-------------|

---

## 📧 Support

### Resources
- **[Documentation Index](doc/INDEX.md)** - Complete documentation catalog
- **[Quick Start Guide](doc/QUICK_START.md)** - Getting started in 5 minutes
- **[FAQ](doc/FAQ.md)** - Frequently asked questions
- **[Design Archive](doc/archive/)** - Implementation history and design decisions

### Contact
For bug reports, feature requests, or technical support:
- **Issue Tracker**: [Internal Tracker Link]
- **Email**: ddkv587@gmail.com

---

**Version**: 1.0.0  
**AUTOSAR Compliance**: Adaptive Platform R23-11  
**Build System**: CMake 3.10+, BuildTemplate v1.1.0  
**Last Updated**: November 3, 2025

---

<p align="center">
  <strong>Built with ❤️ for AUTOSAR Adaptive Platform</strong>
</p>
