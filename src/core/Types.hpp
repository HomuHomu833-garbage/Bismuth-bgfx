#pragma once

/*
    Basic types shared by all Bismuth code. This header (and everything under
    src/core, src/gfx and the GD-independent parts of src/renderer) must not
    depend on Geode, so that it can be built and tested outside the game.
*/

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cstddef>
#include <memory>
#include <glm/glm.hpp>

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using i8  = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

using usize = uintptr_t;
using isize = intptr_t;

template <typename T>
using UPtr = std::unique_ptr<T>;

template <typename T, typename... Args>
inline std::unique_ptr<T> makeUnique(Args&& ...args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

inline usize alignUp(usize value, usize align) {
    usize rem = value % align;
    return rem ? value + (align - rem) : value;
}

namespace bismuth {

/// Mirrors a shader `uvec4` element; every GPU buffer is made of these or of u32.
using UVec4 = glm::uvec4;

inline u32 floatBits(float f) {
    u32 u;
    static_assert(sizeof(u) == sizeof(f));
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

inline float bitsFloat(u32 u) {
    float f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

/// Logging hook, routed to Geode's logger in the mod and to stderr in tests.
enum class LogLevel { Debug, Info, Warn, Error };
void logMessage(LogLevel level, const char* message);

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
inline void logf(LogLevel level, const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    logMessage(level, buffer);
}

} // namespace bismuth
