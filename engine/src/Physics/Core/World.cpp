// okay::phys::World: bodies, colliders, broad and narrow phase, islands, sleeping, events and queries.
// (Sources: see World.hpp. The solver is Solver.cpp, joints Joint.cpp.)
#include "WorldInternal.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace okay {
namespace phys {

namespace {
double NowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::uint64_t PairKey(ColliderId a, ColliderId b) {
    const ColliderId lo = a < b ? a : b, hi = a < b ? b : a;
    return ((std::uint64_t)lo << 32) | hi;
}
} // namespace

M33 WorldInvInertia(const M33& invLocal, const Q& q, std::uint8_t lockAng) {
    const M33 R = M33::FromQ(q);
    M33 r = R * invLocal * R.Transposed();
    for (int k = 0; k < 3; ++k)
        if (lockAng & (1u << k))
            for (int j = 0; j < 3; ++j) { r.m[k][j] = 0.0f; r.m[j][k] = 0.0f; }
    return r;
}

World::World() : m(new Impl) { m_tree.margin = 0.05f; }
World::~World() = default;

// ---- bodies ------------------------------------------------------------------------------------------------------------
namespace {
void UpdateMass(World::Impl& w, BodyId id) {
    BodyData& b = w.bodies[id];
    b.mass = 0.0f; b.invMass = 0.0f;
    b.inertiaLocal = M33::Zero(); b.invInertiaLocal = M33::Zero();
    b.localCom = V3{};
    if (b.type != BodyType::Dynamic) { b.com = b.xf.Apply(b.localCom); return; }
    float mass = 0.0f;
    V3 mc;
    for (ColliderId c : b.colliders) {
        const ColliderData& cd = w.colliders[c];
        if (cd.def.sensor) continue;
        const MassProps mp = cd.def.shape.Mass(cd.def.density);
        mass += mp.mass;
        mc += cd.def.local.Apply(mp.com) * mp.mass;
    }
    if (mass > 0.0f) {
        const V3 com = mc * (1.0f / mass);
        M33 I = M33::Zero();
        for (ColliderId c : b.colliders) {
            const ColliderData& cd = w.colliders[c];
            if (cd.def.sensor) continue;
            const MassProps mp = cd.def.shape.Mass(cd.def.density);
            const M33 R = M33::FromQ(cd.def.local.q);
            const M33 Ib = R * mp.inertia * R.Transposed();
            const V3 d = cd.def.local.Apply(mp.com) - com;   // parallel axis: + m (|d|^2 E - d d^T)
            M33 pa = M33::Zero();
            const float dd = LenSq(d);
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) pa.m[i][j] = mp.mass * ((i == j ? dd : 0.0f) - d[i] * d[j]);
            I = I + Ib + pa;
        }
        b.localCom = com;
        b.mass = mass;
        b.inertiaLocal = I;
    } else {   // a dynamic body with no solid shape: a unit ball
        b.mass = 1.0f;
        b.inertiaLocal = M33::Diagonal(V3{0.4f, 0.4f, 0.4f});
    }
    if (b.massSet && b.setMass > 0.0f) {   // the user's mass: same shape of inertia, scaled
        const float s = b.setMass / b.mass;
        b.inertiaLocal = b.inertiaLocal * s;
        b.mass = b.setMass;
    }
    b.invMass = 1.0f / b.mass;
    b.invInertiaLocal = b.inertiaLocal.Inverse();
    b.com = b.xf.Apply(b.localCom);
}

Aabb ColliderWorldBox(const World::Impl& w, const ColliderData& c) {
    const BodyData& b = w.bodies[c.body];
    return c.def.shape.Bounds(b.xf * c.def.local);
}
} // namespace

BodyId World::CreateBody(const BodyDef& d) {
    BodyId id;
    if (!m->freeBodies.empty()) { id = m->freeBodies.back(); m->freeBodies.pop_back(); }
    else { id = (BodyId)m->bodies.size(); m->bodies.emplace_back(); }
    BodyData& b = m->bodies[id];
    b = BodyData{};
    b.alive = true;
    b.type = d.type;
    b.xf = Xf{d.position, NormalizeQ(d.rotation)};
    b.prevXf = b.xf;
    b.v = d.type == BodyType::Static ? V3{} : d.linearVelocity;
    b.w = d.type == BodyType::Static ? V3{} : d.angularVelocity;
    b.linDamp = d.linearDamping;
    b.angDamp = d.angularDamping;
    b.gravityScale = d.gravityScale;
    b.allowSleep = d.allowSleep;
    b.awake = d.type != BodyType::Static && d.awake;
    b.bullet = d.bullet;
    b.lockLin = d.lockLinear;
    b.lockAng = d.lockAngular;
    b.user = d.user;
    UpdateMass(*m, id);
    return id;
}

void World::DestroyBody(BodyId id) {
    if (!IsValid(id)) return;
    const std::vector<JointId> js = m->bodies[id].joints;
    for (JointId j : js) DestroyJoint(j);
    const std::vector<ColliderId> cs = m->bodies[id].colliders;
    for (ColliderId c : cs) RemoveCollider(c);
    m->bodies[id] = BodyData{};
    m->freeBodies.push_back(id);
}

bool World::IsValid(BodyId id) const { return id < m->bodies.size() && m->bodies[id].alive; }
BodyType World::GetType(BodyId id) const { return IsValid(id) ? m->bodies[id].type : BodyType::Static; }
void World::SetType(BodyId id, BodyType t) {
    if (!IsValid(id)) return;
    BodyData& b = m->bodies[id];
    if (b.type == t) return;
    b.type = t;
    if (t == BodyType::Static) { b.v = V3{}; b.w = V3{}; b.awake = false; }
    else b.awake = true;
    b.sleepTime = 0.0f;
    UpdateMass(*m, id);
    for (ColliderId c : b.colliders) {   // look for pairs again (static-static pairs were never made)
        ColliderData& cd = m->colliders[c];
        m_tree.Destroy(cd.proxy);
        cd.box = ColliderWorldBox(*m, cd);
        cd.proxy = m_tree.Create(cd.box, c);
        if (m->movedFlag.size() <= c) m->movedFlag.resize(c + 1, 0);
        if (!m->movedFlag[c]) { m->movedFlag[c] = 1; m->movedProxies.push_back((int)c); }
    }
}
Xf World::GetTransform(BodyId id) const { return IsValid(id) ? m->bodies[id].xf : Xf{}; }
void World::SetTransform(BodyId id, const V3& p, const Q& q) {
    if (!IsValid(id)) return;
    BodyData& b = m->bodies[id];
    b.xf = Xf{p, NormalizeQ(q)};
    b.prevXf = b.xf;
    b.com = b.xf.Apply(b.localCom);
    if (b.type != BodyType::Static) { b.awake = true; b.sleepTime = 0.0f; }
    for (ColliderId c : b.colliders) {
        ColliderData& cd = m->colliders[c];
        cd.box = ColliderWorldBox(*m, cd);
        if (m_tree.Move(cd.proxy, cd.box, V3{})) {
            if (m->movedFlag.size() <= c) m->movedFlag.resize(c + 1, 0);
            if (!m->movedFlag[c]) { m->movedFlag[c] = 1; m->movedProxies.push_back((int)c); }
        }
    }
}
V3 World::GetLinearVelocity(BodyId id) const { return IsValid(id) ? m->bodies[id].v : V3{}; }
V3 World::GetAngularVelocity(BodyId id) const { return IsValid(id) ? m->bodies[id].w : V3{}; }
void World::SetLinearVelocity(BodyId id, const V3& v) {
    if (!IsValid(id) || m->bodies[id].type == BodyType::Static) return;
    m->bodies[id].v = v;
    if (LenSq(v) > 0.0f) Wake(id);
}
void World::SetAngularVelocity(BodyId id, const V3& w) {
    if (!IsValid(id) || m->bodies[id].type == BodyType::Static) return;
    m->bodies[id].w = w;
    if (LenSq(w) > 0.0f) Wake(id);
}
V3 World::GetCenterOfMass(BodyId id) const { return IsValid(id) ? m->bodies[id].com : V3{}; }
float World::GetMass(BodyId id) const { return IsValid(id) ? m->bodies[id].mass : 0.0f; }
M33 World::GetInertiaLocal(BodyId id) const { return IsValid(id) ? m->bodies[id].inertiaLocal : M33::Zero(); }
void World::SetMass(BodyId id, float mass) {
    if (!IsValid(id)) return;
    m->bodies[id].massSet = mass > 0.0f;
    m->bodies[id].setMass = mass;
    UpdateMass(*m, id);
}
void World::SetDamping(BodyId id, float l, float a) { if (IsValid(id)) { m->bodies[id].linDamp = l; m->bodies[id].angDamp = a; } }
void World::SetGravityScale(BodyId id, float s) { if (IsValid(id)) m->bodies[id].gravityScale = s; }
void World::SetLocks(BodyId id, std::uint8_t l, std::uint8_t a) { if (IsValid(id)) { m->bodies[id].lockLin = l; m->bodies[id].lockAng = a; } }
void World::SetBullet(BodyId id, bool on) { if (IsValid(id)) m->bodies[id].bullet = on; }
void World::SetAllowSleep(BodyId id, bool on) { if (IsValid(id)) { m->bodies[id].allowSleep = on; if (!on) Wake(id); } }
void World::ApplyForce(BodyId id, const V3& f, const V3& p) {
    if (!IsValid(id) || m->bodies[id].type != BodyType::Dynamic) return;
    BodyData& b = m->bodies[id];
    b.force += f;
    b.torque += Cross(p - b.com, f);
    Wake(id);
}
void World::ApplyForceAtCenter(BodyId id, const V3& f) {
    if (!IsValid(id) || m->bodies[id].type != BodyType::Dynamic) return;
    m->bodies[id].force += f;
    Wake(id);
}
void World::ApplyTorque(BodyId id, const V3& t) {
    if (!IsValid(id) || m->bodies[id].type != BodyType::Dynamic) return;
    m->bodies[id].torque += t;
    Wake(id);
}
void World::ApplyImpulse(BodyId id, const V3& j, const V3& p) {
    if (!IsValid(id) || m->bodies[id].type != BodyType::Dynamic) return;
    BodyData& b = m->bodies[id];
    b.v += j * b.invMass;
    b.w += WorldInvInertia(b.invInertiaLocal, b.xf.q, b.lockAng) * Cross(p - b.com, j);
    Wake(id);
}
void World::ApplyAngularImpulse(BodyId id, const V3& j) {
    if (!IsValid(id) || m->bodies[id].type != BodyType::Dynamic) return;
    BodyData& b = m->bodies[id];
    b.w += WorldInvInertia(b.invInertiaLocal, b.xf.q, b.lockAng) * j;
    Wake(id);
}
bool World::IsAwake(BodyId id) const { return IsValid(id) && m->bodies[id].awake; }
void World::Wake(BodyId id) {
    if (!IsValid(id) || m->bodies[id].type == BodyType::Static) return;
    m->bodies[id].awake = true;
    m->bodies[id].sleepTime = 0.0f;
}
void World::Sleep(BodyId id) {
    if (!IsValid(id) || m->bodies[id].type == BodyType::Static) return;
    BodyData& b = m->bodies[id];
    b.awake = false;
    b.v = V3{}; b.w = V3{};
}
std::uint64_t World::GetUser(BodyId id) const { return IsValid(id) ? m->bodies[id].user : 0; }
Xf World::InterpolatedTransform(BodyId id, float alpha) const {
    if (!IsValid(id)) return Xf{};
    const BodyData& b = m->bodies[id];
    alpha = std::min(std::max(alpha, 0.0f), 1.0f);
    Xf r;
    r.p = b.prevXf.p + (b.xf.p - b.prevXf.p) * alpha;
    Q q1 = b.xf.q;
    const Q& q0 = b.prevXf.q;
    if (q0.x * q1.x + q0.y * q1.y + q0.z * q1.z + q0.w * q1.w < 0.0f) q1 = Q{-q1.x, -q1.y, -q1.z, -q1.w};
    r.q = NormalizeQ(Q{q0.x + (q1.x - q0.x) * alpha, q0.y + (q1.y - q0.y) * alpha, q0.z + (q1.z - q0.z) * alpha,
                       q0.w + (q1.w - q0.w) * alpha});
    return r;
}

// ---- colliders ---------------------------------------------------------------------------------------------------------
ColliderId World::AddCollider(BodyId body, const ColliderDef& def) {
    if (!IsValid(body)) return kInvalidId;
    ColliderId id;
    if (!m->freeColliders.empty()) { id = m->freeColliders.back(); m->freeColliders.pop_back(); }
    else { id = (ColliderId)m->colliders.size(); m->colliders.emplace_back(); }
    ColliderData& c = m->colliders[id];
    c = ColliderData{};
    c.alive = true;
    c.body = body;
    c.def = def;
    c.def.local.q = NormalizeQ(def.local.q);
    m->bodies[body].colliders.push_back(id);
    UpdateMass(*m, body);
    c.box = ColliderWorldBox(*m, c);
    c.proxy = m_tree.Create(c.box, id);
    if (m->movedFlag.size() <= id) m->movedFlag.resize(id + 1, 0);
    if (!m->movedFlag[id]) { m->movedFlag[id] = 1; m->movedProxies.push_back((int)id); }
    return id;
}

void World::RemoveCollider(ColliderId id) {
    if (!IsValidCollider(id)) return;
    ColliderData& c = m->colliders[id];
    // contacts that used it end now
    for (ContactPair& cp : m->contacts) {
        if (!cp.alive || (cp.a != id && cp.b != id)) continue;
        if (cp.touching) {
            ContactEvent e;
            e.a = cp.a; e.b = cp.b;
            e.bodyA = m->colliders[cp.a].body; e.bodyB = m->colliders[cp.b].body;
            e.normal = cp.normal; e.sensor = cp.sensor;
            m_end.push_back(e);
        }
        // wake the other side so it notices the support went away
        const ColliderId other = cp.a == id ? cp.b : cp.a;
        Wake(m->colliders[other].body);
        cp.alive = false;
        m->contactIndex.erase(cp.key);
    }
    m_tree.Destroy(c.proxy);
    BodyData& b = m->bodies[c.body];
    b.colliders.erase(std::remove(b.colliders.begin(), b.colliders.end(), id), b.colliders.end());
    const BodyId body = c.body;
    c = ColliderData{};
    if (id < m->movedFlag.size()) m->movedFlag[id] = 0;
    m->movedProxies.erase(std::remove(m->movedProxies.begin(), m->movedProxies.end(), (int)id), m->movedProxies.end());
    m->freeColliders.push_back(id);
    UpdateMass(*m, body);
}
bool World::IsValidCollider(ColliderId id) const { return id < m->colliders.size() && m->colliders[id].alive; }
BodyId World::ColliderBody(ColliderId id) const { return IsValidCollider(id) ? m->colliders[id].body : kInvalidId; }
const ColliderDef& World::GetCollider(ColliderId id) const { static ColliderDef none; return IsValidCollider(id) ? m->colliders[id].def : none; }
void World::SetColliderMaterial(ColliderId id, float f, float r, float roll) {
    if (!IsValidCollider(id)) return;
    m->colliders[id].def.friction = f; m->colliders[id].def.restitution = r; m->colliders[id].def.rollingResistance = roll;
}
void World::SetColliderFilter(ColliderId id, std::uint32_t layer, std::uint32_t mask) {
    if (!IsValidCollider(id)) return;
    m->colliders[id].def.layer = layer; m->colliders[id].def.mask = mask;
}
void World::SetColliderSensor(ColliderId id, bool s) {
    if (!IsValidCollider(id)) return;
    m->colliders[id].def.sensor = s;
    UpdateMass(*m, m->colliders[id].body);
    for (ContactPair& cp : m->contacts) if (cp.alive && (cp.a == id || cp.b == id)) cp.sensor = cp.sensor || s;
}
Aabb World::ColliderBounds(ColliderId id) const { return IsValidCollider(id) ? m->colliders[id].box : Aabb{}; }

// ---- joints ------------------------------------------------------------------------------------------------------------
void SetupJoint(JointDef& d) {
    // the anchor is held in place except along a slider's axis (which keeps the Free or Limited mode it was given)
    for (int k = (d.type == JointType::Slider ? 1 : 0); k < 3; ++k) d.linear[k].mode = AxisMode::Locked;
    switch (d.type) {
        case JointType::Fixed:
            for (int k = 0; k < 3; ++k) d.angular[k].mode = AxisMode::Locked;
            d.useCone = false;
            break;
        case JointType::Ball:
            for (int k = 0; k < 3; ++k) d.angular[k].mode = AxisMode::Free;
            d.useCone = false;
            break;
        case JointType::Hinge:
            if (d.angular[0].mode == AxisMode::Locked) d.angular[0].mode = AxisMode::Free;
            d.angular[1].mode = AxisMode::Locked;
            d.angular[2].mode = AxisMode::Locked;
            d.useCone = false;
            break;
        case JointType::Slider:
            if (d.linear[0].mode == AxisMode::Locked) d.linear[0].mode = AxisMode::Free;
            for (int k = 0; k < 3; ++k) d.angular[k].mode = AxisMode::Locked;
            d.useCone = false;
            break;
        case JointType::ConeTwist:
            if (d.angular[0].mode == AxisMode::Locked) d.angular[0].mode = AxisMode::Limited;
            d.angular[1].mode = AxisMode::Free;
            d.angular[2].mode = AxisMode::Free;
            d.useCone = true;
            break;
        case JointType::Distance:
            for (int k = 0; k < 3; ++k) { d.linear[k].mode = AxisMode::Free; d.angular[k].mode = AxisMode::Free; }
            d.useCone = false;
            break;
        case JointType::SixDof: break;
    }
}

JointId World::CreateJoint(const JointDef& def) {
    if (def.bodyA != kInvalidId && !IsValid(def.bodyA)) return kInvalidId;
    if (def.bodyB != kInvalidId && !IsValid(def.bodyB)) return kInvalidId;
    if (def.bodyA == def.bodyB) return kInvalidId;
    JointId id;
    if (!m->freeJoints.empty()) { id = m->freeJoints.back(); m->freeJoints.pop_back(); }
    else { id = (JointId)m->joints.size(); m->joints.emplace_back(); }
    JointData& j = m->joints[id];
    j = JointData{};
    j.alive = true;
    j.def = def;
    j.def.frameA.q = NormalizeQ(def.frameA.q);
    j.def.frameB.q = NormalizeQ(def.frameB.q);
    j.def.driveTarget = NormalizeQ(def.driveTarget);
    if (def.bodyA != kInvalidId) { m->bodies[def.bodyA].joints.push_back(id); Wake(def.bodyA); }
    if (def.bodyB != kInvalidId) { m->bodies[def.bodyB].joints.push_back(id); Wake(def.bodyB); }
    if (!def.collideConnected && def.bodyA != kInvalidId && def.bodyB != kInvalidId) {
        // drop any contact between the two bodies now
        for (ContactPair& cp : m->contacts) {
            if (!cp.alive) continue;
            const BodyId ba = m->colliders[cp.a].body, bb = m->colliders[cp.b].body;
            if ((ba == def.bodyA && bb == def.bodyB) || (ba == def.bodyB && bb == def.bodyA)) {
                cp.alive = false;
                m->contactIndex.erase(cp.key);
            }
        }
    }
    return id;
}
void World::DestroyJoint(JointId id) {
    if (!IsValidJoint(id)) return;
    JointData& j = m->joints[id];
    for (BodyId b : {j.def.bodyA, j.def.bodyB}) {
        if (b == kInvalidId || !IsValid(b)) continue;
        auto& v = m->bodies[b].joints;
        v.erase(std::remove(v.begin(), v.end(), id), v.end());
        Wake(b);
        // the pair may collide again: let its colliders look for pairs
        for (ColliderId c : m->bodies[b].colliders) {
            if (m->movedFlag.size() <= c) m->movedFlag.resize(c + 1, 0);
            if (!m->movedFlag[c]) { m->movedFlag[c] = 1; m->movedProxies.push_back((int)c); }
        }
    }
    j = JointData{};
    m->freeJoints.push_back(id);
}
bool World::IsValidJoint(JointId id) const { return id < m->joints.size() && m->joints[id].alive; }
JointDef& World::EditJoint(JointId id) {
    static JointDef none;
    if (!IsValidJoint(id)) return none;
    JointData& j = m->joints[id];
    Wake(j.def.bodyA);
    Wake(j.def.bodyB);
    return j.def;
}
const JointDef& World::GetJoint(JointId id) const { static JointDef none; return IsValidJoint(id) ? m->joints[id].def : none; }
bool World::IsJointBroken(JointId id) const { return IsValidJoint(id) && m->joints[id].broken; }
V3 World::JointForce(JointId id) const { return IsValidJoint(id) ? m->joints[id].lastForce : V3{}; }
V3 World::JointTorque(JointId id) const { return IsValidJoint(id) ? m->joints[id].lastTorque : V3{}; }
V3 World::JointAngles(JointId id) const {
    if (!IsValidJoint(id)) return V3{};
    const JointDef& d = m->joints[id].def;
    const Q qa = d.bodyA == kInvalidId ? Q{} : m->bodies[d.bodyA].xf.q;
    const Q qb = d.bodyB == kInvalidId ? Q{} : m->bodies[d.bodyB].xf.q;
    return JointAnglesOf(qa, qb, d);
}

// ---- step --------------------------------------------------------------------------------------------------------------
namespace {

bool CanCollide(const World& world, const World::Impl& w, ColliderId ia, ColliderId ib) {
    const ColliderData& a = w.colliders[ia];
    const ColliderData& b = w.colliders[ib];
    if (a.body == b.body) return false;
    const BodyData& ba = w.bodies[a.body];
    const BodyData& bb = w.bodies[b.body];
    if (ba.type != BodyType::Dynamic && bb.type != BodyType::Dynamic && !a.def.sensor && !b.def.sensor) return false;
    if (ba.type == BodyType::Static && bb.type == BodyType::Static) return false;
    if (!((a.def.mask >> (b.def.layer & 31)) & 1u) || !((b.def.mask >> (a.def.layer & 31)) & 1u)) return false;
    // joined bodies that should not collide
    const BodyData& small = ba.joints.size() <= bb.joints.size() ? ba : bb;
    const BodyId other = &small == &ba ? b.body : a.body;
    for (JointId j : small.joints) {
        const JointData& jd = w.joints[j];
        if (!jd.alive || jd.def.collideConnected) continue;
        if (jd.def.bodyA == other || jd.def.bodyB == other) return false;
    }
    (void)world;
    return true;
}

// The speculative margin of a body: how far its surface can move in a step.
float MotionMargin(const BodyData& b, const ColliderData& c, float dt) {
    if (b.type == BodyType::Static || !b.awake) return 0.0f;
    const float reach = Len(b.xf.Apply(c.def.local.p) - b.com) + c.def.shape.OuterRadius();
    return (Len(b.v) + Len(b.w) * reach) * dt;
}

void MatchWarm(ContactPair& cp, const ContactPointState* old, int oldCount, float tol) {
    bool used[4] = {false, false, false, false};
    for (int i = 0; i < cp.count; ++i) {
        ContactPointState& p = cp.pts[i];
        int best = -1;
        float bd = tol * tol;
        for (int j = 0; j < oldCount; ++j) {
            if (used[j]) continue;
            const float d = LenSq(old[j].localA - p.localA) + LenSq(old[j].localB - p.localB);
            if (d < bd) { bd = d; best = j; }
        }
        if (best >= 0) {
            used[best] = true;
            p.normalImpulse = old[best].normalImpulse;
            p.tangentImpulse[0] = old[best].tangentImpulse[0];
            p.tangentImpulse[1] = old[best].tangentImpulse[1];
            p.warm = true;
        }
    }
}

float Combine(float a, float b) { return std::sqrt(std::max(a, 0.0f) * std::max(b, 0.0f)); }

} // namespace

void World::Step(float dt) {
    m_begin.clear();
    m_end.clear();
    m_stats = WorldStats{};
    if (!(dt > 0.0f) || !std::isfinite(dt)) return;
    Impl& w = *m;
    const double t0 = NowMs();

    for (BodyData& b : w.bodies) if (b.alive) b.prevXf = b.xf;

    // 1. new pairs: colliders whose fat box changed look for partners in the tree (sorted, so the order is fixed)
    std::sort(w.movedProxies.begin(), w.movedProxies.end());
    std::vector<std::uint64_t> fresh;
    for (int ci : w.movedProxies) {
        const ColliderId c = (ColliderId)ci;
        if (c < w.movedFlag.size()) w.movedFlag[c] = 0;
        if (!IsValidCollider(c)) continue;
        m_tree.Query(m_tree.FatAabb(w.colliders[c].proxy), [&](int proxy) {
            const ColliderId o = (ColliderId)m_tree.User(proxy);
            if (o == c) return true;
            const std::uint64_t key = PairKey(c, o);
            if (w.contactIndex.count(key)) return true;
            if (!CanCollide(*this, w, c, o)) return true;
            if (shouldCollide && !shouldCollide(std::min(c, o), std::max(c, o))) return true;
            fresh.push_back(key);
            return true;
        });
    }
    m_stats.proxiesMoved = (int)w.movedProxies.size();
    w.movedProxies.clear();
    std::sort(fresh.begin(), fresh.end());
    fresh.erase(std::unique(fresh.begin(), fresh.end()), fresh.end());
    if (!fresh.empty()) {
        for (std::uint64_t key : fresh) {
            ContactPair cp;
            cp.key = key;
            cp.a = (ColliderId)(key >> 32);
            cp.b = (ColliderId)(key & 0xFFFFFFFFu);
            const ColliderDef& da = w.colliders[cp.a].def;
            const ColliderDef& db = w.colliders[cp.b].def;
            cp.sensor = da.sensor || db.sensor;
            w.contacts.push_back(cp);
        }
        m_stats.pairsCreated = (int)fresh.size();
    }
    // keep contacts sorted by key, dead ones dropped
    {
        std::vector<ContactPair> keep;
        keep.reserve(w.contacts.size());
        for (ContactPair& cp : w.contacts) if (cp.alive) keep.push_back(cp);
        std::sort(keep.begin(), keep.end(), [](const ContactPair& x, const ContactPair& y) { return x.key < y.key; });
        w.contacts.swap(keep);
        w.contactIndex.clear();
        w.contactIndex.reserve(w.contacts.size() * 2);
        for (std::size_t i = 0; i < w.contacts.size(); ++i) w.contactIndex[w.contacts[i].key] = (int)i;
    }
    const double t1 = NowMs();

    // 2. narrow phase: each pair writes only itself, so pairs can run on many threads
    const float spec0 = speculativeDistance;
    const bool cont = continuous;
    auto narrow = [&](int begin, int end) {
        for (int i = begin; i < end; ++i) {
            ContactPair& cp = w.contacts[(std::size_t)i];
            const ColliderData& ca = w.colliders[cp.a];
            const ColliderData& cb = w.colliders[cp.b];
            const BodyData& ba = w.bodies[ca.body];
            const BodyData& bb = w.bodies[cb.body];
            const bool aActive = ba.awake && ba.type != BodyType::Static;
            const bool bActive = bb.awake && bb.type != BodyType::Static;
            cp.active = aActive || bActive;
            if (!cp.active) continue;   // both asleep or fixed: keep last step's result
            cp.wasTouching = cp.touching;
            if (!m_tree.FatAabb(ca.proxy).Overlaps(m_tree.FatAabb(cb.proxy))) {
                cp.alive = false;   // boxes apart: the pair ends (the end event is sent below)
                cp.count = 0;
                cp.touching = false;
                continue;
            }
            const Xf xa = ba.xf * ca.def.local, xb = bb.xf * cb.def.local;
            float spec = cp.sensor ? 0.0f : spec0;
            if (cont && !cp.sensor) {
                float mm = MotionMargin(ba, ca, dt) + MotionMargin(bb, cb, dt);
                if (ba.bullet || bb.bullet) mm *= 1.5f;
                spec += mm;
            }
            Manifold man;
            const bool hit = Collide(ca.def.shape, xa, cb.def.shape, xb, spec, man);
            ContactPointState old[4];
            const int oldCount = cp.count;
            for (int k = 0; k < oldCount; ++k) old[k] = cp.pts[k];
            cp.count = 0;
            if (!hit) { cp.touching = false; continue; }
            cp.normal = man.normal;
            cp.mixed = man.mixedNormals;
            cp.count = man.count;
            float minSep = 1e30f;
            for (int k = 0; k < man.count; ++k) {
                ContactPointState& p = cp.pts[k];
                p = ContactPointState{};
                p.localA = ba.xf.ApplyInv(man.pts[k].pA);
                p.localB = bb.xf.ApplyInv(man.pts[k].pB);
                p.separation = man.pts[k].separation;
                p.normal = man.pts[k].normal;
                minSep = std::min(minSep, p.separation);
            }
            if (cp.sensor) { cp.touching = minSep <= 0.0f; cp.count = 0; continue; }
            cp.touching = minSep <= linearSlop;
            cp.friction = Combine(ca.def.friction, cb.def.friction);
            cp.restitution = std::max(ca.def.restitution, cb.def.restitution);
            cp.rolling = std::max(ca.def.rollingResistance, cb.def.rollingResistance);
            const float tol = 0.05f * std::max(0.2f, std::min(ca.def.shape.OuterRadius(), cb.def.shape.OuterRadius()) * 2.0f);
            MatchWarm(cp, old, oldCount, tol);
        }
    };
    const int nContacts = (int)w.contacts.size();
    if (m_parallel && nContacts > 256) m_parallel(nContacts, narrow);
    else narrow(0, nContacts);

    // events (serial, key order) and waking: a moving body touching a sleeping one wakes it
    for (ContactPair& cp : w.contacts) {
        if (!cp.active) continue;
        const BodyId ba = w.colliders[cp.a].body, bb = w.colliders[cp.b].body;
        ContactEvent e;
        e.a = cp.a; e.b = cp.b; e.bodyA = ba; e.bodyB = bb; e.normal = cp.normal; e.sensor = cp.sensor;
        if (cp.count > 0) {
            V3 p; float best = 1e30f;
            for (int k = 0; k < cp.count; ++k)
                if (cp.pts[k].separation < best) { best = cp.pts[k].separation; p = (w.bodies[ba].xf.Apply(cp.pts[k].localA) + w.bodies[bb].xf.Apply(cp.pts[k].localB)) * 0.5f; }
            e.point = p;
        } else {
            e.point = (w.colliders[cp.a].box.Center() + w.colliders[cp.b].box.Center()) * 0.5f;
        }
        if (cp.touching && !cp.wasTouching) {
            const V3 vrel = (w.bodies[bb].v + Cross(w.bodies[bb].w, e.point - w.bodies[bb].com)) -
                            (w.bodies[ba].v + Cross(w.bodies[ba].w, e.point - w.bodies[ba].com));
            cp.approach = std::max(0.0f, -Dot(vrel, cp.normal));
            e.approachSpeed = cp.approach;
            m_begin.push_back(e);
        } else if (!cp.touching && cp.wasTouching) {
            m_end.push_back(e);
        }
        if (!cp.alive) w.contactIndex.erase(cp.key);
    }
    const double t2 = NowMs();

    // 3. islands: union-find over awake dynamic bodies through solved contacts and joints; sleeping dynamic bodies
    //    reached from an awake one wake up (a whole resting group wakes together)
    const std::size_t nb = w.bodies.size();
    std::vector<int> parent(nb);
    for (std::size_t i = 0; i < nb; ++i) parent[i] = (int)i;
    auto find = [&](int x) { while (parent[(std::size_t)x] != x) { parent[(std::size_t)x] = parent[(std::size_t)parent[(std::size_t)x]]; x = parent[(std::size_t)x]; } return x; };
    auto unite = [&](int x, int y) { x = find(x); y = find(y); if (x == y) return; if (x < y) parent[(std::size_t)y] = x; else parent[(std::size_t)x] = y; };
    auto isDyn = [&](BodyId b) { return b != kInvalidId && w.bodies[b].alive && w.bodies[b].type == BodyType::Dynamic; };
    // wake propagation: repeat until nothing new wakes (contacts and joints between a sleeper and an awake body)
    for (bool changed = true; changed;) {
        changed = false;
        for (const ContactPair& cp : w.contacts) {
            if (!cp.alive || cp.sensor || cp.count == 0) continue;
            const BodyId ba = w.colliders[cp.a].body, bb = w.colliders[cp.b].body;
            BodyData& A = w.bodies[ba];
            BodyData& B = w.bodies[bb];
            const bool aMoves = A.awake && A.type != BodyType::Static;
            const bool bMoves = B.awake && B.type != BodyType::Static;
            if (aMoves && !B.awake && B.type == BodyType::Dynamic && cp.touching) { B.awake = true; B.sleepTime = 0; changed = true; }
            if (bMoves && !A.awake && A.type == BodyType::Dynamic && cp.touching) { A.awake = true; A.sleepTime = 0; changed = true; }
        }
        for (const JointData& j : w.joints) {
            if (!j.alive || j.broken) continue;
            const BodyId a = j.def.bodyA, b = j.def.bodyB;
            const bool aw = a != kInvalidId && w.bodies[a].awake, bw = b != kInvalidId && w.bodies[b].awake;
            if (aw && isDyn(b) && !w.bodies[b].awake) { w.bodies[b].awake = true; w.bodies[b].sleepTime = 0; changed = true; }
            if (bw && isDyn(a) && !w.bodies[a].awake) { w.bodies[a].awake = true; w.bodies[a].sleepTime = 0; changed = true; }
        }
    }
    // contacts that just woke up a body were not looked at this step: look at them now (same code path, serial)
    {
        std::vector<int> late;
        for (std::size_t i = 0; i < w.contacts.size(); ++i) {
            const ContactPair& cp = w.contacts[i];
            if (!cp.alive || cp.active) continue;
            const BodyData& A = w.bodies[w.colliders[cp.a].body];
            const BodyData& B = w.bodies[w.colliders[cp.b].body];
            if ((A.awake && A.type != BodyType::Static) || (B.awake && B.type != BodyType::Static)) late.push_back((int)i);
        }
        for (int i : late) {
            const bool wasT = w.contacts[(std::size_t)i].touching;
            narrow(i, i + 1);
            ContactPair& cp = w.contacts[(std::size_t)i];
            cp.wasTouching = wasT;   // (it stayed in contact while asleep: no new begin event)
            if (!cp.alive) w.contactIndex.erase(cp.key);
        }
    }
    for (const ContactPair& cp : w.contacts) {
        if (!cp.alive || cp.sensor || cp.count == 0) continue;
        const BodyId ba = w.colliders[cp.a].body, bb = w.colliders[cp.b].body;
        if (isDyn(ba) && isDyn(bb) && w.bodies[ba].awake && w.bodies[bb].awake) unite((int)ba, (int)bb);
    }
    for (std::size_t ji = 0; ji < w.joints.size(); ++ji) {
        const JointData& j = w.joints[ji];
        if (!j.alive || j.broken) continue;
        if (isDyn(j.def.bodyA) && isDyn(j.def.bodyB) && w.bodies[j.def.bodyA].awake && w.bodies[j.def.bodyB].awake)
            unite((int)j.def.bodyA, (int)j.def.bodyB);
    }
    std::vector<int> islandOf(nb, -1);
    std::vector<IslandWork> islands;
    for (std::size_t i = 0; i < nb; ++i) {
        BodyData& b = w.bodies[i];
        b.island = -1;
        if (!b.alive || b.type != BodyType::Dynamic || !b.awake) continue;
        const int root = find((int)i);
        if (islandOf[(std::size_t)root] < 0) { islandOf[(std::size_t)root] = (int)islands.size(); islands.emplace_back(); }
        const int is = islandOf[(std::size_t)root];
        b.island = is;
        islands[(std::size_t)is].bodies.push_back((BodyId)i);
    }
    for (std::size_t i = 0; i < w.contacts.size(); ++i) {
        const ContactPair& cp = w.contacts[i];
        if (!cp.alive || cp.sensor || cp.count == 0) continue;
        const BodyData& A = w.bodies[w.colliders[cp.a].body];
        const BodyData& B = w.bodies[w.colliders[cp.b].body];
        const int is = A.island >= 0 ? A.island : B.island;
        if (is >= 0) islands[(std::size_t)is].contacts.push_back((int)i);
    }
    for (std::size_t ji = 0; ji < w.joints.size(); ++ji) {
        const JointData& j = w.joints[ji];
        if (!j.alive || j.broken) continue;
        const int ia = j.def.bodyA != kInvalidId ? w.bodies[j.def.bodyA].island : -1;
        const int ib = j.def.bodyB != kInvalidId ? w.bodies[j.def.bodyB].island : -1;
        const int is = ia >= 0 ? ia : ib;
        if (is >= 0) islands[(std::size_t)is].joints.push_back((JointId)ji);
    }
    // kinematic bodies move on their own
    for (BodyData& b : w.bodies) {
        if (!b.alive || b.type != BodyType::Kinematic) continue;
        b.com += b.v * dt;
        b.xf.q = Integrate(b.xf.q, b.w, dt);
        b.xf.p = b.com - Rotate(b.xf.q, b.localCom);
        b.awake = LenSq(b.v) > 0.0f || LenSq(b.w) > 0.0f;
    }

    // 4. solve islands (in parallel when there is a ParallelFor; each island touches only its own bodies, contacts and joints)
    SolveSettings ss;
    ss.dt = dt; ss.gravity = gravity;
    ss.velocityIterations = std::max(1, velocityIterations);
    ss.positionIterations = std::max(0, positionIterations);
    ss.warmStarting = warmStarting; ss.frictionCone = frictionCone; ss.blockSolver = blockSolver;
    ss.linearSlop = linearSlop; ss.positionCorrection = positionCorrection; ss.maxCorrection = maxCorrection;
    ss.restitutionThreshold = restitutionThreshold;
    ss.maxLinearSpeed = maxLinearSpeed; ss.maxAngularSpeed = maxAngularSpeed;
    ss.sleepLinear = sleepLinear; ss.sleepAngular = sleepAngular;
    auto solve = [&](int begin, int end) { for (int i = begin; i < end; ++i) SolveIsland(w, islands[(std::size_t)i], ss); };
    if (m_parallel && islands.size() > 1) m_parallel((int)islands.size(), solve);
    else solve(0, (int)islands.size());

    // sleeping: a whole island sleeps when every body in it has been slow long enough
    for (const IslandWork& is : islands) {
        bool canSleep = enableSleep;
        float minTime = 1e30f;
        for (BodyId b : is.bodies) {
            const BodyData& bd = w.bodies[b];
            if (!bd.allowSleep) canSleep = false;
            minTime = std::min(minTime, bd.sleepTime);
        }
        if (canSleep && minTime >= timeToSleep)
            for (BodyId b : is.bodies) { BodyData& bd = w.bodies[b]; bd.awake = false; bd.v = V3{}; bd.w = V3{}; }
    }
    const double t3 = NowMs();

    // 5. move the moved colliders in the tree (their boxes stretched by this step's motion for the next step)
    for (std::size_t ci = 0; ci < w.colliders.size(); ++ci) {
        ColliderData& c = w.colliders[ci];
        if (!c.alive) continue;
        const BodyData& b = w.bodies[c.body];
        if (b.type == BodyType::Static) continue;
        if (b.xf.p == b.prevXf.p && b.xf.q == b.prevXf.q) continue;
        c.box = ColliderWorldBox(w, c);
        const V3 disp = b.xf.p - b.prevXf.p;
        if (m_tree.Move(c.proxy, c.box, disp)) {
            if (w.movedFlag.size() <= ci) w.movedFlag.resize(ci + 1, 0);
            if (!w.movedFlag[ci]) { w.movedFlag[ci] = 1; w.movedProxies.push_back((int)ci); }
        }
    }

    // stats
    m_stats.islands = (int)islands.size();
    for (const BodyData& b : w.bodies) {
        if (!b.alive) continue;
        ++m_stats.bodies;
        if (b.awake && b.type == BodyType::Dynamic) ++m_stats.awake;
    }
    for (const ContactPair& cp : w.contacts) {
        if (!cp.alive) continue;
        ++m_stats.contacts;
        if (cp.touching) ++m_stats.touching;
        m_stats.points += cp.count;
        for (int k = 0; k < cp.count; ++k) m_stats.warmPoints += cp.pts[k].warm;
    }
    for (const JointData& j : w.joints) if (j.alive) ++m_stats.joints;
    m_stats.broadMs = t1 - t0;
    m_stats.narrowMs = t2 - t1;
    m_stats.solveMs = t3 - t2;
}

int World::Advance(float frameDt, float fixedDt, int maxSteps) {
    if (!(fixedDt > 0.0f)) return 0;
    if (frameDt > 0.0f && std::isfinite(frameDt)) m_accumulator += frameDt;
    int n = 0;
    while (m_accumulator >= fixedDt && n < maxSteps) { Step(fixedDt); m_accumulator -= fixedDt; ++n; }
    if (m_accumulator >= fixedDt) m_accumulator = std::fmod(m_accumulator, fixedDt);   // too far behind: drop the rest
    m_alpha = m_accumulator / fixedDt;
    return n;
}

void World::ForEachTouching(const std::function<void(const ContactEvent&)>& fn) const {
    for (const ContactPair& cp : m->contacts) {
        if (!cp.alive || !cp.touching) continue;
        ContactEvent e;
        e.a = cp.a; e.b = cp.b;
        e.bodyA = m->colliders[cp.a].body; e.bodyB = m->colliders[cp.b].body;
        e.normal = cp.normal; e.sensor = cp.sensor; e.approachSpeed = cp.approach;
        if (cp.count > 0) {
            int best = 0;
            for (int k = 1; k < cp.count; ++k) if (cp.pts[k].separation < cp.pts[best].separation) best = k;
            e.point = (m->bodies[e.bodyA].xf.Apply(cp.pts[best].localA) + m->bodies[e.bodyB].xf.Apply(cp.pts[best].localB)) * 0.5f;
        } else {
            e.point = (m->colliders[cp.a].box.Center() + m->colliders[cp.b].box.Center()) * 0.5f;
        }
        fn(e);
    }
}

bool World::GetManifold(ColliderId a, ColliderId b, Manifold& out) const {
    auto it = m->contactIndex.find(PairKey(a, b));
    if (it == m->contactIndex.end()) return false;
    const ContactPair& cp = m->contacts[(std::size_t)it->second];
    if (!cp.alive || !cp.touching || cp.count == 0) return false;
    const bool flip = a > b;
    out.normal = flip ? -cp.normal : cp.normal;
    out.count = cp.count;
    const BodyData& A = m->bodies[m->colliders[cp.a].body];
    const BodyData& B = m->bodies[m->colliders[cp.b].body];
    for (int k = 0; k < cp.count; ++k) {
        const V3 pa = A.xf.Apply(cp.pts[k].localA), pb = B.xf.Apply(cp.pts[k].localB);
        out.pts[k].pA = flip ? pb : pa;
        out.pts[k].pB = flip ? pa : pb;
        out.pts[k].separation = cp.pts[k].separation;
    }
    return true;
}

// ---- queries -------------------------------------------------------------------------------------------------------------
namespace {
// Exact ray against a shape in its own frame (unit direction). Analytic for sphere, box, capsule and plane; the others by
// conservative advancement with GJK (a point moved along the ray until it reaches the surface: B. Mirtich, "Impulse-based
// dynamic simulation of rigid body systems", PhD thesis 1996, ch. 3; Ericson 9.5 for the distance).
bool RayShape(const Shape& s, const V3& o, const V3& d, float maxT, float& tOut, V3& nOut) {
    switch (s.type) {
        case ShapeType::Sphere: {
            const float b = Dot(o, d), c = LenSq(o) - s.radius * s.radius;
            if (c > 0.0f && b > 0.0f) return false;
            const float disc = b * b - c;
            if (disc < 0.0f) return false;
            float t = -b - std::sqrt(disc);
            if (t < 0.0f) t = 0.0f;
            if (t > maxT) return false;
            tOut = t; nOut = Normalize(o + d * t, -d);
            return true;
        }
        case ShapeType::Box: {
            float t0 = 0.0f, t1 = maxT; int axis = -1; float sgn = 1.0f;
            for (int k = 0; k < 3; ++k) {
                const float h = s.halfExtents[k];
                if (std::fabs(d[k]) < 1e-12f) { if (o[k] < -h || o[k] > h) return false; continue; }
                const float inv = 1.0f / d[k];
                float a = (-h - o[k]) * inv, b = (h - o[k]) * inv;
                float sg = -1.0f;
                if (a > b) { std::swap(a, b); sg = 1.0f; }
                if (a > t0) { t0 = a; axis = k; sgn = sg; }
                if (b < t1) t1 = b;
                if (t0 > t1) return false;
            }
            tOut = t0;
            nOut = V3{};
            if (axis >= 0) nOut[axis] = sgn; else nOut = -d;   // started inside
            return true;
        }
        case ShapeType::Mesh: {   // ray against each triangle near the ray (T. Moller and B. Trumbore, "Fast, minimum storage
                                  // ray-triangle intersection", J. Graphics Tools 1997)
            if (!s.mesh) return false;
            const float reach = std::min(maxT, 1e6f);
            Aabb box{Min(o, o + d * reach), Max(o, o + d * reach)};
            std::vector<Tri3> tris;
            // long rays: walk the ray in pieces so the triangle list stays short
            const int pieces = std::max(1, std::min(64, (int)(reach / 16.0f)));
            bool hit = false;
            float best = maxT;
            for (int pc = 0; pc < pieces && !hit; ++pc) {
                const float t0 = reach * (float)pc / (float)pieces, t1 = reach * (float)(pc + 1) / (float)pieces;
                box = Aabb{Min(o + d * t0, o + d * t1), Max(o + d * t0, o + d * t1)}.Grown(1e-4f);
                tris.clear();
                s.mesh->Collect(box, tris);
                for (const Tri3& t : tris) {
                    const V3 e1 = t.b - t.a, e2 = t.c - t.a;
                    const V3 pv = Cross(d, e2);
                    const float det = Dot(e1, pv);
                    if (std::fabs(det) < 1e-12f) continue;
                    const float inv = 1.0f / det;
                    const V3 tv = o - t.a;
                    const float u = Dot(tv, pv) * inv;
                    if (u < 0.0f || u > 1.0f) continue;
                    const V3 qv = Cross(tv, e1);
                    const float v = Dot(d, qv) * inv;
                    if (v < 0.0f || u + v > 1.0f) continue;
                    const float tt = Dot(e2, qv) * inv;
                    if (tt < 0.0f || tt > best) continue;
                    best = tt;
                    V3 n = Normalize(Cross(e1, e2));
                    if (Dot(n, d) > 0.0f) n = -n;
                    nOut = n;
                    hit = true;
                }
            }
            if (hit) tOut = best;
            return hit;
        }
        case ShapeType::Plane: {
            const float den = Dot(s.planeNormal, d);
            const float h = Dot(s.planeNormal, o) - s.planeOffset;
            if (h <= 0.0f) { tOut = 0.0f; nOut = s.planeNormal; return true; }
            if (den >= 0.0f) return false;
            const float t = -h / den;
            if (t > maxT) return false;
            tOut = t; nOut = s.planeNormal;
            return true;
        }
        default: break;
    }
    // conservative advancement: march the point by its distance to the shape
    const Shape pt = Shape::Sphere(0.0f);
    float t = 0.0f;
    const float r = s.CoreRadius();
    for (int it = 0; it < 64; ++it) {
        const V3 p = o + d * t;
        const DistanceResult g = GjkDistance(s, Xf{}, pt, Xf{p, Q{}});
        if (g.overlap) {
            if (it == 0) { tOut = 0.0f; nOut = -d; return true; }
            break;
        }
        const float dist = g.distance - r;
        if (dist < 1e-4f) {
            tOut = t;
            nOut = Normalize(p - g.pA, -d);
            return true;
        }
        const float closing = -Dot(d, g.normal);   // normal points from the shape to the point
        (void)closing;
        const float along = Dot(d, -Normalize(p - g.pA, -d));
        if (along <= 1e-6f) return false;           // moving away or parallel
        t += dist / along * 0.999f + 1e-6f;
        if (t > maxT) return false;
    }
    tOut = t; nOut = -d;
    return t <= maxT;
}
} // namespace

RayHit World::RayCast(const V3& origin, const V3& dirIn, float maxDistance, std::uint32_t layerMask, bool hitSensors,
                      const std::function<bool(ColliderId)>& filter) const {
    RayHit best;
    const V3 dir = Normalize(dirIn, V3{});
    if (LenSq(dir) == 0.0f || !(maxDistance > 0.0f) || !Finite(origin)) return best;
    float maxT = std::min(maxDistance, 1e7f);
    m_tree.RayCast(origin, dir, maxT, [&](int proxy, float clip) {
        const ColliderId c = (ColliderId)m_tree.User(proxy);
        const ColliderData& cd = m->colliders[c];
        if (!cd.alive) return clip;
        if (cd.def.sensor && !hitSensors) return clip;
        if (!((layerMask >> (cd.def.layer & 31)) & 1u)) return clip;
        if (filter && !filter(c)) return clip;
        const Xf x = m->bodies[cd.body].xf * cd.def.local;
        float t; V3 n;
        if (!RayShape(cd.def.shape, x.ApplyInv(origin), InvRotate(x.q, dir), clip, t, n)) return clip;
        if (best.hit && t >= best.t) return clip;
        best.hit = true;
        best.t = t;
        best.collider = c;
        best.body = cd.body;
        best.point = origin + dir * t;
        best.normal = Rotate(x.q, n);
        return t;
    });
    return best;
}

std::vector<ColliderId> World::Overlap(const Shape& shape, const Xf& xf, std::uint32_t layerMask, bool hitSensors,
                                       const std::function<bool(ColliderId)>& filter) const {
    std::vector<ColliderId> out;
    const Aabb box = shape.Bounds(xf);
    m_tree.Query(box, [&](int proxy) {
        const ColliderId c = (ColliderId)m_tree.User(proxy);
        const ColliderData& cd = m->colliders[c];
        if (!cd.alive || (cd.def.sensor && !hitSensors)) return true;
        if (!((layerMask >> (cd.def.layer & 31)) & 1u)) return true;
        if (!cd.box.Overlaps(box)) return true;
        if (filter && !filter(c)) return true;
        Manifold mm;
        if (Collide(shape, xf, cd.def.shape, m->bodies[cd.body].xf * cd.def.local, 0.0f, mm)) {
            float minSep = 1e30f;
            for (int k = 0; k < mm.count; ++k) minSep = std::min(minSep, mm.pts[k].separation);
            if (minSep <= 0.0f) out.push_back(c);
        }
        return true;
    });
    std::sort(out.begin(), out.end());
    return out;
}

RayHit World::ShapeCast(const Shape& shape, const Xf& xf, const V3& dirIn, float maxDistance, std::uint32_t layerMask,
                        bool hitSensors, const std::function<bool(ColliderId)>& filter) const {
    RayHit best;
    const V3 dir = Normalize(dirIn, V3{});
    if (LenSq(dir) == 0.0f || !(maxDistance > 0.0f)) return best;
    Aabb sweep = shape.Bounds(xf);
    const Aabb end = shape.Bounds(Xf{xf.p + dir * maxDistance, xf.q});
    sweep = Aabb::Union(sweep, end);
    float bestT = maxDistance;
    m_tree.Query(sweep, [&](int proxy) {
        const ColliderId c = (ColliderId)m_tree.User(proxy);
        const ColliderData& cd = m->colliders[c];
        if (!cd.alive || (cd.def.sensor && !hitSensors)) return true;
        if (!((layerMask >> (cd.def.layer & 31)) & 1u)) return true;
        if (filter && !filter(c)) return true;
        const Xf xo = m->bodies[cd.body].xf * cd.def.local;
        const float rr = shape.CoreRadius() + cd.def.shape.CoreRadius();
        float t = 0.0f;
        for (int it = 0; it < 64; ++it) {   // conservative advancement of the moving shape
            const Xf xm{xf.p + dir * t, xf.q};
            if (cd.def.shape.type == ShapeType::Plane || cd.def.shape.type == ShapeType::Mesh) {
                Manifold mm;
                const float far = bestT - t + 1.0f;
                if (!Collide(cd.def.shape, xo, shape, xm, far, mm)) return true;
                float minSep = 1e30f;
                for (int k = 0; k < mm.count; ++k) minSep = std::min(minSep, mm.pts[k].separation);
                const float along = -Dot(dir, mm.normal);
                if (minSep <= 1e-4f) {
                    if (t < bestT || !best.hit) { bestT = t; best.hit = true; best.t = t; best.collider = c; best.body = cd.body; best.normal = mm.normal; best.point = mm.pts[0].pA; }
                    return true;
                }
                if (along <= 1e-6f) return true;
                t += minSep / along;
                if (t > bestT) return true;
                continue;
            }
            const DistanceResult g = GjkDistance(cd.def.shape, xo, shape, xm);
            if (g.overlap) {
                if (it == 0) { if (!best.hit || 0.0f < bestT) { bestT = 0.0f; best.hit = true; best.t = 0.0f; best.collider = c; best.body = cd.body; best.normal = -dir; best.point = xm.p; } }
                else if (t < bestT || !best.hit) { bestT = t; best.hit = true; best.t = t; best.collider = c; best.body = cd.body; best.normal = -dir; best.point = xm.p; }
                return true;
            }
            const float dist = g.distance - rr;
            if (dist < 1e-4f) {
                if (t < bestT || !best.hit) {
                    bestT = t; best.hit = true; best.t = t; best.collider = c; best.body = cd.body;
                    best.normal = g.normal;   // from the hit collider toward the moving shape
                    best.point = g.pA + g.normal * cd.def.shape.CoreRadius();
                }
                return true;
            }
            const float along = -Dot(dir, g.normal);
            if (along <= 1e-6f) return true;
            t += dist / along;
            if (t > bestT) return true;
        }
        return true;
    });
    return best;
}

std::uint64_t World::StateHash() const {
    std::uint64_t h = 1469598103934665603ULL;
    auto mix = [&](const void* p, std::size_t n) {
        const unsigned char* c = (const unsigned char*)p;
        for (std::size_t i = 0; i < n; ++i) { h ^= c[i]; h *= 1099511628211ULL; }
    };
    for (const BodyData& b : m->bodies) {
        if (!b.alive) continue;
        mix(&b.xf.p, sizeof(V3)); mix(&b.xf.q, sizeof(Q)); mix(&b.v, sizeof(V3)); mix(&b.w, sizeof(V3));
        const char aw = b.awake ? 1 : 0;
        mix(&aw, 1);
    }
    return h;
}

} // namespace phys
} // namespace okay
