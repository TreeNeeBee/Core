#include "CMemoryManager.hpp"
#include "CMemory.hpp"

// Memory facade implementations
namespace lap {
namespace core {
    // Memory implementation
    void* Memory::malloc(Size size, const Char* className, UInt32 classId) noexcept {
        try {
            return MemoryManager::getInstance()->malloc(size, className, classId);
        } catch (...) {
            return nullptr;
        }
    }

    void Memory::free(void* ptr) noexcept {
        try {
            MemoryManager::getInstance()->free(ptr);
        } catch (...) {
            // swallow
        }
    }

    Int32 Memory::checkPtr(void* ptr, const Char* hint) noexcept {
        try {
            return MemoryManager::getInstance()->checkPtr(ptr, hint);
        } catch (...) {
            return -1;
        }
    }

    UInt32 Memory::registerClassName(const Char* className) noexcept {
        try {
            return MemoryManager::getInstance()->registerClassName(className);
        } catch (...) {
            return 0u;
        }
    }

    MemoryStats Memory::getMemoryStats() noexcept {
        try {
            return MemoryManager::getInstance()->getMemoryStats();
        } catch (...) {
            // Return empty stats on error
            MemoryStats stats;
            stats.currentAllocSize = 0;
            stats.currentAllocCount = 0;
            stats.totalPoolMemory = 0;
            stats.poolCount = 0;
            stats.threadCount = 0;
            return stats;
        }
    }
} // namespace core
} // namespace lap

MEMORY_CONTROL