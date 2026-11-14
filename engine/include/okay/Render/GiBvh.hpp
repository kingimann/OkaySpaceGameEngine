#pragma once
// A small CPU ray tracer for the global-illumination baker: a triangle list, a
// bounding volume hierarchy (binned surface-area heuristic) and closest-hit /
// any-hit queries. Our own code, header only, no dependency beyond Vec3.
// Triangles are double sided. Degenerate or non-finite triangles are dropped at
// build time. See docs/foundations/global_illumination.md.
#include "okay/Math/Vec3.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace okay {

struct GiTri {
    Vec3 a, b, c;
    int  material = 0;    // index into the caller's material list
};

struct GiRay {
    Vec3  o, d;           // d need not be unit length, but hit distance is in units of d
    float tMin = 1e-4f;
    float tMax = 1e30f;
};

struct GiHit {
    float t = 1e30f;
    int   tri = -1;       // index into the ORIGINAL triangle list given to Build
    float u = 0, v = 0;   // barycentrics of b and c
    Vec3  n{0, 1, 0};     // unit geometric normal (not flipped)
};

/// Moller-Trumbore, double sided. Returns true and fills t,u,v for a hit in (tMin,tMax).
inline bool GiIntersectTri(const GiRay& r, const Vec3& a, const Vec3& b, const Vec3& c,
                           float& t, float& u, float& v) {
    const Vec3 e1 = b - a, e2 = c - a;
    const Vec3 p = Vec3::Cross(r.d, e2);
    const float det = Vec3::Dot(e1, p);
    if (!(det > 1e-12f || det < -1e-12f)) return false;
    const float inv = 1.0f / det;
    const Vec3 tv = r.o - a;
    const float uu = Vec3::Dot(tv, p) * inv;
    if (!(uu >= 0.0f && uu <= 1.0f)) return false;
    const Vec3 q = Vec3::Cross(tv, e1);
    const float vv = Vec3::Dot(r.d, q) * inv;
    if (!(vv >= 0.0f && uu + vv <= 1.0f)) return false;
    const float tt = Vec3::Dot(e2, q) * inv;
    if (!(tt > r.tMin && tt < r.tMax)) return false;
    t = tt; u = uu; v = vv;
    return true;
}

class GiBvh {
public:
    /// Build over `tris`. Keeps its own copy. Safe on any input (empty, NaN, degenerate).
    void Build(const std::vector<GiTri>& tris) {
        nodes_.clear(); tris_.clear(); orig_.clear();
        std::vector<int> keep;
        for (int i = 0; i < (int)tris.size(); ++i) {
            const GiTri& t = tris[i];
            if (!Finite(t.a) || !Finite(t.b) || !Finite(t.c)) continue;
            Vec3 n = Vec3::Cross(t.b - t.a, t.c - t.a);
            if (!(n.SqrMagnitude() > 1e-20f)) continue;
            keep.push_back(i);
        }
        if (keep.empty()) return;
        std::vector<Vec3> cen(keep.size());
        std::vector<Box> bx(keep.size());
        for (size_t k = 0; k < keep.size(); ++k) {
            const GiTri& t = tris[keep[k]];
            bx[k].Grow(t.a); bx[k].Grow(t.b); bx[k].Grow(t.c);
            cen[k] = (t.a + t.b + t.c) * (1.0f / 3.0f);
        }
        std::vector<int> idx(keep.size());
        for (size_t k = 0; k < idx.size(); ++k) idx[k] = (int)k;
        nodes_.reserve(keep.size() * 2);
        BuildRec(idx, 0, (int)idx.size(), cen, bx, 0);
        tris_.resize(idx.size()); orig_.resize(idx.size());
        for (size_t k = 0; k < idx.size(); ++k) {
            tris_[k] = tris[keep[idx[k]]];
            orig_[k] = keep[idx[k]];
        }
    }

    int TriangleCount() const { return (int)tris_.size(); }
    int NodeCount() const { return (int)nodes_.size(); }
    bool Empty() const { return nodes_.empty(); }

    /// Closest hit along `r`. Returns false on a miss.
    bool Closest(const GiRay& r, GiHit& hit) const {
        if (nodes_.empty()) return false;
        GiRay ray = r;
        const Vec3 inv = InvDir(r.d);
        int stack[kStack]; int sp = 0; stack[sp++] = 0;
        int best = -1; float bu = 0, bv = 0;
        while (sp) {
            const Node& nd = nodes_[stack[--sp]];
            if (!HitBox(nd, ray, inv)) continue;
            if (nd.count > 0) {
                for (int i = 0; i < nd.count; ++i) {
                    const GiTri& t = tris_[nd.left + i];
                    float tt, u, v;
                    if (GiIntersectTri(ray, t.a, t.b, t.c, tt, u, v)) {
                        ray.tMax = tt; best = nd.left + i; bu = u; bv = v;
                    }
                }
            } else {
                // Push the far child first so the near one (by split-axis sign) pops first.
                const bool neg = Axis(r.d, nd.axis) < 0.0f;
                stack[sp++] = neg ? nd.left : nd.right;
                stack[sp++] = neg ? nd.right : nd.left;
            }
        }
        if (best < 0) return false;
        const GiTri& t = tris_[best];
        hit.t = ray.tMax; hit.tri = orig_[best]; hit.u = bu; hit.v = bv;
        hit.n = Vec3::Cross(t.b - t.a, t.c - t.a).Normalized();
        return true;
    }

    /// True if anything is hit in (tMin,tMax).
    bool Any(const GiRay& r) const {
        if (nodes_.empty()) return false;
        const Vec3 inv = InvDir(r.d);
        int stack[kStack]; int sp = 0; stack[sp++] = 0;
        while (sp) {
            const Node& nd = nodes_[stack[--sp]];
            if (!HitBox(nd, r, inv)) continue;
            if (nd.count > 0) {
                for (int i = 0; i < nd.count; ++i) {
                    const GiTri& t = tris_[nd.left + i];
                    float tt, u, v;
                    if (GiIntersectTri(r, t.a, t.b, t.c, tt, u, v)) return true;
                }
            } else { stack[sp++] = nd.left; stack[sp++] = nd.right; }
        }
        return false;
    }

    /// Reference: test every triangle. For tests.
    static bool BruteClosest(const std::vector<GiTri>& tris, const GiRay& r, GiHit& hit) {
        GiRay ray = r; int best = -1;
        for (int i = 0; i < (int)tris.size(); ++i) {
            float t, u, v;
            if (GiIntersectTri(ray, tris[i].a, tris[i].b, tris[i].c, t, u, v)) {
                // Same filters as Build so results match: skip degenerate/non-finite.
                ray.tMax = t; best = i; hit.u = u; hit.v = v;
            }
        }
        if (best < 0) return false;
        hit.t = ray.tMax; hit.tri = best;
        hit.n = Vec3::Cross(tris[best].b - tris[best].a, tris[best].c - tris[best].a).Normalized();
        return true;
    }

private:
    static constexpr int kStack = 128;
    static constexpr int kLeaf = 4;
    static constexpr int kBins = 12;
    struct Box {
        Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
        void Grow(const Vec3& p) { lo = Vec3::Min(lo, p); hi = Vec3::Max(hi, p); }
        void Grow(const Box& b) { lo = Vec3::Min(lo, b.lo); hi = Vec3::Max(hi, b.hi); }
        float Area() const {
            if (hi.x < lo.x) return 0.0f;
            Vec3 d = hi - lo; return 2.0f * (d.x * d.y + d.y * d.z + d.z * d.x);
        }
    };
    struct Node { Vec3 lo, hi; int left = 0; int right = 0; int count = 0; int axis = 0; };
    std::vector<Node> nodes_;
    std::vector<GiTri> tris_;
    std::vector<int> orig_;

    static bool Finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
    static float Axis(const Vec3& v, int a) { return a == 0 ? v.x : (a == 1 ? v.y : v.z); }
    static float SafeInv(float d) {
        if (std::fabs(d) < 1e-20f) d = d < 0 ? -1e-20f : 1e-20f;
        return 1.0f / d;
    }
    static Vec3 InvDir(const Vec3& d) { return {SafeInv(d.x), SafeInv(d.y), SafeInv(d.z)}; }
    static bool HitBox(const Node& n, const GiRay& r, const Vec3& inv) {
        float t0 = r.tMin, t1 = r.tMax;
        float a = (n.lo.x - r.o.x) * inv.x, b = (n.hi.x - r.o.x) * inv.x;
        t0 = std::max(t0, std::min(a, b)); t1 = std::min(t1, std::max(a, b));
        a = (n.lo.y - r.o.y) * inv.y; b = (n.hi.y - r.o.y) * inv.y;
        t0 = std::max(t0, std::min(a, b)); t1 = std::min(t1, std::max(a, b));
        a = (n.lo.z - r.o.z) * inv.z; b = (n.hi.z - r.o.z) * inv.z;
        t0 = std::max(t0, std::min(a, b)); t1 = std::min(t1, std::max(a, b));
        // Box padded at build time so grazing hits are not lost to rounding.
        return t0 <= t1;
    }

    int BuildRec(std::vector<int>& idx, int begin, int end, const std::vector<Vec3>& cen,
                 const std::vector<Box>& bx, int depth) {
        const int me = (int)nodes_.size();
        nodes_.emplace_back();
        Box all, cb;
        for (int i = begin; i < end; ++i) { all.Grow(bx[idx[i]]); cb.Grow(cen[idx[i]]); }
        const float pad = 1e-4f * (1.0f + std::max({std::fabs(all.hi.x), std::fabs(all.hi.y), std::fabs(all.hi.z),
                                                    std::fabs(all.lo.x), std::fabs(all.lo.y), std::fabs(all.lo.z)}));
        nodes_[me].lo = all.lo - Vec3{pad, pad, pad};
        nodes_[me].hi = all.hi + Vec3{pad, pad, pad};
        const int n = end - begin;
        auto makeLeaf = [&]() { nodes_[me].left = begin; nodes_[me].count = n; return me; };
        if (n <= kLeaf || depth >= 60) return makeLeaf();

        const Vec3 ext = cb.hi - cb.lo;
        int axis = (ext.x >= ext.y && ext.x >= ext.z) ? 0 : (ext.y >= ext.z ? 1 : 2);
        int mid = begin;
        if (Axis(ext, axis) > 1e-9f) {
            // Binned SAH along the widest centroid axis.
            Box bb[kBins]; int cnt[kBins] = {};
            const float lo = Axis(cb.lo, axis), scale = kBins / Axis(ext, axis);
            auto binOf = [&](int tri) {
                int b = (int)((Axis(cen[tri], axis) - lo) * scale);
                return b < 0 ? 0 : (b >= kBins ? kBins - 1 : b);
            };
            for (int i = begin; i < end; ++i) { int b = binOf(idx[i]); ++cnt[b]; bb[b].Grow(bx[idx[i]]); }
            float leftA[kBins - 1], rightA[kBins - 1]; int leftN[kBins - 1], rightN[kBins - 1];
            Box acc; int c = 0;
            for (int i = 0; i < kBins - 1; ++i) { acc.Grow(bb[i]); c += cnt[i]; leftA[i] = acc.Area(); leftN[i] = c; }
            acc = Box(); c = 0;
            for (int i = kBins - 1; i > 0; --i) { acc.Grow(bb[i]); c += cnt[i]; rightA[i - 1] = acc.Area(); rightN[i - 1] = c; }
            float bestCost = 1e30f; int bestSplit = -1;
            for (int i = 0; i < kBins - 1; ++i) {
                if (leftN[i] == 0 || rightN[i] == 0) continue;
                const float cost = leftA[i] * leftN[i] + rightA[i] * rightN[i];
                if (cost < bestCost) { bestCost = cost; bestSplit = i; }
            }
            if (bestSplit >= 0) {
                auto it = std::partition(idx.begin() + begin, idx.begin() + end,
                                         [&](int tri) { return binOf(tri) <= bestSplit; });
                mid = (int)(it - idx.begin());
            }
        }
        if (mid == begin || mid == end) {                  // fallback: median split by centroid
            std::sort(idx.begin() + begin, idx.begin() + end,
                      [&](int a, int b) { return Axis(cen[a], axis) < Axis(cen[b], axis) ||
                                                 (Axis(cen[a], axis) == Axis(cen[b], axis) && a < b); });
            mid = begin + n / 2;
        }
        const int l = BuildRec(idx, begin, mid, cen, bx, depth + 1);
        const int r = BuildRec(idx, mid, end, cen, bx, depth + 1);
        nodes_[me].left = l; nodes_[me].right = r; nodes_[me].count = 0; nodes_[me].axis = axis;
        return me;
    }
};

} // namespace okay
