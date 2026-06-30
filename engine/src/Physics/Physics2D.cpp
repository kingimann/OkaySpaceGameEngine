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
};

Vec2 ClampVec(const Vec2& v, const Vec2& lo, const Vec2& hi) {
    return {Mathf::Clamp(v.x, lo.x, hi.x), Mathf::Clamp(v.y, lo.y, hi.y)};
}

Contact TestBoxBox(const Vec2& ca, const Vec2& ha, const Vec2& cb, const Vec2& hb) {
    Contact c;
    Vec2 d = cb - ca;
    float ox = (ha.x + hb.x) - Mathf::Abs(d.x);
    if (ox <= 0) return c;
    float oy = (ha.y + hb.y) - Mathf::Abs(d.y);
    if (oy <= 0) return c;
    c.hit = true;
    if (ox < oy) { c.normal = {d.x < 0 ? -1.0f : 1.0f, 0.0f}; c.penetration = ox; }
    else         { c.normal = {0.0f, d.y < 0 ? -1.0f : 1.0f}; c.penetration = oy; }
    return c;
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

// Reduce a collider to a circle (center + radius) as seen from `ref`. Capsules
// collapse to a circle at the segment point nearest `ref`.
void AsCircle(Collider2D* col, const Vec2& ref, Vec2& center, float& radius) {
    if (col->shape() == Collider2D::Shape::Circle) {
        auto* c = static_cast<CircleCollider2D*>(col);
        center = c->WorldCenter(); radius = c->WorldRadius();
    } else { // Capsule
        auto* cap = static_cast<CapsuleCollider2D*>(col);
        Vec2 a, b; cap->Segment(a, b);
        center = ClosestOnSegment(ref, a, b);
        radius = cap->WorldRadius();
    }
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
        auto* ba = static_cast<BoxCollider2D*>(a);
        auto* bb = static_cast<BoxCollider2D*>(b);
        return TestBoxBox(ba->WorldCenter(), ba->HalfExtents(),
                          bb->WorldCenter(), bb->HalfExtents());
    }
    if (aBox) { // A box, B circle/capsule
        auto* box = static_cast<BoxCollider2D*>(a);
        Vec2 cc; float r; AsCircle(b, box->WorldCenter(), cc, r);
        return TestBoxCircle(box->WorldCenter(), box->HalfExtents(), cc, r);
    }
    if (bBox) { // A circle/capsule, B box
        auto* box = static_cast<BoxCollider2D*>(b);
        Vec2 cc; float r; AsCircle(a, box->WorldCenter(), cc, r);
        Contact c = TestBoxCircle(box->WorldCenter(), box->HalfExtents(), cc, r);
        c.normal = -c.normal; // flip to point from A toward B
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

    // 1) Integrate dynamic / kinematic bodies.
    auto bodies = scene.FindObjectsOfType<Rigidbody2D>();
    for (Rigidbody2D* rb : bodies) {
        if (!rb->enabled || !rb->gameObject || !rb->gameObject->active) continue;
        Transform* t = rb->transform;
        if (rb->bodyType == Rigidbody2D::BodyType::Dynamic) {
            Vec2 accel = gravity * rb->gravityScale + rb->ConsumeForce() * rb->InvMass();
            rb->velocity += accel * dt;
            if (rb->drag > 0.0f) rb->velocity *= 1.0f / (1.0f + rb->drag * dt);
        }
        if (rb->bodyType != Rigidbody2D::BodyType::Static) {
            t->localPosition += Vec3{rb->velocity * dt};
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
            if (!trigger) {
                float ima = ra ? ra->InvMass() : 0.0f;
                float imb = rb ? rb->InvMass() : 0.0f;
                float imSum = ima + imb;
                if (imSum > 0.0f) {
                    // Positional correction.
                    Vec2 correction = c.normal * (c.penetration / imSum);
                    a->transform->localPosition -= Vec3{correction * ima};
                    b->transform->localPosition += Vec3{correction * imb};

                    // Impulse along the normal.
                    Vec2 va = ra ? ra->velocity : Vec2::Zero;
                    Vec2 vb = rb ? rb->velocity : Vec2::Zero;
                    float velAlongNormal = Vec2::Dot(vb - va, c.normal);
                    float jImp = 0.0f;
                    if (velAlongNormal < 0.0f) {
                        float e = Mathf::Max(ra ? ra->bounciness : 0.0f,
                                             rb ? rb->bounciness : 0.0f);
                        jImp = -(1.0f + e) * velAlongNormal / imSum;
                        Vec2 impulse = c.normal * jImp;
                        if (ra) ra->velocity -= impulse * ima;
                        if (rb) rb->velocity += impulse * imb;
                    }
                    // Coulomb friction on the tangential relative velocity, clamped
                    // to friction x the normal impulse. With gravity re-pressing each
                    // step this also gives resting bodies static-like friction, so a
                    // box thrown along the floor slides to a stop instead of forever.
                    if (jImp > 0.0f) {
                        Vec2 rva = ra ? ra->velocity : Vec2::Zero;   // post-normal-impulse
                        Vec2 rvb = rb ? rb->velocity : Vec2::Zero;
                        Vec2 rvel = rvb - rva;
                        Vec2 tang = rvel - c.normal * Vec2::Dot(rvel, c.normal);
                        float tlen = tang.Magnitude();
                        if (tlen > 1e-4f) {
                            Vec2 t = tang / tlen;
                            float jt = -Vec2::Dot(rvel, t) / imSum;
                            float fa = ra ? ra->friction : 0.4f;
                            float fb = rb ? rb->friction : 0.4f;
                            float mu = Mathf::Sqrt((fa < 0 ? 0 : fa) * (fb < 0 ? 0 : fb));
                            float maxF = mu * jImp;
                            jt = Mathf::Clamp(jt, -maxF, maxF);
                            Vec2 fimp = t * jt;
                            if (ra) ra->velocity -= fimp * ima;
                            if (rb) rb->velocity += fimp * imb;
                        }
                    }
                }
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
        } else { // Box or capsule (via its AABB)
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
            Vec2 mn, mx; c->WorldAABB(mn, mx);
            if (p.x >= mn.x && p.x <= mx.x && p.y >= mn.y && p.y <= mx.y) return c;
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
            Vec2 mn, mx; c->WorldAABB(mn, mx);
            hit = (ClosestOnBox(center, mn, mx) - center).Magnitude() <= radius;
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

} // namespace okay
