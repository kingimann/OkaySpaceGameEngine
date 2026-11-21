// See ColliderShapes2D.hpp. Written from general convex-geometry definitions; no code from any other engine.
#include "okay/Physics/ColliderShapes2D.hpp"
#include "okay/Scene/ReflectedRecord.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace okay {
namespace shape2d {

void Piece::Bounds() {
    if (v.empty()) { mn = mx = {0, 0}; return; }
    mn = mx = v[0];
    for (const Vec2& p : v) { mn = Vec2::Min(mn, p); mx = Vec2::Max(mx, p); }
    mn = {mn.x - r, mn.y - r}; mx = {mx.x + r, mx.y + r};
}

Vec2 Support(const Piece& p, const Vec2& d) {
    if (p.v.empty()) return {0, 0};
    const Vec2* best = &p.v[0]; float bd = best->x * d.x + best->y * d.y;
    for (std::size_t i = 1; i < p.v.size(); ++i) {
        float x = p.v[i].x * d.x + p.v[i].y * d.y;
        if (x > bd) { bd = x; best = &p.v[i]; }
    }
    Vec2 out = *best;
    if (p.r > 0.0f) {
        float m = std::sqrt(d.x * d.x + d.y * d.y);
        if (m > 1e-12f) out = {out.x + d.x * (p.r / m), out.y + d.y * (p.r / m)};
    }
    return out;
}

namespace {
float Cross(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }
float Area2(const std::vector<Vec2>& p) {
    float a = 0; for (std::size_t i = 0; i < p.size(); ++i) a += Cross(p[i], p[(i + 1) % p.size()]); return a;
}
bool PointInTri(const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c) {
    return Cross(b - a, p - a) >= -1e-9f && Cross(c - b, p - b) >= -1e-9f && Cross(a - c, p - c) >= -1e-9f;
}
}  // namespace

std::vector<std::vector<Vec2>> ConvexPieces(const std::vector<Vec2>& in) {
    std::vector<Vec2> p = in;
    if (p.size() >= 2 && (p.front() - p.back()).Magnitude() < 1e-7f) p.pop_back();   // closed with a repeated point
    if (p.size() < 3) return {p};
    if (Area2(p) < 0.0f) std::reverse(p.begin(), p.end());                          // counter-clockwise
    bool convex = true;
    for (std::size_t i = 0; i < p.size() && convex; ++i)
        if (Cross(p[(i + 1) % p.size()] - p[i], p[(i + 2) % p.size()] - p[(i + 1) % p.size()]) < -1e-7f) convex = false;
    if (convex) return {p};
    // Ear clipping: cut off one triangle at a time (a corner that turns left with no other corner inside it).
    std::vector<std::vector<Vec2>> out;
    std::vector<int> idx(p.size()); for (std::size_t i = 0; i < p.size(); ++i) idx[i] = (int)i;
    int guard = (int)p.size() * (int)p.size() + 8;
    while (idx.size() > 3 && guard-- > 0) {
        bool cut = false;
        for (std::size_t i = 0; i < idx.size(); ++i) {
            const Vec2& a = p[(std::size_t)idx[(i + idx.size() - 1) % idx.size()]];
            const Vec2& b = p[(std::size_t)idx[i]];
            const Vec2& c = p[(std::size_t)idx[(i + 1) % idx.size()]];
            if (Cross(b - a, c - b) <= 1e-9f) continue;                              // a reflex or flat corner is no ear
            bool empty = true;
            for (std::size_t k = 0; k < idx.size() && empty; ++k) {
                if (k == i || k == (i + 1) % idx.size() || k == (i + idx.size() - 1) % idx.size()) continue;
                if (PointInTri(p[(std::size_t)idx[k]], a, b, c)) empty = false;
            }
            if (!empty) continue;
            out.push_back({a, b, c});
            idx.erase(idx.begin() + (std::ptrdiff_t)i);
            cut = true; break;
        }
        if (!cut) break;                                                             // a self-crossing outline: stop
    }
    if (idx.size() == 3) out.push_back({p[(std::size_t)idx[0]], p[(std::size_t)idx[1]], p[(std::size_t)idx[2]]});
    else if (out.empty()) out.push_back(p);
    return out;
}

namespace {
struct Part2 { char kind; Vec2 pos; float a = 0, b = 0, c = 0, d = 0; std::vector<Vec2> pts; };
std::vector<Part2> ParseParts(const std::string& text) {
    std::vector<Part2> out;
    std::string cur;
    auto flush = [&]() {
        std::istringstream in(cur); cur.clear();
        std::string kind; if (!(in >> kind)) return;
        Part2 p;
        if (kind == "circle") { p.kind = 'c'; if (!(in >> p.pos.x >> p.pos.y >> p.a)) return; }
        else if (kind == "box") { p.kind = 'b'; if (!(in >> p.pos.x >> p.pos.y >> p.a >> p.b)) return; in >> p.c; }
        else if (kind == "capsule") { p.kind = 'k'; if (!(in >> p.pos.x >> p.pos.y >> p.a >> p.b)) return; in >> p.c; }
        else if (kind == "polygon") { p.kind = 'p'; Vec2 v; while (in >> v.x >> v.y) p.pts.push_back(v); if (p.pts.size() < 3) return; }
        else return;
        out.push_back(std::move(p));
    };
    for (char ch : text) { if (ch == ';') flush(); else cur += ch; }
    flush();
    return out;
}
Vec2 Rot(const Vec2& v, float ca, float sa) { return {v.x * ca - v.y * sa, v.x * sa + v.y * ca}; }
}  // namespace

void PiecesOf(const Collider2D* c, std::vector<Piece>& out, bool withSkin) {
    const float skin = withSkin ? Mathf::Max(c->contactOffset, 0.0f) : 0.0f;
    const std::size_t first = out.size();
    using S = Collider2D::Shape;
    Vec3 sc3 = c->transform ? c->transform->LossyScale() : Vec3::One;
    switch (c->shape()) {
    case S::Box: {
        auto* b = static_cast<const BoxCollider2D*>(c);
        Vec2 ctr = b->WorldCenter(), h = b->HalfExtents();
        float a = b->WorldAngle(), ca = Mathf::Cos(a), sa = Mathf::Sin(a);
        Piece p;
        for (const Vec2& k : {Vec2{-h.x, -h.y}, Vec2{h.x, -h.y}, Vec2{h.x, h.y}, Vec2{-h.x, h.y}}) { Vec2 w = Rot(k, ca, sa); p.v.push_back({ctr.x + w.x, ctr.y + w.y}); }
        out.push_back(std::move(p));
        break;
    }
    case S::Circle: {
        auto* ci = static_cast<const CircleCollider2D*>(c);
        Piece p; p.v = {ci->WorldCenter()}; p.r = ci->WorldRadius();
        out.push_back(std::move(p));
        break;
    }
    case S::Capsule: {
        auto* cap = static_cast<const CapsuleCollider2D*>(c);
        Vec2 a, b; cap->Segment(a, b);
        Piece p; p.v = {a, b}; p.r = cap->WorldRadius();
        out.push_back(std::move(p));
        break;
    }
    case S::Polygon: {
        auto* poly = static_cast<const PolygonCollider2D*>(c);
        for (auto& cv : ConvexPieces(poly->WorldPoints())) { Piece p; p.v = std::move(cv); out.push_back(std::move(p)); }
        break;
    }
    case S::Edge: {
        auto* e = static_cast<const EdgeCollider2D*>(c);
        std::vector<Vec2> w = e->WorldPoints();
        if (w.size() == 1) { Piece p; p.v = {w[0]}; out.push_back(std::move(p)); }
        for (std::size_t i = 0; i + 1 < w.size(); ++i) { Piece p; p.v = {w[i], w[i + 1]}; out.push_back(std::move(p)); }
        break;
    }
    case S::Composite: {
        auto* cc = static_cast<const CompositeCollider2D*>(c);
        Vec2 ctr = cc->WorldCenter();
        float ang = cc->WorldRotation(), ca = Mathf::Cos(ang), sa = Mathf::Sin(ang);
        auto place = [&](const Vec2& local) { Vec2 w = Rot({local.x * sc3.x, local.y * sc3.y}, ca, sa); return Vec2{ctr.x + w.x, ctr.y + w.y}; };
        float sUni = Mathf::Max(Mathf::Abs(sc3.x), Mathf::Abs(sc3.y));
        for (const Part2& pt : ParseParts(cc->parts)) {
            if (pt.kind == 'c') { Piece p; p.v = {place(pt.pos)}; p.r = Mathf::Abs(pt.a) * sUni; out.push_back(std::move(p)); }
            else if (pt.kind == 'b') {
                float an = pt.c * Mathf::Deg2Rad, cb = Mathf::Cos(an), sb = Mathf::Sin(an);
                Vec2 h{Mathf::Abs(pt.a) * 0.5f, Mathf::Abs(pt.b) * 0.5f};
                Piece p;
                for (const Vec2& k : {Vec2{-h.x, -h.y}, Vec2{h.x, -h.y}, Vec2{h.x, h.y}, Vec2{-h.x, h.y}}) { Vec2 l = Rot(k, cb, sb); p.v.push_back(place({pt.pos.x + l.x, pt.pos.y + l.y})); }
                out.push_back(std::move(p));
            } else if (pt.kind == 'k') {
                float an = (pt.c + 90.0f) * Mathf::Deg2Rad;   // angle 0 = lying along X, like the Horizontal capsule
                an = pt.c * Mathf::Deg2Rad;
                Vec2 dir{Mathf::Cos(an), Mathf::Sin(an)};
                float h = Mathf::Abs(pt.b) * 0.5f;
                Piece p; p.v = {place({pt.pos.x - dir.x * h, pt.pos.y - dir.y * h}), place({pt.pos.x + dir.x * h, pt.pos.y + dir.y * h})};
                p.r = Mathf::Abs(pt.a) * sUni; out.push_back(std::move(p));
            } else {
                std::vector<Vec2> w; for (const Vec2& v : pt.pts) w.push_back(place(v));
                for (auto& cv : ConvexPieces(w)) { Piece p; p.v = std::move(cv); out.push_back(std::move(p)); }
            }
        }
        break;
    }
    }
    for (std::size_t i = first; i < out.size(); ++i) { out[i].r += skin; out[i].Bounds(); }
}

bool IsGeneric(const Collider2D* c) {
    switch (c->shape()) { case Collider2D::Shape::Polygon: case Collider2D::Shape::Edge: case Collider2D::Shape::Composite: return true; default: return false; }
}

namespace {
bool RayCircleP(const Vec2& o, const Vec2& d, const Vec2& c, float r, float maxT, float& t, Vec2& n) {
    Vec2 m{o.x - c.x, o.y - c.y};
    float a = d.x * d.x + d.y * d.y, b = m.x * d.x + m.y * d.y, cc = m.x * m.x + m.y * m.y - r * r;
    if (a < 1e-20f || cc <= 0.0f) return false;
    float disc = b * b - a * cc; if (disc < 0.0f) return false;
    float s = (-b - std::sqrt(disc)) / a;
    if (s < 0.0f || s > maxT) return false;
    t = s; Vec2 q{m.x + d.x * s, m.y + d.y * s}; float l = std::sqrt(q.x * q.x + q.y * q.y);
    n = l > 1e-12f ? Vec2{q.x / l, q.y / l} : Vec2{0, 1};
    return true;
}
bool RayPolyP(const std::vector<Vec2>& v, const Vec2& o, const Vec2& d, float maxT, float& t, Vec2& n) {
    float t0 = 0.0f, t1 = maxT; Vec2 nEnter{0, 0}; bool entered = false;
    for (std::size_t i = 0; i < v.size(); ++i) {
        const Vec2& p = v[i]; const Vec2& q = v[(i + 1) % v.size()];
        Vec2 e{q.x - p.x, q.y - p.y}; float len = std::sqrt(e.x * e.x + e.y * e.y);
        if (len < 1e-12f) continue;
        Vec2 nn{e.y / len, -e.x / len};
        float dist = nn.x * (o.x - p.x) + nn.y * (o.y - p.y), den = nn.x * d.x + nn.y * d.y;
        if (Mathf::Abs(den) < 1e-12f) { if (dist > 0.0f) return false; continue; }
        float s = -dist / den;
        if (den < 0.0f) { if (s > t0) { t0 = s; nEnter = nn; entered = true; } } else if (s < t1) t1 = s;
        if (t0 > t1) return false;
    }
    if (!entered) return false;
    t = t0; n = nEnter; return true;
}
}  // namespace

bool RayPiece(const Piece& p, const Vec2& o, const Vec2& d, float maxT, float& t, Vec2& n) {
    if (p.v.empty()) return false;
    if (p.v.size() == 1) return p.r > 0.0f && RayCircleP(o, d, p.v[0], p.r, maxT, t, n);
    if (p.v.size() == 2) {
        const Vec2 &a = p.v[0], &b = p.v[1];
        if (p.r <= 0.0f) {                                   // a bare segment
            Vec2 e{b.x - a.x, b.y - a.y};
            float den = d.x * e.y - d.y * e.x;
            if (Mathf::Abs(den) < 1e-12f) return false;
            Vec2 ao{a.x - o.x, a.y - o.y};
            float s = (ao.x * e.y - ao.y * e.x) / den, u = (ao.x * d.y - ao.y * d.x) / den;
            if (s < 0.0f || s > maxT || u < 0.0f || u > 1.0f) return false;
            t = s; float l = std::sqrt(e.x * e.x + e.y * e.y);
            Vec2 nn{e.y / l, -e.x / l};
            if (nn.x * d.x + nn.y * d.y > 0.0f) nn = {-nn.x, -nn.y};
            n = nn; return true;
        }
        // A capsule: the straight sides (a rectangle) and the two round ends.
        float bestT = 1e30f; Vec2 bestN{0, 1}; bool hit = false;
        Vec2 e{b.x - a.x, b.y - a.y}; float l = std::sqrt(e.x * e.x + e.y * e.y);
        if (l > 1e-12f) {
            Vec2 pr{-e.y / l * p.r, e.x / l * p.r};
            std::vector<Vec2> rect = {{a.x - pr.x, a.y - pr.y}, {b.x - pr.x, b.y - pr.y}, {b.x + pr.x, b.y + pr.y}, {a.x + pr.x, a.y + pr.y}};
            float tt; Vec2 nn;
            if (RayPolyP(rect, o, d, maxT, tt, nn) && tt < bestT) { hit = true; bestT = tt; bestN = nn; }
        }
        for (const Vec2& c : {a, b}) { float tt; Vec2 nn; if (RayCircleP(o, d, c, p.r, maxT, tt, nn) && tt < bestT) { hit = true; bestT = tt; bestN = nn; } }
        if (!hit) return false;
        t = bestT; n = bestN; return true;
    }
    if (p.r > 0.0f) {                                         // a rounded polygon: its sides pushed out, plus a circle at each corner
        float bestT = 1e30f; Vec2 bestN{0, 1}; bool hit = false;
        std::vector<Vec2> grown;
        for (std::size_t i = 0; i < p.v.size(); ++i) {       // (the corner circles cover the rounded corners; the grown polygon covers the flats)
            const Vec2& a = p.v[i]; const Vec2& b = p.v[(i + 1) % p.v.size()];
            Vec2 e{b.x - a.x, b.y - a.y}; float l = std::sqrt(e.x * e.x + e.y * e.y);
            if (l < 1e-12f) continue;
            Vec2 nn{e.y / l * p.r, -e.x / l * p.r};
            std::vector<Vec2> quad = {{a.x, a.y}, {b.x, b.y}, {b.x + nn.x, b.y + nn.y}, {a.x + nn.x, a.y + nn.y}};
            float tt; Vec2 n2;
            if (RayPolyP(quad, o, d, maxT, tt, n2) && tt < bestT) { hit = true; bestT = tt; bestN = n2; }
        }
        float tt; Vec2 n2;
        if (RayPolyP(p.v, o, d, maxT, tt, n2) && tt < bestT) { hit = true; bestT = tt; bestN = n2; }
        for (const Vec2& c : p.v) if (RayCircleP(o, d, c, p.r, maxT, tt, n2) && tt < bestT) { hit = true; bestT = tt; bestN = n2; }
        if (!hit) return false;
        t = bestT; n = bestN; return true;
    }
    return RayPolyP(p.v, o, d, maxT, t, n);
}

}  // namespace shape2d

// ---- Composite collider ----
namespace {
void Append(std::string& parts, const std::string& t) { if (!parts.empty()) parts += "; "; parts += t; }
}
void CompositeCollider2D::AddCircle(const Vec2& p, float r) { std::ostringstream o; o << std::setprecision(9) << "circle " << p.x << ' ' << p.y << ' ' << r; Append(parts, o.str()); }
void CompositeCollider2D::AddBox(const Vec2& p, const Vec2& s, float a) { std::ostringstream o; o << std::setprecision(9) << "box " << p.x << ' ' << p.y << ' ' << s.x << ' ' << s.y << ' ' << a; Append(parts, o.str()); }
void CompositeCollider2D::AddCapsule(const Vec2& p, float r, float l, float a) { std::ostringstream o; o << std::setprecision(9) << "capsule " << p.x << ' ' << p.y << ' ' << r << ' ' << l << ' ' << a; Append(parts, o.str()); }
void CompositeCollider2D::AddPolygon(const std::vector<Vec2>& pts) { std::ostringstream o; o << std::setprecision(9) << "polygon"; for (const Vec2& v : pts) o << ' ' << v.x << ' ' << v.y; Append(parts, o.str()); }
void CompositeCollider2D::WorldAABB(Vec2& mn, Vec2& mx) const {
    std::vector<shape2d::Piece> ps; shape2d::PiecesOf(this, ps);
    if (ps.empty()) { mn = mx = WorldCenter(); return; }
    mn = ps[0].mn; mx = ps[0].mx;
    for (const auto& p : ps) { mn = Vec2::Min(mn, p.mn); mx = Vec2::Max(mx, p.mx); }
}

namespace detail {
void RegisterColliderShape2DSerializers() {
    reflected::RegisterReflectedRecord<CompositeCollider2D>("CompositeCollider2D", "compositecollider2d");
}
} // namespace detail

} // namespace okay
