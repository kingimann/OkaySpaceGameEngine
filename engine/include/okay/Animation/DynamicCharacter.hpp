#pragma once
// ---------------------------------------------------------------------------
// DynamicCharacter - the brain of the ActiveRagdoll: a powered ragdoll of the
// 15 humanoid bones that follows the animation with per-joint PD drives, keeps
// its balance under pushes, steps to catch itself, falls when it cannot, braces
// and protects its head, and gets up again. No Scene here: it reads the
// animated bone pose, a ContactWorld and commands, and gives back bone poses.
// The Components/ActiveRagdoll component connects it to a character.
//
// Written from published research and general control methods only (see
// docs/gameplay/active_ragdoll.md for the full list):
//  - joint PD control toward an animated pose ("tracking by joint torques"),
//    as in Zordan & Hodgins 2002 "Motion capture-driven simulations that hit
//    and react" and Yin, Loken, van de Panne 2007 "SIMBICON" (torso held in
//    the world frame through the stance hip, swing foot placed from the centre
//    of mass position and velocity);
//  - balance as centre of mass over the support polygon with a virtual force
//    (Pratt et al. 2001 "Virtual model control");
//  - stepping to the capture point / extrapolated centre of mass
//    (Pratt, Carff, Drakunov, Goswami 2006 "Capture Point"; Hof 2008);
//  - protective reflexes when falling (arms out toward the ground, head
//    protected) as described in the falling-character literature, e.g.
//    Ha, Ye, Liu 2012 "Falling and landing motion control for character
//    animation";
//  - segment masses from Winter, "Biomechanics and Motor Control of Human
//    Movement" (4th ed., 2009), table 4.1.
// The get-up uses a "helper" force on the pelvis (stated as such; a common
// practice for game characters), keyframed poses we wrote ourselves, and a
// blend back to the animation.
// ---------------------------------------------------------------------------
#include "okay/Animation/RagdollPhysics.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Vec3.hpp"
#include <memory>

namespace okay {
namespace dyn {

constexpr int kBones = 15;   // humanoid::BoneCount, same order

enum class CharState { Animated = 0, Balancing, Falling, Down, GettingUp, Blending, Dead };
const char* StateName(CharState s);

/// How the character uses physics when nothing has happened to it.
enum class RagdollMode { Reactive = 0, Powered, Limp };

/// The standing rig, world space (one entry per humanoid bone).
struct RigInput {
    Vec3  pos[kBones];
    Quat  rot[kBones];
    Vec3  forward{0, 0, 1};     // facing direction
    float groundY = 0.0f;       // the soles rest here
    float mass = 70.0f;
};

struct Tuning {
    float strength = 1.0f;            // all drives
    float spineStrength = 1.0f, neckStrength = 1.0f, armStrength = 1.0f, legStrength = 1.0f;
    float stiffnessPerKg = 60.0f;     // N m / rad per kg the joint carries
    float torquePerKg = 14.0f;         // muscle limit, N m per kg the joint carries (plus 15 N m)
    float targetSpeed = 720.0f;       // degrees per second a drive target may move (no snapping)
    float dampingRatio = 1.0f;
    float balanceForce = 0.35f;       // virtual-force cap, fraction of body weight
    float balanceHelp = 0.2f;         // stated helper: outside push on the pelvis, cap as a fraction of body weight (0 = none)
    float uprightHelp = 20.0f;        // stated helper: chest held upright from the world, N m / rad per kg (0 = none)
    float maxStep = 0.8f;             // longest step, metres for a 1.8 m body (scales)
    int   maxSteps = 6;
    float stepTime = 0.25f;
    float blendTime = 0.35f;          // physics -> animation
    float fallAngle = 50.0f;          // degrees of torso lean that count as a fall
    bool  stumble = true, brace = true, protectHead = true, grab = true, autoGetUp = true;
    float getUpDelay = 0.8f;          // seconds lying still before getting up
    float getUpTime = 2.2f;
    float hitRelaxTime = 0.45f;
    float helperForce = 1.0f;         // get-up helper (0 = none, the body just lies)
};

/// One frame of wishes the behaviours fill in.
struct Frame {
    Quat  want[kBones];               // desired world rotation of a body
    bool  has[kBones] = {};
    float str[kBones];                // drive multiplier
};

class DynamicCharacter;

/// A behaviour module. Each one only writes the Frame and may push bodies.
class DynBehaviour {
public:
    virtual ~DynBehaviour() = default;
    virtual const char* Name() const = 0;
    virtual void Run(DynamicCharacter& c, Frame& f, float h) = 0;
    bool enabled = true;
};

class DynamicCharacter {
public:
    DynamicCharacter();
    ~DynamicCharacter();
    DynamicCharacter(const DynamicCharacter&) = delete;
    DynamicCharacter& operator=(const DynamicCharacter&) = delete;

    Tuning tuning;
    RagdollMode mode = RagdollMode::Reactive;

    /// Build the bodies from a standing rig. False (and nothing built) for a
    /// broken skeleton: missing, coincident or non-finite bones.
    bool Build(const RigInput& rig);
    bool Built() const { return m_built; }

    /// The animation's pose this frame (same layout as RigInput).
    void SetAnimationPose(const Vec3 pos[kBones], const Quat rot[kBones]);
    /// Put the bodies into a world someone else steps (the scene's) instead of one of their own; see
    /// CoreRagdoll::UseWorld. Call before Build. Then every frame is BeginFrame(dt, world) and, before each step of
    /// that world, ControlStep(step) instead of Tick. `user` marks the bodies and colliders as this owner's.
    void UseWorld(phys::World* world, std::weak_ptr<void> alive, std::uint64_t user);
    /// Advance by dt (fixed 60 Hz steps inside), stepping its own world.
    void Tick(float dt, const ContactWorld* world);
    /// The frame part of Tick: the animation's speeds and the world for ray questions.
    void BeginFrame(float dt, const ContactWorld* world);
    /// One control step of `h` seconds: behaviours, drives and forces for the world step that follows.
    void ControlStep(float h);
    /// The bone pose to show: physics blended with the animation.
    void BonePose(int bone, Vec3& pos, Quat& rot) const;
    /// 0 = pure animation, 1 = pure physics.
    float PhysicsWeight() const;

    // ---- Commands ----
    /// Push a body by an impulse (N s) at a world point; starts a reaction.
    void Push(const Vec3& point, const Vec3& impulse);
    /// A hit: push plus the struck region goes soft for a moment.
    void Hit(const Vec3& point, const Vec3& impulse);
    /// A bullet: small push, the struck limb goes slack; legs may buckle.
    void Shot(const Vec3& point, const Vec3& direction, float power);
    /// A blast: every body pushed away from `centre`, fading to 0 at `radius`.
    void Explosion(const Vec3& centre, float force, float radius);
    void Knockdown();
    void GetUp();
    void Die();
    void Revive();
    /// Back to the animation at once, where the animation now stands (a respawn
    /// that moved the object). Clears death, grips and helpers.
    void SnapToAnimation();
    void SetMode(RagdollMode m);

    // ---- State ----
    CharState State() const { return m_state; }
    float StateTime() const { return m_stateTime; }
    bool  Dead() const { return m_dead; }
    Vec3  CentreOfMass() const;
    Vec3  CentreOfMassVelocity() const;
    Vec3  CapturePoint() const { return m_capture; }
    int   StepsTaken() const { return m_steps; }
    bool  FaceDown() const { return m_faceDown; }
    int   Falls() const { return m_falls; }
    int   GetUps() const { return m_getUps; }
    float LastRecoveryTime() const { return m_lastRecovery; }
    bool  Grabbing() const { return m_grabBody >= 0; }
    int   Resets() const { return m_phys ? m_phys->Resets() : 0; }
    float StandingHeight() const { return m_pelvisH0; }
    float Height() const { return m_height; }
    /// Pelvis centre height above the ground under it.
    float PelvisHeight() const;
    /// Torso lean from upright, degrees.
    float TorsoLean() const;
    BodyState Body(int bone) const;
    float BodyRadius(int bone) const { return bone >= 0 && bone < kBones ? m_radius[bone] : 0.0f; }
    float BodyHalfLength(int bone) const { return bone >= 0 && bone < kBones ? m_half[bone] : 0.0f; }
    /// Ray against the body capsules (for shots). Returns the bone or -1.
    int   Raycast(const Vec3& origin, const Vec3& dir, float maxDist, Vec3* hitPoint = nullptr) const;
    RagdollPhysics* Physics() { return m_phys.get(); }
    CoreRagdoll* Bodies() { return m_core; }

    /// When the body ends up away from where the animation stands (after a
    /// stumble or a get-up), the owner moves its root by this much and turns
    /// it to face `forward`; then calls RootMoved().
    /// `groundY` is the height of the ground the body now stands on.
    bool  RootMoveWanted(Vec3& delta, Vec3& forward, float* groundY = nullptr) const;
    void  RootMoved();

    // ---- Behaviour modules (each small, each can be switched off) ----
    int   BehaviourCount() const;
    DynBehaviour* Behaviour(int i);
    DynBehaviour* FindBehaviour(const char* name);

    // ---- For behaviours ----
    struct Leg { int thigh, shin, foot; };
    const ContactWorld* World() const { return m_world; }
    Vec3  Up() const { return Vec3::Up; }
    Vec3  AnimForward() const;
    Vec3  BodyForward() const;     // chest front, flattened
    Vec3  Lateral(int side) const; // outward for side 0 = left, 1 = right, current heading
    Vec3  Anchor(int bone) const;  // world joint point of a bone (its joint with the parent)
    float SegmentLength(int bone) const { return bone >= 0 && bone < kBones ? m_segLen[bone] : 0.0f; }
    /// Height of the ground under the body (from its contacts; the build ground at first).
    float GroundY() const { return m_groundNow; }
    float TotalMass() const { return m_mass; }
    /// World rotation of a body in its build pose turned to face `fwd`.
    Quat  HeadingPose(int bone, const Vec3& fwd) const;
    /// Aim a body's long axis along `dir`, keeping the heading's twist.
    Quat  Aim(int bone, const Vec3& dir, const Vec3& fwd) const;
    /// World direction of a vector in the heading frame (x out to `side`, y up, z forward).
    Vec3  FromHeading(const Vec3& v, const Vec3& fwd, int side) const;
    /// Two-bone leg IK toward an ankle point: fills the frame for one leg.
    void  LegIK(Frame& f, int side, const Vec3& hip, const Vec3& ankle, const Vec3& fwd) const;
    void  FlatFoot(Frame& f, int side) const;
    bool  BodyTouching(int bone) const;
    /// The chest's front, world space (not flattened).
    Vec3  ChestFront() const;
    bool  FootSupport(int side, Vec3& centre) const;
    int   SupportPoints(Vec3* pts, int maxPts) const;
    bool  TorsoTouching() const;
    float MaxSpeed() const;
    /// Pitch: rotation about the axis across `fwd` that tips the top toward `fwd`.
    static Quat Pitch(float degrees, const Vec3& fwd);
    float FootSphereRadius() const { return m_footR; }
    float AnkleHeight() const { return m_ankleH; }
    void  StartPhysics();
    void  ChangeState(CharState s);
    float Relax(int bone) const { return bone >= 0 && bone < kBones ? m_relax[bone] : 0.0f; }
    Quat  AnimBodyRotation(int bone) const { return m_animQ[bone]; }
    Vec3  AnimBodyPosition(int bone) const { return m_animX[bone]; }

    // Balance bookkeeping shared by the modules.
    int   m_steps = 0;
    Vec3  m_capture;
    bool  m_faceDown = false;
    int   m_grabBody = -1;
    float m_stableTime = 0.0f, m_airTime = 0.0f;
    float m_disturbedAt = 0.0f;

private:
    void FixedStep(float h);
    void ApplyDrives(const Frame& f);
    void TrackAnimation(float h);
    float RegionStrength(int bone) const;
    int  NearestBody(const Vec3& p) const;

    CoreRagdoll* m_core = nullptr;      // m_phys, as what it is
    std::unique_ptr<RagdollPhysics> m_phys;
    std::unique_ptr<DynBehaviour> m_mod[6];
    const ContactWorld* m_world = nullptr;
    bool  m_built = false, m_dead = false, m_haveAnim = false;
    CharState m_state = CharState::Animated;
    float m_stateTime = 0.0f, m_clock = 0.0f, m_accum = 0.0f;
    float m_stepH = 1.0f / 60.0f;     // the control step now running
    float m_mass = 70.0f, m_height = 1.8f, m_groundY = 0.0f, m_groundNow = 0.0f, m_footCentreH = 0.0f;
    float m_rootGround = 0.0f;
    void  TrackGround();
    void  TrackSupport();
    int   m_footHold[2][4] = {};      // steps each foot sphere still counts as support
    Vec3  m_footPt[2][4];
    float m_pelvisH0 = 1.0f, m_comH0 = 1.0f;
    int   m_falls = 0, m_getUps = 0;
    float m_lastRecovery = -1.0f;
    float m_blendFrom = 1.0f;
    bool  m_rootMove = false; Vec3 m_rootDelta, m_rootFwd;
    // Build data per body (index = bone).
    Quat  m_bindQ[kBones];
    Vec3  m_bindX[kBones];
    Quat  m_off[kBones];          // bone -> body rotation
    Vec3  m_posOff[kBones];       // body centre in the bone frame
    Vec3  m_anchorLocal[kBones];  // joint point in the parent body frame
    float m_radius[kBones] = {}, m_half[kBones] = {}, m_segLen[kBones] = {};
    float m_support[kBones] = {};
    int   m_joint[kBones];
    Vec3  m_fwd0;
    Vec3  m_lat0[2];
    float m_footR = 0.02f, m_ankleH = 0.08f;
    Vec3  m_chestFwdLocal;        // torso body frame
    Vec3  m_soleUpLocal[2];
    // Animation this frame.
    Vec3  m_animX[kBones], m_animV[kBones];
    Quat  m_animQ[kBones];
    Vec3  m_animW[kBones];
    Vec3  m_animPrevX[kBones];
    Quat  m_animPrevQ[kBones];
    bool  m_haveAnimPrev = false;
    Quat  m_relAnim[kBones];
    Quat  m_relBind[kBones];          // child in parent at build (the drives' zero)
    Quat  m_relDrive[kBones];         // last drive target, for the target speed limit
    bool  m_haveDrive = false;
    // Shown pose when blending back.
    Vec3  m_frozenX[kBones];
    Quat  m_frozenQ[kBones];
    float m_relax[kBones] = {};
    float m_relaxTime[kBones] = {};
};

} // namespace dyn
} // namespace okay
