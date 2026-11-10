#pragma once
// A whole number from a wire. NaN and infinity are not a whole number: callers
// get `fallback` instead of an undefined float-to-int conversion.
#include <cmath>

namespace okay::vs {

inline int VsWhole(float f, int fallback = 0) {
    if (!std::isfinite(f)) return fallback;
    if (f >= 2147483520.0f) return 2147483647;
    if (f <= -2147483520.0f) return -2147483647 - 1;
    return static_cast<int>(std::lround(f));
}

} // namespace okay::vs
