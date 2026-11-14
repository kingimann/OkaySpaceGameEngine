#pragma once
// Convex-vs-convex contact: GJK overlap, EPA penetration.
// The Minkowski difference is A - B. The normal points from A toward B.
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Physics/TriMesh.hpp"

#include <cmath>
#include <utility>
#include <vector>

namespace okay {

/// A hit between two convex shapes. `normal` points from A toward B.
struct ConvexHit {
    bool  hit = false;
    Vec3  normal{0, 1, 0};
    float penetration = 0.0f;
    Vec3  point{0, 0, 0};   // world point between the two witness points
};

namespace convex_detail {

struct Sup {
    Vec3 w;   // point in A - B
    Vec3 a;   // witness on A
    Vec3 b;   // witness on B
};

template <class SA, class SB>
Sup Support(SA& sa, SB& sb, const Vec3& dir) {
    Vec3 d = dir.SqrMagnitude() > 1e-20f ? dir : Vec3{1, 0, 0};
    Sup s;
    s.a = sa(d);
    s.b = sb(-d);
    s.w = s.a - s.b;
    return s;
}

inline Vec3 ClosestOnSegmentToOrigin(const Vec3& a, const Vec3& b, float& t) {
    Vec3 ab = b - a;
    float l2 = ab.SqrMagnitude();
    t = l2 > 1e-20f ? Mathf::Clamp(Vec3::Dot(-a, ab) / l2, 0.0f, 1.0f) : 0.0f;
    return a + ab * t;
}

// Feature of the current simplex closest to the origin. Returns true when the
// origin lies in the simplex (the shapes overlap). Otherwise `dir` is a new
// search direction toward the origin and the simplex is reduced to that feature.
inline bool Evolve(Sup* s, int& n, Vec3& dir) {
    auto toward = [](const Vec3& from) {
        return from.SqrMagnitude() < 1e-16f ? Vec3{0, 1, 0} : -from;
    };
    if (n >= 4) {
        const Vec3 P[4] = {s[0].w, s[1].w, s[2].w, s[3].w};
        // Face i drops vertex i. Origin is outside a face when it and the
        // dropped vertex sit on opposite sides of the plane.
        int faces[4][3] = {{1, 2, 3}, {0, 2, 3}, {0, 1, 3}, {0, 1, 2}};
        int best = -1;
        float bestDist = 1e30f;
        Vec3 bestN{0, 1, 0};
        for (int f = 0; f < 4; ++f) {
            Vec3 a = P[faces[f][0]], b = P[faces[f][1]], c = P[faces[f][2]];
            Vec3 nrm = Vec3::Cross(b - a, c - a);
            float len = nrm.Magnitude();
            if (len < 1e-12f) continue;
            float so = Vec3::Dot(nrm, -a);
            float sd = Vec3::Dot(nrm, P[f] - a);
            if (so * sd >= -1e-10f) continue;          // origin is not outside this face
            float dist = std::fabs(so) / len;
            if (dist < bestDist) { bestDist = dist; best = f; bestN = nrm * (so > 0.0f ? 1.0f : -1.0f); }
        }
        if (best < 0) return true;                     // origin is inside the tetrahedron
        Sup tri[3] = {s[faces[best][0]], s[faces[best][1]], s[faces[best][2]]};
        s[0] = tri[0]; s[1] = tri[1]; s[2] = tri[2];
        n = 3;
        dir = bestN.SqrMagnitude() > 1e-16f ? bestN : toward(tri[0].w);
        return false;
    }
    if (n == 3) {
        Vec3 a = s[0].w, b = s[1].w, c = s[2].w;
        Vec3 cp = trimesh::ClosestPointOnTriangle(Vec3::Zero, a, b, c);
        if (cp.SqrMagnitude() < 1e-12f) return true;   // origin lies in the triangle
        dir = toward(cp);
        // Keep the feature the closest point sits on (vertex, edge, or the face).
        Vec3 v0 = b - a, v1 = c - a, v2 = cp - a;
        float d00 = Vec3::Dot(v0, v0), d01 = Vec3::Dot(v0, v1), d11 = Vec3::Dot(v1, v1);
        float d20 = Vec3::Dot(v2, v0), d21 = Vec3::Dot(v2, v1);
        float den = d00 * d11 - d01 * d01;
        float v = den > 1e-12f ? (d11 * d20 - d01 * d21) / den : 0.0f;
        float w = den > 1e-12f ? (d00 * d21 - d01 * d20) / den : 0.0f;
        float u = 1.0f - v - w;
        const float eps = 1e-4f;
        Sup keep[3]; int nk = 0;
        if (u > eps) keep[nk++] = s[0];
        if (v > eps) keep[nk++] = s[1];
        if (w > eps) keep[nk++] = s[2];
        if (nk == 0) { keep[nk++] = s[0]; }
        for (int i = 0; i < nk; ++i) s[i] = keep[i];
        n = nk;
        return false;
    }
    if (n == 2) {
        float t = 0.0f;
        Vec3 cp = ClosestOnSegmentToOrigin(s[0].w, s[1].w, t);
        if (cp.SqrMagnitude() < 1e-12f) return true;
        dir = toward(cp);
        if (t <= 1e-4f) n = 1;
        else if (t >= 1.0f - 1e-4f) { s[0] = s[1]; n = 1; }
        return false;
    }
    dir = toward(s[0].w);
    return s[0].w.SqrMagnitude() < 1e-12f;
}

struct Face {
    int i = 0, j = 0, k = 0;
    Vec3 n{0, 1, 0};
    float dist = 0.0f;
};

inline bool MakeFace(const std::vector<Sup>& v, int i, int j, int k, Face& f) {
    Vec3 a = v[i].w, b = v[j].w, c = v[k].w;
    Vec3 n = Vec3::Cross(b - a, c - a);
    float len = n.Magnitude();
    if (len < 1e-10f) return false;
    n = n * (1.0f / len);
    float dist = Vec3::Dot(n, a);
    if (dist < 0.0f) { n = -n; dist = -dist; std::swap(j, k); }
    f.i = i; f.j = j; f.k = k; f.n = n; f.dist = dist;
    return true;
}

inline void Barycentric(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& p,
                        float& u, float& v, float& w) {
    Vec3 v0 = b - a, v1 = c - a, v2 = p - a;
    float d00 = Vec3::Dot(v0, v0), d01 = Vec3::Dot(v0, v1), d11 = Vec3::Dot(v1, v1);
    float d20 = Vec3::Dot(v2, v0), d21 = Vec3::Dot(v2, v1);
    float den = d00 * d11 - d01 * d01;
    v = den > 1e-12f ? (d11 * d20 - d01 * d21) / den : 0.0f;
    w = den > 1e-12f ? (d00 * d21 - d01 * d20) / den : 0.0f;
    u = 1.0f - v - w;
    if (u < 0.0f) u = 0.0f;
    if (v < 0.0f) v = 0.0f;
    if (w < 0.0f) w = 0.0f;
    float s = u + v + w;
    if (s > 1e-8f) { u /= s; v /= s; w /= s; }
    else { u = v = w = 1.0f / 3.0f; }
}

inline ConvexHit HitFromFace(const std::vector<Sup>& verts, const Face& f) {
    ConvexHit h;
    if (!(f.dist > 1e-8f) || !std::isfinite(f.dist)) return h;
    const Sup& A = verts[f.i];
    const Sup& B = verts[f.j];
    const Sup& C = verts[f.k];
    float u, v, w;
    Barycentric(A.w, B.w, C.w, f.n * f.dist, u, v, w);
    Vec3 pa = A.a * u + B.a * v + C.a * w;
    Vec3 pb = A.b * u + B.b * v + C.b * w;
    h.hit = true;
    h.normal = f.n;
    h.penetration = f.dist;
    h.point = (pa + pb) * 0.5f;
    return h;
}

template <class SA, class SB>
ConvexHit Epa(SA& sa, SB& sb, Sup* seed, int nSeed) {
    std::vector<Sup> verts;
    verts.reserve(32);
    for (int i = 0; i < nSeed; ++i) verts.push_back(seed[i]);
    // A flat simplex (origin on a face) needs one more point so EPA has a volume.
    if ((int)verts.size() < 4) {
        const Vec3 axes[8] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0},
                              {0, 0, 1}, {0, 0, -1}, {1, 1, 1}, {-1, 1, -1}};
        for (const Vec3& ax : axes) {
            if ((int)verts.size() >= 3) {
                Sup p = Support(sa, sb, ax);
                float vol = Vec3::Dot(p.w - verts[0].w,
                                       Vec3::Cross(verts[1].w - verts[0].w, verts[2].w - verts[0].w));
                if (std::fabs(vol) > 1e-6f) { verts.push_back(p); break; }
            } else {
                verts.push_back(Support(sa, sb, ax));
            }
        }
    }
    ConvexHit none;
    if ((int)verts.size() < 4) return none;
    std::vector<Face> faces;
    int seedI[4][3] = {{0, 1, 2}, {0, 2, 3}, {0, 3, 1}, {1, 3, 2}};
    for (auto& ix : seedI) {
        Face f;
        if (MakeFace(verts, ix[0], ix[1], ix[2], f)) faces.push_back(f);
    }
    if (faces.size() < 4) return none;

    for (int iter = 0; iter < 32; ++iter) {
        int fi = 0;
        for (int i = 1; i < (int)faces.size(); ++i)
            if (faces[i].dist < faces[fi].dist) fi = i;
        const Face closest = faces[fi];
        Sup p = Support(sa, sb, closest.n);
        if (!std::isfinite(p.w.x + p.w.y + p.w.z)) return none;
        float proj = Vec3::Dot(p.w, closest.n);
        if (proj - closest.dist < 1e-4f || (int)verts.size() > 48) return HitFromFace(verts, closest);
        verts.push_back(p);
        int ni = (int)verts.size() - 1;
        std::vector<char> vis(faces.size(), 0);
        bool any = false;
        for (int i = 0; i < (int)faces.size(); ++i) {
            if (Vec3::Dot(faces[i].n, p.w) > faces[i].dist + 1e-5f) { vis[i] = 1; any = true; }
        }
        if (!any) return HitFromFace(verts, closest);
        std::vector<std::pair<int, int>> edges;
        for (int i = 0; i < (int)faces.size(); ++i) if (vis[i]) {
            edges.push_back({faces[i].i, faces[i].j});
            edges.push_back({faces[i].j, faces[i].k});
            edges.push_back({faces[i].k, faces[i].i});
        }
        std::vector<std::pair<int, int>> horizon;
        for (auto& e : edges) {
            bool rev = false;
            for (auto& f : edges) if (f.first == e.second && f.second == e.first) { rev = true; break; }
            if (!rev) horizon.push_back(e);
        }
        std::vector<Face> keep;
        keep.reserve(faces.size());
        for (int i = 0; i < (int)faces.size(); ++i) if (!vis[i]) keep.push_back(faces[i]);
        for (auto& e : horizon) {
            Face f;
            // Boundary edge was a->b on the visible face; the new face reverses it.
            if (MakeFace(verts, e.second, e.first, ni, f)) keep.push_back(f);
        }
        if (keep.empty()) return HitFromFace(verts, closest);
        faces.swap(keep);
    }
    int fi = 0;
    for (int i = 1; i < (int)faces.size(); ++i)
        if (faces[i].dist < faces[fi].dist) fi = i;
    return HitFromFace(verts, faces[fi]);
}

template <class SA, class SB>
ConvexHit Collide(SA supportA, SB supportB) {
    ConvexHit miss;
    Sup simplex[4];
    int n = 1;
    simplex[0] = Support(supportA, supportB, Vec3{1, 1, 1});
    Vec3 dir = -simplex[0].w;
    if (dir.SqrMagnitude() < 1e-16f) dir = {0, 1, 0};
    bool overlap = false;
    for (int iter = 0; iter < 32; ++iter) {
        Sup p = Support(supportA, supportB, dir);
        if (!std::isfinite(p.w.x + p.w.y + p.w.z)) return miss;
        if (Vec3::Dot(p.w, dir) < 0.0f) return miss;     // origin is outside the Minkowski difference
        if (n < 4) simplex[n++] = p;
        else simplex[3] = p;
        if (Evolve(simplex, n, dir)) { overlap = true; break; }
        if (dir.SqrMagnitude() < 1e-16f) { overlap = true; break; }
    }
    if (!overlap) return miss;
    return Epa(supportA, supportB, simplex, n);
}

} // namespace convex_detail

/// Penetration of convex A against convex B. Support functions return the
/// world-space point furthest along `dir` (dir is not necessarily unit length).
template <class SA, class SB>
ConvexHit ConvexContact(SA supportA, SB supportB) {
    return convex_detail::Collide(supportA, supportB);
}

} // namespace okay
