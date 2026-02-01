#ifndef DEF_LAP_CORE_ATOMIC_HPP
#define DEF_LAP_CORE_ATOMIC_HPP

#include <atomic>
#include <type_traits>

template<typename T>
struct RawAtomic {
    static_assert(std::is_trivially_copyable<T>::value,
                  "RawAtomic requires TriviallyCopyable type");

    T value; // POD 成员

    // store 支持 memory_order
    void store(T v, std::memory_order order = std::memory_order_seq_cst) noexcept {
        __atomic_store_n(&value, v, static_cast<int>(order));
    }

    // load 支持 memory_order
    T load(std::memory_order order = std::memory_order_seq_cst) const noexcept {
        return __atomic_load_n(&value, static_cast<int>(order));
    }

    // compare_exchange_strong
    bool compare_exchange(T& expected, T desired,
                          std::memory_order success = std::memory_order_seq_cst,
                          std::memory_order failure = std::memory_order_seq_cst) noexcept {
        return __atomic_compare_exchange_n(&value, &expected, desired, false,
                                           static_cast<int>(success), static_cast<int>(failure));
    }

    // compare_exchange_weak
    bool compare_exchange_weak(T& expected, T desired,
                               std::memory_order success = std::memory_order_seq_cst,
                               std::memory_order failure = std::memory_order_seq_cst) noexcept {
        return __atomic_compare_exchange_n(&value, &expected, desired, true,
                                           static_cast<int>(success), static_cast<int>(failure));
    }

    // fetch_add/fetch_sub
    T fetch_add(T v, std::memory_order order = std::memory_order_seq_cst) noexcept {
        return __atomic_fetch_add(&value, v, static_cast<int>(order));
    }

    T fetch_sub(T v, std::memory_order order = std::memory_order_seq_cst) noexcept {
        return __atomic_fetch_sub(&value, v, static_cast<int>(order));
    }

    // 新增：fetch_or/fetch_and/fetch_xor
    T fetch_or(T v, std::memory_order order = std::memory_order_seq_cst) noexcept {
        return __atomic_fetch_or(&value, v, static_cast<int>(order));
    }

    T fetch_and(T v, std::memory_order order = std::memory_order_seq_cst) noexcept {
        return __atomic_fetch_and(&value, v, static_cast<int>(order));
    }

    T fetch_xor(T v, std::memory_order order = std::memory_order_seq_cst) noexcept {
        return __atomic_fetch_xor(&value, v, static_cast<int>(order));
    }
};

#endif // DEF_LAP_CORE_ATOMIC_HPP