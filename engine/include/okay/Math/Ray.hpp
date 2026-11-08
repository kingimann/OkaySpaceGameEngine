#pragma once
#include "okay/Math/Vec3.hpp"

namespace okay {

/// A half-line: origin + direction * t, t >= 0. `direction` should be a unit vector;
/// the intersection routines in Geom/Bounds normalise it themselves, so the returned
/// `t` is always a true distance, even for an unnormalised direction.
struct Ray {
    Vec3 origin;
    Vec3 direction{0.0f, 0.0f, 1.0f};

    constexpr Ray() = default;
    Ray(const Vec3& o, const Vec3& d) : origin(o), direction(d.Normalized()) {}

    Vec3 GetPoint(float t) const { return origin + direction * t; }
};

} // namespace okay
