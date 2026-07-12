#include "okay/Physics/Physics2D.hpp"
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Physics/Collider2D.hpp"
#include "okay/Components/Joint2D.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Mathf.hpp"

#include <vector>
#include <unordered_set>

namespace okay {

namespace {

struct Contact {
    bool  hit = false;
    Vec2  normal{0, 0};   // points from A toward B
    float penetration = 0.0f;
    Vec2  point{0, 0};    // approximate world contact point (for angular response)
};

// Inverse moment of inertia about Z for a body+collider. 0 when rotation is
// locked, mass is infinite, or there's no collider to size the inertia from.
float InvInertia(Rigidbody2D* rb, Collider2D* col) {
    if (!rb || rb->freezeRotation || !col) return 0.0f;
    float im = rb->InvMass();
    if (im <= 0.0f) return 0.0f;
    float mass = rb->mass, I;
    if (col->shape() == Collider2D::Shape::Box) {
        Vec2 he = static_cast<BoxCollider2D*>(col)->HalfExtents();
        float w = he.x * 2.0f, h = he.y * 2.0f;
        I = mass * (w * w + h * h) / 12.0f;
    } else if (col->shape() == Collider2D::Shape::Circle) {
        float r = static_cast<CircleCollider2D*>(col)->WorldRadius();
        I = mass * r * r * 0.5f;
    } else {
        Vec2 mn, mx; col->WorldAABB(mn, mx);
        float w = mx.x - mn.x, h = mx.y - mn.y;
        I = mass * (w * w + h * h) / 12.0f;
    }
    return I > 1e-8f ? 1.0f / I : 0.0f;
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

// Should this contact be skipped because it's a one-way edge and the other body
// is approaching from the pass-through side? Returns true to let it pass.
bool OneWayPassThrough(Collider2D* a, Collider2D* b) {
    for (int k = 0; k < 2; ++k) {
        Collider2D* ec = k == 0 ? a : b;
        Collider2D* other = k == 0 ? b : a;
        if (ec->shape() != Collider2D::Shape::Edge) continue;
        auto* e = static_cast<EdgeCollider2D*>(ec);
        if (!e->oneWay) continue;
        Vec2 oc = other->WorldCenter();
        Vec2 cp = ClosestOnPoly(e->WorldPoints(), false, oc);
        Vec2 n = e->oneWayNormal.Normalized();
        if (Vec2::Dot(oc - cp, n) < 0.0f) return true;   // body is below the platform
    }
    return false;
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

Contact TestColliders(Collider2D* a, Collider2D* b) {
    using S = Collider2D::Shape;
    S sa = a->shape(), sb = b->shape();

    // Capsule vs capsule: closest points between the two segments.
    if (sa == S::Capsule && sb == S::Capsule) {
        auto* ca = static_cast<CapsuleCollider2D*>(a);
        auto* cb = static_cast<CapsuleCollider2D*>(b);
        Vec2 a0, a1, b0, b1; ca->Segment(a0, a1); cb->Segment(b0, b1);
        Vec2 pa, pb; ClosestSegSeg(a0, a1, b0, b1, pa, pb);
        return TestCircleCircle(pa, ca->WorldRadius(), pb, cb->WorldRadius());
    }

    bool aBox = sa == S::Box, bBox = sb == S::Box;
    if (aBox && bBox) {
        return TestOBBOBB(MakeOBB(static_cast<BoxCollider2D*>(a)),
                          MakeOBB(static_cast<BoxCollider2D*>(b)));
    }
    if (aBox) { // A box, B circle/capsule
        auto* box = static_cast<BoxCollider2D*>(a);
        Vec2 cc; float r; AsCircle(b, box->WorldCenter(), cc, r);
        return TestOBBCircle(MakeOBB(box), cc, r);
    }
    if (bBox) { // A circle/capsule, B box
        auto* box = static_cast<BoxCollider2D*>(b);
        Vec2 cc; float r; AsCircle(a, box->WorldCenter(), cc, r);
        Contact c = TestOBBCircle(MakeOBB(box), cc, r);
        c.normal = c.normal * -1.0f; // flip to point from A toward B
        return c;
    }
    // Circle/capsule vs circle/capsule.
    Vec2 ac, bc; float ar, br;
    AsCircle(a, b->WorldCenter(), ac, ar);
    AsCircle(b, a->WorldCenter(), bc, br);
    return TestCircleCircle(ac, ar, bc, br);
}

void DispatchCollision(GameObject* go, void (Component::*fn)(const Collision2D&),
                       const Collision2D& info) {
    for (Component* c : go->GetComponents<Component>()) (c->*fn)(info);
}
void DispatchTrigger(GameObject* go, void (Component::*fn)(Collider2D*), Collider2D* other) {
    for (Component* c : go->GetComponents<Component>()) (c->*fn)(other);
}

} // namespace

void Physics2D::Step(Scene& scene, float dt) {
    if (dt <= 0.0f) return;

    // Sleeping thresholds: below these speeds for kSleepTime seconds, a dynamic
    // body sleeps (skipped until woken). Mirrors Unity/Box2D's rest optimisation.
    constexpr float kLinSleep = 0.05f;   // world units / second
    constexpr float kAngSleep = 2.0f;    // degrees / second
    constexpr float kSleepTime = 0.5f;   // seconds at rest before sleeping

    // 1) Integrate dynamic / kinematic bodies.
    auto bodies = scene.FindObjectsOfType<Rigidbody2D>();
    for (Rigidbody2D* rb : bodies) {
        if (!rb->enabled || !rb->gameObject || !rb->gameObject->active) continue;
        Transform* t = rb->transform;
        // A sleeping body stays put until something wakes it. If its velocity was
        // set directly (script/teleport) it exceeds the threshold -> wake and run.
        if (rb->bodyType == Rigidbody2D::BodyType::Dynamic && rb->sleeping) {
            if (rb->velocity.SqrMagnitude() > kLinSleep * kLinSleep ||
                Mathf::Abs(rb->angularVelocity) > kAngSleep) {
                rb->WakeUp();
            } else {
                rb->ConsumeForce(); rb->ConsumeTorque();   // discard so nothing accrues
                continue;
            }
        }
        if (rb->bodyType == Rigidbody2D::BodyType::Dynamic) {
            Vec2 accel = gravity * rb->gravityScale + rb->ConsumeForce() * rb->InvMass();
            rb->velocity += accel * dt;
            if (rb->drag > 0.0f) rb->velocity *= 1.0f / (1.0f + rb->drag * dt);
            // Angular: apply accumulated torque (scaled by inverse inertia) and damp.
            if (!rb->freezeRotation) {
                float invI = InvInertia(rb, rb->gameObject->GetComponent<Collider2D>());
                rb->angularVelocity += rb->ConsumeTorque() * invI * dt * Mathf::Rad2Deg;
                if (rb->angularDrag > 0.0f) rb->angularVelocity *= 1.0f / (1.0f + rb->angularDrag * dt);
            } else {
                rb->ConsumeTorque();   // discard so it doesn't accumulate while frozen
            }
        }
        if (rb->bodyType != Rigidbody2D::BodyType::Static) {
            t->localPosition += Vec3{rb->velocity * dt};
            if (!rb->freezeRotation && rb->angularVelocity != 0.0f) {
                Vec3 e = t->localRotation.ToEuler();
                e.z += rb->angularVelocity * dt;
                t->localRotation = Quat::Euler(e);
            }
        }
    }

    // 2) Broad+narrow phase over every collider pair.
    auto colliders = scene.FindObjectsOfType<Collider2D>();
    std::set<Pair> current;

    // Precompute world AABBs once per step (were recomputed O(n) times per collider)
    // and a membership set for the exit-message cleanup (was an O(contacts*n) scan).
    const std::size_t nc = colliders.size();
    std::vector<Vec2> aabbMin(nc), aabbMax(nc);
    std::unordered_set<Collider2D*> aliveSet;
    aliveSet.reserve(nc * 2 + 1);
    for (std::size_t i = 0; i < nc; ++i) {
        colliders[i]->WorldAABB(aabbMin[i], aabbMax[i]);
        aliveSet.insert(colliders[i]);
    }

    for (std::size_t i = 0; i < nc; ++i) {
        for (std::size_t j = i + 1; j < nc; ++j) {
            Collider2D* a = colliders[i];
            Collider2D* b = colliders[j];
            if (!a->enabled || !b->enabled) continue;
            if (!a->gameObject->active || !b->gameObject->active) continue;
            if (a->gameObject == b->gameObject) continue;
            if (!LayersCollide(a->layer, b->layer)) continue; // collision matrix

            // Broad phase: AABB reject (precomputed AABBs).
            const Vec2 &aMin = aabbMin[i], &aMax = aabbMax[i], &bMin = aabbMin[j], &bMax = aabbMax[j];
            if (aMax.x < bMin.x || aMin.x > bMax.x || aMax.y < bMin.y || aMin.y > bMax.y)
                continue;

            Contact c = TestColliders(a, b);
            if (!c.hit) continue;

            current.insert({a, b});

            Rigidbody2D* ra = a->gameObject->GetComponent<Rigidbody2D>();
            Rigidbody2D* rb = b->gameObject->GetComponent<Rigidbody2D>();
            bool trigger = a->isTrigger || b->isTrigger;

            // 3) Resolve solids (skip triggers and pairs without dynamics).
            if (!trigger && !OneWayPassThrough(a, b)) {
                // Wake a sleeping body when a moving body (dynamic or kinematic)
                // runs into it; two bodies both at rest stay asleep.
                auto awakeMover = [](Rigidbody2D* r) {
                    return r && !r->sleeping && r->bodyType != Rigidbody2D::BodyType::Static;
                };
                if (ra && ra->sleeping && awakeMover(rb)) ra->WakeUp();
                if (rb && rb->sleeping && awakeMover(ra)) rb->WakeUp();
                // Both still asleep -> nothing to resolve (resting stack).
                if (!(ra && ra->sleeping) || !(rb && rb->sleeping)) {
                float ima = ra ? ra->InvMass() : 0.0f;
                float imb = rb ? rb->InvMass() : 0.0f;
                float imSum = ima + imb;
                if (imSum > 0.0f) {
                    // Positional correction.
                    Vec2 correction = c.normal * (c.penetration / imSum);
                    a->transform->localPosition -= Vec3{correction * ima};
                    b->transform->localPosition += Vec3{correction * imb};

                    // Angular terms: lever arms from each body's center to the contact
                    // point, and inverse inertia. When rotation is locked these are 0,
                    // so the math collapses to the old linear-only impulse exactly.
                    float iia = InvInertia(ra, a);
                    float iib = InvInertia(rb, b);
                    Vec2 cenA = a->WorldCenter(), cenB = b->WorldCenter();
                    Vec2 rA = c.point - cenA, rB = c.point - cenB;
                    auto pointVel = [](const Vec2& v, float wDeg, const Vec2& r) {
                        float w = wDeg * Mathf::Deg2Rad;            // ω×r in 2D
                        return v + Vec2{-w * r.y, w * r.x};
                    };
                    float rnA = rA.x * c.normal.y - rA.y * c.normal.x;   // cross(r, n)
                    float rnB = rB.x * c.normal.y - rB.y * c.normal.x;
                    float denom = imSum + rnA * rnA * iia + rnB * rnB * iib;

                    Vec2 va = ra ? ra->velocity : Vec2::Zero;
                    Vec2 vb = rb ? rb->velocity : Vec2::Zero;
                    float wa = ra ? ra->angularVelocity : 0.0f;
                    float wb = rb ? rb->angularVelocity : 0.0f;
                    Vec2 rvel = pointVel(vb, wb, rB) - pointVel(va, wa, rA);
                    float velAlongNormal = Vec2::Dot(rvel, c.normal);
                    float jImp = 0.0f;
                    if (velAlongNormal < 0.0f && denom > 0.0f) {
                        float e = Mathf::Max(ra ? ra->bounciness : 0.0f,
                                             rb ? rb->bounciness : 0.0f);
                        // Below a small approach speed, drop restitution so bodies
                        // settle (and then sleep) instead of buzzing with micro-bounces
                        // (Box2D/Unity's velocity threshold).
                        if (-velAlongNormal < 0.5f) e = 0.0f;
                        jImp = -(1.0f + e) * velAlongNormal / denom;
                        Vec2 impulse = c.normal * jImp;
                        if (ra) { ra->velocity -= impulse * ima; ra->angularVelocity -= rnA * jImp * iia * Mathf::Rad2Deg; }
                        if (rb) { rb->velocity += impulse * imb; rb->angularVelocity += rnB * jImp * iib * Mathf::Rad2Deg; }
                    }
                    // Coulomb friction on the tangential relative velocity at the
                    // contact point, clamped to friction x the normal impulse. With
                    // gravity re-pressing each step this also gives resting bodies
                    // static-like friction, and the lever arm makes off-center drag
                    // spin the body (so a box thrown along the floor tumbles to rest).
                    if (jImp > 0.0f) {
                        wa = ra ? ra->angularVelocity : 0.0f;        // post-normal-impulse
                        wb = rb ? rb->angularVelocity : 0.0f;
                        va = ra ? ra->velocity : Vec2::Zero;
                        vb = rb ? rb->velocity : Vec2::Zero;
                        Vec2 rv = pointVel(vb, wb, rB) - pointVel(va, wa, rA);
                        Vec2 tang = rv - c.normal * Vec2::Dot(rv, c.normal);
                        float tlen = tang.Magnitude();
                        if (tlen > 1e-4f) {
                            Vec2 t = tang / tlen;
                            float rtA = rA.x * t.y - rA.y * t.x;
                            float rtB = rB.x * t.y - rB.y * t.x;
                            float denomT = imSum + rtA * rtA * iia + rtB * rtB * iib;
                            float jt = denomT > 0.0f ? -Vec2::Dot(rv, t) / denomT : 0.0f;
                            float fa = ra ? ra->friction : 0.4f;
                            float fb = rb ? rb->friction : 0.4f;
                            float mu = Mathf::Sqrt((fa < 0 ? 0 : fa) * (fb < 0 ? 0 : fb));
                            float maxF = mu * jImp;
                            jt = Mathf::Clamp(jt, -maxF, maxF);
                            Vec2 fimp = t * jt;
                            if (ra) { ra->velocity -= fimp * ima; ra->angularVelocity -= rtA * jt * iia * Mathf::Rad2Deg; }
                            if (rb) { rb->velocity += fimp * imb; rb->angularVelocity += rtB * jt * iib * Mathf::Rad2Deg; }
                        }
                    }
                }
                } // both-asleep guard
            }

            // 4) Fire enter/stay messages.
            bool wasContact = m_contacts.count({a, b}) != 0;
            if (trigger) {
                if (!wasContact) {
                    DispatchTrigger(a->gameObject, &Component::OnTriggerEnter2D, b);
                    DispatchTrigger(b->gameObject, &Component::OnTriggerEnter2D, a);
                } else {
                    DispatchTrigger(a->gameObject, &Component::OnTriggerStay2D, b);
                    DispatchTrigger(b->gameObject, &Component::OnTriggerStay2D, a);
                }
            } else {
                Collision2D forA{b->gameObject, b, c.normal, c.penetration};
                Collision2D forB{a->gameObject, a, -c.normal, c.penetration};
                if (!wasContact) {
                    DispatchCollision(a->gameObject, &Component::OnCollisionEnter2D, forA);
                    DispatchCollision(b->gameObject, &Component::OnCollisionEnter2D, forB);
                } else {
                    DispatchCollision(a->gameObject, &Component::OnCollisionStay2D, forA);
                    DispatchCollision(b->gameObject, &Component::OnCollisionStay2D, forB);
                }
            }
        }
    }

    // 4.5) Joints: positional constraints (after collisions, mirrors Physics3D).
    for (Joint2D* j : scene.FindObjectsOfType<Joint2D>()) {
        if (j->broken || !j->gameObject || !j->transform) continue;
        Rigidbody2D* ra = j->gameObject->GetComponent<Rigidbody2D>();
        if (!ra) continue;                          // the joint moves THIS body
        Transform* ta = j->transform;
        Rigidbody2D* rbB = nullptr; Transform* tb = nullptr;
        if (!j->connectedBody.empty())
            if (GameObject* g = scene.Find(j->connectedBody)) { tb = g->transform; rbB = g->GetComponent<Rigidbody2D>(); }
        Vec3 pa3 = ta->Position();
        Vec2 pa{pa3.x, pa3.y};
        Vec2 pb = j->anchor;
        if (tb) { Vec3 p = tb->Position(); pb = {p.x, p.y}; }
        if (!j->initialized) {
            j->pinOffset = pa - pb;
            j->restLen = j->autoConfigure ? (pa - pb).Magnitude() : j->distance;
            j->hingeLocalA = pb - pa;                            // lever COM_A -> pivot, at init
            j->refAngleA = ta->localRotation.ToEuler().z;
            j->initialized = true;
        }
        float imA = ra->InvMass();
        float imB = rbB ? rbB->InvMass() : 0.0f;
        float imSum = imA + imB;
        if (imSum <= 0.0f) continue;
        Joint2D::Mode m = (Joint2D::Mode)j->mode;

        if (m == Joint2D::Mode::Pin) {
            Vec2 target = pb + j->pinOffset;
            Vec2 err = target - pa;
            ta->localPosition += Vec3{err * (imA / imSum)};
            if (tb && rbB) tb->localPosition -= Vec3{err * (imB / imSum)};
            ra->velocity = rbB ? rbB->velocity : Vec2::Zero;   // weld: follow B (anchor -> freeze)
            if (j->breakable && err.Magnitude() > j->breakForce) j->broken = true;
        } else if (m == Joint2D::Mode::Hinge) {
            // Revolute joint: pin a material point of A to the pivot, free to spin.
            // (B supplies the pivot/linear reference; the reaction is applied to A.)
            float angleA = ta->localRotation.ToEuler().z;
            Vec2  rA = Vec2::Rotate(j->hingeLocalA, angleA - j->refAngleA);
            Vec2  anchorWorld = pa + rA;
            float iiA = InvInertia(ra, ra->gameObject->GetComponent<Collider2D>());
            Vec2  vB = rbB ? rbB->velocity : Vec2::Zero;

            // Velocity constraint: cancel the velocity of A's anchor point (vs B).
            float wA = ra->angularVelocity * Mathf::Deg2Rad;
            Vec2  vAnchor = ra->velocity + Vec2{-wA * rA.y, wA * rA.x} - vB;
            float k00 = imA + iiA * rA.y * rA.y;
            float k01 = -iiA * rA.x * rA.y;
            float k11 = imA + iiA * rA.x * rA.x;
            float det = k00 * k11 - k01 * k01;
            if (Mathf::Abs(det) > 1e-9f) {
                Vec2 P{ -( k11 * vAnchor.x - k01 * vAnchor.y) / det,
                        -(-k01 * vAnchor.x + k00 * vAnchor.y) / det };
                ra->velocity += P * imA;
                ra->angularVelocity += (rA.x * P.y - rA.y * P.x) * iiA * Mathf::Rad2Deg;
            }
            // Motor: drive the spin toward motorSpeed, torque-limited.
            if (j->useMotor && iiA > 0.0f) {
                float Cdot = (ra->angularVelocity - j->motorSpeed) * Mathf::Deg2Rad;
                float imp = Mathf::Clamp(-Cdot / iiA, -j->maxMotorTorque * dt, j->maxMotorTorque * dt);
                ra->angularVelocity += imp * iiA * Mathf::Rad2Deg;
            }
            // Angle limits: clamp the relative angle and kill the outward spin.
            if (j->useLimits) {
                float rel = angleA - j->refAngleA;
                float cl = Mathf::Clamp(rel, j->minAngle, j->maxAngle);
                if (cl != rel) {
                    Vec3 e = ta->localRotation.ToEuler();
                    e.z += (cl - rel);
                    ta->localRotation = Quat::Euler(e);
                    if ((rel > j->maxAngle && ra->angularVelocity > 0.0f) ||
                        (rel < j->minAngle && ra->angularVelocity < 0.0f))
                        ra->angularVelocity = 0.0f;
                }
            }
            // Positional correction: pull A's anchor point back onto the pivot.
            Vec2 Cpos = anchorWorld - pb;
            ta->localPosition -= Vec3{Cpos};
            if (j->breakable && Cpos.Magnitude() > j->breakForce) j->broken = true;
        } else {
            Vec2 d = pb - pa; float len = d.Magnitude();
            Vec2 n = len > 1e-5f ? d / len : Vec2{0, 1};
            float C = len - j->restLen;                 // +stretched / -compressed
            Vec2 va = ra->velocity, vb = rbB ? rbB->velocity : Vec2::Zero;
            float vrelN = Vec2::Dot(vb - va, n);
            if (m == Joint2D::Mode::Spring) {
                float f = j->spring * C + j->damper * vrelN;   // restore + damp, along n (A->B)
                Vec2 imp = n * (f * dt);
                ra->velocity += imp * imA;
                if (rbB) rbB->velocity -= imp * imB;
            } else {                                    // Distance (rigid rod)
                float jImp = -vrelN / imSum;
                Vec2 imp = n * jImp;
                ra->velocity -= imp * imA;
                if (rbB) rbB->velocity += imp * imB;
                Vec2 corr = n * C;                       // restore the rest length
                ta->localPosition += Vec3{corr * (imA / imSum)};
                if (tb && rbB) tb->localPosition -= Vec3{corr * (imB / imSum)};
            }
            if (j->breakable && Mathf::Abs(C) > j->breakForce) j->broken = true;
        }
    }

    // 4.75) Sleep bookkeeping: a dynamic body under the speed thresholds for
    // kSleepTime seconds goes to sleep (zeroed and skipped next step). Any motion
    // resets the timer. Bodies with allowSleep off never sleep.
    for (Rigidbody2D* rb : bodies) {
        if (!rb->enabled || !rb->gameObject || !rb->gameObject->active) continue;
        if (rb->bodyType != Rigidbody2D::BodyType::Dynamic) continue;
        if (!rb->allowSleep) { rb->WakeUp(); continue; }
        if (rb->sleeping) continue;
        if (rb->velocity.SqrMagnitude() < kLinSleep * kLinSleep &&
            Mathf::Abs(rb->angularVelocity) < kAngSleep) {
            rb->m_sleepTimer += dt;
            if (rb->m_sleepTimer >= kSleepTime) {
                rb->sleeping = true;
                rb->velocity = Vec2::Zero;
                rb->angularVelocity = 0.0f;
            }
        } else {
            rb->m_sleepTimer = 0.0f;
        }
    }

    // 5) Fire exit messages for contacts that ended.
    for (const Pair& p : m_contacts) {
        if (current.count(p)) continue;
        Collider2D* a = p.first;
        Collider2D* b = p.second;
        // The colliders may have been destroyed; guard via the live list.
        if (!aliveSet.count(a) || !aliveSet.count(b)) continue;
        if (a->isTrigger || b->isTrigger) {
            DispatchTrigger(a->gameObject, &Component::OnTriggerExit2D, b);
            DispatchTrigger(b->gameObject, &Component::OnTriggerExit2D, a);
        } else {
            Collision2D forA{b->gameObject, b, {0, 0}, 0.0f};
            Collision2D forB{a->gameObject, a, {0, 0}, 0.0f};
            DispatchCollision(a->gameObject, &Component::OnCollisionExit2D, forA);
            DispatchCollision(b->gameObject, &Component::OnCollisionExit2D, forB);
        }
    }

    m_contacts.swap(current);
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

bool Alive(Collider2D* c) { return c->enabled && c->gameObject && c->gameObject->active; }

} // namespace

RaycastHit2D Physics2D::Raycast(Scene& scene, const Vec2& origin, const Vec2& direction,
                                float maxDistance) {
    RaycastHit2D best;
    best.distance = maxDistance;
    Vec2 dir = direction.Normalized();
    for (Collider2D* c : scene.FindObjectsOfType<Collider2D>()) {
        if (!Alive(c)) continue;
        float t; Vec2 n;
        bool hit = false;
        if (c->shape() == Collider2D::Shape::Circle) {
            auto* cc = static_cast<CircleCollider2D*>(c);
            hit = RayCircle(origin, dir, cc->WorldCenter(), cc->WorldRadius(), best.distance, t, n);
        } else if (c->shape() == Collider2D::Shape::Box) {   // exact oriented-box ray test
            hit = RayOBB2(origin, dir, static_cast<BoxCollider2D*>(c), best.distance, t, n);
        } else { // capsule/edge/polygon (via its AABB)
            Vec2 mn, mx; c->WorldAABB(mn, mx);
            hit = RayAABB(origin, dir, mn, mx, best.distance, t, n);
        }
        if (hit && t <= best.distance) {
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

Collider2D* Physics2D::OverlapPoint(Scene& scene, const Vec2& p) {
    for (Collider2D* c : scene.FindObjectsOfType<Collider2D>()) {
        if (!Alive(c)) continue;
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
        } else { // Capsule: distance from point to the inner segment.
            auto* cap = static_cast<CapsuleCollider2D*>(c);
            Vec2 a, b; cap->Segment(a, b);
            if ((p - ClosestOnSegment(p, a, b)).Magnitude() <= cap->WorldRadius()) return c;
        }
    }
    return nullptr;
}

std::vector<Collider2D*> Physics2D::OverlapCircle(Scene& scene, const Vec2& center, float radius) {
    std::vector<Collider2D*> out;
    for (Collider2D* c : scene.FindObjectsOfType<Collider2D>()) {
        if (!Alive(c)) continue;
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
        } else { // circle or capsule, reduced to a circle near the query center
            Vec2 cc; float r; AsCircle(c, center, cc, r);
            hit = (cc - center).Magnitude() <= radius + r;
        }
        if (hit) out.push_back(c);
    }
    return out;
}

std::vector<Collider2D*> Physics2D::OverlapBox(Scene& scene, const Vec2& center, const Vec2& half) {
    std::vector<Collider2D*> out;
    Vec2 qmn{center.x - half.x, center.y - half.y};
    Vec2 qmx{center.x + half.x, center.y + half.y};
    for (Collider2D* c : scene.FindObjectsOfType<Collider2D>()) {
        if (!Alive(c)) continue;
        bool hit = false;
        if (c->shape() == Collider2D::Shape::Box) {
            Vec2 mn, mx; c->WorldAABB(mn, mx);
            hit = mn.x <= qmx.x && mx.x >= qmn.x && mn.y <= qmx.y && mx.y >= qmn.y;
        } else { // circle or capsule
            Vec2 cc; float r; AsCircle(c, center, cc, r);
            Vec2 cl = ClosestOnBox(cc, qmn, qmx);
            hit = (cl - cc).Magnitude() <= r;
        }
        if (hit) out.push_back(c);
    }
    return out;
}

namespace {
// Does a circle (centre c, radius r) touch collider `col`? If so fill `n` (unit
// normal from the surface toward c) and `point` (the struck surface point).
bool CircleTouch(Collider2D* col, const Vec2& c, float r, Vec2& n, Vec2& point) {
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
} // namespace

RaycastHit2D Physics2D::CircleCast(Scene& scene, const Vec2& origin, const Vec2& direction,
                                   float radius, float maxDistance) {
    RaycastHit2D best;
    Vec2 dir = direction.Normalized();
    if (radius <= 0.0f) return Raycast(scene, origin, dir, maxDistance);
    auto colliders = scene.FindObjectsOfType<Collider2D>();
    float step = radius * 0.5f; if (step < 0.02f) step = 0.02f;
    for (float t = 0.0f; t <= maxDistance; t += step) {
        Vec2 c = origin + dir * t;
        for (Collider2D* col : colliders) {
            if (!Alive(col) || col->isTrigger) continue;
            Vec2 n, p;
            if (CircleTouch(col, c, radius, n, p)) {
                best.hit = true; best.collider = col; best.gameObject = col->gameObject;
                best.distance = t; best.point = p; best.normal = n;
                return best;
            }
        }
    }
    return best;
}

} // namespace okay
