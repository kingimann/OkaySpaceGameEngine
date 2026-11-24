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
#include "okay/Physics/Core/DynamicTree.hpp"
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
#include <cstring>
#include <functional>
#include <iterator>
#include <string>
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
        // face normal instead of chattering on a nearby-tie edge axis.
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
            GameObject* other = Joint3DPartner(scene, j);
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
        m_query.components != scene.ComponentVersion() || m_query.objects != scene.Objects().size()) {
        m_query.scene = scene.Uid();
        m_query.structure = scene.StructureVersion();
        m_query.components = scene.ComponentVersion();
        m_query.objects = scene.Objects().size();
        m_query.colliders = scene.FindObjectsOfType<Collider3D>();
        m_query.terrains = scene.FindObjectsOfType<Terrain>();
        ++m_query.version;
    }
    return m_query;
}

// ===================== The broad phase of the scene queries =====================
// Every scene query (ray, overlap, shape cast, closest point) first asks ONE box tree which colliders are nearby, then
// runs its exact tests on those only, in the order of the collider list (so answers, their order and ties are what a
// walk over every collider gives). The tree is a phys::DynamicTree, the tree class our physics core's broad phase is
// made of. It is its own instance over the cached collider list, not the tree inside the core world, because the
// world's tree only holds what the last Step put there: queries are asked before the first step, between steps after
// game code moved something, and for colliders the world has no shape for; and its boxes are the core shapes', not
// Collider3D::WorldAABB, which is what the exact tests here are written against.
//
// Kept right at every query, not once a frame: SyncQueryIndex looks at each live collider and works its box out again
// only when what the box is made from changed (the object's world matrix stamp, the offset, the shape's sizes). A shape
// with no cheap "did it change" (a convex hull, a compound: their sizes are text) has its box worked out at every query.
// A plane (endless) and a heightfield (solid below its bounds) are never in the tree: every query tests them.
// Not thread safe, as before: the transform and mesh caches the queries read are not either.
namespace {

// What a collider's box is worked out from. Equal keys = an equal box.
struct BoundsKey {
    const Transform* xf = nullptr;
    unsigned long long stamp = 0;   // Transform::WorldVersion: moves when the world matrix does
    int shape = -1, n = 0;
    Vec3 offset{0, 0, 0};
    float skin = 0.0f;
    Vec3 size{0, 0, 0};
    bool operator==(const BoundsKey& o) const {
        return xf == o.xf && stamp == o.stamp && shape == o.shape && n == o.n && offset == o.offset && skin == o.skin && size == o.size;
    }
};
// Fills the key; false for the shapes whose box has to be worked out every time.
bool BoundsKeyOf(Collider3D* c, BoundsKey& k) {
    using S = Collider3D::Shape;
    k.xf = c->transform;
    k.stamp = c->transform ? c->transform->WorldVersion() : 0;
    k.shape = (int)c->shape();
    k.offset = c->offset;
    k.skin = c->contactOffset;
    switch (c->shape()) {
    case S::Box: k.size = static_cast<BoxCollider3D*>(c)->size; return true;
    case S::Sphere: k.size.x = static_cast<SphereCollider3D*>(c)->radius; return true;
    case S::Capsule:
    case S::Cylinder: {
        auto* cc = static_cast<CapsuleCollider3D*>(c);
        k.size.x = cc->radius; k.size.y = cc->height; k.n = cc->axis;
        return true;
    }
    case S::Cone: {
        auto* cc = static_cast<ConeCollider3D*>(c);
        k.size.x = cc->radius; k.size.y = cc->height; k.n = cc->axis;
        return true;
    }
    case S::Mesh: {   // its triangles (rebuilt = a new build count), or its size box without any; the hull of a dynamic body
        auto* mc = static_cast<MeshCollider3D*>(c);
        k.size = mc->size;
        k.n = mc->HasTriangles() ? mc->BuildCount() * 2 + (mc->UsesConvexHull() ? 3 : 2) : 0;
        return true;
    }
    default: return false;
    }
}
// The box the tree keeps for a collider: its world bounds, grown by more than any exact test reaches past them (the
// contact offset, the tolerance of the tests, float rounding far from the origin). False when the collider cannot be
// in the tree and every query has to test it.
bool BroadBox(Collider3D* c, phys::Aabb& box) {
    if (IsFieldShape(c)) return false;
    Vec3 mn, mx;
    c->WorldAABB(mn, mx);
    if (!std::isfinite(mn.x + mn.y + mn.z + mx.x + mx.y + mx.z)) return false;
    const Vec3 d = mx - mn;
    float skin = 0.05f + Mathf::Max(c->contactOffset, 0.0f);
    if (UsesHull(c)) skin += d.Magnitude() * 0.03f;   // a flat mesh's hull is thickened a little past its triangles
    const Vec3 pad{skin + d.x * 1e-3f + 8e-6f * Mathf::Max(Mathf::Abs(mn.x), Mathf::Abs(mx.x)),
                   skin + d.y * 1e-3f + 8e-6f * Mathf::Max(Mathf::Abs(mn.y), Mathf::Abs(mx.y)),
                   skin + d.z * 1e-3f + 8e-6f * Mathf::Max(Mathf::Abs(mn.z), Mathf::Abs(mx.z))};
    if (!std::isfinite(pad.x + pad.y + pad.z)) return false;
    box.lo = phys::V3{mn.x - pad.x, mn.y - pad.y, mn.z - pad.z};
    box.hi = phys::V3{mx.x + pad.x, mx.y + pad.y, mx.z + pad.z};
    return true;
}

// A height grid ClosestPoint built (a terrain's, a heightfield collider's): the core shape, and the numbers it was built
// from. Kept while those numbers stay the same (Physics3DEngine.inl, TerrainGrid / FieldGrid).
struct HeightGridMemo {
    phys::Shape shape;
    std::vector<float> nums;                       // sizes, cells and scale
    std::vector<float> heights;                    // a terrain's samples
    std::string text;                              // a heightfield's samples
};

} // namespace

struct Physics3D::QueryIndex {
    struct Leaf {
        Collider3D* col = nullptr;                 // nullptr = a free slot
        int proxy = phys::DynamicTree::kNull;      // its leaf in the tree (kNull: not in the tree)
        std::uint32_t order = 0;                   // its place in the collider list
        std::uint64_t seen = 0;                    // the list version that last had it
        bool current = false;                      // `key` is what the box was worked out from
        BoundsKey key;
    };
    phys::DynamicTree tree;                        // leaf user value = slot in `leaves`
    std::vector<Leaf> leaves;
    std::vector<std::uint32_t> freeSlots;
    std::unordered_map<const Collider3D*, std::uint32_t> slotOf;
    std::vector<std::uint32_t> listed;             // the slot of each collider, in list order
    std::vector<std::uint32_t> loose;              // live colliders that are not in the tree (every query tests them)
    std::vector<std::uint32_t> found;              // scratch
    std::uint64_t listVersion = 0;
    // ClosestPoint's height grids
    std::unordered_map<const Terrain*, HeightGridMemo> terrainGrids;
    std::unordered_map<const Collider3D*, HeightGridMemo> fieldGrids;

    // `found` -> colliders, in the order of the collider list.
    void Emit(std::vector<Collider3D*>& out) {
        std::sort(found.begin(), found.end(), [this](std::uint32_t a, std::uint32_t b) { return leaves[a].order < leaves[b].order; });
        out.clear();
        out.reserve(found.size());
        for (std::uint32_t slot : found) out.push_back(leaves[slot].col);
    }
};

Physics3D::QueryIndex& Physics3D::SyncQueryIndex(Scene& scene) {
    const QueryCache& q = QueryLists(scene);
    if (!m_queryIndex) m_queryIndex = std::make_unique<QueryIndex>();
    QueryIndex& ix = *m_queryIndex;
    if (ix.listVersion != q.version) {   // objects or components came or went: keep the leaves of those that stayed
        ix.listVersion = q.version;
        ix.listed.clear();
        ix.listed.reserve(q.colliders.size());
        for (std::size_t i = 0; i < q.colliders.size(); ++i) {
            Collider3D* c = q.colliders[i];
            auto it = ix.slotOf.find(c);
            std::uint32_t slot;
            if (it != ix.slotOf.end()) slot = it->second;
            else {
                if (!ix.freeSlots.empty()) { slot = ix.freeSlots.back(); ix.freeSlots.pop_back(); }
                else { slot = (std::uint32_t)ix.leaves.size(); ix.leaves.emplace_back(); }
                ix.leaves[slot] = QueryIndex::Leaf{};
                ix.leaves[slot].col = c;
                ix.slotOf.emplace(c, slot);
            }
            ix.leaves[slot].order = (std::uint32_t)i;
            ix.leaves[slot].seen = q.version;
            ix.listed.push_back(slot);
        }
        for (std::uint32_t slot = 0; slot < (std::uint32_t)ix.leaves.size(); ++slot) {
            QueryIndex::Leaf& l = ix.leaves[slot];
            if (!l.col || l.seen == q.version) continue;
            if (l.proxy != phys::DynamicTree::kNull) ix.tree.Destroy(l.proxy);
            ix.slotOf.erase(l.col);
            l = QueryIndex::Leaf{};
            ix.freeSlots.push_back(slot);
        }
        for (auto it = ix.terrainGrids.begin(); it != ix.terrainGrids.end();)
            it = std::find(q.terrains.begin(), q.terrains.end(), it->first) == q.terrains.end() ? ix.terrainGrids.erase(it) : std::next(it);
        for (auto it = ix.fieldGrids.begin(); it != ix.fieldGrids.end();)
            it = ix.slotOf.count(it->first) ? std::next(it) : ix.fieldGrids.erase(it);
    }
    ix.loose.clear();
    for (std::uint32_t slot : ix.listed) {
        QueryIndex::Leaf& l = ix.leaves[slot];
        Collider3D* c = l.col;
        if (!Alive(c)) { l.current = false; continue; }   // no query takes it (Passes); its box is redone when it is back
        BoundsKey k;
        const bool keyed = BoundsKeyOf(c, k);
        if (!(keyed && l.current && l.key == k)) {
            ++m_queryStats.boundsUpdates;
            phys::Aabb box;
            if (BroadBox(c, box)) {
                if (l.proxy == phys::DynamicTree::kNull) l.proxy = ix.tree.Create(box, slot);
                else ix.tree.Move(l.proxy, box, phys::V3{});
            } else if (l.proxy != phys::DynamicTree::kNull) {
                ix.tree.Destroy(l.proxy);
                l.proxy = phys::DynamicTree::kNull;
            }
            l.key = k;
            l.current = keyed;
        }
        if (l.proxy == phys::DynamicTree::kNull) ix.loose.push_back(slot);
    }
    return ix;
}

// The colliders whose box meets the box mn..mx (after SyncQueryIndex). Returns true when that is every collider the
// tree holds (a caller that widens its box step by step can stop).
bool Physics3D::NearBox(const Vec3& mn, const Vec3& mx, std::vector<Collider3D*>& out) {
    QueryIndex& ix = *m_queryIndex;
    if (QueryFullScan() || !std::isfinite(mn.x + mn.y + mn.z + mx.x + mx.y + mx.z)) { out = m_query.colliders; return true; }
    const phys::Aabb box{phys::V3{mn.x, mn.y, mn.z}, phys::V3{mx.x, mx.y, mx.z}};
    ix.found = ix.loose;
    ix.tree.Query(box, [&ix](int proxy) { ix.found.push_back((std::uint32_t)ix.tree.User(proxy)); return true; });
    ix.Emit(out);
    return ix.tree.Root() == phys::DynamicTree::kNull || box.Contains(ix.tree.FatAabb(ix.tree.Root()));
}

// The colliders whose box a box of half-size `halfSize` meets on its way from `origin` along `dir` for `maxDistance`
// (a zero half-size is a ray; a zero `dir` is the box where it stands). After SyncQueryIndex.
void Physics3D::NearPath(const Vec3& origin, const Vec3& dir, float maxDistance, const Vec3& halfSize,
                         std::vector<Collider3D*>& out) {
    QueryIndex& ix = *m_queryIndex;
    const bool usable = maxDistance >= 0.0f && std::isfinite(maxDistance) && std::isfinite(origin.x + origin.y + origin.z) &&
                        std::isfinite(dir.x + dir.y + dir.z) && std::isfinite(halfSize.x + halfSize.y + halfSize.z);
    if (QueryFullScan() || !usable) { out = m_query.colliders; return; }
    ix.found = ix.loose;
    ix.tree.RayCast(phys::V3{origin.x, origin.y, origin.z}, phys::V3{dir.x, dir.y, dir.z}, maxDistance,
                    [&ix](int proxy, float maxT) { ix.found.push_back((std::uint32_t)ix.tree.User(proxy)); return maxT; },
                    phys::V3{Mathf::Abs(halfSize.x), Mathf::Abs(halfSize.y), Mathf::Abs(halfSize.z)});
    ix.Emit(out);
}

RaycastHit3D Physics3D::Raycast(Scene& scene, const Vec3& origin, const Vec3& direction,
                                float maxDistance, GameObject* ignore,
                                std::uint32_t layerMask, bool queryTriggers) {
    RaycastHit3D best;
    best.distance = maxDistance;
    Vec3 dir = direction.Normalized();
    std::vector<Collider3D*> nearby;
    SyncQueryIndex(scene);
    NearPath(origin, dir, maxDistance, Vec3::Zero, nearby);
    const QueryCache& q = m_query;
    ++m_queryStats.queries;
    m_queryStats.considered += nearby.size();
    for (Collider3D* c : nearby) {
        if (!Passes(c, ignore, layerMask, queryTriggers) || !RayNearBounds(c, origin, dir, best.distance)) continue;
        ++m_queryStats.shapeTests;
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
    std::vector<Collider3D*> nearby;
    SyncQueryIndex(scene);
    NearPath(origin, dir, maxDistance, Vec3::Zero, nearby);
    const QueryCache& q = m_query;
    ++m_queryStats.queries;
    m_queryStats.considered += nearby.size();
    for (Collider3D* c : nearby) {
        if (!Passes(c, ignore, layerMask, queryTriggers) || !RayNearBounds(c, origin, dir, maxDistance)) continue;
        ++m_queryStats.shapeTests;
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
    // Sphere / capsule / cylinder: reduce to a sphere nearby c.
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

// `nearby` is what the broad phase found for the shape's box (Physics3D::NearBox), in collider-list order.
std::vector<Collider3D*> Overlap(const std::vector<Collider3D*>& nearby, const QueryShape& q, std::uint32_t mask,
                                 bool queryTriggers, GameObject* ignore, PhysicsQueryStats& st) {
    std::vector<Collider3D*> out;
    ++st.queries;
    for (Collider3D* c : nearby) {
        ++st.considered;
        if (!Passes(c, ignore, mask, queryTriggers)) continue;
        ++st.shapeTests;
        Vec3 n, p;
        if (ShapeTouch(c, q, n, p)) out.push_back(c);
    }
    return out;
}

// The interval of a sweep where `c`'s bounds can meet the query. False when the bounds are unusable or the
// shape never reaches them. `t0`/`t1` come back clamped to [0, maxDist].
bool SweepWindow(Collider3D* c, const Vec3& origin, const Vec3& dir, const Vec3& extent, float maxDist, float& t0, float& t1) {
    Vec3 mn, mx;
    c->WorldAABB(mn, mx);
    if (!(std::isfinite(mn.x + mn.y + mn.z + mx.x + mx.y + mx.z))) return false;
    if (!RaySlabs(origin, dir, mn - extent, mx + extent, t0, t1)) return false;
    if (t1 < 0.0f || t0 > maxDist) return false;
    t0 = Mathf::Max(t0, 0.0f);
    t1 = Mathf::Min(t1, maxDist);
    return true;
}

// Sweep query shape q along unit `dir` up to maxDist; the nearest collider it
// touches. Only the stretch of the path where a collider's (inflated) bounds
// lie is marched, in steps of half the shape's thinnest size (so a thin wall
// can't slip between two samples), and each touch is refined by bisection.
// `skipStartOverlaps` ignores colliders already touching at the start (as
// casts usually do; continuous collision needs it so a body resting on the floor can
// still slide along it).
// `nearby` is what the broad phase found along the path (Physics3D::NearPath), in collider-list order.
RaycastHit3D Sweep(const std::vector<Collider3D*>& nearby, const QueryShape& q, const Vec3& dir, float maxDist,
                   GameObject* ignore, std::uint32_t mask, bool queryTriggers,
                   bool skipStartOverlaps, PhysicsQueryStats& st, bool skipTriMeshes = false) {
    RaycastHit3D best;
    struct Cand { Collider3D* c; float t0, t1; };
    std::vector<Cand> cands;
    const Vec3 o = q.Center(), ext = q.Extent();
    float lo = 1e30f, hi = -1e30f;
    ++st.queries;
    for (Collider3D* c : nearby) {
        ++st.considered;
        if (!Passes(c, ignore, mask, queryTriggers)) continue;
        if (skipTriMeshes && MeshHasTris(c)) continue;
        float t0, t1;
        if (!SweepWindow(c, o, dir, ext, maxDist, t0, t1)) continue;
        ++st.shapeTests;
        Vec3 n, p;   // (the bounds test comes first: it is cheap, and most colliders are nowhere nearby the path)
        if (skipStartOverlaps && ShapeTouch(c, q, n, p)) continue;
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

// How far `q` reaches from its centre along `n` (n should be unit length).
float ShapeReach(const QueryShape& q, const Vec3& n) {
    if (q.kind == QueryShape::Box)
        return std::fabs(Vec3::Dot(q.box.ax, n)) * q.box.h.x
             + std::fabs(Vec3::Dot(q.box.ay, n)) * q.box.h.y
             + std::fabs(Vec3::Dot(q.box.az, n)) * q.box.h.z;
    float reach = q.r;
    if (q.kind == QueryShape::Capsule) {
        Vec3 seg = q.b - q.a;
        float len = seg.Magnitude();
        if (len > 1e-8f) reach += 0.5f * len * std::fabs(Vec3::Dot(seg * (1.0f / len), n));
    }
    return reach;
}

// Terrain is part of every shape cast, not only a zero-radius ray. The ray hits the
// surface; the cast hits earlier by the shape's reach along the ground normal.
void AddTerrainSweep(const std::vector<Terrain*>& terrains, const QueryShape& q, const Vec3& dir, float maxDist,
                     GameObject* ignore, std::uint32_t mask, std::vector<RaycastHit3D>& hits) {
    if (!(dir.SqrMagnitude() > 0.5f) || !(maxDist > 0.0f)) return;
    for (Terrain* terr : terrains) {
        if (!TerrainPasses(terr, ignore, mask) || !terr->gameObject) continue;
        float t = 0.0f;
        Vec3 n{0, 1, 0};
        float reachGuess = q.kind == QueryShape::Box ? (q.box.h.x + q.box.h.y + q.box.h.z) : q.r + 0.01f;
        if (!RayTerrain(terr, q.Center(), dir, maxDist + reachGuess + 1.0f, t, n)) continue;
        float approach = -Vec3::Dot(n, dir);
        if (!(approach > 0.05f)) continue;
        float hitT = t - ShapeReach(q, n) / approach;
        if (hitT < 0.0f) hitT = 0.0f;
        if (hitT > maxDist) continue;
        RaycastHit3D h;
        h.hit = true; h.collider = nullptr; h.gameObject = terr->gameObject;
        h.distance = hitT; h.point = q.Center() + dir * t; h.normal = n;
        hits.push_back(h);
    }
}

// Every collider the sweep touches, nearest first. Each candidate is marched on its own, with the same
// step and bisection as Sweep, so a thin wall between two samples is not skipped and a later wall is kept.
// A zero direction or a zero distance still reports what the shape already overlaps, the same as SphereCast
// testing the place it stands.
// `nearby` is what the broad phase found along the path (Physics3D::NearPath), in collider-list order.
std::vector<RaycastHit3D> SweepAll(const std::vector<Collider3D*>& nearby, const std::vector<Terrain*>& terrains,
                                   const QueryShape& q, const Vec3& dir, float maxDist,
                                   GameObject* ignore, std::uint32_t mask, bool queryTriggers, PhysicsQueryStats& st) {
    std::vector<RaycastHit3D> hits;
    ++st.queries;
    const bool badDir = !(dir.SqrMagnitude() >= 1e-12f) || !std::isfinite(dir.x) || !std::isfinite(dir.y) || !std::isfinite(dir.z);
    const bool zeroDist = !(maxDist > 1e-8f);
    if (badDir || zeroDist) {
        if (!(maxDist >= 0.0f) || !std::isfinite(maxDist)) return hits;
        for (Collider3D* c : nearby) {
            ++st.considered;
            if (!Passes(c, ignore, mask, queryTriggers)) continue;
            ++st.shapeTests;
            Vec3 n, p;
            if (!ShapeTouch(c, q, n, p)) continue;
            RaycastHit3D h;
            h.hit = true; h.collider = c; h.gameObject = c->gameObject;
            h.distance = 0.0f; h.point = p; h.normal = n;
            hits.push_back(h);
        }
        return hits;
    }
    struct Cand { Collider3D* c; float t0, t1; };
    std::vector<Cand> cands;
    const Vec3 o = q.Center(), ext = q.Extent();
    for (Collider3D* c : nearby) {
        ++st.considered;
        if (!Passes(c, ignore, mask, queryTriggers)) continue;
        float t0, t1;
        if (!SweepWindow(c, o, dir, ext, maxDist, t0, t1)) continue;
        ++st.shapeTests;
        cands.push_back({c, t0, t1});
    }
    float step = Mathf::Max(q.Thinnest() * 0.5f, 0.02f);
    if (!std::isfinite(step) || step <= 0.0f) return hits;
    for (const Cand& cd : cands) {
        if (!(cd.t1 >= cd.t0) || !std::isfinite(cd.t0 + cd.t1)) continue;
        Vec3 n, p;
        auto touchAt = [&](float t) { return ShapeTouch(cd.c, q.Moved(dir * t), n, p); };
        float hitT = cd.t0;
        bool found = touchAt(cd.t0);
        if (!found) {
            float prev = cd.t0;
            float span = cd.t1 - cd.t0;
            float st = (span / step > 20000.0f) ? span / 20000.0f : step;
            int samples = 0;
            for (float t = cd.t0 + st; ; t += st) {
                bool last = t >= cd.t1 || ++samples > 20002;
                if (last) t = cd.t1;
                if (touchAt(t)) {
                    float a = prev, b = t;
                    for (int it = 0; it < 12 && b > a; ++it) {
                        float m = (a + b) * 0.5f;
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
        RaycastHit3D h;
        h.hit = true; h.collider = cd.c; h.gameObject = cd.c->gameObject;
        h.distance = hitT; h.point = p; h.normal = n;
        hits.push_back(h);
    }
    AddTerrainSweep(terrains, q, dir, maxDist, ignore, mask, hits);
    std::sort(hits.begin(), hits.end(), [](const RaycastHit3D& a, const RaycastHit3D& b) {
        return a.distance < b.distance;
    });
    return hits;
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

// Push one sphere centre out of one solid collider. True when the centre moved.
bool NudgeSphere(Collider3D* col, Vec3& c, float r) {
    if (MeshCollider3D* mc = TriMeshOf(col)) {
        trimesh::Contact k = trimesh::WorldView(ActiveTris(mc), mc->WorldMatrix()).Sphere(c, r);
        if (k.hit && k.penetration > 1e-5f) { c = c + k.normal * k.penetration; return true; }
        return false;
    }
    if (col->shape() == Collider3D::Shape::Sphere) {
        Vec3 sc; float sr; AsSphere(col, c, sc, sr);
        Vec3 d = c - sc; float dl = d.Magnitude();
        if (dl < r + sr) {
            Vec3 n = dl > 1e-5f ? d * (1.0f / dl) : Vec3{0, 1, 0};
            c = sc + n * (r + sr); return true;
        }
        return false;
    }
    if (IsBoxLike(col->shape())) {
        auto* box = static_cast<BoxCollider3D*>(col);
        Vec3 bc = box->WorldCenter(), h = box->HalfExtents();
        Vec3 ax, ay, az; box->WorldAxes(ax, ay, az);
        Vec3 rel = c - bc;
        Vec3 l{Vec3::Dot(rel, ax), Vec3::Dot(rel, ay), Vec3::Dot(rel, az)};
        Vec3 cl = ClampVec(l, {-h.x, -h.y, -h.z}, {h.x, h.y, h.z});
        Vec3 dl3 = l - cl; float dl = dl3.Magnitude();
        Vec3 ln;
        float push = 0.0f;
        if (dl > 1e-5f) {
            if (dl < r) { ln = dl3 * (1.0f / dl); push = r - dl; }
        } else {
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
            Vec3 wn = ax * ln.x + ay * ln.y + az * ln.z;
            c = c + wn * push; return true;
        }
        return false;
    }
    if (IsGenericShape(col) || col->shape() == Collider3D::Shape::Capsule) {
        Vec3 sn, sp;
        if (!SphereTouch(col, c, r, sn, sp)) return false;
        SupFn sup = [c, r](const Vec3& d) { float m = d.Magnitude(); return m > 1e-12f ? c + d * (r / m) : c; };
        float depth;
        if (IsGenericShape(col)) {
            Contact k = GenericVs(col, sup, c - Vec3{r, r, r}, c + Vec3{r, r, r});
            if (!k.hit) return false;
            depth = k.penetration; sn = k.normal;
        } else {
            Vec3 sc; float sr; AsSphere(col, c, sc, sr);
            Vec3 dv = c - sc; float dl = dv.Magnitude();
            depth = r + sr - dl; sn = dl > 1e-6f ? dv * (1.0f / dl) : Vec3{0, 1, 0};
        }
        if (depth > 1e-5f) { c = c + sn * depth; return true; }
        return false;
    }
    Vec3 mn, mx; col->WorldAABB(mn, mx);
    Vec3 cp = ClampVec(c, mn, mx);
    Vec3 d = c - cp; float dl = d.Magnitude();
    if (dl > 1e-5f) {
        if (dl < r) { c = cp + d * (1.0f / dl) * r; return true; }
        return false;
    }
    float dxl = c.x - mn.x, dxh = mx.x - c.x;
    float dyl = c.y - mn.y, dyh = mx.y - c.y;
    float dzl = c.z - mn.z, dzh = mx.z - c.z;
    float m = dxl; Vec3 n{-1, 0, 0};
    if (dxh < m) { m = dxh; n = {1, 0, 0}; }
    if (dyl < m) { m = dyl; n = {0, -1, 0}; }
    if (dyh < m) { m = dyh; n = {0, 1, 0}; }
    if (dzl < m) { m = dzl; n = {0, 0, -1}; }
    if (dzh < m) { m = dzh; n = {0, 0, 1}; }
    c = c + n * (m + r);
    return true;
}
} // namespace

template <class Shape>
std::vector<Collider3D*> Physics3D::OverlapShape(Scene& scene, const Shape& q, std::uint32_t layerMask, bool queryTriggers,
                                                 GameObject* ignore) {
    std::vector<Collider3D*> nearby;
    SyncQueryIndex(scene);
    const Vec3 c = q.Center(), e = q.Extent();
    const Vec3 h{Mathf::Abs(e.x), Mathf::Abs(e.y), Mathf::Abs(e.z)};
    NearBox(c - h, c + h, nearby);
    return Overlap(nearby, q, layerMask, queryTriggers, ignore, m_queryStats);
}
template <class Shape>
RaycastHit3D Physics3D::SweepShape(Scene& scene, const Shape& q, const Vec3& dir, float maxDistance, GameObject* ignore,
                                   std::uint32_t layerMask, bool queryTriggers) {
    std::vector<Collider3D*> nearby;
    SyncQueryIndex(scene);
    NearPath(q.Center(), dir, maxDistance, q.Extent(), nearby);
    return Sweep(nearby, q, dir, maxDistance, ignore, layerMask, queryTriggers, /*skipStartOverlaps*/ false, m_queryStats);
}
template <class Shape>
std::vector<RaycastHit3D> Physics3D::SweepShapeAll(Scene& scene, const Shape& q, const Vec3& dir, float maxDistance,
                                                   GameObject* ignore, std::uint32_t layerMask, bool queryTriggers) {
    std::vector<Collider3D*> nearby;
    SyncQueryIndex(scene);
    NearPath(q.Center(), dir, maxDistance, q.Extent(), nearby);
    return SweepAll(nearby, m_query.terrains, q, dir, maxDistance, ignore, layerMask, queryTriggers, m_queryStats);
}

std::vector<Collider3D*> Physics3D::OverlapSphere(Scene& scene, const Vec3& center, float radius,
                                                  std::uint32_t layerMask, bool queryTriggers,
                                                  GameObject* ignore) {
    return OverlapShape(scene, SphereShape(center, radius), layerMask, queryTriggers, ignore);
}

std::vector<Collider3D*> Physics3D::OverlapBox(Scene& scene, const Vec3& center, const Vec3& halfExtents,
                                               const Quat& rotation, std::uint32_t layerMask,
                                               bool queryTriggers, GameObject* ignore) {
    return OverlapShape(scene, BoxShape(center, halfExtents, rotation), layerMask, queryTriggers, ignore);
}

std::vector<Collider3D*> Physics3D::OverlapCapsule(Scene& scene, const Vec3& a, const Vec3& b, float radius,
                                                   std::uint32_t layerMask, bool queryTriggers,
                                                   GameObject* ignore) {
    return OverlapShape(scene, CapsuleShape(a, b, radius), layerMask, queryTriggers, ignore);
}

bool& Physics3D::QueryFullScan() { static bool on = false; return on; }

Physics3D::IgnoreHierarchy::IgnoreHierarchy(const GameObject* root) : m_prev(g_ignoreTree) { g_ignoreTree = root; }
Physics3D::IgnoreHierarchy::~IgnoreHierarchy() { g_ignoreTree = m_prev; }

RaycastHit3D Physics3D::SphereCast(Scene& scene, const Vec3& origin, const Vec3& direction,
                                   float radius, float maxDistance, GameObject* ignore,
                                   std::uint32_t layerMask, bool queryTriggers) {
    Vec3 dir = direction.Normalized();
    if (radius <= 0.0f) return Raycast(scene, origin, dir, maxDistance, ignore, layerMask, queryTriggers);
    return SweepShape(scene, SphereShape(origin, radius), dir, maxDistance, ignore, layerMask,
                 queryTriggers);
}

std::vector<RaycastHit3D> Physics3D::SphereCastAll(Scene& scene, const Vec3& origin, const Vec3& direction,
                                                   float radius, float maxDistance, GameObject* ignore,
                                                   std::uint32_t layerMask, bool queryTriggers) {
    Vec3 dir = direction.Normalized();
    if (radius <= 0.0f) return RaycastAll(scene, origin, dir, maxDistance, ignore, layerMask, queryTriggers);
    return SweepShapeAll(scene, SphereShape(origin, radius), dir, maxDistance, ignore, layerMask, queryTriggers);
}

RaycastHit3D Physics3D::BoxCast(Scene& scene, const Vec3& center, const Vec3& halfExtents,
                                const Vec3& direction, const Quat& rotation, float maxDistance,
                                GameObject* ignore, std::uint32_t layerMask, bool queryTriggers) {
    return SweepShape(scene, BoxShape(center, halfExtents, rotation), direction.Normalized(), maxDistance,
                 ignore, layerMask, queryTriggers);
}

std::vector<RaycastHit3D> Physics3D::BoxCastAll(Scene& scene, const Vec3& center, const Vec3& halfExtents,
                                               const Vec3& direction, const Quat& rotation, float maxDistance,
                                               GameObject* ignore, std::uint32_t layerMask, bool queryTriggers) {
    // A box with no size is a ray, and a ray includes terrain (the same as SphereCastAll with radius 0).
    if (!(halfExtents.x > 1e-6f) && !(halfExtents.y > 1e-6f) && !(halfExtents.z > 1e-6f))
        return RaycastAll(scene, center, direction.Normalized(), maxDistance, ignore, layerMask, queryTriggers);
    return SweepShapeAll(scene, BoxShape(center, halfExtents, rotation), direction.Normalized(), maxDistance,
                    ignore, layerMask, queryTriggers);
}

RaycastHit3D Physics3D::CapsuleCast(Scene& scene, const Vec3& a, const Vec3& b, float radius,
                                    const Vec3& direction, float maxDistance, GameObject* ignore,
                                    std::uint32_t layerMask, bool queryTriggers) {
    return SweepShape(scene, CapsuleShape(a, b, radius), direction.Normalized(), maxDistance,
                 ignore, layerMask, queryTriggers);
}

std::vector<RaycastHit3D> Physics3D::CapsuleCastAll(Scene& scene, const Vec3& a, const Vec3& b, float radius,
                                                   const Vec3& direction, float maxDistance, GameObject* ignore,
                                                   std::uint32_t layerMask, bool queryTriggers) {
    if (!(radius > 0.0f) && (a - b).SqrMagnitude() < 1e-12f)
        return RaycastAll(scene, a, direction.Normalized(), maxDistance, ignore, layerMask, queryTriggers);
    return SweepShapeAll(scene, CapsuleShape(a, b, radius), direction.Normalized(), maxDistance,
                    ignore, layerMask, queryTriggers);
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

// ResolveSphere and ResolveCapsule: only a collider the shape reaches can push it, so a pass asks the broad phase for
// the colliders within `slack` of where the shape starts. When the pushes carry it further than `slack`, the same pass
// is done again from its start with a wider net, so the result is the one a pass over every collider gives.
Vec3 Physics3D::ResolveSphere(Scene& scene, Vec3 c, float r, GameObject* ignore, int iterations) {
    SyncQueryIndex(scene);
    ++m_queryStats.queries;
    std::vector<Collider3D*> nearby;
    const float reach = Mathf::Abs(r), firstSlack = Mathf::Max(reach, 0.5f);
    float slack = firstSlack;
    for (int it = 0; it < iterations; ++it) {
        bool moved = false, tooFar = false;
        const Vec3 from = c, net{reach + slack, reach + slack, reach + slack};
        const bool all = NearBox(from - net, from + net, nearby);
        m_queryStats.considered += nearby.size();
        for (Collider3D* col : nearby) {
            if (!Alive(col)) continue;
            if (ignore && col->gameObject == ignore) continue;
            if (col->isTrigger) continue;
            if (NudgeSphere(col, c, r)) moved = true;
            if (moved && !all && !((c - from).SqrMagnitude() <= slack * slack)) { tooFar = true; break; }
        }
        if (tooFar) { c = from; slack *= 4.0f; --it; continue; }
        slack = firstSlack;
        if (!moved) break;
    }
    return c;
}

Vec3 Physics3D::ResolveCapsule(Scene& scene, Vec3 center, Vec3 axis, float halfSegment, float radius,
                               GameObject* ignore, int iterations) {
    float al = axis.Magnitude();
    if (al > 1e-8f) axis = axis * (1.0f / al);
    else axis = {0, 1, 0};
    if (!(halfSegment > 1e-4f) || !(radius > 0.0f))
        return ResolveSphere(scene, center, std::max(radius, 0.0f), ignore, iterations);
    SyncQueryIndex(scene);
    ++m_queryStats.queries;
    std::vector<Collider3D*> nearby;
    const Vec3 reach{Mathf::Abs(axis.x) * halfSegment + radius, Mathf::Abs(axis.y) * halfSegment + radius, Mathf::Abs(axis.z) * halfSegment + radius};
    const float firstSlack = Mathf::Max(radius, 0.5f);
    float slack = firstSlack;
    for (int it = 0; it < iterations; ++it) {
        bool moved = false, tooFar = false;
        Vec3 a = center - axis * halfSegment;
        Vec3 b = center + axis * halfSegment;
        const Vec3 from = center, net = reach + Vec3{slack, slack, slack};
        const bool all = NearBox(from - net, from + net, nearby);
        m_queryStats.considered += nearby.size();
        for (Collider3D* col : nearby) {
            if (!Alive(col) || (ignore && col->gameObject == ignore) || col->isTrigger) continue;
            // Seven stations along the segment, plus the point nearest this collider.
            // Three stations (the two ends and the waist) miss a corner that overlaps
            // the body between them.
            Vec3 samples[8];
            int ns = 0;
            for (int s = 0; s <= 6; ++s) samples[ns++] = a + (b - a) * ((float)s / 6.0f);
            samples[ns++] = ClosestOnSegment(col->WorldCenter(), a, b);
            Vec3 best{0, 0, 0};
            float best2 = 0.0f;
            for (int si = 0; si < ns; ++si) {
                const Vec3& sample = samples[si];
                Vec3 s = sample;
                if (!NudgeSphere(col, s, radius)) continue;
                Vec3 d = s - sample;
                float m = d.SqrMagnitude();
                if (m > best2) { best2 = m; best = d; }
            }
            if (best2 > 1e-12f) {
                center = center + best;
                a = center - axis * halfSegment;
                b = center + axis * halfSegment;
                moved = true;
                if (!all && !((center - from).SqrMagnitude() <= slack * slack)) { tooFar = true; break; }
            }
        }
        if (tooFar) { center = from; slack *= 4.0f; --it; continue; }
        slack = firstSlack;
        if (!moved) break;
    }
    return center;
}

} // namespace okay

#include "Physics3DEngine.inl"   // our own physics engine core runs the scene
