// CoreRagdoll - the ragdoll's bodies on our physics core (okay::phys::World, docs/physics/physics_core.md).
//
// One core body per bone with a capsule (or a box for a foot) as its collider, one core joint per joint (ConeTwist
// for a ball with a swing cone and a twist limit, Hinge with limits for knees and elbows), and the joint's own
// orientation drive as the muscle. Pins and the get-up helper are core joints to the world. Nothing is solved here:
// the core's solver does it, together with whatever else is in that world, so limbs meet dynamic bodies and each
// other. Written for this engine from the core's own interface (World.hpp); the control ideas are listed in
// docs/gameplay/active_ragdoll.md.
#include "okay/Animation/RagdollPhysics.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Physics/Core/World.hpp"
#include <cmath>
#include <unordered_map>
#include <vector>

namespace okay {
namespace dyn {

namespace {
phys::V3 P3(const Vec3& v) { return {v.x, v.y, v.z}; }
Vec3 E3(const phys::V3& v) { return {v.x, v.y, v.z}; }
phys::Q PQ(const Quat& q) { return phys::NormalizeQ(phys::Q{q.x, q.y, q.z, q.w}); }
Quat EQ(const phys::Q& q) { return Quat{q.x, q.y, q.z, q.w}; }

bool Ok(float f) { return std::isfinite(f); }
bool Ok(const Vec3& v) { return Ok(v.x) && Ok(v.y) && Ok(v.z); }
bool Ok(const Quat& q) { return Ok(q.x) && Ok(q.y) && Ok(q.z) && Ok(q.w) && q.Magnitude() > 1e-6f; }
float Capped(float v, float hi, float fallback) { return Ok(v) ? Mathf::Clamp(v, 0.0f, hi) : fallback; }

// The shortest turn that takes +X onto the unit vector `to` (a joint frame only needs its X axis placed).
phys::Q FrameWithX(const phys::V3& to) {
    const phys::V3 x{1, 0, 0};
    const float d = phys::Dot(x, to);
    if (d < -0.999999f) return phys::Q::AxisAngle(phys::V3{0, 1, 0}, 3.14159265358979f);
    const phys::V3 c = phys::Cross(x, to);
    return phys::NormalizeQ(phys::Q{c.x, c.y, c.z, 1.0f + d});
}

constexpr float kTwoPi = 6.28318530717959f;
} // namespace

struct CoreRagdoll::Impl {
    std::unique_ptr<phys::World> own;      // the world we made (no scene)
    phys::World* world = nullptr;          // own.get(), or the scene's
    std::weak_ptr<void> sharedAlive;       // the scene world's life (expired = it is gone, with every id)
    bool shared = false;

    struct Probe { Vec3 local; float radius = 0.0f; bool touching = false; Vec3 normal{0, 1, 0}, point; };
    struct Body {
        phys::BodyId id = phys::kInvalidId;
        BodyDesc desc;
        float mass = 1.0f, turnInertia = 0.01f;
        std::vector<phys::ColliderId> colliders;
        std::vector<Probe> probes;
        bool touching = false;
        Vec3 force, torque;
        phys::JointId pin = phys::kInvalidId;
        Vec3 pinLocal, pinWorld; float pinK = 0.0f;
        phys::JointId helper = phys::kInvalidId;
    };
    struct Joint {
        phys::JointId id = phys::kInvalidId;
        int p = -1, c = -1;
        phys::Q frameB;                    // the joint frame in the child (drive targets are turned into it)
        JointDrive drive;
    };
    std::vector<Body> bodies;
    std::vector<Joint> joints;
    std::unordered_map<phys::ColliderId, int> owner;   // collider -> body index
    float friction = 0.0f;
    bool active = true, filtered = false, stale = true;
    int resets = 0;

    bool Live() {
        if (shared && sharedAlive.expired()) {   // the scene dropped its world: every id died with it
            world = nullptr;
            shared = false;
            Forget();
        }
        return world != nullptr;
    }
    void Forget() { bodies.clear(); joints.clear(); owner.clear(); filtered = false; stale = true; }

    phys::Shape ShapeOf(const BodyDesc& d, phys::Xf& local) const {
        local = phys::Xf{};
        if (d.hasBox) {
            local.p = P3(d.boxCentre);
            return phys::Shape::Box(phys::V3{Mathf::Max(d.boxHalf.x, 1e-3f), Mathf::Max(d.boxHalf.y, 1e-3f), Mathf::Max(d.boxHalf.z, 1e-3f)});
        }
        if (d.capsuleRadius > 0.0f) return phys::Shape::Capsule(d.capsuleRadius, Mathf::Max(d.capsuleHalf, 0.0f));
        float r = 0.02f;                                    // no shape given: a ball around its probes
        for (const ContactSphere& s : d.spheres) r = Mathf::Max(r, s.local.Magnitude() + s.radius);
        return phys::Shape::Sphere(r);
    }
    void AddColliders(Body& b, std::uint64_t user) {
        if (!b.colliders.empty()) return;
        phys::ColliderDef cd;
        cd.shape = ShapeOf(b.desc, cd.local);
        cd.friction = b.desc.friction;
        cd.user = user;
        const phys::ColliderId c = world->AddCollider(b.id, cd);
        if (c == phys::kInvalidId) return;
        b.colliders.push_back(c);
        owner[c] = (int)(&b - bodies.data());
        world->SetMass(b.id, b.mass);
    }
    void RemoveColliders(Body& b) {
        for (phys::ColliderId c : b.colliders) { world->RemoveCollider(c); owner.erase(c); }
        b.colliders.clear();
    }
    // Limbs that overlap in the build pose (a hand resting on a thigh) never collide: they would fight the pose.
    void FilterRestOverlaps() {
        if (filtered) return;
        filtered = true;
        const int n = (int)bodies.size();
        for (int i = 0; i < n; ++i)
            for (int k = i + 1; k < n; ++k) {
                bool jointed = false;
                for (const Joint& j : joints) if ((j.p == i && j.c == k) || (j.p == k && j.c == i)) { jointed = true; break; }
                if (jointed) continue;
                phys::Xf la, lb;
                const phys::Shape sa = ShapeOf(bodies[i].desc, la), sb = ShapeOf(bodies[k].desc, lb);
                const phys::Xf xa = phys::Xf{P3(bodies[i].desc.position), PQ(bodies[i].desc.rotation)} * la;
                const phys::Xf xb = phys::Xf{P3(bodies[k].desc.position), PQ(bodies[k].desc.rotation)} * lb;
                phys::Manifold man;
                if (phys::Collide(sa, xa, sb, xb, 0.01f, man)) world->SetBodiesCollide(bodies[i].id, bodies[k].id, false);
            }
    }
    void DropPin(Body& b) {
        if (b.pin != phys::kInvalidId) world->DestroyJoint(b.pin);
        b.pin = phys::kInvalidId;
        b.pinK = 0.0f;
    }
    void DropHelper(Body& b) {
        if (b.helper != phys::kInvalidId) world->DestroyJoint(b.helper);
        b.helper = phys::kInvalidId;
    }
    // A joint between the world and a body, every axis free: springs and drives are added by the caller.
    phys::JointId WorldJoint(const Body& b, const Vec3& worldPoint, const Vec3& localPoint) {
        phys::JointDef d;
        d.type = phys::JointType::SixDof;
        d.bodyB = b.id;
        d.frameA.p = P3(worldPoint);
        d.frameB.p = P3(localPoint);
        for (int k = 0; k < 3; ++k) { d.linear[k].mode = phys::AxisMode::Free; d.angular[k].mode = phys::AxisMode::Free; }
        return world->CreateJoint(d);
    }
    // The mass a pull along world axis n meets at a point of the body (arm r from its centre of mass).
    float PointMass(const Body& b, const phys::V3& r, const phys::V3& n) const {
        const phys::Q q = world->GetRotation(b.id);
        const phys::V3 rn = phys::InvRotate(q, phys::Cross(r, n));
        const phys::V3 t = world->GetInertiaLocal(b.id).Inverse() * rn;
        const float k = 1.0f / b.mass + phys::Dot(rn, t);
        return k > 1e-9f ? 1.0f / k : b.mass;
    }
    void CommitPins() {
        for (Body& b : bodies) {
            if (b.pin == phys::kInvalidId) continue;
            phys::JointDef& d = world->EditJoint(b.pin);
            d.frameA.p = P3(b.pinWorld);
            const phys::V3 r = world->GetTransform(b.id).Apply(P3(b.pinLocal)) - world->GetCenterOfMass(b.id);
            for (int k = 0; k < 3; ++k) {
                phys::V3 n{};
                n[k] = 1.0f;
                d.linear[k].springFrequency = std::sqrt(b.pinK / PointMass(b, r, n)) / kTwoPi;   // k = m (2 pi f)^2
                d.linear[k].springDamping = 1.0f;
                d.linear[k].springTarget = 0.0f;
            }
        }
    }
    void CommitDrives() {
        for (Joint& j : joints) {
            phys::JointDef& d = world->EditJoint(j.id);
            const JointDrive& in = j.drive;
            // The two limbs turning against each other about their centres, as a spring between them sees them.
            const float ia = bodies[(std::size_t)j.p].turnInertia, ib = bodies[(std::size_t)j.c].turnInertia;
            const float inertia = ia * ib / (ia + ib);
            float k = 0.0f, c = 0.0f;
            if (in.useDrive) {
                k = in.stiffness;
                if (k <= 0.0f && in.frequency > 0.0f) { const float om = kTwoPi * in.frequency; k = inertia * om * om; }
                c = in.damping > 0.0f ? in.damping : 2.0f * in.dampingRatio * std::sqrt(k * inertia);
            }
            c += friction * inertia;                         // joint friction: turning dies out at `friction` per second
            d.drive = k > 0.0f || c > 0.0f;
            d.driveTarget = phys::NormalizeQ(phys::Conj(j.frameB) * PQ(in.useDrive ? in.target : Quat::Identity) * j.frameB);
            d.driveStiffness = k;
            d.driveDamping = c;
            d.driveFrequency = 0.0f;
            d.driveMaxTorque = in.useDrive && in.maxTorque > 0.0f ? in.maxTorque : 1e30f;
        }
    }
    void ClampSpeeds(float maxSpeed, float maxSpin) {
        for (Body& b : bodies) {
            const phys::V3 v = world->GetLinearVelocity(b.id), w = world->GetAngularVelocity(b.id);
            const float lv = phys::Len(v), lw = phys::Len(w);
            if (lv > maxSpeed) world->SetLinearVelocity(b.id, v * (maxSpeed / lv));
            if (lw > maxSpin) world->SetAngularVelocity(b.id, w * (maxSpin / lw));
        }
    }
    void Refresh() {
        if (!stale) return;
        stale = false;
        for (Body& b : bodies) {
            b.touching = false;
            for (Probe& p : b.probes) { p.touching = false; p.normal = Vec3::Up; }
        }
        if (!active) return;
        world->ForEachTouching([this](const phys::ContactEvent& e) {
            if (e.sensor) return;
            const auto ia = owner.find(e.a), ib = owner.find(e.b);
            if ((ia == owner.end()) == (ib == owner.end())) return;   // not ours, or two of our own limbs
            const bool mineIsA = ia != owner.end();
            Body& b = bodies[(std::size_t)(mineIsA ? ia->second : ib->second)];
            b.touching = true;
            if (b.probes.empty()) return;
            // Which probes are at the contact: the ones a contact point is close to.
            phys::Manifold man;
            phys::V3 pts[4];
            int count = 0;
            if (world->GetManifold(e.a, e.b, man))
                for (int k = 0; k < man.count; ++k) pts[count++] = (man.pts[k].pA + man.pts[k].pB) * 0.5f;
            if (count == 0) pts[count++] = e.point;
            const Vec3 n = E3(mineIsA ? e.normal * -1.0f : e.normal);   // out of the surface, toward our body
            const phys::Xf xf = world->GetTransform(b.id);
            for (Probe& p : b.probes) {
                const phys::V3 c = xf.Apply(P3(p.local));
                for (int k = 0; k < count; ++k) {
                    if (phys::Len(pts[k] - c) > 1.5f * p.radius + 0.02f) continue;
                    p.touching = true;
                    p.normal = n;
                    p.point = E3(pts[k]);
                    break;
                }
            }
        });
    }
};

CoreRagdoll::CoreRagdoll() : m(new Impl()) { UseWorld(nullptr, {}); }

CoreRagdoll::~CoreRagdoll() { Clear(); }

void CoreRagdoll::UseWorld(phys::World* world, std::weak_ptr<void> alive) {
    Clear();
    if (world) {
        m->world = world;
        m->sharedAlive = std::move(alive);
        m->shared = true;
        return;
    }
    if (!m->own) {
        m->own.reset(new phys::World());
        m->own->velocityIterations = 10;
    }
    m->world = m->own.get();
    m->shared = false;
}

phys::World* CoreRagdoll::World() { return m->Live() ? m->world : nullptr; }
bool CoreRagdoll::SharesWorld() const { return m->shared; }
void CoreRagdoll::ContactsChanged() { m->stale = true; }
std::uint32_t CoreRagdoll::CoreBody(int body) const {
    return body >= 0 && body < (int)m->bodies.size() ? m->bodies[(std::size_t)body].id : phys::kInvalidId;
}

void CoreRagdoll::Clear() {
    if (m->Live()) {
        for (Impl::Body& b : m->bodies) m->world->DestroyBody(b.id);   // (its colliders, joints, pin and helper go with it)
    }
    m->Forget();
    m->friction = 0.0f;
    m->active = true;
    m->resets = 0;
}

int CoreRagdoll::AddBody(const BodyDesc& in) {
    if (!m->Live()) return -1;
    Impl::Body b;
    b.desc = in;
    BodyDesc& d = b.desc;
    if (!Ok(d.position)) d.position = Vec3::Zero;
    d.rotation = Ok(d.rotation) ? d.rotation.Normalized() : Quat::Identity;
    b.mass = Ok(d.mass) && d.mass > 1e-3f ? d.mass : 1e-3f;
    d.friction = Ok(d.friction) ? Mathf::Clamp(d.friction, 0.0f, 2.0f) : 0.7f;
    d.capsuleRadius = Capped(d.capsuleRadius, 100.0f, 0.0f);
    d.capsuleHalf = Capped(d.capsuleHalf, 100.0f, 0.0f);
    if (d.hasBox && (!Ok(d.boxCentre) || !Ok(d.boxHalf))) d.hasBox = false;
    for (const ContactSphere& s : d.spheres)
        if (Ok(s.local) && Ok(s.radius) && s.radius > 0.0f) { Impl::Probe p; p.local = s.local; p.radius = s.radius; b.probes.push_back(p); }
    phys::BodyDef bd;
    bd.type = m->active ? phys::BodyType::Dynamic : phys::BodyType::Kinematic;
    bd.position = P3(d.position);
    bd.rotation = PQ(d.rotation);
    bd.allowSleep = false;                 // its muscles work every step
    bd.user = colliderUser;
    b.id = m->world->CreateBody(bd);
    if (b.id == phys::kInvalidId) return -1;
    m->bodies.push_back(b);
    Impl::Body& made = m->bodies.back();
    if (m->active) m->AddColliders(made, colliderUser);
    else m->world->SetMass(made.id, made.mass);
    // What a joint spring turns: the body's inertia across its long axis.
    {
        phys::Xf local;
        const phys::MassProps mp = m->ShapeOf(d, local).Mass(1.0f);
        const float t = mp.mass > 0.0f ? 0.5f * (mp.inertia.m[0][0] + mp.inertia.m[2][2]) * (made.mass / mp.mass) : 0.0f;
        made.turnInertia = Mathf::Max(t, 1e-6f);
    }
    m->filtered = false;
    m->stale = true;
    return (int)m->bodies.size() - 1;
}

int CoreRagdoll::AddJoint(const JointDesc& in) {
    const int n = (int)m->bodies.size();
    if (!m->Live() || in.parent < 0 || in.child < 0 || in.parent >= n || in.child >= n || in.parent == in.child) return -1;
    const Impl::Body& Pb = m->bodies[(std::size_t)in.parent];
    const Impl::Body& Cb = m->bodies[(std::size_t)in.child];
    const phys::Xf xp = m->world->GetTransform(Pb.id), xc = m->world->GetTransform(Cb.id);
    const phys::V3 anchor = Ok(in.anchor) ? P3(in.anchor) : xc.p;
    phys::JointDef d;
    d.bodyA = Pb.id;
    d.bodyB = Cb.id;
    phys::V3 axis;
    if (in.hinge) {
        const Vec3 ax = Ok(in.hingeAxis) && in.hingeAxis.SqrMagnitude() > 1e-8f ? in.hingeAxis.Normalized() : Vec3::Right;
        float lo = Ok(in.hingeMin) ? in.hingeMin : 0.0f, hi = Ok(in.hingeMax) ? in.hingeMax : 2.0f;
        if (hi < lo) { const float t = lo; lo = hi; hi = t; }
        axis = P3(ax);
        d.type = phys::JointType::Hinge;
        d.angular[0].mode = phys::AxisMode::Limited;
        d.angular[0].lower = lo;
        d.angular[0].upper = hi;
    } else {
        const float swing = Ok(in.swing) ? Mathf::Clamp(in.swing, 0.0f, Mathf::PI) : 1.0f;
        const float twist = Ok(in.twist) ? Mathf::Clamp(in.twist, 0.0f, Mathf::PI) : 0.5f;
        axis = phys::Rotate(xc.q, phys::V3{0, 1, 0});       // the child's long axis: it swings in a cone and twists about itself
        d.type = phys::JointType::ConeTwist;
        d.angular[0].mode = phys::AxisMode::Limited;
        d.angular[0].lower = -twist;
        d.angular[0].upper = twist;
        d.swingSpan1 = d.swingSpan2 = swing;
    }
    phys::SetupJoint(d);
    const phys::Q frame = FrameWithX(phys::Normalize(axis, phys::V3{1, 0, 0}));   // the same world frame on both sides: angles are 0 in the build pose
    d.frameA = phys::Xf{xp.ApplyInv(anchor), phys::NormalizeQ(phys::Conj(xp.q) * frame)};
    d.frameB = phys::Xf{xc.ApplyInv(anchor), phys::NormalizeQ(phys::Conj(xc.q) * frame)};
    Impl::Joint j;
    j.id = m->world->CreateJoint(d);
    if (j.id == phys::kInvalidId) return -1;
    j.p = in.parent;
    j.c = in.child;
    j.frameB = d.frameB.q;
    m->joints.push_back(j);
    m->filtered = false;
    return (int)m->joints.size() - 1;
}

int CoreRagdoll::BodyCount() const { return (int)m->bodies.size(); }
int CoreRagdoll::JointCount() const { return (int)m->joints.size(); }

void CoreRagdoll::SetDrive(int joint, const JointDrive& d) {
    if (joint < 0 || joint >= (int)m->joints.size()) return;
    JointDrive& j = m->joints[(std::size_t)joint].drive;
    j.useDrive = d.useDrive;
    if (Ok(d.target)) j.target = d.target.Normalized();
    j.stiffness = Capped(d.stiffness, 1e7f, 0.0f);
    j.damping = Capped(d.damping, 1e7f, 0.0f);
    j.frequency = Capped(d.frequency, 1000.0f, 0.0f);
    j.dampingRatio = Capped(d.dampingRatio, 10.0f, 1.0f);
    j.maxTorque = Capped(d.maxTorque, 1e7f, 0.0f);
}

void CoreRagdoll::SetJointFriction(float perSecond) { m->friction = Capped(perSecond, 1000.0f, 0.0f); }

void CoreRagdoll::SetActive(bool on) {
    if (on == m->active) return;
    m->active = on;
    m->stale = true;
    if (!m->Live()) return;
    for (Impl::Body& b : m->bodies) {
        if (on) {
            m->world->SetType(b.id, phys::BodyType::Dynamic);
            m->AddColliders(b, colliderUser);
        } else {
            m->DropPin(b);
            m->DropHelper(b);
            m->RemoveColliders(b);
            m->world->SetType(b.id, phys::BodyType::Kinematic);
            m->world->SetLinearVelocity(b.id, phys::V3{});
            m->world->SetAngularVelocity(b.id, phys::V3{});
            b.force = b.torque = Vec3::Zero;
        }
    }
}

bool CoreRagdoll::Active() const { return m->active; }

void CoreRagdoll::SetState(int body, const BodyState& s) {
    if (body < 0 || body >= (int)m->bodies.size() || !m->Live()) return;
    const Impl::Body& b = m->bodies[(std::size_t)body];
    const phys::Xf now = m->world->GetTransform(b.id);
    m->world->SetTransform(b.id, Ok(s.position) ? P3(s.position) : now.p, Ok(s.rotation) ? PQ(s.rotation) : now.q);
    if (!m->active) return;                // (a still body: it is placed, it does not move by itself)
    m->world->SetLinearVelocity(b.id, Ok(s.velocity) ? P3(Vec3::ClampMagnitude(s.velocity, maxSpeed)) : phys::V3{});
    m->world->SetAngularVelocity(b.id, Ok(s.angularVelocity) ? P3(Vec3::ClampMagnitude(s.angularVelocity, maxSpin)) : phys::V3{});
    m->stale = true;
}

BodyState CoreRagdoll::State(int body) const {
    if (body < 0 || body >= (int)m->bodies.size() || !m->Live()) return {Vec3::Zero, Quat::Identity, Vec3::Zero, Vec3::Zero};
    const phys::BodyId id = m->bodies[(std::size_t)body].id;
    const phys::Xf x = m->world->GetTransform(id);
    return {E3(x.p), EQ(x.q), E3(m->world->GetLinearVelocity(id)), E3(m->world->GetAngularVelocity(id))};
}

float CoreRagdoll::Mass(int body) const {
    return body >= 0 && body < (int)m->bodies.size() ? m->bodies[(std::size_t)body].mass : 0.0f;
}

void CoreRagdoll::ApplyImpulse(int body, const Vec3& impulse, const Vec3& atPoint) {
    if (body < 0 || body >= (int)m->bodies.size() || !Ok(impulse) || !m->Live() || !m->active) return;
    const Impl::Body& b = m->bodies[(std::size_t)body];
    const Vec3 J = Vec3::ClampMagnitude(impulse, b.mass * maxSpeed);
    const Vec3 centre = E3(m->world->GetCenterOfMass(b.id));
    const Vec3 r = Ok(atPoint) ? Vec3::ClampMagnitude(atPoint - centre, 2.0f) : Vec3::Zero;
    m->world->ApplyImpulse(b.id, P3(J), P3(centre + r));
    const phys::V3 v = m->world->GetLinearVelocity(b.id), w = m->world->GetAngularVelocity(b.id);
    m->world->SetLinearVelocity(b.id, P3(Vec3::ClampMagnitude(E3(v), maxSpeed)));
    m->world->SetAngularVelocity(b.id, P3(Vec3::ClampMagnitude(E3(w), maxSpin)));
}

void CoreRagdoll::AddForce(int body, const Vec3& f) {
    if (body < 0 || body >= (int)m->bodies.size() || !Ok(f)) return;
    m->bodies[(std::size_t)body].force += f;
}

void CoreRagdoll::AddTorque(int body, const Vec3& t) {
    if (body < 0 || body >= (int)m->bodies.size() || !Ok(t)) return;
    m->bodies[(std::size_t)body].torque += t;
}

void CoreRagdoll::SetPin(int body, const Vec3& localPoint, const Vec3& worldPoint, float stiffness) {
    if (body < 0 || body >= (int)m->bodies.size() || !Ok(localPoint) || !Ok(worldPoint) || !Ok(stiffness)) return;
    if (!m->Live() || !m->active) return;
    Impl::Body& b = m->bodies[(std::size_t)body];
    if (!(stiffness > 0.0f)) { m->DropPin(b); return; }
    if (b.pin == phys::kInvalidId) b.pin = m->WorldJoint(b, worldPoint, localPoint);
    else m->world->EditJoint(b.pin).frameB.p = P3(localPoint);
    b.pinLocal = localPoint;
    b.pinWorld = worldPoint;
    b.pinK = Mathf::Min(stiffness, 1e7f);
}

void CoreRagdoll::ClearPin(int body) {
    if (body >= 0 && body < (int)m->bodies.size() && m->Live()) m->DropPin(m->bodies[(std::size_t)body]);
}

void CoreRagdoll::SetHelper(int body, const Vec3& pos, float posK, const Quat& rot, float rotK, float damping) {
    if (body < 0 || body >= (int)m->bodies.size() || !m->Live() || !m->active) return;
    Impl::Body& b = m->bodies[(std::size_t)body];
    const float kx = Ok(pos) ? Capped(posK, 1e7f, 0.0f) : 0.0f;
    const float kq = Ok(rot) ? Capped(rotK, 1e7f, 0.0f) : 0.0f;
    const float rate = Capped(damping, 1000.0f, 0.0f);
    if (kx <= 0.0f && kq <= 0.0f) { m->DropHelper(b); return; }
    if (b.helper == phys::kInvalidId) {
        // Anchored at the centre of mass, so a pull does not turn the body and the spring sees the body's whole mass.
        const Vec3 local = E3(m->world->GetTransform(b.id).ApplyInv(m->world->GetCenterOfMass(b.id)));
        b.helper = m->WorldJoint(b, Ok(pos) ? pos : Vec3::Zero, local);
    }
    phys::JointDef& d = m->world->EditJoint(b.helper);
    // `damping` is how fast the body's speed dies out (per second): the damping ratio of a spring of this stiffness.
    const float om = std::sqrt(kx / b.mass);
    for (int k = 0; k < 3; ++k) {
        d.linear[k].springFrequency = om / kTwoPi;
        d.linear[k].springDamping = om > 0.0f ? Mathf::Clamp(helperDamping + rate / (2.0f * om), 0.0f, 4.0f) : 0.0f;
        d.linear[k].springTarget = 0.0f;
    }
    if (kx > 0.0f) d.frameA.p = P3(pos);
    const phys::M33 I = m->world->GetInertiaLocal(b.id);
    const float inertia = (I.m[0][0] + I.m[1][1] + I.m[2][2]) / 3.0f;
    d.drive = kq > 0.0f;
    d.driveTarget = kq > 0.0f ? PQ(rot) : phys::Q{};
    d.driveStiffness = kq;
    d.driveDamping = rate * inertia;
    d.driveFrequency = 0.0f;
}

void CoreRagdoll::ClearHelpers() {
    if (!m->Live()) return;
    for (Impl::Body& b : m->bodies) m->DropHelper(b);
}

void CoreRagdoll::Step(float dt, const ContactWorld*) {
    if (!m->Live() || m->bodies.empty() || !Ok(dt) || dt <= 0.0f) return;
    m->stale = true;
    if (!m->active) return;
    dt = Mathf::Min(dt, 0.1f);
    m->FilterRestOverlaps();
    m->CommitDrives();
    auto push = [this] {
        for (Impl::Body& b : m->bodies) {
            if (b.force.SqrMagnitude() > 0.0f) m->world->ApplyForceAtCenter(b.id, P3(b.force));
            if (b.torque.SqrMagnitude() > 0.0f) m->world->ApplyTorque(b.id, P3(b.torque));
        }
    };
    if (m->shared) {
        // The scene steps its world right after the control step: hand the forces over and let it.
        m->ClampSpeeds(maxSpeed, maxSpin);
        m->CommitPins();
        push();
    } else {
        const int n = substeps < 1 ? 1 : (substeps > 64 ? 64 : substeps);
        for (int i = 0; i < n; ++i) {
            m->CommitPins();
            push();
            m->world->Step(dt / (float)n);
            m->ClampSpeeds(maxSpeed, maxSpin);
        }
    }
    for (Impl::Body& b : m->bodies) b.force = b.torque = Vec3::Zero;
}

bool CoreRagdoll::BodyTouching(int body) const {
    if (body < 0 || body >= (int)m->bodies.size() || !m->Live()) return false;
    m->Refresh();
    return m->bodies[(std::size_t)body].touching;
}

int CoreRagdoll::SphereCount(int body) const {
    return body >= 0 && body < (int)m->bodies.size() ? (int)m->bodies[(std::size_t)body].probes.size() : 0;
}

bool CoreRagdoll::SphereTouching(int body, int sphere, Vec3* worldCentre, Vec3* normal, Vec3* point) const {
    if (body < 0 || body >= (int)m->bodies.size() || !m->Live()) return false;
    const Impl::Body& b = m->bodies[(std::size_t)body];
    if (sphere < 0 || sphere >= (int)b.probes.size()) return false;
    m->Refresh();
    const Impl::Probe& p = b.probes[(std::size_t)sphere];
    if (worldCentre) *worldCentre = E3(m->world->GetTransform(b.id).Apply(P3(p.local)));
    if (normal) *normal = p.normal;
    if (point) *point = p.point;
    return p.touching;
}

int CoreRagdoll::Resets() const { return m->resets; }

float CoreRagdoll::JointGap(int joint) const {
    if (joint < 0 || joint >= (int)m->joints.size() || !m->Live()) return 0.0f;
    const phys::JointDef& d = m->world->GetJoint(m->joints[(std::size_t)joint].id);
    return phys::Len(m->world->GetTransform(d.bodyA).Apply(d.frameA.p) - m->world->GetTransform(d.bodyB).Apply(d.frameB.p));
}

float CoreRagdoll::JointLimitExcess(int joint) const {
    if (joint < 0 || joint >= (int)m->joints.size() || !m->Live()) return 0.0f;
    const phys::JointDef& d = m->world->GetJoint(m->joints[(std::size_t)joint].id);
    const phys::Q ra = phys::NormalizeQ(m->world->GetRotation(d.bodyA) * d.frameA.q);
    const phys::Q rb = phys::NormalizeQ(m->world->GetRotation(d.bodyB) * d.frameB.q);
    const phys::Q rel = phys::NormalizeQ(phys::Conj(ra) * rb);
    // The swing / twist split of the core's joint (Physics/Core/Joint.cpp): twist about X first, the rest is swing.
    phys::Q qt{rel.x, 0.0f, 0.0f, rel.w};
    float twist = Mathf::PI;
    const float l = std::sqrt(qt.x * qt.x + qt.w * qt.w);
    if (l > 1e-9f) {
        qt = phys::Q{qt.x / l, 0.0f, 0.0f, qt.w / l};
        if (qt.w < 0.0f) qt = phys::Q{-qt.x, 0.0f, 0.0f, -qt.w};
        twist = 2.0f * std::atan2(qt.x, qt.w);
    } else {
        qt = phys::Q{};
    }
    const phys::V3 sw = phys::ToRotationVector(phys::NormalizeQ(rel * phys::Conj(qt)));
    const float swing = std::sqrt(sw.y * sw.y + sw.z * sw.z);
    const phys::JointAxis& tw = d.angular[0];
    float out = Mathf::Max(0.0f, Mathf::Max(tw.lower - twist, twist - tw.upper));
    if (d.type == phys::JointType::Hinge) out = Mathf::Max(out, swing);
    else out = Mathf::Max(out, swing - Mathf::Max(d.swingSpan1, d.swingSpan2));
    return out;
}

} // namespace dyn
} // namespace okay
