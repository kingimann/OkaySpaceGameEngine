#pragma once
// ---------------------------------------------------------------------------
// AimIK — point one bone's aim axis straight at a target (turret barrels, a held
// weapon, a spotlight, a head that snaps to look). Unlike LookAtIK (which eases a
// whole spine/neck/head chain), this is a single, exact aim with an optional up
// axis to stop the bone rolling, a weight to blend, and an angle clamp.
//
// Smoothing lives on the component (one world aim direction), not in the bone.
// An animated bone is a fresh pose every frame, so easing the bone's own
// rotation would shrink with the frame time and never arrive. The direction
// eases with 1-exp(-smoothing*dt); each frame the animated pose turns toward
// that direction and `weight` blends the turn. smoothing 0 stays instant.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Math/IKFade.hpp"
#include "okay/Math/IKMath.hpp"
#include "okay/Components/IKBones.hpp"
#include "okay/Components/IKRestore.hpp"
#include <cmath>
#include <string>

namespace okay {

class AimIK : public Component {
public:
    int ExecutionOrder() const override { return 100; }   // after animation
    Transform* bone = nullptr;         ///< the bone to aim (defaults to this object's transform)
    Transform* targetObject = nullptr;
    std::string boneName;              ///< editor/serialized: resolve `bone` by object name at Start
    std::string targetName;            ///< editor/serialized: resolve `targetObject` by object name
    Vec3  target{0, 0, 0};
    Vec3  aimAxis = Vec3::Forward;     ///< the bone-local axis that should point at the target
    Vec3  upAxis  = Vec3::Up;          ///< keeps the bone from rolling around the aim axis
    float weight  = 1.0f;
    bool  ikActive = true;             ///< switch IK on/off; the change eases over fadeTime
    float fadeTime = 0.0f;             ///< seconds to blend IK in/out (0 = instant)
    float maxAngle = 180.0f;           ///< clamp the turn from the rest pose (degrees)
    /// Ease speed (per second) of the aim direction toward the target. The bone
    /// still reaches the target at any frame rate; only the swing takes time.
    /// 0 = exact/instant (turret-style default).
    float smoothing = 0.0f;

    void Start() override {
        // Names resolve inside this character first (two copies of one model
        // share bone names), then anywhere in the scene.
        Scene* s = GetScene();
        if (!bone) bone = ResolveBone(s, gameObject, boneName);
        if (!targetObject) targetObject = ResolveBone(s, gameObject, targetName);
    }

    // Solve in LateUpdate so the correction lands AFTER every animation driver
    // has posed the bones this frame (imported rigs create their per-node
    // Animators lazily, which puts them late in the Update order).
    void LateUpdate(float dt) override {
        // Last frame's write comes off first, so weight 0 is exactly the animated
        // pose on this frame (a still bone keeps the pose IK took over from).
        m_restore.Restore();
        const float w = weight * m_fade.Step(ikActive, fadeTime, dt);
        Transform* b = bone ? bone : transform;
        if (w <= 0.0f || !b) { ForgetAim(); return; }

        const Vec3 tgt = targetObject ? targetObject->Position() : target;
        if (!Finite(tgt)) return;
        const Vec3 raw = tgt - b->Position();
        if (!Finite(raw) || raw.SqrMagnitude() < 1e-8f) return;
        const Vec3 goal = SafeNormalize(raw, Vec3::Forward);

        const Vec3 tgtChar = InCharacter(tgt);
        const Vec3 boneChar = InCharacter(b->Position());
        const bool jumped = m_have && (Jumped(tgtChar, m_tgtChar) || Jumped(boneChar, m_boneChar));
        const float ease = smoothing > 0.0f ? (1.0f - std::exp(-smoothing * dt)) : 1.0f;
        const bool snap = !m_have || smoothing <= 0.0f || jumped;
        m_dir = snap ? goal : SafeNormalize(Vec3::LerpUnclamped(m_dir, goal, ease), goal);
        m_have = true;
        m_tgtChar = tgtChar;
        m_boneChar = boneChar;

        // Orientation whose aimAxis points down the smoothed direction, with
        // upAxis controlling roll. LookRotation aligns +Z; remap the aim axis.
        const Quat animated = b->Rotation();
        const Quat look = Quat::LookRotation(m_dir, SafeNormalize(upAxis, Vec3::Up));
        const Quat axisFix = Quat::FromToRotation(Vec3::Forward, SafeNormalize(aimAxis, Vec3::Forward));
        Quat desired = (look * axisFix.Inverse()).Normalized();
        if (!Finite(desired)) return;

        // maxAngle limits the TOTAL turn from this frame's animated pose.
        const float total = Quat::Angle(animated, desired);
        if (total > maxAngle && total > 1e-4f) desired = Quat::Slerp(animated, desired, maxAngle / total);
        m_restore.Touch(b);
        BlendWorldRotation(b, animated, desired, w);
        m_restore.Commit();
    }
private:
    /// A goal or bone that leaps this far in one frame (character space, metres
    /// squared) is a teleport: snap the aim instead of trailing across the map.
    static constexpr float kSnapDist2 = 4.0f;

    Vec3 InCharacter(const Vec3& world) const {
        if (!transform) return world;
        return transform->Rotation().Inverse() * (world - transform->Position());
    }
    static bool Jumped(const Vec3& now, const Vec3& prev) {
        const Vec3 d = now - prev;
        return !Finite(d) || d.SqrMagnitude() > kSnapDist2;
    }
    void ForgetAim() { m_have = false; }

    IKFade m_fade;
    IKRestore m_restore;
    Vec3 m_dir = Vec3::Forward;
    Vec3 m_tgtChar = Vec3::Zero;
    Vec3 m_boneChar = Vec3::Zero;
    bool m_have = false;
};

} // namespace okay
