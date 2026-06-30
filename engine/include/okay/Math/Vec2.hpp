#pragma once
#include "okay/Math/Mathf.hpp"

namespace okay {

/// 2D vector, modeled after Unity's `Vector2`.
struct Vec2 {
    float x = 0.0f, y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float x, float y) : x(x), y(y) {}
    explicit constexpr Vec2(float s) : x(s), y(s) {}

    // Component access
    float&       operator[](int i)       { return (&x)[i]; }
    const float& operator[](int i) const { return (&x)[i]; }

    // Arithmetic
    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(const Vec2& o) const { return {x * o.x, y * o.y}; }
    Vec2 operator/(const Vec2& o) const { return {x / o.x, y / o.y}; }
    Vec2 operator*(float s) const       { return {x * s, y * s}; }
    Vec2 operator/(float s) const       { return {x / s, y / s}; }
    Vec2 operator-() const              { return {-x, -y}; }

    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s)       { x *= s;   y *= s;   return *this; }
    Vec2& operator/=(float s)       { x /= s;   y /= s;   return *this; }

    bool operator==(const Vec2& o) const {
        return Mathf::Approximately(x, o.x) && Mathf::Approximately(y, o.y);
    }
    bool operator!=(const Vec2& o) const { return !(*this == o); }

    float SqrMagnitude() const { return x * x + y * y; }
    float Magnitude() const    { return Mathf::Sqrt(SqrMagnitude()); }

    Vec2 Normalized() const {
        float m = Magnitude();
        return m > Mathf::Epsilon ? (*this) / m : Vec2{};
    }
    void Normalize() { *this = Normalized(); }

    static float Dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
    /// 2D cross product — the z of the 3D cross, i.e. the signed area of the
    /// parallelogram. Positive when `b` is counter-clockwise from `a`.
    static float Cross(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }
    static float Distance(const Vec2& a, const Vec2& b) { return (a - b).Magnitude(); }
    static Vec2  Lerp(const Vec2& a, const Vec2& b, float t) {
        t = Mathf::Clamp01(t);
        return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
    }
    static Vec2 LerpUnclamped(const Vec2& a, const Vec2& b, float t) {
        return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
    }
    /// Step from `current` toward `target`, moving at most `maxDelta` units.
    static Vec2 MoveTowards(const Vec2& current, const Vec2& target, float maxDelta) {
        Vec2 d = target - current;
        float dist = d.Magnitude();
        if (dist <= maxDelta || dist < Mathf::Epsilon) return target;
        return current + d / dist * maxDelta;
    }
    /// Reflect `v` off a surface with unit normal `n` (bounce a velocity/ray).
    static Vec2 Reflect(const Vec2& v, const Vec2& n) { return v - n * (2.0f * Dot(v, n)); }
    /// Perpendicular vector, rotated +90° (counter-clockwise): (x,y) -> (-y,x).
    static Vec2 Perpendicular(const Vec2& v) { return {-v.y, v.x}; }
    /// The component of `a` along `b` (vector projection of a onto b).
    static Vec2 Project(const Vec2& a, const Vec2& b) {
        float d = Dot(b, b);
        return d < Mathf::Epsilon ? Vec2{} : b * (Dot(a, b) / d);
    }
    /// Clamp a vector's length to `maxLen` (keeps direction).
    static Vec2 ClampMagnitude(const Vec2& v, float maxLen) {
        float m = v.Magnitude();
        return (m > maxLen && m > Mathf::Epsilon) ? v * (maxLen / m) : v;
    }
    /// Unsigned angle between two vectors, in DEGREES.
    static float Angle(const Vec2& a, const Vec2& b) {
        float d = Mathf::Sqrt(a.SqrMagnitude() * b.SqrMagnitude());
        if (d < Mathf::Epsilon) return 0.0f;
        return std::acos(Mathf::Clamp(Dot(a, b) / d, -1.0f, 1.0f)) * Mathf::Rad2Deg;
    }
    /// Signed angle from `a` to `b`, in DEGREES (positive = counter-clockwise).
    static float SignedAngle(const Vec2& a, const Vec2& b) {
        return Angle(a, b) * (Cross(a, b) < 0.0f ? -1.0f : 1.0f);
    }
    /// Rotate a vector by `degrees` (counter-clockwise).
    static Vec2 Rotate(const Vec2& v, float degrees) {
        float r = degrees * Mathf::Deg2Rad, c = Mathf::Cos(r), s = Mathf::Sin(r);
        return {v.x * c - v.y * s, v.x * s + v.y * c};
    }
    /// Critically-damped spring toward `target` (Unity's Vector2.SmoothDamp).
    /// `velocity` is carried between calls. Great for smooth camera/object follow.
    static Vec2 SmoothDamp(const Vec2& current, Vec2 target, Vec2& velocity,
                           float smoothTime, float deltaTime,
                           float maxSpeed = Mathf::Infinity) {
        smoothTime = Mathf::Max(0.0001f, smoothTime);
        float omega = 2.0f / smoothTime;
        float x = omega * deltaTime;
        float exp = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
        Vec2 change = current - target;
        float maxChange = maxSpeed * smoothTime;
        float mag = change.Magnitude();
        if (mag > maxChange && mag > Mathf::Epsilon) change *= (maxChange / mag);
        Vec2 origTarget = target;
        target = current - change;
        Vec2 temp = (velocity + change * omega) * deltaTime;
        velocity = (velocity - temp * omega) * exp;
        Vec2 output = target + (change + temp) * exp;
        if (Dot(origTarget - current, output - origTarget) > 0.0f) {
            output = origTarget;
            velocity = (output - origTarget) / deltaTime;
        }
        return output;
    }
    static Vec2 Scale(const Vec2& a, const Vec2& b) { return {a.x * b.x, a.y * b.y}; }
    static Vec2 Min(const Vec2& a, const Vec2& b) { return {Mathf::Min(a.x, b.x), Mathf::Min(a.y, b.y)}; }
    static Vec2 Max(const Vec2& a, const Vec2& b) { return {Mathf::Max(a.x, b.x), Mathf::Max(a.y, b.y)}; }
    static Vec2 Abs(const Vec2& v) { return {Mathf::Abs(v.x), Mathf::Abs(v.y)}; }

    static const Vec2 Zero;
    static const Vec2 One;
    static const Vec2 Up;
    static const Vec2 Down;
    static const Vec2 Left;
    static const Vec2 Right;
};

inline Vec2 operator*(float s, const Vec2& v) { return v * s; }

inline const Vec2 Vec2::Zero  {0, 0};
inline const Vec2 Vec2::One   {1, 1};
inline const Vec2 Vec2::Up    {0, 1};
inline const Vec2 Vec2::Down  {0, -1};
inline const Vec2 Vec2::Left  {-1, 0};
inline const Vec2 Vec2::Right {1, 0};

} // namespace okay
