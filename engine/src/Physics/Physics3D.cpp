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

Contact TestBoxBox(const Vec3& ca, const Vec3& ha, const Vec3& cb, const Vec3& hb) {
    Contact c;
    Vec3 d = cb - ca;
    float ox = (ha.x + hb.x) - Mathf::Abs(d.x); if (ox <= 0) return c;
    float oy = (ha.y + hb.y) - Mathf::Abs(d.y); if (oy <= 0) return c;
    float oz = (ha.z + hb.z) - Mathf::Abs(d.z); if (oz <= 0) return c;
    c.hit = true;
    // Minimum translation axis.
    if (ox <= oy && ox <= oz)      { c.normal = {d.x < 0 ? -1.0f : 1.0f, 0, 0}; c.penetration = ox; }
    else if (oy <= ox && oy <= oz) { c.normal = {0, d.y < 0 ? -1.0f : 1.0f, 0}; c.penetration = oy; }
    else                           { c.normal = {0, 0, d.z < 0 ? -1.0f : 1.0f}; c.penetration = oz; }
    // Contact point: clamp each center into the other box and split the difference.
    Vec3 pA = ClampVec(cb, ca - ha, ca + ha);
    Vec3 pB = ClampVec(ca, cb - hb, cb + hb);
    c.point = (pA + pB) * 0.5f;
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

Contact TestColliders(Collider3D* a, Collider3D* b) {
    using S = Collider3D::Shape;
    S sa = a->shape(), sb = b->shape();

    // A mesh collider that actually has geometry is resolved exactly (per triangle)
    // in a dedicated pass — skip it here so it doesn't ALSO over-block as a big AABB
    // box. A mesh collider with no MeshRenderer keeps the simple box behaviour.
    auto meshHasTris = [](Collider3D* col) {
        if (col->shape() != S::Mesh || !col->gameObject) return false;
        auto* mr = col->gameObject->GetComponent<MeshRenderer>();
        return mr && !mr->mesh.triangles.empty();
    };
    if (meshHasTris(a) || meshHasTris(b)) return {};

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
        auto* ba = static_cast<BoxCollider3D*>(a);
        auto* bb = static_cast<BoxCollider3D*>(b);
        return TestBoxBox(ba->WorldCenter(), ba->HalfExtents(),
                          bb->WorldCenter(), bb->HalfExtents());
    }
    if (aBox) { // A box, B sphere/capsule
        auto* box = static_cast<BoxCollider3D*>(a);
        Vec3 sc; float sr; AsSphere(b, box->WorldCenter(), sc, sr);
        return TestBoxSphere(box->WorldCenter(), box->HalfExtents(), sc, sr);
    }
    if (bBox) { // A sphere/capsule, B box
        auto* box = static_cast<BoxCollider3D*>(b);
        Vec3 sc; float sr; AsSphere(a, box->WorldCenter(), sc, sr);
        Contact c = TestBoxSphere(box->WorldCenter(), box->HalfExtents(), sc, sr);
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

void DispatchCollision(GameObject* go, void (Component::*fn)(const Collision3D&),
                       const Collision3D& info) {
    for (Component* c : go->GetComponents<Component>()) (c->*fn)(info);
}
void DispatchTrigger(GameObject* go, void (Component::*fn)(Collider3D*), Collider3D* other) {
    for (Component* c : go->GetComponents<Component>()) (c->*fn)(other);
}

bool Alive(Collider3D* c) { return c->enabled && c->gameObject && c->gameObject->active; }

} // namespace

void Physics3D::Step(Scene& scene, float dt) {
    if (dt <= 0.0f) return;

    // 1) Integrate dynamic / kinematic bodies.
    for (Rigidbody3D* rb : scene.FindObjectsOfType<Rigidbody3D>()) {
        if (!rb->enabled || !rb->gameObject || !rb->gameObject->active) continue;
        Transform* t = rb->transform;
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
            t->localPosition = t->localPosition + rb->velocity * dt;
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

    // Precompute each collider's world AABB once per step (it was recomputed O(n)
    // times per collider inside the pair loop) and a fast membership set used by the
    // exit-message cleanup below (was an O(contacts*colliders) linear scan).
    const std::size_t nc = colliders.size();
    std::vector<Vec3> aabbMin(nc), aabbMax(nc);
    std::unordered_set<Collider3D*> aliveSet;
    aliveSet.reserve(nc * 2 + 1);
    for (std::size_t i = 0; i < nc; ++i) {
        colliders[i]->WorldAABB(aabbMin[i], aabbMax[i]);
        aliveSet.insert(colliders[i]);
    }

    for (std::size_t i = 0; i < nc; ++i) {
        for (std::size_t j = i + 1; j < nc; ++j) {
            Collider3D* a = colliders[i];
            Collider3D* b = colliders[j];
            if (!a->enabled || !b->enabled) continue;
            if (!a->gameObject->active || !b->gameObject->active) continue;
            if (a->gameObject == b->gameObject) continue;
            if (!LayersCollide(a->layer, b->layer)) continue;

            // Broad phase: AABB reject (precomputed AABBs).
            const Vec3 &aMin = aabbMin[i], &aMax = aabbMax[i], &bMin = aabbMin[j], &bMax = aabbMax[j];
            if (aMax.x < bMin.x || aMin.x > bMax.x ||
                aMax.y < bMin.y || aMin.y > bMax.y ||
                aMax.z < bMin.z || aMin.z > bMax.z) continue;

            Contact c = TestColliders(a, b);
            if (!c.hit) continue;

            current.insert({a, b});

            Rigidbody3D* ra = a->gameObject->GetComponent<Rigidbody3D>();
            Rigidbody3D* rb = b->gameObject->GetComponent<Rigidbody3D>();
            bool trigger = a->isTrigger || b->isTrigger;

            // 3) Resolve solids (skip triggers and pairs without dynamics).
            if (!trigger) {
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
            }

            // 4) Fire enter/stay messages.
            bool wasContact = m_contacts.count({a, b}) != 0;
            if (trigger) {
                if (!wasContact) {
                    DispatchTrigger(a->gameObject, &Component::OnTriggerEnter3D, b);
                    DispatchTrigger(b->gameObject, &Component::OnTriggerEnter3D, a);
                } else {
                    DispatchTrigger(a->gameObject, &Component::OnTriggerStay3D, b);
                    DispatchTrigger(b->gameObject, &Component::OnTriggerStay3D, a);
                }
            } else {
                Collision3D forA{b->gameObject, b, c.normal, c.penetration};
                Collision3D forB{a->gameObject, a, -c.normal, c.penetration};
                if (!wasContact) {
                    DispatchCollision(a->gameObject, &Component::OnCollisionEnter3D, forA);
                    DispatchCollision(b->gameObject, &Component::OnCollisionEnter3D, forB);
                } else {
                    DispatchCollision(a->gameObject, &Component::OnCollisionStay3D, forA);
                    DispatchCollision(b->gameObject, &Component::OnCollisionStay3D, forB);
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
        if (a->isTrigger || b->isTrigger) {
            DispatchTrigger(a->gameObject, &Component::OnTriggerExit3D, b);
            DispatchTrigger(b->gameObject, &Component::OnTriggerExit3D, a);
        } else {
            Collision3D forA{b->gameObject, b, {0, 0, 0}, 0.0f};
            Collision3D forB{a->gameObject, a, {0, 0, 0}, 0.0f};
            DispatchCollision(a->gameObject, &Component::OnCollisionExit3D, forA);
            DispatchCollision(b->gameObject, &Component::OnCollisionExit3D, forB);
        }
    }

    m_contacts.swap(current);

    // 4) Terrain ground-follow: heightmap terrain isn't a polygon collider, so make
    // dynamic bodies stand on its surface directly — including craters/hills carved
    // by the Terrain Digger. Each body samples the terrain height under it and is
    // lifted to rest its collider's underside on the surface, grounding downward
    // motion. Cheap (one bilinear height sample per body per terrain it's over).
    // Clear the per-frame terrain-grounded flag on every body first, so both the
    // heightmap and the voxel pass below can set it (and it falls back to false
    // when a body is airborne over either kind of terrain).
    for (Rigidbody3D* rb : scene.FindObjectsOfType<Rigidbody3D>()) rb->groundedOnTerrain = false;

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
                Vec3 tp = terr->gameObject->transform->Position();
                float lx = pos.x - tp.x, lz = pos.z - tp.z;
                float half = terr->size * 0.5f;
                if (lx < -half || lx > half || lz < -half || lz > half) continue;
                float targetY = tp.y + terr->SampleHeight(lx, lz) + foot;
                // Resting on (or just above) the surface counts as grounded, so a
                // player standing on terrain can jump repeatedly. A small skin
                // tolerance avoids flicker from the per-frame gravity nudge.
                if (pos.y <= targetY + 0.05f) rb->groundedOnTerrain = true;
                if (pos.y < targetY) {                      // sank into the ground -> lift out
                    t->localPosition.y += (targetY - pos.y);

                    // Slope-aware response: resolve velocity against the terrain
                    // surface normal instead of just zeroing Y. This lets bodies
                    // slide down hills, bounce off (restitution) and lose tangential
                    // speed to friction — so debris tumbles and rolls realistically
                    // instead of sticking flat where it lands.
                    Vec3 n = terr->NormalAt(lx, lz);        // unit surface normal (Y up)
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
            if (!mr || mr->mesh.triangles.empty()) continue;
            Mat4 M = col->gameObject->transform->LocalToWorldMatrix();
            MeshShape ms; ms.tri.reserve(mr->mesh.triangles.size());
            for (int idx : mr->mesh.triangles)
                if (idx >= 0 && idx < (int)mr->mesh.vertices.size())
                    ms.tri.push_back(M.MultiplyPoint(mr->mesh.vertices[idx]));
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
            if (tb && rbB) tb->localPosition = tb->localPosition - err * (imB / imSum);
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
                if (rbB) rbB->velocity = rbB->velocity - imp * imB;
            } else {                                    // Distance (rigid rod)
                float jImp = -vrelN / imSum;
                Vec3 imp = n * jImp;
                ra->velocity = ra->velocity - imp * imA;
                if (rbB) rbB->velocity = rbB->velocity + imp * imB;
                Vec3 corr = n * C;                       // restore the rest length
                ta->localPosition = ta->localPosition + corr * (imA / imSum);
                if (tb && rbB) tb->localPosition = tb->localPosition - corr * (imB / imSum);
            }
            if (j->breakable && std::fabs(C) > j->breakForce) j->broken = true;
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

} // namespace

RaycastHit3D Physics3D::Raycast(Scene& scene, const Vec3& origin, const Vec3& direction,
                                float maxDistance, GameObject* ignore) {
    RaycastHit3D best;
    best.distance = maxDistance;
    Vec3 dir = direction.Normalized();
    for (Collider3D* c : scene.FindObjectsOfType<Collider3D>()) {
        if (!Alive(c)) continue;
        if (ignore && c->gameObject == ignore) continue;   // skip the caller (e.g. player)
        float t; Vec3 n; bool hit = false;
        if (c->shape() == Collider3D::Shape::Sphere) {
            auto* s = static_cast<SphereCollider3D*>(c);
            hit = RaySphere(origin, dir, s->WorldCenter(), s->WorldRadius(), best.distance, t, n);
        } else { // Box, or capsule via its AABB
            Vec3 mn, mx; c->WorldAABB(mn, mx);
            hit = RayAABB(origin, dir, mn, mx, best.distance, t, n);
        }
        if (hit && t <= best.distance) {
            best.hit = true; best.collider = c; best.gameObject = c->gameObject;
            best.distance = t; best.point = origin + dir * t; best.normal = n;
        }
    }
    return best;
}

std::vector<Collider3D*> Physics3D::OverlapSphere(Scene& scene, const Vec3& center, float radius) {
    std::vector<Collider3D*> out;
    for (Collider3D* c : scene.FindObjectsOfType<Collider3D>()) {
        if (!Alive(c)) continue;
        bool hit = false;
        if (IsBoxLike(c->shape())) {
            Vec3 mn, mx; c->WorldAABB(mn, mx);
            hit = (ClampVec(center, mn, mx) - center).Magnitude() <= radius;
        } else {
            Vec3 sc; float sr; AsSphere(c, center, sc, sr);
            hit = (sc - center).Magnitude() <= radius + sr;
        }
        if (hit) out.push_back(c);
    }
    return out;
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
            // Box / capsule (via its world AABB): push out of the box surface.
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
