#ifndef AURORE_ATOMIC_ORDERING_H
#define AURORE_ATOMIC_ORDERING_H

#include <atomic>
#include <cstdint>

namespace Aurore {

inline void atomic_store_release(std::atomic<bool>& atom, bool value) {
    atom.store(value, std::memory_order_release);
}

inline bool atomic_load_acquire(const std::atomic<bool>& atom) {
    return atom.load(std::memory_order_acquire);
}

inline void atomic_store_release(std::atomic<int64_t>& atom, int64_t value) {
    atom.store(value, std::memory_order_release);
}

inline int64_t atomic_load_acquire(const std::atomic<int64_t>& atom) {
    return atom.load(std::memory_order_acquire);
}

inline void atomic_store_release(std::atomic<int>& atom, int value) {
    atom.store(value, std::memory_order_release);
}

inline int atomic_load_acquire(const std::atomic<int>& atom) {
    return atom.load(std::memory_order_acquire);
}

inline void atomic_store_release(std::atomic<long long>& atom, long long value) {
    atom.store(value, std::memory_order_release);
}

inline long long atomic_load_acquire(const std::atomic<long long>& atom) {
    return atom.load(std::memory_order_acquire);
}

} // namespace Aurore

#endif // AURORE_ATOMIC_ORDERING_H
