#pragma once
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Math/Quat.hpp"

namespace okay {

/// Column-major 3x3 matrix. Element at(col, row) -> data index col*3 + row.
/// Doubles as the 2D homogeneous transform (translation in column 2): see
/// TRS2D / TransformPoint2 / TransformVector2.
struct Mat3 {
    float m[9] = {1, 0, 0,
                  0, 1, 0,
                  0, 0, 1};

    constexpr Mat3() = default;

    float&       at(int col, int row)       { return m[col * 3 + row]; }
    const float& at(int col, int row) const { return m[col * 3 + row]; }

    static Mat3 Identity() { return Mat3{}; }

    /// Build from three COLUMNS.
    static Mat3 FromColumns(const Vec3& c0, const Vec3& c1, const Vec3& c2) {
        Mat3 r;
        for (int i = 0; i < 3; ++i) { r.at(0, i) = c0[i]; r.at(1, i) = c1[i]; r.at(2, i) = c2[i]; }
        return r;
    }
    /// Upper-left 3x3 of a Mat4 (drops translation / projection).
    static Mat3 FromMat4(const Mat4& a) {
        Mat3 r;
        for (int c = 0; c < 3; ++c) for (int row = 0; row < 3; ++row) r.at(c, row) = a.at(c, row);
        return r;
    }
    static Mat3 FromQuat(const Quat& q) { return FromMat4(Mat4::Rotate(q)); }
    static Mat3 Scale(const Vec3& s) { Mat3 r; r.at(0,0) = s.x; r.at(1,1) = s.y; r.at(2,2) = s.z; return r; }
    /// Rotation of `degrees` about `axis`.
    static Mat3 AngleAxis(float degrees, const Vec3& axis) { return FromQuat(Quat::AngleAxis(degrees, axis)); }

    // ---- 2D homogeneous helpers ----
    /// 2D rotation by `degrees` (counter-clockwise, +x toward +y).
    static Mat3 Rotation2D(float degrees) {
        float c = Mathf::Cos(degrees * Mathf::Deg2Rad), s = Mathf::Sin(degrees * Mathf::Deg2Rad);
        Mat3 r;
        r.at(0,0) = c; r.at(1,0) = -s;
        r.at(0,1) = s; r.at(1,1) = c;
        return r;
    }
    static Mat3 Translation2D(const Vec2& t) { Mat3 r; r.at(2,0) = t.x; r.at(2,1) = t.y; return r; }
    static Mat3 Scale2D(const Vec2& s) { Mat3 r; r.at(0,0) = s.x; r.at(1,1) = s.y; return r; }
    /// Translate * Rotate * Scale in 2D.
    static Mat3 TRS2D(const Vec2& t, float degrees, const Vec2& s) {
        return Translation2D(t) * Rotation2D(degrees) * Scale2D(s);
    }
    Vec2 TransformPoint2(const Vec2& p) const {
        return {at(0,0)*p.x + at(1,0)*p.y + at(2,0), at(0,1)*p.x + at(1,1)*p.y + at(2,1)};
    }
    Vec2 TransformVector2(const Vec2& v) const {
        return {at(0,0)*v.x + at(1,0)*v.y, at(0,1)*v.x + at(1,1)*v.y};
    }
    /// Decompose a 2D TRS (no shear assumed): translation, rotation in degrees, scale.
    /// A mirror puts the reflection on scale.x. Returns false for a degenerate matrix.
    bool Decompose2D(Vec2& t, float& degrees, Vec2& s) const {
        t = {at(2,0), at(2,1)};
        float sx = Mathf::Sqrt(at(0,0)*at(0,0) + at(0,1)*at(0,1));
        float det = at(0,0)*at(1,1) - at(1,0)*at(0,1);
        if (sx < 1e-8f) { degrees = 0; s = {0, 0}; return false; }
        float sy = det / sx;                 // signed: negative for a mirror
        if (sy < 0.0f) { sx = -sx; sy = -sy; } // keep the mirror on x
        s = {sx, sy};
        degrees = Mathf::Atan2(at(0,1) / sx, at(0,0) / sx) * Mathf::Rad2Deg;
        return true;
    }

    Mat3 operator*(const Mat3& b) const {
        Mat3 r;
        for (int c = 0; c < 3; ++c)
            for (int row = 0; row < 3; ++row) {
                float sum = 0.0f;
                for (int k = 0; k < 3; ++k) sum += at(k, row) * b.at(c, k);
                r.at(c, row) = sum;
            }
        return r;
    }
    Vec3 operator*(const Vec3& v) const {
        return {at(0,0)*v.x + at(1,0)*v.y + at(2,0)*v.z,
                at(0,1)*v.x + at(1,1)*v.y + at(2,1)*v.z,
                at(0,2)*v.x + at(1,2)*v.y + at(2,2)*v.z};
    }
    Mat3 operator*(float s) const { Mat3 r; for (int i = 0; i < 9; ++i) r.m[i] = m[i] * s; return r; }
    Mat3 operator+(const Mat3& b) const { Mat3 r; for (int i = 0; i < 9; ++i) r.m[i] = m[i] + b.m[i]; return r; }

    Vec3 Column(int c) const { return {at(c,0), at(c,1), at(c,2)}; }
    Vec3 Row(int r) const { return {at(0,r), at(1,r), at(2,r)}; }

    Mat3 Transpose() const {
        Mat3 r;
        for (int c = 0; c < 3; ++c) for (int row = 0; row < 3; ++row) r.at(c, row) = at(row, c);
        return r;
    }
    float Determinant() const {
        return at(0,0) * (at(1,1)*at(2,2) - at(2,1)*at(1,2))
             - at(1,0) * (at(0,1)*at(2,2) - at(2,1)*at(0,2))
             + at(2,0) * (at(0,1)*at(1,2) - at(1,1)*at(0,2));
    }
    /// Inverse; identity when singular (same convention as Mat4::Inverse).
    Mat3 Inverse() const {
        float det = Determinant();
        float bound = 1.0f;
        for (int c = 0; c < 3; ++c) bound *= Column(c).Magnitude();
        if (!(Mathf::Abs(det) > 1e-9f * bound)) return Mat3{};
        float id = 1.0f / det;
        Mat3 r;
        r.at(0,0) =  (at(1,1)*at(2,2) - at(2,1)*at(1,2)) * id;
        r.at(1,0) = -(at(1,0)*at(2,2) - at(2,0)*at(1,2)) * id;
        r.at(2,0) =  (at(1,0)*at(2,1) - at(2,0)*at(1,1)) * id;
        r.at(0,1) = -(at(0,1)*at(2,2) - at(2,1)*at(0,2)) * id;
        r.at(1,1) =  (at(0,0)*at(2,2) - at(2,0)*at(0,2)) * id;
        r.at(2,1) = -(at(0,0)*at(2,1) - at(2,0)*at(0,1)) * id;
        r.at(0,2) =  (at(0,1)*at(1,2) - at(1,1)*at(0,2)) * id;
        r.at(1,2) = -(at(0,0)*at(1,2) - at(1,0)*at(0,2)) * id;
        r.at(2,2) =  (at(0,0)*at(1,1) - at(1,0)*at(0,1)) * id;
        return r;
    }
    /// Inverse-transpose: transforms normals correctly under non-uniform scale.
    Mat3 NormalMatrix() const { return Inverse().Transpose(); }

    /// Rotation quaternion of this matrix's orientation. Scale (also a mirror) is
    /// stripped by Gram-Schmidt, so any invertible matrix gives a valid rotation.
    Quat ToQuat() const {
        Mat4 a;
        for (int c = 0; c < 3; ++c) for (int row = 0; row < 3; ++row) a.at(c, row) = at(c, row);
        Vec3 t, s; Quat q;
        a.Decompose(t, q, s);
        return q;
    }
};

} // namespace okay
