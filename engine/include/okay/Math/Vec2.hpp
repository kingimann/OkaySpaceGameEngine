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
    static float Distance(const Vec2& a, const Vec2& b) { return (a - b).Magnitude(); }
    static Vec2  Lerp(const Vec2& a, const Vec2& b, float t) {
        t = Mathf::Clamp01(t);
        return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
    }

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
