#ifndef AURORE_SAFE_MEMCPY_H
#define AURORE_SAFE_MEMCPY_H

#include <cstring>
#include <stdexcept>
#include <cassert>

namespace Aurore {

inline void* safe_memcpy(void* dest, const void* src, size_t dest_size, size_t copy_size, const char* file = __FILE__, int line = __LINE__) {
    if (dest == nullptr || src == nullptr) {
        throw std::invalid_argument("Null pointer passed to safe_memcpy");
    }
    if (copy_size > dest_size) {
        throw std::runtime_error("safe_memcpy: copy_size (" + std::to_string(copy_size) + 
                                 ") exceeds destination size (" + std::to_string(dest_size) + 
                                 ") at " + std::string(file) + ":" + std::to_string(line));
    }
    return std::memcpy(dest, src, copy_size);
}

inline void* safe_memcpy_with_size(void* dest, const void* src, size_t dest_size, size_t copy_size, 
                                    const char* context = "unknown") {
    if (dest == nullptr || src == nullptr) {
        throw std::invalid_argument("Null pointer passed to safe_memcpy: " + std::string(context));
    }
    if (copy_size > dest_size) {
        throw std::runtime_error("safe_memcpy: copy_size=" + std::to_string(copy_size) + 
                                 " > dest_size=" + std::to_string(dest_size) + 
                                 " (context: " + context + ")");
    }
    return std::memcpy(dest, src, copy_size);
}

} // namespace Aurore

#define SAFE_MEMCPY(dest, src, dest_size, copy_size) \
    Aurore::safe_memcpy(dest, src, dest_size, copy_size, __FILE__, __LINE__)

#define SAFE_MEMCPY_CTX(dest, src, dest_size, copy_size, ctx) \
    Aurore::safe_memcpy_with_size(dest, src, dest_size, copy_size, ctx)

#endif // AURORE_SAFE_MEMCPY_H
