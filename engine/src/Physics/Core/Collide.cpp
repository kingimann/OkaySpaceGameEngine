// Narrow phase of the physics core: GJK, EPA and manifold clipping (see Collide.hpp for the sources).
#include "okay/Physics/Core/Collide.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace okay {
namespace phys {

namespace {

struct SPt { V3 w, a, b; };   // a point of the Minkowski difference A - B and the two support points it came from

struct Minkowski {
    const Shape& sa; const Xf& xa; const Shape& sb; const Xf& xb;
    SPt Support(const V3& d) const {
        SPt p;
        p.a = xa.Apply(sa.Support(InvRotate(xa.q, d)));
        p.b = xb.Apply(sb.Support(InvRotate(xb.q, -d)));
        p.w = p.a - p.b;
        return p;
    }
};

// Closest point to the origin on the simplex s[0..n); reduces s to the smallest sub-simplex holding that point and
// writes its barycentric weights. Returns false when the origin is inside a tetrahedron.
bool ClosestOnSimplex(SPt* s, int& n, float* lam, V3& v) {
    if (n == 1) { lam[0] = 1.0f; v = s[0].w; return true; }
    if (n == 2) {
        const V3 a = s[0].w, ab = s[1].w - a;
        const float den = LenSq(ab);
        float t = den > 0.0f ? -Dot(a, ab) / den : 0.0f;
        if (t <= 0.0f) { n = 1; lam[0] = 1.0f; v = a; return true; }
        if (t >= 1.0f) { s[0] = s[1]; n = 1; lam[0] = 1.0f; v = s[0].w; return true; }
        lam[0] = 1.0f - t; lam[1] = t;
        v = a + ab * t;
        return true;
    }
    if (n == 3) {
        const V3 a = s[0].w, b = s[1].w, c = s[2].w;
        const V3 ab = b - a, ac = c - a, ap = -a;
        const float d1 = Dot(ab, ap), d2 = Dot(ac, ap);
        if (d1 <= 0.0f && d2 <= 0.0f) { n = 1; lam[0] = 1; v = a; return true; }
        const V3 bp = -b;
        const float d3 = Dot(ab, bp), d4 = Dot(ac, bp);
        if (d3 >= 0.0f && d4 <= d3) { s[0] = s[1]; n = 1; lam[0] = 1; v = b; return true; }
        const float vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
            const float t = d1 / (d1 - d3);
            n = 2; lam[0] = 1 - t; lam[1] = t; v = a + ab * t; return true;
        }
        const V3 cp = -c;
        const float d5 = Dot(ab, cp), d6 = Dot(ac, cp);
        if (d6 >= 0.0f && d5 <= d6) { s[0] = s[2]; n = 1; lam[0] = 1; v = c; return true; }
        const float vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
            const float t = d2 / (d2 - d6);
            s[1] = s[2]; n = 2; lam[0] = 1 - t; lam[1] = t; v = a + ac * t; return true;
        }
        const float va = d3 * d6 - d5 * d4;
        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
            const float t = (d4 - d3) / ((d4 - d3) + (d5 - d6));
            s[0] = s[1]; s[1] = s[2]; n = 2; lam[0] = 1 - t; lam[1] = t; v = b + (c - b) * t; return true;
        }
        const float den = va + vb + vc;
        if (!(std::fabs(den) > 0.0f)) { n = 1; lam[0] = 1; v = a; return true; }
        const float inv = 1.0f / den;
        const float tv = vb * inv, tw = vc * inv;
        lam[0] = 1 - tv - tw; lam[1] = tv; lam[2] = tw;
        v = a + ab * tv + ac * tw;
        return true;
    }
    // tetrahedron: the origin is inside unless it is outside one of the four faces
    const int faces[4][4] = {{0, 1, 2, 3}, {0, 3, 1, 2}, {0, 2, 3, 1}, {1, 3, 2, 0}};
    bool anyOutside = false;
    float best = 1e30f;
    SPt bestS[3]; int bestN = 0; float bestLam[3] = {0, 0, 0}; V3 bestV;
    for (const auto& f : faces) {
        const V3 a = s[f[0]].w, b = s[f[1]].w, c = s[f[2]].w, d = s[f[3]].w;
        const V3 nrm = Cross(b - a, c - a);
        const float sideO = Dot(-a, nrm), sideD = Dot(d - a, nrm);
        const bool flat = std::fabs(sideD) < 1e-12f;
        if (!flat && sideO * sideD >= 0.0f) continue;   // origin on the same side as the fourth point
        anyOutside = true;
        SPt t[3] = {s[f[0]], s[f[1]], s[f[2]]};
        int tn = 3; float tl[3]; V3 tv;
        ClosestOnSimplex(t, tn, tl, tv);
        const float dd = LenSq(tv);
        if (dd < best) { best = dd; bestN = tn; for (int i = 0; i < tn; ++i) { bestS[i] = t[i]; bestLam[i] = tl[i]; } bestV = tv; }
    }
    if (!anyOutside) return false;
    n = bestN;
    for (int i = 0; i < n; ++i) { s[i] = bestS[i]; lam[i] = bestLam[i]; }
    v = bestV;
    return true;
}

// The size the tolerances are measured against: the smaller shape (so a 1 m box on a 100 m ground box is resolved to
// micrometres), but never below what float rounding on the bigger one allows.
float TolScale(const Shape& a, const Shape& b) {
    const float ra = a.OuterRadius(), rb = b.OuterRadius();
    const float small = std::min(ra, rb), big = std::max(ra, rb);
    return std::max(std::max(small, 0.05f * big), 1e-3f);
}

// GJK in a frame where A sits at the origin (better float precision far from the world origin).
DistanceResult Gjk(const Shape& a, const Xf& xa, const Shape& b, const Xf& xb, SPt* simplexOut, int* countOut) {
    DistanceResult r;
    const Minkowski mk{a, xa, b, xb};
    SPt s[4];
    int n = 1;
    V3 d = xa.p - xb.p;
    if (LenSq(d) < 1e-12f) d = V3{1, 0, 0};
    s[0] = mk.Support(-d);
    float lam[4] = {1, 0, 0, 0};
    V3 v = s[0].w;
    float prev = 1e30f;
    const float scale = TolScale(a, b);
    const float tolSq = (1e-6f * scale) * (1e-6f * scale);
    for (int it = 0; it < 64; ++it) {
        r.iterations = it + 1;
        if (!ClosestOnSimplex(s, n, lam, v)) { r.overlap = true; break; }
        const float vv = LenSq(v);
        if (vv <= tolSq) { r.overlap = true; break; }
        if (vv >= prev) break;   // no progress (rounding): v is as close as it gets
        prev = vv;
        const SPt w = mk.Support(-v);
        if (vv - Dot(v, w.w) <= 1e-6f * vv) break;   // the new point is no closer: converged
        bool dup = false;
        for (int i = 0; i < n; ++i) if (LenSq(s[i].w - w.w) < 1e-14f * scale * scale) dup = true;
        if (dup) break;
        s[n++] = w;
        lam[n - 1] = 0.0f;
    }
    if (!r.overlap) {
        V3 pa, pb;
        for (int i = 0; i < n; ++i) { pa += s[i].a * lam[i]; pb += s[i].b * lam[i]; }
        r.pA = pa; r.pB = pb;
        r.distance = std::sqrt(LenSq(v));
        r.normal = Normalize(pb - pa, Normalize(xb.p - xa.p, V3{0, 1, 0}));
    }
    if (simplexOut) { for (int i = 0; i < n; ++i) simplexOut[i] = s[i]; *countOut = n; }
    return r;
}

// The closest points between segments p1-q1 and p2-q2 (Ericson 5.1.9).
void ClosestSegSeg(const V3& p1, const V3& q1, const V3& p2, const V3& q2, V3& c1, V3& c2) {
    const V3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
    const float a = Dot(d1, d1), e = Dot(d2, d2), f = Dot(d2, r);
    float s = 0.0f, t = 0.0f;
    if (a <= 1e-12f && e <= 1e-12f) { c1 = p1; c2 = p2; return; }
    if (a <= 1e-12f) { t = std::min(std::max(f / e, 0.0f), 1.0f); }
    else {
        const float c = Dot(d1, r);
        if (e <= 1e-12f) { s = std::min(std::max(-c / a, 0.0f), 1.0f); }
        else {
            const float b = Dot(d1, d2), den = a * e - b * b;
            s = den > 1e-12f ? std::min(std::max((b * f - c * e) / den, 0.0f), 1.0f) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0.0f) { t = 0.0f; s = std::min(std::max(-c / a, 0.0f), 1.0f); }
            else if (t > 1.0f) { t = 1.0f; s = std::min(std::max((b - c) / a, 0.0f), 1.0f); }
        }
    }
    c1 = p1 + d1 * s;
    c2 = p2 + d2 * t;
}

bool IsRound(const Shape& s) { return s.type == ShapeType::Sphere || s.type == ShapeType::Capsule; }

void CoreSegment(const Shape& s, const Xf& x, V3& p, V3& q) {
    if (s.type == ShapeType::Capsule) { p = x.Apply(V3{0, -s.halfHeight, 0}); q = x.Apply(V3{0, s.halfHeight, 0}); }
    else { p = q = x.p; }
}

// Clip the points of a polygon (count >= 3, closed) or a segment (2) or a point (1) to the half-space dot(n, x) <= d.
int ClipToPlane(const V3* in, int count, const V3& n, float d, V3* out) {
    int m = 0;
    if (count == 1) { if (Dot(n, in[0]) <= d) out[m++] = in[0]; return m; }
    if (count == 2) {
        const float da = Dot(n, in[0]) - d, db = Dot(n, in[1]) - d;
        if (da <= 0.0f) out[m++] = in[0];
        if ((da < 0.0f) != (db < 0.0f) && da != db) out[m++] = in[0] + (in[1] - in[0]) * (da / (da - db));
        if (db <= 0.0f) out[m++] = in[1];
        return std::min(m, 2);
    }
    for (int i = 0; i < count; ++i) {
        const V3& p = in[i];
        const V3& q = in[(i + 1) % count];
        const float dp = Dot(n, p) - d, dq = Dot(n, q) - d;
        if (dp <= 0.0f) out[m++] = p;
        if ((dp < 0.0f) != (dq < 0.0f) && dp != dq && m < 32) out[m++] = p + (q - p) * (dp / (dp - dq));
        if (m >= 32) break;
    }
    return m;
}

// The face, edge or point of `s` facing world direction `dir`, in world space.
int WorldFeature(const Shape& s, const Xf& x, const V3& dir, V3* out, V3& normal) {
    V3 ln;
    const int n = s.SupportFeature(InvRotate(x.q, dir), out, ln);
    for (int i = 0; i < n; ++i) out[i] = x.Apply(out[i]);
    normal = Rotate(x.q, ln);
    return n;
}

int Single(Manifold& m, const V3& n, const V3& pA, const V3& pB) {
    m.normal = n;
    m.count = 1;
    m.pts[0].pA = pA;
    m.pts[0].pB = pB;
    m.pts[0].separation = Dot(pB - pA, n);
    return 1;
}

// Builds the manifold from the normal n (A to B) and one witness pair, by clipping the features facing each other.
void BuildManifold(const Shape& a, const Xf& xa, const Shape& b, const Xf& xb, const V3& n, const V3& wA, const V3& wB,
                   float speculative, Manifold& m) {
    const float rA = a.CoreRadius(), rB = b.CoreRadius();
    V3 fa[32], fb[32], na, nb;
    const int ca = WorldFeature(a, xa, n, fa, na);
    const int cb = WorldFeature(b, xb, -n, fb, nb);
    constexpr float kRefCos = 0.95f;
    const float alignA = ca >= 3 ? Dot(na, n) : -1.0f;
    const float alignB = cb >= 3 ? Dot(nb, -n) : -1.0f;
    m.count = 0;
    if (alignA > kRefCos || alignB > kRefCos) {
        const bool refIsA = alignA >= alignB;
        const V3* ref = refIsA ? fa : fb;
        const int rc = refIsA ? ca : cb;
        const V3 refN = refIsA ? na : nb;                  // outward normal of the reference face
        const V3* inc = refIsA ? fb : fa;
        const int ic = refIsA ? cb : ca;
        V3 bufA[40], bufB[40];
        int cnt = ic;
        for (int i = 0; i < ic; ++i) bufA[i] = inc[i];
        V3* cur = bufA; V3* nxt = bufB;
        for (int i = 0; i < rc && cnt > 0; ++i) {
            const V3 e = ref[(i + 1) % rc] - ref[i];
            const V3 sn = Normalize(Cross(e, refN), V3{});   // side plane, outward
            if (LenSq(sn) == 0.0f) continue;
            cnt = ClipToPlane(cur, cnt, sn, Dot(sn, ref[i]), nxt);
            V3* t = cur; cur = nxt; nxt = t;
        }
        const V3 nAB = refIsA ? refN : -refN;
        const float refD = Dot(refN, ref[0]);
        ContactPoint pts[40];
        int k = 0;
        for (int i = 0; i < cnt && k < 40; ++i) {
            const V3 q = cur[i];
            const float dist = Dot(refN, q) - refD;        // incident core point above the reference face
            const V3 onRef = q - refN * dist;
            ContactPoint cp;
            if (refIsA) { cp.pA = onRef + nAB * rA; cp.pB = q - nAB * rB; }
            else { cp.pB = onRef - nAB * rB; cp.pA = q + nAB * rA; }
            cp.separation = Dot(cp.pB - cp.pA, nAB);
            if (cp.separation <= speculative) pts[k++] = cp;
        }
        if (k > 0) {
            k = ReduceContacts(pts, k, nAB);
            m.normal = nAB;
            m.count = k;
            for (int i = 0; i < k; ++i) m.pts[i] = pts[i];
            return;
        }
    }
    if (ca == 2 && cb == 2) {
        const V3 da = Normalize(fa[1] - fa[0]), db = Normalize(fb[1] - fb[0]);
        if (std::fabs(Dot(da, db)) > 0.995f) {   // parallel edges: up to two points where they overlap
            const float l = Dot(fa[1] - fa[0], da);
            ContactPoint pts[2];
            int k = 0;
            V3 seg[2] = {fb[0], fb[1]}, tmp[2];
            int c = ClipToPlane(seg, 2, -da, -Dot(da, fa[0]), tmp);
            c = ClipToPlane(tmp, c, da, Dot(da, fa[0]) + l, seg);
            for (int i = 0; i < c; ++i) {
                const V3 q = seg[i];
                const V3 onA = fa[0] + da * std::min(std::max(Dot(q - fa[0], da), 0.0f), l);
                ContactPoint cp;
                cp.pA = onA + n * rA;
                cp.pB = q - n * rB;
                cp.separation = Dot(cp.pB - cp.pA, n);
                if (cp.separation <= speculative) pts[k++] = cp;
            }
            if (k > 0) {
                m.normal = n; m.count = k;
                for (int i = 0; i < k; ++i) m.pts[i] = pts[i];
                return;
            }
        }
    }
    Single(m, n, wA + n * rA, wB - n * rB);
}

// Exact contact between two round shapes (sphere and capsule cores are a point or a segment).
bool RoundPair(const Shape& a, const Xf& xa, const Shape& b, const Xf& xb, float speculative, Manifold& m) {
    V3 p1, q1, p2, q2;
    CoreSegment(a, xa, p1, q1);
    CoreSegment(b, xb, p2, q2);
    V3 c1, c2;
    ClosestSegSeg(p1, q1, p2, q2, c1, c2);
    const float rA = a.radius, rB = b.radius;
    const float dist = Len(c2 - c1);
    if (dist - rA - rB > speculative) return false;
    V3 n;
    if (dist > 1e-6f) n = (c2 - c1) * (1.0f / dist);
    else {   // the cores cross: push apart across both segments
        n = Normalize(Cross(q1 - p1, q2 - p2), V3{});
        if (LenSq(n) == 0.0f) n = Normalize(xb.p - xa.p, V3{0, 1, 0});
        if (Dot(n, xb.p - xa.p) < 0.0f) n = -n;
    }
    m.count = 0;
    if (a.type == ShapeType::Capsule && b.type == ShapeType::Capsule) {
        const V3 da = Normalize(q1 - p1), db = Normalize(q2 - p2);
        if (std::fabs(Dot(da, db)) > 0.995f) {   // side by side: two points
            const float l = Dot(q1 - p1, da);
            V3 seg[2] = {p2, q2}, tmp[2];
            int c = ClipToPlane(seg, 2, -da, -Dot(da, p1), tmp);
            c = ClipToPlane(tmp, c, da, Dot(da, p1) + l, seg);
            if (c == 2) {
                for (int i = 0; i < 2; ++i) {
                    const V3 q = seg[i];
                    const V3 onA = p1 + da * std::min(std::max(Dot(q - p1, da), 0.0f), l);
                    const V3 nn = Normalize(q - onA, n);
                    m.pts[i].pA = onA + nn * rA;
                    m.pts[i].pB = q - nn * rB;
                    m.pts[i].separation = Dot(m.pts[i].pB - m.pts[i].pA, n);
                }
                m.normal = n;
                m.count = 2;
                return true;
            }
        }
    }
    Single(m, n, c1 + n * rA, c2 - n * rB);
    return true;
}

// Box against box by the separating axis test (Ericson 4.4.1: the 3 + 3 face normals and the 9 edge-pair cross
// products). The axis with the largest separation (or least overlap) wins; face axes are preferred over edge axes
// unless an edge axis is clearly better, so resting boxes keep a steady face normal. A face axis is then clipped into
// a full manifold; an edge axis gives one point between the two edges.
bool BoxBox(const Shape& a, const Xf& xa, const Shape& b, const Xf& xb, float speculative, Manifold& m) {
    const M33 Ra = M33::FromQ(xa.q), Rb = M33::FromQ(xb.q);
    const V3 ua[3] = {Ra.Col(0), Ra.Col(1), Ra.Col(2)}, ub[3] = {Rb.Col(0), Rb.Col(1), Rb.Col(2)};
    const V3 ha = a.halfExtents, hb = b.halfExtents;
    const V3 T = xb.p - xa.p;
    float bestFace = -1e30f, bestEdge = -1e30f;
    V3 nFace, nEdge;
    int edgeI = -1, edgeJ = -1;
    auto test = [&](const V3& axisIn, bool face, int ei, int ej) -> bool {
        const float l = Len(axisIn);
        if (l < 1e-5f) return true;   // parallel edges: covered by the face axes
        const V3 L = axisIn * (1.0f / l);
        const float ra = ha.x * std::fabs(Dot(ua[0], L)) + ha.y * std::fabs(Dot(ua[1], L)) + ha.z * std::fabs(Dot(ua[2], L));
        const float rb = hb.x * std::fabs(Dot(ub[0], L)) + hb.y * std::fabs(Dot(ub[1], L)) + hb.z * std::fabs(Dot(ub[2], L));
        const float tl = Dot(T, L);
        const float sep = std::fabs(tl) - (ra + rb);
        if (sep > speculative) return false;
        const V3 n = tl < 0.0f ? -L : L;
        if (face) { if (sep > bestFace) { bestFace = sep; nFace = n; } }
        else if (sep > bestEdge) { bestEdge = sep; nEdge = n; edgeI = ei; edgeJ = ej; }
        return true;
    };
    for (int i = 0; i < 3; ++i) if (!test(ua[i], true, -1, -1)) return false;
    for (int i = 0; i < 3; ++i) if (!test(ub[i], true, -1, -1)) return false;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) if (!test(Cross(ua[i], ub[j]), false, i, j)) return false;
    const float faceTol = 0.005f + 0.02f * std::min({ha.x, ha.y, ha.z, hb.x, hb.y, hb.z});
    if (edgeI >= 0 && bestEdge > bestFace + faceTol) {
        // edge against edge: the two edges that face each other along n, and their closest points
        const V3 n = nEdge;
        V3 pa = xa.p, pb = xb.p;
        for (int k = 0; k < 3; ++k) {
            if (k != edgeI) pa += ua[k] * (Dot(ua[k], n) > 0.0f ? ha[k] : -ha[k]);
            if (k != edgeJ) pb += ub[k] * (Dot(ub[k], n) < 0.0f ? hb[k] : -hb[k]);
        }
        V3 c1, c2;
        ClosestSegSeg(pa - ua[edgeI] * ha[edgeI], pa + ua[edgeI] * ha[edgeI], pb - ub[edgeJ] * hb[edgeJ], pb + ub[edgeJ] * hb[edgeJ], c1, c2);
        m.normal = n;
        m.count = 1;
        m.pts[0].pA = c1;
        m.pts[0].pB = c2;
        m.pts[0].separation = Dot(c2 - c1, n);
        return m.pts[0].separation <= speculative;
    }
    const V3 n = nFace;
    BuildManifold(a, xa, b, xb, n, xa.Apply(a.Support(InvRotate(xa.q, n))), xb.Apply(b.Support(InvRotate(xb.q, -n))), speculative, m);
    return m.count > 0;
}

// A plane (A) against any convex shape (B).
bool PlanePair(const Shape& plane, const Xf& xp, const Shape& b, const Xf& xb, float speculative, Manifold& m) {
    const V3 n = Rotate(xp.q, plane.planeNormal);
    const float off = plane.planeOffset + Dot(n, xp.p);
    V3 f[32], fn;
    const int c = WorldFeature(b, xb, -n, f, fn);
    const float rB = b.CoreRadius();
    ContactPoint pts[32];
    int k = 0;
    for (int i = 0; i < c; ++i) {
        ContactPoint cp;
        cp.pB = f[i] - n * rB;
        const float h = Dot(n, cp.pB) - off;
        cp.pA = cp.pB - n * h;
        cp.separation = h;
        if (h <= speculative) pts[k++] = cp;
    }
    if (k == 0) return false;
    k = ReduceContacts(pts, k, n);
    m.normal = n;
    m.count = k;
    for (int i = 0; i < k; ++i) m.pts[i] = pts[i];
    return true;
}

void Flip(Manifold& m) {
    m.normal = -m.normal;
    for (int i = 0; i < m.count; ++i) { const V3 t = m.pts[i].pA; m.pts[i].pA = m.pts[i].pB; m.pts[i].pB = t; }
}

} // namespace

DistanceResult GjkDistance(const Shape& a, const Xf& xa, const Shape& b, const Xf& xb) {
    const Xf la{V3{}, xa.q}, lb{xb.p - xa.p, xb.q};
    DistanceResult r = Gjk(a, la, b, lb, nullptr, nullptr);
    r.pA += xa.p;
    r.pB += xa.p;
    return r;
}

PenetrationResult EpaPenetration(const Shape& a, const Xf& xaIn, const Shape& b, const Xf& xbIn) {
    PenetrationResult res;
    const Xf xa{V3{}, xaIn.q}, xb{xbIn.p - xaIn.p, xbIn.q};
    const Minkowski mk{a, xa, b, xb};
    SPt simplex[4];
    int n = 0;
    const DistanceResult g = Gjk(a, xa, b, xb, simplex, &n);
    if (!g.overlap) return res;
    const float scale = TolScale(a, b);
    const float eps = 1e-5f * scale;

    std::vector<SPt> verts(simplex, simplex + n);
    auto addIfNew = [&](const SPt& p) {
        for (const SPt& v : verts) if (LenSq(v.w - p.w) < eps * eps) return false;
        verts.push_back(p);
        return true;
    };
    // Grow the GJK simplex into a tetrahedron around the origin.
    const V3 axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    if (verts.size() == 1) for (const V3& d : axes) if (addIfNew(mk.Support(d))) break;
    if (verts.size() == 2) {
        const V3 l = verts[1].w - verts[0].w;
        V3 t1, t2;
        Basis(Normalize(l, V3{1, 0, 0}), t1, t2);
        const V3 dirs[4] = {t1, -t1, t2, -t2};
        for (const V3& d : dirs) {
            const SPt p = mk.Support(d);
            const V3 r = p.w - verts[0].w;
            if (LenSq(Cross(r, l)) > eps * eps * LenSq(l)) { verts.push_back(p); break; }
        }
    }
    if (verts.size() == 3) {
        const V3 nn = Cross(verts[1].w - verts[0].w, verts[2].w - verts[0].w);
        SPt p = mk.Support(nn);
        if (std::fabs(Dot(p.w - verts[0].w, nn)) < eps * Len(nn)) p = mk.Support(-nn);
        if (std::fabs(Dot(p.w - verts[0].w, nn)) >= eps * Len(nn)) verts.push_back(p);
    }
    if (verts.size() < 4) return res;

    struct Face { int i, j, k; V3 n; float d; bool alive; };
    std::vector<Face> faces;
    const V3 inner = (verts[0].w + verts[1].w + verts[2].w + verts[3].w) * 0.25f;
    auto makeFace = [&](int i, int j, int k, Face& f) {
        V3 nn = Cross(verts[(std::size_t)j].w - verts[(std::size_t)i].w, verts[(std::size_t)k].w - verts[(std::size_t)i].w);
        const float l = Len(nn);
        if (!(l > 1e-20f)) return false;
        nn *= 1.0f / l;
        f = Face{i, j, k, nn, Dot(nn, verts[(std::size_t)i].w), true};
        return true;
    };
    const int tf[4][3] = {{0, 1, 2}, {0, 3, 1}, {0, 2, 3}, {1, 3, 2}};
    for (const auto& t : tf) {
        Face f;
        if (!makeFace(t[0], t[1], t[2], f)) return res;
        if (Dot(f.n, inner) > f.d) { if (!makeFace(t[0], t[2], t[1], f)) return res; }
        faces.push_back(f);
    }
    Face best{};
    std::vector<std::pair<int, int>> horizon;
    for (int it = 0; it < 64; ++it) {
        int bi = -1;
        float bd = 1e30f;
        for (std::size_t i = 0; i < faces.size(); ++i)
            if (faces[i].alive && faces[i].d < bd) { bd = faces[i].d; bi = (int)i; }
        if (bi < 0) break;
        best = faces[(std::size_t)bi];
        const SPt p = mk.Support(best.n);
        const float gain = Dot(p.w, best.n) - best.d;
        if (gain < eps) break;
        bool dup = false;
        for (const SPt& v : verts) if (LenSq(v.w - p.w) < eps * eps) { dup = true; break; }
        if (dup) break;
        verts.push_back(p);
        const int pi = (int)verts.size() - 1;
        horizon.clear();
        std::vector<int> vis;
        // faces the new point sees (the face being expanded always counts); a small tolerance keeps nearly coplanar
        // neighbours out, so the horizon stays one clean loop
        for (std::size_t i = 0; i < faces.size(); ++i)
            if (faces[i].alive && ((int)i == bi || Dot(faces[i].n, p.w) - faces[i].d > eps * 0.1f)) vis.push_back((int)i);
        for (int vf : vis) {
            const Face& f = faces[(std::size_t)vf];
            const int e[3][2] = {{f.i, f.j}, {f.j, f.k}, {f.k, f.i}};
            for (const auto& ed : e) {
                bool shared = false;
                for (int of : vis) {
                    if (of == vf) continue;
                    const Face& g = faces[(std::size_t)of];
                    if ((g.i == ed[1] && g.j == ed[0]) || (g.j == ed[1] && g.k == ed[0]) || (g.k == ed[1] && g.i == ed[0])) { shared = true; break; }
                }
                if (!shared) horizon.emplace_back(ed[0], ed[1]);
            }
        }
        // the new faces must all keep the origin inside; if rounding broke that, stop with what we have
        std::vector<Face> fresh;
        bool broken = horizon.size() < 3;
        for (const auto& h : horizon) {
            Face f;
            if (!makeFace(h.first, h.second, pi, f)) continue;
            if (f.d < -eps) broken = true;
            fresh.push_back(f);
        }
        if (broken || fresh.empty()) break;
        for (int vf : vis) faces[(std::size_t)vf].alive = false;
        for (const Face& f : fresh) faces.push_back(f);
        if (faces.size() > 512) break;
    }
    // the origin projected on the best face, in barycentric coordinates of its three points
    const SPt& A = verts[(std::size_t)best.i];
    const SPt& B = verts[(std::size_t)best.j];
    const SPt& C = verts[(std::size_t)best.k];
    const V3 p = best.n * best.d;
    const V3 v0 = B.w - A.w, v1 = C.w - A.w, v2 = p - A.w;
    const float d00 = Dot(v0, v0), d01 = Dot(v0, v1), d11 = Dot(v1, v1), d20 = Dot(v2, v0), d21 = Dot(v2, v1);
    const float den = d00 * d11 - d01 * d01;
    float u = 1.0f / 3.0f, v = 1.0f / 3.0f;
    if (std::fabs(den) > 1e-20f) { u = (d11 * d20 - d01 * d21) / den; v = (d00 * d21 - d01 * d20) / den; }
    const float w0 = 1.0f - u - v;
    res.ok = true;
    res.normal = best.n;
    res.depth = std::max(best.d, 0.0f);
    res.pA = A.a * w0 + B.a * u + C.a * v + xaIn.p;
    res.pB = A.b * w0 + B.b * u + C.b * v + xaIn.p;
    return res;
}

int ReduceContacts(ContactPoint* pts, int count, const V3& n) {
    if (count <= 4) return count;
    auto pos = [&](int i) { return (pts[i].pA + pts[i].pB) * 0.5f; };
    int i0 = 0;
    for (int i = 1; i < count; ++i) if (pts[i].separation < pts[i0].separation) i0 = i;
    int i1 = -1; float best = -1.0f;
    for (int i = 0; i < count; ++i) { if (i == i0) continue; const float d = LenSq(pos(i) - pos(i0)); if (d > best) { best = d; i1 = i; } }
    int i2 = -1; best = -1.0f;
    float sign = 1.0f;
    for (int i = 0; i < count; ++i) {
        if (i == i0 || i == i1) continue;
        const float a = Dot(Cross(pos(i1) - pos(i0), pos(i) - pos(i0)), n);
        if (std::fabs(a) > best) { best = std::fabs(a); i2 = i; sign = a < 0 ? -1.0f : 1.0f; }
    }
    int i3 = -1; best = 0.0f;
    const int tri[3] = {i0, i1, i2};
    for (int i = 0; i < count; ++i) {
        if (i == i0 || i == i1 || i == i2) continue;
        for (int e = 0; e < 3; ++e) {   // area of the triangle this point adds outside each edge
            const V3 p = pos(tri[e]), q = pos(tri[(e + 1) % 3]);
            const float a = -sign * Dot(Cross(q - p, pos(i) - p), n);
            if (a > best) { best = a; i3 = i; }
        }
    }
    ContactPoint keep[4];
    int k = 0;
    keep[k++] = pts[i0];
    keep[k++] = pts[i1];
    if (i2 >= 0) keep[k++] = pts[i2];
    if (i3 >= 0) keep[k++] = pts[i3];
    for (int i = 0; i < k; ++i) pts[i] = keep[i];
    return k;
}

bool Collide(const Shape& a, const Xf& xa, const Shape& b, const Xf& xb, float speculative, Manifold& m) {
    m.count = 0;
    if (a.type == ShapeType::Plane && b.type == ShapeType::Plane) return false;
    if (b.type == ShapeType::Plane) { if (!PlanePair(b, xb, a, xa, speculative, m)) return false; Flip(m); return true; }
    if (a.type == ShapeType::Plane) return PlanePair(a, xa, b, xb, speculative, m);
    if (IsRound(a) && IsRound(b)) return RoundPair(a, xa, b, xb, speculative, m);
    if (a.type == ShapeType::Box && b.type == ShapeType::Box) return BoxBox(a, xa, b, xb, speculative, m);

    const float rA = a.CoreRadius(), rB = b.CoreRadius();
    const DistanceResult d = GjkDistance(a, xa, b, xb);
    if (!d.overlap) {
        if (d.distance - rA - rB > speculative) return false;
        BuildManifold(a, xa, b, xb, d.normal, d.pA, d.pB, speculative, m);
        return m.count > 0;
    }
    const PenetrationResult p = EpaPenetration(a, xa, b, xb);
    V3 n = p.normal, wA = p.pA, wB = p.pB;
    if (!p.ok) {   // flat Minkowski difference (a segment in a triangle's plane, say): use a face normal or the centres
        V3 fn;
        V3 tmp[32];
        if (a.type == ShapeType::Triangle) { a.SupportFeature(V3{0, 1, 0}, tmp, fn); n = Rotate(xa.q, Normalize(Cross(a.tri[1] - a.tri[0], a.tri[2] - a.tri[0]))); if (Dot(n, xb.p - xa.p) < 0) n = -n; }
        else if (b.type == ShapeType::Triangle) { n = -Rotate(xb.q, Normalize(Cross(b.tri[1] - b.tri[0], b.tri[2] - b.tri[0]))); if (Dot(n, xb.p - xa.p) < 0) n = -n; }
        else n = Normalize(xb.p - xa.p, V3{0, 1, 0});
        wA = xa.Apply(a.Support(InvRotate(xa.q, n)));
        wB = xb.Apply(b.Support(InvRotate(xb.q, -n)));
    }
    BuildManifold(a, xa, b, xb, n, wA, wB, speculative, m);
    return m.count > 0;
}

} // namespace phys
} // namespace okay
