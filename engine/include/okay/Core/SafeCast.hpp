#pragma once
// Float -> integer conversions that never hit undefined behaviour. A plain C++ cast of a value
// outside the target's range (or of NaN) is undefined, and script values, config files and
// network data can all carry such numbers. These saturate instead: out-of-range values clamp to
// the nearest end of the integer range, and NaN reads as 0. In-range values truncate toward zero,
// exactly like the plain cast.
#include <cstddef>
#include <cstdint>
#include <limits>

namespace okay {

inline int SafeInt(double v) {
    if (v != v) return 0;
    if (v >= 2147483648.0) return std::numeric_limits<int>::max();
    if (v <= -2147483649.0) return std::numeric_limits<int>::min();
    return static_cast<int>(v);
}
inline int SafeInt(float v) { return SafeInt(static_cast<double>(v)); }

inline long long SafeInt64(double v) {
    if (v != v) return 0;
    if (v >= 9223372036854775808.0) return std::numeric_limits<long long>::max();
    if (v <= -9223372036854775808.0) return std::numeric_limits<long long>::min();
    return static_cast<long long>(v);
}
inline long long SafeInt64(float v) { return SafeInt64(static_cast<double>(v)); }

/// Negative values and NaN read as 0.
inline std::uint32_t SafeUInt32(double v) {
    if (!(v > 0.0)) return 0;
    if (v >= 4294967296.0) return std::numeric_limits<std::uint32_t>::max();
    return static_cast<std::uint32_t>(v);
}
inline std::uint32_t SafeUInt32(float v) { return SafeUInt32(static_cast<double>(v)); }

/// Negative values and NaN read as 0.
inline std::size_t SafeSize(double v) {
    if (!(v > 0.0)) return 0;
    if (v >= static_cast<double>(std::numeric_limits<std::size_t>::max())) return std::numeric_limits<std::size_t>::max();
    return static_cast<std::size_t>(v);
}
inline std::size_t SafeSize(float v) { return SafeSize(static_cast<double>(v)); }

} // namespace okay
