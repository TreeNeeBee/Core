#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include "CMemory.hpp"
#include "CInitialization.hpp"

using namespace lap::core;

// Class tracked with class-name aware allocation (IMP_OPERATOR_NEW)
class TrackedFoo {
public:
    IMP_OPERATOR_NEW(TrackedFoo)
    TrackedFoo() { std::cout << "[example] TrackedFoo ctor\n"; }
    ~TrackedFoo() { std::cout << "[example] TrackedFoo dtor\n"; }
    int x{42};
};

// Class using generic MEMORY_CONTROL (no class-name tag, but still via Memory)
class PooledBar {
public:
    MEMORY_CONTROL
    PooledBar() { std::cout << "[example] PooledBar ctor\n"; }
    ~PooledBar() { std::cout << "[example] PooledBar dtor\n"; }
    char buf[48];
};

int main() {
    std::cout << "[example] CMemory demo starting...\n";

    // AUTOSAR-compliant initialization (includes MemoryManager initialization)
    auto initResult = Initialize();
    if (!initResult.HasValue()) {
        std::cerr << "[example] Failed to initialize Core: " << initResult.Error().Message() << "\n";
        return 1;
    }
    std::cout << "[example] Core initialized\n";

    // Default to enabling checker; if config specifies otherwise, load will toggle it
    // Memory::enableMemoryChecker(true);

    // Register current thread name for nicer logs
    {
        UInt32 tid = static_cast<UInt32>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        MemoryManager::getInstance()->registerThreadName(tid, "main-thread");
    }
    // Allocate using the facade; checker validates, allocator picks suitable pool
    // Demonstrate explicit local PoolAllocator usage and pool states
    PoolAllocator localAlloc;
    localAlloc.initialize(8, 10);
    localAlloc.createPool(32, 4, 0, 4);
    localAlloc.createPool(64, 4, 0, 4);
    localAlloc.createPool(128, 2, 0, 2);

    // Print initial pool states
    std::cout << "[example] Initial pools:\n";
    for (UInt32 i = 0; i < localAlloc.getPoolCount(); ++i) {
        PoolAllocator::MemoryPoolState st{};
        if (localAlloc.getPoolState(i, st)) {
            std::cout << "  pool[" << i << "] size=" << st.unitAvailableSize
                      << " free=" << st.freeCount << " current=" << st.currentCount << "\n";
        }
    }

    void* p1 = localAlloc.malloc(40);
    void* p2 = localAlloc.malloc(200);
    if (!p1 || !p2) {
        std::cerr << "[example] Allocation failed\n";
        return 2;
    }
    std::cout << "[example] Allocated p1(40)=" << p1 << ", p2(200)=" << p2 << "\n";

    // Allocate objects using the macros
    TrackedFoo* foo = new TrackedFoo();
    PooledBar*  bar = new PooledBar();
    std::cout << "[example] New TrackedFoo=" << static_cast<void*>(foo)
              << ", PooledBar=" << static_cast<void*>(bar) << "\n";

    for (uint32_t i = 0; i < 20; ++i) {
        void* p3 = Memory::malloc(4);
        void* p4 = Memory::malloc(16);
        void* p5 = Memory::malloc(400);
        TrackedFoo* temp = new TrackedFoo();
        Memory::free(p3);
        Memory::free(p4);
        Memory::free(p5);
        delete temp;
    }
    std::cout << "[example] Completed allocation/deallocation loop\n";

    // Note: p1/p2 are allocated from localAlloc; don't pass them to global Memory API.
    // Validate and free via the same allocator to avoid mismatched free.
    std::cout << "[example] localAlloc-managed pointers; skipping global checkPtr for p1.\n";
    localAlloc.free(p1);
    delete bar; // this uses MEMORY_CONTROL -> Memory::free
    std::cout << "[example] Freed p1 (via localAlloc), intentionally leaking p2 to demonstrate leak report...\n";
    std::cout << "[example] Intentionally leaking TrackedFoo to demonstrate class-tagged leak...\n";

    // Dump state snapshot (leak details will be reported on program exit by checker destructor)
    MemoryManager::getInstance()->outputState(0);
    std::cout << "[example] State output requested. See memory_leak.log for details.\n";

    // Print final pool states
    std::cout << "[example] Final pools after operations:\n";
    for (UInt32 i = 0; i < localAlloc.getPoolCount(); ++i) {
        PoolAllocator::MemoryPoolState st{};
        if (localAlloc.getPoolState(i, st)) {
            std::cout << "  pool[" << i << "] size=" << st.unitAvailableSize
                      << " free=" << st.freeCount << " current=" << st.currentCount 
                      << " max=" << st.maxCount << "\n";
        }
    }

    // Print summary
    std::cout << "[example] MemoryManager operations completed successfully.\n";

    // AUTOSAR-compliant deinitialization (includes MemoryManager cleanup)
    auto deinitResult = Deinitialize();
    (void)deinitResult;
    std::cout << "[example] Core deinitialized and configuration saved.\n";

    // Config will be auto-saved (wrapped with align/check_enable/pools) by RAII guard

    std::cout << "[example] Done.\n";
    return 0;
}
