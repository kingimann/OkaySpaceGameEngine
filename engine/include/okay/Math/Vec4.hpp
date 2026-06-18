#pragma once
#include "okay/Math/Mathf.hpp"
#include "okay/Math/Vec3.hpp"

namespace okay {

/// 4D vector, used for homogeneous coordinates and colors.
struct Vec4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;

    constexpr Vec4() = default;
    constexpr Vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    constexpr Vec4(const Vec3& v, float w = 1.0f) : x(v.x), y(v.y), z(v.z), w(w) {}

    float&       operator[](int i)       { return (&x)[i]; }
    const float& operator[](int i) const { return (&x)[i]; }

    Vec3 xyz() const { return {x, y, z}; }

    Vec4 operator+(const Vec4& o) const { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
    Vec4 operator-(const Vec4& o) const { return {x - o.x, y - o.y, z - o.z, w - o.w}; }
    Vec4 operator*(float s) const       { return {x * s, y * s, z * s, w * s}; }

    static float Dot(const Vec4& a, const Vec4& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    }
};

} // namespace okay
