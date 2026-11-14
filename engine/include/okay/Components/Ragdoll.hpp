#pragma once
// Ragdoll — a humanoid goes limp on death and flinches when hit.
//
// The body is a Verlet particle chain over the 15 humanoid bones (plus a tip
// past the head, hands and feet). Distance constraints keep the bone lengths,
// braces keep the torso a box, and minimum-reach constraints stop elbows and
// knees folding through themselves. Particles collide with the scene by ray
// (Physics3D), ignoring the body's own colliders. Each frame the bones are
// posed from the particles, parents first.
//
// Bones come from the blocky Character (Character::Part) or, for an imported
// rig, from humanoid::MapHumanoidBones. While limp, the animators, IK and
// movement controllers on the body are switched off and the root Rigidbody3D
// is left alone; Deactivate() turns them back on.
//
// With `ragdollOnDeath` a HealthStat death goes limp, pushed away from the
// hit; a revive stands back up. With `hitReactions` a hit that leaves the body
// alive pushes the struck side and springs back to the animated pose.
#include "okay/Animation/HumanoidBones.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Net/PlayerInput.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include <vector>

namespace okay {

class Ragdoll : public Behaviour {
public:
    // ---- Simulation ----
    float gravity = 9.81f;
    float damping = 0.02f;         // velocity lost per step (0..1)
    float friction = 0.6f;         // sliding speed lost on contact (0..1)
    float radius = 0.06f;          // particle skin above surfaces
    int   iterations = 8;          // constraint passes per step
    float maxSpeed = 30.0f;        // m/s cap, keeps a bad frame from launching the body

    // ---- Triggers ----
    bool  ragdollOnDeath = true;   // go limp when the HealthStat dies
    float deathImpulse = 2.5f;     // m/s away from the hit on death
    bool  hitReactions = true;     // flinch on non-lethal hits
    float hitImpulse = 1.5f;       // m/s at the struck particle
    float hitRecover = 0.35f;      // seconds to spring back to the animated pose

    bool  IsActive() const { return m_active; }
    bool  IsReacting() const { return m_react > 0.0f; }
    /// Rest is near: every particle slower than `speed` m/s.
    bool  IsSettled(float speed = 0.05f) const;
    int   ParticleCount() const { return (int)m_p.size(); }
    Vec3  ParticlePosition(int i) const { return (i >= 0 && i < (int)m_p.size()) ? m_p[i].pos : Vec3::Zero; }
    /// Particle index of a humanoid::Bone (-1 when the rig lacks it).
    int   BoneParticle(int bone) const { return (bone >= 0 && bone < humanoid::BoneCount) ? m_boneParticle[bone] : -1; }

    /// Go limp. Carries the motion the bones had last frame.
    bool Activate();
    /// Stand back up: restore the bones' rest pose and switch everything back on.
    void Deactivate();
    /// Push the particle nearest `point` by `velocity` (m/s). Starts a flinch
    /// when the ragdoll isn't active and hit reactions are on.
    void Hit(const Vec3& point, const Vec3& velocity);

    void Start() override;
    void Update(float dt) override;
    void LateUpdate(float dt) override;
    void OnDisable() override { if (m_active) Deactivate(); }
    int  ExecutionOrder() const override { return 900; }   // after animation and IK

private:
    struct Particle {
        Vec3 pos, prev, offset;              // offset: bone-local, tips only
        Transform* bone = nullptr;
        bool contact = false;                // touching a surface this step
        Vec3 planePoint, planeNormal;
    };
    struct Link { int a, b; float len; bool minOnly; };
    struct Pose { Transform* t; Vec3 lpos; Quat lrot; };

    bool Build();
    void CapturePose();
    void SampleBones(bool keepVelocity);
    void Step(float h, float gravityScale);
    void Collide(Particle& p, const Vec3& from);
    void Push(Particle& p) const;
    void Rub(Particle& p) const;
    void PoseBones();
    void SetDriven(bool limp);
    bool Owns(const GameObject* go) const;
    int  Nearest(const Vec3& point) const;
    void WatchHealth();

    std::vector<Particle> m_p;
    std::vector<Link> m_links;
    std::vector<int> m_childOf;      // bone particle -> particle the bone points at
    std::vector<Vec3> m_restDir;     // bone-local direction to the child at capture
    std::vector<Vec3> m_restSide;    // bone-local left->right reference (for twist)
    std::vector<int> m_sideOf;       // per bone: left, right particle of that reference (-1 none)
    std::vector<Pose> m_rest;
    std::vector<Component*> m_switched;
    int   m_boneParticle[humanoid::BoneCount];
    bool  m_built = false, m_active = false, m_hadPrev = false;
    bool  m_wasDead = false;
    float m_lastHealth = -1.0f;
    float m_react = 0.0f;
    float m_accum = 0.0f;
    float m_lastDt = 0.0f;
};

} // namespace okay
