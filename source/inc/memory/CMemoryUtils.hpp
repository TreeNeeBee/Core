/**
 * @file CMemoryUtils.hpp
 * @brief Memory allocation utility macros for Core module
 * @details Provides abstraction layer for memory allocation that can route to
 *          jemalloc or standard allocator based on build configuration
 */

#ifndef LAP_CORE_MEMORY_UTILS_HPP
#define LAP_CORE_MEMORY_UTILS_HPP

#include <cstdlib>

namespace lap {
namespace core {

/**
 * @brief System memory allocation macro
 * @details Routes to jemalloc (je_malloc) if linked, otherwise std::malloc
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or nullptr on failure
 */
#define SYS_MALLOC(size) std::malloc(size)

/**
 * @brief System memory deallocation macro
 * @details Routes to jemalloc (je_free) if linked, otherwise std::free
 * @param ptr Pointer to memory to deallocate
 */
#define SYS_FREE(ptr) std::free(ptr)

/**
 * @brief System memory reallocation macro
 * @details Routes to jemalloc (je_realloc) if linked, otherwise std::realloc
 * @param ptr Pointer to previously allocated memory
 * @param size New size in bytes
 * @return Pointer to reallocated memory, or nullptr on failure
 */
#define SYS_REALLOC(ptr, size) std::realloc(ptr, size)

/**
 * @brief System memory calloc macro
 * @details Routes to jemalloc (je_calloc) if linked, otherwise std::calloc
 * @param num Number of elements
 * @param size Size of each element in bytes
 * @return Pointer to allocated and zero-initialized memory, or nullptr on failure
 */
#define SYS_CALLOC(num, size) std::calloc(num, size)

} // namespace core
} // namespace lap

#endif // LAP_CORE_MEMORY_UTILS_HPP
