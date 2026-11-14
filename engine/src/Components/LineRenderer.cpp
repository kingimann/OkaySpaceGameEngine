#include "okay/Components/LineRenderer.hpp"
#include "okay/Components/Spline.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include <algorithm>
#include <cmath>

namespace okay {

namespace {

Vec3 Unit(const Vec3& v, const Vec3& fallback) {
    const float m = v.Magnitude();
    return m > 1e-8f ? v * (1.0f / m) : fallback;
}
Vec3 Turn(const Vec3& v, const Vec3& axis, float rad) {
    const float c = std::cos(rad), s = std::sin(rad);
    return v * c + Vec3::Cross(axis, v) * s + axis * (Vec3::Dot(axis, v) * (1.0f - c));
}
// Any unit vector perpendicular to `d`.
Vec3 AnyPerp(const Vec3& d) {
    const Vec3 ref = std::fabs(d.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    return Unit(Vec3::Cross(d, ref), Vec3{1, 0, 0});
}
Color Tint(const Color& a, const Color& b) { return {a.r * b.r, a.g * b.g, a.b * b.b, a.a * b.a}; }

} // namespace

// ---- The ribbon ----------------------------------------------------------------

void BuildRibbon(const std::vector<RibbonPoint>& in, const RibbonOptions& opt, Ribbon& out) {
    out.vertices.clear();
    out.indices.clear();
    std::vector<RibbonPoint> p;
    p.reserve(in.size());
    for (const RibbonPoint& q : in)
        if (p.empty() || (q.position - p.back().position).SqrMagnitude() > 1e-12f) p.push_back(q);
    bool loop = opt.loop;
    if (loop && p.size() > 2 && (p.back().position - p.front().position).SqrMagnitude() <= 1e-12f) p.pop_back();
    const int n = (int)p.size();
    if (n < 2) return;
    if (n < 3) loop = false;   // two points make a line, not a loop
    const int m = loop ? n : n - 1;
    std::vector<Vec3> dir((std::size_t)m);
    for (int j = 0; j < m; ++j)
        dir[(std::size_t)j] = Unit(p[(std::size_t)((j + 1) % n)].position - p[(std::size_t)j].position,
                                   j > 0 ? dir[(std::size_t)j - 1] : Vec3{1, 0, 0});
    const int k = std::clamp(opt.cornerVertices, 0, 32), caps = std::clamp(opt.capVertices, 0, 32);
    auto facing = [&](int i) {
        return opt.alignment == LineAlignment::Flat ? Unit(opt.flatNormal, Vec3{0, 1, 0})
                                                    : Unit(opt.eye - p[(std::size_t)i].position, AnyPerp(dir[0]));
    };
    auto add = [&](const Vec3& pos, const RibbonPoint& rp, float u, float v) {
        out.vertices.push_back({pos, rp.color, Vec2{u, v}});
        return (int)out.vertices.size() - 1;
    };
    auto tri = [&](int a, int b, int c) { out.indices.insert(out.indices.end(), {a, b, c}); };
    // A joint that really bends is rounded (cornerVertices > 0); a straight one, like
    // the extra points CollectLineDraws adds along long segments, stays a plain pair.
    auto rounded = [&](int i) {
        return k > 0 && Vec3::Dot(dir[(std::size_t)((i - 1 + m) % m)], dir[(std::size_t)(i % m)]) < 0.99995f;
    };
    struct Pair { int a = -1, b = -1; };   // a: the +side edge (v = 0), b: the -side edge (v = 1)
    auto quad = [&](const Pair& s, const Pair& e) { tri(s.a, e.a, e.b); tri(s.a, e.b, s.b); };

    Vec3 lastSide = AnyPerp(dir[0]);
    // A joint's sides: perpendicular to each segment, in the ribbon's plane.
    auto sides = [&](int i, Vec3& f, Vec3& sIn, Vec3& sOut) {
        f = facing(i);
        const bool hasIn = loop || i > 0, hasOut = loop || i < n - 1;
        if (hasIn)  sIn  = Unit(Vec3::Cross(dir[(std::size_t)((i - 1 + m) % m)], f), lastSide);
        if (hasOut) sOut = Unit(Vec3::Cross(dir[(std::size_t)(i % m)], f), lastSide);
        if (!hasIn) sIn = sOut;
        if (!hasOut) sOut = sIn;
        lastSide = sOut;
    };
    // The mitred direction and length (capped so sharp bends don't spike).
    auto miter = [](const Vec3& sIn, const Vec3& sOut, float& scale) {
        const Vec3 mit = Unit(sIn + sOut, sOut);
        const float c = Vec3::Dot(mit, sOut);
        scale = c > 0.25f ? 1.0f / c : 4.0f;
        return mit;
    };
    // Square or mitred pair at a point.
    auto pairAt = [&](int i, const Vec3& sIn, const Vec3& sOut, float u) {
        const RibbonPoint& rp = p[(std::size_t)i];
        float scale = 1.0f;
        const Vec3 mit = miter(sIn, sOut, scale);
        const float hw = rp.width * 0.5f * scale;
        Pair q;
        q.a = add(rp.position + mit * hw, rp, u, 0.0f);
        q.b = add(rp.position - mit * hw, rp, u, 1.0f);
        return q;
    };
    // A rounded bend: the inner corner is mitred, the outer edge is an arc of k
    // steps from the incoming side to the outgoing one. Returns the pair the
    // incoming segment ends on and the one the outgoing segment starts from.
    auto roundAt = [&](int i, const Vec3& f, const Vec3& sIn, const Vec3& sOut, float u, const Vec3& outDir,
                       Pair& endPair, Pair& startPair) {
        const RibbonPoint& rp = p[(std::size_t)i];
        const float hw = rp.width * 0.5f;
        const bool aInner = Vec3::Dot(outDir, sIn) > 0.0f;   // turning towards +side: that side is inside
        const float outer = aInner ? -1.0f : 1.0f;
        float scale = 1.0f;
        const Vec3 mit = miter(sIn, sOut, scale);
        const int inner = add(rp.position - mit * (outer * hw * scale), rp, u, aInner ? 0.0f : 1.0f);
        const float vOuter = aInner ? 1.0f : 0.0f;
        const Vec3 oIn = sIn * outer, oOut = sOut * outer;
        const float ang = std::atan2(Vec3::Dot(Vec3::Cross(oIn, oOut), f), Vec3::Dot(oIn, oOut));
        int prev = add(rp.position + oIn * hw, rp, u, vOuter);
        endPair = aInner ? Pair{inner, prev} : Pair{prev, inner};
        for (int s = 1; s <= k + 1; ++s) {
            const Vec3 o = s == k + 1 ? oOut : Turn(oIn, f, ang * (float)s / (float)(k + 1));
            const int cur = add(rp.position + o * hw, rp, u, vOuter);
            tri(inner, prev, cur);
            prev = cur;
        }
        startPair = aInner ? Pair{inner, prev} : Pair{prev, inner};
    };
    // A rounded end: a half disc of `caps` steps around the end, behind it (back = -1)
    // or past it (back = +1).
    auto capAt = [&](int i, const Vec3& f, const Vec3& side, const Pair& q, float back) {
        const RibbonPoint& rp = p[(std::size_t)i];
        const float hw = rp.width * 0.5f;
        const Vec3 along = Unit(Vec3::Cross(f, side), dir[0]) * back;
        const int center = add(rp.position, rp, rp.u, 0.5f);
        int prev = q.a;
        for (int s = 1; s <= caps; ++s) {
            const float th = 3.14159265f * (float)s / (float)(caps + 1);
            const int cur = add(rp.position + (side * std::cos(th) + along * std::sin(th)) * hw, rp, rp.u, 0.5f - 0.5f * std::cos(th));
            tri(center, prev, cur);
            prev = cur;
        }
        tri(center, prev, q.b);
    };

    Vec3 f, sIn, sOut;
    // First point: the start of segment 0.
    sides(0, f, sIn, sOut);
    Pair start;
    const Vec3 f0 = f, sIn0 = sIn, sOut0 = sOut;
    if (loop && rounded(0)) {
        // A rounded first bend: its fan is drawn where the loop closes; here only
        // the pair the first segment starts from (same corner as roundAt's).
        const RibbonPoint& rp = p[0];
        const float hw = rp.width * 0.5f;
        const bool aInner = Vec3::Dot(dir[0], sIn) > 0.0f;
        const float outer = aInner ? -1.0f : 1.0f;
        float scale = 1.0f;
        const Vec3 mit = miter(sIn, sOut, scale);
        const int inner = add(rp.position - mit * (outer * hw * scale), rp, rp.u, aInner ? 0.0f : 1.0f);
        const int o = add(rp.position + sOut * (outer * hw), rp, rp.u, aInner ? 1.0f : 0.0f);
        start = aInner ? Pair{inner, o} : Pair{o, inner};
    } else {
        start = pairAt(0, loop ? sIn : sOut, sOut, p[0].u);
    }
    if (!loop && caps > 0) {
        // The start cap is built against the outgoing side.
        capAt(0, f, sOut, start, -1.0f);
    }
    // Every other point.
    for (int i = 1; i < n; ++i) {
        sides(i, f, sIn, sOut);
        const float u = p[(std::size_t)i].u;
        if (!loop && i == n - 1) {
            const Pair end = pairAt(i, sIn, sIn, u);
            quad(start, end);
            if (caps > 0) capAt(i, f, sIn, end, 1.0f);
        } else if (rounded(i)) {
            Pair endPair, startPair;
            roundAt(i, f, sIn, sOut, u, dir[(std::size_t)(i % m)], endPair, startPair);
            quad(start, endPair);
            start = startPair;
        } else {
            const Pair q = pairAt(i, sIn, sOut, u);
            quad(start, q);
            start = q;
        }
    }
    if (loop) {
        // Back to the first point, carrying the closing u.
        if (rounded(0)) {
            Pair endPair, startPair;
            roundAt(0, f0, sIn0, sOut0, opt.loopEndU, dir[0], endPair, startPair);
            quad(start, endPair);
        } else {
            quad(start, pairAt(0, sIn0, sOut0, opt.loopEndU));
        }
    }
}

// ---- LineRenderer --------------------------------------------------------------

void LineRenderer::SetPosition(int index, const Vec3& p) {
    if (index < 0) return;
    if (index >= (int)positions.size()) positions.resize((std::size_t)index + 1, p);
    positions[(std::size_t)index] = p;
}

Vec3 LineRenderer::GetPosition(int index) const {
    return index >= 0 && index < (int)positions.size() ? positions[(std::size_t)index] : Vec3{0, 0, 0};
}

std::vector<Vec3> LineRenderer::WorldPoints() const {
    std::vector<Vec3> out;
    if (!spline.empty()) {
        const Spline* sp = Spline::Resolve(this, spline);
        if (sp && sp->Count() >= 2) {
            const int n = std::clamp(splineSamples, 2, 4096);
            const float len = sp->Length();
            out.reserve((std::size_t)n);
            for (int i = 0; i < n; ++i) {
                const float f = sp->closed ? (float)i / (float)n : (float)i / (float)(n - 1);
                out.push_back(sp->SampleAtDistance(f * len).position);
            }
            return out;
        }
    }
    out = positions;
    if (!useWorldSpace && transform) {
        const Mat4 m = transform->LocalToWorldMatrix();
        for (Vec3& p : out) p = m.MultiplyPoint(p);
    }
    return out;
}

void LineRenderer::RibbonInput(const Vec3& eye, std::vector<RibbonPoint>& pts, RibbonOptions& o) const {
    pts.clear();
    o = RibbonOptions{};
    const std::vector<Vec3> w = WorldPoints();
    const int n = (int)w.size();
    if (n < 2) return;
    const Spline* sp = spline.empty() ? nullptr : Spline::Resolve(this, spline);
    const bool lp = loop || (sp && sp->closed && sp->Count() >= 2);
    std::vector<float> dist((std::size_t)n, 0.0f);
    for (int i = 1; i < n; ++i) dist[(std::size_t)i] = dist[(std::size_t)i - 1] + (w[(std::size_t)i] - w[(std::size_t)i - 1]).Magnitude();
    const float total = dist.back() + (lp ? (w.front() - w.back()).Magnitude() : 0.0f);
    const float tile = tileLength > 1e-4f ? tileLength : 1.0f;
    pts.resize((std::size_t)n);
    for (int i = 0; i < n; ++i) {
        const float t = total > 1e-6f ? dist[(std::size_t)i] / total : 0.0f;
        RibbonPoint& rp = pts[(std::size_t)i];
        rp.position = w[(std::size_t)i];
        rp.width = width * (widthCurve.Active() ? widthCurve.Evaluate(t) : 1.0f);
        rp.color = colorGradient.Active() ? Tint(colorGradient.Evaluate(t), color) : color;
        rp.u = textureMode == LineTextureMode::Stretch ? t : dist[(std::size_t)i] / tile;
    }
    o.alignment = alignment;
    o.eye = eye;
    o.flatNormal = transform ? transform->Up() : Vec3{0, 1, 0};
    o.loop = lp;
    o.loopEndU = textureMode == LineTextureMode::Stretch ? 1.0f : total / tile;
    o.cornerVertices = cornerVertices;
    o.capVertices = capVertices;
}

void LineRenderer::Build(const Vec3& eye, Ribbon& out) const {
    std::vector<RibbonPoint> pts;
    RibbonOptions o;
    RibbonInput(eye, pts, o);
    BuildRibbon(pts, o, out);
}

// ---- TrailRenderer -------------------------------------------------------------

void TrailRenderer::Clear() { m_points.clear(); }

std::vector<Vec3> TrailRenderer::Points() const {
    std::vector<Vec3> out;
    out.reserve(m_points.size());
    for (const Point& p : m_points) out.push_back(p.position);
    return out;
}

void TrailRenderer::Update(float dt) {
    if (!transform) return;
    m_now += dt > 0.0f ? dt : 0.0f;
    const Vec3 p = transform->Position();
    // A jump (teleport, respawn) starts a new trail instead of a streak across the scene.
    if (m_hasLast && teleportDistance > 0.0f && (p - m_last).Magnitude() > teleportDistance) m_points.clear();
    m_last = p;
    m_hasLast = true;
    const float life = time > 1e-3f ? time : 1e-3f;
    while (!m_points.empty() && m_now - m_points.front().born > life) m_points.pop_front();
    if (emitting && (m_points.empty() || (p - m_points.back().position).Magnitude() >= std::max(minVertexDistance, 1e-4f))) {
        m_points.push_back({p, m_now});
        while (m_points.size() > 4096) m_points.pop_front();
    }
}

void TrailRenderer::RibbonInput(const Vec3& eye, std::vector<RibbonPoint>& pts, RibbonOptions& o) const {
    pts.clear();
    o = RibbonOptions{};
    struct Q { Vec3 pos; float age; };
    std::vector<Q> q;
    q.reserve(m_points.size() + 1);
    const float life = time > 1e-3f ? time : 1e-3f;
    for (const Point& p : m_points) q.push_back({p.position, Mathf::Clamp01((m_now - p.born) / life)});
    // The head follows the object between stored points.
    if (emitting && transform) {
        const Vec3 head = transform->Position();
        if (q.empty() || (head - q.back().pos).SqrMagnitude() > 1e-8f) q.push_back({head, 0.0f});
    }
    const int n = (int)q.size();
    if (n < 2) return;
    // u runs from the head (0) towards the tail.
    std::vector<float> fromHead((std::size_t)n, 0.0f);
    for (int i = n - 2; i >= 0; --i)
        fromHead[(std::size_t)i] = fromHead[(std::size_t)i + 1] + (q[(std::size_t)i + 1].pos - q[(std::size_t)i].pos).Magnitude();
    const float total = fromHead[0];
    const float tile = tileLength > 1e-4f ? tileLength : 1.0f;
    pts.resize((std::size_t)n);
    for (int i = 0; i < n; ++i) {
        const float age = q[(std::size_t)i].age;
        RibbonPoint& rp = pts[(std::size_t)i];
        rp.position = q[(std::size_t)i].pos;
        rp.width = width * (widthCurve.Active() ? widthCurve.Evaluate(age) : 1.0f);
        rp.color = colorGradient.Active() ? Tint(colorGradient.Evaluate(age), color) : color;
        rp.u = textureMode == LineTextureMode::Stretch ? (total > 1e-6f ? fromHead[(std::size_t)i] / total : 0.0f)
                                                       : fromHead[(std::size_t)i] / tile;
    }
    o.alignment = alignment;
    o.eye = eye;
    o.flatNormal = transform ? transform->Up() : Vec3{0, 1, 0};
}

void TrailRenderer::Build(const Vec3& eye, Ribbon& out) const {
    std::vector<RibbonPoint> pts;
    RibbonOptions o;
    RibbonInput(eye, pts, o);
    BuildRibbon(pts, o, out);
}

// ---- Drawing -------------------------------------------------------------------

namespace {

struct ClipV { Vec4 c; Color col; Vec2 uv; };

ClipV LerpV(const ClipV& a, const ClipV& b, float t) {
    ClipV r;
    r.c = Vec4{a.c.x + (b.c.x - a.c.x) * t, a.c.y + (b.c.y - a.c.y) * t, a.c.z + (b.c.z - a.c.z) * t, a.c.w + (b.c.w - a.c.w) * t};
    r.col = Color{a.col.r + (b.col.r - a.col.r) * t, a.col.g + (b.col.g - a.col.g) * t,
                  a.col.b + (b.col.b - a.col.b) * t, a.col.a + (b.col.a - a.col.a) * t};
    r.uv = a.uv + (b.uv - a.uv) * t;
    return r;
}

void AddRibbon(const Ribbon& rb, const std::string& texture, const Mat4& vp, const Vec3& eye, float vx, float vy,
               float vw, float vh, LineOcclusionFn occluded, std::vector<LineDraw>& out) {
    if (rb.indices.empty()) return;
    LineDraw d;
    d.texture = texture;
    std::vector<ClipV> cv(rb.vertices.size());
    Vec3 mid{0, 0, 0};
    for (std::size_t i = 0; i < rb.vertices.size(); ++i) {
        cv[i] = {vp * Vec4{rb.vertices[i].position, 1.0f}, rb.vertices[i].color, rb.vertices[i].uv};
        mid += rb.vertices[i].position;
    }
    mid = mid * (1.0f / (float)rb.vertices.size());
    d.depth = (mid - eye).Magnitude();
    auto emit = [&](const ClipV& v) {
        const float iw = 1.0f / v.c.w;
        const float nx = v.c.x * iw * 0.5f + 0.5f, ny = 0.5f - v.c.y * iw * 0.5f;
        LineDrawVertex o{vx + nx * vw, vy + ny * vh, v.uv.x, v.uv.y, v.col};
        if (occluded && occluded(nx, ny, v.c.w)) o.color.a = 0.0f;   // behind solid geometry
        d.tris.push_back(o);
    };
    for (std::size_t t = 0; t + 2 < rb.indices.size(); t += 3) {
        const ClipV in[3] = {cv[(std::size_t)rb.indices[t]], cv[(std::size_t)rb.indices[t + 1]], cv[(std::size_t)rb.indices[t + 2]]};
        // Clip against the near plane (z + w >= 0), keeping what is in front of the camera.
        ClipV poly[4]; int np = 0;
        for (int e = 0; e < 3; ++e) {
            const ClipV& a = in[e]; const ClipV& b = in[(e + 1) % 3];
            const float da = a.c.z + a.c.w, db = b.c.z + b.c.w;
            if (da >= 0.0f) poly[np++] = a;
            if ((da >= 0.0f) != (db >= 0.0f)) poly[np++] = LerpV(a, b, da / (da - db));
        }
        if (np < 3) continue;
        bool ok = true;
        for (int e = 0; e < np; ++e) ok = ok && poly[e].c.w > 1e-5f;
        if (!ok) continue;
        for (int e = 1; e + 1 < np; ++e) { emit(poly[0]); emit(poly[e]); emit(poly[e + 1]); }
    }
    if (!d.tris.empty()) out.push_back(std::move(d));
}

// The occlusion test is per vertex, so a long segment gets extra points every
// kOcclusionStep pixels on screen: a line passing behind something hides just there.
constexpr float kOcclusionStep = 12.0f;

// The length on screen (pixels) of the part of a-b in front of the camera.
float ScreenLength(const Vec3& a, const Vec3& b, const Mat4& vp, float vw, float vh) {
    Vec4 ca = vp * Vec4{a, 1.0f}, cb = vp * Vec4{b, 1.0f};
    const float da = ca.z + ca.w, db = cb.z + cb.w;
    if (da < 0.0f && db < 0.0f) return 0.0f;
    if (da < 0.0f) ca = ca + (cb - ca) * (da / (da - db));
    else if (db < 0.0f) cb = ca + (cb - ca) * (da / (da - db));
    if (ca.w <= 1e-5f || cb.w <= 1e-5f) return 0.0f;
    const float dx = (cb.x / cb.w - ca.x / ca.w) * 0.5f * vw, dy = (cb.y / cb.w - ca.y / ca.w) * 0.5f * vh;
    return std::sqrt(dx * dx + dy * dy);
}

void SplitForOcclusion(std::vector<RibbonPoint>& pts, const RibbonOptions& o, const Mat4& vp, float vw, float vh) {
    const std::size_t n = pts.size();
    if (n < 2) return;
    std::vector<RibbonPoint> out;
    out.reserve(n * 2);
    const std::size_t segs = o.loop ? n : n - 1;
    for (std::size_t i = 0; i < segs; ++i) {
        const RibbonPoint& a = pts[i];
        RibbonPoint b = pts[(i + 1) % n];
        if (i + 1 == n) b.u = o.loopEndU;   // the closing segment of a loop
        out.push_back(a);
        const int pieces = std::clamp((int)std::ceil(ScreenLength(a.position, b.position, vp, vw, vh) / kOcclusionStep), 1, 256);
        for (int s = 1; s < pieces; ++s) {
            const float t = (float)s / (float)pieces;
            RibbonPoint q;
            q.position = a.position + (b.position - a.position) * t;
            q.width = a.width + (b.width - a.width) * t;
            q.color = Color{a.color.r + (b.color.r - a.color.r) * t, a.color.g + (b.color.g - a.color.g) * t,
                            a.color.b + (b.color.b - a.color.b) * t, a.color.a + (b.color.a - a.color.a) * t};
            q.u = a.u + (b.u - a.u) * t;
            out.push_back(q);
        }
    }
    if (!o.loop) out.push_back(pts.back());
    pts.swap(out);
}

bool Drawn(const GameObject* go, const Component* c, const GameObject* ignore, int cullingMask) {
    if (!go || !go->active || !c->enabled) return false;
    if (go->HiddenFromCamera(ignore)) return false;
    return (cullingMask & (1 << (go->layer & 31))) != 0;
}

} // namespace

void CollectLineDraws(const Scene& scene, const Mat4& vp, const Vec3& eye, float viewX, float viewY,
                      float viewW, float viewH, std::vector<LineDraw>& out, const GameObject* ignore,
                      int cullingMask, LineOcclusionFn occluded) {
    out.clear();
    std::vector<RibbonPoint> pts;
    RibbonOptions opt;
    Ribbon rb;
    auto draw = [&](const auto* r) {
        r->RibbonInput(eye, pts, opt);
        if (occluded) SplitForOcclusion(pts, opt, vp, viewW, viewH);
        BuildRibbon(pts, opt, rb);
        AddRibbon(rb, r->texture, vp, eye, viewX, viewY, viewW, viewH, occluded, out);
    };
    for (const auto& up : scene.Objects()) {
        const GameObject* go = up.get();
        for (const LineRenderer* lr : go->GetComponents<LineRenderer>())
            if (Drawn(go, lr, ignore, cullingMask)) draw(lr);
        for (const TrailRenderer* tr : go->GetComponents<TrailRenderer>())
            if (Drawn(go, tr, ignore, cullingMask)) draw(tr);
    }
    std::stable_sort(out.begin(), out.end(), [](const LineDraw& a, const LineDraw& b) { return a.depth > b.depth; });
}

Mat4 LineView2D(const Vec3& camPos, float pixelsPerUnit, float viewW, float viewH) {
    Mat4 m;
    const float sx = viewW > 0.0f ? 2.0f * pixelsPerUnit / viewW : 1.0f;
    const float sy = viewH > 0.0f ? 2.0f * pixelsPerUnit / viewH : 1.0f;
    m.at(0, 0) = sx; m.at(3, 0) = -camPos.x * sx;
    m.at(1, 1) = sy; m.at(3, 1) = -camPos.y * sy;
    m.at(2, 2) = 0.0f;
    return m;
}

} // namespace okay
