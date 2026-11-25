#include "okay/Physics/Physics2D.hpp"
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Physics/Collider2D.hpp"
#include "okay/Physics/ColliderShapes2D.hpp"
#include "okay/Physics/ConvexContact2D.hpp"
#include "okay/Physics/Core/World.hpp"
#include "okay/Components/Joint2D.hpp"
#include "okay/Core/Jobs.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/ComponentCache.hpp"
#include "okay/Scene/NearestLookup.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Mathf.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace okay {

namespace {

struct Contact {
    bool  hit = false;
    Vec2  normal{0, 0};   // points from A toward B
    float penetration = 0.0f;
    Vec2  point{0, 0};    // approximate world contact point (for angular response)
};

// One convex piece weighed as a flat plate of even thickness: its area, its centre, and its turning inertia about that
// centre for a mass of 1.
struct PieceMassProps { double area = 0, cx = 0, cy = 0, inertia = 0; };
// False when the piece has no area (a bare point or line).
bool PieceMass(const shape2d::Piece& p, PieceMassProps& o) {
    const double kPi = 3.14159265358979;
    o = PieceMassProps{};
    if (p.v.size() == 1 && p.r > 0) { o.area = kPi * p.r * p.r; o.cx = p.v[0].x; o.cy = p.v[0].y; o.inertia = 0.5 * p.r * p.r; }
    else if (p.v.size() == 2 && p.r > 0) {
        double L = (p.v[1] - p.v[0]).Magnitude(), r = p.r;
        double ar = 2 * r * L, ac = kPi * r * r;
        o.area = ar + ac; o.cx = 0.5 * (p.v[0].x + p.v[1].x); o.cy = 0.5 * (p.v[0].y + p.v[1].y);
        double iRect = (4 * r * r + L * L) / 12.0, iDisc = 0.5 * r * r + 0.25 * L * L;   // the two half discs, parallel-axis approx
        o.inertia = (ar * iRect + ac * iDisc) / o.area;
    } else if (p.v.size() >= 3) {
        double a2 = 0, sx = 0, sy = 0, num = 0, den = 0;
        for (std::size_t i = 0; i < p.v.size(); ++i) {
            const Vec2& a = p.v[i]; const Vec2& b = p.v[(i + 1) % p.v.size()];
            double c = (double)a.x * b.y - (double)b.x * a.y;
            a2 += c; sx += (a.x + b.x) * c; sy += (a.y + b.y) * c;
            num += c * ((double)a.x * a.x + a.y * a.y + (double)a.x * b.x + (double)a.y * b.y + (double)b.x * b.x + (double)b.y * b.y);
            den += c;
        }
        if (std::abs(a2) < 1e-12) return false;
        o.area = std::abs(a2) * 0.5; o.cx = sx / (3 * a2); o.cy = sy / (3 * a2);
        double iOrigin = num / (6.0 * den);
        o.inertia = std::max(iOrigin - (o.cx * o.cx + o.cy * o.cy), 1e-9);
    } else return false;
    return true;
}

// Turning inertia about Z, about their shared centre, of a body of mass 1 made of these pieces (each weighted by its
// area). 0 when it cannot be worked out (edge lines have no area).
float PiecesInertia(const std::vector<shape2d::Piece>& ps) {
    double areaSum = 0, cx = 0, cy = 0;
    std::vector<PieceMassProps> ones;
    for (const auto& p : ps) {
        PieceMassProps o;
        if (!PieceMass(p, o)) continue;
        ones.push_back(o); areaSum += o.area; cx += o.area * o.cx; cy += o.area * o.cy;
    }
    if (areaSum < 1e-9 || ones.empty()) return 0.0f;
    cx /= areaSum; cy /= areaSum;
    double I = 0;
    for (const auto& o : ones) {
        double dx = o.cx - cx, dy = o.cy - cy;
        I += (o.area / areaSum) * (o.inertia + dx * dx + dy * dy);
    }
    return (float)I;
}

Vec2 ClampVec(const Vec2& v, const Vec2& lo, const Vec2& hi) {
    return {Mathf::Clamp(v.x, lo.x, hi.x), Mathf::Clamp(v.y, lo.y, hi.y)};
}

Contact TestCircleCircle(const Vec2& ca, float ra, const Vec2& cb, float rb) {
    Contact c;
    Vec2 d = cb - ca;
    float r = ra + rb;
    float d2 = d.SqrMagnitude();
    if (d2 >= r * r) return c;
    float dist = Mathf::Sqrt(d2);
    c.hit = true;
    c.normal = dist > Mathf::Epsilon ? d / dist : Vec2{0, 1};
    c.penetration = r - dist;
    c.point = ca + c.normal * ra;        // on A's surface toward B
    return c;
}

// Box (A) vs circle (B). Normal points from box toward circle.
Contact TestBoxCircle(const Vec2& cBox, const Vec2& hBox, const Vec2& cCir, float r) {
    Contact c;
    Vec2 mn = cBox - hBox, mx = cBox + hBox;
    Vec2 closest = ClampVec(cCir, mn, mx);
    Vec2 d = cCir - closest;
    float d2 = d.SqrMagnitude();
    if (d2 > r * r) return c;
    float dist = Mathf::Sqrt(d2);
    c.hit = true;
    if (dist > Mathf::Epsilon) {
        c.normal = d / dist;
        c.penetration = r - dist;
    } else {
        // Center inside the box: push out along the smallest axis.
        float dx = hBox.x - Mathf::Abs(cCir.x - cBox.x);
        float dy = hBox.y - Mathf::Abs(cCir.y - cBox.y);
        if (dx < dy) { c.normal = {(cCir.x < cBox.x) ? -1.0f : 1.0f, 0.0f}; c.penetration = dx + r; }
        else         { c.normal = {0.0f, (cCir.y < cBox.y) ? -1.0f : 1.0f}; c.penetration = dy + r; }
    }
    c.point = closest;        // closest point on the box surface
    return c;
}

Vec2 ClosestOnSegment(const Vec2& p, const Vec2& a, const Vec2& b) {
    Vec2 ab = b - a;
    float len2 = ab.SqrMagnitude();
    if (len2 < Mathf::Epsilon) return a;
    float t = Mathf::Clamp(Vec2::Dot(p - a, ab) / len2, 0.0f, 1.0f);
    return a + ab * t;
}

void ClosestSegSeg(const Vec2& p1, const Vec2& q1, const Vec2& p2, const Vec2& q2,
                   Vec2& c1, Vec2& c2) {
    Vec2 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
    float a = d1.SqrMagnitude(), e = d2.SqrMagnitude(), f = Vec2::Dot(d2, r);
    float s, t;
    if (a <= Mathf::Epsilon && e <= Mathf::Epsilon) { c1 = p1; c2 = p2; return; }
    if (a <= Mathf::Epsilon) { s = 0.0f; t = Mathf::Clamp(f / e, 0.0f, 1.0f); }
    else {
        float c = Vec2::Dot(d1, r);
        if (e <= Mathf::Epsilon) { t = 0.0f; s = Mathf::Clamp(-c / a, 0.0f, 1.0f); }
        else {
            float b = Vec2::Dot(d1, d2);
            float denom = a * e - b * b;
            s = denom > Mathf::Epsilon ? Mathf::Clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0.0f)      { t = 0.0f; s = Mathf::Clamp(-c / a, 0.0f, 1.0f); }
            else if (t > 1.0f) { t = 1.0f; s = Mathf::Clamp((b - c) / a, 0.0f, 1.0f); }
        }
    }
    c1 = p1 + d1 * s;
    c2 = p2 + d2 * t;
}

// Closest point on a vertex chain (open polyline or closed loop) to `ref`.
Vec2 ClosestOnPoly(const std::vector<Vec2>& pts, bool closed, const Vec2& ref) {
    const std::size_t n = pts.size();
    if (n == 0) return ref;
    if (n == 1) return pts[0];
    float best = 1e30f; Vec2 bp = pts[0];
    std::size_t segs = closed ? n : n - 1;
    for (std::size_t i = 0; i < segs; ++i) {
        Vec2 cp = ClosestOnSegment(ref, pts[i], pts[(i + 1) % n]);
        float d = (cp - ref).SqrMagnitude();
        if (d < best) { best = d; bp = cp; }
    }
    return bp;
}

// Reduce a collider to a circle (center + radius) as seen from `ref`. Capsules
// collapse to a circle at the segment point nearest `ref`; edges/polygons to the
// nearest point on their segment chain (radius 0).
void AsCircle(Collider2D* col, const Vec2& ref, Vec2& center, float& radius) {
    if (col->shape() == Collider2D::Shape::Circle) {
        auto* c = static_cast<CircleCollider2D*>(col);
        center = c->WorldCenter(); radius = c->WorldRadius();
    } else if (col->shape() == Collider2D::Shape::Capsule) {
        auto* cap = static_cast<CapsuleCollider2D*>(col);
        Vec2 a, b; cap->Segment(a, b);
        center = ClosestOnSegment(ref, a, b);
        radius = cap->WorldRadius();
    } else { // Edge or Polygon
        auto* poly = static_cast<PolyShapeCollider2D*>(col);
        center = ClosestOnPoly(poly->WorldPoints(), poly->closedLoop(), ref);
        radius = 0.0f;
    }
}

// An oriented box: center, half-extents, and its two world-space unit axes.
struct OBB2 {
    Vec2 c, h, ax, ay;
};
OBB2 MakeOBB(BoxCollider2D* box) {
    OBB2 o; o.c = box->WorldCenter(); o.h = box->HalfExtents();
    float a = box->WorldAngle();
    float ca = Mathf::Cos(a), sa = Mathf::Sin(a);
    o.ax = {ca, sa};      // local +X in world
    o.ay = {-sa, ca};     // local +Y in world
    return o;
}
// Farthest point of the box in direction `dir` (a box vertex).
Vec2 OBBSupport(const OBB2& o, const Vec2& dir) {
    return o.c + o.ax * (Vec2::Dot(dir, o.ax) >= 0 ? o.h.x : -o.h.x)
               + o.ay * (Vec2::Dot(dir, o.ay) >= 0 ? o.h.y : -o.h.y);
}
// Separating-axis test between two oriented boxes. Normal points A -> B.
Contact TestOBBOBB(const OBB2& A, const OBB2& B) {
    Contact c;
    const Vec2 axes[4] = {A.ax, A.ay, B.ax, B.ay};
    Vec2 d = B.c - A.c;
    float minPen = 1e30f; Vec2 bestAxis{0, 1};
    for (int i = 0; i < 4; ++i) {
        Vec2 ax = axes[i];
        float ra = A.h.x * Mathf::Abs(Vec2::Dot(A.ax, ax)) + A.h.y * Mathf::Abs(Vec2::Dot(A.ay, ax));
        float rb = B.h.x * Mathf::Abs(Vec2::Dot(B.ax, ax)) + B.h.y * Mathf::Abs(Vec2::Dot(B.ay, ax));
        float overlap = ra + rb - Mathf::Abs(Vec2::Dot(d, ax));
        if (overlap <= 0.0f) return c;                 // found a separating axis
        if (overlap < minPen) { minPen = overlap; bestAxis = ax; }
    }
    c.hit = true;
    if (Vec2::Dot(d, bestAxis) < 0.0f) bestAxis = bestAxis * -1.0f;   // orient A -> B
    c.normal = bestAxis;
    c.penetration = minPen;
    // Approx contact point: B's deepest vertex into A, nudged to the surface.
    Vec2 pB = OBBSupport(B, bestAxis * -1.0f);
    c.point = pB + bestAxis * (minPen * 0.5f);
    return c;
}
// Oriented box vs circle: work in the box's local frame (unrotated), then map the
// resulting normal/point back to world.
Contact TestOBBCircle(const OBB2& box, const Vec2& cc, float r) {
    Vec2 d = cc - box.c;
    Vec2 local{Vec2::Dot(d, box.ax), Vec2::Dot(d, box.ay)};
    Contact c = TestBoxCircle({0, 0}, box.h, local, r);
    if (!c.hit) return c;
    c.normal = box.ax * c.normal.x + box.ay * c.normal.y;
    c.point  = box.c + box.ax * c.point.x + box.ay * c.point.y;
    return c;
}

// Components a callback removes are skipped (`gone`), so a handler that removes
// a sibling component (or its own collider/body) never runs a freed one. Once the
// other collider itself is removed, the dispatch stops: the remaining handlers
// would be handed a freed collider.
void DispatchCollision(GameObject* go, void (Component::*fn)(const Collision2D&),
                       const Collision2D& info, const Scene::RemovalWatch& gone) {
    for (Component* c : go->GetComponents<Component>()) {
        if (gone.Gone(info.collider)) break;
        if (!gone.Gone(c)) (c->*fn)(info);
    }
}
// The trigger overlap being dispatched right now (Physics2D::CurrentTrigger).
const TriggerContact2D* g_currentTrigger = nullptr;

// Tell `self`'s object it overlapped `other`, with the overlap point/normal
// readable through Physics2D::CurrentTrigger() for the duration of the calls.
void DispatchTrigger(Collider2D* self, void (Component::*fn)(Collider2D*), Collider2D* other,
                     const Vec2& point, const Vec2& normal, const Scene::RemovalWatch& gone) {
    TriggerContact2D info{self, other, point, normal};
    const TriggerContact2D* prev = g_currentTrigger;
    g_currentTrigger = &info;
    for (Component* c : self->gameObject->GetComponents<Component>()) {
        if (gone.Gone(other) || gone.Gone(self)) break;   // both are handed out (CurrentTrigger)
        if (!gone.Gone(c)) (c->*fn)(other);
    }
    g_currentTrigger = prev;
}

// The Collision2D handed to one side of a contact. `n` points from that side
// toward the other; `relVel` is the other's velocity minus this side's.
Collision2D MakeCollision(Collider2D* other, const Vec2& n, float pen, const Vec2& point,
                          const Vec2& relVel) {
    Collision2D c;
    c.gameObject = other->gameObject; c.collider = other;
    c.normal = n; c.penetration = pen; c.point = point;
    c.relativeVelocity = relVel;
    float closing = -Vec2::Dot(relVel, n);   // > 0 when the two approach
    c.impactSpeed = closing > 0.0f ? closing : 0.0f;
    return c;
}

// Does trigger collider `trig` react to `other`? (its tag / layer filter)
bool TriggerAccepts(const Collider2D* trig, const Collider2D* other) {
    if (!trig->isTrigger) return true;
    if (!trig->triggerTag.empty() && (!other->gameObject || other->gameObject->tag != trig->triggerTag))
        return false;
    return ((trig->triggerLayers >> (other->layer & 31)) & 1u) != 0;
}

} // namespace

Vec2 Collider2D::WorldCenter() const {
    Vec3 p = transform ? transform->Position() : Vec3::Zero;
    if (offset.x == 0.0f && offset.y == 0.0f) return {p.x, p.y};
    if (!transform) return {p.x + offset.x, p.y + offset.y};
    // The offset turns and scales with the object, as a child at that local position would.
    const Vec3 s = transform->LossyScale();
    const Vec3 o = transform->Rotation() * Vec3{offset.x * s.x, offset.y * s.y, 0.0f};
    return {p.x + o.x, p.y + o.y};
}

// ===================== Scene queries ===================================
namespace {

bool RayAABB(const Vec2& o, const Vec2& d, const Vec2& mn, const Vec2& mx,
             float maxT, float& tHit, Vec2& n) {
    float tmin = 0.0f, tmax = maxT;
    Vec2 nrm{0, 0};
    for (int a = 0; a < 2; ++a) {
        float od = a == 0 ? d.x : d.y;
        float oo = a == 0 ? o.x : o.y;
        float lo = a == 0 ? mn.x : mn.y;
        float hi = a == 0 ? mx.x : mx.y;
        if (Mathf::Abs(od) < 1e-8f) {
            if (oo < lo || oo > hi) return false;
        } else {
            float inv = 1.0f / od;
            float t1 = (lo - oo) * inv, t2 = (hi - oo) * inv;
            if (t1 > t2) std::swap(t1, t2);
            if (t1 > tmin) {
                tmin = t1;
                nrm = a == 0 ? Vec2{od > 0 ? -1.0f : 1.0f, 0}
                             : Vec2{0, od > 0 ? -1.0f : 1.0f};
            }
            if (t2 < tmax) tmax = t2;
            if (tmin > tmax) return false;
        }
    }
    tHit = tmin;
    n = nrm;
    return true;
}

bool RayCircle(const Vec2& o, const Vec2& d, const Vec2& c, float r,
               float maxT, float& tHit, Vec2& n) {
    Vec2 m = o - c;
    float b = Vec2::Dot(m, d);
    float cc = Vec2::Dot(m, m) - r * r;
    if (cc > 0.0f && b > 0.0f) return false;
    float disc = b * b - cc;
    if (disc < 0.0f) return false;
    float t = -b - Mathf::Sqrt(disc);
    if (t < 0.0f) t = 0.0f;
    if (t > maxT) return false;
    Vec2 p = o + d * t;
    n = (p - c).Normalized();
    tHit = t;
    return true;
}

Vec2 ClosestOnBox(const Vec2& p, const Vec2& mn, const Vec2& mx) {
    return {Mathf::Clamp(p.x, mn.x, mx.x), Mathf::Clamp(p.y, mn.y, mx.y)};
}

// Ray vs oriented box: rotate the ray into the box's local frame (axis-aligned,
// centred at origin), reuse RayAABB, rotate the normal back to world.
bool RayOBB2(const Vec2& o, const Vec2& d, BoxCollider2D* box,
             float maxT, float& tHit, Vec2& n) {
    Vec2 bc = box->WorldCenter(), h = box->HalfExtents();
    float a = box->WorldAngle();
    float ca = Mathf::Cos(a), sa = Mathf::Sin(a);
    Vec2 ax{ca, sa}, ay{-sa, ca};        // box local axes in world
    Vec2 ro = o - bc;
    Vec2 lo{Vec2::Dot(ro, ax), Vec2::Dot(ro, ay)};
    Vec2 ld{Vec2::Dot(d, ax), Vec2::Dot(d, ay)};   // d unit -> ld unit
    Vec2 ln;
    if (!RayAABB(lo, ld, {-h.x, -h.y}, {h.x, h.y}, maxT, tHit, ln)) return false;
    n = ax * ln.x + ay * ln.y;
    return true;
}

bool Alive(Collider2D* c) { return c->enabled && c->gameObject && c->gameObject->ActiveInHierarchy(); }

} // namespace

namespace {
// Does a query consider this collider? (alive, not ignored, layer, trigger rule)
bool Passes(Collider2D* c, GameObject* ignore, std::uint32_t mask, bool queryTriggers) {
    if (!Alive(c)) return false;
    if (ignore && c->gameObject == ignore) return false;
    if (!queryTriggers && c->isTrigger) return false;
    return ((mask >> (c->layer & 31)) & 1u) != 0;
}

// Ray test against one collider: exact for every shape.
bool RayCollider(Collider2D* c, const Vec2& o, const Vec2& d, float maxT, float& t, Vec2& n) {
    if (c->shape() == Collider2D::Shape::Circle) {
        auto* cc = static_cast<CircleCollider2D*>(c);
        return RayCircle(o, d, cc->WorldCenter(), cc->WorldRadius(), maxT, t, n);
    }
    if (c->shape() == Collider2D::Shape::Box)   // exact oriented-box ray test
        return RayOBB2(o, d, static_cast<BoxCollider2D*>(c), maxT, t, n);
    // Capsule, Polygon, Edge, Composite: the exact shape, piece by piece (never the bounding box).
    std::vector<shape2d::Piece> ps; shape2d::PiecesOf(c, ps, false);
    bool hit = false; float best = maxT;
    for (const auto& p : ps) {
        float tt; Vec2 nn;
        if (shape2d::RayPiece(p, o, d, best, tt, nn) && tt <= best) { hit = true; best = tt; t = tt; n = nn; }
    }
    return hit;
}
} // namespace

RaycastHit2D Physics2D::Raycast(Scene& scene, const Vec2& origin, const Vec2& direction,
                                float maxDistance, GameObject* ignore,
                                std::uint32_t layerMask, bool queryTriggers) {
    RaycastHit2D best;
    best.distance = maxDistance;
    Vec2 dir = direction.Normalized();
    for (Collider2D* c : scene.FindObjectsOfType<Collider2D>()) {
        if (!Passes(c, ignore, layerMask, queryTriggers)) continue;
        float t; Vec2 n;
        if (RayCollider(c, origin, dir, best.distance, t, n) && t <= best.distance) {
            best.hit = true;
            best.collider = c;
            best.gameObject = c->gameObject;
            best.distance = t;
            best.point = origin + dir * t;
            best.normal = n;
        }
    }
    return best;
}

std::vector<RaycastHit2D> Physics2D::RaycastAll(Scene& scene, const Vec2& origin, const Vec2& direction,
                                                float maxDistance, GameObject* ignore,
                                                std::uint32_t layerMask, bool queryTriggers) {
    std::vector<RaycastHit2D> hits;
    Vec2 dir = direction.Normalized();
    for (Collider2D* c : scene.FindObjectsOfType<Collider2D>()) {
        if (!Passes(c, ignore, layerMask, queryTriggers)) continue;
        float t; Vec2 n;
        if (!RayCollider(c, origin, dir, maxDistance, t, n)) continue;
        RaycastHit2D h;
        h.hit = true; h.collider = c; h.gameObject = c->gameObject;
        h.distance = t; h.point = origin + dir * t; h.normal = n;
        hits.push_back(h);
    }
    std::stable_sort(hits.begin(), hits.end(),
                     [](const RaycastHit2D& x, const RaycastHit2D& y) { return x.distance < y.distance; });
    return hits;
}

Collider2D* Physics2D::OverlapPoint(Scene& scene, const Vec2& p, std::uint32_t layerMask, bool queryTriggers) {
    for (Collider2D* c : scene.FindObjectsOfType<Collider2D>()) {
        if (!Passes(c, nullptr, layerMask, queryTriggers)) continue;
        if (c->shape() == Collider2D::Shape::Box) {
            // Exact oriented-box test: bring the point into the box's local frame.
            auto* box = static_cast<BoxCollider2D*>(c);
            Vec2 bc = box->WorldCenter(), h = box->HalfExtents();
            float a = box->WorldAngle();
            float ca = Mathf::Cos(a), sa = Mathf::Sin(a);
            Vec2 d = p - bc;
            float lx = d.x * ca + d.y * sa;      // project onto local X (cos,sin)
            float ly = -d.x * sa + d.y * ca;     // project onto local Y (-sin,cos)
            if (Mathf::Abs(lx) <= h.x && Mathf::Abs(ly) <= h.y) return c;
        } else if (c->shape() == Collider2D::Shape::Circle) {
            auto* cc = static_cast<CircleCollider2D*>(c);
            if ((p - cc->WorldCenter()).Magnitude() <= cc->WorldRadius()) return c;
        } else if (c->shape() == Collider2D::Shape::Capsule) {   // distance to the inner segment
            auto* cap = static_cast<CapsuleCollider2D*>(c);
            Vec2 a, b; cap->Segment(a, b);
            if ((p - ClosestOnSegment(p, a, b)).Magnitude() <= cap->WorldRadius()) return c;
        } else if (shape2d::IsGeneric(c)) {                      // Polygon (solid), Edge, Composite
            std::vector<shape2d::Piece> ps; shape2d::PiecesOf(c, ps);
            shape2d::Piece pt; pt.v = {p};
            for (const auto& pc : ps) {
                if (p.x < pc.mn.x || p.x > pc.mx.x || p.y < pc.mn.y || p.y > pc.mx.y) continue;
                ConvexHit2 h = ConvexContact2D([&](const Vec2& d) { return shape2d::Support(pc, d); }, [&](const Vec2&) { return p; });
                if (h.hit) return c;
            }
        }
    }
    return nullptr;
}

namespace {
// Does generic collider `col` touch the convex piece `qp`? n points from the collider toward the piece; point is on the collider.
bool PieceTouch(Collider2D* col, const shape2d::Piece& qp, Vec2& n, Vec2& point) {
    std::vector<shape2d::Piece> ps; shape2d::PiecesOf(col, ps);
    shape2d::Piece q = qp; q.Bounds();
    bool hit = false; float best = -1.0f;
    for (const auto& pc : ps) {
        if (pc.mx.x < q.mn.x || pc.mn.x > q.mx.x || pc.mx.y < q.mn.y || pc.mn.y > q.mx.y) continue;
        ConvexHit2 h = ConvexContact2D([&](const Vec2& d) { return shape2d::Support(pc, d); },
                                        [&](const Vec2& d) { return shape2d::Support(q, d); });
        if (h.hit && std::isfinite(h.penetration) && h.penetration > best) { hit = true; best = h.penetration; n = h.normal; point = h.point; }
    }
    return hit;
}
bool CircleTouchPieces(Collider2D* col, const Vec2& c, float r, Vec2& n, Vec2& point) {
    shape2d::Piece q; q.v = {c}; q.r = r;
    return PieceTouch(col, q, n, point);
}
} // namespace

std::vector<Collider2D*> Physics2D::OverlapCircle(Scene& scene, const Vec2& center, float radius,
                                                  std::uint32_t layerMask, bool queryTriggers) {
    std::vector<Collider2D*> out;
    for (Collider2D* c : scene.FindObjectsOfType<Collider2D>()) {
        if (!Passes(c, nullptr, layerMask, queryTriggers)) continue;
        bool hit = false;
        if (c->shape() == Collider2D::Shape::Box) {
            // Exact oriented-box distance: closest point in the box's local frame.
            auto* box = static_cast<BoxCollider2D*>(c);
            Vec2 bc = box->WorldCenter(), h = box->HalfExtents();
            float a = box->WorldAngle(), ca = Mathf::Cos(a), sa = Mathf::Sin(a);
            Vec2 d = center - bc;
            Vec2 l{d.x * ca + d.y * sa, -d.x * sa + d.y * ca};
            Vec2 cl{Mathf::Clamp(l.x, -h.x, h.x), Mathf::Clamp(l.y, -h.y, h.y)};
            hit = (l - cl).Magnitude() <= radius;
        } else if (shape2d::IsGeneric(c)) {
            Vec2 n, pt;
            hit = CircleTouchPieces(c, center, radius, n, pt);
        } else { // circle or capsule, reduced to a circle near the query center
            Vec2 cc; float r; AsCircle(c, center, cc, r);
            hit = (cc - center).Magnitude() <= radius + r;
        }
        if (hit) out.push_back(c);
    }
    return out;
}

namespace {
// A box or capsule used as a query (overlap or cast), not a scene collider.
struct Query2 {
    enum Kind { Box, Capsule } kind = Box;
    OBB2 box{};
    Vec2 a{}, b{};
    float r = 0.0f;
    Vec2 Center() const { return kind == Box ? box.c : (a + b) * 0.5f; }
    // Half-size of the shape's bounds, for inflating a slab test.
    Vec2 Extent() const {
        if (kind == Box) {
            float ex = box.h.x * Mathf::Abs(box.ax.x) + box.h.y * Mathf::Abs(box.ay.x);
            float ey = box.h.x * Mathf::Abs(box.ax.y) + box.h.y * Mathf::Abs(box.ay.y);
            return {ex, ey};
        }
        Vec2 mid = (a + b) * 0.5f;
        float minx = Mathf::Min(a.x, b.x) - r, maxx = Mathf::Max(a.x, b.x) + r;
        float miny = Mathf::Min(a.y, b.y) - r, maxy = Mathf::Max(a.y, b.y) + r;
        return {Mathf::Max(mid.x - minx, maxx - mid.x), Mathf::Max(mid.y - miny, maxy - mid.y)};
    }
    float Thinnest() const {
        if (kind == Box) return 2.0f * Mathf::Min(box.h.x, box.h.y);
        return 2.0f * r;
    }
    Query2 Moved(const Vec2& off) const {
        Query2 q = *this;
        if (q.kind == Box) q.box.c = q.box.c + off;
        else { q.a = q.a + off; q.b = q.b + off; }
        return q;
    }
};

OBB2 MakeQueryBox(const Vec2& c, const Vec2& half, float angle) {
    OBB2 o;
    o.c = c;
    o.h = {Mathf::Abs(half.x), Mathf::Abs(half.y)};
    float ca = Mathf::Cos(angle), sa = Mathf::Sin(angle);
    o.ax = {ca, sa};
    o.ay = {-sa, ca};
    return o;
}

Vec2 ClosestOnOBB2(const OBB2& o, const Vec2& p) {
    Vec2 d = p - o.c;
    Vec2 l{Vec2::Dot(d, o.ax), Vec2::Dot(d, o.ay)};
    Vec2 cl{Mathf::Clamp(l.x, -o.h.x, o.h.x), Mathf::Clamp(l.y, -o.h.y, o.h.y)};
    return o.c + o.ax * cl.x + o.ay * cl.y;
}

// Does `col` touch the query? `n` points from the struck surface back toward the
// query; `point` lies on the struck surface.
bool QueryTouch(Collider2D* col, const Query2& q, Vec2& n, Vec2& point) {
    using S = Collider2D::Shape;
    if (shape2d::IsGeneric(col)) {                           // Polygon, Edge, Composite: the real shape against the query
        shape2d::Piece qp;
        if (q.kind == Query2::Box) {
            for (const Vec2& k : {Vec2{-q.box.h.x, -q.box.h.y}, Vec2{q.box.h.x, -q.box.h.y}, Vec2{q.box.h.x, q.box.h.y}, Vec2{-q.box.h.x, q.box.h.y}})
                qp.v.push_back(q.box.c + q.box.ax * k.x + q.box.ay * k.y);
        } else { qp.v = {q.a, q.b}; qp.r = q.r; }
        return PieceTouch(col, qp, n, point);
    }
    if (q.kind == Query2::Box) {
        if (col->shape() == S::Box) {
            Contact c = TestOBBOBB(MakeOBB(static_cast<BoxCollider2D*>(col)), q.box);
            if (!c.hit) return false;
            n = c.normal;          // collider -> query
            point = c.point;
            return true;
        }
        Vec2 sc; float sr;
        if (col->shape() == S::Capsule) {
            auto* cap = static_cast<CapsuleCollider2D*>(col);
            Vec2 s0, s1; cap->Segment(s0, s1);
            sc = ClosestOnSegment(q.box.c, s0, s1);
            for (int it = 0; it < 3; ++it) sc = ClosestOnSegment(ClosestOnOBB2(q.box, sc), s0, s1);
            sr = cap->WorldRadius();
        } else if (col->shape() == S::Circle) {
            auto* cc = static_cast<CircleCollider2D*>(col);
            sc = cc->WorldCenter(); sr = cc->WorldRadius();
        } else {
            AsCircle(col, q.box.c, sc, sr);
        }
        Contact c = TestOBBCircle(q.box, sc, sr);
        if (!c.hit) return false;
        n = c.normal * -1.0f;      // box -> circle, flipped so it points back at the box
        point = sc + n * sr;
        return true;
    }
    // Capsule query: reduce the collider to the circle nearest the capsule.
    Vec2 onQ = ClosestOnSegment(col->WorldCenter(), q.a, q.b);
    if (col->shape() == S::Box) {
        auto* box = static_cast<BoxCollider2D*>(col);
        OBB2 ob = MakeOBB(box);
        onQ = ClosestOnSegment(ob.c, q.a, q.b);
        for (int it = 0; it < 3; ++it) onQ = ClosestOnSegment(ClosestOnOBB2(ob, onQ), q.a, q.b);
        Contact c = TestOBBCircle(ob, onQ, q.r);
        if (!c.hit) return false;
        n = c.normal;              // box -> capsule sample
        point = c.point;
        return true;
    }
    Vec2 sc; float sr;
    if (col->shape() == S::Circle) {
        auto* cc = static_cast<CircleCollider2D*>(col);
        sc = cc->WorldCenter(); sr = cc->WorldRadius();
        onQ = ClosestOnSegment(sc, q.a, q.b);
    } else if (col->shape() == S::Capsule) {
        auto* cap = static_cast<CapsuleCollider2D*>(col);
        Vec2 s0, s1; cap->Segment(s0, s1);
        Vec2 onCol; ClosestSegSeg(q.a, q.b, s0, s1, onQ, onCol);
        sc = onCol; sr = cap->WorldRadius();
    } else {
        AsCircle(col, onQ, sc, sr);
        onQ = ClosestOnSegment(sc, q.a, q.b);
        AsCircle(col, onQ, sc, sr);
    }
    Contact c = TestCircleCircle(sc, sr, onQ, q.r);
    if (!c.hit) return false;
    n = c.normal;                  // collider sample -> query sample
    point = c.point;
    return true;
}

std::vector<Collider2D*> OverlapOf(Scene& scene, const Query2& q, std::uint32_t mask,
                                   bool queryTriggers, GameObject* ignore) {
    std::vector<Collider2D*> out;
    for (Collider2D* c : scene.FindObjectsOfType<Collider2D>()) {
        if (!Passes(c, ignore, mask, queryTriggers)) continue;
        Vec2 n, p;
        if (QueryTouch(c, q, n, p)) out.push_back(c);
    }
    return out;
}

// Where a sweep's bounds can meet this collider, clamped to [0, maxDist].
bool SweepSpan(Collider2D* col, const Vec2& o, const Vec2& dir, const Vec2& ext, float maxDist, float& t0, float& t1) {
    Vec2 mn, mx; col->WorldAABB(mn, mx);
    if (!(std::isfinite(mn.x + mn.y + mx.x + mx.y))) return false;
    mn = mn - ext; mx = mx + ext;
    t0 = -1e30f; t1 = 1e30f; bool miss = false;
    for (int a = 0; a < 2 && !miss; ++a) {
        float od = a == 0 ? dir.x : dir.y, oo = a == 0 ? o.x : o.y;
        float l = a == 0 ? mn.x : mn.y, h = a == 0 ? mx.x : mx.y;
        if (Mathf::Abs(od) < 1e-8f) { if (oo < l || oo > h) miss = true; continue; }
        float s0 = (l - oo) / od, s1 = (h - oo) / od;
        if (s0 > s1) std::swap(s0, s1);
        t0 = Mathf::Max(t0, s0); t1 = Mathf::Min(t1, s1);
        if (t0 > t1) miss = true;
    }
    if (miss || t1 < 0.0f || t0 > maxDist) return false;
    t0 = Mathf::Max(t0, 0.0f); t1 = Mathf::Min(t1, maxDist);
    return t1 >= t0;
}

// Sweep `q` along unit `dir` up to maxDist and report what it touches, nearest first. Each candidate collider is
// marched on its own (only the stretch near it is sampled, then the first touch is bisected). With `firstOnly` the
// march stops once no later candidate can beat the nearest touch, and the list holds that one hit. A zero direction
// tests where the shape stands.
std::vector<RaycastHit2D> SweepQueryHits(Scene& scene, const Query2& q, const Vec2& dir, float maxDist,
                                         GameObject* ignore, std::uint32_t mask, bool queryTriggers, bool firstOnly) {
    std::vector<RaycastHit2D> hits;
    if (!(maxDist >= 0.0f) || !std::isfinite(maxDist)) return hits;
    const bool still = dir.SqrMagnitude() < 1e-12f;
    const Vec2 o = q.Center(), ext = q.Extent();
    struct Cand { Collider2D* c; float t0, t1; };
    std::vector<Cand> cands;
    for (Collider2D* col : scene.FindObjectsOfType<Collider2D>()) {
        if (!Passes(col, ignore, mask, queryTriggers)) continue;
        float t0, t1;
        if (!SweepSpan(col, o, dir, ext, maxDist, t0, t1)) continue;
        if (still) t1 = t0;
        cands.push_back({col, t0, t1});
    }
    float step = Mathf::Max(q.Thinnest() * 0.5f, 0.02f);
    if (cands.empty() || !std::isfinite(step)) return hits;
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.t0 < b.t0; });
    float nearest = 1e30f;
    for (const Cand& cd : cands) {
        if (firstOnly && cd.t0 > nearest) break;
        if (!std::isfinite(cd.t0) || !std::isfinite(cd.t1)) continue;
        const float end = firstOnly ? Mathf::Min(cd.t1, nearest) : cd.t1;
        Vec2 n, p;
        auto touchAt = [&](float t) { return QueryTouch(cd.c, q.Moved(dir * t), n, p); };
        float hitT = cd.t0;
        bool found = touchAt(cd.t0);
        if (!found && end > cd.t0) {
            float prev = cd.t0;
            const float span = end - cd.t0;
            const float st = (span / step > 20000.0f) ? span / 20000.0f : step;   // bound the work
            int samples = 0;   // the backstop: far from the origin one step can be smaller than the float spacing
            for (float t = cd.t0 + st; ; t += st) {
                const bool last = t >= end || ++samples > 20002;
                if (last) t = end;
                if (touchAt(t)) {
                    float a = prev, b = t;
                    for (int it = 0; it < 12 && b > a; ++it) {
                        const float m = (a + b) * 0.5f;
                        if (touchAt(m)) b = m; else a = m;
                    }
                    hitT = b;
                    touchAt(b);
                    found = true;
                    break;
                }
                if (last) break;
                prev = t;
            }
        }
        if (!found) continue;
        RaycastHit2D h;
        h.hit = true; h.collider = cd.c; h.gameObject = cd.c->gameObject;
        h.distance = hitT; h.point = p; h.normal = n;
        if (firstOnly) { hits.assign(1, h); nearest = hitT; }
        else hits.push_back(h);
    }
    std::sort(hits.begin(), hits.end(), [](const RaycastHit2D& a, const RaycastHit2D& b) { return a.distance < b.distance; });
    return hits;
}

std::vector<RaycastHit2D> SweepQueryAll(Scene& scene, const Query2& q, const Vec2& dir, float maxDist,
                                        GameObject* ignore, std::uint32_t mask, bool queryTriggers) {
    return SweepQueryHits(scene, q, dir, maxDist, ignore, mask, queryTriggers, false);
}

RaycastHit2D SweepQuery(Scene& scene, const Query2& q, const Vec2& dir, float maxDist,
                        GameObject* ignore, std::uint32_t mask, bool queryTriggers) {
    std::vector<RaycastHit2D> hits = SweepQueryHits(scene, q, dir, maxDist, ignore, mask, queryTriggers, true);
    return hits.empty() ? RaycastHit2D{} : hits.front();
}

Query2 BoxQuery(const Vec2& c, const Vec2& half, float angle) {
    Query2 q; q.kind = Query2::Box; q.box = MakeQueryBox(c, half, angle); return q;
}
Query2 CapsuleQuery(const Vec2& a, const Vec2& b, float r) {
    Query2 q; q.kind = Query2::Capsule; q.a = a; q.b = b; q.r = Mathf::Max(r, 0.0f); return q;
}
} // namespace

std::vector<Collider2D*> Physics2D::OverlapBox(Scene& scene, const Vec2& center, const Vec2& half,
                                               std::uint32_t layerMask, bool queryTriggers,
                                               GameObject* ignore, float angle) {
    if (Mathf::Abs(angle) > 1e-5f)
        return OverlapOf(scene, BoxQuery(center, half, angle), layerMask, queryTriggers, ignore);
    std::vector<Collider2D*> out;
    Vec2 qmn{center.x - half.x, center.y - half.y};
    Vec2 qmx{center.x + half.x, center.y + half.y};
    for (Collider2D* c : scene.FindObjectsOfType<Collider2D>()) {
        if (!Passes(c, ignore, layerMask, queryTriggers)) continue;
        bool hit = false;
        if (c->shape() == Collider2D::Shape::Box) {
            Vec2 mn, mx; c->WorldAABB(mn, mx);
            hit = mn.x <= qmx.x && mx.x >= qmn.x && mn.y <= qmx.y && mx.y >= qmn.y;
        } else { // circle, capsule, polygon, edge, composite: the real shape against the box
            Vec2 n, pt;
            hit = QueryTouch(c, BoxQuery(center, half, 0.0f), n, pt);
        }
        if (hit) out.push_back(c);
    }
    return out;
}

namespace {
// Does a circle (centre c, radius r) touch collider `col`? If so fill `n` (unit
// normal from the surface toward c) and `point` (the struck surface point).
bool CircleTouch(Collider2D* col, const Vec2& c, float r, Vec2& n, Vec2& point) {
    if (shape2d::IsGeneric(col)) return CircleTouchPieces(col, c, r, n, point);
    if (col->shape() == Collider2D::Shape::Box) {
        auto* box = static_cast<BoxCollider2D*>(col);
        Vec2 bc = box->WorldCenter(), h = box->HalfExtents();
        float a = box->WorldAngle(), ca = Mathf::Cos(a), sa = Mathf::Sin(a);
        Vec2 ax{ca, sa}, ay{-sa, ca};
        Vec2 rel = c - bc;
        Vec2 l{Vec2::Dot(rel, ax), Vec2::Dot(rel, ay)};
        Vec2 cl{Mathf::Clamp(l.x, -h.x, h.x), Mathf::Clamp(l.y, -h.y, h.y)};
        Vec2 dl = l - cl; float dist = dl.Magnitude();
        if (dist > 1e-6f) {
            if (dist > r) return false;
            Vec2 ln = dl / dist;
            n = ax * ln.x + ay * ln.y;
            point = bc + ax * cl.x + ay * cl.y;
            return true;
        }
        float bx = h.x - Mathf::Abs(l.x); Vec2 ln{l.x < 0 ? -1.f : 1.f, 0};
        float by = h.y - Mathf::Abs(l.y); if (by < bx) ln = {0, l.y < 0 ? -1.f : 1.f};
        n = ax * ln.x + ay * ln.y; point = c;
        return true;
    }
    // Circle / capsule / edge / polygon: reduce to a circle near c.
    Vec2 sc; float sr; AsCircle(col, c, sc, sr);
    Vec2 d = c - sc; float dist = d.Magnitude();
    if (dist > r + sr) return false;
    n = dist > 1e-6f ? d / dist : Vec2{0, 1};
    point = sc + n * sr;
    return true;
}

// Sweep a circle along unit `dir` up to maxDist; the nearest collider it touches.
// Steps of half the radius (a thin wall can't slip between samples), limited to
// the stretch of path near any candidate's bounds, each touch refined by
// bisection.
RaycastHit2D SweepCircle(Scene& scene, const Vec2& origin, float radius, const Vec2& dir,
                         float maxDist, GameObject* ignore, std::uint32_t mask,
                         bool queryTriggers) {
    RaycastHit2D best;
    struct Cand { Collider2D* c; float t0, t1; };
    std::vector<Cand> cands;
    float lo = 1e30f, hi = -1e30f;
    for (Collider2D* col : scene.FindObjectsOfType<Collider2D>()) {
        if (!Passes(col, ignore, mask, queryTriggers)) continue;
        Vec2 mn, mx; col->WorldAABB(mn, mx);
        if (!(std::isfinite(mn.x + mn.y + mx.x + mx.y))) continue;   // NaN or infinite bounds: not a place to hit
        mn = mn - Vec2{radius, radius}; mx = mx + Vec2{radius, radius};
        // Slab test of the centre's path against the inflated bounds.
        float t0 = -1e30f, t1 = 1e30f; bool miss = false;
        for (int a = 0; a < 2 && !miss; ++a) {
            float od = a == 0 ? dir.x : dir.y, oo = a == 0 ? origin.x : origin.y;
            float l = a == 0 ? mn.x : mn.y, h = a == 0 ? mx.x : mx.y;
            if (Mathf::Abs(od) < 1e-8f) { if (oo < l || oo > h) miss = true; continue; }
            float s0 = (l - oo) / od, s1 = (h - oo) / od;
            if (s0 > s1) std::swap(s0, s1);
            t0 = Mathf::Max(t0, s0); t1 = Mathf::Min(t1, s1);
            if (t0 > t1) miss = true;
        }
        if (miss || t1 < 0.0f || t0 > maxDist) continue;
        t0 = Mathf::Max(t0, 0.0f); t1 = Mathf::Min(t1, maxDist);
        cands.push_back({col, t0, t1});
        lo = Mathf::Min(lo, t0); hi = Mathf::Max(hi, t1);
    }
    if (cands.empty()) return best;
    if (dir.SqrMagnitude() < 1e-12f) hi = lo;   // no direction: just test where it stands
    float step = Mathf::Max(radius * 0.5f, 0.02f);
    // A NaN or infinite reach (or radius) gives a span the march cannot walk: nothing to find.
    if (!std::isfinite(lo) || !std::isfinite(hi) || !std::isfinite(step)) return best;
    if ((hi - lo) / step > 20000.0f) step = (hi - lo) / 20000.0f;   // bound the work
    float prev = lo;
    // The count is the backstop: far from the origin one step can be smaller than the float
    // spacing (t + step == t), and a float comparison alone would then never end the march.
    int samples = 0;
    for (float t = lo; ; t += step) {
        bool last = t >= hi || ++samples > 20002;   // the 20000-step bound above, plus the first and last samples
        if (last) t = hi;
        float bestT = 1e30f; Collider2D* bestC = nullptr;
        for (const Cand& cd : cands) {
            if (t < cd.t0 - step || t > cd.t1 + step) continue;
            Vec2 n, p;
            if (!CircleTouch(cd.c, origin + dir * t, radius, n, p)) continue;
            float a = prev, b = t;
            if (b > a) for (int it = 0; it < 12; ++it) {
                float m = (a + b) * 0.5f;
                if (CircleTouch(cd.c, origin + dir * m, radius, n, p)) b = m; else a = m;
            }
            if (b < bestT) { bestT = b; bestC = cd.c; }
        }
        if (bestC) {
            Vec2 n, p;
            CircleTouch(bestC, origin + dir * bestT, radius, n, p);
            best.hit = true; best.collider = bestC; best.gameObject = bestC->gameObject;
            best.distance = bestT; best.point = p; best.normal = n;
            return best;
        }
        if (last) break;
        prev = t;
    }
    return best;
}
} // namespace

RaycastHit2D Physics2D::CircleCast(Scene& scene, const Vec2& origin, const Vec2& direction,
                                   float radius, float maxDistance, GameObject* ignore,
                                   std::uint32_t layerMask, bool queryTriggers) {
    Vec2 dir = direction.Normalized();
    if (radius <= 0.0f) return Raycast(scene, origin, dir, maxDistance, ignore, layerMask, queryTriggers);
    return SweepCircle(scene, origin, radius, dir, maxDistance, ignore, layerMask, queryTriggers);
}

std::vector<RaycastHit2D> Physics2D::CircleCastAll(Scene& scene, const Vec2& origin, const Vec2& direction,
                                                  float radius, float maxDistance, GameObject* ignore,
                                                  std::uint32_t layerMask, bool queryTriggers) {
    Vec2 dir = direction.Normalized();
    if (radius <= 0.0f) return RaycastAll(scene, origin, dir, maxDistance, ignore, layerMask, queryTriggers);
    return SweepQueryAll(scene, CapsuleQuery(origin, origin, radius), dir, maxDistance, ignore, layerMask, queryTriggers);
}

std::vector<Collider2D*> Physics2D::OverlapCapsule(Scene& scene, const Vec2& a, const Vec2& b, float radius,
                                                   std::uint32_t layerMask, bool queryTriggers,
                                                   GameObject* ignore) {
    return OverlapOf(scene, CapsuleQuery(a, b, radius), layerMask, queryTriggers, ignore);
}

RaycastHit2D Physics2D::BoxCast(Scene& scene, const Vec2& center, const Vec2& halfExtents,
                                const Vec2& direction, float angle, float maxDistance,
                                GameObject* ignore, std::uint32_t layerMask, bool queryTriggers) {
    const std::vector<RaycastHit2D> hits = BoxCastAll(scene, center, halfExtents, direction, angle, maxDistance, ignore, layerMask, queryTriggers);
    return hits.empty() ? RaycastHit2D{} : hits.front();
}

std::vector<RaycastHit2D> Physics2D::BoxCastAll(Scene& scene, const Vec2& center, const Vec2& halfExtents,
                                               const Vec2& direction, float angle, float maxDistance,
                                               GameObject* ignore, std::uint32_t layerMask, bool queryTriggers) {
    Vec2 dir = direction.Normalized();
    Vec2 h{Mathf::Abs(halfExtents.x), Mathf::Abs(halfExtents.y)};
    if (h.x <= 1e-6f && h.y <= 1e-6f)
        return RaycastAll(scene, center, dir, maxDistance, ignore, layerMask, queryTriggers);
    return SweepQueryAll(scene, BoxQuery(center, h, angle), dir, maxDistance, ignore, layerMask, queryTriggers);
}

RaycastHit2D Physics2D::CapsuleCast(Scene& scene, const Vec2& a, const Vec2& b, float radius,
                                    const Vec2& direction, float maxDistance, GameObject* ignore,
                                    std::uint32_t layerMask, bool queryTriggers) {
    const std::vector<RaycastHit2D> hits = CapsuleCastAll(scene, a, b, radius, direction, maxDistance, ignore, layerMask, queryTriggers);
    return hits.empty() ? RaycastHit2D{} : hits.front();
}

std::vector<RaycastHit2D> Physics2D::CapsuleCastAll(Scene& scene, const Vec2& a, const Vec2& b, float radius,
                                                   const Vec2& direction, float maxDistance, GameObject* ignore,
                                                   std::uint32_t layerMask, bool queryTriggers) {
    Vec2 dir = direction.Normalized();
    float r = Mathf::Max(radius, 0.0f);
    if (r <= 1e-6f && (a - b).SqrMagnitude() <= 1e-12f)
        return RaycastAll(scene, a, dir, maxDistance, ignore, layerMask, queryTriggers);
    return SweepQueryAll(scene, CapsuleQuery(a, b, r), dir, maxDistance, ignore, layerMask, queryTriggers);
}

namespace {
// Distance from `p` to one collider piece (a rounded convex polygon, a segment or a circle: shape2d::Piece), and the
// nearest point of its surface. A point inside it, or inside its rounding, is 0 away and is its own nearest point.
float DistanceToPiece2D(const shape2d::Piece& pc, const Vec2& p, Vec2& closest) {
    const std::vector<Vec2>& v = pc.v;
    if (v.empty()) return 1e30f;
    if (v.size() >= 3) {   // corners run counter-clockwise: inside means left of every side
        bool in = true;
        for (std::size_t i = 0; i < v.size() && in; ++i) {
            const Vec2 e = v[(i + 1) % v.size()] - v[i], w = p - v[i];
            if (e.x * w.y - e.y * w.x < 0.0f) in = false;
        }
        if (in) { closest = p; return 0.0f; }
    }
    const Vec2 on = ClosestOnPoly(v, v.size() >= 3, p);
    const Vec2 delta = p - on;
    const float m = delta.Magnitude();
    if (m <= pc.r) { closest = p; return 0.0f; }
    closest = m > 1e-8f ? on + delta * (pc.r / m) : on;
    return m - pc.r;
}
} // namespace

SurfaceQuery2D Physics2D::ClosestPoint(Scene& scene, const Vec2& point, float maxDistance,
                                        GameObject* ignore, std::uint32_t layerMask) {
    SurfaceQuery2D best;
    if (!(maxDistance >= 0.0f) || !std::isfinite(point.x + point.y)) return best;
    best.distance = maxDistance;
    bool any = false;
    std::vector<shape2d::Piece> pieces;
    for (Collider2D* col : scene.FindObjectsOfType<Collider2D>()) {
        if (!Passes(col, ignore, layerMask, false)) continue;
        pieces.clear();
        shape2d::PiecesOf(col, pieces, /*withSkin=*/false);
        for (const shape2d::Piece& pc : pieces) {
            Vec2 cp = point;
            const float d = DistanceToPiece2D(pc, point, cp);
            if (!std::isfinite(d) || d > maxDistance) continue;
            if (any && !(d < best.distance)) continue;
            any = true;
            best.hit = true;
            best.collider = col;
            best.gameObject = col->gameObject;
            best.point = cp;
            best.distance = d;
        }
    }
    if (!any) return SurfaceQuery2D{};
    return best;
}

const TriggerContact2D* Physics2D::CurrentTrigger() { return g_currentTrigger; }

void Physics2D::RecordQueryRay(const Vec2& origin, const Vec2& direction, float maxDistance,
                               const RaycastHit2D& hit, float radius) {
    if (!PhysicsDebug::Collect() || !PhysicsDebug::ScriptRays()) return;
    if ((int)m_debugRays.size() >= PhysicsDebug::kMaxRays) return;
    Vec2 dir = direction.Normalized();
    PhysicsDebugRay2D r;
    r.from = origin;
    r.to = origin + dir * Mathf::Min(maxDistance, 1000.0f);   // "infinite" rays drawn 1 km long
    r.hit = hit.hit; r.point = hit.point; r.normal = hit.normal; r.radius = radius;
    m_debugRays.push_back(r);
}

} // namespace okay

#include "Physics2DEngine.inl"   // the step: our own physics core runs the 2D scene
