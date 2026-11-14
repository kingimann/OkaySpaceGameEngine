#pragma once
// Polygon triangulation shared by the own readers that meet polygons (OBJ, PLY, COLLADA). Internal: not installed.
#include "okay/Math/Vec3.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace okay {
namespace detail {

// ---------------------------------------------------------------------------
// Polygon triangulation: convex polygons are fanned (the corner order the file gave); a concave one
// is ear clipped on its dominant plane. Polygons over kMaxEarCorners corners are fanned either way.
// ---------------------------------------------------------------------------
inline constexpr std::size_t kMaxEarCorners = 256;

// Triangles as indices into `p` (0 .. p.size()-1), winding kept as the polygon's own.
inline void TriangulatePolygon(const std::vector<Vec3>& p, std::vector<std::array<int, 3>>& tris) {
    const int n = (int)p.size();
    tris.clear();
    auto fan = [&]() { for (int i = 1; i + 1 < n; ++i) tris.push_back({0, i, i + 1}); };
    if (n <= 3 || (std::size_t)n > kMaxEarCorners) { fan(); return; }
    // Newell normal, dominant axis dropped
    double nx = 0, ny = 0, nz = 0;
    for (int i = 0; i < n; ++i) {
        const Vec3& a = p[(std::size_t)i]; const Vec3& b = p[(std::size_t)((i + 1) % n)];
        nx += ((double)a.y - b.y) * ((double)a.z + b.z);
        ny += ((double)a.z - b.z) * ((double)a.x + b.x);
        nz += ((double)a.x - b.x) * ((double)a.y + b.y);
    }
    const double ax = std::fabs(nx), ay = std::fabs(ny), az = std::fabs(nz);
    if (!(ax + ay + az > 1e-30)) { fan(); return; }
    std::vector<double> u((std::size_t)n), v((std::size_t)n);
    for (int i = 0; i < n; ++i) {
        const Vec3& a = p[(std::size_t)i];
        if (az >= ax && az >= ay) { u[(std::size_t)i] = a.x; v[(std::size_t)i] = a.y; if (nz < 0) u[(std::size_t)i] = -u[(std::size_t)i]; }
        else if (ax >= ay) { u[(std::size_t)i] = a.y; v[(std::size_t)i] = a.z; if (nx < 0) u[(std::size_t)i] = -u[(std::size_t)i]; }
        else { u[(std::size_t)i] = a.z; v[(std::size_t)i] = a.x; if (ny < 0) u[(std::size_t)i] = -u[(std::size_t)i]; }
    }
    // now the polygon is counter-clockwise in (u, v)
    auto cross = [&](int a, int b, int c) { return (u[(std::size_t)b] - u[(std::size_t)a]) * (v[(std::size_t)c] - v[(std::size_t)a]) - (v[(std::size_t)b] - v[(std::size_t)a]) * (u[(std::size_t)c] - u[(std::size_t)a]); };
    bool convex = true;
    for (int i = 0; i < n && convex; ++i) if (cross(i, (i + 1) % n, (i + 2) % n) < -1e-12) convex = false;
    if (convex) { fan(); return; }
    std::vector<int> idx((std::size_t)n);
    for (int i = 0; i < n; ++i) idx[(std::size_t)i] = i;
    int guard = 0;
    while (idx.size() > 3 && guard++ < 4 * n) {
        bool clipped = false;
        const std::size_t m = idx.size();
        for (std::size_t i = 0; i < m; ++i) {
            const int a = idx[(i + m - 1) % m], b = idx[i], c = idx[(i + 1) % m];
            if (cross(a, b, c) <= 1e-12) continue;   // reflex or flat corner: not an ear
            bool inside = false;
            for (std::size_t k = 0; k < m && !inside; ++k) {
                const int q = idx[k];
                if (q == a || q == b || q == c) continue;
                auto same = [&](int o) { return std::fabs(u[(std::size_t)q] - u[(std::size_t)o]) < 1e-12 && std::fabs(v[(std::size_t)q] - v[(std::size_t)o]) < 1e-12; };
                if (same(a) || same(b) || same(c)) continue;   // a bridge or duplicate corner at the same spot is not inside
                if (cross(a, b, q) >= 0 && cross(b, c, q) >= 0 && cross(c, a, q) >= 0) inside = true;
            }
            if (inside) continue;
            tris.push_back({a, b, c});
            idx.erase(idx.begin() + (std::ptrdiff_t)i);
            clipped = true;
            break;
        }
        if (!clipped) break;
    }
    if (idx.size() == 3) tris.push_back({idx[0], idx[1], idx[2]});
    else for (std::size_t i = 1; i + 1 < idx.size(); ++i) tris.push_back({idx[0], idx[i], idx[i + 1]});   // degenerate input: fan what is left
    // zero-area slivers (the two coincident edges of a bridge into a hole) draw nothing
    tris.erase(std::remove_if(tris.begin(), tris.end(), [&](const std::array<int, 3>& t) { return std::fabs(cross(t[0], t[1], t[2])) < 1e-12; }), tris.end());
}

} // namespace detail
} // namespace okay
