#pragma once
#include "okay/Math/Mathf.hpp"
#include "okay/Math/Vec2.hpp"

namespace okay {

/// 3D vector, modeled after Unity's `Vector3`.
struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
    explicit constexpr Vec3(float s) : x(s), y(s), z(s) {}
    constexpr Vec3(const Vec2& v, float z = 0.0f) : x(v.x), y(v.y), z(z) {}

    float&       operator[](int i)       { return (&x)[i]; }
    const float& operator[](int i) const { return (&x)[i]; }

    Vec2 xy() const { return {x, y}; }

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(const Vec3& o) const { return {x * o.x, y * o.y, z * o.z}; }
    Vec3 operator/(const Vec3& o) const { return {x / o.x, y / o.y, z / o.z}; }
    Vec3 operator*(float s) const       { return {x * s, y * s, z * s}; }
    Vec3 operator/(float s) const       { return {x / s, y / s, z / s}; }
    Vec3 operator-() const              { return {-x, -y, -z}; }

    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s)       { x *= s;   y *= s;   z *= s;   return *this; }
    Vec3& operator/=(float s)       { x /= s;   y /= s;   z /= s;   return *this; }

    bool operator==(const Vec3& o) const {
        return Mathf::Approximately(x, o.x) &&
               Mathf::Approximately(y, o.y) &&
               Mathf::Approximately(z, o.z);
    }
    bool operator!=(const Vec3& o) const { return !(*this == o); }

    float SqrMagnitude() const { return x * x + y * y + z * z; }
    float Magnitude() const    { return Mathf::Sqrt(SqrMagnitude()); }

    Vec3 Normalized() const {
        float m = Magnitude();
        return m > Mathf::Epsilon ? (*this) / m : Vec3{};
    }
    void Normalize() { *this = Normalized(); }

    static float Dot(const Vec3& a, const Vec3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }
    static Vec3 Cross(const Vec3& a, const Vec3& b) {
        return {a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x};
    }
    static float Distance(const Vec3& a, const Vec3& b) { return (a - b).Magnitude(); }
    static Vec3  Lerp(const Vec3& a, const Vec3& b, float t) {
        t = Mathf::Clamp01(t);
        return a + (b - a) * t;
    }
    static Vec3 MoveTowards(const Vec3& current, const Vec3& target, float maxDelta) {
        Vec3 d = target - current;
        float dist = d.Magnitude();
        if (dist <= maxDelta || dist < Mathf::Epsilon) return target;
        return current + d / dist * maxDelta;
    }
    static Vec3 LerpUnclamped(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }

    /// Reflect `v` off a surface with unit normal `n` (mirror across the plane) —
    /// bounce a velocity/ray. v' = v - 2(v.n)n.
    static Vec3 Reflect(const Vec3& v, const Vec3& n) { return v - n * (2.0f * Dot(v, n)); }
    /// The component of `a` along `b` (vector projection of a onto b).
    static Vec3 Project(const Vec3& a, const Vec3& b) {
        float d = Dot(b, b);
        return d < Mathf::Epsilon ? Vec3{} : b * (Dot(a, b) / d);
    }
    /// `a` with its component along the plane normal removed (slide along a surface).
    static Vec3 ProjectOnPlane(const Vec3& a, const Vec3& planeNormal) {
        return a - Project(a, planeNormal);
    }
    /// Clamp a vector's length to `maxLen` (keeps direction).
    static Vec3 ClampMagnitude(const Vec3& v, float maxLen) {
        float m = v.Magnitude();
        return (m > maxLen && m > Mathf::Epsilon) ? v * (maxLen / m) : v;
    }
    /// Unsigned angle between two vectors, in DEGREES.
    static float Angle(const Vec3& a, const Vec3& b) {
        float d = Mathf::Sqrt(a.SqrMagnitude() * b.SqrMagnitude());
        if (d < Mathf::Epsilon) return 0.0f;
        float c = Mathf::Clamp(Dot(a, b) / d, -1.0f, 1.0f);
        return std::acos(c) * Mathf::Rad2Deg;
    }
    /// Signed angle from `a` to `b` about `axis`, in DEGREES (right-hand rule).
    static float SignedAngle(const Vec3& a, const Vec3& b, const Vec3& axis) {
        float ang = Angle(a, b);
        float sign = Dot(axis, Cross(a, b)) < 0.0f ? -1.0f : 1.0f;
        return ang * sign;
    }
    /// Spherical interpolation between two vectors (eases direction + length).
    static Vec3 Slerp(const Vec3& a, const Vec3& b, float t) {
        t = Mathf::Clamp01(t);
        float ma = a.Magnitude(), mb = b.Magnitude();
        if (ma < Mathf::Epsilon || mb < Mathf::Epsilon) return Lerp(a, b, t);
        Vec3 na = a / ma, nb = b / mb;
        float d = Mathf::Clamp(Dot(na, nb), -1.0f, 1.0f);
        float theta = std::acos(d);
        float mag = ma + (mb - ma) * t;
        if (theta < 1e-4f) return Lerp(a, b, t);
        float s = std::sin(theta);
        Vec3 dir = (na * (std::sin((1.0f - t) * theta) / s)) + (nb * (std::sin(t * theta) / s));
        return dir * mag;
    }
    /// Critically-damped spring toward `target` (Unity's Vector3.SmoothDamp).
    /// `velocity` is carried between calls. Great for smooth camera/object follow.
    static Vec3 SmoothDamp(const Vec3& current, Vec3 target, Vec3& velocity,
                           float smoothTime, float deltaTime,
                           float maxSpeed = Mathf::Infinity) {
        smoothTime = Mathf::Max(0.0001f, smoothTime);
        float omega = 2.0f / smoothTime;
        float x = omega * deltaTime;
        float exp = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
        Vec3 change = current - target;
        float maxChange = maxSpeed * smoothTime;
        float mag = change.Magnitude();
        if (mag > maxChange && mag > Mathf::Epsilon) change *= (maxChange / mag);
        Vec3 origTarget = target;
        target = current - change;
        Vec3 temp = (velocity + change * omega) * deltaTime;
        velocity = (velocity - temp * omega) * exp;
        Vec3 output = target + (change + temp) * exp;
        if (Dot(origTarget - current, output - origTarget) > 0.0f) {
            output = origTarget;
            velocity = (output - origTarget) / deltaTime;
        }
        return output;
    }
    static Vec3 Scale(const Vec3& a, const Vec3& b) { return {a.x*b.x, a.y*b.y, a.z*b.z}; }
    static Vec3 Min(const Vec3& a, const Vec3& b) { return {Mathf::Min(a.x,b.x), Mathf::Min(a.y,b.y), Mathf::Min(a.z,b.z)}; }
    static Vec3 Max(const Vec3& a, const Vec3& b) { return {Mathf::Max(a.x,b.x), Mathf::Max(a.y,b.y), Mathf::Max(a.z,b.z)}; }
    static Vec3 Abs(const Vec3& v) { return {Mathf::Abs(v.x), Mathf::Abs(v.y), Mathf::Abs(v.z)}; }

    static const Vec3 Zero;
    static const Vec3 One;
    static const Vec3 Up;
    static const Vec3 Down;
    static const Vec3 Left;
    static const Vec3 Right;
    static const Vec3 Forward;
    static const Vec3 Back;
};

inline Vec3 operator*(float s, const Vec3& v) { return v * s; }

inline const Vec3 Vec3::Zero    {0, 0, 0};
inline const Vec3 Vec3::One     {1, 1, 1};
inline const Vec3 Vec3::Up      {0, 1, 0};
inline const Vec3 Vec3::Down    {0, -1, 0};
inline const Vec3 Vec3::Left    {-1, 0, 0};
inline const Vec3 Vec3::Right   {1, 0, 0};
inline const Vec3 Vec3::Forward {0, 0, 1};
inline const Vec3 Vec3::Back    {0, 0, -1};

} // namespace okay
