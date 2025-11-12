/**
 * @file        CMemory.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       Memory management facade and STL allocator for LightAP Core
 * @date        2025-11-12
 * @details     Provides:
 *              - Memory facade for manual memory management
 *              - STL-compatible allocator (StlMemoryAllocator)
 *              - Global operator new/delete overloads
 *              - Type aliases for allocator_traits
 * @copyright   Copyright (c) 2025
 * @version     2.0
 * sdk:
 * platform:
 * project:     LightAP
 */
#ifndef LAP_CORE_MEMORY_HPP
#define LAP_CORE_MEMORY_HPP

#include "CTypedef.hpp"
#include "CMemoryManager.hpp"
#include <memory>

// Global operator new/delete overloads (defined in CMemory.cpp)
void* operator new(std::size_t size);
void* operator new[](std::size_t size);
void operator delete(void* ptr) noexcept;
void operator delete[](void* ptr) noexcept;

namespace lap {
namespace core {
    /**
     * @brief Memory management facade providing static allocation APIs
     * @details This class wraps MemoryManager for convenient memory operations.
     *          All methods are thread-safe and route to the global MemoryManager singleton.
     */
    class Memory final {
    public:
        /**
         * @brief Allocate memory with optional tracking metadata
         * @param size Size in bytes to allocate
         * @param className Optional class name for tracking (default: nullptr)
         * @param classId Optional class ID for tracking (default: 0)
         * @return Pointer to allocated memory, or nullptr on failure
         */
        static void* malloc(Size size, const Char* className = nullptr, UInt32 classId = 0) noexcept;
        
        /**
         * @brief Free previously allocated memory
         * @param ptr Pointer to memory to free (nullptr-safe)
         */
        static void free(void* ptr) noexcept;
        
        /**
         * @brief Verify pointer validity (debug builds only)
         * @param ptr Pointer to check
         * @param hint Optional hint string for error messages
         * @return 0 if valid, non-zero error code otherwise
         */
        static Int32 checkPtr(void* ptr, const Char* hint = nullptr) noexcept;
        
        /**
         * @brief Register a class name for memory tracking
         * @param className Class name string (max 63 characters)
         * @return Unique class ID for use in malloc()
         */
        static UInt32 registerClassName(const Char* className) noexcept;
        
        /**
         * @brief Get current memory statistics
         * @return MemoryStats structure with allocation counters and pool info
         */
        static MemoryStats getMemoryStats() noexcept;

    private:
        Memory() = delete;
        Memory(const Memory&) = delete;
        Memory& operator=(const Memory&) = delete;
        ~Memory() = delete;
    };

    /**
     * @brief STL-compatible allocator routing to MemoryManager pools
     * @tparam T Value type to allocate
     * @details This allocator is:
     *          - Stateless (all instances are interchangeable)
     *          - Thread-safe (backed by MemoryManager)
     *          - Efficient for small objects (uses memory pools)
     *          - Compatible with all STL containers
     * 
     * @usage
     * @code
     * Vector<int, StlMemoryAllocator<int>> vec;
     * Map<String, int, std::less<String>, 
     *     StlMemoryAllocator<Pair<const String, int>>> map;
     * @endcode
     */
    template <typename T>
    class StlMemoryAllocator {
    public:
        // Type definitions (C++17 allocator requirements)
        using value_type = T;
        using size_type = Size;
        using difference_type = std::ptrdiff_t;
        using propagate_on_container_move_assignment = std::true_type;
        using is_always_equal = std::true_type;

        template <typename U>
        struct rebind { 
            using other = StlMemoryAllocator<U>; 
        };

        // Constructors (stateless, trivial)
        constexpr StlMemoryAllocator() noexcept = default;
        constexpr StlMemoryAllocator(const StlMemoryAllocator&) noexcept = default;
        
        template <typename U>
        constexpr StlMemoryAllocator(const StlMemoryAllocator<U>&) noexcept {}

        /**
         * @brief Allocate memory for n objects of type T
         * @param n Number of objects to allocate
         * @return Pointer to allocated memory
         * @throws std::bad_alloc if allocation fails
         */
        [[nodiscard]] T* allocate(size_type n) {
            if (n > max_size()) {
                throw std::bad_alloc();
            }
            void* p = Memory::malloc(n * sizeof(T), "StlMemoryAllocator", 0);
            if (!p) {
                throw std::bad_alloc();
            }
            return static_cast<T*>(p);
        }

        /**
         * @brief Deallocate memory previously allocated by this allocator
         * @param p Pointer to memory to deallocate
         * @param n Number of objects (ignored, for interface compatibility)
         */
        void deallocate(T* p, size_type /*n*/) noexcept {
            Memory::free(static_cast<void*>(p));
        }

        /**
         * @brief Get maximum number of objects that can be allocated
         * @return Maximum size_type value / sizeof(T)
         */
        constexpr size_type max_size() const noexcept {
            return static_cast<size_type>(-1) / sizeof(T);
        }

        // Equality comparison (stateless allocators are always equal)
        template <typename U>
        constexpr bool operator==(const StlMemoryAllocator<U>&) const noexcept { 
            return true; 
        }
        
        template <typename U>
        constexpr bool operator!=(const StlMemoryAllocator<U>&) const noexcept { 
            return false; 
        }
    };

    /**
     * @brief Convenience alias for allocator_traits
     * @tparam T Value type
     */
    template <typename T>
    using StlMemoryAllocatorTraits = std::allocator_traits<StlMemoryAllocator<T>>;

    /**
     * @brief Helper function to create allocator-aware containers
     * @example
     * auto vec = MakeVectorWithMemoryAllocator<int>();
     */
    template <typename T>
    inline Vector<T, StlMemoryAllocator<T>> MakeVectorWithMemoryAllocator() {
        return Vector<T, StlMemoryAllocator<T>>();
    }

} // namespace core
} // namespace lap

#endif // LAP_CORE_MEMORY_HPP
