/**
 * @file        test_memory_allocator.cpp
 * @brief       Test MemoryAllocator with STL containers
 * @date        2025-11-02
 */

#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <list>
#include <set>
#include "CMemory.hpp"
#include "CMemoryAllocator.hpp"
#include "CConfig.hpp"

using namespace lap::core;

// Helper to print memory statistics
void printMemoryStats(const char* label) {
    auto stats = Memory::getMemoryStats();
    std::cout << "\n[" << label << "] Memory Statistics:\n";
    std::cout << "  Current allocated size: " << stats.currentAllocSize << " bytes\n";
    std::cout << "  Current allocated blocks: " << stats.currentAllocCount << "\n";
    std::cout << "  Total pool memory: " << stats.totalPoolMemory << " bytes\n";
    std::cout << "  Pool count: " << stats.poolCount << "\n";
}

int main() {
    std::cout << "=== Testing MemoryAllocator with STL Containers ===\n\n";
    
    // Initialize ConfigManager to load memory configuration
    ConfigManager::getInstance().initialize("config.json");
    
    printMemoryStats("Initial State");
    
    // Test 1: Vector with MemoryAllocator
    {
        std::cout << "\n--- Test 1: std::vector<int, MemoryAllocator<int>> ---\n";
        std::vector<int, MemoryAllocator<int>> vec;
        
        std::cout << "Pushing 10 elements...\n";
        for (int i = 0; i < 10; ++i) {
            vec.push_back(i * 10);
        }
        
        std::cout << "Vector size: " << vec.size() << ", capacity: " << vec.capacity() << "\n";
        std::cout << "Contents: ";
        for (const auto& v : vec) {
            std::cout << v << " ";
        }
        std::cout << "\n";
        
        printMemoryStats("After vector operations");
    }
    
    printMemoryStats("After vector destroyed");
    
    // Test 2: String with MemoryAllocator
    {
        std::cout << "\n--- Test 2: std::basic_string with MemoryAllocator ---\n";
        using MyString = std::basic_string<char, std::char_traits<char>, MemoryAllocator<char>>;
        
        MyString str1("Hello, MemoryAllocator!");
        MyString str2 = str1 + " Testing...";
        
        std::cout << "str1: " << str1 << "\n";
        std::cout << "str2: " << str2 << " (length: " << str2.length() << ")\n";
        
        printMemoryStats("After string operations");
    }
    
    printMemoryStats("After strings destroyed");
    
    // Test 3: Map with MemoryAllocator
    {
        std::cout << "\n--- Test 3: std::map with MemoryAllocator ---\n";
        using MyMap = std::map<int, std::string, 
                               std::less<int>, 
                               MemoryAllocator<std::pair<const int, std::string>>>;
        
        MyMap myMap;
        myMap[1] = "one";
        myMap[2] = "two";
        myMap[3] = "three";
        myMap[10] = "ten";
        myMap[100] = "hundred";
        
        std::cout << "Map contents:\n";
        for (const auto& [key, value] : myMap) {
            std::cout << "  " << key << " -> " << value << "\n";
        }
        
        printMemoryStats("After map operations");
    }
    
    printMemoryStats("After map destroyed");
    
    // Test 4: List with MemoryAllocator
    {
        std::cout << "\n--- Test 4: std::list with MemoryAllocator ---\n";
        std::list<double, MemoryAllocator<double>> myList;
        
        for (int i = 0; i < 8; ++i) {
            myList.push_back(i * 1.5);
        }
        
        std::cout << "List size: " << myList.size() << "\n";
        std::cout << "Contents: ";
        for (const auto& v : myList) {
            std::cout << v << " ";
        }
        std::cout << "\n";
        
        printMemoryStats("After list operations");
    }
    
    printMemoryStats("After list destroyed");
    
    // Test 5: Set with MemoryAllocator
    {
        std::cout << "\n--- Test 5: std::set with MemoryAllocator ---\n";
        std::set<int, std::less<int>, MemoryAllocator<int>> mySet;
        
        mySet.insert({5, 2, 8, 1, 9, 3, 7});
        
        std::cout << "Set size: " << mySet.size() << "\n";
        std::cout << "Sorted contents: ";
        for (const auto& v : mySet) {
            std::cout << v << " ";
        }
        std::cout << "\n";
        
        printMemoryStats("After set operations");
    }
    
    printMemoryStats("After set destroyed");
    
    // Test 6: Complex nested structures
    {
        std::cout << "\n--- Test 6: Nested containers ---\n";
        using InnerVec = std::vector<int, MemoryAllocator<int>>;
        using OuterVec = std::vector<InnerVec, MemoryAllocator<InnerVec>>;
        
        OuterVec nested;
        for (int i = 0; i < 3; ++i) {
            InnerVec inner;
            for (int j = 0; j < 4; ++j) {
                inner.push_back(i * 10 + j);
            }
            nested.push_back(inner);
        }
        
        std::cout << "Nested vector structure:\n";
        for (size_t i = 0; i < nested.size(); ++i) {
            std::cout << "  Row " << i << ": ";
            for (const auto& val : nested[i]) {
                std::cout << val << " ";
            }
            std::cout << "\n";
        }
        
        printMemoryStats("After nested containers");
    }
    
    printMemoryStats("After nested containers destroyed");
    
    // Test 7: Large allocation test
    {
        std::cout << "\n--- Test 7: Large allocation stress test ---\n";
        std::vector<int, MemoryAllocator<int>> largeVec;
        
        std::cout << "Allocating 1000 elements...\n";
        for (int i = 0; i < 1000; ++i) {
            largeVec.push_back(i);
        }
        
        std::cout << "Vector size: " << largeVec.size() << ", capacity: " << largeVec.capacity() << "\n";
        std::cout << "Sum of first 10: ";
        int sum = 0;
        for (int i = 0; i < 10; ++i) {
            sum += largeVec[i];
        }
        std::cout << sum << "\n";
        
        printMemoryStats("After large allocation");
    }
    
    printMemoryStats("Final State (after all destroyed)");
    
    // Check for memory leaks
    std::cout << "\n--- Memory Leak Check ---\n";
    auto finalStats = Memory::getMemoryStats();
    if (finalStats.currentAllocCount == 0) {
        std::cout << "✓ No memory leaks detected - all allocations properly freed!\n";
        std::cout << "  Final allocated blocks: " << finalStats.currentAllocCount << "\n";
        std::cout << "  Final allocated size: " << finalStats.currentAllocSize << " bytes\n";
    } else {
        std::cout << "⚠ Warning: Still have " << finalStats.currentAllocCount 
                  << " allocated blocks (" << finalStats.currentAllocSize << " bytes)\n";
    }
    
    std::cout << "\n=== Test Complete ===\n";
    
    return (finalStats.currentAllocCount == 0) ? 0 : 1;
}
