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

    /// Rotation that orients +Z along `forward` with the given `up` (Unity-style).
    static Quat LookRotation(Vec3 forward, Vec3 up = Vec3::Up) {
        forward = forward.Normalized();
        Vec3 right = Vec3::Cross(up, forward).Normalized();
        up = Vec3::Cross(forward, right);
        float m00 = right.x, m01 = up.x, m02 = forward.x;
        float m10 = right.y, m11 = up.y, m12 = forward.y;
        float m20 = right.z, m21 = up.z, m22 = forward.z;
        float t = m00 + m11 + m22;
        Quat q;
        if (t > 0.0f) {
            float s = Mathf::Sqrt(t + 1.0f) * 2.0f;
            q.w = 0.25f * s; q.x = (m21 - m12) / s; q.y = (m02 - m20) / s; q.z = (m10 - m01) / s;
        } else if (m00 > m11 && m00 > m22) {
            float s = Mathf::Sqrt(1.0f + m00 - m11 - m22) * 2.0f;
            q.w = (m21 - m12) / s; q.x = 0.25f * s; q.y = (m01 + m10) / s; q.z = (m02 + m20) / s;
        } else if (m11 > m22) {
            float s = Mathf::Sqrt(1.0f + m11 - m00 - m22) * 2.0f;
            q.w = (m02 - m20) / s; q.x = (m01 + m10) / s; q.y = 0.25f * s; q.z = (m12 + m21) / s;
        } else {
            float s = Mathf::Sqrt(1.0f + m22 - m00 - m11) * 2.0f;
            q.w = (m10 - m01) / s; q.x = (m02 + m20) / s; q.y = (m12 + m21) / s; q.z = 0.25f * s;
        }
        return q.Normalized();
    }

    static const Quat Identity;
};

inline const Quat Quat::Identity {0, 0, 0, 1};

} // namespace okay
