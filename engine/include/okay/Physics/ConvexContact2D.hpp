#pragma once
// Convex-vs-convex contact in 2D: GJK overlap, then EPA penetration, over the Minkowski difference A - B.
// The normal points from A toward B. Written from the general definitions of the two methods (support points, a simplex
// that closes in on the origin, a polygon that grows toward the nearest edge); no code from any other engine.
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Mathf.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace okay {

struct ConvexHit2 {
    bool  hit = false;
    Vec2  normal{0, 1};
    float penetration = 0.0f;
    Vec2  point{0, 0};      // between the two witness points
};

namespace convex2d_detail {
struct Sup { Vec2 w, a, b; };
inline Vec2 Perp(const Vec2& v) { return {-v.y, v.x}; }
inline float Dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
}  // namespace convex2d_detail

/// supportA / supportB: callables Vec2(const Vec2& dir) giving the furthest point of each shape along dir.
template <class SA, class SB>
ConvexHit2 ConvexContact2D(SA supportA, SB supportB) {
    using namespace convex2d_detail;
    ConvexHit2 out;
    auto sup = [&](const Vec2& dir) {
        Vec2 d = Dot(dir, dir) > 1e-20f ? dir : Vec2{1, 0};
        Sup s; s.a = supportA(d); s.b = supportB(Vec2{-d.x, -d.y}); s.w = {s.a.x - s.b.x, s.a.y - s.b.y};
        return s;
    };
    Sup s[3]; int n = 0;
    Vec2 d{1, 0};
    s[n++] = sup(d);
    d = {-s[0].w.x, -s[0].w.y};
    bool inside = false;
    if (Dot(d, d) < 1e-14f) inside = true;                              // the first point is the origin
    for (int it = 0; it < 64 && !inside; ++it) {
        Sup a = sup(d);
        if (Dot(a.w, d) < 0.0f) return out;                             // the origin lies beyond the support: apart
        if (n == 1) {
            s[1] = s[0]; s[0] = a; n = 2;                               // s[1] = oldest, s[0] = newest for a line
            Vec2 ab{s[1].w.x - s[0].w.x, s[1].w.y - s[0].w.y};
            Vec2 ao{-s[0].w.x, -s[0].w.y};
            Vec2 p = Perp(ab);
            if (Dot(p, ao) < 0.0f) p = {-p.x, -p.y};
            if (Dot(p, p) < 1e-20f) { inside = true; break; }           // the origin is on the line: touching
            d = p;
            continue;
        }
        if (n == 2) { s[2] = s[1]; s[1] = s[0]; s[0] = a; n = 3; }
        else { s[2] = s[1]; s[1] = s[0]; s[0] = a; }
        // Triangle: s[0] = A (newest), s[1] = B, s[2] = C.
        Vec2 A = s[0].w, B = s[1].w, C = s[2].w;
        Vec2 ab{B.x - A.x, B.y - A.y}, ac{C.x - A.x, C.y - A.y}, ao{-A.x, -A.y};
        Vec2 abp = Perp(ab); if (Dot(abp, ac) > 0.0f) abp = {-abp.x, -abp.y};   // away from C
        Vec2 acp = Perp(ac); if (Dot(acp, ab) > 0.0f) acp = {-acp.x, -acp.y};   // away from B
        if (Dot(abp, ao) > 0.0f) { n = 2; d = abp; }                          // origin beyond edge AB: keep A, B (s[0], s[1])
        else if (Dot(acp, ao) > 0.0f) { s[1] = s[2]; n = 2; d = acp; }          // origin beyond edge AC: keep A, C
        else inside = true;
    }
    if (!inside) return out;
    // ---- EPA: grow the polygon around the origin until its nearest edge cannot be pushed out any further ----
    std::vector<Sup> poly;
    if (n >= 3) poly = {s[0], s[1], s[2]};
    else {
        // Touching / a line: fatten it into a triangle with one more support point on each side.
        Sup p0 = n >= 1 ? s[0] : sup({1, 0});
        Sup p1 = n >= 2 ? s[1] : sup({-1, 0});
        Vec2 e{p1.w.x - p0.w.x, p1.w.y - p0.w.y};
        Vec2 pr = Perp(e); if (Dot(pr, pr) < 1e-20f) pr = {0, 1};
        Sup p2 = sup(pr), p3 = sup({-pr.x, -pr.y});
        poly = {p0, p2, p1, p3};
    }
    auto area2 = [&]() { float a = 0; for (std::size_t i = 0; i < poly.size(); ++i) { const Vec2& p = poly[i].w; const Vec2& q = poly[(i + 1) % poly.size()].w; a += p.x * q.y - q.x * p.y; } return a; };
    if (area2() < 0.0f) std::reverse(poly.begin(), poly.end());         // counter-clockwise
    for (int it = 0; it < 64; ++it) {
        std::size_t bi = 0; float bd = 1e30f; Vec2 bn{0, 1};
        for (std::size_t i = 0; i < poly.size(); ++i) {
            const Vec2& p = poly[i].w; const Vec2& q = poly[(i + 1) % poly.size()].w;
            Vec2 e{q.x - p.x, q.y - p.y};
            float len = std::sqrt(Dot(e, e));
            if (len < 1e-12f) continue;
            Vec2 nn{e.y / len, -e.x / len};                              // outward for a counter-clockwise polygon
            float dist = Dot(nn, p);
            if (dist < bd) { bd = dist; bi = i; bn = nn; }
        }
        Sup p = sup(bn);
        float gain = Dot(p.w, bn) - bd;
        if (gain < 1e-4f || it == 63) {
            const Sup& e0 = poly[bi]; const Sup& e1 = poly[(bi + 1) % poly.size()];
            Vec2 ed{e1.w.x - e0.w.x, e1.w.y - e0.w.y};
            float l2 = Dot(ed, ed);
            float t = l2 > 1e-20f ? Mathf::Clamp(Dot({-e0.w.x, -e0.w.y}, ed) / l2, 0.0f, 1.0f) : 0.0f;
            Vec2 pa{e0.a.x + (e1.a.x - e0.a.x) * t, e0.a.y + (e1.a.y - e0.a.y) * t};
            Vec2 pb{e0.b.x + (e1.b.x - e0.b.x) * t, e0.b.y + (e1.b.y - e0.b.y) * t};
            out.hit = true; out.normal = bn; out.penetration = Mathf::Max(bd, 0.0f);
            out.point = {(pa.x + pb.x) * 0.5f, (pa.y + pb.y) * 0.5f};
            return out;
        }
        poly.insert(poly.begin() + (std::ptrdiff_t)bi + 1, p);
    }
    return out;
}

}  // namespace okay
