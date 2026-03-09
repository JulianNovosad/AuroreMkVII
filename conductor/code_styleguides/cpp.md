# C++ Style Guide - AuroreMkVII

## Overview

This document defines the C++ coding style for AuroreMkVII. The project uses **Google C++ Style** with project-specific modifications enforced via `.clang-format` and `.clang-tidy`.

## Tooling

### Automatic Formatting
```bash
# Format all source files
cmake --build build-native --target format

# Check formatting without modifying
cmake --build build-native --target format-check
```

### Static Analysis
```bash
# Run clang-tidy
cmake --build build-native --target tidy
```

## Naming Conventions

| Element | Convention | Example |
|---------|------------|---------|
| **Classes** | PascalCase (CamelCase) | `LockFreeRingBuffer`, `SafetyMonitor` |
| **Functions** | snake_case | `update_frame()`, `run_cycle()` |
| **Variables** | snake_case | `frame_count`, `buffer_size` |
| **Constants** | kPrefix | `kMaxBufferSize`, `kDefaultTimeout` |
| **Enum Values** | kPrefix | `kStateBoot`, `kStateTracking` |
| **Template Parameters** | PascalCase | `typename T`, `size_t N` |
| **Namespaces** | snake_case | `namespace aurore`, `namespace vision` |
| **Macro Guards** | UPPER_SNAKE | `#ifndef AURORE_RING_BUFFER_HPP` |

## File Organization

### Header Files (`.hpp`)
- Location: `include/aurore/`
- Include guard: `AURORE_<FILENAME>_HPP`
- Forward declarations preferred over includes

### Source Files (`.cpp`)
- Location: `src/<module>/`
- Include corresponding header first
- Use anonymous namespace for file-local functions

### Example Structure
```cpp
// include/aurore/ring_buffer.hpp
#ifndef AURORE_RING_BUFFER_HPP
#define AURORE_RING_BUFFER_HPP

namespace aurore {

template<typename T, size_t N>
class LockFreeRingBuffer {
public:
    bool push(const T& item);
    bool pop(T& item);
};

} // namespace aurore

#endif
```

## Formatting Rules

### Indentation
- **Spaces:** 4 spaces per indent level
- **No tabs:** Always use spaces

### Line Length
- **Soft limit:** 100 characters
- **Hard limit:** 120 characters (only when necessary)

### Braces
- **K&R style:** Opening brace on same line
```cpp
void function() {
    if (condition) {
        // body
    }
}
```

### Pointers and References
- **Alignment:** Left-aligned with type
```cpp
const T* ptr;    // Not: T const* ptr
T& ref;          // Not: T &ref
```

## Code Guidelines

### Modern C++ Features (C++17)
- Use `std::optional` for nullable returns
- Use `std::variant` for tagged unions
- Use structured bindings where clear
- Use `if constexpr` for compile-time branches

### Memory Management
- **No raw `new`/`delete`:** Use RAII containers
- **Prefer `std::unique_ptr`:** For exclusive ownership
- **Avoid `std::shared_ptr`:** In real-time paths (atomic overhead)
- **No heap allocation:** In real-time threads after init

### Error Handling
- **Return `std::optional<T>`:** For recoverable failures
- **Use `std::expected` (C++23):** When available
- **Assert for invariants:** `assert(ptr != nullptr)`
- **No exceptions:** In real-time code paths

### Concurrency
- **Use `std::atomic`:** With explicit memory ordering
```cpp
std::atomic<bool> ready{false};
ready.store(true, std::memory_order_release);
if (ready.load(std::memory_order_acquire)) { }
```
- **Lock-free preferred:** For inter-thread communication
- **Document thread safety:** In class comments

### Real-Time Constraints
- **No `malloc`/`free`:** After initialization
- **No `memcpy`:** Use zero-copy design
- **Bounded loops only:** No unbounded iteration
- **Precompute at boot:** Runtime = table lookup

## Documentation

### Class Comments
```cpp
/// @brief Lock-free single-producer single-consumer ring buffer.
/// @tparam T Element type (must be trivially copyable)
/// @tparam N Buffer capacity (must be power of 2)
///
/// Thread-safe for SPSC access. No internal locking.
template<typename T, size_t N>
class LockFreeRingBuffer {
```

### Function Comments
```cpp
/// @brief Push item to buffer.
/// @param item Item to push (const reference)
/// @return true if successful, false if buffer full
///
/// @note O(1) time complexity. Thread-safe for single producer.
bool push(const T& item);
```

## Prohibited Patterns

| Pattern | Reason | Alternative |
|---------|--------|-------------|
| `malloc`/`free` | Non-deterministic | `std::vector`, stack allocation |
| `try`/`catch` | Overhead in RT paths | Return codes, `std::optional` |
| `std::mutex` | Blocking, priority inversion | Lock-free structures |
| Unbounded loops | WCET violation | Fixed iteration count |
| Virtual functions | vtable lookup overhead | CRTP, templates |
| RTTI (`dynamic_cast`) | Overhead, non-deterministic | Tagged unions, variants |

## Testing Requirements

- **Unit tests:** For all public APIs
- **Coverage target:** >80% line coverage
- **Test framework:** GoogleTest
- **Test location:** `tests/<module>_test.cpp`

```cpp
// tests/ring_buffer_test.cpp
TEST(RingBufferTest, PushPopSingleElement) {
    LockFreeRingBuffer<int, 4> buffer;
    EXPECT_TRUE(buffer.push(42));
    int value;
    EXPECT_TRUE(buffer.pop(value));
    EXPECT_EQ(value, 42);
}
```
