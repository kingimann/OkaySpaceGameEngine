// okay::phys::World: bodies, colliders, broad and narrow phase, islands, sleeping, events and queries.
// (Sources: see World.hpp. The solver is Solver.cpp, joints Joint.cpp.)
#include "WorldInternal.hpp"
#include "okay/Core/StringId.hpp"

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
    if (b.inertiaSet) b.inertiaLocal = b.setInertia;
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
    std::uint32_t slot;
    if (!m->freeBodies.empty()) { slot = m->freeBodies.back(); m->freeBodies.pop_back(); }
    else { slot = (std::uint32_t)m->bodies.size(); m->bodies.emplace_back(); }
    const std::uint8_t gen = NextGen(m->bodies[slot].gen);
    const BodyId id = MakeId(slot, gen);
    BodyData& b = m->bodies[id];
    b = BodyData{};
    b.gen = gen;
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
    const std::uint8_t gen = m->bodies[id].gen;
    m->bodies[id] = BodyData{};
    m->bodies[id].gen = gen;
    m->freeBodies.push_back(IdIndex(id));
    if (!m->noCollide.empty())
        m->noCollide.erase(std::remove_if(m->noCollide.begin(), m->noCollide.end(), [id](std::uint64_t k) {
            return (BodyId)(k >> 32) == id || (BodyId)(k & 0xFFFFFFFFu) == id; }), m->noCollide.end());
}

void World::SetBodiesCollide(BodyId a, BodyId b, bool collide) {
    if (!IsValid(a) || !IsValid(b) || a == b) return;
    const std::uint64_t key = Impl::BodyPairKey(a, b);
    const auto at = std::lower_bound(m->noCollide.begin(), m->noCollide.end(), key);
    const bool listed = at != m->noCollide.end() && *at == key;
    if (collide) {
        if (!listed) return;
        m->noCollide.erase(at);
        for (BodyId id : {a, b}) {   // the two look for each other again
            for (ColliderId c : m->bodies[id].colliders) m->MarkMoved(c);
        }
    } else if (!listed) {
        m->noCollide.insert(at, key);
        m->DropContactsBetween(a, b);
    }
}

bool World::IsValid(BodyId id) const {
    return id != kInvalidId && IdIndex(id) < m->bodies.size() && m->bodies[id].alive && m->bodies[id].gen == (id >> 24);
}
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
        m->MarkMoved(c);
    }
}
Xf World::GetTransform(BodyId id) const { return IsValid(id) ? m->bodies[id].xf : Xf{}; }
void World::SetTransform(BodyId id, const V3& p, const Q& q) {
    if (!IsValid(id) || !Finite(p) || !std::isfinite(q.x + q.y + q.z + q.w)) return;   // (a NaN pose is refused)
    BodyData& b = m->bodies[id];
    b.xf = Xf{p, NormalizeQ(q)};
    b.prevXf = b.xf;
    b.com = b.xf.Apply(b.localCom);
    if (b.type != BodyType::Static) { b.awake = true; b.sleepTime = 0.0f; }
    // whatever rested on it must notice that it moved
    for (ContactPair& cp : m->contacts) {
        if (!cp.alive) continue;
        const BodyId ba = m->colliders[cp.a].body, bb = m->colliders[cp.b].body;
        if (ba != id && bb != id) continue;
        cp.recheck = true;
        if (ba == id && bb != id) Wake(bb);
        else if (bb == id && ba != id) Wake(ba);
    }
    for (ColliderId c : b.colliders) {
        ColliderData& cd = m->colliders[c];
        cd.box = ColliderWorldBox(*m, cd);
        if (m_tree.Move(cd.proxy, cd.box, V3{})) {
            m->MarkMoved(c);
        }
    }
}
V3 World::GetLinearVelocity(BodyId id) const { return IsValid(id) ? m->bodies[id].v : V3{}; }
V3 World::GetAngularVelocity(BodyId id) const { return IsValid(id) ? m->bodies[id].w : V3{}; }
void World::SetLinearVelocity(BodyId id, const V3& v) {
    if (!IsValid(id) || m->bodies[id].type == BodyType::Static || !Finite(v)) return;
    m->bodies[id].v = v;
    if (LenSq(v) > 0.0f) Wake(id);
}
void World::SetAngularVelocity(BodyId id, const V3& w) {
    if (!IsValid(id) || m->bodies[id].type == BodyType::Static || !Finite(w)) return;
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
void World::SetInertia(BodyId id, const M33& inertiaLocal) {
    if (!IsValid(id)) return;
    bool given = false;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) given = given || inertiaLocal.m[i][j] != 0.0f;
    m->bodies[id].inertiaSet = given;
    m->bodies[id].setInertia = inertiaLocal;
    UpdateMass(*m, id);
}
void World::Configure(BodyId id, const BodySettings& s) {
    if (!IsValid(id)) return;
    SetType(id, s.type);   // (does nothing when the type is the same)
    SetDamping(id, s.linearDamping, s.angularDamping);
    SetGravityScale(id, s.gravityScale);
    SetLocks(id, s.lockLinear, s.lockAngular);
    SetBullet(id, s.bullet);
    SetAllowSleep(id, s.allowSleep);
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
    std::uint32_t slot;
    if (!m->freeColliders.empty()) { slot = m->freeColliders.back(); m->freeColliders.pop_back(); }
    else { slot = (std::uint32_t)m->colliders.size(); m->colliders.emplace_back(); }
    const std::uint8_t gen = NextGen(m->colliders[slot].gen);
    const ColliderId id = MakeId(slot, gen);
    ColliderData& c = m->colliders[id];
    c = ColliderData{};
    c.gen = gen;
    c.alive = true;
    c.body = body;
    c.def = def;
    c.def.local.q = NormalizeQ(def.local.q);
    m->bodies[body].colliders.push_back(id);
    UpdateMass(*m, body);
    c.box = ColliderWorldBox(*m, c);
    c.proxy = m_tree.Create(c.box, id);
    m->MarkMoved(id);
    // a sleeping body that gets a new shape looks for its contacts again (otherwise a collider rebuilt on a resting
    // body, e.g. after a friction change, would lose its floor contact until something woke it)
    Wake(body);
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
    const std::uint8_t gen = c.gen;
    c = ColliderData{};
    c.gen = gen;
    if (IdIndex(id) < m->movedFlag.size()) m->movedFlag[IdIndex(id)] = 0;
    m->movedProxies.erase(std::remove(m->movedProxies.begin(), m->movedProxies.end(), id), m->movedProxies.end());
    m->freeColliders.push_back(IdIndex(id));
    UpdateMass(*m, body);
}
bool World::IsValidCollider(ColliderId id) const {
    return id != kInvalidId && IdIndex(id) < m->colliders.size() && m->colliders[id].alive && m->colliders[id].gen == (id >> 24);
}
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
    std::uint32_t slot;
    if (!m->freeJoints.empty()) { slot = m->freeJoints.back(); m->freeJoints.pop_back(); }
    else { slot = (std::uint32_t)m->joints.size(); m->joints.emplace_back(); }
    const std::uint8_t gen = NextGen(m->joints[slot].gen);
    const JointId id = MakeId(slot, gen);
    JointData& j = m->joints[id];
    j = JointData{};
    j.gen = gen;
    j.alive = true;
    j.def = def;
    j.def.frameA.q = NormalizeQ(def.frameA.q);
    j.def.frameB.q = NormalizeQ(def.frameB.q);
    j.def.driveTarget = NormalizeQ(def.driveTarget);
    if (def.bodyA != kInvalidId) { m->bodies[def.bodyA].joints.push_back(id); Wake(def.bodyA); }
    if (def.bodyB != kInvalidId) { m->bodies[def.bodyB].joints.push_back(id); Wake(def.bodyB); }
    if (!def.collideConnected && def.bodyA != kInvalidId && def.bodyB != kInvalidId) m->DropContactsBetween(def.bodyA, def.bodyB);
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
            m->MarkMoved(c);
        }
    }
    const std::uint8_t gen = j.gen;
    j = JointData{};
    j.gen = gen;
    m->freeJoints.push_back(IdIndex(id));
}
bool World::IsValidJoint(JointId id) const {
    return id != kInvalidId && IdIndex(id) < m->joints.size() && m->joints[id].alive && m->joints[id].gen == (id >> 24);
}
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
    const bool sensor = a.def.sensor || b.def.sensor;   // (a trigger reports overlaps with anything, still or moving)
    if (ba.type != BodyType::Dynamic && bb.type != BodyType::Dynamic && !sensor && !world.reportStillPairs) return false;
    if (!((a.def.mask >> (b.def.layer & 31)) & 1u) || !((b.def.mask >> (a.def.layer & 31)) & 1u)) return false;
    if (!w.noCollide.empty() && std::binary_search(w.noCollide.begin(), w.noCollide.end(), World::Impl::BodyPairKey(a.body, b.body))) return false;
    // joined bodies that should not collide
    const BodyData& fewerJoints = ba.joints.size() <= bb.joints.size() ? ba : bb;
    const BodyId other = &fewerJoints == &ba ? b.body : a.body;
    for (JointId j : fewerJoints.joints) {
        const JointData& jd = w.joints[j];
        if (!jd.alive || jd.def.collideConnected) continue;
        if (jd.def.bodyA == other || jd.def.bodyB == other) return false;
    }
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

    // 0. every moving collider's box is stretched over where THIS step's motion can take it before pairs are looked
    //    for, so a body that starts fast (spawned with a speed, or given one while resting) meets what is in its way
    //    in this step's narrow phase (its speculative contact then stops it)
    for (std::size_t ci = 0; ci < w.colliders.size(); ++ci) {
        ColliderData& c = w.colliders[ci];
        if (!c.alive) continue;
        const BodyData& b = w.bodies[c.body];
        if (b.type == BodyType::Static || !b.awake) continue;
        V3 v = b.v;
        if (b.type == BodyType::Dynamic) v += gravity * (b.gravityScale * dt);
        const float reach = Len(b.xf.Apply(c.def.local.p) - b.com) + c.def.shape.OuterRadius();
        const V3 move = v * dt;
        const Aabb now = c.box;
        if (!Finite(now.lo) || !Finite(now.hi) || !Finite(move)) continue;
        const Aabb ahead{now.lo + move, now.hi + move};
        const Aabb sweep = Aabb::Union(now, ahead).Grown(Len(b.w) * reach * dt);
        if (!m_tree.FatAabb(c.proxy).Contains(sweep) && m_tree.Move(c.proxy, sweep, move)) w.MarkMoved(MakeId(ci, c.gen));
    }

    // 1. new pairs: colliders whose fat box changed look for partners in the tree (sorted, so the order is fixed)
    std::sort(w.movedProxies.begin(), w.movedProxies.end());
    std::vector<std::uint64_t> fresh;
    for (const ColliderId c : w.movedProxies) {
        if (IdIndex(c) < w.movedFlag.size()) w.movedFlag[IdIndex(c)] = 0;
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
            // a still pair: neither side can be pushed; it only reports whether the two overlap, and is looked at while
            // a side moves and once more after it stopped (the narrow phase sees the poses before this step's motion)
            const bool still = reportStillPairs && !cp.sensor && ba.type != BodyType::Dynamic && bb.type != BodyType::Dynamic;
            cp.active = aActive || bActive || cp.sensor || (still && cp.recheck);   // (a trigger is looked at every step, even between still colliders)
            cp.recheck = aActive || bActive;
            if (!cp.active) continue;   // both asleep or fixed: keep last step's result
            cp.wasTouching = cp.touching;
            if (!m_tree.FatAabb(ca.proxy).Overlaps(m_tree.FatAabb(cb.proxy))) {
                cp.alive = false;   // boxes apart: the pair ends (the end event is sent below)
                cp.count = 0;
                cp.touching = false;
                continue;
            }
            const Xf xa = ba.xf * ca.def.local, xb = bb.xf * cb.def.local;
            float spec = cp.sensor || still ? 0.0f : spec0;
            if (cont && !cp.sensor && !still) {
                float mm = MotionMargin(ba, ca, dt) + MotionMargin(bb, cb, dt);
                if (ba.bullet || bb.bullet) mm *= 1.5f;
                spec += mm;
            }
            Manifold man;
            const float offs = ca.def.contactOffset + cb.def.contactOffset;
            const bool hit = Collide(ca.def.shape, xa, cb.def.shape, xb, spec + offs, man) &&
                             (cp.sensor || !contactEnabled || contactEnabled(cp.a, cp.b));
            if (hit && offs > 0.0f)   // the colliders count as thicker: every point is that much closer
                for (int k = 0; k < man.count; ++k) {
                    man.pts[k].pA += man.pts[k].normal * ca.def.contactOffset;
                    man.pts[k].pB -= man.pts[k].normal * cb.def.contactOffset;
                    man.pts[k].separation -= offs;
                }
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
            cp.touching = still ? minSep < 0.0f : minSep <= linearSlop;
            cp.minSeparation = minSep;
            {   // how fast the two close along the normal (for waking a sleeper a speculative contact is about to push)
                const V3 pa = ba.xf.Apply(cp.pts[0].localA);
                const V3 vrel = (bb.v + Cross(bb.w, pa - bb.com)) - (ba.v + Cross(ba.w, pa - ba.com));
                cp.closing = std::max(0.0f, -Dot(vrel, cp.normal));
            }
            PairMaterial pm{Combine(ca.def.friction, cb.def.friction), std::max(ca.def.restitution, cb.def.restitution), 0.0f,
                            std::max(ca.def.rollingResistance, cb.def.rollingResistance)};
            if (combineMaterials) combineMaterials(cp.a, cp.b, pm);
            cp.friction = pm.friction;
            cp.restitution = pm.restitution;
            cp.staticFriction = pm.staticFriction;
            cp.rolling = pm.rolling;
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
        const V3 vrel = (w.bodies[bb].v + Cross(w.bodies[bb].w, e.point - w.bodies[bb].com)) -
                        (w.bodies[ba].v + Cross(w.bodies[ba].w, e.point - w.bodies[ba].com));
        const float closing = std::max(0.0f, -Dot(vrel, cp.normal));
        if (cp.touching && !cp.wasTouching) {
            // a speculative contact stops a fast body exactly at the surface one step before it "touches": the impact
            // speed is the fastest closing speed seen on the way in
            cp.approach = std::max(closing, cp.preApproach);
            cp.preApproach = 0.0f;
            e.approachSpeed = cp.approach;
            m_begin.push_back(e);
        } else if (!cp.touching && cp.wasTouching) {
            m_end.push_back(e);
        }
        if (!cp.touching) cp.preApproach = cp.count > 0 ? std::max(cp.preApproach, closing) : 0.0f;
        if (!cp.alive) w.contactIndex.erase(cp.key);
    }
    const double t2 = NowMs();

    // 3. islands: union-find over awake dynamic bodies through solved contacts and joints; sleeping dynamic bodies
    //    reached from an awake one wake up (a whole resting group wakes together)
    const std::size_t nb = w.bodies.size();
    std::vector<int> parent(nb);
    for (std::size_t i = 0; i < nb; ++i) parent[i] = (int)i;
    // (a plain union-find with path halving; the Improved step's copy is gone with it)
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
            // a contact acts when touching, or when it is apart but will close this step (a fast body about to hit a
            // sleeper): either wakes the sleeper, so the hit moves it instead of meeting a wall
            const bool acts = cp.touching || cp.closing * dt >= cp.minSeparation - linearSlop;
            if (aMoves && !B.awake && B.type == BodyType::Dynamic && acts) { B.awake = true; B.sleepTime = 0; changed = true; }
            if (bMoves && !A.awake && A.type == BodyType::Dynamic && acts) { A.awake = true; A.sleepTime = 0; changed = true; }
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
        if (isDyn(ba) && isDyn(bb) && w.bodies[ba].awake && w.bodies[bb].awake) unite((int)IdIndex(ba), (int)IdIndex(bb));
    }
    for (std::size_t ji = 0; ji < w.joints.size(); ++ji) {
        const JointData& j = w.joints[ji];
        if (!j.alive || j.broken) continue;
        if (isDyn(j.def.bodyA) && isDyn(j.def.bodyB) && w.bodies[j.def.bodyA].awake && w.bodies[j.def.bodyB].awake)
            unite((int)IdIndex(j.def.bodyA), (int)IdIndex(j.def.bodyB));
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
        islands[(std::size_t)is].bodies.push_back(MakeId(i, b.gen));
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
        if (is >= 0) islands[(std::size_t)is].joints.push_back(MakeId(ji, j.gen));
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

    // a body whose state went non-finite (NaN / inf from outside, or an extreme case) goes back to its last good pose
    // at rest, so it can never spread NaN to the others
    for (BodyData& b : w.bodies) {
        if (!b.alive || b.type == BodyType::Static) continue;
        if (Finite(b.xf.p) && Finite(b.v) && Finite(b.w) && std::isfinite(b.xf.q.x + b.xf.q.y + b.xf.q.z + b.xf.q.w)) continue;
        b.xf = Finite(b.prevXf.p) ? b.prevXf : Xf{};
        b.com = b.xf.Apply(b.localCom);
        b.v = V3{}; b.w = V3{};
    }

    // 5. move the moved colliders in the tree (their boxes stretched by this step's motion for the next step)
    for (std::size_t ci = 0; ci < w.colliders.size(); ++ci) {
        ColliderData& c = w.colliders[ci];
        if (!c.alive) continue;
        const BodyData& b = w.bodies[c.body];
        if (b.type == BodyType::Static) continue;
        if (b.xf.p == b.prevXf.p && b.xf.q == b.prevXf.q) continue;
        c.box = ColliderWorldBox(w, c);
        const V3 disp = b.xf.p - b.prevXf.p;
        if (!Finite(c.box.lo) || !Finite(c.box.hi)) continue;
        if (m_tree.Move(c.proxy, c.box, disp)) {
            w.MarkMoved(MakeId(ci, c.gen));
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

// ---- queries (scene rays and sweeps stay in Physics3D: one ray and sweep code for every quality) ----
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

std::uint64_t World::StateHash() const {
    std::string bytes;
    auto mix = [&](const void* p, std::size_t n) { bytes.append(static_cast<const char*>(p), n); };
    for (const BodyData& b : m->bodies) {
        if (!b.alive) continue;
        mix(&b.xf.p, sizeof(V3)); mix(&b.xf.q, sizeof(Q)); mix(&b.v, sizeof(V3)); mix(&b.w, sizeof(V3));
        const char aw = b.awake ? 1 : 0;
        mix(&aw, 1);
    }
    return StringId::HashOf(bytes);
}

} // namespace phys
} // namespace okay
