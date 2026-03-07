#ifndef AURORE_SAFE_BOUNDS_H
#define AURORE_SAFE_BOUNDS_H

#include <vector>
#include <stdexcept>
#include <string>

// REMEDIATION 2026-02-02: Finding #9 - Array bounds checking utilities

namespace Aurore {

template<typename T>
inline T& safe_vector_at(std::vector<T>& vec, size_t index, const char* file = __FILE__, int line = __LINE__) {
    if (index >= vec.size()) {
        throw std::out_of_range("safe_vector_at: index " + std::to_string(index) + 
                                  " out of bounds (size=" + std::to_string(vec.size()) + 
                                  ") at " + std::string(file) + ":" + std::to_string(line));
    }
    return vec[index];
}

template<typename T>
inline const T& safe_vector_at(const std::vector<T>& vec, size_t index, const char* file = __FILE__, int line = __LINE__) {
    if (index >= vec.size()) {
        throw std::out_of_range("safe_vector_at: index " + std::to_string(index) + 
                                  " out of bounds (size=" + std::to_string(vec.size()) + 
                                  ") at " + std::string(file) + ":" + std::to_string(line));
    }
    return vec[index];
}

template<typename T, size_t N>
inline T& safe_array_at(T (&arr)[N], size_t index, const char* file = __FILE__, int line = __LINE__) {
    if (index >= N) {
        throw std::out_of_range("safe_array_at: index " + std::to_string(index) + 
                                  " out of bounds (size=" + std::to_string(N) + 
                                  ") at " + std::string(file) + ":" + std::to_string(line));
    }
    return arr[index];
}

} // namespace Aurore

#define SAFE_VECTOR_AT(vec, idx) Aurore::safe_vector_at(vec, idx, __FILE__, __LINE__)
#define SAFE_ARRAY_AT(arr, idx) Aurore::safe_array_at(arr, idx, __FILE__, __LINE__)

#endif // AURORE_SAFE_BOUNDS_H
