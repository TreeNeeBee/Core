/**
 * @file        CMemoryAllocator.hpp
 * @brief       Adapter allocator that routes STL allocations to lap::core::Memory
 * @date        2025-11-01
 */
#ifndef LAP_CORE_MEMORY_ALLOCATOR_HPP
#define LAP_CORE_MEMORY_ALLOCATOR_HPP

#include <cstddef>
#include <memory>
#include "CMemory.hpp"

namespace lap {
namespace core {
    // Minimal stateless allocator that works with std::allocator_traits
    // and forwards allocate/deallocate to lap::core::Memory
    template <class T>
    class MemoryAllocator {
    public:
        using value_type = T;
        using pointer = T*;
        using const_pointer = const T*;
        using void_pointer = void*;
        using const_void_pointer = const void*;
        using size_type = ::lap::core::Size;
        using difference_type = std::ptrdiff_t;

        template <class U>
        struct rebind { using other = MemoryAllocator<U>; };

        // Constructors are stateless
        constexpr MemoryAllocator() noexcept {}
        template <class U>
        constexpr MemoryAllocator(const MemoryAllocator<U>&) noexcept {}

        [[nodiscard]] pointer allocate(size_type n) {
            // Check overflow
            if (n > max_size()) {
                throw std::bad_alloc();
            }
            void* p = ::lap::core::Memory::malloc(n * sizeof(T));
            if (!p) throw std::bad_alloc();
            return static_cast<pointer>(p);
        }

        void deallocate(pointer p, size_type /*n*/) noexcept {
            ::lap::core::Memory::free(static_cast<void*>(p));
        }

        // Construct an object (required for full allocator_traits support)
        template <class U, class... Args>
        void construct(U* p, Args&&... args) {
            ::new (static_cast<void*>(p)) U(std::forward<Args>(args)...);
        }

        // Destroy an object (required for full allocator_traits support)
        template <class U>
        void destroy(U* p) noexcept {
            p->~U();
        }

        // Maximum number of elements storable
        constexpr size_type max_size() const noexcept {
            return static_cast<size_type>(-1) / sizeof(T);
        }

        // Propagation traits (stateless allocator)
        using propagate_on_container_move_assignment = std::true_type;
        using is_always_equal = std::true_type;

        // Equality (stateless)
        template <class U>
        constexpr bool operator==(const MemoryAllocator<U>&) const noexcept { return true; }
        template <class U>
        constexpr bool operator!=(const MemoryAllocator<U>&) const noexcept { return false; }
    };

    // Convenience alias for allocator_traits on our allocator
    template <class T>
    using MemoryAllocatorTraits = std::allocator_traits< MemoryAllocator<T> >;
} // namespace core
} // namespace lap

#endif // LAP_CORE_MEMORY_ALLOCATOR_HPP
