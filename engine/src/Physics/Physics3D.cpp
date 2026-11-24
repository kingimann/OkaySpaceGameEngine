#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/BodyPose.hpp"
#include "okay/Physics/PhysicsMaterial.hpp"
#include "okay/Components/PhysicsSurface.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/ColliderShapes3D.hpp"
#include "okay/Physics/ConvexContact.hpp"
#include "okay/Physics/TriMesh.hpp"
#include "okay/Physics/Core/World.hpp"
#include "okay/Core/Jobs.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/StringId.hpp"
#include "okay/Components/Joint3D.hpp"
#include "okay/Components/Terrain.hpp"
#include "okay/Components/VoxelTerrain.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/ComponentCache.hpp"
#include "okay/Scene/NearestLookup.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Mathf.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <typeinfo>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace okay {

namespace {

struct Contact {
    bool  hit = false;
    Vec3  normal{0, 0, 0};   // points from A toward B
    float penetration = 0.0f;
    Vec3  point{0, 0, 0};    // approximate world contact point (for angular response)
};

Vec3 ClampVec(const Vec3& v, const Vec3& lo, const Vec3& hi) {
    return {Mathf::Clamp(v.x, lo.x, hi.x),
            Mathf::Clamp(v.y, lo.y, hi.y),
            Mathf::Clamp(v.z, lo.z, hi.z)};
}

// Closest point on segment a-b to point p.
Vec3 ClosestOnSegment(const Vec3& p, const Vec3& a, const Vec3& b) {
    Vec3 ab = b - a;
    float len2 = ab.SqrMagnitude();
    if (len2 < Mathf::Epsilon) return a;
    float t = Mathf::Clamp(Vec3::Dot(p - a, ab) / len2, 0.0f, 1.0f);
    return a + ab * t;
}

// Closest points between two segments (p1-q1) and (p2-q2). (Ericson, RTCD.)
void ClosestSegSeg(const Vec3& p1, const Vec3& q1, const Vec3& p2, const Vec3& q2,
                   Vec3& c1, Vec3& c2) {
    Vec3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
    float a = d1.SqrMagnitude(), e = d2.SqrMagnitude(), f = Vec3::Dot(d2, r);
    float s, t;
    if (a <= Mathf::Epsilon && e <= Mathf::Epsilon) { c1 = p1; c2 = p2; return; }
    if (a <= Mathf::Epsilon) { s = 0.0f; t = Mathf::Clamp(f / e, 0.0f, 1.0f); }
    else {
        float c = Vec3::Dot(d1, r);
        if (e <= Mathf::Epsilon) { t = 0.0f; s = Mathf::Clamp(-c / a, 0.0f, 1.0f); }
        else {
            float b = Vec3::Dot(d1, d2);
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


// Box (A) vs sphere (B). Normal points from box toward sphere.
Contact TestBoxSphere(const Vec3& cBox, const Vec3& hBox, const Vec3& cSph, float r) {
    Contact c;
    Vec3 mn = cBox - hBox, mx = cBox + hBox;
    Vec3 closest = ClampVec(cSph, mn, mx);
    Vec3 d = cSph - closest;
    float d2 = d.SqrMagnitude();
    if (d2 > r * r) return c;
    float dist = Mathf::Sqrt(d2);
    c.hit = true;
    if (dist > Mathf::Epsilon) {
        c.normal = d / dist;
        c.penetration = r - dist;
    } else {
        // Center inside the box: push out along the least-penetrating axis.
        float dx = hBox.x - Mathf::Abs(cSph.x - cBox.x);
        float dy = hBox.y - Mathf::Abs(cSph.y - cBox.y);
        float dz = hBox.z - Mathf::Abs(cSph.z - cBox.z);
        if (dx <= dy && dx <= dz)      { c.normal = {cSph.x < cBox.x ? -1.0f : 1.0f, 0, 0}; c.penetration = dx + r; }
        else if (dy <= dx && dy <= dz) { c.normal = {0, cSph.y < cBox.y ? -1.0f : 1.0f, 0}; c.penetration = dy + r; }
        else                           { c.normal = {0, 0, cSph.z < cBox.z ? -1.0f : 1.0f}; c.penetration = dz + r; }
    }
    c.point = closest;        // closest point on the box surface
    return c;
}

// Reduce a collider to a sphere (center + radius) as seen from `ref`. Boxes are
// left as boxes (handled separately); spheres are themselves; capsules collapse
// to a sphere at the segment point nearest `ref`.
void AsSphere(Collider3D* col, const Vec3& ref, Vec3& center, float& radius) {
    if (col->shape() == Collider3D::Shape::Sphere) {
        auto* s = static_cast<SphereCollider3D*>(col);
        center = s->WorldCenter(); radius = s->WorldRadius();
    } else { // Capsule
        auto* cap = static_cast<CapsuleCollider3D*>(col);
        Vec3 a, b; cap->Segment(a, b);
        center = ClosestOnSegment(ref, a, b);
        radius = cap->WorldRadius();
    }
}

// A Mesh with no triangles reuses the box; the Cylinder, Cone, Convex Hull, Compound, Plane and Heightfield have their
// own exact tests (IsGenericShape, TestGeneric below).
inline bool IsCapsuleLike(Collider3D::Shape s) {
    return s == Collider3D::Shape::Capsule;   // (a Cylinder is its own flat-ended shape, see IsGenericShape)
}
inline bool IsBoxLike(Collider3D::Shape s) {
    return s == Collider3D::Shape::Box || s == Collider3D::Shape::Mesh;
}
// A mesh collider with real triangles collides per triangle (BVH), never as its box.
inline bool MeshHasTris(Collider3D* col) {
    return col->shape() == Collider3D::Shape::Mesh && col->gameObject &&
           static_cast<MeshCollider3D*>(col)->HasTriangles();
}
inline MeshCollider3D* TriMeshOf(Collider3D* col) {
    return MeshHasTris(col) ? static_cast<MeshCollider3D*>(col) : nullptr;
}
// A dynamic mesh body collides as its convex hull, not as the concave triangle soup.
inline bool UsesHull(Collider3D* col) {
    return col->shape() == Collider3D::Shape::Mesh &&
           static_cast<MeshCollider3D*>(col)->UsesConvexHull();
}
// Exact (concave) triangle mesh: static or kinematic. Dynamic hulls are excluded.
inline bool ExactMesh(Collider3D* col) {
    return MeshHasTris(col) && !UsesHull(col);
}
// Triangles queries should hit: the hull while the body is dynamic, otherwise the soup.
inline const TriMesh& ActiveTris(MeshCollider3D* mc) {
    return mc->UsesConvexHull() ? mc->HullTris() : mc->Tris();
}


// An oriented box: center, half-extents, and its three world-space unit axes.
struct OBB3 { Vec3 c, h, ax, ay, az; };
OBB3 MakeOBB3(BoxCollider3D* box) {
    OBB3 o; o.c = box->WorldCenter(); o.h = box->HalfExtents();
    box->WorldAxes(o.ax, o.ay, o.az);
    return o;
}
float OBBProjRadius(const OBB3& o, const Vec3& L) {
    return o.h.x * Mathf::Abs(Vec3::Dot(o.ax, L))
         + o.h.y * Mathf::Abs(Vec3::Dot(o.ay, L))
         + o.h.z * Mathf::Abs(Vec3::Dot(o.az, L));
}
Vec3 OBB3Support(const OBB3& o, const Vec3& dir) {
    return o.c + o.ax * (Vec3::Dot(dir, o.ax) >= 0 ? o.h.x : -o.h.x)
               + o.ay * (Vec3::Dot(dir, o.ay) >= 0 ? o.h.y : -o.h.y)
               + o.az * (Vec3::Dot(dir, o.az) >= 0 ? o.h.z : -o.h.z);
}
// World-space support of a primitive or a convex-hull mesh. `dir` need not be unit.
Vec3 SupportBase(Collider3D* col, const Vec3& dir) {
    if (col->HasConvexSupport()) return col->ConvexSupport(dir);          // Cylinder, Cone, Convex Hull
    if (UsesHull(col)) return static_cast<MeshCollider3D*>(col)->SupportWorld(dir);
    Vec3 d = dir.SqrMagnitude() > 1e-12f ? dir.Normalized() : Vec3{0, 1, 0};
    if (col->shape() == Collider3D::Shape::Sphere) {
        auto* s = static_cast<SphereCollider3D*>(col);
        return s->WorldCenter() + d * s->WorldRadius();
    }
    if (IsCapsuleLike(col->shape())) {
        auto* c = static_cast<CapsuleCollider3D*>(col);
        Vec3 a, b; c->Segment(a, b);
        Vec3 end = Vec3::Dot(dir, b - a) >= 0.0f ? b : a;
        return end + d * c->WorldRadius();
    }
    if (col->shape() == Collider3D::Shape::Box) return OBB3Support(MakeOBB3(static_cast<BoxCollider3D*>(col)), dir);
    // Every other shape must not be read as a box: a Compound's part text sits where a box keeps its size, so the
    // "size" was a heap address and the answer changed from run to run (the floor rescue then moved compound bodies
    // differently each run, and a fast compound fell through thin floors).
    if (col->shape() == Collider3D::Shape::Compound) {   // the furthest point of any part
        auto* cc = static_cast<CompoundCollider3D*>(col);
        const int n = cc->PartCount();
        if (n == 0) return col->WorldCenter();
        Vec3 best = cc->PartSupport(0, dir);
        float bestD = Vec3::Dot(best, dir);
        for (int i = 1; i < n; ++i) {
            const Vec3 p = cc->PartSupport(i, dir);
            const float pd = Vec3::Dot(p, dir);
            if (pd > bestD) { bestD = pd; best = p; }
        }
        return best;
    }
    // Plane, Heightfield, a triangle mesh without a hull: the corner of the world bounding box furthest along dir.
    Vec3 mn, mx;
    col->WorldAABB(mn, mx);
    return {dir.x >= 0.0f ? mx.x : mn.x, dir.y >= 0.0f ? mx.y : mn.y, dir.z >= 0.0f ? mx.z : mn.z};
}
// The same, grown by the collider's contact offset (a skin).
Vec3 SupportOf(Collider3D* col, const Vec3& dir) {
    Vec3 p = SupportBase(col, dir);
    if (col->contactOffset > 0.0f) {
        float m = dir.Magnitude();
        p = p + (m > 1e-12f ? dir * (col->contactOffset / m) : Vec3{0, 1, 0} * col->contactOffset);
    }
    return p;
}

// ---- The shapes with their own exact tests: Cylinder, Cone, Convex Hull, Compound, Plane, Heightfield ----
using SupFn = std::function<Vec3(const Vec3&)>;
struct Piece { SupFn sup; Vec3 mn, mx; };
// A collider as convex pieces (a Compound gives one per part; Plane and Heightfield are not convex and are handled apart).
void PiecesOf(Collider3D* c, std::vector<Piece>& out) {
    if (c->shape() == Collider3D::Shape::Compound) {
        auto* cc = static_cast<CompoundCollider3D*>(c);
        const float off = c->contactOffset;
        for (int i = 0; i < cc->PartCount(); ++i) {
            Piece p;
            p.sup = [cc, i, off](const Vec3& d) {
                Vec3 q = cc->PartSupport(i, d);
                float m = d.Magnitude();
                return off > 0.0f && m > 1e-12f ? q + d * (off / m) : q;
            };
            cc->PartBounds(i, p.mn, p.mx);
            out.push_back(std::move(p));
        }
        return;
    }
    Piece p;
    p.sup = [c](const Vec3& d) { return SupportOf(c, d); };
    c->WorldAABB(p.mn, p.mx);
    out.push_back(std::move(p));
}
inline bool IsFieldShape(const Collider3D* c) {
    return c->shape() == Collider3D::Shape::Plane || c->shape() == Collider3D::Shape::Heightfield;
}
void KeepDeepest(Contact& best, const ConvexHit& h) {
    if (!h.hit || !std::isfinite(h.penetration)) return;
    if (!best.hit || h.penetration > best.penetration) {
        best.hit = true; best.normal = h.normal; best.penetration = h.penetration; best.point = h.point;
    }
}
// The deepest contact of generic collider `col` against the convex solid whose support function is supB (bounds bmn..bmx).
// The normal points from `col` toward that solid.
Contact GenericVs(Collider3D* col, const SupFn& supB, const Vec3& bmn, const Vec3& bmx) {
    Contact best;
    const float off = Mathf::Max(col->contactOffset, 0.0f);
    switch (col->shape()) {
    case Collider3D::Shape::Plane: {
        auto* pl = static_cast<PlaneCollider3D*>(col);
        Vec3 n = pl->WorldNormal();
        Vec3 p = supB(n * -1.0f);                            // the solid's lowest point
        float sd = Vec3::Dot(p - pl->WorldCenter(), n);      // negative: below the plane
        if (sd < off) { best.hit = true; best.normal = n; best.penetration = off - sd; best.point = p - n * (sd * 0.5f); }
        break;
    }
    case Collider3D::Shape::Heightfield: {
        auto* hf = static_cast<HeightfieldCollider3D*>(col);
        std::vector<std::array<Vec3, 3>> tris;
        Vec3 pad{off + 0.01f, off + 0.01f, off + 0.01f};
        hf->CollectTriangles(bmn - pad, bmx + pad, tris);
        const Vec3 down = (hf->transform ? hf->transform->Rotation() * Vec3{0, -1, 0} : Vec3{0, -1, 0}) * hf->Depth();
        for (const auto& t : tris) {
            Vec3 tn = Vec3::Cross(t[1] - t[0], t[2] - t[0]);
            float tl = tn.Magnitude();
            if (tl < 1e-12f) continue;
            tn = tn * (1.0f / tl);
            if (tn.y < 0.0f) tn = tn * -1.0f;                // the upward face normal
            const Vec3 v[6] = {t[0], t[1], t[2], t[0] + down, t[1] + down, t[2] + down};
            auto prism = [&](const Vec3& d) {
                int bi = 0; float bd = Vec3::Dot(v[0], d);
                for (int i = 1; i < 6; ++i) { float x = Vec3::Dot(v[i], d); if (x > bd) { bd = x; bi = i; } }
                Vec3 q = v[bi];
                float m = d.Magnitude();
                return off > 0.0f && m > 1e-12f ? q + d * (off / m) : q;
            };
            ConvexHit h = ConvexContact(prism, supB);
            if (h.hit && Vec3::Dot(h.normal, tn) < 0.5f) continue;   // a prism side or an edge ghost, not the ground face
            KeepDeepest(best, h);
        }
        break;
    }
    case Collider3D::Shape::Compound: {
        std::vector<Piece> ps; PiecesOf(col, ps);
        for (const Piece& p : ps) {
            Vec3 amn = p.mn - Vec3{off, off, off}, amx = p.mx + Vec3{off, off, off};
            if (amx.x < bmn.x || amn.x > bmx.x || amx.y < bmn.y || amn.y > bmx.y || amx.z < bmn.z || amn.z > bmx.z) continue;
            KeepDeepest(best, ConvexContact(p.sup, supB));
        }
        break;
    }
    default: {
        SupFn sa = [col](const Vec3& d) { return SupportOf(col, d); };
        KeepDeepest(best, ConvexContact(sa, supB));
        break;
    }
    }
    return best;
}
// Separating-axis test between two oriented boxes (15 axes: 3+3 faces, 9 edge
// cross-products). Normal points A -> B.
Contact TestOBB3OBB3(const OBB3& A, const OBB3& B) {
    Contact c;
    Vec3 axes[15];
    axes[0] = A.ax; axes[1] = A.ay; axes[2] = A.az;
    axes[3] = B.ax; axes[4] = B.ay; axes[5] = B.az;
    const Vec3 Aa[3] = {A.ax, A.ay, A.az}, Bb[3] = {B.ax, B.ay, B.az};
    int n = 6;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) axes[n++] = Vec3::Cross(Aa[i], Bb[j]);
    Vec3 d = B.c - A.c;
    float bestMetric = 1e30f, bestOverlap = 0.0f; Vec3 bestAxis{0, 1, 0};
    for (int i = 0; i < 15; ++i) {
        float len2 = axes[i].SqrMagnitude();
        if (len2 < 1e-8f) continue;                       // parallel edges -> skip
        Vec3 L = axes[i] * (1.0f / Mathf::Sqrt(len2));
        float overlap = OBBProjRadius(A, L) + OBBProjRadius(B, L) - Mathf::Abs(Vec3::Dot(d, L));
        if (overlap <= 0.0f) return c;                    // separating axis found
        // Bias face axes slightly under edge-cross axes so flat rests pick a clean
        // face normal instead of chattering on a near-tie edge axis.
        float metric = overlap + (i < 6 ? 0.0f : 1e-3f);
        if (metric < bestMetric) { bestMetric = metric; bestOverlap = overlap; bestAxis = L; }
    }
    c.hit = true;
    c.penetration = bestOverlap;
    if (Vec3::Dot(d, bestAxis) < 0.0f) bestAxis = bestAxis * -1.0f;   // orient A -> B
    c.normal = bestAxis;
    Vec3 pB = OBB3Support(B, bestAxis * -1.0f);            // B's deepest vertex into A
    c.point = pB + bestAxis * (bestOverlap * 0.5f);
    return c;
}
// Oriented box vs sphere: solve in the box's local (unrotated) frame, map back.
Contact TestOBB3Sphere(const OBB3& box, const Vec3& sc, float r) {
    Vec3 d = sc - box.c;
    Vec3 local{Vec3::Dot(d, box.ax), Vec3::Dot(d, box.ay), Vec3::Dot(d, box.az)};
    Contact c = TestBoxSphere({0, 0, 0}, box.h, local, r);
    if (!c.hit) return c;
    c.normal = box.ax * c.normal.x + box.ay * c.normal.y + box.az * c.normal.z;
    c.point  = box.c + box.ax * c.point.x + box.ay * c.point.y + box.az * c.point.z;
    return c;
}



// Components a callback removes are skipped (`gone`), so a handler that removes
// a sibling component (or its own collider/body) never runs a freed one. Once the
// other collider itself is removed, the dispatch stops: the remaining handlers
// would be handed a freed collider.
void DispatchCollision(GameObject* go, void (Component::*fn)(const Collision3D&),
                       const Collision3D& info, const Scene::RemovalWatch& gone) {
    for (Component* c : go->GetComponents<Component>()) {
        if (gone.Gone(info.collider)) break;
        if (!gone.Gone(c)) (c->*fn)(info);
    }
}
// The trigger overlap being dispatched right now (Physics3D::CurrentTrigger).
const TriggerContact3D* g_currentTrigger = nullptr;

// Tell `self`'s object it overlapped `other`, with the overlap point/normal
// readable through Physics3D::CurrentTrigger() for the duration of the calls.
void DispatchTrigger(Collider3D* self, void (Component::*fn)(Collider3D*), Collider3D* other,
                     const Vec3& point, const Vec3& normal, const Scene::RemovalWatch& gone) {
    TriggerContact3D info{self, other, point, normal};
    const TriggerContact3D* prev = g_currentTrigger;
    g_currentTrigger = &info;
    for (Component* c : self->gameObject->GetComponents<Component>()) {
        if (gone.Gone(other) || gone.Gone(self)) break;   // both are handed out (CurrentTrigger)
        if (!gone.Gone(c)) (c->*fn)(other);
    }
    g_currentTrigger = prev;
}

// The Collision3D handed to one side of a contact. `n` points from that side
// toward the other; `relVel` is the other's velocity minus this side's.
Collision3D MakeCollision(Collider3D* other, const Vec3& n, float pen, const Vec3& point,
                          const Vec3& relVel, Collider3D* self = nullptr) {
    Collision3D c;
    c.surface = other ? SurfaceOf(other->gameObject) : nullptr;       // the physics material pair of the contact
    c.ownSurface = self ? SurfaceOf(self->gameObject) : nullptr;
    c.gameObject = other->gameObject; c.collider = other;
    c.normal = n; c.penetration = pen; c.point = point;
    c.relativeVelocity = relVel;
    float closing = -Vec3::Dot(relVel, n);   // > 0 when the two approach
    c.impactSpeed = closing > 0.0f ? closing : 0.0f;
    return c;
}

// Does trigger collider `trig` react to `other`? (its tag / layer filter)
// Two colliders stopped touching: OnTriggerExit3D (at the midpoint of their centres) or OnCollisionExit3D, to both.
// (Shared by every physics quality.)
void DispatchExit(Collider3D* a, Collider3D* b, const Scene::RemovalWatch& gone) {
    if (a->isTrigger || b->isTrigger) {
        // No overlap any more: report the midpoint between the two centres.
        Vec3 mid = (a->WorldCenter() + b->WorldCenter()) * 0.5f;
        Vec3 n = (b->WorldCenter() - a->WorldCenter()).Normalized();
        DispatchTrigger(a, &Component::OnTriggerExit3D, b, mid, n, gone);
        if (!gone.Gone(a) && !gone.Gone(b))
            DispatchTrigger(b, &Component::OnTriggerExit3D, a, mid, -n, gone);
    } else {
        Collision3D forA = MakeCollision(b, Vec3::Zero, 0.0f, Vec3::Zero, Vec3::Zero, a);
        Collision3D forB = MakeCollision(a, Vec3::Zero, 0.0f, Vec3::Zero, Vec3::Zero, b);
        DispatchCollision(a->gameObject, &Component::OnCollisionExit3D, forA, gone);
        if (!gone.Gone(a) && !gone.Gone(b))
            DispatchCollision(b->gameObject, &Component::OnCollisionExit3D, forB, gone);
    }
}

bool TriggerAccepts(const Collider3D* trig, const Collider3D* other) {
    if (!trig->isTrigger) return true;
    if (!trig->triggerTag.empty() && (!other->gameObject || other->gameObject->tag != trig->triggerTag))
        return false;
    return ((trig->triggerLayers >> (other->layer & 31)) & 1u) != 0;
}

bool Alive(Collider3D* c) { return c->enabled && c->gameObject && c->gameObject->ActiveInHierarchy(); }

// Tests defined further down, used by the vertical rays below.
bool Passes(Collider3D* c, GameObject* ignore, std::uint32_t mask, bool queryTriggers);
bool RaySlabs(const Vec3& o, const Vec3& d, const Vec3& mn, const Vec3& mx, float& tEnter, float& tExit);
bool RayCollider(Collider3D* c, const Vec3& o, const Vec3& d, float maxT, float& t, Vec3& n);
bool RayTerrain(Terrain* terr, const Vec3& origin, const Vec3& dir, float maxT, float& tOut, Vec3& nOut);
bool TerrainPasses(Terrain* terr, GameObject* ignore, std::uint32_t mask);



} // namespace

namespace {
/// The body a collider belongs to: the Rigidbody3D on its object, else (only when that ancestor asks for it with
/// `compoundChildren`) the nearest ancestor's body. nullptr = a static collider.
Rigidbody3D* BodyOfCollider(Collider3D* c) {
    GameObject* g = c ? c->gameObject : nullptr;
    if (!g) return nullptr;
    if (Rigidbody3D* r = g->GetComponent<Rigidbody3D>()) return r;
    if (!g->transform) return nullptr;
    for (Transform* t = g->transform->Parent(); t; t = t->Parent()) {
        GameObject* pg = t->gameObject;
        if (!pg) break;
        if (Rigidbody3D* r = pg->GetComponent<Rigidbody3D>()) return r->compoundChildren ? r : nullptr;
    }
    return nullptr;
}
} // namespace

Physics3D::Physics3D() { for (int i = 0; i < 32; ++i) m_layerMask[i] = 0xFFFFFFFFu; }

/// The collider's centre: its offset turns and scales with the object, as a child at that local position would.
Vec3 Collider3D::WorldCenter() const {
    Vec3 p = transform ? transform->Position() : Vec3::Zero;
    if (offset.x == 0.0f && offset.y == 0.0f && offset.z == 0.0f) return p;
    if (!transform) return p + offset;
    const Vec3 sc = transform->LossyScale();
    return p + transform->Rotation() * Vec3{offset.x * sc.x, offset.y * sc.y, offset.z * sc.z};
}

void Physics3D::BeginDebugFrame() {
    m_debugRays.clear(); m_debugContacts.clear(); m_debugBodies.clear(); m_debugJoints.clear();
    MeshCollider3D::AdvanceEpoch();   // mesh colliders re-check their mesh data this frame
}

// One physics path: our own physics engine core (okay::phys, engine/include/okay/Physics/Core) moves every body,
// finds every contact and solves every joint (Physics3DEngine.inl). What the core has no shape for yet is done here
// after it: voxel terrain (a density field with caves) and the "standing on terrain" signals that character code reads.
void Physics3D::Step(Scene& scene, float dt) {
    if (dt <= 0.0f) return;
    // a long step (30 steps a second, a slow frame) is solved in equal slices no longer than kMaxSolveSlice
    const int slices = dt > kMaxSolveSlice * 1.001f ? (int)std::ceil(dt / kMaxSolveSlice - 0.001f) : 1;
    for (int i = 0; i < slices; ++i) StepEngine(scene, dt / (float)slices);
    StepTerrain(scene);

    // Debug view: every moving body (a box around it, sleeping ones marked) and every joint (the line it holds).
    if (PhysicsDebug::Collect()) {
        for (Rigidbody3D* rb : CachedComponents<Rigidbody3D>(scene)) {
            if (!rb->enabled || !rb->gameObject || !rb->gameObject->ActiveInHierarchy()) continue;
            if (rb->bodyType == Rigidbody3D::BodyType::Static || (int)m_debugBodies.size() >= PhysicsDebug::kMaxBodies) continue;
            Collider3D* col = rb->gameObject->GetComponent<Collider3D>();
            if (!col) continue;
            Vec3 mn, mx; col->WorldAABB(mn, mx);
            if (!std::isfinite(mn.x + mn.y + mn.z + mx.x + mx.y + mx.z)) continue;
            m_debugBodies.push_back({(mn + mx) * 0.5f, (mx - mn) * 0.5f, rb->sleeping});
        }
        for (Joint3D* j : CachedComponents<Joint3D>(scene)) {
            if (!j->gameObject || !j->transform || (int)m_debugJoints.size() >= PhysicsDebug::kMaxBodies) continue;
            GameObject* other = CachedPartner(scene, j);
            m_debugJoints.push_back({j->transform->Position(), other && other->transform ? other->transform->Position() : j->anchor, j->broken});
        }
    }

    // Record where every moving body ended up (the voxel terrain's swept catch reads it next step).
    for (Rigidbody3D* rb : CachedComponents<Rigidbody3D>(scene)) {
        if (rb->bodyType == Rigidbody3D::BodyType::Static || !rb->transform) continue;
        rb->prevPos = rb->transform->Position();
        rb->hasPrevPos = true;
    }
}

void Physics3D::StepTerrain(Scene& scene) {
    // "Standing on terrain" (Rigidbody3D::groundedOnTerrain, groundNormal), read by character and vehicle code. The
    // heightmap Terrain itself collides in the engine core (as a height grid); this only answers "is it on the ground":
    // a body whose underside is on or within 5 cm above the surface is grounded, and gets the surface normal there.
    for (Rigidbody3D* rb : CachedComponents<Rigidbody3D>(scene)) {
        rb->groundedOnTerrain = false;
        rb->groundNormal = Vec3{0.0f, 1.0f, 0.0f};
    }
    auto terrains = CachedComponents<Terrain>(scene);
    if (!terrains.empty()) {
        for (Rigidbody3D* rb : CachedComponents<Rigidbody3D>(scene)) {
            if (!rb->enabled || !rb->gameObject || !rb->gameObject->ActiveInHierarchy()) continue;
            if (rb->bodyType == Rigidbody3D::BodyType::Static || !rb->transform) continue;
            const Vec3 pos = rb->transform->Position();
            // The underside: the collider box's bottom and footprint (else 0.5 m below the origin, a point).
            float bottom = pos.y - 0.5f;
            Vec3 lo = pos, hi = pos;
            if (auto* col = rb->gameObject->GetComponent<Collider3D>()) {
                Vec3 mn, mx; col->WorldAABB(mn, mx);
                bottom = std::min(mn.y, pos.y);
                lo = mn; hi = mx;
            }
            // The core rests the real shape on the grid, so on a slope a box stands on its downhill edge and the surface
            // under its centre is lower than its bottom (0.14 m for a 0.6 m box on 25 degrees). Grounded when the bottom is
            // within 5 cm of the surface under the centre OR any corner of the footprint. (Before the core collided terrain,
            // the terrain step put the origin on the surface under the centre, so the centre alone was enough.)
            const Vec3 probes[5] = {{pos.x, 0.0f, pos.z}, {lo.x, 0.0f, lo.z}, {hi.x, 0.0f, lo.z}, {lo.x, 0.0f, hi.z}, {hi.x, 0.0f, hi.z}};
            for (Terrain* terr : terrains) {
                if (!terr->gameObject || !terr->gameObject->transform) continue;
                const Mat4 l2w = terr->gameObject->transform->LocalToWorldMatrix();
                const Mat4 w2l = l2w.Inverse();
                const Vec3 lp = w2l.MultiplyPoint(pos);
                const float half = terr->size * 0.5f;
                if (lp.x < -half || lp.x > half || lp.z < -half || lp.z > half) continue;
                bool onIt = false;
                for (const Vec3& pr : probes) {
                    const Vec3 lq = w2l.MultiplyPoint({pr.x, pos.y, pr.z});
                    if (lq.x < -half || lq.x > half || lq.z < -half || lq.z > half) continue;
                    const float groundY = l2w.MultiplyPoint({lq.x, terr->SampleHeight(lq.x, lq.z), lq.z}).y;
                    if (bottom <= groundY + 0.05f) { onIt = true; break; }
                }
                if (onIt) {
                    rb->groundedOnTerrain = true;
                    rb->groundNormal = l2w.MultiplyVector(terr->NormalAt(lp.x, lp.z)).Normalized();
                }
            }
        }
    }

    // 5) Voxel terrain collision: heightmap terrain only has a top surface, but a
    // marching-cubes VoxelTerrain has caves, tunnels and overhangs. Resolve each
    // dynamic body as a vertical stack of spheres (a capsule) against the density
    // field, so the player walks through tunnels, stands on cave floors and is
    // blocked by walls and ceilings. The clamped density reads as an approximate
    // signed distance (positive inside solid), so a sphere of radius r at centre c
    // overlaps solid by (r + density(c)); push it out along the surface normal.
    auto voxels = CachedComponents<VoxelTerrain>(scene);
    if (!voxels.empty()) {
        for (Rigidbody3D* rb : CachedComponents<Rigidbody3D>(scene)) {
            if (!rb->enabled || !rb->gameObject || !rb->gameObject->ActiveInHierarchy()) continue;
            if (rb->bodyType == Rigidbody3D::BodyType::Static) continue;
            Transform* t = rb->transform;
            if (!t) continue;

            // Approximate the body as a capsule from its collider AABB: a vertical
            // stack of probe spheres from feet to head.
            float radius = 0.35f, footY = 0.0f, headY = 1.6f;
            if (auto* col = rb->gameObject->GetComponent<Collider3D>()) {
                Vec3 mn, mx; col->WorldAABB(mn, mx);
                Vec3 pos = t->Position();
                radius = std::min(mx.x - mn.x, mx.z - mn.z) * 0.5f;
                if (radius < 0.05f) radius = 0.05f;
                footY = (mn.y + radius) - pos.y;     // sphere centres relative to origin
                headY = (mx.y - radius) - pos.y;
            }
            if (headY < footY) headY = footY;
            // Overlapping probe spheres (spacing <= radius) so a thin wall/floor/ceiling
            // can never slip BETWEEN two spheres and be missed.
            int spheres = (int)std::ceil((headY - footY) / std::max(radius, 0.01f)) + 1;
            if (spheres < 2) spheres = 2; if (spheres > 12) spheres = 12;

            for (VoxelTerrain* vox : voxels) {
                if (!vox->gameObject || !vox->gameObject->transform) continue;
                Vec3 vp = vox->gameObject->transform->Position();

                // --- Swept (continuous) catch: a fast fall (or low frame rate) can
                // jump the body past a thin floor in one step. March the foot sphere
                // from its PREVIOUS position to its current one; if it crossed from air
                // into solid, place it back on that surface so it can't tunnel through.
                if (rb->hasPrevPos) {
                    Vec3 cur = t->Position();
                    float footCurY = cur.y + footY, footPrevY = rb->prevPos.y + footY;
                    if (footPrevY - footCurY > radius) {          // moved down more than a radius
                        float step = radius * 0.5f;
                        for (float yy = footPrevY; yy >= footCurY - radius; yy -= step) {
                            Vec3 local{cur.x - vp.x, yy - vp.y, cur.z - vp.z};
                            if (!vox->WithinBounds(local, radius)) continue;
                            if (vox->SampleDensity(local) > vox->iso) {   // solid the body skipped over
                                MoveBodyWorld(t, Vec3{0.0f, (yy + radius) - footCurY, 0.0f});  // sit the foot on it
                                if (rb->velocity.y < 0.0f) rb->velocity.y = 0.0f;
                                rb->groundedOnTerrain = true;
                                break;
                            }
                        }
                    }
                }

                // --- Static depenetration: push each sphere out of any solid it overlaps.
                for (int si = 0; si < spheres; ++si) {
                    float fy = footY + (headY - footY) * (float)si / (float)(spheres - 1);
                    for (int it = 0; it < 4; ++it) {     // a few depenetration iterations
                        Vec3 pos = t->Position();
                        Vec3 local{pos.x - vp.x, pos.y + fy - vp.y, pos.z - vp.z};
                        if (!vox->WithinBounds(local, radius)) break;
                        float d = vox->SampleDensity(local);
                        float pen = radius + d;          // > 0 -> the sphere is in solid
                        if (pen <= 0.0f) break;
                        Vec3 n = vox->SurfaceNormal(local);
                        MoveBodyWorld(t, n * pen); // push the whole body out
                        Vec3& v = rb->velocity;
                        float vn = v.x * n.x + v.y * n.y + v.z * n.z;
                        if (vn < 0.0f) {                 // remove velocity into the surface
                            float rest = rb->bounciness;
                            v.x -= vn * n.x * (1.0f + rest);
                            v.y -= vn * n.y * (1.0f + rest);
                            v.z -= vn * n.z * (1.0f + rest);
                        }
                        if (n.y > 0.4f) rb->groundedOnTerrain = true;   // standing on a floor
                    }
                }

                // Grounded + wake probe: look for solid just beneath the feet. A
                // body sitting on a cave floor is grounded even once it falls
                // asleep (penetration alone wouldn't flag it, since a sleeping body
                // no longer sinks in). If instead there's air below a sleeping body
                // — the floor was dug away — wake it so it drops into the pit.
                {
                    Vec3 pos = t->Position();
                    Vec3 below{pos.x - vp.x, (pos.y + footY - radius - 0.06f) - vp.y, pos.z - vp.z};
                    bool solidBelow = vox->WithinBounds(below, 0.0f) &&
                                      vox->SampleDensity(below) > vox->iso;
                    if (solidBelow) rb->groundedOnTerrain = true;
                    else if (rb->sleeping) rb->WakeUp();
                }
            }
        }
    }

}

// ===================== Scene queries ===================================
namespace {

bool RayAABB(const Vec3& o, const Vec3& d, const Vec3& mn, const Vec3& mx,
             float maxT, float& tHit, Vec3& n) {
    float tmin = 0.0f, tmax = maxT;
    Vec3 nrm{0, 0, 0};
    for (int a = 0; a < 3; ++a) {
        float od = a == 0 ? d.x : a == 1 ? d.y : d.z;
        float oo = a == 0 ? o.x : a == 1 ? o.y : o.z;
        float lo = a == 0 ? mn.x : a == 1 ? mn.y : mn.z;
        float hi = a == 0 ? mx.x : a == 1 ? mx.y : mx.z;
        if (Mathf::Abs(od) < 1e-8f) {
            if (oo < lo || oo > hi) return false;
        } else {
            float inv = 1.0f / od;
            float t1 = (lo - oo) * inv, t2 = (hi - oo) * inv;
            if (t1 > t2) std::swap(t1, t2);
            if (t1 > tmin) {
                tmin = t1;
                Vec3 axis{0, 0, 0};
                float sgn = od > 0 ? -1.0f : 1.0f;
                if (a == 0) axis.x = sgn; else if (a == 1) axis.y = sgn; else axis.z = sgn;
                nrm = axis;
            }
            if (t2 < tmax) tmax = t2;
            if (tmin > tmax) return false;
        }
    }
    // A ray that starts inside the box has no entry face: point the normal back along the ray (never a zero-length one).
    if (nrm.x == 0.0f && nrm.y == 0.0f && nrm.z == 0.0f) nrm = d * -1.0f;
    tHit = tmin; n = nrm; return true;
}

// Ray (o + d*t) vs box slab test returning BOTH entry and exit (unclamped below
// 0), used to bound shape sweeps to the stretch where a collider could be hit.
bool RaySlabs(const Vec3& o, const Vec3& d, const Vec3& mn, const Vec3& mx,
              float& tEnter, float& tExit) {
    tEnter = -1e30f; tExit = 1e30f;
    for (int a = 0; a < 3; ++a) {
        float od = a == 0 ? d.x : a == 1 ? d.y : d.z;
        float oo = a == 0 ? o.x : a == 1 ? o.y : o.z;
        float lo = a == 0 ? mn.x : a == 1 ? mn.y : mn.z;
        float hi = a == 0 ? mx.x : a == 1 ? mx.y : mx.z;
        if (Mathf::Abs(od) < 1e-8f) {
            if (oo < lo || oo > hi) return false;
            continue;
        }
        float t1 = (lo - oo) / od, t2 = (hi - oo) / od;
        if (t1 > t2) std::swap(t1, t2);
        tEnter = Mathf::Max(tEnter, t1);
        tExit  = Mathf::Min(tExit, t2);
        if (tEnter > tExit) return false;
    }
    return true;
}

// Ray vs oriented box: transform the ray into the box's local frame (where it's an
// axis-aligned box centred at the origin), reuse RayAABB, rotate the normal back.
bool RayOBB(const Vec3& o, const Vec3& d, BoxCollider3D* box,
            float maxT, float& tHit, Vec3& n) {
    Vec3 bc = box->WorldCenter(), h = box->HalfExtents();
    Vec3 ax, ay, az; box->WorldAxes(ax, ay, az);
    Vec3 ro = o - bc;
    Vec3 lo{Vec3::Dot(ro, ax), Vec3::Dot(ro, ay), Vec3::Dot(ro, az)};
    Vec3 ld{Vec3::Dot(d, ax), Vec3::Dot(d, ay), Vec3::Dot(d, az)};  // d is unit -> ld is unit
    Vec3 ln;
    if (!RayAABB(lo, ld, {-h.x, -h.y, -h.z}, {h.x, h.y, h.z}, maxT, tHit, ln)) return false;
    n = ax * ln.x + ay * ln.y + az * ln.z;
    return true;
}

bool RaySphere(const Vec3& o, const Vec3& d, const Vec3& c, float r,
               float maxT, float& tHit, Vec3& n) {
    Vec3 m = o - c;
    float b = Vec3::Dot(m, d);
    float cc = Vec3::Dot(m, m) - r * r;
    if (cc > 0.0f && b > 0.0f) return false;
    float disc = b * b - cc;
    if (disc < 0.0f) return false;
    float t = -b - Mathf::Sqrt(disc);
    if (t < 0.0f) t = 0.0f;
    if (t > maxT) return false;
    n = ((o + d * t) - c).Normalized();
    tHit = t; return true;
}

// Ray vs capsule (segment a-b thickened by r): the tube around the segment,
// limited to the segment's span, else whichever end-cap sphere comes first.
// (Rays used to test capsules by their AABB, which hit the empty corners.)
bool RayCapsule(const Vec3& o, const Vec3& d, const Vec3& a, const Vec3& b, float r,
                float maxT, float& tHit, Vec3& n) {
    Vec3 ab = b - a;
    float len = ab.Magnitude();
    if (len < 1e-6f) return RaySphere(o, d, a, r, maxT, tHit, n);
    Vec3 inside = ClosestOnSegment(o, a, b);
    if ((o - inside).SqrMagnitude() <= r * r) {        // starts inside: hit at 0
        Vec3 out = o - inside;
        n = out.SqrMagnitude() > 1e-12f ? out.Normalized() : d * -1.0f;
        tHit = 0.0f; return true;
    }
    Vec3 ax = ab * (1.0f / len);
    Vec3 ao = o - a;
    Vec3 dp = d - ax * Vec3::Dot(d, ax), op = ao - ax * Vec3::Dot(ao, ax);
    float best = 1e30f; Vec3 bestN{0, 0, 0};
    float A = Vec3::Dot(dp, dp), B = 2.0f * Vec3::Dot(dp, op), C = Vec3::Dot(op, op) - r * r;
    if (A > 1e-10f) {
        float disc = B * B - 4.0f * A * C;
        if (disc >= 0.0f) {
            float t = (-B - Mathf::Sqrt(disc)) / (2.0f * A);
            float s = Vec3::Dot(ao + d * t, ax);
            if (t >= 0.0f && t <= maxT && s >= 0.0f && s <= len) {
                best = t;
                bestN = ((o + d * t) - (a + ax * s)).Normalized();
            }
        }
    }
    float tc; Vec3 nc;
    if (RaySphere(o, d, a, r, maxT, tc, nc) && tc < best) { best = tc; bestN = nc; }
    if (RaySphere(o, d, b, r, maxT, tc, nc) && tc < best) { best = tc; bestN = nc; }
    if (best > maxT) return false;
    tHit = best; n = bestN; return true;
}

// Does a query consider this collider? (alive, not ignored, layer, trigger rule)
// The hierarchy a Physics3D::IgnoreHierarchy is skipping (null = none).
thread_local const GameObject* g_ignoreTree = nullptr;

bool Passes(Collider3D* c, GameObject* ignore, std::uint32_t mask, bool queryTriggers) {
    if (!Alive(c)) return false;
    if (ignore && c->gameObject == ignore) return false;
    if (g_ignoreTree && c->gameObject && c->gameObject->IsSelfOrDescendantOf(g_ignoreTree)) return false;
    if (!queryTriggers && c->isTrigger) return false;
    return ((mask >> (c->layer & 31)) & 1u) != 0;
}

// Exact ray test against one collider's shape.
bool RayCollider(Collider3D* c, const Vec3& o, const Vec3& d, float maxT, float& t, Vec3& n) {
    if (IsGenericShape(c)) return c->RayShape(o, d, maxT, t, n);   // exact for each of the shapes
    if (c->shape() == Collider3D::Shape::Sphere) {
        auto* s = static_cast<SphereCollider3D*>(c);
        return RaySphere(o, d, s->WorldCenter(), s->WorldRadius(), maxT, t, n);
    }
    if (MeshCollider3D* mc = TriMeshOf(c)) {       // exact triangles, or the convex hull when dynamic
        trimesh::WorldView v(ActiveTris(mc), mc->WorldMatrix());
        return v.Raycast(o, d, maxT, t, n);
    }
    if (IsBoxLike(c->shape()))    // exact oriented-box ray test
        return RayOBB(o, d, static_cast<BoxCollider3D*>(c), maxT, t, n);
    auto* cap = static_cast<CapsuleCollider3D*>(c);   // capsule / cylinder
    Vec3 a, b; cap->Segment(a, b);
    return RayCapsule(o, d, a, b, cap->WorldRadius(), maxT, t, n);
}

// Heightmap terrain: not a polygon collider, so march the ray against the
// height field (transform-aware — scaled/rotated terrains work). Lets Foot
// IK plant feet on hills, aim/mouse rays strike the ground, etc. Coarse
// steps sized to the heightmap cell, then a short bisection refine.
bool RayTerrain(Terrain* terr, const Vec3& origin, const Vec3& dir, float maxT,
                float& tOut, Vec3& nOut) {
    GameObject* tg = terr->gameObject;
    Mat4 l2w = tg->transform->LocalToWorldMatrix();
    Mat4 w2l = l2w.Inverse();
    float half = terr->size * 0.5f;
    auto below = [&](float t) {   // is the ray point at t under the surface?
        Vec3 lp = w2l.MultiplyPoint(origin + dir * t);
        if (lp.x < -half || lp.x > half || lp.z < -half || lp.z > half) return false;
        return lp.y <= terr->SampleHeight(lp.x, lp.z);
    };
    // Bound the march to the ray's overlap with the terrain's (generous)
    // world AABB — callers raycast with huge max distances (camera rays),
    // and stepping all the way out would stall the frame.
    Vec3 bmn, bmx; bool bAny = false;
    for (int cix = 0; cix < 8; ++cix) {
        Vec3 c2 = l2w.MultiplyPoint({cix & 1 ? half : -half,
                                     cix & 2 ? 1000.0f : -1000.0f,
                                     cix & 4 ? half : -half});
        if (!bAny) { bmn = bmx = c2; bAny = true; }
        else {
            bmn.x = Mathf::Min(bmn.x, c2.x); bmx.x = Mathf::Max(bmx.x, c2.x);
            bmn.y = Mathf::Min(bmn.y, c2.y); bmx.y = Mathf::Max(bmx.y, c2.y);
            bmn.z = Mathf::Min(bmn.z, c2.z); bmx.z = Mathf::Max(bmx.z, c2.z);
        }
    }
    float tEnter, tExit;
    if (!RaySlabs(origin, dir, bmn, bmx, tEnter, tExit)) return false;
    tEnter = Mathf::Max(tEnter, 0.0f);
    tExit  = Mathf::Min(tExit, maxT);
    if (tEnter > tExit) return false;
    float step = terr->size / (float)(terr->resolution > 1 ? terr->resolution : 64);
    if (step < 0.05f) step = 0.05f;
    if (below(tEnter)) return false;   // started under the terrain — no forward hit
    float prev = tEnter;
    int guard = 0;
    for (float t = tEnter + step; t <= tExit && guard < 4096; t += step, ++guard) {
        if (!below(t)) { prev = t; continue; }
        float lo = prev, hi = t;                    // crossed: refine the crossing
        for (int it = 0; it < 10; ++it) {
            float mid = (lo + hi) * 0.5f;
            if (below(mid)) hi = mid; else lo = mid;
        }
        float tHit = (lo + hi) * 0.5f;
        if (tHit > maxT) return false;
        Vec3 lp = w2l.MultiplyPoint(origin + dir * tHit);
        tOut = tHit;
        nOut = l2w.MultiplyVector(terr->NormalAt(lp.x, lp.z)).Normalized();
        return true;
    }
    return false;
}

bool TerrainPasses(Terrain* terr, GameObject* ignore, std::uint32_t mask) {
    GameObject* tg = terr->gameObject;
    if (!tg || !tg->ActiveInHierarchy() || !tg->transform) return false;
    if (ignore && tg == ignore) return false;
    return ((mask >> (tg->layer & 31)) & 1u) != 0;
}

// Cheap reject before the exact shape test: does the ray come within maxT of the
// collider's world bounds? Padded so an exact hit is never rejected.
bool RayNearBounds(Collider3D* c, const Vec3& o, const Vec3& d, float maxT) {
    Vec3 mn, mx; c->WorldAABB(mn, mx);
    const Vec3 pad = (mx - mn) * 1e-4f + Vec3(1e-3f, 1e-3f, 1e-3f);
    float t0, t1;
    return RaySlabs(o, d, mn - pad, mx + pad, t0, t1) && t1 >= 0.0f && t0 <= maxT;
}

} // namespace

const Physics3D::QueryCache& Physics3D::QueryLists(Scene& scene) {
    if (m_query.scene != scene.Uid() || m_query.structure != scene.StructureVersion() ||
        m_query.components != scene.ComponentVersion()) {
        m_query.scene = scene.Uid();
        m_query.structure = scene.StructureVersion();
        m_query.components = scene.ComponentVersion();
        m_query.colliders = scene.FindObjectsOfType<Collider3D>();
        m_query.terrains = scene.FindObjectsOfType<Terrain>();
    }
    return m_query;
}

RaycastHit3D Physics3D::Raycast(Scene& scene, const Vec3& origin, const Vec3& direction,
                                float maxDistance, GameObject* ignore,
                                std::uint32_t layerMask, bool queryTriggers) {
    RaycastHit3D best;
    best.distance = maxDistance;
    Vec3 dir = direction.Normalized();
    const QueryCache& q = QueryLists(scene);
    for (Collider3D* c : q.colliders) {
        if (!Passes(c, ignore, layerMask, queryTriggers) || !RayNearBounds(c, origin, dir, best.distance)) continue;
        float t; Vec3 n;
        if (RayCollider(c, origin, dir, best.distance, t, n) && t <= best.distance) {
            best.hit = true; best.collider = c; best.gameObject = c->gameObject;
            best.distance = t; best.point = origin + dir * t; best.normal = n;
        }
    }
    for (Terrain* terr : q.terrains) {
        if (!TerrainPasses(terr, ignore, layerMask)) continue;
        float t; Vec3 n;
        if (RayTerrain(terr, origin, dir, best.distance, t, n) && t <= best.distance) {
            best.hit = true; best.collider = nullptr; best.gameObject = terr->gameObject;
            best.distance = t; best.point = origin + dir * t; best.normal = n;
        }
    }
    return best;
}

std::vector<RaycastHit3D> Physics3D::RaycastAll(Scene& scene, const Vec3& origin, const Vec3& direction,
                                                float maxDistance, GameObject* ignore,
                                                std::uint32_t layerMask, bool queryTriggers) {
    std::vector<RaycastHit3D> hits;
    Vec3 dir = direction.Normalized();
    auto add = [&](Collider3D* c, GameObject* go, float t, const Vec3& n) {
        RaycastHit3D h;
        h.hit = true; h.collider = c; h.gameObject = go;
        h.distance = t; h.point = origin + dir * t; h.normal = n;
        hits.push_back(h);
    };
    const QueryCache& q = QueryLists(scene);
    for (Collider3D* c : q.colliders) {
        if (!Passes(c, ignore, layerMask, queryTriggers) || !RayNearBounds(c, origin, dir, maxDistance)) continue;
        float t; Vec3 n;
        if (RayCollider(c, origin, dir, maxDistance, t, n)) add(c, c->gameObject, t, n);
    }
    for (Terrain* terr : q.terrains) {
        if (!TerrainPasses(terr, ignore, layerMask)) continue;
        float t; Vec3 n;
        if (RayTerrain(terr, origin, dir, maxDistance, t, n)) add(nullptr, terr->gameObject, t, n);
    }
    std::stable_sort(hits.begin(), hits.end(),
                     [](const RaycastHit3D& x, const RaycastHit3D& y) { return x.distance < y.distance; });
    return hits;
}

namespace {
// Does a sphere (centre c, radius r) touch collider `col`? If so, fill `n` (unit
// normal from the surface toward c) and `point` (the struck surface point).
bool SphereTouch(Collider3D* col, const Vec3& c, float r, Vec3& n, Vec3& point) {
    if (IsGenericShape(col)) {
        SupFn sup = [c, r](const Vec3& d) { float m = d.Magnitude(); return m > 1e-12f ? c + d * (r / m) : c; };
        Contact k = GenericVs(col, sup, c - Vec3{r, r, r}, c + Vec3{r, r, r});
        if (!k.hit) return false;
        n = k.normal; point = k.point;
        return true;
    }
    if (IsBoxLike(col->shape())) {
        auto* box = static_cast<BoxCollider3D*>(col);
        Vec3 bc = box->WorldCenter(), h = box->HalfExtents();
        Vec3 ax, ay, az; box->WorldAxes(ax, ay, az);
        Vec3 rel = c - bc;
        Vec3 l{Vec3::Dot(rel, ax), Vec3::Dot(rel, ay), Vec3::Dot(rel, az)};
        Vec3 cl{Mathf::Clamp(l.x, -h.x, h.x), Mathf::Clamp(l.y, -h.y, h.y), Mathf::Clamp(l.z, -h.z, h.z)};
        Vec3 dl = l - cl; float dist = dl.Magnitude();
        if (dist > 1e-6f) {
            if (dist > r) return false;
            Vec3 ln = dl * (1.0f / dist);
            n = ax * ln.x + ay * ln.y + az * ln.z;
            point = bc + ax * cl.x + ay * cl.y + az * cl.z;
            return true;
        }
        // Centre inside the box: eject along the least-penetrating local axis.
        float best = h.x - Mathf::Abs(l.x); Vec3 ln{l.x < 0 ? -1.f : 1.f, 0, 0};
        float ty = h.y - Mathf::Abs(l.y); if (ty < best) { best = ty; ln = {0, l.y < 0 ? -1.f : 1.f, 0}; }
        float tz = h.z - Mathf::Abs(l.z); if (tz < best) { best = tz; ln = {0, 0, l.z < 0 ? -1.f : 1.f}; }
        n = ax * ln.x + ay * ln.y + az * ln.z;
        point = c;
        return true;
    }
    // Sphere / capsule / cylinder: reduce to a sphere near c.
    Vec3 sc; float sr; AsSphere(col, c, sc, sr);
    Vec3 d = c - sc; float dist = d.Magnitude();
    if (dist > r + sr) return false;
    n = dist > 1e-6f ? d * (1.0f / dist) : Vec3{0, 1, 0};
    point = sc + n * sr;
    return true;
}

// The closest point on an oriented box to p.
Vec3 ClosestOnOBB(const OBB3& o, const Vec3& p) {
    Vec3 d = p - o.c;
    float x = Mathf::Clamp(Vec3::Dot(d, o.ax), -o.h.x, o.h.x);
    float y = Mathf::Clamp(Vec3::Dot(d, o.ay), -o.h.y, o.h.y);
    float z = Mathf::Clamp(Vec3::Dot(d, o.az), -o.h.z, o.h.z);
    return o.c + o.ax * x + o.ay * y + o.az * z;
}

// A query shape for overlaps and casts: a sphere, an oriented box, or a capsule.
struct QueryShape {
    enum Kind { Sphere, Box, Capsule } kind = Sphere;
    Vec3 c{0, 0, 0};        // sphere centre / box centre
    float r = 0.0f;         // sphere / capsule radius
    OBB3 box{};             // box (Kind::Box)
    Vec3 a{0, 0, 0}, b{0, 0, 0};   // capsule segment
    QueryShape Moved(const Vec3& d) const {
        QueryShape q = *this;
        q.c = c + d; q.box.c = box.c + d; q.a = a + d; q.b = b + d;
        return q;
    }
    Vec3 Center() const { return kind == Box ? box.c : kind == Capsule ? (a + b) * 0.5f : c; }
    /// Half-size of the shape's world AABB around Center().
    Vec3 Extent() const {
        if (kind == Box)
            return {Mathf::Abs(box.ax.x) * box.h.x + Mathf::Abs(box.ay.x) * box.h.y + Mathf::Abs(box.az.x) * box.h.z,
                    Mathf::Abs(box.ax.y) * box.h.x + Mathf::Abs(box.ay.y) * box.h.y + Mathf::Abs(box.az.y) * box.h.z,
                    Mathf::Abs(box.ax.z) * box.h.x + Mathf::Abs(box.ay.z) * box.h.y + Mathf::Abs(box.az.z) * box.h.z};
        if (kind == Capsule)
            return {Mathf::Abs(b.x - a.x) * 0.5f + r, Mathf::Abs(b.y - a.y) * 0.5f + r, Mathf::Abs(b.z - a.z) * 0.5f + r};
        return {r, r, r};
    }
    /// The smallest "thickness" — sets the sweep's step so nothing slips between samples.
    float Thinnest() const {
        if (kind == Box) return Mathf::Min(box.h.x, Mathf::Min(box.h.y, box.h.z));
        return r;
    }
};

// Does query shape q touch collider col? Fills n (unit, from col's surface
// toward the shape) and point (on col's surface). Mesh colliders count as boxes.
bool ShapeTouch(Collider3D* col, const QueryShape& q, Vec3& n, Vec3& point) {
    if (MeshCollider3D* mc = TriMeshOf(col)) {
        trimesh::WorldView v(ActiveTris(mc), mc->WorldMatrix());
        trimesh::Contact c;
        if (q.kind == QueryShape::Sphere)       c = v.Sphere(q.c, q.r);
        else if (q.kind == QueryShape::Capsule) c = v.Capsule(q.a, q.b, q.r);
        else                                    c = v.Box(q.box.c, q.box.h, q.box.ax, q.box.ay, q.box.az);
        if (!c.hit) return false;
        n = c.normal; point = c.point;
        return true;
    }
    if (IsGenericShape(col)) {
        SupFn sup;
        if (q.kind == QueryShape::Sphere) {
            Vec3 c = q.c; float r = q.r;
            sup = [c, r](const Vec3& d) { float m = d.Magnitude(); return m > 1e-12f ? c + d * (r / m) : c; };
        } else if (q.kind == QueryShape::Capsule) {
            Vec3 a = q.a, b = q.b; float r = q.r;
            sup = [a, b, r](const Vec3& d) {
                Vec3 e = Vec3::Dot(d, b - a) >= 0.0f ? b : a; float m = d.Magnitude();
                return m > 1e-12f ? e + d * (r / m) : e;
            };
        } else {
            OBB3 bx = q.box;
            sup = [bx](const Vec3& d) { return OBB3Support(bx, d); };
        }
        Vec3 ext = q.Extent(), cen = q.Center();
        Contact k = GenericVs(col, sup, cen - ext, cen + ext);
        if (!k.hit) return false;
        n = k.normal; point = k.point;
        return true;
    }
    switch (q.kind) {
    case QueryShape::Sphere:
        return SphereTouch(col, q.c, q.r, n, point);
    case QueryShape::Capsule: {
        // The segment point nearest the collider, then a sphere test there.
        Vec3 p;
        if (IsBoxLike(col->shape())) {
            OBB3 o = MakeOBB3(static_cast<BoxCollider3D*>(col));
            p = ClosestOnSegment(o.c, q.a, q.b);
            for (int it = 0; it < 3; ++it) p = ClosestOnSegment(ClosestOnOBB(o, p), q.a, q.b);
        } else if (IsCapsuleLike(col->shape())) {
            Vec3 s0, s1; static_cast<CapsuleCollider3D*>(col)->Segment(s0, s1);
            Vec3 onCol; ClosestSegSeg(q.a, q.b, s0, s1, p, onCol);
        } else {
            p = ClosestOnSegment(col->WorldCenter(), q.a, q.b);
        }
        return SphereTouch(col, p, q.r, n, point);
    }
    case QueryShape::Box: {
        if (IsBoxLike(col->shape())) {
            Contact c = TestOBB3OBB3(MakeOBB3(static_cast<BoxCollider3D*>(col)), q.box);
            if (!c.hit) return false;
            n = c.normal; point = c.point;    // normal points col -> query box
            return true;
        }
        Vec3 sc; float sr;
        if (IsCapsuleLike(col->shape())) {    // capsule point nearest the box
            auto* cap = static_cast<CapsuleCollider3D*>(col);
            Vec3 s0, s1; cap->Segment(s0, s1);
            sc = ClosestOnSegment(q.box.c, s0, s1);
            for (int it = 0; it < 3; ++it) sc = ClosestOnSegment(ClosestOnOBB(q.box, sc), s0, s1);
            sr = cap->WorldRadius();
        } else {
            AsSphere(col, q.box.c, sc, sr);
        }
        Contact c = TestOBB3Sphere(q.box, sc, sr);
        if (!c.hit) return false;
        n = c.normal * -1.0f;                 // box->sphere flipped: col -> query box
        point = sc + n * sr;
        return true;
    }
    }
    return false;
}

std::vector<Collider3D*> Overlap(Scene& scene, const QueryShape& q, std::uint32_t mask,
                                 bool queryTriggers, GameObject* ignore) {
    std::vector<Collider3D*> out;
    for (Collider3D* c : scene.FindObjectsOfType<Collider3D>()) {
        if (!Passes(c, ignore, mask, queryTriggers)) continue;
        Vec3 n, p;
        if (ShapeTouch(c, q, n, p)) out.push_back(c);
    }
    return out;
}

// Sweep query shape q along unit `dir` up to maxDist; the nearest collider it
// touches. Only the stretch of the path where a collider's (inflated) bounds
// lie is marched, in steps of half the shape's thinnest size (so a thin wall
// can't slip between two samples), and each touch is refined by bisection.
// `skipStartOverlaps` ignores colliders already touching at the start (as
// casts usually do; continuous collision needs it so a body resting on the floor can
// still slide along it).
RaycastHit3D Sweep(Scene& scene, const QueryShape& q, const Vec3& dir, float maxDist,
                   GameObject* ignore, std::uint32_t mask, bool queryTriggers,
                   bool skipStartOverlaps, bool skipTriMeshes = false,
                   const std::vector<Collider3D*>* only = nullptr) {
    RaycastHit3D best;
    struct Cand { Collider3D* c; float t0, t1; };
    std::vector<Cand> cands;
    const Vec3 o = q.Center(), ext = q.Extent();
    float lo = 1e30f, hi = -1e30f;
    std::vector<Collider3D*> everything;
    if (!only) everything = scene.FindObjectsOfType<Collider3D>();
    for (Collider3D* c : (only ? *only : everything)) {
        if (!Passes(c, ignore, mask, queryTriggers)) continue;
        if (skipTriMeshes && MeshHasTris(c)) continue;
        Vec3 mn, mx; c->WorldAABB(mn, mx);
        if (!(std::isfinite(mn.x + mn.y + mn.z + mx.x + mx.y + mx.z))) continue;   // NaN or infinite bounds: not a place to hit
        float t0, t1;
        if (!RaySlabs(o, dir, mn - ext, mx + ext, t0, t1)) continue;
        if (t1 < 0.0f || t0 > maxDist) continue;
        Vec3 n, p;   // (the bounds test comes first: it is cheap, and most colliders are nowhere near the path)
        if (skipStartOverlaps && ShapeTouch(c, q, n, p)) continue;
        t0 = Mathf::Max(t0, 0.0f); t1 = Mathf::Min(t1, maxDist);
        cands.push_back({c, t0, t1});
        lo = Mathf::Min(lo, t0); hi = Mathf::Max(hi, t1);
    }
    if (cands.empty()) return best;
    if (dir.SqrMagnitude() < 1e-12f) hi = lo;   // no direction: just test where it stands
    float step = Mathf::Max(q.Thinnest() * 0.5f, 0.02f);
    // A NaN or infinite reach (or shape) gives a span the march cannot walk: nothing to find.
    if (!std::isfinite(lo) || !std::isfinite(hi) || !std::isfinite(step)) return best;
    if ((hi - lo) / step > 20000.0f) step = (hi - lo) / 20000.0f;   // bound the work
    float prev = lo;
    // The count is the backstop: far from the origin one step can be smaller than the float
    // spacing (t + step == t), and a float comparison alone would then never end the march.
    int samples = 0;
    for (float t = lo; ; t += step) {
        bool last = t >= hi || ++samples > 20002;   // the 20000-step bound above, plus the first and last samples
        if (last) t = hi;
        QueryShape s = q.Moved(dir * t);
        float bestT = 1e30f; Collider3D* bestC = nullptr;
        for (const Cand& cd : cands) {
            if (t < cd.t0 - step || t > cd.t1 + step) continue;
            Vec3 n, p;
            if (!ShapeTouch(cd.c, s, n, p)) continue;
            // Refine the first touch between the previous (clear) sample and t.
            float a = prev, b = t;
            if (b > a) for (int it = 0; it < 12; ++it) {
                float m = (a + b) * 0.5f;
                if (ShapeTouch(cd.c, q.Moved(dir * m), n, p)) b = m; else a = m;
            }
            if (b < bestT) { bestT = b; bestC = cd.c; }
        }
        if (bestC) {
            Vec3 n, p;
            ShapeTouch(bestC, q.Moved(dir * bestT), n, p);
            best.hit = true; best.collider = bestC; best.gameObject = bestC->gameObject;
            best.distance = bestT; best.point = p; best.normal = n;
            return best;   // first touch along the sweep = nearest
        }
        if (last) break;
        prev = t;
    }
    return best;
}

QueryShape SphereShape(const Vec3& c, float r) {
    QueryShape q; q.kind = QueryShape::Sphere; q.c = c; q.r = r; return q;
}
QueryShape BoxShape(const Vec3& c, const Vec3& h, const Quat& rot) {
    QueryShape q; q.kind = QueryShape::Box;
    q.box.c = c; q.box.h = {Mathf::Abs(h.x), Mathf::Abs(h.y), Mathf::Abs(h.z)};
    q.box.ax = rot * Vec3{1, 0, 0}; q.box.ay = rot * Vec3{0, 1, 0}; q.box.az = rot * Vec3{0, 0, 1};
    return q;
}
QueryShape CapsuleShape(const Vec3& a, const Vec3& b, float r) {
    QueryShape q; q.kind = QueryShape::Capsule; q.a = a; q.b = b; q.r = r; return q;
}
} // namespace

std::vector<Collider3D*> Physics3D::OverlapSphere(Scene& scene, const Vec3& center, float radius,
                                                  std::uint32_t layerMask, bool queryTriggers,
                                                  GameObject* ignore) {
    return Overlap(scene, SphereShape(center, radius), layerMask, queryTriggers, ignore);
}

std::vector<Collider3D*> Physics3D::OverlapBox(Scene& scene, const Vec3& center, const Vec3& halfExtents,
                                               const Quat& rotation, std::uint32_t layerMask,
                                               bool queryTriggers, GameObject* ignore) {
    return Overlap(scene, BoxShape(center, halfExtents, rotation), layerMask, queryTriggers, ignore);
}

std::vector<Collider3D*> Physics3D::OverlapCapsule(Scene& scene, const Vec3& a, const Vec3& b, float radius,
                                                   std::uint32_t layerMask, bool queryTriggers,
                                                   GameObject* ignore) {
    return Overlap(scene, CapsuleShape(a, b, radius), layerMask, queryTriggers, ignore);
}

Physics3D::IgnoreHierarchy::IgnoreHierarchy(const GameObject* root) : m_prev(g_ignoreTree) { g_ignoreTree = root; }
Physics3D::IgnoreHierarchy::~IgnoreHierarchy() { g_ignoreTree = m_prev; }

RaycastHit3D Physics3D::SphereCast(Scene& scene, const Vec3& origin, const Vec3& direction,
                                   float radius, float maxDistance, GameObject* ignore,
                                   std::uint32_t layerMask, bool queryTriggers) {
    Vec3 dir = direction.Normalized();
    if (radius <= 0.0f) return Raycast(scene, origin, dir, maxDistance, ignore, layerMask, queryTriggers);
    return Sweep(scene, SphereShape(origin, radius), dir, maxDistance, ignore, layerMask,
                 queryTriggers, /*skipStartOverlaps*/ false);
}

RaycastHit3D Physics3D::BoxCast(Scene& scene, const Vec3& center, const Vec3& halfExtents,
                                const Vec3& direction, const Quat& rotation, float maxDistance,
                                GameObject* ignore, std::uint32_t layerMask, bool queryTriggers) {
    return Sweep(scene, BoxShape(center, halfExtents, rotation), direction.Normalized(), maxDistance,
                 ignore, layerMask, queryTriggers, /*skipStartOverlaps*/ false);
}

RaycastHit3D Physics3D::CapsuleCast(Scene& scene, const Vec3& a, const Vec3& b, float radius,
                                    const Vec3& direction, float maxDistance, GameObject* ignore,
                                    std::uint32_t layerMask, bool queryTriggers) {
    return Sweep(scene, CapsuleShape(a, b, radius), direction.Normalized(), maxDistance,
                 ignore, layerMask, queryTriggers, /*skipStartOverlaps*/ false);
}

const TriggerContact3D* Physics3D::CurrentTrigger() { return g_currentTrigger; }

void Physics3D::RecordQueryRay(const Vec3& origin, const Vec3& direction, float maxDistance,
                               const RaycastHit3D& hit, float radius) {
    if (!PhysicsDebug::Collect() || !PhysicsDebug::ScriptRays()) return;
    if ((int)m_debugRays.size() >= PhysicsDebug::kMaxRays) return;
    Vec3 dir = direction.Normalized();
    PhysicsDebugRay3D r;
    r.from = origin;
    r.to = origin + dir * Mathf::Min(maxDistance, 1000.0f);   // "infinite" rays drawn 1 km long
    r.hit = hit.hit; r.point = hit.point; r.normal = hit.normal; r.radius = radius;
    m_debugRays.push_back(r);
}

Vec3 Physics3D::ResolveSphere(Scene& scene, Vec3 c, float r, GameObject* ignore, int iterations) {
    for (int it = 0; it < iterations; ++it) {
        bool moved = false;
        for (Collider3D* col : scene.FindObjectsOfType<Collider3D>()) {
            if (!Alive(col)) continue;
            if (ignore && col->gameObject == ignore) continue;
            if (col->isTrigger) continue;                 // triggers never block
            if (MeshCollider3D* mc = TriMeshOf(col)) {     // exact triangles, or the hull when dynamic
                trimesh::Contact k = trimesh::WorldView(ActiveTris(mc), mc->WorldMatrix()).Sphere(c, r);
                if (k.hit && k.penetration > 1e-5f) { c = c + k.normal * k.penetration; moved = true; }
                continue;
            }
            if (col->shape() == Collider3D::Shape::Sphere) {
                Vec3 sc; float sr; AsSphere(col, c, sc, sr);
                Vec3 d = c - sc; float dl = d.Magnitude();
                if (dl < r + sr) {
                    Vec3 n = dl > 1e-5f ? d * (1.0f / dl) : Vec3{0, 1, 0};
                    c = sc + n * (r + sr); moved = true;
                }
                continue;
            }
            if (IsBoxLike(col->shape())) {
                // Oriented box: work in the box's local frame so a rotated platform
                // pushes the sphere off its true faces, not its (larger) AABB.
                auto* box = static_cast<BoxCollider3D*>(col);
                Vec3 bc = box->WorldCenter(), h = box->HalfExtents();
                Vec3 ax, ay, az; box->WorldAxes(ax, ay, az);
                Vec3 rel = c - bc;
                Vec3 l{Vec3::Dot(rel, ax), Vec3::Dot(rel, ay), Vec3::Dot(rel, az)};
                Vec3 cl = ClampVec(l, {-h.x, -h.y, -h.z}, {h.x, h.y, h.z});
                Vec3 dl3 = l - cl; float dl = dl3.Magnitude();
                Vec3 ln;                                   // push direction, local space
                float push = 0.0f;
                if (dl > 1e-5f) {
                    if (dl < r) { ln = dl3 * (1.0f / dl); push = r - dl; }
                } else {
                    // Centre inside: eject along the least-penetrating local axis.
                    float dxl = l.x + h.x, dxh = h.x - l.x;
                    float dyl = l.y + h.y, dyh = h.y - l.y;
                    float dzl = l.z + h.z, dzh = h.z - l.z;
                    float m = dxl; ln = {-1, 0, 0};
                    if (dxh < m) { m = dxh; ln = {1, 0, 0}; }
                    if (dyl < m) { m = dyl; ln = {0, -1, 0}; }
                    if (dyh < m) { m = dyh; ln = {0, 1, 0}; }
                    if (dzl < m) { m = dzl; ln = {0, 0, -1}; }
                    if (dzh < m) { m = dzh; ln = {0, 0, 1}; }
                    push = m + r;
                }
                if (push > 0.0f) {
                    Vec3 wn = ax * ln.x + ay * ln.y + az * ln.z;   // local -> world normal
                    c = c + wn * push; moved = true;
                }
                continue;
            }
            if (IsGenericShape(col) || col->shape() == Collider3D::Shape::Capsule) {
                // Capsule, Cylinder, Cone, Convex Hull, Compound, Plane, Heightfield: the exact shape, not its bounds.
                Vec3 sn, sp;
                if (!SphereTouch(col, c, r, sn, sp)) continue;
                SupFn sup = [c, r](const Vec3& d) { float m = d.Magnitude(); return m > 1e-12f ? c + d * (r / m) : c; };
                float depth;
                if (IsGenericShape(col)) {
                    Contact k = GenericVs(col, sup, c - Vec3{r, r, r}, c + Vec3{r, r, r});
                    if (!k.hit) continue;
                    depth = k.penetration; sn = k.normal;
                } else {
                    Vec3 sc; float sr; AsSphere(col, c, sc, sr);
                    Vec3 dv = c - sc; float dl = dv.Magnitude();
                    depth = r + sr - dl; sn = dl > 1e-6f ? dv * (1.0f / dl) : Vec3{0, 1, 0};
                }
                if (depth > 1e-5f) { c = c + sn * depth; moved = true; }
                continue;
            }
            // Anything else (no exact shape): push out of the box surface.
            Vec3 mn, mx; col->WorldAABB(mn, mx);
            Vec3 cp = ClampVec(c, mn, mx);
            Vec3 d = c - cp; float dl = d.Magnitude();
            if (dl > 1e-5f) {
                if (dl < r) { c = cp + d * (1.0f / dl) * r; moved = true; }
            } else {
                // Centre is inside the box: eject along the axis of least penetration.
                float dxl = c.x - mn.x, dxh = mx.x - c.x;
                float dyl = c.y - mn.y, dyh = mx.y - c.y;
                float dzl = c.z - mn.z, dzh = mx.z - c.z;
                float m = dxl; Vec3 n{-1, 0, 0};
                if (dxh < m) { m = dxh; n = {1, 0, 0}; }
                if (dyl < m) { m = dyl; n = {0, -1, 0}; }
                if (dyh < m) { m = dyh; n = {0, 1, 0}; }
                if (dzl < m) { m = dzl; n = {0, 0, -1}; }
                if (dzh < m) { m = dzh; n = {0, 0, 1}; }
                c = c + n * (m + r); moved = true;
            }
        }
        if (!moved) break;
    }
    return c;
}

} // namespace okay

#include "Physics3DEngine.inl"   // our own physics engine core runs the scene
