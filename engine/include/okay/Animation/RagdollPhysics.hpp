#pragma once
// ---------------------------------------------------------------------------
// RagdollPhysics - the thin physics layer under the ActiveRagdoll (our own
// "dynamic character" system, docs/gameplay/active_ragdoll.md).
//
// The controller (DynamicCharacter) only talks to the abstract RagdollPhysics
// below: add bodies and joints, set a joint's drive (target rotation, stiffness,
// damping), push bodies, pin a hand to the world, step, read poses and contacts.
// Today the one implementation is XpbdRagdoll: a small articulated rigid-body
// solver of our own, written from the published method of
//   M. Mueller, M. Macklin, N. Chentanez, S. Jeschke, T.-Y. Kim,
//   "Detailed Rigid Body Simulation with Extended Position Based Dynamics",
//   Computer Graphics Forum (SCA) 2020
// (substepping, one positional pass per substep, positional and angular
// corrections with generalized inverse masses, compliance = 1 / stiffness,
// velocity pass for damping), plus the swing / twist split of a joint rotation
// that is standard in the robotics and animation literature.
// When the scene physics gains 6-DOF joints with spherical drives (branch
// claude2/own-physics-engine), a second implementation can build Rigidbody3D
// bodies and those joints instead; the controller does not change.
//
// The world is reached only through ContactWorld::Cast (a segment query), so
// the layer has no Scene dependency and runs in plain unit tests.
// ---------------------------------------------------------------------------
#include "okay/Math/Quat.hpp"
#include "okay/Math/Vec3.hpp"
#include <vector>

namespace okay {
namespace dyn {

/// The world the ragdoll touches. Cast returns the first surface along
/// from -> to (point and outward unit normal).
class ContactWorld {
public:
    virtual ~ContactWorld() = default;
    virtual bool Cast(const Vec3& from, const Vec3& to, Vec3& point, Vec3& normal) const = 0;
};

/// A flat ground at `groundY` plus optional axis-aligned boxes (a ledge, a
/// wall). Used by the tests and as the component's fallback when a scene has
/// no colliders under the character.
class SimpleWorld : public ContactWorld {
public:
    struct Box { Vec3 min, max; };
    float groundY = 0.0f;
    bool  hasGround = true;
    std::vector<Box> boxes;
    bool Cast(const Vec3& from, const Vec3& to, Vec3& point, Vec3& normal) const override;
};

struct ContactSphere { Vec3 local; float radius = 0.05f; };

struct BodyDesc {
    Vec3  position;
    Quat  rotation = Quat::Identity;
    float mass = 1.0f;
    Vec3  inertia{0.01f, 0.01f, 0.01f};   // principal moments in the body frame (kg m^2)
    float friction = 0.7f;
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
    virtual void  Step(float dt, const ContactWorld* world) = 0;
    /// Contacts after the last Step.
    virtual bool  BodyTouching(int body) const = 0;
    virtual int   SphereCount(int body) const = 0;
    virtual bool  SphereTouching(int body, int sphere, Vec3* worldCentre = nullptr) const = 0;
    /// Number of times a non-finite state was caught and reset.
    virtual int   Resets() const = 0;
};

/// Our own solver (see the header comment for the method).
class XpbdRagdoll : public RagdollPhysics {
public:
    Vec3  gravity{0.0f, -9.81f, 0.0f};
    int   substeps = 12;
    float maxSpeed = 40.0f;          // m/s cap per body
    float maxSpin = 60.0f;           // rad/s cap per body

    void  Clear() override;
    int   AddBody(const BodyDesc& d) override;
    int   AddJoint(const JointDesc& d) override;
    int   BodyCount() const override { return (int)m_bodies.size(); }
    int   JointCount() const override { return (int)m_joints.size(); }
    void  SetDrive(int joint, const JointDrive& drive) override;
    void  SetJointFriction(float perSecond) override { m_jointFriction = perSecond < 0.0f ? 0.0f : perSecond; }
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
    void  Step(float dt, const ContactWorld* world) override;
    bool  BodyTouching(int body) const override;
    int   SphereCount(int body) const override;
    bool  SphereTouching(int body, int sphere, Vec3* worldCentre) const override;
    int   Resets() const override { return m_resets; }

    /// Joint geometry for tests: the gap between the two anchor points (m).
    float JointGap(int joint) const;

private:
    struct Sphere {
        Vec3 local; float radius;
        bool touching = false, hasPlane = false;
        Vec3 planePoint, planeNormal;
    };
    struct Body {
        Vec3 x, v, w; Quat q;
        Vec3 px; Quat pq;               // pose at the start of the substep
        float invMass = 1.0f, mass = 1.0f;
        Vec3 invI;                      // inverse principal inertia, body frame
        float friction = 0.7f;
        Vec3 force, torque;
        std::vector<Sphere> spheres;
        bool pinned = false; Vec3 pinLocal, pinWorld; float pinK = 0.0f;
        Vec3 helpX; Quat helpQ; float helpXK = 0.0f, helpQK = 0.0f, helpDamp = 0.0f;
        BodyState good;                 // last finite state
    };
    struct Joint {
        int p, c;
        Vec3 rp, rc;                    // anchors in the parent / child frames
        Quat rest;                      // child frame in parent frame at build
        Vec3 axisInParent;              // child +Y in the parent frame at build
        bool hinge; float swing, twist;
        Vec3 hingeP, hingeC;            // hinge axis in each frame
        Vec3 refP;                      // child +Y at build, for the hinge angle, parent frame
        float hingeMin, hingeMax;
        JointDrive drive;
    };

    Vec3 InvIWorld(const Body& b, const Vec3& v) const;
    float GenInvMass(const Body* b, const Vec3& r, const Vec3& n) const;
    float GenInvInertia(const Body* b, const Vec3& n) const;
    void  Positional(Body* a, const Vec3& ra, Body* b, const Vec3& rb, const Vec3& delta, float alpha, float h);
    void  Angular(Body* a, Body* b, const Vec3& rot, float alpha, float h, float maxTorque = 0.0f);
    void  AngularVelocity(Body* a, Body* b, const Vec3& dw);
    void  FindContacts(float dt, const ContactWorld* world);
    void  SolveJoint(Joint& j, float h);
    float DriveStiffness(const Joint& j, const Body* P, const Body* C) const;
    void  SolveContacts(float h);
    void  Sanitise();

    std::vector<Body> m_bodies;
    std::vector<Joint> m_joints;
    float m_jointFriction = 0.0f;
    int m_resets = 0;
};

} // namespace dyn
} // namespace okay
