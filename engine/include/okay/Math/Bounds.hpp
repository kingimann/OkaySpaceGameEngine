#pragma once
#include "okay/Math/Vec3.hpp"

namespace okay {

/// Axis-aligned bounding box defined by a center and extents (half-size),
/// modeled after Unity's `Bounds`.
struct Bounds {
    Vec3 center;
    Vec3 extents; // half-size

    constexpr Bounds() = default;
    Bounds(const Vec3& center, const Vec3& size) : center(center), extents(size * 0.5f) {}

    Vec3 Size() const { return extents * 2.0f; }
    Vec3 Min() const { return center - extents; }
    Vec3 Max() const { return center + extents; }

    void SetMinMax(const Vec3& mn, const Vec3& mx) {
        center  = (mn + mx) * 0.5f;
        extents = (mx - mn) * 0.5f;
    }

    bool Contains(const Vec3& p) const {
        Vec3 mn = Min(), mx = Max();
        return p.x >= mn.x && p.x <= mx.x && p.y >= mn.y && p.y <= mx.y &&
               p.z >= mn.z && p.z <= mx.z;
    }
    bool Intersects(const Bounds& b) const {
        Vec3 amn = Min(), amx = Max(), bmn = b.Min(), bmx = b.Max();
        return amn.x <= bmx.x && amx.x >= bmn.x &&
               amn.y <= bmx.y && amx.y >= bmn.y &&
               amn.z <= bmx.z && amx.z >= bmn.z;
    }
    /// Grow to include a point.
    void Encapsulate(const Vec3& p) {
        Vec3 mn = Min(), mx = Max();
        mn = {Mathf::Min(mn.x, p.x), Mathf::Min(mn.y, p.y), Mathf::Min(mn.z, p.z)};
        mx = {Mathf::Max(mx.x, p.x), Mathf::Max(mx.y, p.y), Mathf::Max(mx.z, p.z)};
        SetMinMax(mn, mx);
    }
};

} // namespace okay
