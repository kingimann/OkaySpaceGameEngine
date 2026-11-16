#pragma once
// Contact points between two oriented boxes: the whole touching face, not one corner.
//
// Written from the usual separating-axis idea and the face-clipping idea described in "Real-Time Collision Detection"
// (C. Ericson, 2005, chapter 5): find the axis of least overlap among the 15 candidate axes (three faces of each box and
// the nine edge pairs); when it is a face, clip the other box's most facing face against the side planes of that
// reference face (Sutherland-Hodgman) and keep up to four of the clipped corners; when it is an edge pair, use the closest
// points of the two edges. Nothing is copied from any engine; the constants (the edge-axis preference, the reduction to
// four points) were worked out here and are checked by tests/test_physics_deep_manifold.cpp.
//
// Header only, no scene types, so it can be tested on its own.
#include "okay/Math/Vec3.hpp"
#include <cmath>

namespace okay {
namespace boxmanifold {

/// An oriented box: centre, half sizes and its three unit axes (world space).
struct Box {
    Vec3 c{0, 0, 0}, h{0.5f, 0.5f, 0.5f};
    Vec3 ax[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    float HalfAlong(int i) const { return i == 0 ? h.x : i == 1 ? h.y : h.z; }
    /// Half the width of the box along the unit direction L.
    float Radius(const Vec3& L) const {
        return h.x * std::fabs(Vec3::Dot(ax[0], L)) + h.y * std::fabs(Vec3::Dot(ax[1], L)) + h.z * std::fabs(Vec3::Dot(ax[2], L));
    }
    Vec3 Support(const Vec3& d) const {
        Vec3 p = c;
        for (int i = 0; i < 3; ++i) p = p + ax[i] * (Vec3::Dot(d, ax[i]) >= 0.0f ? HalfAlong(i) : -HalfAlong(i));
        return p;
    }
};

struct Point {
    Vec3 pos{0, 0, 0};     // between the two surfaces
    float depth = 0.0f;    // > 0: the boxes overlap here; < 0: a gap of -depth (inside the skin)
};

struct Result {
    bool hit = false;
    Vec3 normal{0, 1, 0};  // unit, from A toward B
    int count = 0;
    Point pts[4];
    bool edgeEdge = false;
    float deepest = 0.0f;  // the largest depth among the points
};

namespace detail {

inline void ClosestOnSegments(const Vec3& p1, const Vec3& d1, float len1, const Vec3& p2, const Vec3& d2, float len2,
                              Vec3& c1, Vec3& c2) {
    // Two segments p + d t, t in [0, len] (d unit).
    Vec3 r = p1 - p2;
    float a = 1.0f, e = 1.0f, b = Vec3::Dot(d1, d2), c = Vec3::Dot(d1, r), f = Vec3::Dot(d2, r);
    float denom = a * e - b * b;
    float s = denom > 1e-8f ? (b * f - c * e) / denom : 0.0f;
    s = s < 0.0f ? 0.0f : (s > len1 ? len1 : s);
    float t = b * s + f;
    if (t < 0.0f) { t = 0.0f; s = -c; s = s < 0.0f ? 0.0f : (s > len1 ? len1 : s); }
    else if (t > len2) { t = len2; s = b * t - c; s = s < 0.0f ? 0.0f : (s > len1 ? len1 : s); }
    c1 = p1 + d1 * s;
    c2 = p2 + d2 * t;
}

/// Keep at most four of `n` points: the deepest, the one farthest from it, then the two that make the largest area.
inline int ReduceToFour(Point* p, int n) {
    if (n <= 4) return n;
    int i0 = 0;
    for (int i = 1; i < n; ++i) if (p[i].depth > p[i0].depth) i0 = i;
    int i1 = -1; float best = -1.0f;
    for (int i = 0; i < n; ++i) { if (i == i0) continue; float d = (p[i].pos - p[i0].pos).SqrMagnitude(); if (d > best) { best = d; i1 = i; } }
    Vec3 e = p[i1].pos - p[i0].pos;
    int i2 = -1, i3 = -1; float bestPos = 0.0f, bestNeg = 0.0f;
    Vec3 axis{0, 0, 0};
    for (int i = 0; i < n; ++i) {   // a normal for the side test: from the first three points that are not collinear
        if (i == i0 || i == i1) continue;
        Vec3 cr = Vec3::Cross(e, p[i].pos - p[i0].pos);
        if (cr.SqrMagnitude() > axis.SqrMagnitude()) axis = cr;
    }
    for (int i = 0; i < n; ++i) {
        if (i == i0 || i == i1) continue;
        float s = Vec3::Dot(Vec3::Cross(e, p[i].pos - p[i0].pos), axis);
        if (s > bestPos) { bestPos = s; i2 = i; }
        if (s < bestNeg) { bestNeg = s; i3 = i; }
    }
    Point out[4]; int m = 0;
    out[m++] = p[i0]; out[m++] = p[i1];
    if (i2 >= 0) out[m++] = p[i2];
    if (i3 >= 0) out[m++] = p[i3];
    // Fewer than four distinct picks (all on one side): fill with the deepest remaining.
    for (int fill = 0; m < 4 && fill < n; ++fill) {
        int pick = -1; float bd = -1e30f;
        for (int i = 0; i < n; ++i) {
            bool used = false;
            for (int k = 0; k < m; ++k) if ((out[k].pos - p[i].pos).SqrMagnitude() < 1e-12f) { used = true; break; }
            if (!used && p[i].depth > bd) { bd = p[i].depth; pick = i; }
        }
        if (pick < 0) break;
        out[m++] = p[pick];
    }
    for (int i = 0; i < m; ++i) p[i] = out[i];
    return m;
}

} // namespace detail

/// Contact between boxes A and B. `skin` lets boxes that are apart by less than that still touch (negative depth), so a
/// body at rest stays in contact. Returns hit = false when they are farther apart than `skin` along some axis.
inline Result Collide(const Box& A, const Box& B, float skin) {
    Result out;
    Vec3 axes[15];
    int n = 0;
    for (int i = 0; i < 3; ++i) axes[n++] = A.ax[i];
    for (int i = 0; i < 3; ++i) axes[n++] = B.ax[i];
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) axes[n++] = Vec3::Cross(A.ax[i], B.ax[j]);
    const Vec3 d = B.c - A.c;
    float bestFace = 1e30f, bestEdge = 1e30f; int faceIdx = -1, edgeIdx = -1;
    float overlaps[15];
    for (int i = 0; i < 15; ++i) {
        float len2 = axes[i].SqrMagnitude();
        overlaps[i] = 1e30f;
        if (len2 < 1e-8f) continue;                                    // two parallel edges: no axis
        Vec3 L = axes[i] * (1.0f / std::sqrt(len2));
        float ov = A.Radius(L) + B.Radius(L) - std::fabs(Vec3::Dot(d, L));
        if (ov < -skin) return out;                                    // a separating axis wider than the skin
        overlaps[i] = ov;
        axes[i] = L;
        if (i < 6) { if (ov < bestFace) { bestFace = ov; faceIdx = i; } }
        else       { if (ov < bestEdge) { bestEdge = ov; edgeIdx = i; } }
    }
    if (faceIdx < 0) return out;                                       // no face axis (cannot happen for real boxes)
    // Prefer a face unless an edge pair is clearly better (so a box resting flat is never read as an edge on edge).
    bool useEdge = edgeIdx >= 0 && bestEdge < bestFace - (0.004f + 0.05f * std::fabs(bestFace));
    out.hit = true;
    if (!useEdge) {
        Vec3 L = axes[faceIdx];
        if (Vec3::Dot(d, L) < 0.0f) L = L * -1.0f;                     // from A toward B
        out.normal = L;
        const bool refIsA = faceIdx < 3;
        const Box& R = refIsA ? A : B;       // reference box (owns the face axis)
        const Box& I = refIsA ? B : A;       // incident box
        const Vec3 nr = refIsA ? L : L * -1.0f;   // reference face normal, pointing toward the incident box
        const int ri = refIsA ? faceIdx : faceIdx - 3;
        // The reference face: centre and its two side directions.
        const Vec3 faceCenter = R.c + nr * R.HalfAlong(ri);
        const int u = (ri + 1) % 3, v = (ri + 2) % 3;
        // The incident face: the face of I whose normal points most against nr.
        int ii = 0; float worst = 1e30f; float sgn = 1.0f;
        for (int k = 0; k < 3; ++k) {
            float dp = Vec3::Dot(I.ax[k], nr);
            if (dp < worst) { worst = dp; ii = k; sgn = 1.0f; }
            if (-dp < worst) { worst = -dp; ii = k; sgn = -1.0f; }
        }
        const int iu = (ii + 1) % 3, iv = (ii + 2) % 3;
        const Vec3 ic = I.c + I.ax[ii] * (sgn * I.HalfAlong(ii));
        Vec3 poly[16]; int pn = 4;
        poly[0] = ic + I.ax[iu] * I.HalfAlong(iu) + I.ax[iv] * I.HalfAlong(iv);
        poly[1] = ic - I.ax[iu] * I.HalfAlong(iu) + I.ax[iv] * I.HalfAlong(iv);
        poly[2] = ic - I.ax[iu] * I.HalfAlong(iu) - I.ax[iv] * I.HalfAlong(iv);
        poly[3] = ic + I.ax[iu] * I.HalfAlong(iu) - I.ax[iv] * I.HalfAlong(iv);
        // Clip against the four side planes of the reference face (inside: |dot(p - faceCenter, side)| <= half).
        for (int plane = 0; plane < 4 && pn > 0; ++plane) {
            const Vec3& side = R.ax[plane < 2 ? u : v];
            const float half = R.HalfAlong(plane < 2 ? u : v);
            const float sign = (plane & 1) ? -1.0f : 1.0f;
            Vec3 next[16]; int nn = 0;
            for (int k = 0; k < pn; ++k) {
                const Vec3& p = poly[k];
                const Vec3& q = poly[(k + 1) % pn];
                float dp = sign * Vec3::Dot(p - faceCenter, side) - half;   // <= 0 inside
                float dq = sign * Vec3::Dot(q - faceCenter, side) - half;
                if (dp <= 0.0f) { if (nn < 16) next[nn++] = p; }
                if ((dp < 0.0f && dq > 0.0f) || (dp > 0.0f && dq < 0.0f)) {
                    float t = dp / (dp - dq);
                    if (nn < 16) next[nn++] = p + (q - p) * t;
                }
            }
            for (int k = 0; k < nn; ++k) poly[k] = next[k];
            pn = nn;
        }
        Point pts[16]; int m = 0;
        for (int k = 0; k < pn; ++k) {
            float depth = -Vec3::Dot(poly[k] - faceCenter, nr);     // below the reference face = overlap
            if (depth < -skin) continue;
            pts[m].depth = depth;
            pts[m].pos = poly[k] + nr * (depth * 0.5f);              // halfway between the two surfaces
            ++m;
        }
        if (m == 0) {   // the faces just miss each other sideways but the boxes touch: one point at the deepest corner
            Vec3 s = I.Support(nr * -1.0f);
            float depth = -Vec3::Dot(s - faceCenter, nr);
            pts[0].depth = depth; pts[0].pos = s + nr * (depth * 0.5f); m = 1;
        }
        m = detail::ReduceToFour(pts, m);
        out.count = m;
        out.deepest = -1e30f;
        for (int k = 0; k < m; ++k) { out.pts[k] = pts[k]; if (pts[k].depth > out.deepest) out.deepest = pts[k].depth; }
        return out;
    }
    // Edge on edge.
    Vec3 L = axes[edgeIdx];
    if (Vec3::Dot(d, L) < 0.0f) L = L * -1.0f;
    out.normal = L; out.edgeEdge = true;
    const int ia = (edgeIdx - 6) / 3, ib = (edgeIdx - 6) % 3;
    Vec3 pa = A.c, pb = B.c;
    for (int k = 0; k < 3; ++k) {
        if (k != ia) pa = pa + A.ax[k] * (Vec3::Dot(A.ax[k], L) >= 0.0f ? A.HalfAlong(k) : -A.HalfAlong(k));
        if (k != ib) pb = pb + B.ax[k] * (Vec3::Dot(B.ax[k], L) >= 0.0f ? -B.HalfAlong(k) : B.HalfAlong(k));
    }
    Vec3 da = A.ax[ia], db = B.ax[ib];
    float la = A.HalfAlong(ia), lb = B.HalfAlong(ib);
    Vec3 c1, c2;
    detail::ClosestOnSegments(pa - da * la, da, 2.0f * la, pb - db * lb, db, 2.0f * lb, c1, c2);
    out.count = 1;
    out.pts[0].pos = (c1 + c2) * 0.5f;
    out.pts[0].depth = overlaps[edgeIdx];
    out.deepest = out.pts[0].depth;
    return out;
}

} // namespace boxmanifold
} // namespace okay
