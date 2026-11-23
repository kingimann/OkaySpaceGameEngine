// Mesh UV projection.
// Bodies of okay::Mesh members moved out of engine/include/okay/Render/Mesh.hpp (modularization plan M1); the
// declarations (with their default arguments) stay in the struct. The code is unchanged.
#include "okay/Render/Mesh.hpp"

namespace okay {

    void Mesh::ProjectUVPlanar(int axis, float tile) {
        if (vertices.empty() || axis < 0 || axis > 2 || !std::isfinite(tile)) return;   // a typed NaN or infinite value would turn every UV into NaN
        Vec3 lo, hi; Bounds(lo, hi);
        Vec3 size = hi - lo;
        auto span = [&](int a) { float s = (&size.x)[a]; return s > 1e-6f ? s : 1.0f; };
        int ua = (axis == 0) ? 2 : 0, va = (axis == 1) ? 2 : 1;
        const bool flipU = axis == 0, flipV = axis != 1;   // seen from +X: u runs along -Z; sides: v runs down
        std::vector<Vec2> corner(triangles.size());
        for (std::size_t i = 0; i < triangles.size(); ++i) {
            const Vec3& p = vertices[triangles[i]];
            const float tu = ((&p.x)[ua] - (&lo.x)[ua]) / span(ua), tv = ((&p.x)[va] - (&lo.x)[va]) / span(va);
            corner[i] = {(flipU ? 1.0f - tu : tu) * tile, (flipV ? 1.0f - tv : tv) * tile};
        }
        SetCornerUVs(corner);
    }

    void Mesh::ProjectUVBox(float tile) {
        if (vertices.empty() || !std::isfinite(tile)) return;   // a typed NaN or infinite value would turn every UV into NaN
        Vec3 lo, hi; Bounds(lo, hi);
        Vec3 size = hi - lo;
        auto span = [&](int a) { float s = (&size.x)[a]; return s > 1e-6f ? s : 1.0f; };
        auto rel = [&](const Vec3& p, int a, bool flip) {
            const float t = ((&p.x)[a] - (&lo.x)[a]) / span(a);
            return (flip ? 1.0f - t : t) * tile;
        };
        std::vector<Vec2> corner(triangles.size());
        for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
            const Vec3 n = Vec3::Cross(vertices[triangles[t + 1]] - vertices[triangles[t]],
                                       vertices[triangles[t + 2]] - vertices[triangles[t]]);
            const float ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
            const int axis = (ax >= ay && ax >= az) ? 0 : (ay >= az ? 1 : 2);
            const bool neg = (&n.x)[axis] < 0.0f;
            for (int k = 0; k < 3; ++k) {
                const Vec3& p = vertices[triangles[t + k]];
                // u runs right and v runs down (v = 0 is the image's top) as each side
                // is seen from outside: +X (-z, -y), -X (z, -y), +Y (x, z), -Y (x, -z), +Z (x, -y), -Z (-x, -y)
                if (axis == 0)      corner[t + k] = {rel(p, 2, !neg), rel(p, 1, true)};
                else if (axis == 1) corner[t + k] = {rel(p, 0, false), rel(p, 2, neg)};
                else                corner[t + k] = {rel(p, 0, neg), rel(p, 1, true)};
            }
        }
        SetCornerUVs(corner);
    }

    void Mesh::ProjectUVCylinder(float tile) {
        if (vertices.empty() || !std::isfinite(tile)) return;   // a typed NaN or infinite value would turn every UV into NaN
        Vec3 lo, hi; Bounds(lo, hi);
        Vec3 c = (lo + hi) * 0.5f;
        float h = (hi.y - lo.y) > 1e-6f ? (hi.y - lo.y) : 1.0f;
        const float kPi = 3.14159265358979323846f;
        std::vector<Vec2> corner(triangles.size());
        for (std::size_t i = 0; i < triangles.size(); ++i) {
            const Vec3& p = vertices[triangles[i]];
            // u grows to the right seen from outside, v runs down from the top.
            corner[i] = {0.5f - std::atan2(p.z - c.z, p.x - c.x) / (2.0f * kPi), 1.0f - (p.y - lo.y) / h};
        }
        UnwrapAroundU(corner);
        for (Vec2& t : corner) t = t * tile;
        SetCornerUVs(corner);
    }

    void Mesh::ProjectUVSphere(float tile) {
        if (vertices.empty() || !std::isfinite(tile)) return;   // a typed NaN or infinite value would turn every UV into NaN
        Vec3 lo, hi; Bounds(lo, hi);
        Vec3 c = (lo + hi) * 0.5f;
        const float kPi = 3.14159265358979323846f;
        std::vector<Vec2> corner(triangles.size());
        for (std::size_t i = 0; i < triangles.size(); ++i) {
            Vec3 d = vertices[triangles[i]] - c;
            float m = d.Magnitude();
            if (m < 1e-8f) { corner[i] = {0.5f, 0.5f}; continue; }
            d = d * (1.0f / m);
            corner[i] = {0.5f - std::atan2(d.z, d.x) / (2.0f * kPi),
                         0.5f - std::asin(d.y < -1.0f ? -1.0f : (d.y > 1.0f ? 1.0f : d.y)) / kPi};
        }
        UnwrapAroundU(corner);
        for (Vec2& t : corner) t = t * tile;
        SetCornerUVs(corner);
    }

    void Mesh::SetCornerUVs(const std::vector<Vec2>& corner) {
        if (corner.size() != triangles.size()) return;
        cornerUVs = corner;
        uvs.assign(vertices.size(), Vec2{0, 0});
        std::vector<char> seen(vertices.size(), 0);
        for (std::size_t i = 0; i < triangles.size(); ++i) {
            const int v = triangles[i];
            if (v >= 0 && v < (int)vertices.size() && !seen[v]) { seen[v] = 1; uvs[v] = corner[i]; }
        }
        name = "";
    }

    void Mesh::UnwrapAroundU(std::vector<Vec2>& corner) const {
        for (std::size_t t = 0; t + 2 < corner.size(); t += 3) {
            const float u0 = corner[t].x, u1 = corner[t + 1].x, u2 = corner[t + 2].x;
            const float lo = std::min(u0, std::min(u1, u2)), hi = std::max(u0, std::max(u1, u2));
            if (hi - lo <= 0.5f) continue;
            for (int k = 0; k < 3; ++k) if (corner[t + k].x < 0.5f) corner[t + k].x += 1.0f;
        }
    }

} // namespace okay
