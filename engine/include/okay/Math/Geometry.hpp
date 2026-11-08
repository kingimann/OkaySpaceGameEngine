#pragma once
// Shared 3D geometry: Plane, Sphere, Capsule, OBB, ViewFrustum and the intersection /
// closest-point routines between them and Ray / Bounds (the engine's AABB).
//
// Conventions
//  * Ray hit routines return a true distance `t` (the ray direction is normalised
//    internally); an origin already inside a solid shape returns t = 0.
//  * Overlap tests are inclusive (touching counts) and never allocate.
//  * Everything is header-only and free of OS headers.
#include "okay/Math/Mathf.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Math/Ray.hpp"
#include "okay/Math/Bounds.hpp"

namespace okay {

/// Infinite plane: dot(normal, p) + d == 0. `normal` is unit length.
struct Plane {
    Vec3  normal{0.0f, 1.0f, 0.0f};
    float d = 0.0f;

    Plane() = default;
    Plane(const Vec3& n, float d) { float l = n.Magnitude(); if (l > 1e-12f) { normal = n / l; this->d = d / l; } }
    static Plane FromPointNormal(const Vec3& point, const Vec3& n) {
        Vec3 u = n.Normalized(); if (u.SqrMagnitude() < 0.5f) u = Vec3{0, 1, 0};
        Plane p; p.normal = u; p.d = -Vec3::Dot(u, point); return p;
    }
    /// Plane through three points (counter-clockwise a,b,c faces the normal side).
    static Plane FromPoints(const Vec3& a, const Vec3& b, const Vec3& c) {
        return FromPointNormal(a, Vec3::Cross(b - a, c - a));
    }
    /// Positive on the normal side.
    float SignedDistance(const Vec3& p) const { return Vec3::Dot(normal, p) + d; }
    Vec3 ClosestPoint(const Vec3& p) const { return p - normal * SignedDistance(p); }
    bool SameSide(const Vec3& a, const Vec3& b) const { return (SignedDistance(a) >= 0) == (SignedDistance(b) >= 0); }
    void Flip() { normal = -normal; d = -d; }
    /// Ray hit (front or back). A ray lying in the plane or parallel to it misses.
    bool Raycast(const Ray& ray, float& t) const {
        Vec3 dir = ray.direction.Normalized();
        float denom = Vec3::Dot(normal, dir);
        if (Mathf::Abs(denom) < 1e-9f) return false;
        float h = -SignedDistance(ray.origin) / denom;
        if (h < 0.0f) return false;
        t = h; return true;
    }
};

struct Sphere {
    Vec3  center;
    float radius = 0.5f;
    Sphere() = default;
    Sphere(const Vec3& c, float r) : center(c), radius(r) {}
    bool Contains(const Vec3& p) const { return (p - center).SqrMagnitude() <= radius * radius; }
    Bounds GetBounds() const { return Bounds(center, Vec3{radius * 2, radius * 2, radius * 2}); }
};

/// Segment a-b thickened by `radius`.
struct Capsule {
    Vec3  a, b;
    float radius = 0.5f;
    Capsule() = default;
    Capsule(const Vec3& a, const Vec3& b, float r) : a(a), b(b), radius(r) {}
    Bounds GetBounds() const {
        Bounds r; Vec3 e{radius, radius, radius};
        r.Encapsulate(a - e); r.Encapsulate(a + e); r.Encapsulate(b - e); r.Encapsulate(b + e);
        return r;
    }
};

/// Oriented box: `extents` are half-sizes along the rotated local axes.
struct OBB {
    Vec3 center;
    Vec3 extents{0.5f, 0.5f, 0.5f};
    Quat rotation;

    OBB() = default;
    OBB(const Vec3& c, const Vec3& e, const Quat& r) : center(c), extents(e), rotation(r.Normalized()) {}
    static OBB FromBounds(const Bounds& b) { return OBB(b.center, Vec3::Abs(b.extents), Quat{}); }
    Vec3 Axis(int i) const { return rotation * (i == 0 ? Vec3{1, 0, 0} : i == 1 ? Vec3{0, 1, 0} : Vec3{0, 0, 1}); }
    Vec3 ToLocal(const Vec3& p) const { return rotation.Conjugate() * (p - center); }
    Vec3 ToWorld(const Vec3& p) const { return rotation * p + center; }
    bool Contains(const Vec3& p) const {
        Vec3 l = ToLocal(p);
        return Mathf::Abs(l.x) <= extents.x && Mathf::Abs(l.y) <= extents.y && Mathf::Abs(l.z) <= extents.z;
    }
    Vec3 Corner(int i) const {
        return ToWorld({(i & 1) ? extents.x : -extents.x, (i & 2) ? extents.y : -extents.y, (i & 4) ? extents.z : -extents.z});
    }
    /// World AABB enclosing this box.
    Bounds GetBounds() const {
        Bounds r; for (int i = 0; i < 8; ++i) r.Encapsulate(Corner(i)); return r;
    }
};

/// Six inward-facing planes extracted from a view-projection matrix (clip = vp * p,
/// column-major; works for OpenGL-style -1..1 and 0..1 depth since the near plane is
/// taken from the -w..w form, which is the looser of the two). Overlap tests are
/// conservative: they never reject something visible, but may keep a box that only
/// grazes a frustum corner.
struct ViewFrustum {
    enum { Left, Right, Bottom, Top, Near, Far };
    Plane planes[6];

    static ViewFrustum FromViewProjection(const Mat4& vp) {
        ViewFrustum f;
        auto row = [&](int r) { return Vec4{vp.m[r], vp.m[4 + r], vp.m[8 + r], vp.m[12 + r]}; };
        Vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
        Vec4 v[6] = {r3 + r0, r3 - r0, r3 + r1, r3 - r1, r3 + r2, r3 - r2};
        for (int i = 0; i < 6; ++i) f.planes[i] = Plane(Vec3{v[i].x, v[i].y, v[i].z}, v[i].w);
        return f;
    }
    bool Contains(const Vec3& p) const {
        for (const Plane& pl : planes) if (pl.SignedDistance(p) < 0.0f) return false;
        return true;
    }
    bool Intersects(const Sphere& s) const {
        for (const Plane& pl : planes) if (pl.SignedDistance(s.center) < -s.radius) return false;
        return true;
    }
    bool Intersects(const Bounds& b) const {
        if (b.IsEmpty()) return false;
        for (const Plane& pl : planes) {
            float r = b.extents.x * Mathf::Abs(pl.normal.x) + b.extents.y * Mathf::Abs(pl.normal.y) + b.extents.z * Mathf::Abs(pl.normal.z);
            if (pl.SignedDistance(b.center) < -r) return false;
        }
        return true;
    }
    bool Intersects(const OBB& o) const {
        Vec3 ax[3] = {o.Axis(0), o.Axis(1), o.Axis(2)};
        for (const Plane& pl : planes) {
            float r = o.extents.x * Mathf::Abs(Vec3::Dot(pl.normal, ax[0])) +
                      o.extents.y * Mathf::Abs(Vec3::Dot(pl.normal, ax[1])) +
                      o.extents.z * Mathf::Abs(Vec3::Dot(pl.normal, ax[2]));
            if (pl.SignedDistance(o.center) < -r) return false;
        }
        return true;
    }
    bool Intersects(const Capsule& c) const {
        // Conservative: test the capsule's bounding sphere pair via both end spheres.
        Vec3 mid = (c.a + c.b) * 0.5f;
        float rad = (c.b - c.a).Magnitude() * 0.5f + c.radius;
        return Intersects(Sphere(mid, rad));
    }
};

/// Closest-point and intersection routines (statics so they can be found by name).
struct Geom {
    // ---- closest points ----
    static Vec3 ClosestPointOnSegment(const Vec3& p, const Vec3& a, const Vec3& b) {
        Vec3 ab = b - a; float l2 = ab.SqrMagnitude();
        if (l2 < 1e-12f) return a;
        return a + ab * Mathf::Clamp01(Vec3::Dot(p - a, ab) / l2);
    }
    static Vec3 ClosestPoint(const Sphere& s, const Vec3& p) {       // on/in the solid sphere
        Vec3 d = p - s.center; float l = d.Magnitude();
        return l <= s.radius || l < 1e-12f ? p : s.center + d * (s.radius / l);
    }
    static Vec3 ClosestPoint(const Capsule& c, const Vec3& p) {      // on/in the solid capsule
        Vec3 q = ClosestPointOnSegment(p, c.a, c.b);
        Vec3 d = p - q; float l = d.Magnitude();
        return l <= c.radius || l < 1e-12f ? p : q + d * (c.radius / l);
    }
    static Vec3 ClosestPoint(const OBB& o, const Vec3& p) {          // on/in the solid box
        Vec3 l = o.ToLocal(p);
        l = {Mathf::Clamp(l.x, -o.extents.x, o.extents.x), Mathf::Clamp(l.y, -o.extents.y, o.extents.y),
             Mathf::Clamp(l.z, -o.extents.z, o.extents.z)};
        return o.ToWorld(l);
    }
    static Vec3 ClosestPoint(const Bounds& b, const Vec3& p) { return b.ClosestPoint(p); }
    static Vec3 ClosestPoint(const Plane& pl, const Vec3& p) { return pl.ClosestPoint(p); }

    /// Squared distance between two segments, with the closest points.
    static float SegmentSegment(const Vec3& p1, const Vec3& q1, const Vec3& p2, const Vec3& q2,
                                Vec3& c1, Vec3& c2) {
        Vec3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
        float a = d1.SqrMagnitude(), e = d2.SqrMagnitude(), f = Vec3::Dot(d2, r);
        float s, t;
        const float eps = 1e-12f;
        if (a <= eps && e <= eps) { s = t = 0; }
        else if (a <= eps) { s = 0; t = Mathf::Clamp01(f / e); }
        else {
            float c = Vec3::Dot(d1, r);
            if (e <= eps) { t = 0; s = Mathf::Clamp01(-c / a); }
            else {
                float b = Vec3::Dot(d1, d2), denom = a * e - b * b;
                s = denom > eps * a * e ? Mathf::Clamp01((b * f - c * e) / denom) : 0.0f;
                t = (b * s + f) / e;
                if (t < 0.0f) { t = 0.0f; s = Mathf::Clamp01(-c / a); }
                else if (t > 1.0f) { t = 1.0f; s = Mathf::Clamp01((b - c) / a); }
            }
        }
        c1 = p1 + d1 * s; c2 = p2 + d2 * t;
        return (c1 - c2).SqrMagnitude();
    }

    // ---- ray casts ----
    static bool Raycast(const Ray& ray, const Plane& p, float& t) { return p.Raycast(ray, t); }
    static bool Raycast(const Ray& ray, const Bounds& b, float& t, float maxDistance = Mathf::Infinity) {
        return b.Raycast(ray, t, maxDistance);
    }
    static bool Raycast(const Ray& ray, const Sphere& s, float& t, float maxDistance = Mathf::Infinity) {
        Vec3 d = ray.direction.Normalized();
        if (d.SqrMagnitude() < 0.5f) return false;
        Vec3 oc = ray.origin - s.center;
        float c = oc.SqrMagnitude() - s.radius * s.radius;
        if (c <= 0.0f) { t = 0.0f; return true; }            // starts inside
        float b = Vec3::Dot(oc, d);
        if (b > 0.0f) return false;                           // pointing away
        float disc = b * b - c;
        if (disc < 0.0f) return false;
        float h = -b - Mathf::Sqrt(disc);
        if (h > maxDistance) return false;
        t = h; return true;
    }
    static bool Raycast(const Ray& ray, const OBB& o, float& t, float maxDistance = Mathf::Infinity) {
        Quat inv = o.rotation.Conjugate();
        Ray local;
        local.origin = inv * (ray.origin - o.center);
        local.direction = inv * ray.direction.Normalized();   // rotation keeps it unit
        Bounds b; b.center = Vec3{}; b.extents = Vec3::Abs(o.extents);
        return b.Raycast(local, t, maxDistance);
    }
    static bool Raycast(const Ray& ray, const Capsule& c, float& t, float maxDistance = Mathf::Infinity) {
        Vec3 d = ray.direction.Normalized();
        if (d.SqrMagnitude() < 0.5f) return false;
        if ((ray.origin - ClosestPointOnSegment(ray.origin, c.a, c.b)).SqrMagnitude() <= c.radius * c.radius) { t = 0.0f; return true; }
        float best = Mathf::Infinity;
        Vec3 ba = c.b - c.a, oa = ray.origin - c.a;
        float baba = ba.SqrMagnitude(), bard = Vec3::Dot(ba, d), baoa = Vec3::Dot(ba, oa);
        float k2 = baba - bard * bard;
        if (baba > 1e-12f && Mathf::Abs(k2) > 1e-9f * baba) {
            float k1 = baba * Vec3::Dot(oa, d) - baoa * bard;
            float k0 = baba * oa.SqrMagnitude() - baoa * baoa - c.radius * c.radius * baba;
            float h = k1 * k1 - k2 * k0;
            if (h < 0.0f) return false;                       // misses the infinite tube => misses all
            float th = (-k1 - Mathf::Sqrt(h)) / k2;
            float y = baoa + th * bard;
            if (th >= 0.0f && y > 0.0f && y < baba) best = th;
        }
        float ts;
        if (Raycast(ray, Sphere(c.a, c.radius), ts, maxDistance) && ts < best) best = ts;
        if (Raycast(ray, Sphere(c.b, c.radius), ts, maxDistance) && ts < best) best = ts;
        if (best > maxDistance || best == Mathf::Infinity) return false;
        t = best; return true;
    }

    // ---- overlap tests (inclusive) ----
    static bool Overlap(const Sphere& a, const Sphere& b) {
        float r = a.radius + b.radius; return (a.center - b.center).SqrMagnitude() <= r * r;
    }
    static bool Overlap(const Sphere& s, const Bounds& b) {
        return !b.IsEmpty() && b.SqrDistance(s.center) <= s.radius * s.radius;
    }
    static bool Overlap(const Bounds& b, const Sphere& s) { return Overlap(s, b); }
    static bool Overlap(const Sphere& s, const OBB& o) {
        return (ClosestPoint(o, s.center) - s.center).SqrMagnitude() <= s.radius * s.radius;
    }
    static bool Overlap(const OBB& o, const Sphere& s) { return Overlap(s, o); }
    static bool Overlap(const Bounds& a, const Bounds& b) { return a.Intersects(b); }
    static bool Overlap(const Sphere& s, const Capsule& c) {
        float r = s.radius + c.radius;
        return (ClosestPointOnSegment(s.center, c.a, c.b) - s.center).SqrMagnitude() <= r * r;
    }
    static bool Overlap(const Capsule& c, const Sphere& s) { return Overlap(s, c); }
    static bool Overlap(const Capsule& a, const Capsule& b) {
        Vec3 c1, c2; float r = a.radius + b.radius;
        return SegmentSegment(a.a, a.b, b.a, b.b, c1, c2) <= r * r;
    }
    /// Separating-axis test over the 15 candidate axes.
    static bool Overlap(const OBB& a, const OBB& b) {
        Vec3 A[3] = {a.Axis(0), a.Axis(1), a.Axis(2)}, B[3] = {b.Axis(0), b.Axis(1), b.Axis(2)};
        Vec3 T = b.center - a.center;
        auto separated = [&](const Vec3& axis) {
            if (axis.SqrMagnitude() < 1e-10f) return false;  // parallel edges: no information
            Vec3 n = axis.Normalized();
            float ra = 0, rb = 0;
            for (int i = 0; i < 3; ++i) { ra += a.extents[i] * Mathf::Abs(Vec3::Dot(n, A[i])); rb += b.extents[i] * Mathf::Abs(Vec3::Dot(n, B[i])); }
            return Mathf::Abs(Vec3::Dot(T, n)) > ra + rb;
        };
        for (int i = 0; i < 3; ++i) if (separated(A[i]) || separated(B[i])) return false;
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) if (separated(Vec3::Cross(A[i], B[j]))) return false;
        return true;
    }
    static bool Overlap(const Bounds& a, const OBB& b) { return !a.IsEmpty() && Overlap(OBB::FromBounds(a), b); }
    static bool Overlap(const OBB& a, const Bounds& b) { return Overlap(b, a); }
    /// Which side of the plane a sphere is on: 1 in front, -1 behind, 0 straddling.
    static int Classify(const Plane& p, const Sphere& s) {
        float d = p.SignedDistance(s.center);
        return d > s.radius ? 1 : (d < -s.radius ? -1 : 0);
    }
};

} // namespace okay
