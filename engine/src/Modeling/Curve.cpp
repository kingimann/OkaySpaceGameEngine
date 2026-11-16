#include "okay/Modeling/Curve.hpp"
#include "okay/Modeling/PolyMesh.hpp"
#include <algorithm>
#include <cmath>

namespace okay::modeling {

namespace {
bool Finite(const Vec3& v) { return std::isfinite(v.x + v.y + v.z); }
Vec3 Bez(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, float t) {
    const float u = 1.0f - t;
    return a * (u * u * u) + b * (3.0f * u * u * t) + c * (3.0f * u * t * t) + d * (t * t * t);
}
// Frames along the samples: tangents and a normal carried along without twisting.
struct Frames { std::vector<Vec3> t, n, b; };
Frames MakeFrames(const std::vector<Vec3>& p, bool closed) {
    Frames f; const int n = (int)p.size();
    f.t.resize(n); f.n.resize(n); f.b.resize(n);
    for (int i = 0; i < n; ++i) {
        const Vec3 prev = p[closed ? (i + n - 1) % n : std::max(0, i - 1)], next = p[closed ? (i + 1) % n : std::min(n - 1, i + 1)];
        Vec3 d = next - prev;
        if (d.SqrMagnitude() < 1e-14f) d = i > 0 ? f.t[i - 1] : Vec3{1, 0, 0};
        f.t[i] = d.Normalized();
    }
    Vec3 up = std::fabs(f.t[0].y) < 0.95f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    Vec3 nrm = (up - f.t[0] * Vec3::Dot(up, f.t[0]));
    nrm = nrm.SqrMagnitude() > 1e-14f ? nrm.Normalized() : Vec3{0, 0, 1};
    for (int i = 0; i < n; ++i) {
        if (i > 0) {                                            // carry the normal to the next tangent by the smallest turn
            nrm = nrm - f.t[i] * Vec3::Dot(nrm, f.t[i]);
            nrm = nrm.SqrMagnitude() > 1e-14f ? nrm.Normalized() : Vec3::Cross(f.t[i], Vec3{1, 0, 0}).Normalized();
        }
        f.n[i] = nrm;
        f.b[i] = Vec3::Cross(f.t[i], nrm).Normalized();
    }
    return f;
}
float Volume(const Mesh& m) {
    float v = 0;
    for (int t = 0; t < m.TriangleCount(); ++t) v += Vec3::Dot(m.vertices[m.triangles[t * 3]], Vec3::Cross(m.vertices[m.triangles[t * 3 + 1]], m.vertices[m.triangles[t * 3 + 2]])) / 6.0f;
    return v;
}
}

void SmoothHandles(BezierCurve& c, float tension) {
    const int n = (int)c.pts.size();
    if (!std::isfinite(tension)) tension = 0.5f;
    tension = std::clamp(tension, 0.0f, 1.0f);
    for (int i = 0; i < n; ++i) {
        const bool first = !c.closed && i == 0, last = !c.closed && i == n - 1;
        const Vec3 prev = c.pts[c.closed ? (i + n - 1) % n : std::max(0, i - 1)].p, next = c.pts[c.closed ? (i + 1) % n : std::min(n - 1, i + 1)].p;
        Vec3 dir = (next - prev) * (tension / 3.0f);             // Catmull-Rom: a third of the way along half the chord
        if (first) dir = (next - c.pts[i].p) * (tension / 3.0f);
        if (last) dir = (c.pts[i].p - prev) * (tension / 3.0f);
        c.pts[i].outH = dir; c.pts[i].inH = dir * -1.0f;
    }
}

BezierCurve CurveThrough(const std::vector<Vec3>& points, bool closed, float tension) {
    BezierCurve c; c.closed = closed && points.size() >= 3;
    for (const Vec3& p : points) if (Finite(p)) { CurvePoint cp; cp.p = p; c.pts.push_back(cp); }
    SmoothHandles(c, tension);
    return c;
}

std::vector<Vec3> SampleCurve(const BezierCurve& c, int perSpan) {
    std::vector<Vec3> out;
    const int spans = c.Spans();
    if (spans < 1) return out;
    perSpan = std::clamp(perSpan, 1, 256);
    const int n = (int)c.pts.size();
    for (int s = 0; s < spans; ++s) {
        const CurvePoint& a = c.pts[s]; const CurvePoint& b = c.pts[(s + 1) % n];
        if (!Finite(a.p) || !Finite(b.p) || !Finite(a.outH) || !Finite(b.inH)) return {};
        for (int k = 0; k < perSpan; ++k) out.push_back(Bez(a.p, a.p + a.outH, b.p + b.inH, b.p, (float)k / (float)perSpan));
    }
    if (!c.closed) out.push_back(c.pts.back().p);
    return out;
}

float CurveLength(const BezierCurve& c, int perSpan) {
    const std::vector<Vec3> p = SampleCurve(c, perSpan);
    float len = 0;
    for (std::size_t i = 1; i < p.size(); ++i) len += (p[i] - p[i - 1]).Magnitude();
    if (c.closed && p.size() > 1) len += (p.front() - p.back()).Magnitude();
    return len;
}

Mesh CurveSweep(const BezierCurve& c, const std::vector<Vec2>& profile, bool closedProfile, int perSpan, float scaleEnd, bool caps) {
    Mesh none;
    if (profile.size() < 2 || !std::isfinite(scaleEnd)) return none;
    for (const Vec2& q : profile) if (!std::isfinite(q.x + q.y)) return none;
    const std::vector<Vec3> p = SampleCurve(c, perSpan);
    if (p.size() < 2) return none;
    const bool ring = c.closed;
    const Frames fr = MakeFrames(p, ring);
    const int rings = (int)p.size(), pn = (int)profile.size();
    PolyMesh pm;
    for (int i = 0; i < rings; ++i) {
        const float t = rings > 1 ? (float)i / (float)(ring ? rings : rings - 1) : 0.0f;
        const float sc = 1.0f + (scaleEnd - 1.0f) * t;
        for (int k = 0; k < pn; ++k) pm.verts.push_back(p[i] + fr.n[i] * (profile[k].x * sc) + fr.b[i] * (profile[k].y * sc));
    }
    const int spans = ring ? rings : rings - 1;
    const int segs = closedProfile ? pn : pn - 1;
    for (int i = 0; i < spans; ++i) {
        const int j = (i + 1) % rings;
        for (int k = 0; k < segs; ++k) {
            const int k2 = (k + 1) % pn;
            PolyFace f; f.verts = {i * pn + k, j * pn + k, j * pn + k2, i * pn + k2};
            pm.faces.push_back(f);
        }
    }
    if (caps && !ring && closedProfile && pn >= 3) {
        PolyFace a, b;
        for (int k = 0; k < pn; ++k) { a.verts.push_back(k); b.verts.push_back((rings - 1) * pn + (pn - 1 - k)); }
        pm.faces.push_back(a); pm.faces.push_back(b);
    }
    Mesh m = pm.ToMesh();
    m.name = "";
    if (closedProfile && !ring && caps && Volume(m) < 0) m.FlipWinding();     // outward
    else if (closedProfile && ring && Volume(m) < 0) m.FlipWinding();
    m.RefreshNormals();
    return m;
}

Mesh CurvePipe(const BezierCurve& c, float radius, int sides, int perSpan, bool caps) {
    if (!(radius > 0.0f) || !std::isfinite(radius)) return Mesh{};
    sides = std::clamp(sides, 3, 64);
    std::vector<Vec2> prof;
    for (int k = 0; k < sides; ++k) { const float a = 6.28318530718f * (float)k / (float)sides; prof.push_back({std::cos(a) * radius, std::sin(a) * radius}); }
    return CurveSweep(c, prof, true, perSpan, 1.0f, caps);
}

Mesh CurveRibbon(const BezierCurve& c, float width, int perSpan, bool bothSides) {
    if (!(width > 0.0f) || !std::isfinite(width)) return Mesh{};
    const std::vector<Vec2> prof = {{0, -width * 0.5f}, {0, width * 0.5f}};   // across the curve sideways (the carried frame's binormal), so the strip lies flat
    Mesh m = CurveSweep(c, prof, false, perSpan, 1.0f, false);
    // Face up where the curve runs along +X: the sweep gives quads with the normal along +/-Y for that case.
    if (m.TriangleCount() > 0) {
        Vec3 n{0, 0, 0};
        for (int t = 0; t < m.TriangleCount(); ++t) { const Vec3& a = m.vertices[m.triangles[t * 3]]; n += Vec3::Cross(m.vertices[m.triangles[t * 3 + 1]] - a, m.vertices[m.triangles[t * 3 + 2]] - a); }
        if (n.y < 0) m.FlipWinding();
    }
    if (bothSides && m.TriangleCount() > 0) {   // a second copy of every triangle, wound the other way, on the same points
        const std::size_t n0 = m.triangles.size();
        for (std::size_t i = 0; i + 2 < n0; i += 3) { m.triangles.push_back(m.triangles[i]); m.triangles.push_back(m.triangles[i + 2]); m.triangles.push_back(m.triangles[i + 1]); }
    }
    return m;
}

} // namespace okay::modeling
