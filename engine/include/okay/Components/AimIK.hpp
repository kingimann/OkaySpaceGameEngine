#pragma once
// ---------------------------------------------------------------------------
// AimIK — point one bone's aim axis straight at a target (turret barrels, a held
// weapon, a spotlight, a head that snaps to look). Unlike LookAtIK (which eases a
// whole spine/neck/head chain), this is a single, exact aim with an optional up
// axis to stop the bone rolling, a weight to blend, and an angle clamp.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Math/IKFade.hpp"
#include "okay/Components/IKBones.hpp"
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
    /// Ease speed (per second) toward the aim: >0 swings the bone smoothly onto a
    /// moved target instead of snapping. 0 = exact/instant (turret-style default).
    float smoothing = 0.0f;

    void Start() override {
        // Names resolve inside this character first (two copies of one model
        // share bone names), then anywhere in the scene.
        Scene* s = GetScene();
        if (!bone) bone = ResolveBone(s, gameObject, boneName);
        if (!targetObject) targetObject = ResolveBone(s, gameObject, targetName);
    }

    void Update(float) override { m_restore.Restore(); }   // before the animator blends (see IKRestore.hpp)

    // Solve in LateUpdate so the correction lands AFTER every animation driver
    // has posed the bones this frame (imported rigs create their per-node
    // Animators lazily, which puts them late in the Update order).
    void LateUpdate(float dt) override {
        const float w = weight * m_fade.Step(ikActive, fadeTime, dt);
        if (w <= 0.0f) return;
        Transform* b = bone ? bone : transform;
        if (!b) return;
        Vec3 tgt = targetObject ? targetObject->Position() : target;
        Vec3 dir = tgt - b->Position();
        if (dir.SqrMagnitude() < 1e-8f) return;
        dir = dir.Normalized();

        // Orientation whose aimAxis points down `dir`, with upAxis controlling roll.
        Quat look = Quat::LookRotation(dir, upAxis.Normalized());
        // LookRotation aligns +Z to dir; re-map so the chosen aimAxis aligns instead.
        Quat axisFix = Quat::FromToRotation(Vec3::Forward, aimAxis.Normalized());
        Quat desired = (look * axisFix.Inverse()).Normalized();

        // maxAngle limits the TOTAL turn from the bone's pose before IK: the
        // animated pose this frame, or (when nothing re-poses the bone) the pose it
        // had when IK took over — not each frame's step, which would add up.
        if (!m_haveWritten || !SameRot(b->localRotation, m_written)) m_base = b->localRotation;
        const Quat baseW = (b->Parent() ? b->Parent()->Rotation() : Quat::Identity) * m_base;
        const float total = Quat::Angle(baseW, desired);
        if (total > maxAngle && total > 1e-4f) desired = Quat::Slerp(baseW, desired, maxAngle / total);
        // The rotation persists between frames, so easing a fraction per frame
        // converges on the aim — smooth tracking of a moving target when set.
        float ease = smoothing > 0.0f ? (1.0f - std::exp(-smoothing * dt)) : 1.0f;
        b->SetRotation(Quat::Slerp(b->Rotation(), desired, w * ease));
        m_written = b->localRotation; m_haveWritten = true;
    }
private:
    static bool SameRot(const Quat& a, const Quat& b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; }
    IKFade m_fade;
    Quat m_base, m_written;     // pre-IK local rotation / what IK last wrote
    bool m_haveWritten = false;
};

} // namespace okay
