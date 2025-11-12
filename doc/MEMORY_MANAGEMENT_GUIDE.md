# LightAP Core Memory Management Guide

**Version:** 2.0  
**Date:** 2025-11-12  
**Author:** LightAP Team

---

## 📋 Table of Contents

- [Overview](#overview)
- [Architecture](#architecture)
- [Quick Start](#quick-start)
- [API Reference](#api-reference)
- [Usage Patterns](#usage-patterns)
- [Best Practices](#best-practices)
- [Performance Tuning](#performance-tuning)
- [Troubleshooting](#troubleshooting)
- [Examples](#examples)

---

## 🎯 Overview

The LightAP Core memory management system provides:

- **Pool-based allocation** for small objects (≤ 1024 bytes)
- **System fallback** for large allocations (> 1024 bytes)
- **STL-compatible allocator** for seamless container integration
- **Optional memory tracking** for leak detection and profiling
- **Thread-safe operations** with minimal lock contention
- **Global operator new/delete** override for transparent operation

### Key Benefits

✅ **Performance** - 10-100x faster than system malloc for small objects  
✅ **Fragmentation** - Minimal memory fragmentation  
✅ **Scalability** - Lock-free fast paths for multi-threaded applications  
✅ **Tracking** - Optional comprehensive memory profiling  
✅ **Integration** - Works seamlessly with existing C++ code

---

## 🏗️ Architecture

```
┌─────────────────────────────────────────────────────┐
│           Application Code                          │
│   (operator new/delete, Memory::malloc, STL)       │
└────────────────┬────────────────────────────────────┘
                 │
┌────────────────┴────────────────────────────────────┐
│              Memory Facade                          │
│  ┌──────────────┐  ┌──────────────────────┐       │
│  │ Memory Class │  │ StlMemoryAllocator  │       │
│  └──────┬───────┘  └──────────┬───────────┘       │
└─────────┼────────────────────┼─────────────────────┘
          │                     │
┌─────────┴─────────────────────┴─────────────────────┐
│           MemoryManager (Singleton)                 │
│  ┌──────────────┐  ┌────────────────────────────┐ │
│  │PoolAllocator │  │ MemoryTracker (Optional)   │ │
│  │              │  │ - Leak detection           │ │
│  │ - Size pools │  │ - Statistics               │ │
│  │ - Expansion  │  │ - Per-thread tracking      │ │
│  └──────────────┘  └────────────────────────────┘ │
└─────────────────────────────────────────────────────┘
          │                     │
┌─────────┴─────────────────────┴─────────────────────┐
│     System Allocator (malloc/free)                  │
└─────────────────────────────────────────────────────┘
```

### Components

#### 1. **MemoryManager**
- Singleton managing all memory operations
- Routes allocations to appropriate subsystem
- Provides statistics and monitoring

#### 2. **PoolAllocator**
- Manages multiple size-classed memory pools
- O(1) allocation/deallocation
- Automatic pool expansion

#### 3. **MemoryTracker** (Optional)
- Tracks all allocations/deallocations
- Detects memory leaks
- Provides per-thread statistics
- **Note:** Has performance overhead, enable only for debugging

#### 4. **Memory Facade**
- Static API for manual memory management
- Compatible with C-style code

#### 5. **StlMemoryAllocator**
- C++17 standard allocator interface
- Stateless, always-equal allocator
- Compatible with all STL containers

---

## 🚀 Quick Start

### 1. Basic Memory Allocation

```cpp
#include "CMemory.hpp"

// Allocate
void* ptr = lap::core::Memory::malloc(256);

// Use memory
// ...

// Free
lap::core::Memory::free(ptr);
```

### 2. Using with STL Containers

```cpp
#include "CMemory.hpp"

// Vector with pool allocator
lap::core::Vector<int, lap::core::StlMemoryAllocator<int>> vec;
vec.push_back(42);

// Map with pool allocator
using MyMap = lap::core::Map<
    lap::core::String, 
    int,
    std::less<lap::core::String>,
    lap::core::StlMemoryAllocator<lap::core::Pair<const lap::core::String, int>>
>;
MyMap settings;
settings["timeout"] = 5000;
```

### 3. Custom Class with Pool Allocation

```cpp
#include "CMemory.hpp"

class MyClass {
public:
    IMP_OPERATOR_NEW(MyClass)  // Enable pool allocation
    
    MyClass(int id) : id_(id) {}
    ~MyClass() {}
    
private:
    int id_;
};

// Usage
MyClass* obj = new MyClass(42);  // Uses pool allocator
delete obj;                       // Returns to pool
```

---

## 📚 API Reference

### Memory Facade

```cpp
namespace lap::core {
    class Memory {
    public:
        // Allocate memory
        static void* malloc(Size size, 
                           const Char* className = nullptr, 
                           UInt32 classId = 0) noexcept;
        
        // Free memory (nullptr-safe)
        static void free(void* ptr) noexcept;
        
        // Validate pointer (debug builds only)
        static Int32 checkPtr(void* ptr, 
                             const Char* hint = nullptr) noexcept;
        
        // Register class name for tracking
        static UInt32 registerClassName(const Char* className) noexcept;
        
        // Get memory statistics
        static MemoryStats getMemoryStats() noexcept;
    };
}
```

### StlMemoryAllocator

```cpp
namespace lap::core {
    template <typename T>
    class StlMemoryAllocator {
    public:
        using value_type = T;
        
        // Allocate n objects of type T
        T* allocate(size_type n);
        
        // Deallocate memory
        void deallocate(T* p, size_type n) noexcept;
        
        // Maximum allocatable size
        size_type max_size() const noexcept;
    };
    
    // Helper function
    template <typename T>
    Vector<T, StlMemoryAllocator<T>> MakeVectorWithMemoryAllocator();
}
```

### MemoryManager

```cpp
namespace lap::core {
    class MemoryManager {
    public:
        static MemoryManager* getInstance();
        
        void initialize();           // Load config, start pools
        void uninitialize();         // Save config, cleanup
        
        void* malloc(Size size, const Char* className = nullptr, 
                    UInt32 classId = 0);
        void free(void* ptr);
        
        MemoryStats getMemoryStats();
        Bool hasMemoryTracker() noexcept;
        
        void registerThreadName(UInt32 threadId, const String& name);
    };
}
```

### MemoryStats Structure

```cpp
struct MemoryStats {
    Size currentAllocSize;      // Current allocated bytes (user data)
    UInt32 currentAllocCount;   // Current allocation count
    Size totalPoolMemory;       // Total pool memory (including overhead)
    UInt32 poolCount;           // Number of active pools
    UInt32 threadCount;         // Tracked threads (if MemoryTracker enabled)
};
```

---

## 💡 Usage Patterns

### Pattern 1: RAII with unique_ptr

```cpp
auto deleter = [](void* p) { lap::core::Memory::free(p); };
auto ptr = std::unique_ptr<byte[], decltype(deleter)>(
    static_cast<byte*>(lap::core::Memory::malloc(1024)),
    deleter
);
// Automatic cleanup on scope exit
```

### Pattern 2: Custom Allocator for Containers

```cpp
// Type alias for convenience
template <typename T>
using PoolVector = lap::core::Vector<T, lap::core::StlMemoryAllocator<T>>;

PoolVector<int> numbers;
numbers.push_back(42);
```

### Pattern 3: Memory Tracking

```cpp
// Register class for tracking
auto classId = lap::core::Memory::registerClassName("MyClass");

// Allocate with tracking
void* ptr = lap::core::Memory::malloc(128, "MyClass", classId);

// Check allocation validity
if (lap::core::Memory::checkPtr(ptr, "validation") == 0) {
    // Pointer is valid
}

lap::core::Memory::free(ptr);
```

### Pattern 4: Global Override

```cpp
// Automatically uses pool allocator
int* arr = new int[100];  // Pool allocation (if size ≤ 1024 bytes)
// ...
delete[] arr;             // Returns to pool
```

---

## ✅ Best Practices

### 1. **Prefer STL Containers with StlMemoryAllocator**

**Good:**
```cpp
lap::core::Vector<int, lap::core::StlMemoryAllocator<int>> vec;
```

**Avoid:**
```cpp
std::vector<int*> vec;
for (int i = 0; i < 100; ++i) {
    vec.push_back(new int(i));  // Many small allocations
}
```

### 2. **Use IMP_OPERATOR_NEW for Frequently Allocated Classes**

```cpp
class FrequentlyCreated {
public:
    IMP_OPERATOR_NEW(FrequentlyCreated)
    // ...
};
```

### 3. **Always Free What You Allocate**

```cpp
void* ptr = lap::core::Memory::malloc(256);
// MUST call free, even on error paths
lap::core::Memory::free(ptr);
```

### 4. **nullptr Checks Are Unnecessary for free()**

```cpp
void* ptr = nullptr;
lap::core::Memory::free(ptr);  // Safe, no-op
```

### 5. **Monitor Memory Usage in Production**

```cpp
auto stats = lap::core::Memory::getMemoryStats();
if (stats.currentAllocSize > THRESHOLD) {
    // Log warning, trigger cleanup, etc.
}
```

### 6. **Enable MemoryTracker Only for Debugging**

```json
// memory_config.json
{
  "enable_checker": true,    // Only in debug builds
  "alignment": 8,
  "max_pools": 64
}
```

### 7. **Register Thread Names for Better Diagnostics**

```cpp
lap::core::MemoryManager::getInstance()->registerThreadName(
    std::this_thread::get_id(),
    "WorkerThread-1"
);
```

---

## ⚡ Performance Tuning

### Pool Configuration

Edit `memory_config.json`:

```json
{
  "enable_checker": false,
  "alignment": 8,
  "max_pools": 64,
  "pools": [
    {"unit_size": 16,  "init_count": 100, "max_count": 1000, "append_count": 50},
    {"unit_size": 32,  "init_count": 100, "max_count": 1000, "append_count": 50},
    {"unit_size": 64,  "init_count": 50,  "max_count": 500,  "append_count": 25},
    {"unit_size": 128, "init_count": 50,  "max_count": 500,  "append_count": 25},
    {"unit_size": 256, "init_count": 20,  "max_count": 200,  "append_count": 10},
    {"unit_size": 512, "init_count": 10,  "max_count": 100,  "append_count": 5},
    {"unit_size": 1024,"init_count": 10,  "max_count": 100,  "append_count": 5}
  ]
}
```

### Parameters

- **unit_size**: Size class for the pool (bytes)
- **init_count**: Initial units allocated
- **max_count**: Maximum units (0 = unlimited)
- **append_count**: Units to add when expanding
- **alignment**: Memory alignment (must be power of 2: 1, 2, 4, 8, 16...)

### Tuning Guidelines

| Scenario | Configuration |
|----------|---------------|
| **Many small objects** | More pools, larger init_count for small sizes |
| **Few large objects** | Fewer pools, smaller init_count for large sizes |
| **Memory constrained** | Lower max_count, smaller append_count |
| **Performance critical** | Disable checker, higher alignment |

---

## 🔧 Troubleshooting

### Memory Leaks

**Enable Memory Tracking:**

```cpp
// Set environment variable
export MEMORY_LEAK_CHECK=1

// Or configure programmatically
auto& config = lap::core::ConfigManager::getInstance();
nlohmann::json memConfig;
memConfig["enable_checker"] = true;
config.setModuleConfig("memory", memConfig);
```

**Check for Leaks:**

```cpp
lap::core::MemoryManager::getInstance()->outputState();
// Creates memory_leak.log with detailed leak report
```

### Performance Issues

**Symptom:** Allocations are slow

**Solutions:**
1. Disable MemoryTracker in production
2. Increase pool `init_count` to reduce expansions
3. Verify pool sizes match allocation patterns
4. Check for lock contention (use profiler)

### Out of Memory

**Symptom:** malloc() returns nullptr

**Solutions:**
1. Check `max_count` limits in config
2. Verify no memory leaks
3. Monitor with `getMemoryStats()`
4. Consider increasing pool sizes

### Thread Safety Issues

**Symptom:** Crashes in multi-threaded code

**Solutions:**
1. MemoryManager is thread-safe by design
2. Verify no double-free bugs
3. Check pointer validity with `checkPtr()`
4. Enable MemoryTracker for debugging

---

## 📘 Examples

See comprehensive examples in:

- **memory_example_comprehensive.cpp** - Full feature demonstration
- **test_memory.cpp** - Unit tests showing usage patterns
- **test_memory_allocator.cpp** - STL allocator examples

### Running Examples

```bash
cd build
./memory_example_comprehensive
```

---

## 📊 Performance Characteristics

### Allocation Performance

| Operation | Pool Allocator | System malloc |
|-----------|----------------|---------------|
| Allocate small (≤64B) | ~50 ns | ~500 ns |
| Allocate medium (≤512B) | ~80 ns | ~600 ns |
| Allocate large (>1024B) | System malloc | System malloc |
| Deallocate | ~30 ns | ~400 ns |

*Benchmarks on Intel i7, Linux, GCC 11*

### Memory Overhead

| Component | Overhead |
|-----------|----------|
| Pool header (per pool) | ~32 bytes |
| Block header (per block) | ~24 bytes |
| Unit header (per allocation) | ~24 bytes |
| Tracker (per allocation, optional) | ~56 bytes |

---

## 🔗 See Also

- [Core Module README](../README.md)
- [AUTOSAR AP SWS_CORE Specification](../doc/AUTOSAR_AP_SWS_Core.pdf)
- [BuildTemplate Documentation](../../BuildTemplate/README.md)

---

**Questions or Issues?**  
Contact: ddkv587@gmail.com  
Project: https://github.com/TreeNeeBee/LightAP
