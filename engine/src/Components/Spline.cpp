#include "okay/Components/Spline.hpp"
#include "okay/Components/ActionList.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Mathf.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace okay {

namespace {

using Mode = SplinePoint::Mode;

Vec3 Bez(const Vec3* c, float u) {
    const float v = 1.0f - u;
    return c[0] * (v * v * v) + c[1] * (3.0f * v * v * u) + c[2] * (3.0f * v * u * u) + c[3] * (u * u * u);
}
Vec3 BezD(const Vec3* c, float u) {
    const float v = 1.0f - u;
    return (c[1] - c[0]) * (3.0f * v * v) + (c[2] - c[1]) * (6.0f * v * u) + (c[3] - c[2]) * (3.0f * u * u);
}
Vec3 Unit(const Vec3& v, const Vec3& fallback) {
    const float m = v.Magnitude();
    return m > 1e-8f ? v * (1.0f / m) : fallback;
}
// Rodrigues: `v` turned by `rad` about the unit `axis`.
Vec3 Turn(const Vec3& v, const Vec3& axis, float rad) {
    const float c = std::cos(rad), s = std::sin(rad);
    return v * c + Vec3::Cross(axis, v) * s + axis * (Vec3::Dot(axis, v) * (1.0f - c));
}
// `v` with its component along the unit `n` removed, normalised (fallback when parallel).
Vec3 Perp(const Vec3& v, const Vec3& n, const Vec3& fallback) {
    return Unit(v - n * Vec3::Dot(v, n), fallback);
}
// An up vector perpendicular to `t`, close to local +Y (or +Z when `t` is vertical).
Vec3 StartUp(const Vec3& t) {
    const Vec3 ref = std::fabs(t.y) < 0.99f ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
    return Perp(ref, t, Vec3{1, 0, 0});
}
// Squared world length of a local vector under the metric G = L^T L (xx xy xz yy yz zz).
float MetricLen(const float* g, const Vec3& v) {
    const float q = v.x * v.x * g[0] + v.y * v.y * g[3] + v.z * v.z * g[5] +
                    2.0f * (v.x * v.y * g[1] + v.x * v.z * g[2] + v.y * v.z * g[4]);
    return std::sqrt(q > 0.0f ? q : 0.0f);
}
void MetricOf(const Mat4& m, float* g) {
    const Vec3 ex = m.MultiplyVector({1, 0, 0}), ey = m.MultiplyVector({0, 1, 0}), ez = m.MultiplyVector({0, 0, 1});
    g[0] = Vec3::Dot(ex, ex); g[1] = Vec3::Dot(ex, ey); g[2] = Vec3::Dot(ex, ez);
    g[3] = Vec3::Dot(ey, ey); g[4] = Vec3::Dot(ey, ez); g[5] = Vec3::Dot(ez, ez);
}
bool Explicit(Mode m) { return m == Mode::Aligned || m == Mode::Free; }

} // namespace

// ---- Points and segments ------------------------------------------------------

int Spline::SegmentCount() const {
    const int n = (int)points.size();
    if (n < 2) return 0;
    return closed ? n : n - 1;
}

void Spline::Effective(const std::vector<SplinePoint>& pts, int i, Vec3& in, Vec3& out) const {
    const int n = (int)pts.size();
    const SplinePoint& p = pts[(std::size_t)i];
    if (Explicit(p.mode)) { in = p.tangentIn; out = p.tangentOut; return; }
    const bool hasPrev = i > 0 || (closed && n > 1);
    const bool hasNext = i < n - 1 || (closed && n > 1);
    const Vec3 prev = hasPrev ? pts[(std::size_t)((i - 1 + n) % n)].position : p.position;
    const Vec3 next = hasNext ? pts[(std::size_t)((i + 1) % n)].position : p.position;
    in = out = Vec3{0, 0, 0};
    if (p.mode == Mode::Linear) {
        if (hasPrev) in = (prev - p.position) * (1.0f / 3.0f);
        if (hasNext) out = (next - p.position) * (1.0f / 3.0f);
        return;
    }
    if (hasPrev && hasNext) {
        // Catmull-Rom-like direction, each side as long as a third of its neighbour gap.
        const Vec3 dir = Unit(next - prev, Unit(next - p.position, Vec3{0, 0, 1}));
        in = dir * (-(p.position - prev).Magnitude() / 3.0f);
        out = dir * ((next - p.position).Magnitude() / 3.0f);
    } else if (hasNext) {
        out = (next - p.position) * (1.0f / 3.0f); in = out * -1.0f;
    } else if (hasPrev) {
        in = (prev - p.position) * (1.0f / 3.0f); out = in * -1.0f;
    }
}

void Spline::Controls(const std::vector<SplinePoint>& pts, int seg, Vec3 c[4]) const {
    const int n = (int)pts.size();
    const int i = seg, j = (seg + 1) % n;
    c[0] = pts[(std::size_t)i].position;
    c[3] = pts[(std::size_t)j].position;
    if (type == Type::CatmullRom) {
        // Uniform Catmull-Rom as a Bezier: P1 + (P2 - P0) / 6, P2 - (P3 - P1) / 6. An
        // open end continues straight (a mirrored phantom point).
        auto at = [&](int k) -> Vec3 {
            if (closed) return pts[(std::size_t)(((k % n) + n) % n)].position;
            if (k < 0) return pts[0].position * 2.0f - pts[1].position;
            if (k >= n) return pts[(std::size_t)(n - 1)].position * 2.0f - pts[(std::size_t)(n - 2)].position;
            return pts[(std::size_t)k].position;
        };
        c[1] = c[0] + (c[3] - at(i - 1)) * (1.0f / 6.0f);
        c[2] = c[3] - (at(i + 2) - c[0]) * (1.0f / 6.0f);
        if (pts[(std::size_t)i].mode == Mode::Linear) c[1] = c[0] + (c[3] - c[0]) * (1.0f / 3.0f);
        if (pts[(std::size_t)j].mode == Mode::Linear) c[2] = c[3] + (c[0] - c[3]) * (1.0f / 3.0f);
        return;
    }
    Vec3 in, out;
    Effective(pts, i, in, out); c[1] = c[0] + out;
    Effective(pts, j, in, out); c[2] = c[3] + in;
}

Vec3 Spline::HandleIn(int index) const {
    if (index < 0 || index >= Count()) return Vec3{0, 0, 0};
    Vec3 in, out; Effective(points, index, in, out); return in;
}
Vec3 Spline::HandleOut(int index) const {
    if (index < 0 || index >= Count()) return Vec3{0, 0, 0};
    Vec3 in, out; Effective(points, index, in, out); return out;
}

void Spline::ApplyAutoHandles() {
    const std::vector<SplinePoint> before = points;
    for (int i = 0; i < Count(); ++i) {
        if (Explicit(points[(std::size_t)i].mode)) continue;
        Vec3 in, out; Effective(before, i, in, out);
        points[(std::size_t)i].tangentIn = in; points[(std::size_t)i].tangentOut = out;
    }
}

int Spline::AddPoint(const Vec3& localPosition) {
    SplinePoint p; p.position = localPosition;
    points.push_back(p);
    return Count() - 1;
}

int Spline::InsertPoint(int index, const SplinePoint& p) {
    index = std::clamp(index, 0, Count());
    points.insert(points.begin() + index, p);
    return index;
}

void Spline::SetMode(int index, SplinePoint::Mode mode) {
    if (index < 0 || index >= Count()) return;
    SplinePoint& p = points[(std::size_t)index];
    Vec3 in, out; Effective(points, index, in, out);
    if (mode == Mode::Aligned && p.mode == Mode::Free) in = Unit(out, Vec3{0, 0, 1}) * -in.Magnitude();
    p.tangentIn = in; p.tangentOut = out; p.mode = mode;
}

void Spline::SetHandle(int index, bool out, const Vec3& localOffset, bool breakAlignment) {
    if (index < 0 || index >= Count()) return;
    SplinePoint& p = points[(std::size_t)index];
    if (!Explicit(p.mode)) {
        Vec3 hi, ho; Effective(points, index, hi, ho);
        p.tangentIn = hi; p.tangentOut = ho;
        p.mode = (breakAlignment || p.mode == Mode::Linear) ? Mode::Free : Mode::Aligned;
    } else if (breakAlignment) {
        p.mode = Mode::Free;
    }
    (out ? p.tangentOut : p.tangentIn) = localOffset;
    if (p.mode == Mode::Aligned) {
        Vec3& other = out ? p.tangentIn : p.tangentOut;
        const float len = other.Magnitude();
        if (localOffset.SqrMagnitude() > 1e-12f) other = Unit(localOffset, Vec3{0, 0, 1}) * -len;
    }
}

int Spline::SplitAt(float t) {
    const int segs = SegmentCount();
    if (segs <= 0) return -1;
    const float ft = ClampParam(t) * (float)segs;
    int s = std::min((int)std::floor(ft), segs - 1);
    float u = ft - (float)s;
    if (u < 1e-4f || u > 1.0f - 1e-4f) return -1;   // on an existing point
    const int n = Count(), i = s, j = (s + 1) % n;
    SplinePoint m;
    m.roll = points[(std::size_t)i].roll + (points[(std::size_t)j].roll - points[(std::size_t)i].roll) * u;
    m.scale = points[(std::size_t)i].scale + (points[(std::size_t)j].scale - points[(std::size_t)i].scale) * u;
    Vec3 c[4];
    Controls(points, s, c);
    if (type == Type::CatmullRom) {
        m.position = Bez(c, u);
        return InsertPoint(i + 1, m);
    }
    // de Casteljau: the two halves are exactly the old segment.
    const Vec3 p01 = Vec3::LerpUnclamped(c[0], c[1], u), p12 = Vec3::LerpUnclamped(c[1], c[2], u),
               p23 = Vec3::LerpUnclamped(c[2], c[3], u);
    const Vec3 p012 = Vec3::LerpUnclamped(p01, p12, u), p123 = Vec3::LerpUnclamped(p12, p23, u);
    m.position = Vec3::LerpUnclamped(p012, p123, u);
    const bool straight = points[(std::size_t)i].mode == Mode::Linear && points[(std::size_t)j].mode == Mode::Linear;
    if (straight) {
        m.mode = Mode::Linear;   // both halves stay straight lines
    } else {
        // Freeze the computed handles of both ends so the new neighbour can't move them.
        for (int k : {i, j}) {
            SplinePoint& e = points[(std::size_t)k];
            if (Explicit(e.mode)) continue;
            Vec3 in, out; Effective(points, k, in, out);
            e.tangentIn = in; e.tangentOut = out;
            e.mode = e.mode == Mode::Linear ? Mode::Free : Mode::Aligned;
        }
        points[(std::size_t)i].tangentOut = p01 - c[0];
        points[(std::size_t)j].tangentIn = p23 - c[3];
        m.mode = Mode::Aligned;
        m.tangentIn = p012 - m.position;
        m.tangentOut = p123 - m.position;
    }
    return InsertPoint(i + 1, m);
}

bool Spline::RemovePoint(int index) {
    const int n = Count();
    if (index < 0 || index >= n) return false;
    if (type == Type::Bezier && n >= 3 && (closed || (index > 0 && index < n - 1))) {
        // Undo a split: a point inserted at u has handles u : (1 - u) of the merged
        // segment's middle span, and its neighbours' handles were shortened by u
        // and (1 - u). Lengthen them back.
        Vec3 in, out; Effective(points, index, in, out);
        const float li = in.Magnitude(), lo = out.Magnitude();
        if (li + lo > 1e-6f) {
            const float u = li / (li + lo);
            if (u > 0.02f && u < 0.98f) {
                SplinePoint& a = points[(std::size_t)((index - 1 + n) % n)];
                SplinePoint& b = points[(std::size_t)((index + 1) % n)];
                if (Explicit(a.mode)) a.tangentOut = a.tangentOut * (1.0f / u);
                if (Explicit(b.mode)) b.tangentIn = b.tangentIn * (1.0f / (1.0f - u));
            }
        }
    }
    points.erase(points.begin() + index);
    return true;
}

// ---- The arc-length table ------------------------------------------------------

const Spline::Table& Spline::Ensure() const {
    const int res = std::clamp(resolution, 4, 256);
    const bool same = m_t.valid && m_t.type == type && m_t.closed == closed && m_t.up == upMode && m_t.res == res &&
                      m_t.pts.size() == points.size() &&
                      (points.empty() || std::memcmp(m_t.pts.data(), points.data(), points.size() * sizeof(SplinePoint)) == 0);
    if (!same) Build();
    return m_t;
}

void Spline::Build() const {
    Table& T = m_t;
    T.pts = points; T.type = type; T.closed = closed; T.up = upMode;
    T.res = std::clamp(resolution, 4, 256);
    T.valid = true; T.worldValid = false;
    ++m_revision;
    const int n = (int)points.size();
    T.segs = SegmentCount();
    T.perSeg = T.res;
    T.ctrl.assign((std::size_t)T.segs * 4, Vec3{0, 0, 0});
    for (int s = 0; s < T.segs; ++s) Controls(points, s, &T.ctrl[(std::size_t)s * 4]);
    const int count = T.segs > 0 ? T.segs * T.perSeg + 1 : (n == 1 ? 1 : 0);
    T.t.assign((std::size_t)count, 0.0f); T.len.assign((std::size_t)count, 0.0f);
    T.pos.assign((std::size_t)count, Vec3{0, 0, 0}); T.tan.assign((std::size_t)count, Vec3{0, 0, 1});
    T.upv.assign((std::size_t)count, Vec3{0, 1, 0}); T.scl.assign((std::size_t)count, Vec2{1, 1});
    if (count == 0) return;
    if (T.segs == 0) {   // a single point
        T.pos[0] = points[0].position; T.scl[0] = points[0].scale;
        T.upv[0] = Turn(Vec3{0, 1, 0}, Vec3{0, 0, 1}, -points[0].roll * Mathf::Deg2Rad);
        return;
    }
    std::vector<float> roll((std::size_t)count, 0.0f);
    for (int k = 0; k < count; ++k) {
        const int s = std::min(k / T.perSeg, T.segs - 1);
        const float u = (float)(k - s * T.perSeg) / (float)T.perSeg;
        const Vec3* c = &T.ctrl[(std::size_t)s * 4];
        T.pos[(std::size_t)k] = Bez(c, u);
        const Vec3 d = BezD(c, u);
        // A zero derivative (handle on its point) at an end: use the chord instead.
        const Vec3 fb = k > 0 ? T.tan[(std::size_t)k - 1] : Unit(c[3] - c[0], Vec3{0, 0, 1});
        T.tan[(std::size_t)k] = Unit(d, Unit(Bez(c, std::min(1.0f, u + 1e-3f)) - Bez(c, std::max(0.0f, u - 1e-3f)), fb));
        T.t[(std::size_t)k] = ((float)s + u) / (float)T.segs;
        if (k > 0) T.len[(std::size_t)k] = T.len[(std::size_t)k - 1] + (T.pos[(std::size_t)k] - T.pos[(std::size_t)k - 1]).Magnitude();
        const SplinePoint& a = points[(std::size_t)s];
        const SplinePoint& b = points[(std::size_t)((s + 1) % n)];
        roll[(std::size_t)k] = a.roll + (b.roll - a.roll) * u;
        T.scl[(std::size_t)k] = a.scale + (b.scale - a.scale) * u;
    }
    // Frames. Minimal: rotation-minimising frames by double reflection, so the up
    // vector never flips (a helix, a loop, a corkscrew). Closed: the twist left over
    // after one lap is spread along the length so the frame meets itself.
    if (upMode == UpMode::World) {
        Vec3 prev = StartUp(T.tan[0]);
        for (int k = 0; k < count; ++k) {
            const Vec3& tk = T.tan[(std::size_t)k];
            T.upv[(std::size_t)k] = std::fabs(Vec3::Dot(tk, Vec3{0, 1, 0})) < 0.999f ? Perp(Vec3{0, 1, 0}, tk, prev) : Perp(prev, tk, StartUp(tk));
            prev = T.upv[(std::size_t)k];
        }
    } else {
        T.upv[0] = StartUp(T.tan[0]);
        for (int k = 0; k + 1 < count; ++k) {
            const Vec3 v1 = T.pos[(std::size_t)k + 1] - T.pos[(std::size_t)k];
            const float c1 = Vec3::Dot(v1, v1);
            const Vec3& r = T.upv[(std::size_t)k];
            const Vec3& tk = T.tan[(std::size_t)k];
            Vec3 rL = r, tL = tk;
            if (c1 > 1e-12f) {
                rL = r - v1 * (2.0f / c1 * Vec3::Dot(v1, r));
                tL = tk - v1 * (2.0f / c1 * Vec3::Dot(v1, tk));
            }
            const Vec3 v2 = T.tan[(std::size_t)k + 1] - tL;
            const float c2 = Vec3::Dot(v2, v2);
            Vec3 rn = c2 > 1e-12f ? rL - v2 * (2.0f / c2 * Vec3::Dot(v2, rL)) : rL;
            T.upv[(std::size_t)k + 1] = Perp(rn, T.tan[(std::size_t)k + 1], StartUp(T.tan[(std::size_t)k + 1]));
        }
        if (closed && T.len.back() > 1e-6f) {
            const Vec3& t0 = T.tan[0];
            const Vec3 end = Perp(T.upv.back(), t0, T.upv[0]);
            const float ang = std::atan2(Vec3::Dot(Vec3::Cross(T.upv[0], end), t0), Vec3::Dot(T.upv[0], end));
            const float total = T.len.back();
            for (int k = 1; k < count; ++k)
                T.upv[(std::size_t)k] = Turn(T.upv[(std::size_t)k], T.tan[(std::size_t)k], -ang * T.len[(std::size_t)k] / total);
        }
    }
    // Roll: positive banks the up vector towards the right (clockwise seen from behind).
    for (int k = 0; k < count; ++k)
        if (roll[(std::size_t)k] != 0.0f)
            T.upv[(std::size_t)k] = Turn(T.upv[(std::size_t)k], T.tan[(std::size_t)k], -roll[(std::size_t)k] * Mathf::Deg2Rad);
}

const std::vector<float>& Spline::WorldLengths(const Mat4& m) const {
    const Table& T = Ensure();
    float g[6];
    MetricOf(m, g);
    bool same = T.worldValid;
    for (int i = 0; i < 6 && same; ++i) same = std::fabs(g[i] - T.metric[i]) <= 1e-6f * (1.0f + std::fabs(g[i]));
    if (!same) {
        std::memcpy(m_t.metric, g, sizeof(g));
        m_t.wlen.assign(T.len.size(), 0.0f);
        for (std::size_t k = 1; k < T.pos.size(); ++k)
            m_t.wlen[k] = m_t.wlen[k - 1] + MetricLen(g, T.pos[k] - T.pos[k - 1]);
        m_t.worldValid = true;
    }
    return m_t.wlen;
}

std::uint64_t Spline::Revision() const { Ensure(); return m_revision; }

Mat4 Spline::LocalToWorld() const { return transform ? transform->LocalToWorldMatrix() : Mat4::Identity(); }

// ---- Evaluation --------------------------------------------------------------

float Spline::ClampParam(float t) const {
    if (!std::isfinite(t)) return 0.0f;
    if (closed) { t -= std::floor(t); return t; }
    return Mathf::Clamp01(t);
}

float Spline::WrapDistance(float d, float length) const {
    if (!std::isfinite(d) || length <= 0.0f) return 0.0f;
    if (closed) { d = std::fmod(d, length); if (d < 0.0f) d += length; return d; }
    return Mathf::Clamp(d, 0.0f, length);
}

SplineSample Spline::LocalAt(float t) const {
    const Table& T = Ensure();
    SplineSample s;
    if (T.pos.empty()) return s;
    if (T.segs == 0) {
        s.position = T.pos[0]; s.up = T.upv[0]; s.right = Vec3::Cross(s.up, s.tangent); s.scale = T.scl[0];
        return s;
    }
    t = ClampParam(t);
    s.t = t;
    const float ft = t * (float)T.segs;
    const int seg = std::min((int)std::floor(ft), T.segs - 1);
    const float u = ft - (float)seg;
    const Vec3* c = &T.ctrl[(std::size_t)seg * 4];
    s.position = Bez(c, u);
    // Frame from the table, around the exact tangent.
    const float fk = (float)seg * (float)T.perSeg + u * (float)T.perSeg;
    const int k0 = std::min((int)std::floor(fk), (int)T.pos.size() - 2);
    const float f = Mathf::Clamp01(fk - (float)k0);
    const Vec3 tableTan = Unit(T.tan[(std::size_t)k0] * (1.0f - f) + T.tan[(std::size_t)k0 + 1] * f, T.tan[(std::size_t)k0]);
    s.tangent = Unit(BezD(c, u), tableTan);
    const Vec3 up = T.upv[(std::size_t)k0] * (1.0f - f) + T.upv[(std::size_t)k0 + 1] * f;
    s.up = Perp(up, s.tangent, Perp(T.upv[(std::size_t)k0], s.tangent, StartUp(s.tangent)));
    s.right = Vec3::Cross(s.up, s.tangent);
    s.scale = T.scl[(std::size_t)k0] + (T.scl[(std::size_t)k0 + 1] - T.scl[(std::size_t)k0]) * f;
    s.distance = T.len[(std::size_t)k0] + (T.len[(std::size_t)k0 + 1] - T.len[(std::size_t)k0]) * f;
    return s;
}

SplineSample Spline::ToWorld(const SplineSample& s, const Mat4& m) const {
    SplineSample w = s;
    w.position = m.MultiplyPoint(s.position);
    w.tangent = Unit(m.MultiplyVector(s.tangent), s.tangent);
    w.up = Perp(m.MultiplyVector(s.up), w.tangent, StartUp(w.tangent));
    w.right = Vec3::Cross(w.up, w.tangent);
    return w;
}

SplineSample Spline::LocalSample(float t) const { return LocalAt(t); }

SplineSample Spline::Sample(float t) const {
    const Mat4 m = LocalToWorld();
    SplineSample w = ToWorld(LocalAt(t), m);
    w.distance = LengthFromParam(WorldLengths(m), w.t);
    return w;
}

Vec3 Spline::EvaluatePosition(float t) const {
    if (points.empty()) return transform ? transform->Position() : Vec3{0, 0, 0};
    return LocalToWorld().MultiplyPoint(LocalAt(t).position);
}
Vec3 Spline::EvaluateTangent(float t) const {
    const SplineSample s = LocalAt(t);
    return Unit(LocalToWorld().MultiplyVector(s.tangent), s.tangent);
}
Vec3 Spline::EvaluateUp(float t) const { return ToWorld(LocalAt(t), LocalToWorld()).up; }
Vec2 Spline::EvaluateScale(float t) const { return LocalAt(t).scale; }

float Spline::LocalLength() const {
    const Table& T = Ensure();
    return T.len.empty() ? 0.0f : T.len.back();
}

float Spline::Length() const {
    const std::vector<float>& w = WorldLengths(LocalToWorld());
    return w.empty() ? 0.0f : w.back();
}

// Parameter at distance `d` along a cumulative-length table (same samples as the table).
float Spline::ParamFromLengths(const std::vector<float>& lengths, float d) const {
    const Table& T = Ensure();
    if (lengths.size() < 2) return 0.0f;
    d = WrapDistance(d, lengths.back());
    auto it = std::upper_bound(lengths.begin(), lengths.end(), d);
    int k = (int)(it - lengths.begin()) - 1;
    k = std::clamp(k, 0, (int)lengths.size() - 2);
    const float span = lengths[(std::size_t)k + 1] - lengths[(std::size_t)k];
    const float f = span > 1e-9f ? Mathf::Clamp01((d - lengths[(std::size_t)k]) / span) : 0.0f;
    return T.t[(std::size_t)k] + (T.t[(std::size_t)k + 1] - T.t[(std::size_t)k]) * f;
}

float Spline::LengthFromParam(const std::vector<float>& lengths, float t) const {
    const Table& T = Ensure();
    if (lengths.size() < 2 || T.segs == 0) return 0.0f;
    t = ClampParam(t);
    if (closed && t == 0.0f) return 0.0f;
    const float fk = t * (float)(T.segs * T.perSeg);
    const int k = std::min((int)std::floor(fk), (int)lengths.size() - 2);
    const float f = Mathf::Clamp01(fk - (float)k);
    return lengths[(std::size_t)k] + (lengths[(std::size_t)k + 1] - lengths[(std::size_t)k]) * f;
}

float Spline::ParameterAtDistance(float distance) const { return ParamFromLengths(WorldLengths(LocalToWorld()), distance); }
float Spline::DistanceAtParameter(float t) const { return LengthFromParam(WorldLengths(LocalToWorld()), t); }

SplineSample Spline::SampleAtDistance(float distance) const {
    const Mat4 m = LocalToWorld();
    const std::vector<float>& w = WorldLengths(m);
    const float d = w.empty() ? 0.0f : WrapDistance(distance, w.back());
    SplineSample s = ToWorld(LocalAt(ParamFromLengths(w, d)), m);
    s.distance = d;
    return s;
}

SplineSample Spline::LocalSampleAtDistance(float localDistance) const {
    const Table& T = Ensure();
    const float d = T.len.empty() ? 0.0f : WrapDistance(localDistance, T.len.back());
    SplineSample s = LocalAt(ParamFromLengths(T.len, d));
    s.distance = d;
    return s;
}

float Spline::NearestParameter(const Vec3& world, Vec3* point) const {
    const Table& T = Ensure();
    if (T.pos.empty()) { if (point) *point = EvaluatePosition(0.0f); return 0.0f; }
    const Mat4 m = LocalToWorld();
    const int count = (int)T.pos.size();
    int best = 0; float bestD = 1e30f;
    for (int k = 0; k < count; ++k) {
        const float d = (m.MultiplyPoint(T.pos[(std::size_t)k]) - world).SqrMagnitude();
        if (d < bestD) { bestD = d; best = k; }
    }
    auto dist2 = [&](float t) { return (m.MultiplyPoint(LocalAt(t).position) - world).SqrMagnitude(); };
    // Refine on both sides of the closest sample (golden-section search).
    auto refine = [&](float lo, float hi, float& bt, float& bd) {
        const float g = 0.61803398875f;
        float a = lo, b = hi, x1 = b - g * (b - a), x2 = a + g * (b - a);
        float f1 = dist2(x1), f2 = dist2(x2);
        for (int it = 0; it < 30; ++it) {
            if (f1 < f2) { b = x2; x2 = x1; f2 = f1; x1 = b - g * (b - a); f1 = dist2(x1); }
            else         { a = x1; x1 = x2; f1 = f2; x2 = a + g * (b - a); f2 = dist2(x2); }
        }
        const float t = 0.5f * (a + b), d = dist2(t);
        if (d < bd) { bd = d; bt = t; }
    };
    float bt = T.t[(std::size_t)best], bd = bestD;
    if (T.segs > 0) {
        if (best > 0) refine(T.t[(std::size_t)best - 1], T.t[(std::size_t)best], bt, bd);
        else if (closed) refine(T.t[(std::size_t)count - 2], 1.0f, bt, bd);
        if (best < count - 1) refine(T.t[(std::size_t)best], T.t[(std::size_t)best + 1], bt, bd);
        else if (closed) refine(0.0f, T.t[1], bt, bd);
    }
    bt = ClampParam(bt);
    if (point) *point = m.MultiplyPoint(LocalAt(bt).position);
    return bt;
}

Vec3 Spline::NearestPoint(const Vec3& world) const {
    Vec3 p; NearestParameter(world, &p); return p;
}

float Spline::NearestDistance(const Vec3& world) const { return DistanceAtParameter(NearestParameter(world)); }

Spline* Spline::Resolve(const Component* self, const std::string& objectName) {
    if (!self || !self->gameObject) return nullptr;
    if (objectName.empty()) return self->gameObject->GetComponent<Spline>();
    Scene* s = self->gameObject->scene();
    GameObject* g = s ? s->Find(objectName) : nullptr;
    return g ? g->GetComponent<Spline>() : nullptr;
}

// ---- SplineFollower ----------------------------------------------------------

Spline* SplineFollower::Target() const {
    Scene* s = gameObject ? gameObject->scene() : nullptr;
    if (!s || spline.empty()) return nullptr;
    if (!m_cached || m_cachedVersion != s->StructureVersion() || m_cachedName != spline) {
        m_cached = s->Find(spline);
        m_cachedVersion = s->StructureVersion();
        m_cachedName = spline;
    }
    return m_cached ? m_cached->GetComponent<Spline>() : nullptr;
}

float SplineFollower::NormalizedPosition() const {
    const Spline* sp = Target();
    const float len = sp ? sp->Length() : 0.0f;
    return len > 0.0f ? m_distance / len : 0.0f;
}

void SplineFollower::Start() {
    const Spline* sp = Target();
    m_distance = sp ? Mathf::Clamp01(startAt) * sp->Length() : 0.0f;
    m_dir = 1;
    m_playing = playOnStart;
    Place();
}

void SplineFollower::Update(float dt) {
    if (m_playing) Step(dt);
}

void SplineFollower::SetDistance(float d) {
    const Spline* sp = Target();
    const float len = sp ? sp->Length() : 0.0f;
    m_distance = len > 0.0f ? (sp->closed ? std::fmod(std::fmod(d, len) + len, len) : Mathf::Clamp(d, 0.0f, len)) : 0.0f;
    Place();
}

void SplineFollower::Step(float dt) {
    Spline* sp = Target();
    if (!sp || dt <= 0.0f) return;
    const float len = sp->Length();
    if (len <= 1e-6f) { Place(); return; }
    const float rate = drive == Drive::Time ? (duration > 1e-4f ? len / duration : 0.0f) : speed;
    m_distance += rate * (float)m_dir * dt;
    // Several ends in one step (a long frame, a short curve): handle each.
    for (int guard = 0; guard < 64; ++guard) {
        if (m_distance > len) {
            if (wrap == Wrap::Loop) { m_distance -= len; Reached(true); }
            else if (wrap == Wrap::PingPong) { m_distance = 2.0f * len - m_distance; m_dir = -m_dir; Reached(true); }
            else { m_distance = len; m_playing = false; Reached(true); break; }
        } else if (m_distance < 0.0f) {
            if (wrap == Wrap::Loop) { m_distance += len; Reached(false); }
            else if (wrap == Wrap::PingPong) { m_distance = -m_distance; m_dir = -m_dir; Reached(false); }
            else { m_distance = 0.0f; m_playing = false; Reached(false); break; }
        } else {
            break;
        }
    }
    m_distance = Mathf::Clamp(m_distance, 0.0f, len);
    Place();
}

void SplineFollower::Place() {
    Spline* sp = Target();
    if (!sp || !transform || sp->Count() == 0) return;
    const SplineSample s = sp->SampleAtDistance(m_distance);
    transform->SetPosition(s.position + s.right * offset.x + s.up * offset.y + s.tangent * offset.z);
    if (orient) {
        // Face the way it moves (a ping-pong turns round on the way back).
        const float sign = (drive == Drive::Speed && speed < 0.0f ? -1.0f : 1.0f) * (float)m_dir;
        transform->SetRotation(Quat::LookRotation(s.tangent * sign, s.up));
    }
}

void SplineFollower::Reached(bool atEnd) {
    if (onEnd) onEnd(atEnd);
    if (!gameObject || !ScriptsRun(gameObject)) return;
    const std::vector<vs::VsValue> args{vs::VsValue(atEnd ? "end" : "start")};
    for (auto* sc : gameObject->GetComponents<ScriptComponent>())
        if (sc->VM()) sc->VM()->CallEventArgs("on_spline_end", args);
    for (auto* al : gameObject->GetComponents<ActionList>()) al->ReceiveMessage("spline_end");
}

} // namespace okay
