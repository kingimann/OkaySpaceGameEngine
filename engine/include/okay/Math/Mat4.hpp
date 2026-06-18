#pragma once
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Vec4.hpp"
#include "okay/Math/Quat.hpp"

namespace okay {

/// Column-major 4x4 matrix, modeled after Unity's `Matrix4x4`.
/// Element m[col][row] -> data index col*4 + row.
struct Mat4 {
    float m[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1};

    constexpr Mat4() = default;

    float&       at(int col, int row)       { return m[col * 4 + row]; }
    const float& at(int col, int row) const { return m[col * 4 + row]; }

    Mat4 operator*(const Mat4& b) const {
        Mat4 r;
        for (int c = 0; c < 4; ++c) {
            for (int row = 0; row < 4; ++row) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k)
                    sum += at(k, row) * b.at(c, k);
                r.at(c, row) = sum;
            }
        }
        return r;
    }

    Vec4 operator*(const Vec4& v) const {
        return {
            at(0,0)*v.x + at(1,0)*v.y + at(2,0)*v.z + at(3,0)*v.w,
            at(0,1)*v.x + at(1,1)*v.y + at(2,1)*v.z + at(3,1)*v.w,
            at(0,2)*v.x + at(1,2)*v.y + at(2,2)*v.z + at(3,2)*v.w,
            at(0,3)*v.x + at(1,3)*v.y + at(2,3)*v.z + at(3,3)*v.w};
    }

    /// Transform a position (w = 1) and apply the perspective divide.
    Vec3 MultiplyPoint(const Vec3& p) const {
        Vec4 r = (*this) * Vec4{p, 1.0f};
        if (Mathf::Abs(r.w) > Mathf::Epsilon && r.w != 1.0f) r = r * (1.0f / r.w);
        return r.xyz();
    }
    /// Transform a direction (w = 0), ignoring translation.
    Vec3 MultiplyVector(const Vec3& v) const {
        return ((*this) * Vec4{v, 0.0f}).xyz();
    }

    static Mat4 Identity() { return Mat4{}; }

    static Mat4 Translate(const Vec3& t) {
        Mat4 r;
        r.at(3,0) = t.x; r.at(3,1) = t.y; r.at(3,2) = t.z;
        return r;
    }

    static Mat4 Scale(const Vec3& s) {
        Mat4 r;
        r.at(0,0) = s.x; r.at(1,1) = s.y; r.at(2,2) = s.z;
        return r;
    }

    static Mat4 Rotate(const Quat& q) {
        Quat n = q.Normalized();
        float x = n.x, y = n.y, z = n.z, w = n.w;
        Mat4 r;
        r.at(0,0) = 1 - 2*(y*y + z*z); r.at(1,0) = 2*(x*y - w*z);     r.at(2,0) = 2*(x*z + w*y);
        r.at(0,1) = 2*(x*y + w*z);     r.at(1,1) = 1 - 2*(x*x + z*z); r.at(2,1) = 2*(y*z - w*x);
        r.at(0,2) = 2*(x*z - w*y);     r.at(1,2) = 2*(y*z + w*x);     r.at(2,2) = 1 - 2*(x*x + y*y);
        return r;
    }

    /// Compose a Translate * Rotate * Scale transform (Unity's TRS order).
    static Mat4 TRS(const Vec3& t, const Quat& r, const Vec3& s) {
        return Translate(t) * Rotate(r) * Scale(s);
    }

    static Mat4 Ortho(float left, float right, float bottom, float top, float zNear, float zFar) {
        Mat4 r;
        r.at(0,0) = 2.0f / (right - left);
        r.at(1,1) = 2.0f / (top - bottom);
        r.at(2,2) = -2.0f / (zFar - zNear);
        r.at(3,0) = -(right + left) / (right - left);
        r.at(3,1) = -(top + bottom) / (top - bottom);
        r.at(3,2) = -(zFar + zNear) / (zFar - zNear);
        return r;
    }

    static Mat4 Perspective(float fovDeg, float aspect, float zNear, float zFar) {
        float f = 1.0f / Mathf::Tan(fovDeg * Mathf::Deg2Rad * 0.5f);
        Mat4 r;
        for (int i = 0; i < 16; ++i) r.m[i] = 0.0f;
        r.at(0,0) = f / aspect;
        r.at(1,1) = f;
        r.at(2,2) = (zFar + zNear) / (zNear - zFar);
        r.at(2,3) = -1.0f;
        r.at(3,2) = (2.0f * zFar * zNear) / (zNear - zFar);
        return r;
    }
};

} // namespace okay
