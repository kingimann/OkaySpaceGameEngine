#pragma once
// ---------------------------------------------------------------------------
// okay::phys::World — our own rigid-body physics engine core.
//
// A world holds bodies (static, kinematic, dynamic), convex colliders attached to them, and joints. Step(dt) runs
//   1. broad phase: a dynamic AABB tree (DynamicTree.hpp); only colliders that left their fat box look for new pairs;
//   2. narrow phase: GJK / EPA / clipped manifolds (Collide.hpp) for every pair, with speculative points so a fast body
//      is stopped before it reaches what it would hit (continuous collision without sub-stepping);
//   3. islands: bodies linked by contacts or joints are solved together; groups at rest fall asleep together and wake
//      together; islands are independent, so they are solved in parallel (SetParallelFor) with bit-identical results;
//   4. solver: sequential impulses with warm starting, Coulomb friction (pyramid or cone), restitution as a separate
//      pass, then a position pass that removes leftover overlap and joint drift (non-linear Gauss-Seidel);
//   5. joints: fixed, ball, hinge, slider, cone-twist, distance and a general 6-DOF joint, all one solver with
//      per-axis lock / limit / free, velocity motors, springs, and an orientation drive (target rotation with
//      stiffness, damping and a torque cap) made for powered ragdolls.
//
// Written from published sources only (each .cpp names its own):
//  * E. Catto, "Iterative Dynamics with Temporal Coherence" (2005) and "Fast and Simple Physics using Sequential
//    Impulses" (GDC 2006): sequential impulses, accumulated-impulse clamping, warm starting;
//  * E. Catto, "Soft Constraints: Reinventing the Spring" (GDC 2011): springs and drives from frequency and damping ratio;
//  * E. Catto, "Continuous Collision" (GDC 2013): speculative contacts;
//  * K. Erleben, "Stable, Robust, and Versatile Multibody Dynamics Animation" (PhD thesis, 2004): islands, sleeping,
//    position correction by projection;
//  * D. Baraff, "Physically Based Modeling: Rigid Body Simulation" (SIGGRAPH course notes, 2001): rigid body state,
//    inertia in world space;
//  * the swing-twist split of a rotation (e.g. J. Diebel, "Representing attitude", 2006; G. Grassia 1998 for the
//    exponential map) for cone and twist limits.
// No code from Bullet, PhysX, Jolt, Box2D, ODE, Havok or any other engine was read or used.
//
// Deterministic: the same calls in the same order give bit-identical bodies, whatever the thread count.
// No <mutex>/<thread> here; parallel work goes through the callback given to SetParallelFor.
// ---------------------------------------------------------------------------
#include "okay/Physics/Core/Collide.hpp"
#include "okay/Physics/Core/DynamicTree.hpp"
#include "okay/Physics/Core/Shape.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace okay {
namespace phys {

using BodyId = std::uint32_t;
using ColliderId = std::uint32_t;
using JointId = std::uint32_t;
constexpr std::uint32_t kInvalidId = 0xFFFFFFFFu;
/// Ids are handles: the low 24 bits are a slot, the top 8 bits that slot's generation. A removed body's (collider's,
/// joint's) id stays invalid even after something new takes its slot.
constexpr std::uint32_t kIdIndexMask = 0x00FFFFFFu;
inline std::uint32_t IdIndex(std::uint32_t id) { return id & kIdIndexMask; }

enum class BodyType : std::uint8_t { Static, Kinematic, Dynamic };

struct BodyDef {
    BodyType type = BodyType::Dynamic;
    V3 position;
    Q rotation;
    V3 linearVelocity, angularVelocity;
    float linearDamping = 0.0f;      // 1/s: velocity *= 1 / (1 + damping * dt)
    float angularDamping = 0.05f;
    float gravityScale = 1.0f;
    bool allowSleep = true;
    bool awake = true;
    bool bullet = false;             // always use a full continuous-collision margin, whatever the speed
    std::uint8_t lockLinear = 0;     // bit k = do not move along world axis k
    std::uint8_t lockAngular = 0;    // bit k = do not turn about world axis k
    std::uint64_t user = 0;
};

/// What a scene hands a body every step, in one call (World::Configure): its kind, damping, gravity, locks and flags.
struct BodySettings {
    BodyType type = BodyType::Dynamic;
    float linearDamping = 0.0f, angularDamping = 0.05f;
    float gravityScale = 1.0f;
    std::uint8_t lockLinear = 0, lockAngular = 0;   // as in BodyDef
    bool bullet = false;
    bool allowSleep = true;
};

struct ColliderDef {
    Shape shape;
    Xf local;                         // collider frame in the body frame
    float density = 1000.0f;          // kg / m^3 (water); ignored for static and kinematic bodies
    float friction = 0.5f;
    float restitution = 0.0f;
    float rollingResistance = 0.0f;   // 0..1: how much a rolling round body is slowed (stops a ball rolling forever)
    float contactOffset = 0.0f;       // the collider counts as this much thicker (rests this far off what it touches)
    bool sensor = false;              // reports overlaps, never pushes
    std::uint32_t layer = 0;          // 0..31
    std::uint32_t mask = 0xFFFFFFFFu; // layers it collides with
    std::uint64_t user = 0;
};

enum class JointType : std::uint8_t { Fixed, Ball, Hinge, Slider, ConeTwist, Distance, SixDof };
enum class AxisMode : std::uint8_t { Locked, Free, Limited };

/// One degree of freedom of a joint, measured in the joint frame of body A (axis 0 = X = hinge / twist / slider axis).
struct JointAxis {
    AxisMode mode = AxisMode::Locked;
    float lower = 0.0f, upper = 0.0f;   // metres (linear) or radians (angular) when Limited
    bool motor = false;                 // drive the relative speed along this axis toward motorSpeed
    float motorSpeed = 0.0f;            // m/s or rad/s
    float motorMaxForce = 0.0f;         // N or N*m (the most the motor can push)
    float springFrequency = 0.0f;       // > 0: a spring toward springTarget instead of a hard lock (Hz)
    float springDamping = 0.7f;         // damping ratio (1 = critical)
    float springTarget = 0.0f;          // m or rad
};

struct JointDef {
    JointType type = JointType::Ball;
    BodyId bodyA = kInvalidId;          // kInvalidId = the world (a fixed frame)
    BodyId bodyB = kInvalidId;
    Xf frameA, frameB;                  // joint frames in each body's local space (world space for the world)
    bool collideConnected = false;
    /// Per-axis settings; set by the type presets (see SetupJoint) and editable for SixDof.
    JointAxis linear[3], angular[3];
    /// ConeTwist / SixDof with angular[1], angular[2] Limited: an elliptical swing cone (radians about Y and Z).
    float swingSpan1 = 0.7f, swingSpan2 = 0.7f;
    bool useCone = false;
    /// Distance joint: rest length range (min = max for a rod; min 0 for a rope) and an optional spring.
    float minLength = 1.0f, maxLength = 1.0f;
    float distanceSpring = 0.0f, distanceDamping = 0.7f;   // Hz and damping ratio
    float distanceStiffness = 0.0f, distanceDamper = 0.0f; // or N/m and N*s/m (used when distanceSpring is 0)
    /// Orientation drive (powered ragdolls): pulls B's frame toward `driveTarget` (B's frame expressed in A's frame)
    /// like a damped spring, never with more than driveMaxTorque. Stiffness and damping in N*m/rad and N*m*s/rad,
    /// or, when driveFrequency > 0, as a frequency (Hz) and damping ratio that scale with the bodies' inertia.
    bool drive = false;
    Q driveTarget;
    V3 driveTargetVelocity;             // feed-forward relative angular velocity (rad/s, in A's joint frame)
    float driveStiffness = 0.0f, driveDamping = 0.0f;
    float driveFrequency = 0.0f, driveDampingRatio = 1.0f;
    float driveMaxTorque = 1e30f;
    /// Position drive (pulls B's anchor toward linearTarget, in A's joint frame).
    bool linearDrive = false;
    V3 linearTarget;
    float linearFrequency = 2.0f, linearDampingRatio = 1.0f, linearMaxForce = 1e30f;
    /// Break when the joint needs more than this to hold (N, N*m). Infinite by default.
    float breakForce = 1e30f, breakTorque = 1e30f;
    std::uint64_t user = 0;
};
/// Fill the per-axis settings for a joint type (Fixed: all locked; Ball: angular free; Hinge: twist free;
/// Slider: linear X free; ConeTwist: twist and swing limited; Distance and SixDof: as given).
void SetupJoint(JointDef& d);

struct ContactEvent {
    ColliderId a = kInvalidId, b = kInvalidId;
    BodyId bodyA = kInvalidId, bodyB = kInvalidId;
    V3 normal, point;                   // normal from A to B
    float approachSpeed = 0.0f;         // closing speed along the normal when it began (m/s, >= 0)
    bool sensor = false;
};

struct WorldStats {
    int bodies = 0, awake = 0, islands = 0, contacts = 0, touching = 0, points = 0, warmPoints = 0, joints = 0,
        pairsCreated = 0, proxiesMoved = 0;
    double broadMs = 0, narrowMs = 0, solveMs = 0;
};

/// (internal) one contact pair's persistent state
struct ContactPair;

class World {
public:
    World();
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // ---- settings ----
    V3 gravity{0.0f, -9.81f, 0.0f};
    int velocityIterations = 8;
    int positionIterations = 3;
    bool warmStarting = true;
    bool frictionCone = true;            // true: |friction| <= mu * normal as a cone; false: per tangent (pyramid)
    bool blockSolver = true;             // solve the 2 to 4 normal points of a manifold together (steadier stacks)
    bool enableSleep = true;
    bool continuous = true;              // speculative contacts sized by each body's motion this step
    /// Also report solid pairs in which neither body is dynamic (two fixed things, or a kinematic body against a wall).
    /// Nothing is pushed; the pair is touching while the two overlap, with its events and manifold, like a sensor that
    /// reports as solid. It is looked at only when one side moved. Off: such pairs do not exist (the 3D rule).
    bool reportStillPairs = false;
    float linearSlop = 0.002f;           // overlap that is left alone (stops jitter)
    float speculativeDistance = 0.02f;   // contacts closer than this are kept even at rest
    float positionCorrection = 0.2f;     // fraction of the overlap removed per position iteration
    float maxCorrection = 0.2f;          // metres per iteration at most
    float restitutionThreshold = 1.0f;   // m/s: slower impacts do not bounce
    float sleepLinear = 0.05f, sleepAngular = 0.05f, timeToSleep = 0.5f;
    float maxLinearSpeed = 500.0f, maxAngularSpeed = 1000.0f;

    /// Run fn(begin, end) over [0, count) on many threads (e.g. the engine JobSystem). Unset = serial.
    using ParallelFor = std::function<void(int count, const std::function<void(int, int)>& fn)>;
    void SetParallelFor(ParallelFor pf) { m_parallel = std::move(pf); }
    /// Extra pair filter (e.g. the scene's layer matrix): return false to never collide the two.
    std::function<bool(ColliderId, ColliderId)> shouldCollide;
    /// Asked every step for each solid pair that is close: return false and the pair counts as apart for this step (no
    /// push, not touching). For contacts that depend on where the two are, such as a one-way platform that lets through
    /// what comes from its far side. Called from the narrow phase (maybe on several threads: it must only read).
    std::function<bool(ColliderId, ColliderId)> contactEnabled;
    /// The materials of a touching pair. Unset: friction is the square root of the two frictions, bounciness the larger
    /// of the two, no extra still grip, rolling resistance the larger of the two. `staticFriction` (0 = same as friction)
    /// is the grip while the surfaces are nearly still. Called from the narrow phase (maybe on several threads: it
    /// must only read).
    struct PairMaterial { float friction, restitution, staticFriction, rolling; };
    std::function<void(ColliderId, ColliderId, PairMaterial&)> combineMaterials;

    // ---- bodies ----
    BodyId CreateBody(const BodyDef& def);
    void DestroyBody(BodyId id);            // also removes its colliders and joints
    bool IsValid(BodyId id) const;
    BodyType GetType(BodyId id) const;
    void SetType(BodyId id, BodyType t);
    Xf GetTransform(BodyId id) const;
    V3 GetPosition(BodyId id) const { return GetTransform(id).p; }
    Q GetRotation(BodyId id) const { return GetTransform(id).q; }
    void SetTransform(BodyId id, const V3& p, const Q& q);   // teleport (wakes the body)
    V3 GetLinearVelocity(BodyId id) const;
    V3 GetAngularVelocity(BodyId id) const;
    void SetLinearVelocity(BodyId id, const V3& v);
    void SetAngularVelocity(BodyId id, const V3& w);
    V3 GetCenterOfMass(BodyId id) const;     // world
    float GetMass(BodyId id) const;
    M33 GetInertiaLocal(BodyId id) const;    // about the centre of mass, body frame
    void SetMass(BodyId id, float mass);     // keep the shape, scale the inertia to this mass (<= 0: from density again)
    /// Use this inertia (about the centre of mass, body frame) instead of the one worked out from the shapes; an all-zero
    /// matrix goes back to the shapes'. For bodies whose shapes stand in for something else (a 2D disc is a ball here).
    void SetInertia(BodyId id, const M33& inertiaLocal);
    void Configure(BodyId id, const BodySettings& s);   // SetType (when it differs) and the five setters below
    void SetDamping(BodyId id, float linear, float angular);
    void SetGravityScale(BodyId id, float s);
    void SetLocks(BodyId id, std::uint8_t linear, std::uint8_t angular);
    void SetBullet(BodyId id, bool on);
    void SetAllowSleep(BodyId id, bool on);
    void ApplyForce(BodyId id, const V3& force, const V3& worldPoint);
    void ApplyForceAtCenter(BodyId id, const V3& force);
    void ApplyTorque(BodyId id, const V3& torque);
    void ApplyImpulse(BodyId id, const V3& impulse, const V3& worldPoint);
    void ApplyAngularImpulse(BodyId id, const V3& impulse);
    bool IsAwake(BodyId id) const;
    void Wake(BodyId id);
    void Sleep(BodyId id);
    std::uint64_t GetUser(BodyId id) const;
    /// The pose of the body part way between the last two steps (alpha 0 = previous step, 1 = latest).
    Xf InterpolatedTransform(BodyId id, float alpha) const;

    // ---- colliders ----
    ColliderId AddCollider(BodyId body, const ColliderDef& def);
    void RemoveCollider(ColliderId id);
    bool IsValidCollider(ColliderId id) const;
    BodyId ColliderBody(ColliderId id) const;
    const ColliderDef& GetCollider(ColliderId id) const;
    void SetColliderMaterial(ColliderId id, float friction, float restitution, float rollingResistance);
    void SetColliderFilter(ColliderId id, std::uint32_t layer, std::uint32_t mask);
    void SetColliderSensor(ColliderId id, bool sensor);
    Aabb ColliderBounds(ColliderId id) const;

    // ---- joints ----
    JointId CreateJoint(const JointDef& def);   // call SetupJoint(def) first, or fill the axes yourself
    void DestroyJoint(JointId id);
    bool IsValidJoint(JointId id) const;
    JointDef& EditJoint(JointId id);            // change motors, targets, limits between steps (wakes the bodies)
    const JointDef& GetJoint(JointId id) const;
    bool IsJointBroken(JointId id) const;
    /// What the joint pushed with in the last step (N and N*m).
    V3 JointForce(JointId id) const;
    V3 JointTorque(JointId id) const;
    /// Relative rotation of B's joint frame in A's (twist about X, swing about Y and Z, radians).
    V3 JointAngles(JointId id) const;

    // ---- simulation ----
    void Step(float dt);
    /// Fixed steps for a frame: runs as many whole `fixedDt` steps as the time allows (at most maxSteps; the rest of the
    /// time is dropped then) and returns how many ran. Alpha() is how far the frame is into the next step.
    int Advance(float frameDt, float fixedDt, int maxSteps = 8);
    float Alpha() const { return m_alpha; }
    const WorldStats& Stats() const { return m_stats; }

    // ---- events of the last step ----
    const std::vector<ContactEvent>& BeginEvents() const { return m_begin; }
    const std::vector<ContactEvent>& EndEvents() const { return m_end; }
    /// Every touching pair right now (sensor pairs included), with its deepest point.
    void ForEachTouching(const std::function<void(const ContactEvent&)>& fn) const;
    /// The contact manifold of a touching pair (false when they do not touch).
    bool GetManifold(ColliderId a, ColliderId b, Manifold& out) const;

    // ---- queries ----
    // (Rays and sweeps for game code are Physics3D::Raycast / SphereCast / ...: one query code for every quality.)
    /// Every collider overlapping `shape` at `xf`.
    std::vector<ColliderId> Overlap(const Shape& shape, const Xf& xf, std::uint32_t layerMask = 0xFFFFFFFFu,
                                    bool hitSensors = false, const std::function<bool(ColliderId)>& filter = nullptr) const;
    /// A hash of every body's pose and velocity bits (for determinism tests).
    std::uint64_t StateHash() const;
    const DynamicTree& Tree() const { return m_tree; }

    struct Impl;
private:
    std::unique_ptr<Impl> m;
    ParallelFor m_parallel;
    WorldStats m_stats;
    DynamicTree m_tree;
    std::vector<ContactEvent> m_begin, m_end;
    float m_accumulator = 0.0f, m_alpha = 0.0f;
};

} // namespace phys
} // namespace okay
