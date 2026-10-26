#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Components/Joint3D.hpp"
#include "okay/Components/Terrain.hpp"
#include "okay/Components/VoxelTerrain.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Mathf.hpp"

#include <algorithm>
#include <cmath>
#include <vector>
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

Contact TestSphereSphere(const Vec3& ca, float ra, const Vec3& cb, float rb) {
    Contact c;
    Vec3 d = cb - ca;
    float r = ra + rb;
    float d2 = d.SqrMagnitude();
    if (d2 >= r * r) return c;
    float dist = Mathf::Sqrt(d2);
    c.hit = true;
    c.normal = dist > Mathf::Epsilon ? d / dist : Vec3{0, 1, 0};
    c.penetration = r - dist;
    c.point = ca + c.normal * ra;        // on A's surface toward B
    return c;
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

// Cylinder reuses the capsule's segment+radius collision; Mesh reuses the box AABB.
// So new shapes plug into the existing resolvers via inheritance (CylinderCollider3D
// is-a CapsuleCollider3D, MeshCollider3D is-a BoxCollider3D).
inline bool IsCapsuleLike(Collider3D::Shape s) {
    return s == Collider3D::Shape::Capsule || s == Collider3D::Shape::Cylinder;
}
inline bool IsBoxLike(Collider3D::Shape s) {
    return s == Collider3D::Shape::Box || s == Collider3D::Shape::Mesh;
}
// A mesh collider with real triangles is resolved per triangle in its own pass.
bool MeshHasTris(Collider3D* col) {
    if (col->shape() != Collider3D::Shape::Mesh || !col->gameObject) return false;
    auto* mr = col->gameObject->GetComponent<MeshRenderer>();
    return mr && !mr->DisplayMesh().triangles.empty();
}

// Closest point on triangle (a,b,c) to point p — Ericson, Real-Time Collision
// Detection. Used for exact mesh-collider contact.
inline Vec3 ClosestPointOnTri(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c) {
    Vec3 ab = b - a, ac = c - a, ap = p - a;
    float d1 = Vec3::Dot(ab, ap), d2 = Vec3::Dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a;
    Vec3 bp = p - b;
    float d3 = Vec3::Dot(ab, bp), d4 = Vec3::Dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return b;
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) return a + ab * (d1 / (d1 - d3));
    Vec3 cp = p - c;
    float d5 = Vec3::Dot(ab, cp), d6 = Vec3::Dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return c;
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) return a + ac * (d2 / (d2 - d6));
    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
        return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    float denom = 1.0f / (va + vb + vc);
    return a + ab * (vb * denom) + ac * (vc * denom);
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

Contact TestColliders(Collider3D* a, Collider3D* b) {
    using S = Collider3D::Shape;
    S sa = a->shape(), sb = b->shape();

    // A mesh collider that actually has geometry is resolved exactly (per triangle)
    // in a dedicated pass — skip it here so it doesn't ALSO over-block as a big AABB
    // box. A mesh collider with no MeshRenderer keeps the simple box behaviour.
    if (MeshHasTris(a) || MeshHasTris(b)) return {};

    // Capsule/cylinder vs capsule/cylinder: closest points between the two segments.
    if (IsCapsuleLike(sa) && IsCapsuleLike(sb)) {
        auto* ca = static_cast<CapsuleCollider3D*>(a);
        auto* cb = static_cast<CapsuleCollider3D*>(b);
        Vec3 a0, a1, b0, b1; ca->Segment(a0, a1); cb->Segment(b0, b1);
        Vec3 pa, pb; ClosestSegSeg(a0, a1, b0, b1, pa, pb);
        return TestSphereSphere(pa, ca->WorldRadius(), pb, cb->WorldRadius());
    }

    bool aBox = IsBoxLike(sa), bBox = IsBoxLike(sb);
    if (aBox && bBox) {
        return TestOBB3OBB3(MakeOBB3(static_cast<BoxCollider3D*>(a)),
                            MakeOBB3(static_cast<BoxCollider3D*>(b)));
    }
    if (aBox) { // A box, B sphere/capsule
        auto* box = static_cast<BoxCollider3D*>(a);
        Vec3 sc; float sr; AsSphere(b, box->WorldCenter(), sc, sr);
        return TestOBB3Sphere(MakeOBB3(box), sc, sr);
    }
    if (bBox) { // A sphere/capsule, B box
        auto* box = static_cast<BoxCollider3D*>(b);
        Vec3 sc; float sr; AsSphere(a, box->WorldCenter(), sc, sr);
        Contact c = TestOBB3Sphere(MakeOBB3(box), sc, sr);
        c.normal = -c.normal; // flip to point from A toward B
        return c;
    }
    // Sphere/capsule vs sphere/capsule.
    Vec3 ac, bc; float ar, br;
    AsSphere(a, b->WorldCenter(), ac, ar);
    AsSphere(b, a->WorldCenter(), bc, br);
    return TestSphereSphere(ac, ar, bc, br);
}

// Inverse moment of inertia about the center, as an isotropic scalar (exact for
// spheres; a good stable approximation for boxes/capsules). 0 when rotation is
// locked, mass is infinite, or there's no collider to size the inertia from.
float InvInertia(Rigidbody3D* rb, Collider3D* col) {
    if (!rb || rb->freezeRotation || !col) return 0.0f;
    if (rb->InvMass() <= 0.0f) return 0.0f;
    float mass = rb->mass, I;
    if (col->shape() == Collider3D::Shape::Sphere) {
        float r = static_cast<SphereCollider3D*>(col)->WorldRadius();
        I = 0.4f * mass * r * r;
    } else {
        Vec3 mn, mx; col->WorldAABB(mn, mx);
        float w = mx.x - mn.x, h = mx.y - mn.y, d = mx.z - mn.z;
        I = mass * (w * w + h * h + d * d) / 12.0f;   // isotropic box approximation
    }
    return I > 1e-8f ? 1.0f / I : 0.0f;
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
                          const Vec3& relVel) {
    Collision3D c;
    c.gameObject = other->gameObject; c.collider = other;
    c.normal = n; c.penetration = pen; c.point = point;
    c.relativeVelocity = relVel;
    float closing = -Vec3::Dot(relVel, n);   // > 0 when the two approach
    c.impactSpeed = closing > 0.0f ? closing : 0.0f;
    return c;
}

// Does trigger collider `trig` react to `other`? (its tag / layer filter)
bool TriggerAccepts(const Collider3D* trig, const Collider3D* other) {
    if (!trig->isTrigger) return true;
    if (!trig->triggerTag.empty() && (!other->gameObject || other->gameObject->tag != trig->triggerTag))
        return false;
    return ((trig->triggerLayers >> (other->layer & 31)) & 1u) != 0;
}

bool Alive(Collider3D* c) { return c->enabled && c->gameObject && c->gameObject->active; }

} // namespace

void Physics3D::Step(Scene& scene, float dt) {
    if (dt <= 0.0f) return;

    // Sleeping thresholds: under these speeds for kSleepTime seconds, a dynamic body
    // sleeps (skipped until woken). Mirrors the PhysX rest optimisation.
    constexpr float kLinSleep = 0.05f;   // world units / second
    constexpr float kAngSleep = 0.05f;   // radians / second
    constexpr float kSleepTime = 0.5f;   // seconds at rest before sleeping

    // 1) Integrate dynamic / kinematic bodies.
    auto bodies = scene.FindObjectsOfType<Rigidbody3D>();
    for (Rigidbody3D* rb : bodies) {
        if (!rb->enabled || !rb->gameObject || !rb->gameObject->active) continue;
        Transform* t = rb->transform;
        // A sleeping body holds position until woken; a directly-set velocity above
        // the threshold wakes it so scripted moves/teleports still take effect.
        if (rb->bodyType == Rigidbody3D::BodyType::Dynamic && rb->sleeping) {
            if (rb->velocity.SqrMagnitude() > kLinSleep * kLinSleep ||
                rb->angularVelocity.SqrMagnitude() > kAngSleep * kAngSleep) {
                rb->WakeUp();
            } else {
                rb->ConsumeForce(); rb->ConsumeTorque();
                continue;
            }
        }
        if (rb->bodyType == Rigidbody3D::BodyType::Dynamic) {
            Vec3 accel = gravity * rb->gravityScale + rb->ConsumeForce() * rb->InvMass();
            rb->velocity = rb->velocity + accel * dt;
            if (rb->drag > 0.0f) rb->velocity = rb->velocity * (1.0f / (1.0f + rb->drag * dt));
            if (rb->maxFallSpeed > 0.0f && rb->velocity.y < -rb->maxFallSpeed)
                rb->velocity.y = -rb->maxFallSpeed;   // clamp terminal fall speed
        }
        if (rb->freezeX) rb->velocity.x = 0.0f;
        if (rb->freezeY) rb->velocity.y = 0.0f;
        if (rb->freezeZ) rb->velocity.z = 0.0f;
        // Angular: apply accumulated torque (scaled by inverse inertia) and damp.
        if (rb->bodyType == Rigidbody3D::BodyType::Dynamic && !rb->freezeRotation) {
            float invI = InvInertia(rb, rb->gameObject->GetComponent<Collider3D>());
            rb->angularVelocity = rb->angularVelocity + rb->ConsumeTorque() * (invI * dt);
            if (rb->angularDrag > 0.0f) rb->angularVelocity = rb->angularVelocity * (1.0f / (1.0f + rb->angularDrag * dt));
        } else {
            rb->ConsumeTorque();   // discard so it doesn't accumulate while frozen
        }
        if (rb->bodyType != Rigidbody3D::BodyType::Static) {
            if (rb->continuousCollision) SweepBody(scene, rb, dt);   // stops at walls it would skip
            else t->localPosition = t->localPosition + rb->velocity * dt;
            // Integrate orientation: q += 0.5 * (ω as quat) * q * dt, renormalized.
            if (!rb->freezeRotation && rb->angularVelocity.SqrMagnitude() > 1e-12f) {
                const Vec3& w = rb->angularVelocity;
                Quat q = t->localRotation;
                Quat wq{w.x, w.y, w.z, 0.0f};
                Quat dq = wq * q;
                float h = 0.5f * dt;
                t->localRotation = Quat{q.x + dq.x * h, q.y + dq.y * h,
                                        q.z + dq.z * h, q.w + dq.w * h}.Normalized();
            }
        }
    }

    // 2) Broad + narrow phase over every collider pair.
    auto colliders = scene.FindObjectsOfType<Collider3D>();
    std::set<Pair> current;
    // Contact callbacks below may remove colliders / bodies (RemoveComponent frees
    // them at once); every raw pointer held across a callback is checked first.
    Scene::RemovalWatch gone(scene);

    // Precompute each collider's world AABB once per step (it was recomputed O(n)
    // times per collider inside the pair loop) and a fast membership set used by the
    // exit-message cleanup below (was an O(contacts*colliders) linear scan).
    const std::size_t nc = colliders.size();
    std::vector<Vec3> aabbMin(nc), aabbMax(nc);
    // A body whose pose or velocity went non-finite (NaN/inf) is left out of the
    // contact phase: NaN fails every AABB rejection test, so it would "touch"
    // everything and spread NaN into each body it's resolved against.
    std::vector<char> finite(nc, 1);
    std::unordered_set<Collider3D*> aliveSet;
    aliveSet.reserve(nc * 2 + 1);
    auto fin3 = [](const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    for (std::size_t i = 0; i < nc; ++i) {
        colliders[i]->WorldAABB(aabbMin[i], aabbMax[i]);
        aliveSet.insert(colliders[i]);
        const Rigidbody3D* r = colliders[i]->gameObject->GetComponent<Rigidbody3D>();
        finite[i] = fin3(aabbMin[i]) && fin3(aabbMax[i]) &&
                    (!r || (fin3(r->velocity) && fin3(r->angularVelocity)));
    }

    for (std::size_t i = 0; i < nc; ++i) {
        for (std::size_t j = i + 1; j < nc; ++j) {
            Collider3D* a = colliders[i];
            Collider3D* b = colliders[j];
            if (gone.Gone(a)) break;
            if (gone.Gone(b)) continue;
            if (!a->enabled || !b->enabled) continue;
            if (!a->gameObject->active || !b->gameObject->active) continue;
            if (a->gameObject == b->gameObject) continue;
            if (!finite[i] || !finite[j]) continue;   // a NaN body touches nothing
            if (!LayersCollide(a->layer, b->layer)) continue;

            // Broad phase: AABB reject (precomputed AABBs).
            const Vec3 &aMin = aabbMin[i], &aMax = aabbMax[i], &bMin = aabbMin[j], &bMax = aabbMax[j];
            if (aMax.x < bMin.x || aMin.x > bMax.x ||
                aMax.y < bMin.y || aMin.y > bMax.y ||
                aMax.z < bMin.z || aMin.z > bMax.z) continue;

            bool trigger = a->isTrigger || b->isTrigger;
            // Trigger tag / layer filters: a filtered-out object passes through
            // silently (no events on either side).
            if (trigger && (!TriggerAccepts(a, b) || !TriggerAccepts(b, a))) continue;

            Contact c = TestColliders(a, b);
            if (!c.hit) continue;

            current.insert({a, b});

            Rigidbody3D* ra = a->gameObject->GetComponent<Rigidbody3D>();
            Rigidbody3D* rb = b->gameObject->GetComponent<Rigidbody3D>();
            // Relative velocity before the solver changes it (for impact speed).
            const Vec3 relVelBA = (rb ? rb->velocity : Vec3::Zero) - (ra ? ra->velocity : Vec3::Zero);
            if (PhysicsDebug::Collect() && (int)m_debugContacts.size() < PhysicsDebug::kMaxContacts)
                m_debugContacts.push_back({c.point, c.normal, trigger});

            // 3) Resolve solids (skip triggers and pairs without dynamics).
            if (!trigger) {
                // Wake a sleeping body when a moving body runs into it; two bodies
                // both at rest stay asleep (resting stack).
                auto awakeMover = [](Rigidbody3D* r) {
                    return r && !r->sleeping && r->bodyType != Rigidbody3D::BodyType::Static;
                };
                if (ra && ra->sleeping && awakeMover(rb)) ra->WakeUp();
                if (rb && rb->sleeping && awakeMover(ra)) rb->WakeUp();
                if ((!(ra && ra->sleeping) || !(rb && rb->sleeping))) {
                float ima = ra ? ra->InvMass() : 0.0f;
                float imb = rb ? rb->InvMass() : 0.0f;
                float imSum = ima + imb;
                if (imSum > 0.0f) {
                    Vec3 correction = c.normal * (c.penetration / imSum);
                    a->transform->localPosition = a->transform->localPosition - correction * ima;
                    b->transform->localPosition = b->transform->localPosition + correction * imb;

                    // Angular terms: lever arms from each body's center to the contact
                    // point and inverse inertia. When rotation is locked these are 0, so
                    // the math collapses to the old linear-only impulse exactly.
                    float iia = InvInertia(ra, a);
                    float iib = InvInertia(rb, b);
                    Vec3 cenA = a->WorldCenter(), cenB = b->WorldCenter();
                    Vec3 rA = c.point - cenA, rB = c.point - cenB;
                    auto pointVel = [](const Vec3& v, const Vec3& w, const Vec3& r) {
                        return v + Vec3::Cross(w, r);
                    };
                    Vec3 wa0 = ra ? ra->angularVelocity : Vec3::Zero;
                    Vec3 wb0 = rb ? rb->angularVelocity : Vec3::Zero;
                    Vec3 va = ra ? ra->velocity : Vec3::Zero;
                    Vec3 vb = rb ? rb->velocity : Vec3::Zero;
                    Vec3 rvel = pointVel(vb, wb0, rB) - pointVel(va, wa0, rA);
                    float velAlongNormal = Vec3::Dot(rvel, c.normal);
                    // Effective mass along n includes the angular term invI*|r×n|².
                    Vec3 rnA = Vec3::Cross(rA, c.normal), rnB = Vec3::Cross(rB, c.normal);
                    float denom = imSum + rnA.SqrMagnitude() * iia + rnB.SqrMagnitude() * iib;
                    float jImp = 0.0f;
                    if (velAlongNormal < 0.0f && denom > 0.0f) {
                        float e = Mathf::Max(ra ? ra->bounciness : 0.0f,
                                             rb ? rb->bounciness : 0.0f);
                        // Below a small approach speed, drop restitution so bodies
                        // settle (and then sleep) instead of buzzing with micro-bounces
                        //.
                        if (-velAlongNormal < 0.5f) e = 0.0f;
                        jImp = -(1.0f + e) * velAlongNormal / denom;
                        Vec3 impulse = c.normal * jImp;
                        if (ra) { ra->velocity = ra->velocity - impulse * ima; ra->angularVelocity = ra->angularVelocity - Vec3::Cross(rA, impulse) * iia; }
                        if (rb) { rb->velocity = rb->velocity + impulse * imb; rb->angularVelocity = rb->angularVelocity + Vec3::Cross(rB, impulse) * iib; }
                    }
                    // Coulomb friction at the contact point, clamped to friction x the
                    // normal impulse. With gravity re-pressing each step this also gives
                    // resting bodies static-like friction (they stop / stack / hold on
                    // slopes), and the lever arm makes off-center drag spin the body.
                    if (jImp > 0.0f) {
                        Vec3 wa = ra ? ra->angularVelocity : Vec3::Zero;   // post-normal-impulse
                        Vec3 wb = rb ? rb->angularVelocity : Vec3::Zero;
                        va = ra ? ra->velocity : Vec3::Zero;
                        vb = rb ? rb->velocity : Vec3::Zero;
                        Vec3 rv = pointVel(vb, wb, rB) - pointVel(va, wa, rA);
                        Vec3 tang = rv - c.normal * Vec3::Dot(rv, c.normal);
                        float tlen = std::sqrt(Vec3::Dot(tang, tang));
                        if (tlen > 1e-4f) {
                            Vec3 t = tang * (1.0f / tlen);
                            Vec3 rtA = Vec3::Cross(rA, t), rtB = Vec3::Cross(rB, t);
                            float denomT = imSum + rtA.SqrMagnitude() * iia + rtB.SqrMagnitude() * iib;
                            float jt = denomT > 0.0f ? -Vec3::Dot(rv, t) / denomT : 0.0f;
                            float fa = ra ? ra->friction : 0.6f;
                            float fb = rb ? rb->friction : 0.6f;
                            float mu = std::sqrt((fa < 0 ? 0 : fa) * (fb < 0 ? 0 : fb));
                            float maxF = mu * jImp;
                            if (jt > maxF) jt = maxF; else if (jt < -maxF) jt = -maxF;
                            Vec3 fimp = t * jt;
                            if (ra) { ra->velocity = ra->velocity - fimp * ima; ra->angularVelocity = ra->angularVelocity - Vec3::Cross(rA, fimp) * iia; }
                            if (rb) { rb->velocity = rb->velocity + fimp * imb; rb->angularVelocity = rb->angularVelocity + Vec3::Cross(rB, fimp) * iib; }
                        }
                    }
                }
                } // both-asleep guard
            }

            // 4) Fire enter/stay messages.
            bool wasContact = m_contacts.count({a, b}) != 0;
            if (trigger) {
                if (!wasContact) {
                    DispatchTrigger(a, &Component::OnTriggerEnter3D, b, c.point, c.normal, gone);
                    if (!gone.Gone(a) && !gone.Gone(b))
                        DispatchTrigger(b, &Component::OnTriggerEnter3D, a, c.point, -c.normal, gone);
                } else {
                    DispatchTrigger(a, &Component::OnTriggerStay3D, b, c.point, c.normal, gone);
                    if (!gone.Gone(a) && !gone.Gone(b))
                        DispatchTrigger(b, &Component::OnTriggerStay3D, a, c.point, -c.normal, gone);
                }
            } else {
                Collision3D forA = MakeCollision(b, c.normal, c.penetration, c.point, relVelBA);
                Collision3D forB = MakeCollision(a, -c.normal, c.penetration, c.point, -relVelBA);
                if (!wasContact) {
                    DispatchCollision(a->gameObject, &Component::OnCollisionEnter3D, forA, gone);
                    if (!gone.Gone(a) && !gone.Gone(b))
                        DispatchCollision(b->gameObject, &Component::OnCollisionEnter3D, forB, gone);
                } else {
                    DispatchCollision(a->gameObject, &Component::OnCollisionStay3D, forA, gone);
                    if (!gone.Gone(a) && !gone.Gone(b))
                        DispatchCollision(b->gameObject, &Component::OnCollisionStay3D, forB, gone);
                }
            }
        }
    }

    // 5) Fire exit messages for contacts that ended.
    for (const Pair& p : m_contacts) {
        if (current.count(p)) continue;
        Collider3D* a = p.first;
        Collider3D* b = p.second;
        if (!aliveSet.count(a) || !aliveSet.count(b)) continue;
        if (gone.Gone(a) || gone.Gone(b)) continue;
        if (a->isTrigger || b->isTrigger) {
            // No overlap any more: report the midpoint between the two centres.
            Vec3 mid = (a->WorldCenter() + b->WorldCenter()) * 0.5f;
            Vec3 n = (b->WorldCenter() - a->WorldCenter()).Normalized();
            DispatchTrigger(a, &Component::OnTriggerExit3D, b, mid, n, gone);
            if (!gone.Gone(a) && !gone.Gone(b))
                DispatchTrigger(b, &Component::OnTriggerExit3D, a, mid, -n, gone);
        } else {
            Collision3D forA = MakeCollision(b, Vec3::Zero, 0.0f, Vec3::Zero, Vec3::Zero);
            Collision3D forB = MakeCollision(a, Vec3::Zero, 0.0f, Vec3::Zero, Vec3::Zero);
            DispatchCollision(a->gameObject, &Component::OnCollisionExit3D, forA, gone);
            if (!gone.Gone(a) && !gone.Gone(b))
                DispatchCollision(b->gameObject, &Component::OnCollisionExit3D, forB, gone);
        }
    }

    // Drop pairs whose collider a callback removed, so no freed pointer is kept.
    if (gone.Any())
        for (auto it = current.begin(); it != current.end();)
            it = (gone.Gone(it->first) || gone.Gone(it->second)) ? current.erase(it) : std::next(it);
    m_contacts.swap(current);

    // 4) Terrain ground-follow: heightmap terrain isn't a polygon collider, so make
    // dynamic bodies stand on its surface directly — including craters/hills carved
    // by the Terrain Digger. Each body samples the terrain height under it and is
    // lifted to rest its collider's underside on the surface, grounding downward
    // motion. Cheap (one bilinear height sample per body per terrain it's over).
    // Clear the per-frame terrain-grounded flag on every body first, so both the
    // heightmap and the voxel pass below can set it (and it falls back to false
    // when a body is airborne over either kind of terrain).
    for (Rigidbody3D* rb : scene.FindObjectsOfType<Rigidbody3D>()) {
        rb->groundedOnTerrain = false;
        rb->groundNormal = Vec3{0.0f, 1.0f, 0.0f};
    }

    auto terrains = scene.FindObjectsOfType<Terrain>();
    if (!terrains.empty()) {
        for (Rigidbody3D* rb : scene.FindObjectsOfType<Rigidbody3D>()) {
            if (!rb->enabled || !rb->gameObject || !rb->gameObject->active) continue;
            if (rb->bodyType == Rigidbody3D::BodyType::Static) continue;
            Transform* t = rb->transform;
            if (!t) continue;
            Vec3 pos = t->Position();
            // How far the body extends below its origin (so its feet, not its centre,
            // sit on the ground). From its collider AABB, else a small default.
            float foot = 0.5f;
            if (auto* col = rb->gameObject->GetComponent<Collider3D>()) {
                Vec3 mn, mx; col->WorldAABB(mn, mx); foot = pos.y - mn.y;
                if (foot < 0.0f) foot = 0.0f;
            }
            for (Terrain* terr : terrains) {
                if (!terr->gameObject || !terr->gameObject->transform) continue;
                // Work in the terrain's LOCAL space so a scaled (or rotated)
                // terrain supports bodies across its whole VISIBLE footprint —
                // the old world-offset math ignored the transform, so scaling a
                // terrain up let players walk past the unscaled square and fall
                // through ground that looked perfectly solid.
                Mat4 l2w = terr->gameObject->transform->LocalToWorldMatrix();
                Vec3 lp = l2w.Inverse().MultiplyPoint(pos);
                float lx = lp.x, lz = lp.z;
                float half = terr->size * 0.5f;
                if (lx < -half || lx > half || lz < -half || lz > half) continue;
                float targetY = l2w.MultiplyPoint({lx, terr->SampleHeight(lx, lz), lz}).y + foot;
                // Resting on (or just above) the surface counts as grounded, so a
                // player standing on terrain can jump repeatedly. A small skin
                // tolerance avoids flicker from the per-frame gravity nudge.
                if (pos.y <= targetY + 0.05f) {
                    rb->groundedOnTerrain = true;
                    rb->groundNormal = l2w.MultiplyVector(terr->NormalAt(lx, lz)).Normalized();
                } else if (rb->sleeping && pos.y > targetY + 0.1f) {
                    // The ground under a resting body was lowered (e.g. the Terrain
                    // Digger carved a crater): wake it so gravity pulls it down into
                    // the new hole instead of leaving it floating where it slept.
                    rb->WakeUp();
                }
                if (pos.y < targetY) {                      // sank into the ground -> lift out
                    t->localPosition.y += (targetY - pos.y);

                    // Slope-aware response: resolve velocity against the terrain
                    // surface normal instead of just zeroing Y. This lets bodies
                    // slide down hills, bounce off (restitution) and lose tangential
                    // speed to friction — so debris tumbles and rolls realistically
                    // instead of sticking flat where it lands.
                    Vec3 n = l2w.MultiplyVector(terr->NormalAt(lx, lz)).Normalized();   // surface normal, world space
                    Vec3& v = rb->velocity;
                    float vn = v.x * n.x + v.y * n.y + v.z * n.z;   // into-surface component
                    if (vn < 0.0f) {
                        float rest = rb->bounciness;        // 0 = no bounce
                        // Remove the normal component, then add it back scaled by
                        // restitution (a bounce). Tangential part stays (sliding).
                        v.x -= vn * n.x * (1.0f + rest);
                        v.y -= vn * n.y * (1.0f + rest);
                        v.z -= vn * n.z * (1.0f + rest);
                        // Kinetic friction on the remaining tangential velocity.
                        const float friction = 0.18f;
                        v.x *= (1.0f - friction);
                        v.z *= (1.0f - friction);
                    }
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
    auto voxels = scene.FindObjectsOfType<VoxelTerrain>();
    if (!voxels.empty()) {
        for (Rigidbody3D* rb : scene.FindObjectsOfType<Rigidbody3D>()) {
            if (!rb->enabled || !rb->gameObject || !rb->gameObject->active) continue;
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
                                t->localPosition.y += (yy + radius) - footCurY;  // sit the foot on it
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
                        t->localPosition.x += n.x * pen; // push the whole body out
                        t->localPosition.y += n.y * pen;
                        t->localPosition.z += n.z * pen;
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

    // 6) Mesh collider collision: a MeshCollider3D collides EXACTLY against its
    // object's triangles (walls, floors, ramps you modeled or extruded), not as a
    // loose AABB box. Each mesh collider is treated as static world geometry; every
    // dynamic body (approximated as a vertical stack of probe spheres, feet..head)
    // is depenetrated out of any triangle it overlaps — so you stand on modeled
    // floors and are blocked by modeled walls instead of walking through them.
    {
        struct MeshShape { std::vector<Vec3> tri; };   // world-space triangles (3 verts each)
        std::vector<MeshShape> meshes;
        for (Collider3D* col : scene.FindObjectsOfType<Collider3D>()) {
            if (!Alive(col) || col->shape() != Collider3D::Shape::Mesh) continue;
            if (!col->gameObject || !col->gameObject->transform) continue;
            auto* mr = col->gameObject->GetComponent<MeshRenderer>();
            if (!mr || mr->DisplayMesh().triangles.empty()) continue;
            Mat4 M = col->gameObject->transform->LocalToWorldMatrix();
            MeshShape ms; ms.tri.reserve(mr->DisplayMesh().triangles.size());
            for (int idx : mr->DisplayMesh().triangles)
                if (idx >= 0 && idx < (int)mr->DisplayMesh().vertices.size())
                    ms.tri.push_back(M.MultiplyPoint(mr->DisplayMesh().vertices[idx]));
            if (!ms.tri.empty()) meshes.push_back(std::move(ms));
        }
        if (!meshes.empty()) {
            for (Rigidbody3D* rb : scene.FindObjectsOfType<Rigidbody3D>()) {
                if (!rb->enabled || !rb->gameObject || !rb->gameObject->active) continue;
                if (rb->bodyType == Rigidbody3D::BodyType::Static) continue;
                Collider3D* self = rb->gameObject->GetComponent<Collider3D>();
                if (self && self->shape() == Collider3D::Shape::Mesh) continue;  // don't self-collide
                Transform* t = rb->transform; if (!t) continue;
                // Probe spheres spanning the body's collider AABB (feet to head).
                float radius = 0.35f, footY = 0.0f, headY = 1.6f;
                if (self) {
                    Vec3 mn, mx; self->WorldAABB(mn, mx);
                    Vec3 pos = t->Position();
                    radius = std::min(mx.x - mn.x, mx.z - mn.z) * 0.5f;
                    if (radius < 0.05f) radius = 0.05f;
                    footY = (mn.y + radius) - pos.y;
                    headY = (mx.y - radius) - pos.y;
                }
                if (headY < footY) headY = footY;
                int spheres = (int)std::ceil((headY - footY) / std::max(radius, 0.01f)) + 1;
                if (spheres < 2) spheres = 2; if (spheres > 12) spheres = 12;

                for (const MeshShape& ms : meshes) {
                    for (int si = 0; si < spheres; ++si) {
                        float fy = footY + (headY - footY) * (float)si / (float)(spheres - 1);
                        for (int iter = 0; iter < 4; ++iter) {
                            Vec3 pos = t->Position();
                            Vec3 c{pos.x, pos.y + fy, pos.z};
                            float bestPen = 0.0f; Vec3 bestN{0, 0, 0}; bool any = false;
                            for (std::size_t ti = 0; ti + 2 < ms.tri.size(); ti += 3) {
                                Vec3 cp = ClosestPointOnTri(c, ms.tri[ti], ms.tri[ti + 1], ms.tri[ti + 2]);
                                Vec3 d = c - cp; float d2 = d.SqrMagnitude();
                                if (d2 >= radius * radius) continue;
                                float dist = std::sqrt(std::max(d2, 1e-12f));
                                float pen = radius - dist;
                                if (pen > bestPen) {
                                    bestPen = pen; any = true;
                                    bestN = dist > 1e-6f ? d * (1.0f / dist)
                                          : Vec3::Cross(ms.tri[ti + 1] - ms.tri[ti], ms.tri[ti + 2] - ms.tri[ti]).Normalized();
                                }
                            }
                            if (!any) break;
                            t->localPosition.x += bestN.x * bestPen;
                            t->localPosition.y += bestN.y * bestPen;
                            t->localPosition.z += bestN.z * bestPen;
                            Vec3& v = rb->velocity;
                            float vn = v.x * bestN.x + v.y * bestN.y + v.z * bestN.z;
                            if (vn < 0.0f) {
                                float rest = rb->bounciness;
                                v.x -= vn * bestN.x * (1.0f + rest);
                                v.y -= vn * bestN.y * (1.0f + rest);
                                v.z -= vn * bestN.z * (1.0f + rest);
                            }
                            if (bestN.y > 0.4f) rb->groundedOnTerrain = true;   // standing on it
                        }
                    }
                }
            }
        }
    }

    // ---- Joints: positional constraints (after collisions) ----
    for (Joint3D* j : scene.FindObjectsOfType<Joint3D>()) {
        if (j->broken || !j->gameObject || !j->transform) continue;
        Rigidbody3D* ra = j->gameObject->GetComponent<Rigidbody3D>();
        if (!ra) continue;                          // the joint moves THIS body
        Transform* ta = j->transform;
        Rigidbody3D* rbB = nullptr; Transform* tb = nullptr;
        if (!j->connectedBody.empty())
            if (GameObject* g = scene.Find(j->connectedBody)) { tb = g->transform; rbB = g->GetComponent<Rigidbody3D>(); }
        Vec3 pa = ta->Position();
        Vec3 pb = tb ? tb->Position() : j->anchor;
        if (!j->initialized) {
            j->pinOffset = pa - pb;
            j->restLen = j->autoConfigure ? std::sqrt(Vec3::Dot(pa - pb, pa - pb)) : j->distance;
            j->hingeLever = pb - pa;                     // world lever COM_A -> pivot, at init
            j->refRot = ta->localRotation;
            j->initialized = true;
        }
        float imA = ra->InvMass();
        float imB = rbB ? rbB->InvMass() : 0.0f;
        float imSum = imA + imB;
        if (imSum <= 0.0f) continue;
        Joint3D::Mode m = (Joint3D::Mode)j->mode;

        if (m == Joint3D::Mode::Pin) {
            Vec3 target = pb + j->pinOffset;
            Vec3 err = target - pa;
            ta->localPosition = ta->localPosition + err * (imA / imSum);
            if (tb && rbB && imB > 0.0f) tb->localPosition = tb->localPosition - err * (imB / imSum);
            ra->velocity = rbB ? rbB->velocity : Vec3::Zero;   // weld: follow B (anchor -> freeze)
            if (j->breakable && std::sqrt(Vec3::Dot(err, err)) > j->breakForce) j->broken = true;
        } else if (m == Joint3D::Mode::Hinge) {
            // Revolute joint: pin a material point of A to the pivot and lock rotation
            // to `axis`, free to spin about it. (B supplies the pivot reference.)
            if (imA <= 0.0f) continue;
            Quat qDelta = ta->localRotation * j->refRot.Inverse();
            Vec3 rA = qDelta * j->hingeLever;            // lever rotated with the body
            Vec3 anchorWorld = pa + rA;
            float iiA = InvInertia(ra, ra->gameObject->GetComponent<Collider3D>());
            Vec3 axis = j->axis.Normalized();
            Vec3 vB = rbB ? rbB->velocity : Vec3::Zero;

            // 1) Point velocity constraint: cancel A's anchor-point velocity (vs B).
            // K = (imA + iiA|rA|²)I - iiA (rA⊗rA); inverted via Sherman-Morrison.
            Vec3 vAnchor = ra->velocity + Vec3::Cross(ra->angularVelocity, rA) - vB;
            float a = imA + iiA * rA.SqrMagnitude();
            if (a > 1e-9f) {
                Vec3 P = (vAnchor + rA * (iiA / imA * Vec3::Dot(rA, vAnchor))) * (-1.0f / a);
                ra->velocity = ra->velocity + P * imA;
                ra->angularVelocity = ra->angularVelocity + Vec3::Cross(rA, P) * iiA;
            }
            // 2) Axis lock: keep only the spin component along the hinge axis.
            Vec3 w = ra->angularVelocity;
            ra->angularVelocity = axis * Vec3::Dot(w, axis);
            // 3) Motor: drive the spin about the axis toward motorSpeed.
            if (j->useMotor && iiA > 0.0f) {
                float cur = Vec3::Dot(ra->angularVelocity, axis);
                float Cdot = cur - j->motorSpeed * Mathf::Deg2Rad;
                float imp = Mathf::Clamp(-Cdot / iiA, -j->maxMotorTorque * dt, j->maxMotorTorque * dt);
                ra->angularVelocity = ra->angularVelocity + axis * (imp * iiA);
            }
            // 4) Positional correction: pull A's anchor point back onto the pivot.
            Vec3 Cpos = anchorWorld - pb;
            ta->localPosition = ta->localPosition - Cpos;
            if (j->breakable && std::sqrt(Vec3::Dot(Cpos, Cpos)) > j->breakForce) j->broken = true;
        } else {
            Vec3 d = pb - pa; float len = std::sqrt(Vec3::Dot(d, d));
            Vec3 n = len > 1e-5f ? d * (1.0f / len) : Vec3{0, 1, 0};
            float C = len - j->restLen;                 // +stretched / -compressed
            Vec3 va = ra->velocity, vb = rbB ? rbB->velocity : Vec3::Zero;
            float vrelN = Vec3::Dot(vb - va, n);
            if (m == Joint3D::Mode::Spring) {
                float f = j->spring * C + j->damper * vrelN;   // restore + damp, along n (A->B)
                Vec3 imp = n * (f * dt);
                ra->velocity = ra->velocity + imp * imA;
                if (rbB && imB > 0.0f) rbB->velocity = rbB->velocity - imp * imB;
            } else {                                    // Distance (rigid rod)
                float jImp = -vrelN / imSum;
                Vec3 imp = n * jImp;
                ra->velocity = ra->velocity - imp * imA;
                if (rbB && imB > 0.0f) rbB->velocity = rbB->velocity + imp * imB;
                Vec3 corr = n * C;                       // restore the rest length
                ta->localPosition = ta->localPosition + corr * (imA / imSum);
                if (tb && rbB && imB > 0.0f) tb->localPosition = tb->localPosition - corr * (imB / imSum);
            }
            if (j->breakable && std::fabs(C) > j->breakForce) j->broken = true;
        }
    }

    // Sleep bookkeeping: a dynamic body under the speed thresholds for kSleepTime
    // seconds sleeps (zeroed and skipped next step). Any motion resets the timer.
    for (Rigidbody3D* rb : bodies) {
        if (gone.Gone(rb)) continue;                 // removed by a contact callback
        if (!rb->enabled || !rb->gameObject || !rb->gameObject->active) continue;
        if (rb->bodyType != Rigidbody3D::BodyType::Dynamic) continue;
        if (!rb->allowSleep) { rb->WakeUp(); continue; }
        if (rb->sleeping) continue;
        if (rb->velocity.SqrMagnitude() < kLinSleep * kLinSleep &&
            rb->angularVelocity.SqrMagnitude() < kAngSleep * kAngSleep) {
            rb->m_sleepTimer += dt;
            if (rb->m_sleepTimer >= kSleepTime) {
                rb->sleeping = true;
                rb->velocity = Vec3::Zero;
                rb->angularVelocity = Vec3::Zero;
            }
        } else {
            rb->m_sleepTimer = 0.0f;
        }
    }

    // Record where every dynamic body ended up, for next step's swept collision.
    for (Rigidbody3D* rb : scene.FindObjectsOfType<Rigidbody3D>()) {
        if (rb->bodyType == Rigidbody3D::BodyType::Static || !rb->transform) continue;
        rb->prevPos = rb->transform->Position();
        rb->hasPrevPos = true;
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
    if (c->shape() == Collider3D::Shape::Sphere) {
        auto* s = static_cast<SphereCollider3D*>(c);
        return RaySphere(o, d, s->WorldCenter(), s->WorldRadius(), maxT, t, n);
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
    if (!tg || !tg->active || !tg->transform) return false;
    if (ignore && tg == ignore) return false;
    return ((mask >> (tg->layer & 31)) & 1u) != 0;
}

} // namespace

RaycastHit3D Physics3D::Raycast(Scene& scene, const Vec3& origin, const Vec3& direction,
                                float maxDistance, GameObject* ignore,
                                std::uint32_t layerMask, bool queryTriggers) {
    RaycastHit3D best;
    best.distance = maxDistance;
    Vec3 dir = direction.Normalized();
    for (Collider3D* c : scene.FindObjectsOfType<Collider3D>()) {
        if (!Passes(c, ignore, layerMask, queryTriggers)) continue;
        float t; Vec3 n;
        if (RayCollider(c, origin, dir, best.distance, t, n) && t <= best.distance) {
            best.hit = true; best.collider = c; best.gameObject = c->gameObject;
            best.distance = t; best.point = origin + dir * t; best.normal = n;
        }
    }
    for (Terrain* terr : scene.FindObjectsOfType<Terrain>()) {
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
    for (Collider3D* c : scene.FindObjectsOfType<Collider3D>()) {
        if (!Passes(c, ignore, layerMask, queryTriggers)) continue;
        float t; Vec3 n;
        if (RayCollider(c, origin, dir, maxDistance, t, n)) add(c, c->gameObject, t, n);
    }
    for (Terrain* terr : scene.FindObjectsOfType<Terrain>()) {
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
                   bool skipStartOverlaps, bool skipTriMeshes = false) {
    RaycastHit3D best;
    struct Cand { Collider3D* c; float t0, t1; };
    std::vector<Cand> cands;
    const Vec3 o = q.Center(), ext = q.Extent();
    float lo = 1e30f, hi = -1e30f;
    for (Collider3D* c : scene.FindObjectsOfType<Collider3D>()) {
        if (!Passes(c, ignore, mask, queryTriggers)) continue;
        if (skipTriMeshes && MeshHasTris(c)) continue;
        Vec3 n, p;
        if (skipStartOverlaps && ShapeTouch(c, q, n, p)) continue;
        Vec3 mn, mx; c->WorldAABB(mn, mx);
        float t0, t1;
        if (!RaySlabs(o, dir, mn - ext, mx + ext, t0, t1)) continue;
        if (t1 < 0.0f || t0 > maxDist) continue;
        t0 = Mathf::Max(t0, 0.0f); t1 = Mathf::Min(t1, maxDist);
        cands.push_back({c, t0, t1});
        lo = Mathf::Min(lo, t0); hi = Mathf::Max(hi, t1);
    }
    if (cands.empty()) return best;
    if (dir.SqrMagnitude() < 1e-12f) hi = lo;   // no direction: just test where it stands
    float step = Mathf::Max(q.Thinnest() * 0.5f, 0.02f);
    if ((hi - lo) / step > 20000.0f) step = (hi - lo) / 20000.0f;   // bound the work
    float prev = lo;
    for (float t = lo; ; t += step) {
        bool last = t >= hi;
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

void Physics3D::SweepBody(Scene& scene, Rigidbody3D* rb, float dt) {
    Transform* t = rb->transform;
    Vec3 motion = rb->velocity * dt;
    float len = motion.Magnitude();
    Collider3D* self = rb->gameObject->GetComponent<Collider3D>();
    if (!self || !Alive(self) || self->isTrigger || len < 1e-6f) {
        t->localPosition = t->localPosition + motion;
        return;
    }
    // The body as the sphere that fits inside its bounds: a slow body (moving
    // less than half that per step) can't skip a wall, so it moves normally.
    Vec3 mn, mx; self->WorldAABB(mn, mx);
    float r = 0.5f * Mathf::Min(mx.x - mn.x, Mathf::Min(mx.y - mn.y, mx.z - mn.z));
    if (r < 0.01f) r = 0.01f;
    if (len <= r * 0.5f) { t->localPosition = t->localPosition + motion; return; }
    Vec3 dir = motion * (1.0f / len);
    RaycastHit3D h = Sweep(scene, SphereShape(self->WorldCenter(), r), dir, len, rb->gameObject,
                           m_layerMask[self->layer & 31], /*queryTriggers*/ false,
                           /*skipStartOverlaps*/ true, /*skipTriMeshes*/ true);
    float travel = len;
    // Stop just inside the surface so this step's narrow phase sees the contact
    // and bounces / stops the body as usual.
    if (h.hit) travel = Mathf::Min(len, h.distance + Mathf::Min(0.02f, r * 0.2f));
    t->localPosition = t->localPosition + dir * travel;
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
            // Capsule / cylinder (via its world AABB): push out of the box surface.
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
