/**
 * @file        memory_example_comprehensive.cpp
 * @brief       Comprehensive example demonstrating LightAP Core memory management
 * @date        2025-11-12
 * @details     Shows best practices for using Memory facade, StlMemoryAllocator,
 *              and MemoryManager in real-world scenarios
 */

#include "CInitialization.hpp"
#include "CMemory.hpp"
#include "CMemoryManager.hpp"
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <memory>

using namespace lap::core;

// ============================================================================
// Example 1: Basic Memory Allocation with Tracking
// ============================================================================

void example1_basic_allocation() {
    std::cout << "\n=== Example 1: Basic Memory Allocation ===" << std::endl;
    
    // Register class name for tracking
    UInt32 classId = Memory::registerClassName("MyDataStructure");
    
    // Allocate memory with tracking
    void* ptr = Memory::malloc(256, "MyDataStructure", classId);
    if (ptr) {
        std::cout << "✓ Allocated 256 bytes (class ID: " << classId << ")" << std::endl;
        
        // Use the memory (example: initialize with pattern)
        char* data = static_cast<char*>(ptr);
        for (int i = 0; i < 256; ++i) {
            data[i] = static_cast<char>(i % 256);
        }
        
        // Free when done
        Memory::free(ptr);
        std::cout << "✓ Memory freed" << std::endl;
    }
}

// ============================================================================
// Example 2: Using STL Containers with StlMemoryAllocator
// ============================================================================

void example2_stl_containers() {
    std::cout << "\n=== Example 2: STL Containers with Pool Allocator ===" << std::endl;
    
    // Vector with custom allocator
    {
        Vector<int, StlMemoryAllocator<int>> vec;
        for (int i = 0; i < 100; ++i) {
            vec.push_back(i * i);
        }
        std::cout << "✓ Created vector with " << vec.size() << " elements" << std::endl;
    }
    
    // Map with custom allocator
    {
        using MyMap = Map<String, int, 
                          std::less<String>,
                          StlMemoryAllocator<Pair<const String, int>>>;
        
        MyMap settings;
        settings["max_connections"] = 100;
        settings["timeout_ms"] = 5000;
        settings["buffer_size"] = 4096;
        
        std::cout << "✓ Created map with " << settings.size() << " entries" << std::endl;
        for (const auto& [key, value] : settings) {
            std::cout << "  " << key << " = " << value << std::endl;
        }
    }
    
    // Helper function
    {
        auto vec = MakeVectorWithMemoryAllocator<double>();
        vec.push_back(3.14159);
        vec.push_back(2.71828);
        std::cout << "✓ Used helper function to create vector" << std::endl;
    }
}

// ============================================================================
// Example 3: Custom Class with operator new/delete override
// ============================================================================

class SmartObject {
public:
    IMP_OPERATOR_NEW(SmartObject)  // Override operator new/delete
    
    SmartObject(int id, const String& name) 
        : id_(id), name_(name) {
        std::cout << "  SmartObject(" << id_ << ", \"" << name_ << "\") constructed" << std::endl;
    }
    
    ~SmartObject() {
        std::cout << "  ~SmartObject(" << id_ << ") destroyed" << std::endl;
    }
    
    void display() const {
        std::cout << "  SmartObject[" << id_ << "]: " << name_ << std::endl;
    }
    
private:
    int id_;
    String name_;
};

void example3_custom_class() {
    std::cout << "\n=== Example 3: Custom Class with Memory Pool ===" << std::endl;
    
    // Single object
    {
        SmartObject* obj = new SmartObject(1, "First");
        obj->display();
        delete obj;
    }
    
    // Array of objects
    {
        std::cout << "Creating array of 3 objects:" << std::endl;
        SmartObject* arr = new SmartObject[3]{
            SmartObject(10, "Alice"),
            SmartObject(20, "Bob"),
            SmartObject(30, "Charlie")
        };
        
        for (int i = 0; i < 3; ++i) {
            arr[i].display();
        }
        
        delete[] arr;
    }
}

// ============================================================================
// Example 4: Memory Statistics and Monitoring
// ============================================================================

void example4_memory_statistics() {
    std::cout << "\n=== Example 4: Memory Statistics ===" << std::endl;
    
    // Get initial stats
    auto stats_before = Memory::getMemoryStats();
    std::cout << "Initial state:" << std::endl;
    std::cout << "  Pool count: " << stats_before.poolCount << std::endl;
    std::cout << "  Current allocations: " << stats_before.currentAllocCount << std::endl;
    std::cout << "  Allocated size: " << stats_before.currentAllocSize << " bytes" << std::endl;
    std::cout << "  Total pool memory: " << stats_before.totalPoolMemory << " bytes" << std::endl;
    
    // Perform some allocations
    {
        std::vector<void*> pointers;
        for (int i = 0; i < 100; ++i) {
            pointers.push_back(Memory::malloc(64));
        }
        
        auto stats_during = Memory::getMemoryStats();
        std::cout << "\nAfter 100 allocations:" << std::endl;
        std::cout << "  Current allocations: " << stats_during.currentAllocCount 
                  << " (+" << (stats_during.currentAllocCount - stats_before.currentAllocCount) << ")" << std::endl;
        std::cout << "  Allocated size: " << stats_during.currentAllocSize << " bytes "
                  << "(+" << (stats_during.currentAllocSize - stats_before.currentAllocSize) << " bytes)" << std::endl;
        
        // Free all
        for (void* p : pointers) {
            Memory::free(p);
        }
    }
    
    auto stats_after = Memory::getMemoryStats();
    std::cout << "\nAfter freeing:" << std::endl;
    std::cout << "  Current allocations: " << stats_after.currentAllocCount << std::endl;
    std::cout << "  Allocated size: " << stats_after.currentAllocSize << " bytes" << std::endl;
}

// ============================================================================
// Example 5: Complex Data Structures
// ============================================================================

struct Node {
    int value;
    Node* left;
    Node* right;
    
    Node(int v) : value(v), left(nullptr), right(nullptr) {}
};

void example5_complex_structures() {
    std::cout << "\n=== Example 5: Complex Data Structures ===" << std::endl;
    
    // Create a small binary tree
    Node* root = new Node(50);
    root->left = new Node(30);
    root->right = new Node(70);
    root->left->left = new Node(20);
    root->left->right = new Node(40);
    root->right->left = new Node(60);
    root->right->right = new Node(80);
    
    std::cout << "✓ Created binary tree with 7 nodes" << std::endl;
    
    // Cleanup (post-order traversal)
    std::function<void(Node*)> deleteTree = [&](Node* node) {
        if (node) {
            deleteTree(node->left);
            deleteTree(node->right);
            delete node;
        }
    };
    
    deleteTree(root);
    std::cout << "✓ Tree destroyed" << std::endl;
}

// ============================================================================
// Example 6: Thread-Safe Allocations
// ============================================================================

#include <thread>
#include <atomic>

void example6_thread_safety() {
    std::cout << "\n=== Example 6: Thread-Safe Allocations ===" << std::endl;
    
    constexpr int kThreadCount = 4;
    constexpr int kAllocPerThread = 100;
    std::atomic<int> total_allocated{0};
    
    auto worker = [&](int thread_id) {
        Vector<void*, StlMemoryAllocator<void*>> local_ptrs;
        
        for (int i = 0; i < kAllocPerThread; ++i) {
            void* p = Memory::malloc(64);
            if (p) {
                local_ptrs.push_back(p);
                total_allocated++;
            }
        }
        
        std::cout << "  Thread " << thread_id << ": allocated " 
                  << local_ptrs.size() << " blocks" << std::endl;
        
        // Free all
        for (void* p : local_ptrs) {
            Memory::free(p);
        }
    };
    
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back(worker, i);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    std::cout << "✓ Total allocations across threads: " << total_allocated << std::endl;
}

// ============================================================================
// Example 7: Best Practices
// ============================================================================

void example7_best_practices() {
    std::cout << "\n=== Example 7: Best Practices ===" << std::endl;
    
    std::cout << "\n1. Use RAII for automatic cleanup:" << std::endl;
    {
        // Good: Using std::unique_ptr with custom deleter
        auto ptr = std::unique_ptr<int[], decltype(&Memory::free)>(
            static_cast<int*>(Memory::malloc(100 * sizeof(int))),
            Memory::free
        );
        std::cout << "  ✓ Memory will be automatically freed on scope exit" << std::endl;
    }
    
    std::cout << "\n2. Prefer STL containers with StlMemoryAllocator:" << std::endl;
    {
        Vector<String, StlMemoryAllocator<String>> names;
        names.push_back("Alice");
        names.push_back("Bob");
        std::cout << "  ✓ Vector uses pool allocator, automatic cleanup" << std::endl;
    }
    
    std::cout << "\n3. Use IMP_OPERATOR_NEW for frequently allocated classes:" << std::endl;
    {
        SmartObject* obj = new SmartObject(100, "Example");
        obj->display();
        delete obj;
        std::cout << "  ✓ Uses pool allocator transparently" << std::endl;
    }
    
    std::cout << "\n4. Monitor memory usage in production:" << std::endl;
    {
        auto stats = Memory::getMemoryStats();
        if (stats.currentAllocSize > 100 * 1024 * 1024) {  // > 100 MB
            std::cout << "  ⚠ Warning: High memory usage!" << std::endl;
        } else {
            std::cout << "  ✓ Memory usage is reasonable" << std::endl;
        }
    }
    
    std::cout << "\n5. nullptr checks are unnecessary for free():" << std::endl;
    {
        void* ptr = nullptr;
        Memory::free(ptr);  // Safe, no-op
        std::cout << "  ✓ Memory::free(nullptr) is safe" << std::endl;
    }
}

// ============================================================================
// Main Function
// ============================================================================

int main() {
    std::cout << "╔════════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  LightAP Core - Memory Management Comprehensive Example      ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════════════════════════╝" << std::endl;
    
    // AUTOSAR-compliant initialization
    auto initResult = Initialize();
    if (!initResult.HasValue()) {
        std::cerr << "Initialization failed!" << std::endl;
        return 1;
    }
    
    try {
        example1_basic_allocation();
        example2_stl_containers();
        example3_custom_class();
        example4_memory_statistics();
        example5_complex_structures();
        example6_thread_safety();
        example7_best_practices();
        
        std::cout << "\n╔════════════════════════════════════════════════════════════════╗" << std::endl;
        std::cout << "║  ✓ All examples completed successfully!                      ║" << std::endl;
        std::cout << "╚════════════════════════════════════════════════════════════════╝" << std::endl;
        
        // AUTOSAR-compliant deinitialization
        auto deinitResult = Deinitialize();
        (void)deinitResult;
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        
        // AUTOSAR-compliant deinitialization on error
        auto deinitResult = Deinitialize();
        (void)deinitResult;
        
        return 1;
    }
}
