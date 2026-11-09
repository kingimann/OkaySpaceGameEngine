#pragma once
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Math/Ray.hpp"

namespace okay {

/// Axis-aligned bounding box defined by a center and extents (half-size).
/// Also the engine's AABB (`using AABB = Bounds`).
///
/// A default-constructed Bounds is EMPTY (negative extents): it contains nothing,
/// intersects nothing, and the first Encapsulate() makes it exactly that point or
/// box rather than growing from the origin. Constructing with (center, size),
/// SetMinMax() or assigning non-negative extents makes it a real box.
struct Bounds {
    Vec3 center;
    Vec3 extents{-1.0f, -1.0f, -1.0f}; // half-size; any negative component = empty

    constexpr Bounds() = default;
    Bounds(const Vec3& center, const Vec3& size)
        : center(center), extents(Vec3::Abs(size) * 0.5f) {}

    static Bounds Empty() { return Bounds{}; }
    static Bounds FromMinMax(const Vec3& mn, const Vec3& mx) { Bounds b; b.SetMinMax(mn, mx); return b; }

    bool IsEmpty() const { return extents.x < 0.0f || extents.y < 0.0f || extents.z < 0.0f; }
    /// Make the box empty again.
    void Clear() { *this = Bounds{}; }

    Vec3 Size() const { return IsEmpty() ? Vec3{} : extents * 2.0f; }
    Vec3 Min() const { return center - extents; }
    Vec3 Max() const { return center + extents; }

    void SetMinMax(const Vec3& mn, const Vec3& mx) {
        center  = (mn + mx) * 0.5f;
        extents = Vec3::Abs(mx - mn) * 0.5f;
    }

    bool Contains(const Vec3& p) const {
        if (IsEmpty()) return false;
        Vec3 mn = Min(), mx = Max();
        return p.x >= mn.x && p.x <= mx.x && p.y >= mn.y && p.y <= mx.y &&
               p.z >= mn.z && p.z <= mx.z;
    }
    /// Inclusive: boxes that only touch count as intersecting. Empty never intersects.
    bool Intersects(const Bounds& b) const {
        if (IsEmpty() || b.IsEmpty()) return false;
        Vec3 amn = Min(), amx = Max(), bmn = b.Min(), bmx = b.Max();
        return amn.x <= bmx.x && amx.x >= bmn.x &&
               amn.y <= bmx.y && amx.y >= bmn.y &&
               amn.z <= bmx.z && amx.z >= bmn.z;
    }
    /// Grow to include a point. An empty box becomes that single point.
    void Encapsulate(const Vec3& p) {
        if (IsEmpty()) { center = p; extents = Vec3{}; return; }
        Vec3 mn = Min(), mx = Max();
        mn = {Mathf::Min(mn.x, p.x), Mathf::Min(mn.y, p.y), Mathf::Min(mn.z, p.z)};
        mx = {Mathf::Max(mx.x, p.x), Mathf::Max(mx.y, p.y), Mathf::Max(mx.z, p.z)};
        SetMinMax(mn, mx);
    }
    /// Grow to include another box (an empty argument changes nothing).
    void Encapsulate(const Bounds& b) {
        if (b.IsEmpty()) return;
        Encapsulate(b.Min());
        Encapsulate(b.Max());
    }
    /// Grow by `amount` on every side (total size grows by 2*amount). Shrinking past
    /// zero size makes the box empty. Empty boxes stay empty.
    void Expand(float amount) {
        if (IsEmpty()) return;
        extents = extents + Vec3{amount, amount, amount};
        if (IsEmpty()) Clear();
    }
    void Expand(const Vec3& amount) {   // total size grows by `amount`
        if (IsEmpty()) return;
        extents = extents + amount * 0.5f;
        if (IsEmpty()) Clear();
    }

    /// Nearest point of the box (or its interior) to `p`. Empty: returns `p`.
    Vec3 ClosestPoint(const Vec3& p) const {
        if (IsEmpty()) return p;
        Vec3 mn = Min(), mx = Max();
        return {Mathf::Clamp(p.x, mn.x, mx.x), Mathf::Clamp(p.y, mn.y, mx.y),
                Mathf::Clamp(p.z, mn.z, mx.z)};
    }
    /// Squared distance from `p` to the box (0 inside). Empty: infinity.
    float SqrDistance(const Vec3& p) const {
        if (IsEmpty()) return Mathf::Infinity;
        return (p - ClosestPoint(p)).SqrMagnitude();
    }

    /// Ray vs box (slab test). On a hit `distance` is the entry distance along the
    /// ray (0 when the origin is inside). `maxDistance` limits the search.
    bool Raycast(const Ray& ray, float& distance, float maxDistance = Mathf::Infinity) const {
        if (IsEmpty()) return false;
        Vec3 d = ray.direction.Normalized();
        if (d.SqrMagnitude() < 0.5f) return false;   // zero direction
        Vec3 mn = Min(), mx = Max();
        float tmin = 0.0f, tmax = maxDistance;
        for (int i = 0; i < 3; ++i) {
            float o = ray.origin[i], di = d[i];
            if (Mathf::Abs(di) < 1e-12f) {
                if (o < mn[i] || o > mx[i]) return false;
                continue;
            }
            float t1 = (mn[i] - o) / di, t2 = (mx[i] - o) / di;
            if (t1 > t2) { float t = t1; t1 = t2; t2 = t; }
            if (t1 > tmin) tmin = t1;
            if (t2 < tmax) tmax = t2;
            if (tmin > tmax) return false;
        }
        distance = tmin;
        return true;
    }
    bool Raycast(const Ray& ray) const { float t; return Raycast(ray, t); }

    /// The world-space AABB of this box transformed by `m` (all 8 corners, so it is
    /// conservative under rotation). Empty stays empty.
    Bounds Transform(const Mat4& m) const {
        if (IsEmpty()) return Bounds{};
        Bounds r;
        for (int i = 0; i < 8; ++i) {
            Vec3 corner = center + Vec3{(i & 1) ? extents.x : -extents.x,
                                        (i & 2) ? extents.y : -extents.y,
                                        (i & 4) ? extents.z : -extents.z};
            r.Encapsulate(m.MultiplyPoint(corner));
        }
        return r;
    }
};

using AABB = Bounds;

} // namespace okay
