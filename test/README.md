# Core Module Test Organization

This directory contains all tests and examples for the LightAP Core module.

## Directory Structure

```
test/
├── unittest/          # Unit tests (run via core_test binary with GTest)
├── examples/          # Simple demonstration programs
├── benchmark/         # Performance tests and stress tests
└── check_alignment.cpp  # Alignment verification utility
```

## Unit Tests (`unittest/`)

Comprehensive unit tests using Google Test framework. Run all tests:
```bash
./build/modules/Core/core_test
```

Key test suites:
- **Memory System**: `test_memory.cpp`, `test_memory_allocator.cpp`, `test_memory_serialization.cpp`
- **AUTOSAR Containers**: `test_autosar_containers.cpp` (Array, Vector, Map, String, etc.)
- **AUTOSAR Utilities**: `test_autosar_utilities.cpp` (Result, Optional, Variant, Span)
- **Synchronization**: `test_sync.cpp` (Mutex, Event, Semaphore, LockFreeQueue)
- **Functional**: `test_future_basic.cpp`, `test_promise_errors.cpp`, `test_result_*.cpp`
- **Configuration**: `config_test.cpp`
- **Error Handling**: `test_exception.cpp`, `test_error_domain.cpp`
- **Abort & Signal**: `test_abort_v2.cpp` (AUTOSAR AP compliant)
- **File System**: `test_file.cpp`, `test_path.cpp`
- **Time**: `test_time_timer.cpp`
- **Noexcept**: `test_noexcept_extended.cpp`

Current status: **298/298 enabled tests passing** (99.7% overall with 1 disabled)

## Examples (`examples/`)

Simple demonstration programs showing how to use Core APIs.

### Memory Management
- `memory_example.cpp` - Basic memory pool usage
- `global_operator_test.cpp` - Global new/delete operator verification
- `direct_alignment_test.cpp` - Memory alignment configuration
- `test_memory_allocator.cpp` - STL allocator demonstration
- `test_memory_allocator_debug.cpp` - Traced allocation debugging

### Configuration
- `config_example_v4.cpp` - ConfigManager with metadata & JSON API
- `config_verification_test.cpp` - Verification error handling
- `config_update_policy_test.cpp` - Update policy behavior
- `test_default_policy_load.cpp` - Default policy loading
- `test_auto_update_policy.cpp` - Auto-update policy feature

### Core Classes
- `test_dynamic_magic.cpp` - Runtime XOR mask generation
- `test_core_classes.cpp` - Comprehensive API demonstration
- `test_functional_classes.cpp` - Result, Future, Promise, Exception

### Memory Leak Detection
- `memory_leak_multithread_test.cpp` - Multi-threaded leak detection
- `memory_leak_intentional_test.cpp` - Intentional leak verification

### AUTOSAR
- `abort_example.cpp` - AUTOSAR AP Abort & signal handling

## Benchmarks (`benchmark/`)

Performance tests and stress tests for measuring system behavior.

### Memory Performance
- `alignment_performance_test.cpp` - Alignment overhead measurement
- `pool_vs_system_benchmark.cpp` - Memory pool vs system malloc comparison
- `system_malloc_comparison_test.cpp` - Detailed malloc performance analysis

### Stress Tests
- `memory_stress_test.cpp` - High-load memory allocation patterns

## Building

### Build everything
```bash
cd build
cmake ..
make -j$(nproc)
```

### Build only unit tests
```bash
make core_test
```

### Build only examples
```bash
cmake -DENABLE_BUILD_EXAMPLES=ON ..
make
```

### Build only benchmarks
```bash
cmake -DENABLE_BUILD_BENCHMARKS=ON ..
make
```

### Disable examples and benchmarks
```bash
cmake -DENABLE_BUILD_EXAMPLES=OFF -DENABLE_BUILD_BENCHMARKS=OFF ..
make core_test
```

## Running Tests

### Run all unit tests
```bash
./build/modules/Core/core_test
```

### Run specific test suite
```bash
./build/modules/Core/core_test --gtest_filter="CMemoryAllocatorTest.*"
```

### Run example program
```bash
./build/modules/Core/config_example_v4
```

### Run benchmark
```bash
./build/modules/Core/alignment_performance_test
```

## File Consolidation History

The following redundant files were removed during reorganization (2025-11-03):

### Deleted (Superseded)
- `examples/config_example.cpp` → Use `config_example_v4.cpp`
- `examples/config_example_old.cpp` → Use `config_example_v4.cpp`
- `examples/config_example_v3.cpp` → Use `config_example_v4.cpp`
- `unittest/test_abort.cpp` → Use `test_abort_v2.cpp` (27 comprehensive tests)
- `unittest/test_noexcept.cpp` → Merged into `test_noexcept_extended.cpp`

### Deleted (Not Built/Outdated)
- `examples/struct_size_test.cpp` → Not built, diagnostic only
- `examples/test_runtime_save.cpp` → Not built, superseded by config_test.cpp

### Moved to benchmark/
- `examples/alignment_performance_test.cpp` → `benchmark/alignment_performance_test.cpp`
- `examples/pool_vs_system_benchmark.cpp` → `benchmark/pool_vs_system_benchmark.cpp`
- `examples/memory_stress_test.cpp` → `benchmark/memory_stress_test.cpp`
- `examples/system_malloc_comparison_test.cpp` → `benchmark/system_malloc_comparison_test.cpp`

## Notes

- All unit tests use Google Test framework
- Examples are standalone programs demonstrating API usage
- Benchmarks may take longer to run and output performance metrics
- ConfigManager requires `HMAC_SECRET` environment variable for production use
- Memory leak detection is built into the core library
