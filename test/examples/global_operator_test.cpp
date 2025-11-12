#include <iostream>
#include <vector>
#include <string>
#include "CMemory.hpp"
#include "CInitialization.hpp"

using namespace lap::core;

// Test class without any memory control macros
class PlainClass {
public:
    PlainClass(int val) : value(val) {
        std::cout << "  PlainClass(" << val << ") constructed\n";
    }
    ~PlainClass() {
        std::cout << "  PlainClass(" << value << ") destroyed\n";
    }
    int value;
};

// Test class with MEMORY_CONTROL
class ControlledClass {
public:
    MEMORY_CONTROL
    ControlledClass(int val) : value(val) {
        std::cout << "  ControlledClass(" << val << ") constructed\n";
    }
    ~ControlledClass() {
        std::cout << "  ControlledClass(" << value << ") destroyed\n";
    }
    int value;
};

int main() {
    std::cout << "=== Global Operator new/delete Test ===\n\n";

    // AUTOSAR-compliant initialization (includes MemoryManager)
    auto initResult = Initialize();
    if (!initResult.HasValue()) {
        std::cerr << "Failed to initialize Core: " << initResult.Error().Message() << "\n";
        return 1;
    }
    std::cout << "[Info] Core initialized\n\n";

    // Test 1: Plain new/delete (should use global operators defined by MEMORY_CONTROL)
    std::cout << "Test 1: Plain class with global operators\n";
    PlainClass* p1 = new PlainClass(100);
    delete p1;
    std::cout << "\n";

    // Test 2: Array new/delete
    std::cout << "Test 2: Array allocation\n";
    int* arr = new int[10];
    for (int i = 0; i < 10; ++i) arr[i] = i;
    std::cout << "  Array allocated and initialized\n";
    delete[] arr;
    std::cout << "  Array deleted\n\n";

    // Test 3: Class with MEMORY_CONTROL (explicit routing)
    std::cout << "Test 3: Controlled class\n";
    ControlledClass* p2 = new ControlledClass(200);
    delete p2;
    std::cout << "\n";

    // Test 4: STL containers (should also use global operators)
    std::cout << "Test 4: STL vector allocation\n";
    {
        std::vector<int> vec;
        vec.reserve(100);
        for (int i = 0; i < 50; ++i) {
            vec.push_back(i);
        }
        std::cout << "  Vector with " << vec.size() << " elements created\n";
    }
    std::cout << "  Vector destroyed\n\n";

    // Test 5: std::string allocation
    std::cout << "Test 5: String allocation\n";
    {
        std::string str = "This is a test string that should trigger heap allocation";
        std::cout << "  String created: " << str.substr(0, 20) << "...\n";
    }
    std::cout << "  String destroyed\n\n";

    // Test 6: Mix of allocations
    std::cout << "Test 6: Mixed allocations\n";
    PlainClass* p3 = new PlainClass(300);
    ControlledClass* p4 = new ControlledClass(400);
    char* buffer = new char[1024];
    std::cout << "  Multiple objects allocated\n";
    
    delete p3;
    delete p4;
    delete[] buffer;
    std::cout << "  All objects deleted\n\n";

    // Print memory state
    std::cout << "=== Final Memory State ===\n";
    MemoryManager::getInstance()->outputState(0);

    // AUTOSAR-compliant deinitialization (includes MemoryManager cleanup)
    auto deinitResult = Deinitialize();
    (void)deinitResult;
    std::cout << "[Info] Core deinitialized and configuration saved\n";

    std::cout << "\n=== Test Completed Successfully ===\n";
    return 0;
}
