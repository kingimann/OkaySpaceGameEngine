#pragma once
// The editor's snap increments (Move / Scale in scene units, Rotate in degrees), kept to values that can snap.
// Header-only, no ImGui, no engine: the Snap settings popup, the saved setting and every snapping path use it,
// so they all agree on what a bad value turns into (issue #888, finding 72).
//
// Rules (written for this project):
//  - not a number or infinite: refused, the previous good value stays (infinity would make round(x/inf)*inf = NaN);
//  - zero or negative, or smaller than the minimum: raised to the minimum (snapping stays on, never silently off);
//  - larger than the maximum: lowered to the maximum (a huge step snaps everything to 0).
// `note` says in plain words what was done, for a hint next to the field; it is empty when the value was fine.
#include <cmath>

namespace oked {

struct SnapLimits {
    float min;
    float max;
    float fallback;   ///< used when the previous value is not good either
};
inline constexpr SnapLimits kMoveSnapLimits{0.001f, 1000.0f, 1.0f};
inline constexpr SnapLimits kRotateSnapLimits{0.1f, 180.0f, 15.0f};

struct SnapClean {
    float value = 1.0f;
    bool changed = false;
    const char* note = "";
};

inline bool SnapValueGood(float v, const SnapLimits& lim) {
    return std::isfinite(v) && v >= lim.min && v <= lim.max;
}

/// `typed` is the new value; `previous` is what the setting held before (used when `typed` is refused).
inline SnapClean CleanSnapIncrement(float typed, float previous, const SnapLimits& lim) {
    SnapClean c;
    if (!std::isfinite(typed)) {
        c.value = SnapValueGood(previous, lim) ? previous : lim.fallback;
        c.changed = true;
        c.note = "Not a number: the old snap step was kept.";
    } else if (typed < lim.min) {
        c.value = lim.min;
        c.changed = true;
        c.note = typed <= 0.0f ? "A snap step must be above 0: set to the smallest step." : "Raised to the smallest snap step.";
    } else if (typed > lim.max) {
        c.value = lim.max;
        c.changed = true;
        c.note = "Lowered to the largest snap step.";
    } else {
        c.value = typed;
    }
    return c;
}

/// A step read back from the settings file: a missing, zero, negative or non-finite value means "use the
/// default" (a broken file never leaves snapping off); anything else is clamped into the limits.
inline float LoadSnapIncrement(float saved, const SnapLimits& lim) {
    if (!std::isfinite(saved) || saved <= 0.0f) return lim.fallback;
    return CleanSnapIncrement(saved, lim.fallback, lim).value;
}

/// The step to snap with right now: always finite and inside the limits, whatever the setting holds.
inline float SnapStep(float setting, const SnapLimits& lim) {
    return CleanSnapIncrement(setting, lim.fallback, lim).value;
}

/// Round `v` to the nearest multiple of the (cleaned) step.
inline float SnapToStep(float v, float setting, const SnapLimits& lim) {
    const float s = SnapStep(setting, lim);
    return std::round(v / s) * s;
}

} // namespace oked
