#pragma once
// ---------------------------------------------------------------------------
// The small math kit of our own physics core (okay::phys): 3-vectors, unit quaternions, 3x3 matrices and rigid
// transforms. The core keeps its own types so it builds and tests on its own (no engine link), and so every operation
// it depends on for bit-identical results is spelled out here in one place.
//
// Written from the textbook definitions (vector algebra, Hamilton's quaternion product, the rotation of a vector by a
// unit quaternion as q v q*, matrix products). No other code was used.
// ---------------------------------------------------------------------------
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace okay {
namespace phys {

struct V3 {
    float x = 0, y = 0, z = 0;
    constexpr V3() = default;
    constexpr V3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    float& operator[](int i) { return (&x)[i]; }
    const float& operator[](int i) const { return (&x)[i]; }
    V3 operator+(const V3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    V3 operator-(const V3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    V3 operator*(float s) const { return {x * s, y * s, z * s}; }
    V3 operator-() const { return {-x, -y, -z}; }
    V3& operator+=(const V3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    V3& operator-=(const V3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    V3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    bool operator==(const V3& o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!=(const V3& o) const { return !(*this == o); }
};
inline V3 operator*(float s, const V3& v) { return v * s; }
inline float Dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 Cross(const V3& a, const V3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float LenSq(const V3& v) { return Dot(v, v); }
inline float Len(const V3& v) { return std::sqrt(LenSq(v)); }
inline V3 Mul(const V3& a, const V3& b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline V3 Min(const V3& a, const V3& b) { return {a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.z < b.z ? a.z : b.z}; }
inline V3 Max(const V3& a, const V3& b) { return {a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y, a.z > b.z ? a.z : b.z}; }
inline V3 Abs(const V3& a) { return {std::fabs(a.x), std::fabs(a.y), std::fabs(a.z)}; }
/// Unit vector along v, or `fallback` when v is (nearly) zero or not finite.
inline V3 Normalize(const V3& v, const V3& fallback = V3{0, 1, 0}) {
    const float l = Len(v);
    if (!(l > 1e-20f) || !std::isfinite(l)) return fallback;
    return v * (1.0f / l);
}
inline bool Finite(const V3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
/// Two unit vectors that, with n, make a right-handed orthonormal basis (n, t1, t2). Branch on the smallest component so
/// the result is well conditioned for every n.
inline void Basis(const V3& n, V3& t1, V3& t2) {
    if (std::fabs(n.x) >= 0.57735f) t1 = Normalize(V3{n.y, -n.x, 0.0f});
    else t1 = Normalize(V3{0.0f, n.z, -n.y});
    t2 = Cross(n, t1);
}

/// A rotation as a unit quaternion (x, y, z = vector part, w = scalar part).
struct Q {
    float x = 0, y = 0, z = 0, w = 1;
    constexpr Q() = default;
    constexpr Q(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    static Q Identity() { return Q{}; }
    /// Rotation of `radians` about the unit `axis`.
    static Q AxisAngle(const V3& axis, float radians) {
        const float h = 0.5f * radians, s = std::sin(h);
        return {axis.x * s, axis.y * s, axis.z * s, std::cos(h)};
    }
    Q operator*(const Q& b) const {   // Hamilton product: apply b first, then this
        return {w * b.x + x * b.w + y * b.z - z * b.y,
                w * b.y - x * b.z + y * b.w + z * b.x,
                w * b.z + x * b.y - y * b.x + z * b.w,
                w * b.w - x * b.x - y * b.y - z * b.z};
    }
    bool operator==(const Q& o) const { return x == o.x && y == o.y && z == o.z && w == o.w; }
};
inline Q Conj(const Q& q) { return {-q.x, -q.y, -q.z, q.w}; }
inline Q NormalizeQ(const Q& q) {
    const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!(l > 1e-20f) || !std::isfinite(l)) return Q{};
    const float s = 1.0f / l;
    return {q.x * s, q.y * s, q.z * s, q.w * s};
}
/// v rotated by the unit quaternion q (expanded form of q v q*).
inline V3 Rotate(const Q& q, const V3& v) {
    const V3 u{q.x, q.y, q.z};
    const V3 t = Cross(u, v) * 2.0f;
    return v + t * q.w + Cross(u, t);
}
inline V3 InvRotate(const Q& q, const V3& v) { return Rotate(Conj(q), v); }
/// q advanced by angular velocity w (rad/s) over dt: q + 0.5 * (w, 0) * q * dt, renormalised (first-order exponential map).
inline Q Integrate(const Q& q, const V3& w, float dt) {
    const Q s{w.x * 0.5f * dt, w.y * 0.5f * dt, w.z * 0.5f * dt, 0.0f};
    const Q d = s * q;
    return NormalizeQ(Q{q.x + d.x, q.y + d.y, q.z + d.z, q.w + d.w});
}
/// The rotation vector (axis * angle, radians, angle in [0, pi]) of a unit quaternion; the short way round.
inline V3 ToRotationVector(Q q) {
    if (q.w < 0.0f) q = Q{-q.x, -q.y, -q.z, -q.w};
    const V3 v{q.x, q.y, q.z};
    const float s = Len(v);
    if (s < 1e-7f) return v * 2.0f;   // small angle: sin(a/2) ~ a/2
    const float angle = 2.0f * std::atan2(s, q.w);
    return v * (angle / s);
}

/// 3x3 matrix, row-major: m[r][c].
struct M33 {
    float m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    static M33 Zero() { M33 r; for (auto& row : r.m) for (float& v : row) v = 0.0f; return r; }
    static M33 Diagonal(const V3& d) { M33 r = Zero(); r.m[0][0] = d.x; r.m[1][1] = d.y; r.m[2][2] = d.z; return r; }
    static M33 FromQ(const Q& q) {
        const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z, wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        M33 r;
        r.m[0][0] = 1 - 2 * (yy + zz); r.m[0][1] = 2 * (xy - wz);     r.m[0][2] = 2 * (xz + wy);
        r.m[1][0] = 2 * (xy + wz);     r.m[1][1] = 1 - 2 * (xx + zz); r.m[1][2] = 2 * (yz - wx);
        r.m[2][0] = 2 * (xz - wy);     r.m[2][1] = 2 * (yz + wx);     r.m[2][2] = 1 - 2 * (xx + yy);
        return r;
    }
    V3 operator*(const V3& v) const {
        return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
                m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
    }
    M33 operator*(const M33& b) const {
        M33 r = Zero();
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                r.m[i][j] = m[i][0] * b.m[0][j] + m[i][1] * b.m[1][j] + m[i][2] * b.m[2][j];
        return r;
    }
    M33 operator+(const M33& b) const { M33 r; for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) r.m[i][j] = m[i][j] + b.m[i][j]; return r; }
    M33 operator*(float s) const { M33 r; for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) r.m[i][j] = m[i][j] * s; return r; }
    M33 Transposed() const { M33 r; for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) r.m[i][j] = m[j][i]; return r; }
    V3 Col(int c) const { return {m[0][c], m[1][c], m[2][c]}; }
    float Det() const {   // dup-allow: physics core has no engine dependency (3x3 determinant)
        return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
               m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    }
    /// The inverse by the adjugate; a singular matrix gives the zero matrix (so a locked direction simply gets no push).
    /// dup-allow: physics core has no engine dependency (the engine's Mat3 is column-major with 2D helpers)
    M33 Inverse() const {
        const float d = Det();
        if (!(std::fabs(d) > 1e-30f)) return Zero();
        const float s = 1.0f / d;
        M33 r;
        r.m[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) * s;
        r.m[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * s;
        r.m[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * s;
        r.m[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) * s;
        r.m[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * s;
        r.m[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * s;
        r.m[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) * s;
        r.m[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * s;
        r.m[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * s;
        return r;
    }
};
/// The matrix of the cross product: Skew(a) * b == Cross(a, b).
inline M33 Skew(const V3& a) {
    M33 r = M33::Zero();
    r.m[0][1] = -a.z; r.m[0][2] = a.y;
    r.m[1][0] = a.z;  r.m[1][2] = -a.x;
    r.m[2][0] = -a.y; r.m[2][1] = a.x;
    return r;
}

/// A rigid transform: rotate, then translate.
struct Xf {
    V3 p;
    Q q;
    V3 Apply(const V3& v) const { return Rotate(q, v) + p; }
    V3 ApplyInv(const V3& v) const { return InvRotate(q, v - p); }
    Xf operator*(const Xf& b) const { return {Apply(b.p), NormalizeQ(q * b.q)}; }   // apply b, then this
};

/// Byte hash (64-bit FNV-1a) for state fingerprints and caches.
/// dup-allow: physics core has no engine dependency (the engine's is okay::StringId::HashOf)
constexpr std::uint64_t kHashSeed = 1469598103934665603ULL;
inline std::uint64_t HashBytes(std::uint64_t h, const void* p, std::size_t n) {
    const unsigned char* c = static_cast<const unsigned char*>(p);
    for (std::size_t i = 0; i < n; ++i) { h ^= c[i]; h *= 1099511628211ULL; }   // dup-allow: physics core has no engine dependency
    return h;
}

/// Axis-aligned box.
struct Aabb {
    V3 lo, hi;
    static Aabb Union(const Aabb& a, const Aabb& b) { return {Min(a.lo, b.lo), Max(a.hi, b.hi)}; }
    bool Overlaps(const Aabb& b) const {
        return !(hi.x < b.lo.x || lo.x > b.hi.x || hi.y < b.lo.y || lo.y > b.hi.y || hi.z < b.lo.z || lo.z > b.hi.z);
    }
    bool Contains(const Aabb& b) const {
        return lo.x <= b.lo.x && lo.y <= b.lo.y && lo.z <= b.lo.z && hi.x >= b.hi.x && hi.y >= b.hi.y && hi.z >= b.hi.z;
    }
    /// Half the surface area (the cost measure of the surface area heuristic; the factor 2 cancels).
    float HalfArea() const { const V3 d = hi - lo; return d.x * d.y + d.y * d.z + d.z * d.x; }
    Aabb Grown(float r) const { return {lo - V3{r, r, r}, hi + V3{r, r, r}}; }
    V3 Center() const { return (lo + hi) * 0.5f; }
};

} // namespace phys
} // namespace okay
