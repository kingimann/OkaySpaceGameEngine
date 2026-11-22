#pragma once
// ActiveRagdoll - our own "dynamic character": a powered ragdoll that follows
// the animation, keeps its balance under pushes, steps to catch itself, falls
// when it cannot (bracing, protecting its head), and gets up again.
//
// The work is done by dyn::DynamicCharacter (Animation/DynamicCharacter.hpp)
// over dyn::RagdollPhysics (our own small articulated solver). This component
// connects it to a humanoid: the blocky Character's part rig or an imported
// rig (humanoid::MapHumanoidBones), the scene's 3D colliders (rays), a
// HealthStat (damage -> hit reaction, death -> limp, revive -> get up), and
// the movement controllers (switched off while the body is physical and back
// on after it recovers, with the root moved to where the body ended up).
//
// Written from published research only; see docs/gameplay/active_ragdoll.md
// for what each part was written from. Not related to any commercial product.
#include "okay/Animation/DynamicCharacter.hpp"
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Transform.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace okay {

class GameObject;
class Scene;

class ActiveRagdoll : public Behaviour {
public:
    // ---- How it behaves when nothing has happened ----
    /// 0 Reactive: animated until pushed or hit, then physical until it recovers.
    /// 1 Powered: always physical, following the animation with its muscles.
    /// 2 Limp: a plain ragdoll.
    int   mode = 0;
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
    // ---- Hooks ----
    bool  reactToDamage = true;       // a HealthStat hit pushes the struck part
    float damageImpulse = 1.5f;       // N s per point of damage
    bool  limpOnDeath = true;         // death goes limp, revive gets up
    bool  handOverControl = true;     // switch movement controllers off while physical
    bool  rootFollows = true;         // the object's root follows the body while it is down

    /// Called on every state change (from, to).
    std::function<void(dyn::CharState, dyn::CharState)> onStateChanged;

    // ---- Calls (scripts and visual scripts reach these) ----
    bool  Ready() const { return m_built; }
    /// Push (impulse in N s) at a world point; no point = the chest.
    void  Push(const Vec3& impulse);
    void  PushAt(const Vec3& point, const Vec3& impulse);
    void  Hit(const Vec3& point, const Vec3& impulse);
    /// Fire a ray at the body; on a hit, a shot reaction there. True if it hit.
    bool  Shoot(const Vec3& origin, const Vec3& direction, float power, float maxDistance = 1000.0f);
    void  Explode(const Vec3& centre, float force, float radius);
    /// Run a behaviour by name: knockdown, getup, die, revive, limp, powered, reactive.
    bool  Trigger(const std::string& name);
    void  SetMode(int m);
    dyn::CharState State() const { return m_dc.State(); }
    const char* StateName() const { return dyn::StateName(m_dc.State()); }
    bool  IsPhysical() const { return m_built && m_dc.State() != dyn::CharState::Animated; }
    bool  IsDown() const;
    /// A Ragdoll on the same object was switched off when this one started.
    bool  TookOverRagdoll() const { return m_tookOver; }
    dyn::DynamicCharacter& Core() { return m_dc; }
    const dyn::DynamicCharacter& Core() const { return m_dc; }
    Transform* BoneTransform(int bone) const { return bone >= 0 && bone < dyn::kBones ? m_bones[bone] : nullptr; }

    /// (Re)build from the rig; Start does this.
    bool  Build();

    void Start() override;
    void LateUpdate(float dt) override;
    void OnDisable() override;
    int  ExecutionOrder() const override { return 920; }   // after animation, IK and the Ragdoll

private:
    void ApplyTuning();
    void ReadAnimation(Vec3* pos, Quat* rot);
    void WritePose();
    void HandOver(bool physical);
    void WatchHealth();
    bool Owns(const GameObject* go) const;

    struct SceneWorld : dyn::ContactWorld {
        const ActiveRagdoll* owner = nullptr;
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
    std::vector<Component*> m_switched;
    dyn::CharState m_lastState = dyn::CharState::Animated;
    bool  m_wasDead = false;
    bool  m_tookOver = false;
    float m_rootAboveGround = 0.0f;   // root height over the ground at build
    float m_lastHealth = -1.0f;
};

} // namespace okay
