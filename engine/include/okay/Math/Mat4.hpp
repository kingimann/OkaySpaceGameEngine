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

    /// Right-handed view matrix looking from `eye` toward `center`.
    static Mat4 LookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
        Vec3 f = (center - eye).Normalized();
        Vec3 s = Vec3::Cross(f, up).Normalized();
        Vec3 u = Vec3::Cross(s, f);
        Mat4 r;
        r.at(0,0) = s.x; r.at(1,0) = s.y; r.at(2,0) = s.z;
        r.at(0,1) = u.x; r.at(1,1) = u.y; r.at(2,1) = u.z;
        r.at(0,2) = -f.x; r.at(1,2) = -f.y; r.at(2,2) = -f.z;
        r.at(3,0) = -Vec3::Dot(s, eye);
        r.at(3,1) = -Vec3::Dot(u, eye);
        r.at(3,2) =  Vec3::Dot(f, eye);
        return r;
    }

    /// General 4x4 inverse (returns identity if the matrix is singular).
    Mat4 Inverse() const {
        const float* a = m;
        Mat4 out;
        float* inv = out.m;
        inv[0]  =  a[5]*a[10]*a[15] - a[5]*a[11]*a[14] - a[9]*a[6]*a[15] + a[9]*a[7]*a[14] + a[13]*a[6]*a[11] - a[13]*a[7]*a[10];
        inv[4]  = -a[4]*a[10]*a[15] + a[4]*a[11]*a[14] + a[8]*a[6]*a[15] - a[8]*a[7]*a[14] - a[12]*a[6]*a[11] + a[12]*a[7]*a[10];
        inv[8]  =  a[4]*a[9]*a[15]  - a[4]*a[11]*a[13] - a[8]*a[5]*a[15] + a[8]*a[7]*a[13] + a[12]*a[5]*a[11] - a[12]*a[7]*a[9];
        inv[12] = -a[4]*a[9]*a[14]  + a[4]*a[10]*a[13] + a[8]*a[5]*a[14] - a[8]*a[6]*a[13] - a[12]*a[5]*a[10] + a[12]*a[6]*a[9];
        inv[1]  = -a[1]*a[10]*a[15] + a[1]*a[11]*a[14] + a[9]*a[2]*a[15] - a[9]*a[3]*a[14] - a[13]*a[2]*a[11] + a[13]*a[3]*a[10];
        inv[5]  =  a[0]*a[10]*a[15] - a[0]*a[11]*a[14] - a[8]*a[2]*a[15] + a[8]*a[3]*a[14] + a[12]*a[2]*a[11] - a[12]*a[3]*a[10];
        inv[9]  = -a[0]*a[9]*a[15]  + a[0]*a[11]*a[13] + a[8]*a[1]*a[15] - a[8]*a[3]*a[13] - a[12]*a[1]*a[11] + a[12]*a[3]*a[9];
        inv[13] =  a[0]*a[9]*a[14]  - a[0]*a[10]*a[13] - a[8]*a[1]*a[14] + a[8]*a[2]*a[13] + a[12]*a[1]*a[10] - a[12]*a[2]*a[9];
        inv[2]  =  a[1]*a[6]*a[15]  - a[1]*a[7]*a[14]  - a[5]*a[2]*a[15] + a[5]*a[3]*a[14] + a[13]*a[2]*a[7]  - a[13]*a[3]*a[6];
        inv[6]  = -a[0]*a[6]*a[15]  + a[0]*a[7]*a[14]  + a[4]*a[2]*a[15] - a[4]*a[3]*a[14] - a[12]*a[2]*a[7]  + a[12]*a[3]*a[6];
        inv[10] =  a[0]*a[5]*a[15]  - a[0]*a[7]*a[13]  - a[4]*a[1]*a[15] + a[4]*a[3]*a[13] + a[12]*a[1]*a[7]  - a[12]*a[3]*a[5];
        inv[14] = -a[0]*a[5]*a[14]  + a[0]*a[6]*a[13]  + a[4]*a[1]*a[14] - a[4]*a[2]*a[13] - a[12]*a[1]*a[6]  + a[12]*a[2]*a[5];
        inv[3]  = -a[1]*a[6]*a[11]  + a[1]*a[7]*a[10]  + a[5]*a[2]*a[11] - a[5]*a[3]*a[10] - a[9]*a[2]*a[7]   + a[9]*a[3]*a[6];
        inv[7]  =  a[0]*a[6]*a[11]  - a[0]*a[7]*a[10]  - a[4]*a[2]*a[11] + a[4]*a[3]*a[10] + a[8]*a[2]*a[7]   - a[8]*a[3]*a[6];
        inv[11] = -a[0]*a[5]*a[11]  + a[0]*a[7]*a[9]   + a[4]*a[1]*a[11] - a[4]*a[3]*a[9]  - a[8]*a[1]*a[7]   + a[8]*a[3]*a[5];
        inv[15] =  a[0]*a[5]*a[10]  - a[0]*a[6]*a[9]   - a[4]*a[1]*a[10] + a[4]*a[2]*a[9]  + a[8]*a[1]*a[6]   - a[8]*a[2]*a[5];
        float det = a[0]*inv[0] + a[1]*inv[4] + a[2]*inv[8] + a[3]*inv[12];
        if (Mathf::Abs(det) < 1e-12f) return Mat4{}; // singular
        float invDet = 1.0f / det;
        for (int i = 0; i < 16; ++i) inv[i] *= invDet;
        return out;
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
