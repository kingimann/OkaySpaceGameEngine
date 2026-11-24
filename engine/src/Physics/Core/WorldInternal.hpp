#pragma once
// Internal state of okay::phys::World shared by World.cpp, Solver.cpp and Joint.cpp (not a public header).
#include "okay/Physics/Core/World.hpp"

#include <functional>
#include <vector>

namespace okay {
namespace phys {

/// A vector indexed by id handles: the low 24 bits pick the slot (the top 8 bits are the slot's generation, so an id
/// of something removed never names whatever reuses its slot; World::IsValid* check it).
template <class T> struct SlotVec {
    std::vector<T> v;
    T& operator[](std::uint32_t id) { return v[id & kIdIndexMask]; }
    const T& operator[](std::uint32_t id) const { return v[id & kIdIndexMask]; }
    std::size_t size() const { return v.size(); }
    void emplace_back() { v.emplace_back(); }
    typename std::vector<T>::iterator begin() { return v.begin(); }
    typename std::vector<T>::iterator end() { return v.end(); }
    typename std::vector<T>::const_iterator begin() const { return v.begin(); }
    typename std::vector<T>::const_iterator end() const { return v.end(); }
};
inline std::uint32_t MakeId(std::size_t index, std::uint8_t gen) { return (std::uint32_t)index | ((std::uint32_t)gen << 24); }
inline std::uint8_t NextGen(std::uint8_t g) { return (std::uint8_t)(g >= 254 ? 0 : g + 1); }   // (255 would make kInvalidId)

struct BodyData {
    bool alive = false;
    std::uint8_t gen = 0;
    BodyType type = BodyType::Dynamic;
    bool awake = true;
    bool allowSleep = true;
    bool bullet = false;
    bool massSet = false;            // SetMass was called (keep that mass when colliders change)
    float setMass = 0.0f;
    bool inertiaSet = false;         // SetInertia was called (keep that inertia when colliders or the mass change)
    M33 setInertia;
    float sleepTime = 0.0f;
    Xf xf, prevXf;                    // origin pose now and at the previous step
    V3 localCom, com;                 // centre of mass: body frame and world
    V3 v, w;                          // at the centre of mass
    float mass = 0.0f, invMass = 0.0f;
    M33 inertiaLocal, invInertiaLocal;
    float linDamp = 0.0f, angDamp = 0.05f, gravityScale = 1.0f;
    std::uint8_t lockLin = 0, lockAng = 0;
    V3 force, torque;
    std::vector<ColliderId> colliders;
    std::vector<JointId> joints;
    std::uint64_t user = 0;
    int island = -1;
    int slot = -1;                    // solver slot inside its island (dynamic bodies belong to one island only)
};

struct ColliderData {
    bool alive = false;
    std::uint8_t gen = 0;
    BodyId body = kInvalidId;
    ColliderDef def;
    int proxy = DynamicTree::kNull;
    Aabb box;                         // tight world box now
};

struct ContactPointState {
    V3 localA, localB;                // the two contact points in each body's frame (origin frame)
    V3 normal;                        // this point's normal, A to B (differs between points only on meshes)
    float separation = 0.0f;
    float normalImpulse = 0.0f, tangentImpulse[2] = {0.0f, 0.0f};
    float maxNormalImpulse = 0.0f;
    float relativeVelocity = 0.0f;    // normal speed before the solve (for restitution)
    bool warm = false;
};

struct ContactPair {
    std::uint64_t key = 0;            // (min collider << 32) | max collider
    ColliderId a = kInvalidId, b = kInvalidId;   // a < b
    bool alive = true;
    bool sensor = false;
    bool touching = false, wasTouching = false;
    bool active = false;              // looked at this step (one side awake)
    V3 normal;                        // A to B
    bool mixed = false;               // points have their own normals (a mesh pair)
    int count = 0;
    ContactPointState pts[4];
    float friction = 0.5f, restitution = 0.0f, rolling = 0.0f, staticFriction = 0.0f;
    float approach = 0.0f;            // closing speed when it began
    float closing = 0.0f;             // closing speed along the normal this step (narrow phase)
    float minSeparation = 0.0f;       // deepest point's separation this step
    float preApproach = 0.0f;         // fastest closing speed seen while still apart but within reach (speculative)
};

struct JointData {
    bool alive = false;
    std::uint8_t gen = 0;
    bool broken = false;
    JointDef def;
    // accumulated impulses (warm starting) and what they were last step
    V3 linImpulse;                    // point constraint (block) or per linear axis
    float linAxisImpulse[3] = {0, 0, 0};
    float linMotorImpulse[3] = {0, 0, 0};
    float linLowImpulse[3] = {0, 0, 0}, linHighImpulse[3] = {0, 0, 0};
    float angLowImpulse[3] = {0, 0, 0}, angHighImpulse[3] = {0, 0, 0};
    float angAxisImpulse[3] = {0, 0, 0};
    float angMotorImpulse[3] = {0, 0, 0};
    float coneImpulse = 0.0f;
    V3 driveImpulse;
    V3 linDriveImpulse;
    float distImpulse = 0.0f;
    V3 lastForce, lastTorque;
};

/// A body as the solver of one island sees it: a copy, written back only when dynamic.
struct SolverBody {
    BodyId id = kInvalidId;
    bool dynamic = false;
    V3 com, v, w;
    Q q;
    float invMass = 0.0f;
    V3 invMassAxis;                   // invMass per world axis (0 on locked axes)
    M33 invI;                         // world inverse inertia (rows / columns of locked axes cleared)
    std::uint8_t lockAng = 0;
    V3 localCom;                      // centre of mass in the body frame (to rebuild the origin pose)
};

struct World::Impl {
    SlotVec<BodyData> bodies;
    std::vector<BodyId> freeBodies;
    SlotVec<ColliderData> colliders;
    std::vector<ColliderId> freeColliders;
    SlotVec<JointData> joints;
    std::vector<JointId> freeJoints;
    std::vector<ContactPair> contacts;             // sorted by key
    std::unordered_map<std::uint64_t, int> contactIndex;
    std::vector<ColliderId> movedProxies;          // colliders whose fat box changed (look for new pairs)
    std::vector<char> movedFlag;                   // by slot
    void MarkMoved(ColliderId c) {
        const std::size_t i = c & kIdIndexMask;
        if (movedFlag.size() <= i) movedFlag.resize(i + 1, 0);
        if (!movedFlag[i]) { movedFlag[i] = 1; movedProxies.push_back(c); }
    }
};

// ---- helpers shared by the solver files ----
inline V3 ApplyInvMass(const SolverBody& b, const V3& p) { return Mul(b.invMassAxis, p); }
inline void ApplyImpulseTo(SolverBody& b, const V3& p, const V3& r) {
    if (!b.dynamic) return;
    b.v += Mul(b.invMassAxis, p);
    b.w += b.invI * Cross(r, p);
}
inline void ApplyAngular(SolverBody& b, const V3& L) {
    if (!b.dynamic) return;
    b.w += b.invI * L;
}
/// 1 / (J M^-1 J^T) for a linear direction n at arms rA, rB.
inline float EffMass(const SolverBody& a, const SolverBody& b, const V3& rA, const V3& rB, const V3& n) {
    const V3 ra = Cross(rA, n), rb = Cross(rB, n);
    float k = 0.0f;
    if (a.dynamic) k += Dot(n, Mul(a.invMassAxis, n)) + Dot(ra, a.invI * ra);
    if (b.dynamic) k += Dot(n, Mul(b.invMassAxis, n)) + Dot(rb, b.invI * rb);
    return k > 1e-12f ? 1.0f / k : 0.0f;
}
inline float AngEffMass(const SolverBody& a, const SolverBody& b, const V3& axis) {
    float k = 0.0f;
    if (a.dynamic) k += Dot(axis, a.invI * axis);
    if (b.dynamic) k += Dot(axis, b.invI * axis);
    return k > 1e-12f ? 1.0f / k : 0.0f;
}
inline V3 VelAt(const SolverBody& b, const V3& r) { return b.v + Cross(b.w, r); }
M33 WorldInvInertia(const M33& invLocal, const Q& q, std::uint8_t lockAng);

/// Everything one island's solve needs (built by World::Step, solved by SolveIsland, maybe on another thread).
struct IslandWork {
    std::vector<BodyId> bodies;        // dynamic bodies of the island (sorted)
    std::vector<int> contacts;         // indices into Impl::contacts (sorted by key)
    std::vector<JointId> joints;       // sorted
};

struct SolveSettings {
    float dt = 0.0f;
    V3 gravity;
    int velocityIterations = 8, positionIterations = 3;
    bool warmStarting = true, frictionCone = true, blockSolver = true;
    float linearSlop = 0.002f, positionCorrection = 0.2f, maxCorrection = 0.2f, restitutionThreshold = 1.0f;
    float maxLinearSpeed = 500.0f, maxAngularSpeed = 1000.0f;
    float sleepLinear = 0.05f, sleepAngular = 0.05f;
};

void SolveIsland(World::Impl& w, const IslandWork& island, const SolveSettings& s);

// Joint solving (Joint.cpp), on the island's solver bodies.
/// One scalar constraint row: J v = dot(lin, vB - vA) + dot(angB, wB) - dot(angA, wA).
struct JointRow {
    V3 lin, angA, angB;
    float mass = 0.0f;                 // 1 / (J M^-1 J^T + gamma)
    float bias = 0.0f;                 // velocity bias (target, speculative limit room, spring pull)
    float gamma = 0.0f;                // softness (springs, drives)
    float lo = -1e30f, hi = 1e30f;     // accumulated impulse bounds
    float* acc = nullptr;              // accumulated impulse (lives in JointData: warm starting)
    bool angular = false;              // for the reported force / torque
};
struct JointSolver {
    JointId id = kInvalidId;
    int a = 0, b = 0;                  // solver body slots (0 = the fixed world)
    V3 rA, rB;                         // arms from the centres of mass to the anchors
    bool point = false;                // all three linear axes locked: one 3x3 block
    M33 pointInvK;
    V3* pointAcc = nullptr;            // the point block's accumulated impulse (JointData::linImpulse)
    JointRow rows[24];
    int rowCount = 0;
};
void JointsPrepare(World::Impl& w, const IslandWork& island, std::vector<SolverBody>& sb,
                   const std::function<int(BodyId)>& slotOf, std::vector<JointSolver>& out, const SolveSettings& s);
void JointsWarmStart(std::vector<JointSolver>& js, std::vector<SolverBody>& sb);
void JointsSolveVelocity(std::vector<JointSolver>& js, std::vector<SolverBody>& sb, const SolveSettings& s);
void JointsSolvePosition(World::Impl& w, std::vector<JointSolver>& js, std::vector<SolverBody>& sb, const SolveSettings& s);
void JointsFinish(World::Impl& w, std::vector<JointSolver>& js, const SolveSettings& s);
V3 JointAnglesOf(const Q& qa, const Q& qb, const JointDef& d);

} // namespace phys
} // namespace okay
