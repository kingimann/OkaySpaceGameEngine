#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Components/Terrain.hpp"
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

Contact TestColliders(Collider3D* a, Collider3D* b) {
    using S = Collider3D::Shape;
    S sa = a->shape(), sb = b->shape();

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
        }
        if (rb->freezeX) rb->velocity.x = 0.0f;
        if (rb->freezeY) rb->velocity.y = 0.0f;
        if (rb->freezeZ) rb->velocity.z = 0.0f;
        if (rb->bodyType != Rigidbody3D::BodyType::Static)
            t->localPosition = t->localPosition + rb->velocity * dt;
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

                    Vec3 va = ra ? ra->velocity : Vec3::Zero;
                    Vec3 vb = rb ? rb->velocity : Vec3::Zero;
                    float velAlongNormal = Vec3::Dot(vb - va, c.normal);
                    if (velAlongNormal < 0.0f) {
                        float e = Mathf::Max(ra ? ra->bounciness : 0.0f,
                                             rb ? rb->bounciness : 0.0f);
                        float jImp = -(1.0f + e) * velAlongNormal / imSum;
                        Vec3 impulse = c.normal * jImp;
                        if (ra) ra->velocity = ra->velocity - impulse * ima;
                        if (rb) rb->velocity = rb->velocity + impulse * imb;
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
                if (pos.y < targetY) {                      // sank into the ground -> lift out
                    t->localPosition.y += (targetY - pos.y);
                    if (rb->velocity.y < 0.0f) rb->velocity.y = 0.0f;   // ground downward motion
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
