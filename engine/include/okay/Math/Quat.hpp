#pragma once
#include "okay/Math/Mathf.hpp"
#include "okay/Math/Vec3.hpp"

namespace okay {

/// Quaternion rotation, modeled after Unity's `Quaternion`.
struct Quat {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;

    constexpr Quat() = default;
    constexpr Quat(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

    Quat operator*(const Quat& q) const {
        return {
            w * q.x + x * q.w + y * q.z - z * q.y,
            w * q.y - x * q.z + y * q.w + z * q.x,
            w * q.z + x * q.y - y * q.x + z * q.w,
            w * q.w - x * q.x - y * q.y - z * q.z};
    }

    /// Rotate a vector by this quaternion.
    Vec3 operator*(const Vec3& v) const {
        Vec3 u{x, y, z};
        float s = w;
        return u * (2.0f * Vec3::Dot(u, v))
             + v * (s * s - Vec3::Dot(u, u))
             + Vec3::Cross(u, v) * (2.0f * s);
    }

    float Magnitude() const { return Mathf::Sqrt(x * x + y * y + z * z + w * w); }

    Quat Normalized() const {
        float m = Magnitude();
        if (m < Mathf::Epsilon) return Identity;
        return {x / m, y / m, z / m, w / m};
    }
    void Normalize() { *this = Normalized(); }

    Quat Conjugate() const { return {-x, -y, -z, w}; }

    /// Build a rotation from an axis (normalized) and an angle in degrees.
    static Quat AngleAxis(float degrees, const Vec3& axis) {
        Vec3 a = axis.Normalized();
        float r = degrees * Mathf::Deg2Rad * 0.5f;
        float s = Mathf::Sin(r);
        return {a.x * s, a.y * s, a.z * s, Mathf::Cos(r)};
    }

    /// Build a rotation from Euler angles (degrees), applied Z, X, Y like Unity.
    static Quat Euler(float xDeg, float yDeg, float zDeg) {
        Quat qx = AngleAxis(xDeg, Vec3::Right);
        Quat qy = AngleAxis(yDeg, Vec3::Up);
        Quat qz = AngleAxis(zDeg, Vec3::Forward);
        return (qy * qx * qz).Normalized();
    }
    static Quat Euler(const Vec3& e) { return Euler(e.x, e.y, e.z); }

    static Quat Slerp(const Quat& a, Quat b, float t) {
        t = Mathf::Clamp01(t);
        float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
        if (dot < 0.0f) { b = {-b.x, -b.y, -b.z, -b.w}; dot = -dot; }
        if (dot > 0.9995f) {
            return Quat{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                        a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t}.Normalized();
        }
        float theta0 = std::acos(dot);
        float theta  = theta0 * t;
        float sinT   = Mathf::Sin(theta);
        float sinT0  = Mathf::Sin(theta0);
        float s0 = Mathf::Cos(theta) - dot * sinT / sinT0;
        float s1 = sinT / sinT0;
        return {a.x * s0 + b.x * s1, a.y * s0 + b.y * s1,
                a.z * s0 + b.z * s1, a.w * s0 + b.w * s1};
    }

    static const Quat Identity;
};

inline const Quat Quat::Identity {0, 0, 0, 1};

} // namespace okay
