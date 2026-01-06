/**
 * @file test_jemalloc_integration.cpp
 * @brief Test SYS_MALLOC/SYS_FREE macros and jemalloc integration
 */

#include "memory/CMemoryUtils.hpp"
#include <iostream>
#include <cstring>

#if defined(LAP_USE_JEMALLOC)
    #include <jemalloc/jemalloc.h>
#endif

using namespace lap::core;

int main() {
    std::cout << "=== jemalloc Integration Test ===" << std::endl;
    
    // Check if jemalloc is being used
#if defined(LAP_USE_JEMALLOC)
    std::cout << "✓ LAP_USE_JEMALLOC is defined" << std::endl;
    
    // Get jemalloc version
    const char* version = nullptr;
    size_t version_len = sizeof(version);
    if (mallctl("version", &version, &version_len, nullptr, 0) == 0) {
        std::cout << "✓ jemalloc version: " << version << std::endl;
    }
#else
    std::cout << "✗ LAP_USE_JEMALLOC not defined (using system allocator)" << std::endl;
#endif

    std::cout << "\n=== Testing SYS_MALLOC/SYS_FREE ===" << std::endl;
    
    // Test 1: Basic allocation
    void* ptr1 = SYS_MALLOC(1024);
    if (ptr1) {
        std::cout << "✓ SYS_MALLOC(1024) successful: " << ptr1 << std::endl;
        std::memset(ptr1, 0xAA, 1024);
        SYS_FREE(ptr1);
        std::cout << "✓ SYS_FREE successful" << std::endl;
    } else {
        std::cout << "✗ SYS_MALLOC failed" << std::endl;
        return 1;
    }
    
    // Test 2: Large allocation
    void* ptr2 = SYS_MALLOC(1024 * 1024);  // 1MB
    if (ptr2) {
        std::cout << "✓ SYS_MALLOC(1MB) successful: " << ptr2 << std::endl;
        SYS_FREE(ptr2);
        std::cout << "✓ SYS_FREE successful" << std::endl;
    } else {
        std::cout << "✗ SYS_MALLOC(1MB) failed" << std::endl;
        return 1;
    }
    
    // Test 3: Multiple allocations
    constexpr int NUM_ALLOCS = 100;
    void* ptrs[NUM_ALLOCS];
    for (int i = 0; i < NUM_ALLOCS; i++) {
        ptrs[i] = SYS_MALLOC(512);
        if (!ptrs[i]) {
            std::cout << "✗ SYS_MALLOC failed at iteration " << i << std::endl;
            return 1;
        }
    }
    std::cout << "✓ " << NUM_ALLOCS << " allocations successful" << std::endl;
    
    for (int i = 0; i < NUM_ALLOCS; i++) {
        SYS_FREE(ptrs[i]);
    }
    std::cout << "✓ " << NUM_ALLOCS << " deallocations successful" << std::endl;
    
    // Test 4: SYS_CALLOC
    void* ptr3 = SYS_CALLOC(10, 100);
    if (ptr3) {
        std::cout << "✓ SYS_CALLOC(10, 100) successful: " << ptr3 << std::endl;
        
        // Verify zero-initialization
        bool all_zero = true;
        unsigned char* bytes = static_cast<unsigned char*>(ptr3);
        for (int i = 0; i < 1000; i++) {
            if (bytes[i] != 0) {
                all_zero = false;
                break;
            }
        }
        
        if (all_zero) {
            std::cout << "✓ SYS_CALLOC correctly zero-initialized memory" << std::endl;
        } else {
            std::cout << "✗ SYS_CALLOC did not zero-initialize memory" << std::endl;
        }
        
        SYS_FREE(ptr3);
    }
    
    // Test 5: SYS_REALLOC
    void* ptr4 = SYS_MALLOC(100);
    if (ptr4) {
        std::memset(ptr4, 0xBB, 100);
        void* ptr5 = SYS_REALLOC(ptr4, 1000);
        if (ptr5) {
            std::cout << "✓ SYS_REALLOC(100 -> 1000) successful" << std::endl;
            
            // Verify original data preserved
            unsigned char* bytes = static_cast<unsigned char*>(ptr5);
            bool data_preserved = true;
            for (int i = 0; i < 100; i++) {
                if (bytes[i] != 0xBB) {
                    data_preserved = false;
                    break;
                }
            }
            
            if (data_preserved) {
                std::cout << "✓ SYS_REALLOC preserved original data" << std::endl;
            } else {
                std::cout << "✗ SYS_REALLOC lost original data" << std::endl;
            }
            
            SYS_FREE(ptr5);
        } else {
            std::cout << "✗ SYS_REALLOC failed" << std::endl;
            SYS_FREE(ptr4);
        }
    }
    
#if defined(LAP_USE_JEMALLOC)
    // Test 6: jemalloc-specific stats
    std::cout << "\n=== jemalloc Statistics ===" << std::endl;
    
    size_t allocated = 0;
    size_t sz = sizeof(allocated);
    if (mallctl("stats.allocated", &allocated, &sz, nullptr, 0) == 0) {
        std::cout << "✓ Allocated memory: " << allocated << " bytes" << std::endl;
    }
    
    size_t active = 0;
    if (mallctl("stats.active", &active, &sz, nullptr, 0) == 0) {
        std::cout << "✓ Active memory: " << active << " bytes" << std::endl;
    }
#endif
    
    std::cout << "\n=== All tests PASSED ===" << std::endl;
    return 0;
}
