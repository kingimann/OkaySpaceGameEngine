#pragma once
// Ragdoll - the one ragdoll of the engine: a physical humanoid body that goes
// limp on death, reacts to hits, keeps its balance under pushes, steps to
// catch itself, falls when it cannot (bracing, protecting its head, grabbing
// a wall), and gets up again.
//
// The body is 15 rigid bodies joined by powered joints, simulated by
// dyn::RagdollPhysics (Animation/RagdollPhysics.hpp) and steered by
// dyn::DynamicCharacter (Animation/DynamicCharacter.hpp). "Limp" is not a
// second simulation: it is the same body with its muscles off, so a dead
// character falls physically and collides with the world.
//
// Mode (what it does when nothing has happened):
//   0 Reactive - animated until it is pushed, hit or killed; then physical
//                until it recovers (the default; what both old components did).
//   1 Powered  - always physical, following the animation with its muscles.
//   2 Limp     - a plain ragdoll, muscles off.
//
// It connects to a humanoid (the blocky Character's part rig or an imported
// rig through humanoid::MapHumanoidBones), the scene's 3D colliders (rays), a
// HealthStat (damage -> hit reaction, death -> limp, revive -> get up), the
// movement controllers (off while the body is physical, back on after it
// recovers, with the root moved to where the body ended up) and, while dead,
// the animators and IK (off, back on at revive).
//
// Old names: scenes saved with the older Verlet `Ragdoll` record (`ragdoll`)
// and with `ActiveRagdoll` (`activeragdoll`) both load into this component;
// `ActiveRagdoll` is a name for this same class (see below), not a second one.
//
// Written from published research only; see docs/gameplay/ragdoll.md for what
// each part was written from. Not related to any commercial product.
#include "okay/Animation/DynamicCharacter.hpp"
#include "okay/Components/RagdollShared.hpp"
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Transform.hpp"
#include <functional>
#include <string>
#include <vector>

namespace okay {

class GameObject;

class Ragdoll : public Behaviour {
public:
    enum Mode { Reactive = 0, Powered = 1, Limp = 2 };
    /// See the header comment: 0 Reactive, 1 Powered, 2 Limp.
    int   mode = Reactive;
    float mass = 70.0f;               // kg
    // ---- Muscles ----
    float strength = 1.0f;
    float armStrength = 1.0f, legStrength = 1.0f, spineStrength = 1.0f, neckStrength = 1.0f;
    // ---- Balance ----
    float balanceForce = 0.35f;       // fraction of body weight
    float maxStep = 0.8f;             // metres (for a 1.8 m body)
    int   maxSteps = 6;
    float fallAngle = 50.0f;          // degrees
    float balanceHelp = 0.2f;         // helper push on the pelvis (share of body weight; 0 = only its own muscles)
    float uprightHelp = 20.0f;        // helper that keeps the chest upright while it stumbles (0 = none)
    // ---- Behaviours ----
    bool  stumble = true, brace = true, protectHead = true, grab = true, autoGetUp = true;
    float getUpDelay = 0.8f, getUpTime = 2.2f, blendTime = 0.35f;
    float helperForce = 1.0f;         // get-up helper (0 = lies there)
    // ---- Death and hits ----
    bool  limpOnDeath = true;         // death goes limp, revive gets up
    float deathImpulse = 2.5f;        // m/s away from the killing hit
    bool  reactToDamage = true;       // a HealthStat hit (and Hit()) pushes the struck part
    float damageImpulse = 1.5f;       // N s per point of damage
    float hitRecover = 0.45f;         // seconds the struck part stays soft
    // ---- Hand-over ----
    bool  handOverControl = true;     // switch movement controllers off while physical
    bool  rootFollows = true;         // the object's root follows the body while it is down

    /// Called on every state change (from, to).
    std::function<void(dyn::CharState, dyn::CharState)> onStateChanged;

    // ---- The classic calls (scripts ragdoll(), ragdoll(0), ragdoll_push, is_ragdoll) ----
    /// Go limp now (muscles off), carrying the motion the bones had. True when the body could be built.
    bool  Activate();
    /// Stand back up at once: back to the animated pose, everything switched off is back on.
    void  Deactivate();
    /// Push the part nearest `point` by a velocity change of `velocity` (m/s). Does nothing to a
    /// body that is not physical yet when reactToDamage is off.
    void  Hit(const Vec3& point, const Vec3& velocity);
    /// The body is limp or falling: Falling, Down or Dead.
    bool  IsActive() const;
    /// Rest is near: every part slower than `speed` m/s.
    bool  IsSettled(float speed = 0.05f) const;

    // ---- The active calls (scripts active_ragdoll*, visual scripts) ----
    bool  Ready() const { return m_built; }
    /// Push (impulse in N s) at a world point; no point = the chest.
    void  Push(const Vec3& impulse);
    void  PushAt(const Vec3& point, const Vec3& impulse);
    /// A hit by an impulse (N s): the push, and the struck part goes soft for hitRecover seconds.
    void  HitImpulse(const Vec3& point, const Vec3& impulse);
    /// Fire a ray at the body; on a hit, a shot reaction there. True if it hit.
    bool  Shoot(const Vec3& origin, const Vec3& direction, float power, float maxDistance = 1000.0f);
    void  Explode(const Vec3& centre, float force, float radius);
    /// Run a behaviour by name: knockdown, getup, die, revive, limp, powered, reactive, stumble.
    bool  Trigger(const std::string& name);
    void  SetMode(int m);
    dyn::CharState State() const { return m_dc.State(); }
    const char* StateName() const { return dyn::StateName(m_dc.State()); }
    bool  IsPhysical() const { return m_built && m_dc.State() != dyn::CharState::Animated; }
    bool  IsDown() const;
    dyn::DynamicCharacter& Core() { return m_dc; }
    const dyn::DynamicCharacter& Core() const { return m_dc; }
    Transform* BoneTransform(int bone) const { return bone >= 0 && bone < dyn::kBones ? m_bones[bone] : nullptr; }
    /// World position of a humanoid::Bone's body (zero before it is built).
    Vec3  BodyPosition(int bone) const;

    /// (Re)build from the rig; Start does this.
    bool  Build();

    void Start() override;
    void LateUpdate(float dt) override;
    void OnDisable() override;
    int  ExecutionOrder() const override { return 920; }   // after animation and IK

private:
    void ApplyTuning();
    void ReadAnimation(Vec3* pos, Quat* rot);
    void WritePose();
    void RestoreAnimatedPose();
    void HandOver(bool physical);
    void SwitchAnimation(bool off);
    void WatchHealth();
    bool Remote() const;              // a copy another peer owns (NetworkObject)

    struct SceneWorld : dyn::ContactWorld {
        const Ragdoll* owner = nullptr;
        bool Cast(const Vec3& from, const Vec3& to, Vec3& point, Vec3& normal) const override;
    };

    dyn::DynamicCharacter m_dc;
    SceneWorld m_world;
    Transform* m_bones[dyn::kBones] = {};
    Vec3  m_animLocalPos[dyn::kBones];
    Quat  m_animLocalRot[dyn::kBones];
    Vec3  m_wroteLocalPos[dyn::kBones];
    Quat  m_wroteLocalRot[dyn::kBones];
    bool  m_wrote = false;
    bool  m_built = false;
    bool  m_handedOver = false;
    bool  m_animOff = false;
    std::vector<Component*> m_switched;       // movement, off while physical
    std::vector<Component*> m_switchedAnim;   // animation and IK, off while dead
    dyn::CharState m_lastState = dyn::CharState::Animated;
    ragdoll::HealthWatch m_watch;     // death / revive / hits
    Vec3  m_lastRoot;                 // root at the end of last frame (to see a respawn jump)
    float m_rootAboveGround = 0.0f;   // root height over the ground at build
};

/// The old name of the physics-driven ragdoll. The same class: scenes, scripts and C++ written
/// for ActiveRagdoll keep working.
using ActiveRagdoll = Ragdoll;

} // namespace okay
