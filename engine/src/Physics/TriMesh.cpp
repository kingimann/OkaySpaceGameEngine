#include "okay/Physics/TriMesh.hpp"
#include "okay/Math/Mathf.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace okay {

namespace {
constexpr int kLeafSize = 4;

inline bool Finite3(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

inline float Axis(const Vec3& v, int a) { return a == 0 ? v.x : a == 1 ? v.y : v.z; }

// Slab test of a ray (o, d) against [mn, mx]; the entry t (>= 0) when it hits within maxT.
bool RayBox(const Vec3& o, const Vec3& d, const Vec3& mn, const Vec3& mx, float maxT, float& tEnter) {
    float t0 = 0.0f, t1 = maxT;
    for (int a = 0; a < 3; ++a) {
        float oa = Axis(o, a), da = Axis(d, a), lo = Axis(mn, a), hi = Axis(mx, a);
        if (std::fabs(da) < 1e-20f) {
            if (oa < lo || oa > hi) return false;
            continue;
        }
        float inv = 1.0f / da;
        float ta = (lo - oa) * inv, tb = (hi - oa) * inv;
        if (ta > tb) std::swap(ta, tb);
        if (ta > t0) t0 = ta;
        if (tb < t1) t1 = tb;
        if (t0 > t1) return false;
    }
    tEnter = t0;
    return true;
}

// Moller-Trumbore, two-sided. d need not be unit; t is in units of d.
bool RayTri(const Vec3& o, const Vec3& d, const Vec3& a, const Vec3& b, const Vec3& c, float& t) {
    Vec3 e1 = b - a, e2 = c - a;
    Vec3 p = Vec3::Cross(d, e2);
    float det = Vec3::Dot(e1, p);
    if (std::fabs(det) < 1e-30f) return false;
    float inv = 1.0f / det;
    Vec3 s = o - a;
    float u = Vec3::Dot(s, p) * inv;
    if (u < -1e-6f || u > 1.0f + 1e-6f) return false;
    Vec3 q = Vec3::Cross(s, e1);
    float v = Vec3::Dot(d, q) * inv;
    if (v < -1e-6f || u + v > 1.0f + 1e-6f) return false;
    t = Vec3::Dot(e2, q) * inv;
    return t >= 0.0f;
}
} // namespace

void TriMesh::Clear() {
    m_tri.clear(); m_nodes.clear();
    m_bmin = m_bmax = Vec3{0, 0, 0};
    m_dropped = 0;
}

void TriMesh::Build(const Mesh& mesh) {
    Clear();
    const int nv = static_cast<int>(mesh.vertices.size());
    const std::size_t nt = mesh.triangles.size() / 3;
    std::vector<Vec3> tris; tris.reserve(nt * 3);
    for (std::size_t t = 0; t < nt; ++t) {
        int i0 = mesh.triangles[3 * t], i1 = mesh.triangles[3 * t + 1], i2 = mesh.triangles[3 * t + 2];
        if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= nv || i1 >= nv || i2 >= nv) { ++m_dropped; continue; }
        const Vec3 &a = mesh.vertices[i0], &b = mesh.vertices[i1], &c = mesh.vertices[i2];
        if (!Finite3(a) || !Finite3(b) || !Finite3(c) ||
            Vec3::Cross(b - a, c - a).SqrMagnitude() <= 1e-24f) { ++m_dropped; continue; }
        tris.push_back(a); tris.push_back(b); tris.push_back(c);
    }
    const int n = static_cast<int>(tris.size() / 3);
    if (n == 0) return;
    std::vector<int> idx(n);
    std::vector<Vec3> cen(n);
    for (int i = 0; i < n; ++i) {
        idx[i] = i;
        cen[i] = (tris[3 * i] + tris[3 * i + 1] + tris[3 * i + 2]) * (1.0f / 3.0f);
    }
    m_nodes.reserve(static_cast<std::size_t>(n) / 2 + 2);
    m_nodes.emplace_back();
    // Iterative build: (node, lo, hi) ranges over idx.
    struct Job { int node, lo, hi; };
    std::vector<Job> jobs; jobs.push_back({0, 0, n});
    while (!jobs.empty()) {
        Job j = jobs.back(); jobs.pop_back();
        Vec3 mn{1e30f, 1e30f, 1e30f}, mx{-1e30f, -1e30f, -1e30f};
        Vec3 cmn = mn, cmx = mx;
        for (int k = j.lo; k < j.hi; ++k) {
            int ti = idx[k];
            for (int v = 0; v < 3; ++v) {
                const Vec3& p = tris[3 * ti + v];
                mn = {std::min(mn.x, p.x), std::min(mn.y, p.y), std::min(mn.z, p.z)};
                mx = {std::max(mx.x, p.x), std::max(mx.y, p.y), std::max(mx.z, p.z)};
            }
            const Vec3& c = cen[ti];
            cmn = {std::min(cmn.x, c.x), std::min(cmn.y, c.y), std::min(cmn.z, c.z)};
            cmx = {std::max(cmx.x, c.x), std::max(cmx.y, c.y), std::max(cmx.z, c.z)};
        }
        m_nodes[j.node].mn = mn; m_nodes[j.node].mx = mx;
        int count = j.hi - j.lo;
        Vec3 ext = cmx - cmn;
        int axis = ext.x >= ext.y && ext.x >= ext.z ? 0 : ext.y >= ext.z ? 1 : 2;
        if (count <= kLeafSize || Axis(ext, axis) <= 0.0f) {
            // (identical centroids can't be split further: keep them as one leaf)
            m_nodes[j.node].first = j.lo; m_nodes[j.node].count = count;
            continue;
        }
        int mid = j.lo + count / 2;
        std::nth_element(idx.begin() + j.lo, idx.begin() + mid, idx.begin() + j.hi,
                         [&](int x, int y) { return Axis(cen[x], axis) < Axis(cen[y], axis); });
        int left = static_cast<int>(m_nodes.size());
        m_nodes.emplace_back(); m_nodes.emplace_back();
        m_nodes[j.node].first = left; m_nodes[j.node].count = 0;
        jobs.push_back({left, j.lo, mid});
        jobs.push_back({left + 1, mid, j.hi});
    }
    m_tri.resize(static_cast<std::size_t>(n) * 3);
    for (int k = 0; k < n; ++k)
        for (int v = 0; v < 3; ++v) m_tri[3 * k + v] = tris[3 * idx[k] + v];
    m_bmin = m_nodes[0].mn; m_bmax = m_nodes[0].mx;
}

bool TriMesh::RayLocal(const Vec3& o, const Vec3& d, float maxT, float& t, int& tri) const {
    if (m_nodes.empty() || !(maxT >= 0.0f)) return false;
    float best = maxT; int bestTri = -1;
    float te;
    if (!RayBox(o, d, m_nodes[0].mn, m_nodes[0].mx, best, te)) return false;
    struct Item { int node; float t; };
    Item stack[64]; int sp = 0; stack[sp++] = {0, te};
    while (sp > 0) {
        Item it = stack[--sp];
        if (it.t > best) continue;
        const Node& n = m_nodes[it.node];
        if (n.count > 0) {
            for (int i = 0; i < n.count; ++i) {
                float tt;
                if (RayTri(o, d, m_tri[3 * (n.first + i)], m_tri[3 * (n.first + i) + 1],
                           m_tri[3 * (n.first + i) + 2], tt) && tt <= best) {
                    if (tt < best || bestTri < 0) { best = tt; bestTri = n.first + i; }
                }
            }
            continue;
        }
        float tl, tr;
        bool hl = RayBox(o, d, m_nodes[n.first].mn, m_nodes[n.first].mx, best, tl);
        bool hr = RayBox(o, d, m_nodes[n.first + 1].mn, m_nodes[n.first + 1].mx, best, tr);
        if (sp + 2 > 64) continue;
        if (hl && hr) {
            if (tl <= tr) { stack[sp++] = {n.first + 1, tr}; stack[sp++] = {n.first, tl}; }
            else          { stack[sp++] = {n.first, tl};     stack[sp++] = {n.first + 1, tr}; }
        } else if (hl) stack[sp++] = {n.first, tl};
        else if (hr)   stack[sp++] = {n.first + 1, tr};
    }
    if (bestTri < 0) return false;
    t = best; tri = bestTri;
    return true;
}

namespace trimesh {

Vec3 ClosestPointOnTriangle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c) {
    Vec3 ab = b - a, ac = c - a, ap = p - a;
    float d1 = Vec3::Dot(ab, ap), d2 = Vec3::Dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a;
    Vec3 bp = p - b;
    float d3 = Vec3::Dot(ab, bp), d4 = Vec3::Dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return b;
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) return a + ab * (d1 / (d1 - d3));
    Vec3 cp = p - c;
    float d5 = Vec3::Dot(ab, cp), d6 = Vec3::Dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return c;
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) return a + ac * (d2 / (d2 - d6));
    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
        return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    float denom = 1.0f / (va + vb + vc);
    return a + ab * (vb * denom) + ac * (vc * denom);
}

namespace {
Vec3 ClosestOnSeg(const Vec3& p, const Vec3& a, const Vec3& b) {
    Vec3 ab = b - a; float l2 = ab.SqrMagnitude();
    if (l2 < 1e-20f) return a;
    return a + ab * Mathf::Clamp(Vec3::Dot(p - a, ab) / l2, 0.0f, 1.0f);
}
// Closest points between segments p1-q1 and p2-q2 (Ericson).
void SegSeg(const Vec3& p1, const Vec3& q1, const Vec3& p2, const Vec3& q2, Vec3& c1, Vec3& c2) {
    Vec3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
    float a = d1.SqrMagnitude(), e = d2.SqrMagnitude(), f = Vec3::Dot(d2, r);
    float s, t;
    if (a <= 1e-20f && e <= 1e-20f) { c1 = p1; c2 = p2; return; }
    if (a <= 1e-20f) { s = 0.0f; t = Mathf::Clamp(f / e, 0.0f, 1.0f); }
    else {
        float c = Vec3::Dot(d1, r);
        if (e <= 1e-20f) { t = 0.0f; s = Mathf::Clamp(-c / a, 0.0f, 1.0f); }
        else {
            float b = Vec3::Dot(d1, d2);
            float denom = a * e - b * b;
            s = denom > 1e-20f ? Mathf::Clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0.0f) { t = 0.0f; s = Mathf::Clamp(-c / a, 0.0f, 1.0f); }
            else if (t > 1.0f) { t = 1.0f; s = Mathf::Clamp((b - c) / a, 0.0f, 1.0f); }
        }
    }
    c1 = p1 + d1 * s; c2 = p2 + d2 * t;
}
bool PointInTri(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& n) {
    return Vec3::Dot(Vec3::Cross(b - a, p - a), n) >= 0.0f &&
           Vec3::Dot(Vec3::Cross(c - b, p - b), n) >= 0.0f &&
           Vec3::Dot(Vec3::Cross(a - c, p - c), n) >= 0.0f;
}
} // namespace

WorldView::WorldView(const TriMesh& mesh, const Mat4& l2w) : m_mesh(&mesh), m_l2w(l2w) {
    const Vec3 ex = l2w.MultiplyVector({1, 0, 0}), ey = l2w.MultiplyVector({0, 1, 0}),
               ez = l2w.MultiplyVector({0, 0, 1});
    auto bad = [](const Vec3& v) { return !Finite3(v) || v.SqrMagnitude() < 1e-14f; };
    m_valid = !(bad(ex) || bad(ey) || bad(ez));
    if (m_valid) m_w2l = l2w.Inverse();
}

void WorldView::WorldBounds(Vec3& mn, Vec3& mx) const {
    mn = {1e30f, 1e30f, 1e30f}; mx = {-1e30f, -1e30f, -1e30f};
    if (!Valid()) { mn = mx = Vec3{0, 0, 0}; return; }
    const Vec3& lo = m_mesh->BoundsMin(); const Vec3& hi = m_mesh->BoundsMax();
    for (int i = 0; i < 8; ++i) {
        Vec3 p = m_l2w.MultiplyPoint({i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z});
        mn = {std::min(mn.x, p.x), std::min(mn.y, p.y), std::min(mn.z, p.z)};
        mx = {std::max(mx.x, p.x), std::max(mx.y, p.y), std::max(mx.z, p.z)};
    }
}

void WorldView::LocalBounds(const Vec3& wmn, const Vec3& wmx, Vec3& lmn, Vec3& lmx) const {
    lmn = {1e30f, 1e30f, 1e30f}; lmx = {-1e30f, -1e30f, -1e30f};
    for (int i = 0; i < 8; ++i) {
        Vec3 p = m_w2l.MultiplyPoint({i & 1 ? wmx.x : wmn.x, i & 2 ? wmx.y : wmn.y, i & 4 ? wmx.z : wmn.z});
        lmn = {std::min(lmn.x, p.x), std::min(lmn.y, p.y), std::min(lmn.z, p.z)};
        lmx = {std::max(lmx.x, p.x), std::max(lmx.y, p.y), std::max(lmx.z, p.z)};
    }
    // Float slack so a triangle touching the box edge isn't culled by rounding.
    Vec3 pad = (lmx - lmn) * 1e-4f + Vec3(1e-5f, 1e-5f, 1e-5f);
    lmn = lmn - pad; lmx = lmx + pad;
}

void WorldView::WorldTri(int i, Vec3& a, Vec3& b, Vec3& c) const {
    Vec3 la, lb, lc; m_mesh->Triangle(i, la, lb, lc);
    a = m_l2w.MultiplyPoint(la); b = m_l2w.MultiplyPoint(lb); c = m_l2w.MultiplyPoint(lc);
}

bool WorldView::Raycast(const Vec3& origin, const Vec3& dir, float maxT, float& t, Vec3& normal) const {
    if (!Valid()) return false;
    Vec3 lo = m_w2l.MultiplyPoint(origin), ld = m_w2l.MultiplyVector(dir);
    int tri;
    if (!m_mesh->RayLocal(lo, ld, maxT, t, tri)) return false;
    Vec3 a, b, c; WorldTri(tri, a, b, c);
    Vec3 n = Vec3::Cross(b - a, c - a);
    float l = n.Magnitude();
    n = l > 1e-30f ? n * (1.0f / l) : Vec3{0, 1, 0};
    if (Vec3::Dot(n, dir) > 0.0f) n = n * -1.0f;      // face the ray's origin
    normal = n;
    return true;
}

Contact WorldView::Sphere(const Vec3& c, float r) const {
    Contact best;
    if (!Valid() || !(r >= 0.0f)) return best;
    Vec3 lmn, lmx; LocalBounds(c - Vec3(r, r, r), c + Vec3(r, r, r), lmn, lmx);
    float bestDist = r;   // must be strictly inside to count as deeper
    m_mesh->QueryAABB(lmn, lmx, [&](int i) {
        Vec3 a, b, cc; WorldTri(i, a, b, cc);
        Vec3 cp = ClosestPointOnTriangle(c, a, b, cc);
        Vec3 d = c - cp; float dist = d.Magnitude();
        if (dist > r || (best.hit && dist >= bestDist)) return;
        bestDist = dist; best.hit = true; best.point = cp; best.penetration = r - dist;
        if (dist > 1e-6f) best.normal = d * (1.0f / dist);
        else {
            Vec3 n = Vec3::Cross(b - a, cc - a).Normalized();
            if (Vec3::Dot(n, c - (a + b + cc) * (1.0f / 3.0f)) < 0.0f) n = n * -1.0f;
            best.normal = n;
        }
    });
    return best;
}

Contact WorldView::Capsule(const Vec3& a, const Vec3& b, float r) const {
    Contact best;
    if (!Valid() || !(r >= 0.0f)) return best;
    Vec3 mn{std::min(a.x, b.x) - r, std::min(a.y, b.y) - r, std::min(a.z, b.z) - r};
    Vec3 mx{std::max(a.x, b.x) + r, std::max(a.y, b.y) + r, std::max(a.z, b.z) + r};
    Vec3 lmn, lmx; LocalBounds(mn, mx, lmn, lmx);
    float bestPen = 0.0f;
    m_mesh->QueryAABB(lmn, lmx, [&](int i) {
        Vec3 t0, t1, t2; WorldTri(i, t0, t1, t2);
        Vec3 n = Vec3::Cross(t1 - t0, t2 - t0);
        float nl = n.Magnitude(); if (nl < 1e-15f) return;
        n = n * (1.0f / nl);
        float da = Vec3::Dot(a - t0, n), db = Vec3::Dot(b - t0, n);
        Vec3 onSeg, onTri; float dist; bool crossing = false;
        if (da * db < 0.0f) {
            Vec3 p = a + (b - a) * (da / (da - db));
            if (PointInTri(p, t0, t1, t2, n)) { crossing = true; onSeg = onTri = p; dist = 0.0f; }
        }
        if (!crossing) {
            float bd = 1e30f;
            auto consider = [&](const Vec3& s, const Vec3& q) {
                float d2 = (s - q).SqrMagnitude();
                if (d2 < bd) { bd = d2; onSeg = s; onTri = q; }
            };
            consider(a, ClosestPointOnTriangle(a, t0, t1, t2));
            consider(b, ClosestPointOnTriangle(b, t0, t1, t2));
            const Vec3* e[3] = {&t0, &t1, &t2};
            for (int k = 0; k < 3; ++k) {
                Vec3 s, q; SegSeg(a, b, *e[k], *e[(k + 1) % 3], s, q); consider(s, q);
            }
            dist = std::sqrt(bd);
            if (dist > r) return;
        }
        float pen; Vec3 nrm;
        if (dist > 1e-6f) { nrm = (onSeg - onTri) * (1.0f / dist); pen = r - dist; }
        else {
            // The axis pierces the triangle: leave by whichever face is closer.
            float up = r + std::max(-da, -db), down = r + std::max(da, db);
            if (up <= down) { nrm = n; pen = up; } else { nrm = n * -1.0f; pen = down; }
        }
        if (pen > bestPen) { bestPen = pen; best.hit = true; best.normal = nrm; best.point = onTri; best.penetration = pen; }
    });
    return best;
}

Contact WorldView::Box(const Vec3& c, const Vec3& h, const Vec3& ax, const Vec3& ay, const Vec3& az) const {
    Contact best;
    if (!Valid()) return best;
    Vec3 e{std::fabs(ax.x) * h.x + std::fabs(ay.x) * h.y + std::fabs(az.x) * h.z,
           std::fabs(ax.y) * h.x + std::fabs(ay.y) * h.y + std::fabs(az.y) * h.z,
           std::fabs(ax.z) * h.x + std::fabs(ay.z) * h.y + std::fabs(az.z) * h.z};
    Vec3 lmn, lmx; LocalBounds(c - e, c + e, lmn, lmx);
    const Vec3 bax[3] = {ax, ay, az};
    float bestPen = 0.0f;
    m_mesh->QueryAABB(lmn, lmx, [&](int i) {
        Vec3 w0, w1, w2; WorldTri(i, w0, w1, w2);
        Vec3 v[3] = {w0 - c, w1 - c, w2 - c};
        Vec3 ed[3] = {v[1] - v[0], v[2] - v[1], v[0] - v[2]};
        Vec3 axes[13]; int na = 0;
        axes[na++] = Vec3::Cross(ed[0], ed[1]);
        for (int k = 0; k < 3; ++k) axes[na++] = bax[k];
        for (int k = 0; k < 3; ++k) for (int j = 0; j < 3; ++j) axes[na++] = Vec3::Cross(ed[k], bax[j]);
        float minPen = 1e30f; Vec3 minN{0, 1, 0};
        for (int k = 0; k < na; ++k) {
            float l2 = axes[k].SqrMagnitude();
            if (l2 < 1e-16f) continue;
            Vec3 L = axes[k] * (1.0f / std::sqrt(l2));
            float p0 = Vec3::Dot(v[0], L), p1 = Vec3::Dot(v[1], L), p2 = Vec3::Dot(v[2], L);
            float lo = std::min(p0, std::min(p1, p2)), hi = std::max(p0, std::max(p1, p2));
            float rad = h.x * std::fabs(Vec3::Dot(ax, L)) + h.y * std::fabs(Vec3::Dot(ay, L)) + h.z * std::fabs(Vec3::Dot(az, L));
            if (hi < -rad || lo > rad) { minPen = -1.0f; break; }           // separating axis
            float bias = k >= 4 ? 1e-4f : 0.0f;                              // prefer face axes on ties
            float up = hi + rad + bias, down = rad - lo + bias;               // push the box +L or -L
            if (up < minPen) { minPen = up; minN = L; }
            if (down < minPen) { minPen = down; minN = L * -1.0f; }
        }
        if (minPen <= 0.0f || minPen > 1e29f) return;
        if (!best.hit || minPen > bestPen) {
            bestPen = minPen; best.hit = true; best.normal = minN; best.penetration = minPen;
            best.point = ClosestPointOnTriangle(c, w0, w1, w2);
        }
    });
    return best;
}

} // namespace trimesh
} // namespace okay
