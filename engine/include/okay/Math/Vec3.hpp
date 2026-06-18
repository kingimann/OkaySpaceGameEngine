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
