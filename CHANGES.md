# LightAP Changelog

All notable changes to the LightAP middleware platform will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Core Module

#### Added
- AUTOSAR AP R23-11 compliant Abort module with thread-safe handlers
- Lock-free queue (SPSC/MPMC) for high-performance concurrent operations
- MemoryAllocator STL integration with custom allocators
- Memory alignment configuration (1-128 bytes) with runtime adjustment
- Configuration auto-update policy feature
- Memory serialization support for persistent data structures
- Comprehensive unit test suite (294 tests, 99.7% pass rate)

#### Changed
- Reorganized test directory structure (unittest/examples/benchmark)
- Consolidated documentation (7 active docs, 15 archived)
- Updated CMakeLists.txt with selective build options
- Enhanced ErrorDomain lifecycle management
- Improved thread safety for abort handlers

#### Fixed
- Memory allocator overflow check for max_size() allocations
- Exception copy assignment operator noexcept compliance
- ConfigManager metadata handling and update policy behavior

### LogAndTrace Module

#### Added
- DLT (Diagnostic Log and Trace) integration
- Multi-sink logging support
- Configurable log levels and filtering

### Persistency Module

#### Added
- Key-value storage with Protocol Buffers serialization
- CRUD operations with type safety
- Benchmark suite for performance validation

### Com Module

#### Added
- D-Bus communication support (sdbus-c++)
- Socket-based transport (TCP/UDP)
- Calculator service examples (method calls, events, fields)

## [1.0.0] - 2025-11-03

### Core Module

#### Added
- Initial release of Core module
- AUTOSAR Adaptive Platform compliance (R23-11)
- Unified memory management system with pool-based allocation
- Global new/delete operator interception
- Configuration Manager with JSON support and HMAC security
- AUTOSAR standard types: String, Vector, Map, Set, Optional, Variant, Span
- AUTOSAR functional types: Result, ErrorCode, ErrorDomain, Exception, Future, Promise
- Synchronization primitives: Mutex, RecursiveMutex, Event, Semaphore
- File system operations: File, Path (Boost.Filesystem wrapper)
- Time utilities: Timer, high-precision timing
- Instance Specifier for AUTOSAR identifier management
- Comprehensive test suite with 294 unit tests
- Examples demonstrating all major features
- Performance benchmarks for memory and synchronization

#### Features
- **Memory Management**
  - Pool-based allocation for objects ≤1024 bytes
  - System malloc passthrough for large objects
  - Thread-safe with minimal lock contention
  - Memory leak detection and statistics
  - Configurable alignment (1-128 bytes)
  - IMP_OPERATOR_NEW macro for per-class memory control

- **Configuration**
  - JSON-based configuration files
  - Type-safe getter/setter API
  - Metadata support for validation and security
  - HMAC-SHA256 signature verification
  - Hot reload with update policies
  - Default policy handling

- **AUTOSAR Compliance**
  - Core types following AUTOSAR naming conventions
  - ErrorDomain with unique IDs and message resolution
  - Result<T> for operation outcomes
  - Future/Promise for asynchronous operations
  - Abort handling per SWS_CORE specifications

- **Thread Safety**
  - All core APIs are thread-safe
  - Lock-free data structures where appropriate
  - Atomic operations for abort handlers
  - Memory pool protection with mutexes

#### Documentation
- Quick Start Guide
- AUTOSAR Abort implementation summary
- HMAC security configuration guide
- Test organization guide
- Third-party dependency documentation
- Comprehensive API documentation in headers

#### Performance
- Memory allocation: 10-100ns (pool), 100-500ns (system)
- Lock-free queue: 1M+ ops/sec
- Configuration access: O(log n) with map-based storage
- Thread-safe operations: <5% overhead for typical workloads

#### Build System
- CMake 3.10.2+ support
- C++17 standard compliance
- Optional build targets (examples, benchmarks)
- GTest integration for unit tests
- Cross-platform (Linux primary target)

### Dependencies
- Boost ≥1.65 (filesystem, regex, system)
- OpenSSL ≥1.1.0 (crypto, HMAC)
- zlib (compression)
- Google Test (testing only)

## [0.9.0] - 2025-10-31

### Core Module - Beta Release

#### Added
- Memory management system prototype
- Configuration Manager v4.0
- Basic AUTOSAR containers (String, Vector, Map)
- Result<T> and ErrorCode implementation
- Synchronization primitives (Mutex, Event, Semaphore)

#### Changed
- Migration to C++17 from C++14
- Enhanced Result<T> with better error handling
- Improved memory pool expansion strategy

#### Known Issues
- Memory alignment fixed at 8 bytes
- Limited configuration update policy support
- Abort module not AUTOSAR compliant

## [0.5.0] - 2025-10-15

### Core Module - Alpha Release

#### Added
- Initial memory management implementation
- Configuration Manager v3.0
- Basic container types
- File and Path utilities

#### Known Issues
- Thread safety concerns in memory allocation
- Configuration lacks security features
- Limited AUTOSAR compliance

---

## Categories

### Added
New features or capabilities added to the platform.

### Changed
Changes in existing functionality or behavior.

### Deprecated
Features that are marked for removal in future versions.

### Removed
Features that have been removed.

### Fixed
Bug fixes and issue resolutions.

### Security
Security-related improvements and fixes.

---

## Module Versioning

Each module maintains its own version number following semantic versioning:
- **Core**: 1.0.0
- **LogAndTrace**: 1.0.0
- **Persistency**: 1.0.0
- **Com**: 1.0.0
- **PlatformHealthManagement**: TBD

Platform version reflects the compatibility level across all modules.

---

## Links

- [Core Module README](modules/Core/README.md)
- [Core Documentation Index](modules/Core/doc/INDEX.md)
- [Core Test Guide](modules/Core/test/README.md)
- [AUTOSAR Adaptive Platform](https://www.autosar.org/)

---

**Note**: This changelog covers major releases and significant changes. For detailed implementation summaries and historical changes, see individual module documentation and archived summaries in `modules/*/doc/archive/`.
