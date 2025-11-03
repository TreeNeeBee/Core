# Core Module - Standalone Build Guide

This guide explains how to build and use the Core module independently of the LightAP project.

## 📋 Overview

The Core module can be built in two modes:
1. **Standalone mode** - Independent module with BuildTemplate as a submodule
2. **Integrated mode** - Part of the LightAP project (automatic detection)

## 🔧 Prerequisites

### System Requirements
- CMake >= 3.10.2
- C++17 compiler (GCC 7+, Clang 5+) or C++14 fallback
- Git (for submodule management)

### Dependencies
```bash
# Ubuntu/Debian
sudo apt-get install -y \
    cmake \
    g++ \
    libboost-filesystem-dev \
    libboost-regex-dev \
    libboost-system-dev \
    libssl-dev \
    zlib1g-dev \
    libgtest-dev

# macOS (Homebrew)
brew install cmake boost openssl zlib googletest
```

## 🚀 Quick Start

### 1. Clone with Submodules

```bash
# Clone the repository with submodules
git clone --recursive https://github.com/yourorg/Core.git
cd Core

# Or clone then initialize submodules
git clone https://github.com/yourorg/Core.git
cd Core
git submodule update --init --recursive
```

### 2. Build Using Script

The easiest way to build is using the provided build script:

```bash
# Default: Release build with tests, examples, and benchmarks
./build.sh

# Debug build
./build.sh -t Debug

# Clean build
./build.sh -c

# Build without tests
./build.sh --no-tests --no-examples --no-benchmarks

# Build and install
./build.sh -i
```

### 3. Manual Build

```bash
# Create build directory
mkdir build && cd build

# Configure
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
make -j$(nproc)

# Run tests
ctest --output-on-failure

# Install (optional)
sudo make install
```

## 📦 Build Options

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `CMAKE_BUILD_TYPE` | Release | Build type: Debug or Release |
| `ENABLE_BUILD_UNITTEST` | ON | Build unit tests |
| `ENABLE_BUILD_EXAMPLES` | ON | Build example programs |
| `ENABLE_BUILD_BENCHMARKS` | ON | Build benchmark programs |
| `CMAKE_INSTALL_PREFIX` | /usr/local | Installation directory |

### Examples

```bash
# Debug build without examples
cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_BUILD_EXAMPLES=OFF

# Custom install location
cmake .. -DCMAKE_INSTALL_PREFIX=$HOME/.local

# Minimal build (library only)
cmake .. \
    -DENABLE_BUILD_UNITTEST=OFF \
    -DENABLE_BUILD_EXAMPLES=OFF \
    -DENABLE_BUILD_BENCHMARKS=OFF
```

## 🧪 Testing

### Run All Tests
```bash
cd build
ctest --output-on-failure
```

### Run Specific Test
```bash
cd build
./core_test --gtest_filter=MemoryTest.*
```

### Run Examples
```bash
cd build

# Memory management example
./core_memory_example

# Configuration example
./config_example_v4

# See all examples
ls -1 *example* *test_*
```

### Run Benchmarks
```bash
cd build

# Memory pool benchmark
./pool_vs_system_benchmark

# Alignment performance
./alignment_performance_test

# See all benchmarks
ls -1 *benchmark* memory_stress_test
```

## 📥 Installation

### System-wide Installation

```bash
cd build
sudo make install
```

This installs:
- **Library**: `/usr/local/lib/liblap_core.so`
- **Headers**: `/usr/local/include/lap/core/`
- **Tools**: `/usr/local/bin/lap_config_editor`
- **Docs**: `/usr/local/share/doc/lap-core/`

### User Installation

```bash
cmake .. -DCMAKE_INSTALL_PREFIX=$HOME/.local
make install
```

### Using Installed Library

```cmake
# CMakeLists.txt
find_library(LAP_CORE lap_core)
find_path(LAP_CORE_INCLUDE lap/core)

target_link_libraries(your_app PRIVATE ${LAP_CORE})
target_include_directories(your_app PRIVATE ${LAP_CORE_INCLUDE})
```

## 🔄 Submodule Management

### Update BuildTemplate

```bash
# Update to latest BuildTemplate
cd BuildTemplate
git checkout main
git pull
cd ..
git add BuildTemplate
git commit -m "Update BuildTemplate submodule"
```

### Check Submodule Status

```bash
git submodule status
```

### Re-initialize Submodules

```bash
git submodule update --init --recursive --force
```

## 🐛 Troubleshooting

### BuildTemplate Not Found

```
Error: BuildTemplate not found!
```

**Solution:**
```bash
git submodule update --init --recursive
```

### Missing Dependencies

```
Error: Could not find Boost/GTest/OpenSSL
```

**Solution:** Install dependencies (see Prerequisites section)

### C++17 Not Supported

The build system automatically falls back to C++14. Check compiler version:
```bash
g++ --version  # GCC 7+ recommended
clang++ --version  # Clang 5+ recommended
```

### Tests Failing

```bash
# Run tests with verbose output
cd build
ctest --verbose --rerun-failed --output-on-failure
```

## 📝 Development Workflow

### 1. Make Changes

```bash
# Edit source files
vim source/src/CMemory.cpp

# Build
cd build && make -j$(nproc)

# Test
ctest
```

### 2. Add New Test

```bash
# Create test file
vim test/unittest/MyNewTest.cpp

# Rebuild (CMake auto-detects new files)
cd build && make -j$(nproc)

# Run new test
./core_test --gtest_filter=MyNewTest.*
```

### 3. Add New Example

Add to `CMakeLists.txt`:
```cmake
add_core_example(my_new_example my_new_example.cpp)
```

### 4. Clean Build

```bash
# Remove build directory
rm -rf build

# Or use build script
./build.sh -c
```

## 📚 Documentation

- **[README.md](README.md)** - Module overview and features
- **[doc/QUICK_START.md](doc/QUICK_START.md)** - Quick start guide
- **[doc/HMAC_SECRET_CONFIG.md](doc/HMAC_SECRET_CONFIG.md)** - Security configuration
- **[tools/README.md](tools/README.md)** - Configuration editor tool
- **[test/README.md](test/README.md)** - Test organization

## 🤝 Contributing

When contributing to standalone Core:

1. Ensure all tests pass: `ctest`
2. Follow C++17/C++14 compatibility guidelines
3. Update documentation as needed
4. Test both standalone and integrated builds

## 📄 License

Part of the LightAP project. See [LICENSE](LICENSE) for details.

## 🔗 Related

- **[BuildTemplate](BuildTemplate/README.md)** - Build system documentation
- **[LightAP](https://github.com/yourorg/LightAP)** - Parent project
- **[CHANGES.md](CHANGES.md)** - Version history

---

For questions or issues, please open an issue on GitHub.
