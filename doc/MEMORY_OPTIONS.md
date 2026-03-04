# Core Memory Management Build Options

## Overview
Core module supports three memory management configurations:

1. **System Allocator** (Default)
2. **Custom Memory Pool** 
3. **jemalloc**

## Build Options

### 1. System Allocator (Default)
Uses standard system malloc/free.

```bash
cmake .. -DENABLE_MEMORY_POOL=OFF -DENABLE_JEMALLOC=OFF
# or simply
cmake ..
```

### 2. Custom Memory Pool
Enables built-in pool-based allocator for better performance.

```bash
cmake .. -DENABLE_MEMORY_POOL=ON
```

Defines: `LAP_ENABLE_MEMORY_POOL`

### 3. jemalloc
Uses jemalloc for improved memory allocation performance.

```bash
cmake .. -DENABLE_JEMALLOC=ON
```

Defines: `LAP_USE_JEMALLOC`

**Requirements**: libjemalloc-dev must be installed
```bash
# Debian/Ubuntu
sudo apt-get install libjemalloc-dev

# macOS
brew install jemalloc
```

## Directory Structure

```
source/
├── inc/
│   ├── memory/
│   │   ├── CMemory.hpp          # Memory facade API
│   │   └── CMemoryManager.hpp   # Memory pool implementation
│   └── ...
└── src/
    ├── memory/
    │   ├── CMemory.cpp
    │   └── CMemoryManager.cpp
    └── ...
```

## Usage in Code

Memory pool and jemalloc are transparent - no code changes needed.

Headers should include:
```cpp
#include "memory/CMemory.hpp"
```

## Testing

Test all configurations:
```bash
# Test default (system allocator)
cmake .. && make -j && ./simple_init_test

# Test memory pool
cmake .. -DENABLE_MEMORY_POOL=ON && make -j && ./simple_init_test

# Test jemalloc
cmake .. -DENABLE_JEMALLOC=ON && make -j && ./simple_init_test
```

All configurations pass 395/397 tests (99.5% success rate).
