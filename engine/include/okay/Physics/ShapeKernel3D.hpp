#pragma once
// The maths of one convex 3D shape in its own local space: support point (the furthest point along a direction),
// volume, turning inertia, local bounds and an exact ray test. Collider components wrap one of these and add the
// world transform. Written from general convex-geometry definitions (support functions, solid-body inertia of
// primitives, quadric ray tests); no code from any other engine.
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Mathf.hpp"
#include <cmath>
#include <vector>

namespace okay {

struct LocalShape3D {
    enum class Kind { Box, Sphere, Capsule, Cylinder, Cone, Hull };
    Kind kind = Kind::Box;
    Vec3 half{0.5f, 0.5f, 0.5f};       ///< Box: half sizes
    float radius = 0.5f;               ///< Sphere / Capsule / Cylinder / Cone (base)
    float height = 1.0f;               ///< Capsule: tip to tip. Cylinder / Cone: full height
    int axis = 1;                      ///< long axis (0 = X, 1 = Y, 2 = Z); a cone's tip points to +axis
    std::vector<Vec3> pts;             ///< Hull: the points
    std::vector<int> tris;             ///< Hull: triangles (3 indices each, outward-facing) for rays and inertia

    Vec3 Axis() const { return axis == 0 ? Vec3{1, 0, 0} : axis == 2 ? Vec3{0, 0, 1} : Vec3{0, 1, 0}; }

    /// The furthest point of the shape along `d` (any length, not zero-checked beyond a fallback).
    Vec3 Support(const Vec3& d) const {
        switch (kind) {
        case Kind::Box:
            return {d.x >= 0 ? half.x : -half.x, d.y >= 0 ? half.y : -half.y, d.z >= 0 ? half.z : -half.z};
        case Kind::Sphere: return Unit(d) * radius;
        case Kind::Capsule: {
            Vec3 u = Axis();
            float seg = Mathf::Max(0.0f, height * 0.5f - radius);
            return u * (Vec3::Dot(d, u) >= 0 ? seg : -seg) + Unit(d) * radius;
        }
        case Kind::Cylinder: {
            Vec3 u = Axis();
            float hh = height * 0.5f;
            return u * (Vec3::Dot(d, u) >= 0 ? hh : -hh) + Perp(d) * radius;
        }
        case Kind::Cone: {
            Vec3 u = Axis();
            float hh = height * 0.5f;
            Vec3 tip = u * hh;
            Vec3 rim = u * -hh + Perp(d) * radius;
            return Vec3::Dot(tip, d) >= Vec3::Dot(rim, d) ? tip : rim;
        }
        case Kind::Hull: {
            if (pts.empty()) return Vec3::Zero;
            const Vec3* best = &pts[0]; float bd = Vec3::Dot(pts[0], d);
            for (std::size_t i = 1; i < pts.size(); ++i) {
                float v = Vec3::Dot(pts[i], d);
                if (v > bd) { bd = v; best = &pts[i]; }
            }
            return *best;
        }
        }
        return Vec3::Zero;
    }

    /// Local bounds.
    void Bounds(Vec3& lo, Vec3& hi) const {
        lo = {Support({-1, 0, 0}).x, Support({0, -1, 0}).y, Support({0, 0, -1}).z};
        hi = {Support({1, 0, 0}).x, Support({0, 1, 0}).y, Support({0, 0, 1}).z};
    }

    /// Where the centre of mass sits relative to the shape's own origin (a cone's is a quarter of the way up from its base).
    Vec3 CenterOfMassOffset() const {
        if (kind == Kind::Cone) return Axis() * (-height * 0.25f);
        if (kind == Kind::Hull) { Vec3 c, d; float v; HullMass(c, v, d); return c; }
        return Vec3::Zero;
    }

    /// Volume of the solid shape.
    float Volume() const {
        const float kPi = 3.14159265f;
        switch (kind) {
        case Kind::Box: return 8.0f * half.x * half.y * half.z;
        case Kind::Sphere: return 4.0f / 3.0f * kPi * radius * radius * radius;
        case Kind::Capsule: {
            float L = Mathf::Max(0.0f, height - 2.0f * radius);
            return kPi * radius * radius * L + 4.0f / 3.0f * kPi * radius * radius * radius;
        }
        case Kind::Cylinder: return kPi * radius * radius * height;
        case Kind::Cone: return kPi * radius * radius * height / 3.0f;
        case Kind::Hull: { Vec3 c, d; float v; HullMass(c, v, d); return v; }
        }
        return 0.0f;
    }

    /// Turning inertia about the centre of mass for a body of mass 1, as the three diagonal terms in the shape's own axes.
    Vec3 UnitInertia() const {
        switch (kind) {
        case Kind::Box:
            return {(half.y * half.y + half.z * half.z) / 3.0f, (half.x * half.x + half.z * half.z) / 3.0f,
                    (half.x * half.x + half.y * half.y) / 3.0f};
        case Kind::Sphere: { float i = 0.4f * radius * radius; return {i, i, i}; }
        case Kind::Capsule: {
            const float kPi = 3.14159265f;
            float r = radius, L = Mathf::Max(0.0f, height - 2.0f * r);
            float vc = kPi * r * r * L, vh = 4.0f / 3.0f * kPi * r * r * r, v = vc + vh;
            if (v <= 0) return {0, 0, 0};
            float fc = vc / v, fh = vh / v, dd = 0.5f * L;
            float iA = fc * 0.5f * r * r + fh * 0.4f * r * r;
            float iT = fc * (3.0f * r * r + L * L) / 12.0f + fh * (0.4f * r * r + dd * dd + 0.75f * r * dd);
            return OnAxis(iA, iT);
        }
        case Kind::Cylinder:
            return OnAxis(0.5f * radius * radius, (3.0f * radius * radius + height * height) / 12.0f);
        case Kind::Cone:
            return OnAxis(0.3f * radius * radius, 0.15f * radius * radius + 0.0375f * height * height);
        case Kind::Hull: { Vec3 c, d; float v; HullMass(c, v, d); return d; }
        }
        return {0, 0, 0};
    }

    /// Exact ray test in local space. `d` need not be unit; t is in units of d. n is the outward surface normal.
    bool Ray(const Vec3& o, const Vec3& d, float maxT, float& t, Vec3& n) const {
        switch (kind) {
        case Kind::Box: return RayBox(o, d, maxT, t, n);
        case Kind::Sphere: return RaySphere(o, d, Vec3::Zero, radius, maxT, t, n);
        case Kind::Capsule: return RayCapsuleShape(o, d, maxT, t, n);
        case Kind::Cylinder: return RayCylinder(o, d, maxT, t, n);
        case Kind::Cone: return RayCone(o, d, maxT, t, n);
        case Kind::Hull: return RayHull(o, d, maxT, t, n);
        }
        return false;
    }

private:
    static Vec3 Unit(const Vec3& d) {
        float m = d.Magnitude();
        return m > 1e-12f ? d * (1.0f / m) : Vec3{0, 1, 0};
    }
    /// The unit direction of `d` with its axis part removed (zero when d lies along the axis).
    Vec3 Perp(const Vec3& d) const {
        Vec3 u = Axis();
        Vec3 p = d - u * Vec3::Dot(d, u);
        float m = p.Magnitude();
        return m > 1e-9f ? p * (1.0f / m) : Vec3::Zero;
    }
    Vec3 OnAxis(float iAxis, float iTrans) const {
        return {axis == 0 ? iAxis : iTrans, axis == 1 ? iAxis : iTrans, axis == 2 ? iAxis : iTrans};
    }

    // Solid hull: volume, centre of mass and diagonal inertia (unit mass) from tetrahedra to the point average.
    void HullMass(Vec3& com, float& vol, Vec3& diag) const {
        com = Vec3::Zero; vol = 0.0f; diag = {0, 0, 0};
        if (pts.empty() || tris.size() < 12) {
            if (pts.empty()) return;
            Vec3 lo = pts[0], hi = pts[0];
            for (const Vec3& p : pts) { lo = Vec3::Min(lo, p); hi = Vec3::Max(hi, p); }
            Vec3 h = (hi - lo) * 0.5f; com = (hi + lo) * 0.5f; vol = 8.0f * h.x * h.y * h.z;
            diag = {(h.y * h.y + h.z * h.z) / 3.0f, (h.x * h.x + h.z * h.z) / 3.0f, (h.x * h.x + h.y * h.y) / 3.0f};
            return;
        }
        Vec3 ref = Vec3::Zero;
        for (const Vec3& p : pts) ref = ref + p;
        ref = ref * (1.0f / (float)pts.size());
        double cov[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};   // second moments about `ref`
        double V = 0, cx = 0, cy = 0, cz = 0;
        for (std::size_t i = 0; i + 2 < tris.size(); i += 3) {
            Vec3 a = pts[(std::size_t)tris[i]] - ref, b = pts[(std::size_t)tris[i + 1]] - ref, c = pts[(std::size_t)tris[i + 2]] - ref;
            double v = (double)Vec3::Dot(a, Vec3::Cross(b, c)) / 6.0;          // signed volume of the tetra (ref, a, b, c)
            V += v;
            cx += v * 0.25 * (a.x + b.x + c.x); cy += v * 0.25 * (a.y + b.y + c.y); cz += v * 0.25 * (a.z + b.z + c.z);
            const Vec3 P[3] = {a, b, c};
            // Second moment of a tetra with one corner at the origin: V/20 * (sum v_i v_i^T + (sum v_i)(sum v_i)^T).
            Vec3 s = a + b + c;
            auto at = [&](const Vec3& q, int k) { return k == 0 ? q.x : k == 1 ? q.y : q.z; };
            for (int r = 0; r < 3; ++r) for (int q = 0; q < 3; ++q) {
                double sum = 0;
                for (int k = 0; k < 3; ++k) sum += (double)at(P[k], r) * at(P[k], q);
                sum += (double)at(s, r) * at(s, q);
                cov[r][q] += v / 20.0 * sum;
            }
        }
        if (V < 1e-12) { vol = 0; return; }
        double mx = cx / V, my = cy / V, mz = cz / V;                           // centre of mass about `ref`
        double m[3] = {mx, my, mz};
        for (int r = 0; r < 3; ++r) for (int q = 0; q < 3; ++q) cov[r][q] = cov[r][q] / V - m[r] * m[q];   // about the COM, per unit mass
        double tr = cov[0][0] + cov[1][1] + cov[2][2];
        diag = {(float)(tr - cov[0][0]), (float)(tr - cov[1][1]), (float)(tr - cov[2][2])};
        com = ref + Vec3{(float)mx, (float)my, (float)mz};
        vol = (float)V;
    }

    bool RayBox(const Vec3& o, const Vec3& d, float maxT, float& t, Vec3& n) const {
        float t0 = 0.0f, t1 = maxT; Vec3 nEnter{0, 0, 0};
        const float oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z}, hh[3] = {half.x, half.y, half.z};
        for (int i = 0; i < 3; ++i) {
            if (Mathf::Abs(dd[i]) < 1e-12f) { if (oo[i] < -hh[i] || oo[i] > hh[i]) return false; continue; }
            float a = (-hh[i] - oo[i]) / dd[i], b = (hh[i] - oo[i]) / dd[i];
            float sgn = -1.0f;
            if (a > b) { std::swap(a, b); sgn = 1.0f; }
            if (a > t0) { t0 = a; nEnter = {0, 0, 0}; (i == 0 ? nEnter.x : i == 1 ? nEnter.y : nEnter.z) = sgn; }
            if (b < t1) t1 = b;
            if (t0 > t1) return false;
        }
        if (Mathf::Abs(nEnter.x) + Mathf::Abs(nEnter.y) + Mathf::Abs(nEnter.z) < 0.5f) return false;   // started inside
        t = t0; n = nEnter; return true;
    }
    static bool RaySphere(const Vec3& o, const Vec3& d, const Vec3& c, float r, float maxT, float& t, Vec3& n) {
        Vec3 m = o - c;
        float a = Vec3::Dot(d, d), b = Vec3::Dot(m, d), cc = Vec3::Dot(m, m) - r * r;
        if (a < 1e-20f || cc <= 0.0f) return false;
        float disc = b * b - a * cc;
        if (disc < 0.0f) return false;
        float s = (-b - Mathf::Sqrt(disc)) / a;
        if (s < 0.0f || s > maxT) return false;
        t = s; n = (m + d * s).Normalized(); return true;
    }
    bool RayCapsuleShape(const Vec3& o, const Vec3& d, float maxT, float& t, Vec3& n) const {
        Vec3 u = Axis();
        float seg = Mathf::Max(0.0f, height * 0.5f - radius);
        float bestT = 1e30f; Vec3 bestN{0, 1, 0}; bool hit = false;
        // The straight tube between the two caps.
        float oa = Vec3::Dot(o, u), da = Vec3::Dot(d, u);
        Vec3 op = o - u * oa, dp = d - u * da;
        float A = Vec3::Dot(dp, dp), B = Vec3::Dot(op, dp), C = Vec3::Dot(op, op) - radius * radius;
        if (A > 1e-20f && C > 0.0f) {
            float disc = B * B - A * C;
            if (disc >= 0.0f) {
                float s = (-B - Mathf::Sqrt(disc)) / A;
                float al = oa + da * s;
                if (s >= 0.0f && s <= maxT && Mathf::Abs(al) <= seg) { hit = true; bestT = s; bestN = (op + dp * s).Normalized(); }
            }
        }
        for (int e = 0; e < 2; ++e) {                      // the two end caps
            float tt; Vec3 nn;
            if (RaySphere(o, d, u * (e ? seg : -seg), radius, maxT, tt, nn) && tt < bestT) {
                if (Vec3::Dot(nn, u) * (e ? 1.0f : -1.0f) >= 0.0f) { hit = true; bestT = tt; bestN = nn; }
            }
        }
        if (!hit) return false;
        t = bestT; n = bestN; return true;
    }
    bool RayCylinder(const Vec3& o, const Vec3& d, float maxT, float& t, Vec3& n) const {
        Vec3 u = Axis(); float hh = height * 0.5f;
        float oa = Vec3::Dot(o, u), da = Vec3::Dot(d, u);
        Vec3 op = o - u * oa, dp = d - u * da;
        float bestT = 1e30f; Vec3 bestN{0, 1, 0}; bool hit = false;
        if (Vec3::Dot(op, op) <= radius * radius && Mathf::Abs(oa) <= hh) return false;   // starts inside
        float A = Vec3::Dot(dp, dp), B = Vec3::Dot(op, dp), C = Vec3::Dot(op, op) - radius * radius;
        if (A > 1e-20f && C > 0.0f) {
            float disc = B * B - A * C;
            if (disc >= 0.0f) {
                float s = (-B - Mathf::Sqrt(disc)) / A;
                float al = oa + da * s;
                if (s >= 0.0f && s <= maxT && Mathf::Abs(al) <= hh) { hit = true; bestT = s; bestN = (op + dp * s).Normalized(); }
            }
        }
        if (Mathf::Abs(da) > 1e-12f) {
            for (int e = 0; e < 2; ++e) {
                float plane = e ? hh : -hh;
                float s = (plane - oa) / da;
                if (s < 0.0f || s > maxT || s >= bestT) continue;
                Vec3 q = op + dp * s;
                if (Vec3::Dot(q, q) <= radius * radius) { hit = true; bestT = s; bestN = u * (e ? 1.0f : -1.0f); }
            }
        }
        if (!hit) return false;
        t = bestT; n = bestN; return true;
    }
    bool RayCone(const Vec3& o, const Vec3& d, float maxT, float& t, Vec3& n) const {
        Vec3 u = Axis(); float hh = height * 0.5f;
        float k = hh > 1e-9f ? radius / height : 0.0f;                 // radius shrinks by k per unit towards the tip
        float oa = Vec3::Dot(o, u), da = Vec3::Dot(d, u);
        Vec3 op = o - u * oa, dp = d - u * da;
        auto inside = [&]() {
            if (oa < -hh || oa > hh) return false;
            float rr = k * (hh - oa);
            return Vec3::Dot(op, op) <= rr * rr;
        };
        if (inside()) return false;
        float bestT = 1e30f; Vec3 bestN{0, 1, 0}; bool hit = false;
        // Side: |p_perp|^2 = k^2 (hh - a)^2 with a = oa + da s.
        float g0 = hh - oa;
        float A = Vec3::Dot(dp, dp) - k * k * da * da;
        float B = Vec3::Dot(op, dp) + k * k * g0 * da;
        float C = Vec3::Dot(op, op) - k * k * g0 * g0;
        auto trySide = [&](float s) {
            if (s < 0.0f || s > maxT || s >= bestT) return;
            float al = oa + da * s;
            if (al < -hh || al > hh) return;
            Vec3 q = op + dp * s;
            Vec3 nn = q + u * (k * k * (hh - al));
            float m = nn.Magnitude();
            if (m < 1e-12f) return;
            hit = true; bestT = s; bestN = nn * (1.0f / m);
        };
        if (Mathf::Abs(A) > 1e-12f) {
            float disc = B * B - A * C;
            if (disc >= 0.0f) {
                float sq = Mathf::Sqrt(disc);
                float s1 = (-B - sq) / A, s2 = (-B + sq) / A;
                if (s1 > s2) std::swap(s1, s2);
                trySide(s1); if (!hit) trySide(s2);
            }
        } else if (Mathf::Abs(B) > 1e-12f) trySide(-C / (2.0f * B));
        if (Mathf::Abs(da) > 1e-12f) {                                  // the flat base
            float s = (-hh - oa) / da;
            if (s >= 0.0f && s <= maxT && s < bestT) {
                Vec3 q = op + dp * s;
                if (Vec3::Dot(q, q) <= radius * radius) { hit = true; bestT = s; bestN = u * -1.0f; }
            }
        }
        if (!hit) return false;
        t = bestT; n = bestN; return true;
    }
    bool RayHull(const Vec3& o, const Vec3& d, float maxT, float& t, Vec3& n) const {
        bool hit = false; float bestT = maxT; Vec3 bestN{0, 1, 0};
        for (std::size_t i = 0; i + 2 < tris.size(); i += 3) {
            const Vec3& a = pts[(std::size_t)tris[i]]; const Vec3& b = pts[(std::size_t)tris[i + 1]]; const Vec3& c = pts[(std::size_t)tris[i + 2]];
            Vec3 e1 = b - a, e2 = c - a;
            Vec3 pv = Vec3::Cross(d, e2);
            float det = Vec3::Dot(e1, pv);
            if (Mathf::Abs(det) < 1e-12f) continue;
            float inv = 1.0f / det;
            Vec3 tv = o - a;
            float u = Vec3::Dot(tv, pv) * inv;
            if (u < 0.0f || u > 1.0f) continue;
            Vec3 qv = Vec3::Cross(tv, e1);
            float v = Vec3::Dot(d, qv) * inv;
            if (v < 0.0f || u + v > 1.0f) continue;
            float s = Vec3::Dot(e2, qv) * inv;
            if (s < 0.0f || s > bestT) continue;
            Vec3 nn = Vec3::Cross(e1, e2);
            if (Vec3::Dot(nn, d) > 0.0f) continue;               // a back face: the ray is leaving or started inside
            hit = true; bestT = s; bestN = nn.Normalized();
        }
        if (!hit) return false;
        t = bestT; n = bestN; return true;
    }
};

} // namespace okay
