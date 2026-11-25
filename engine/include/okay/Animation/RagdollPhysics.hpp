#pragma once
// ---------------------------------------------------------------------------
// RagdollPhysics - the thin physics layer under the ActiveRagdoll (our own
// "dynamic character" system, docs/gameplay/active_ragdoll.md).
//
// The controller (DynamicCharacter) only talks to the abstract RagdollPhysics
// below: add bodies and joints, set a joint's drive (target rotation, stiffness,
// damping), push bodies, pin a hand to the world, step, read poses and contacts.
// The one implementation is CoreRagdoll: one physics-core body and collider per
// bone and one core joint per joint (okay::phys::World, docs/physics/physics_core.md),
// in the scene's own physics world, so limbs meet everything in it. (Until
// 2026-11-25 there was also XpbdRagdoll, a second rigid-body and joint solver of
// the ragdoll's own; it was deleted when the ragdoll moved onto the core.)
//
// The controller's ray questions (grab, get-up floor) go through
// ContactWorld::Cast (a segment query), so it has no Scene dependency and runs
// in plain unit tests; the bodies themselves collide in the core world.
// ---------------------------------------------------------------------------
#include "okay/Math/Quat.hpp"
#include "okay/Math/Vec3.hpp"
#include <cstdint>
#include <memory>
#include <vector>

namespace okay {
namespace phys { class World; }
namespace dyn {

/// The world the ragdoll touches. Cast returns the first surface along
/// from -> to (point and outward unit normal).
class ContactWorld {
public:
    virtual ~ContactWorld() = default;
    virtual bool Cast(const Vec3& from, const Vec3& to, Vec3& point, Vec3& normal) const = 0;
};

struct ContactSphere { Vec3 local; float radius = 0.05f; };

struct BodyDesc {
    Vec3  position;
    Quat  rotation = Quat::Identity;
    float mass = 1.0f;
    Vec3  inertia{0.01f, 0.01f, 0.01f};   // principal moments in the body frame (kg m^2)
    float friction = 0.7f;
    /// The body's solid shape, in its own frame: a capsule along +Y (radius and half length of its middle part), or a
    /// box when hasBox is set (a foot). With neither it is a ball around its spheres.
    float capsuleRadius = 0.0f, capsuleHalf = 0.0f;
    bool  hasBox = false;
    Vec3  boxCentre, boxHalf;
    /// Points of the shape the controller asks about ("is the heel on the ground?"): see SphereTouching.
    std::vector<ContactSphere> spheres;
};

struct JointDesc {
    int   parent = -1, child = -1;
    Vec3  anchor;                 // world point at build time
    // Limits measured from the build pose. Ball: the child's axis (its local
    // +Y) may swing `swing` radians off its build direction in the parent and
    // twist `twist` radians about itself. Hinge: the child turns only about
    // `hingeAxis` (world, at build), between hingeMin and hingeMax radians.
    bool  hinge = false;
    float swing = 1.2f, twist = 0.6f;
    Vec3  hingeAxis{1, 0, 0};
    float hingeMin = 0.0f, hingeMax = 2.4f;
};

struct BodyState { Vec3 position; Quat rotation; Vec3 velocity, angularVelocity; };

/// A powered joint, field for field the drive of the scene physics' Ball and
/// ConeTwist Joint3D (branch claude2/own-physics-engine: useDrive, driveTarget,
/// driveStiffness, driveDamping, driveFrequency, driveDampingRatio,
/// driveMaxTorque), so a backend on that joint is a one-to-one copy:
///  - target: the turn from the build pose, child relative to parent (Joint3D
///    takes it as Euler degrees, x = twist; `target.ToEuler()`);
///  - stiffness in N m / rad and damping in N m s / rad, or, when stiffness is
///    0 and frequency > 0, both from frequency (Hz) and dampingRatio (scaled
///    to the limb's inertia); damping 0 with a stiffness = from dampingRatio;
///  - maxTorque in N m (0 = no cap): a strong push overcomes the muscle.
struct JointDrive {
    bool  useDrive = false;
    Quat  target = Quat::Identity;
    float stiffness = 0.0f, damping = 0.0f, frequency = 0.0f, dampingRatio = 1.0f, maxTorque = 0.0f;
};

/// What the controller needs from a physics engine. Keep it this small.
class RagdollPhysics {
public:
    virtual ~RagdollPhysics() = default;
    virtual void  Clear() = 0;
    virtual int   AddBody(const BodyDesc& d) = 0;
    virtual int   AddJoint(const JointDesc& d) = 0;
    virtual int   BodyCount() const = 0;
    virtual int   JointCount() const = 0;
    /// Joint drive (a powered joint; see JointDrive).
    virtual void  SetDrive(int joint, const JointDrive& drive) = 0;
    /// Extra damping on every joint (limp bodies stop wobbling), 0..1 per second.
    virtual void  SetJointFriction(float perSecond) = 0;
    virtual void  SetState(int body, const BodyState& s) = 0;
    virtual BodyState State(int body) const = 0;
    virtual float Mass(int body) const = 0;
    /// Instant velocity change from an impulse (N s) at a world point.
    virtual void  ApplyImpulse(int body, const Vec3& impulse, const Vec3& atPoint) = 0;
    /// Force (N) on the body's centre for the next Step only.
    virtual void  AddForce(int body, const Vec3& force) = 0;
    /// Torque (N m) for the next Step only.
    virtual void  AddTorque(int body, const Vec3& torque) = 0;
    /// Soft pin of a body point (local) to a world point; one per body.
    virtual void  SetPin(int body, const Vec3& localPoint, const Vec3& worldPoint, float stiffness) = 0;
    virtual void  ClearPin(int body) = 0;
    /// Get-up helper (not physical, said so in the docs): a soft spring from
    /// the world toward a position and / or rotation, plus velocity damping
    /// (fraction per second). Stiffness 0 switches that part off.
    virtual void  SetHelper(int body, const Vec3& pos, float posStiffness, const Quat& rot, float rotStiffness,
                            float damping) = 0;
    virtual void  ClearHelpers() = 0;
    /// Off: the bodies are placed by SetState and touch nothing (the animation shows). On: they are simulated.
    virtual void  SetActive(bool on) = 0;
    virtual void  Step(float dt, const ContactWorld* world) = 0;
    /// Contacts after the last Step.
    virtual bool  BodyTouching(int body) const = 0;
    virtual int   SphereCount(int body) const = 0;
    /// `point` is where the surface is touched.
    virtual bool  SphereTouching(int body, int sphere, Vec3* worldCentre = nullptr, Vec3* normal = nullptr,
                                 Vec3* point = nullptr) const = 0;
    /// Number of times a non-finite state was caught and reset.
    virtual int   Resets() const = 0;
};

/// The ragdoll's bodies on the physics core (okay::phys::World): one core body and collider per bone, one core
/// joint per joint with its orientation drive as the muscle. The core's solver moves them together with everything
/// else in that world, so limbs collide with dynamic bodies and with each other.
class CoreRagdoll : public RagdollPhysics {
public:
    CoreRagdoll();                      // in a world of its own (no scene: unit tests)
    ~CoreRagdoll() override;
    CoreRagdoll(const CoreRagdoll&) = delete;
    CoreRagdoll& operator=(const CoreRagdoll&) = delete;

    /// Put the bodies into `world` (the scene's) instead of our own; whoever owns that world steps it, and Step()
    /// here only hands over the drives and forces for that step. `alive` expires when that world is destroyed
    /// (every body is then forgotten, nothing is touched). nullptr = a world of our own again. Clears the bodies.
    void  UseWorld(phys::World* world, std::weak_ptr<void> alive);
    /// The world the bodies are in (ours or the scene's). In our own world the caller adds the ground.
    phys::World* World();
    bool  SharesWorld() const;
    /// The shared world was stepped by its owner: what touches is read again at the next question.
    void  ContactsChanged();
    /// The core body of a body here (phys::kInvalidId when there is none).
    std::uint32_t CoreBody(int body) const;
    /// Written into ColliderDef::user and BodyDef::user of everything made here (the scene uses it to know its guests).
    std::uint64_t colliderUser = 0;
    int   substeps = 1;                 // core steps per Step() in our own world
    float maxSpeed = 40.0f;             // m/s cap per body
    float maxSpin = 60.0f;              // rad/s cap per body
    float helperDamping = 1.0f;         // damping ratio of the helper springs (1 = no bounce for the body they hold)

    void  Clear() override;
    int   AddBody(const BodyDesc& d) override;
    int   AddJoint(const JointDesc& d) override;
    int   BodyCount() const override;
    int   JointCount() const override;
    void  SetDrive(int joint, const JointDrive& drive) override;
    void  SetJointFriction(float perSecond) override;
    void  SetState(int body, const BodyState& s) override;
    BodyState State(int body) const override;
    float Mass(int body) const override;
    void  ApplyImpulse(int body, const Vec3& impulse, const Vec3& atPoint) override;
    void  AddForce(int body, const Vec3& force) override;
    void  AddTorque(int body, const Vec3& torque) override;
    void  SetPin(int body, const Vec3& localPoint, const Vec3& worldPoint, float stiffness) override;
    void  ClearPin(int body) override;
    void  SetHelper(int body, const Vec3& pos, float posStiffness, const Quat& rot, float rotStiffness,
                    float damping) override;
    void  ClearHelpers() override;
    void  SetActive(bool on) override;
    bool  Active() const;
    void  Step(float dt, const ContactWorld* world) override;
    /// Touching something that is not part of this ragdoll.
    bool  BodyTouching(int body) const override;
    int   SphereCount(int body) const override;
    /// A sphere counts as touching when a contact point of its body is at it.
    bool  SphereTouching(int body, int sphere, Vec3* worldCentre, Vec3* normal, Vec3* point) const override;
    int   Resets() const override;

    /// The gap between the two anchor points of a joint (m), for tests.
    float JointGap(int joint) const;
    /// How far a joint is past its limits now (radians, 0 = inside), measured the way the core's joint measures it
    /// (twist about the joint's X axis, then the swing; a hinge's swing is all error), for tests.
    float JointLimitExcess(int joint) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

} // namespace dyn
} // namespace okay
