#pragma once
// ---------------------------------------------------------------------------
// LookAtIK — head/eye tracking. Rotates a chain of bones
// (e.g. spine -> neck -> head) so a forward axis points at a target: NPCs that
// watch the player, a character that looks where you aim. Weighted and angle-
// clamped so it eases in and never snaps the neck past a believable limit.
//
// Assign `chain` root-to-tip and either `target` (world point) or `targetObject`.
//
// Smoothing lives on the component (a world look direction per bone), not in the
// bone. An animated bone is a fresh pose every frame, so easing the bone's own
// rotation would shrink with the frame time and never arrive. The direction
// eases with 1-exp(-smoothing*dt); each frame the animated pose turns toward
// that direction and `weight` blends the turn.
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
#include <vector>
#include <string>

namespace okay {

class LookAtIK : public Component {
public:
    int ExecutionOrder() const override { return 100; }   // after animation
    std::vector<Transform*> chain;     ///< bones root..tip (e.g. spine, neck, head)
    Transform* targetObject = nullptr; ///< look at this object (overrides `target`)
    std::vector<std::string> chainNames; ///< editor/serialized: resolve `chain` by object names
    std::string targetName;            ///< editor/serialized: resolve `targetObject` by name
    Vec3  target{0, 0, 0};             ///< world point to look at (if no targetObject)
    Vec3  forwardAxis = Vec3::Forward; ///< the bones' local "forward"
    float weight   = 1.0f;             ///< 0 = off, 1 = full look
    bool  ikActive = true;             ///< switch IK on/off; the change eases over fadeTime
    float fadeTime = 0.0f;             ///< seconds to blend IK in/out (0 = instant)
    float maxAngle = 80.0f;            ///< max turn per bone (degrees), keeps it natural
    /// Ease speed (per second) of the look direction toward the target. The bone
    /// still reaches the target at any frame rate; only the swing takes time.
    /// 0 = instant. ~10 reads like a natural head turn.
    float smoothing = 10.0f;

    void Start() override {
        // Names resolve inside this character first (two copies of one model
        // share bone names), then anywhere in the scene.
        Scene* s = GetScene();
        if (chain.empty() && !chainNames.empty())
            for (const std::string& n : chainNames)
                if (Transform* t = ResolveBone(s, gameObject, n)) chain.push_back(t);
        if (!targetObject) targetObject = ResolveBone(s, gameObject, targetName);
    }

    // Solve in LateUpdate so the correction lands AFTER every animation driver
    // has posed the bones this frame (imported rigs create their per-node
    // Animators lazily, which puts them late in the Update order).
    void LateUpdate(float dt) override {
        // Last frame's writes come off first, so a still bone (and a rotation-only
        // clip) shows the animated pose before this solve. Weight 0 leaves it there.
        m_restore.Restore();
        const float w = weight * m_fade.Step(ikActive, fadeTime, dt);
        if (w <= 0.0f || chain.empty()) { ForgetAim(); return; }

        if (m_smooth.size() != chain.size()) { m_smooth.assign(chain.size(), {}); m_haveTgt = false; }

        const Vec3 tgt = targetObject ? targetObject->Position() : target;
        if (!Finite(tgt)) return;
        const Vec3 tgtChar = InCharacter(tgt);
        const bool tgtJump = m_haveTgt && Jumped(tgtChar, m_tgtChar);
        const float ease = smoothing > 0.0f ? (1.0f - std::exp(-smoothing * dt)) : 1.0f;

        // Root-to-tip: each bone aims its forward at the target. Later bones see
        // the positions their parents just moved them to.
        for (std::size_t i = 0; i < chain.size(); ++i) {
            Transform* b = chain[i];
            if (!b) continue;
            Smooth& s = m_smooth[i];
            if (s.bone != b) { s = {}; s.bone = b; }

            const Vec3 raw = tgt - b->Position();
            if (!Finite(raw) || raw.SqrMagnitude() < 1e-8f) continue;
            const Vec3 goal = SafeNormalize(raw, Vec3::Forward);
            const Vec3 boneChar = InCharacter(b->Position());
            const bool snap = !s.have || smoothing <= 0.0f || tgtJump || (s.have && Jumped(boneChar, s.boneChar));
            s.dir = snap ? goal : SafeNormalize(Vec3::LerpUnclamped(s.dir, goal, ease), goal);
            s.have = true;
            s.boneChar = boneChar;

            const Quat animated = b->Rotation();
            const Vec3 fwd = SafeNormalize(animated * forwardAxis, Vec3::Forward);
            Quat desired = (Quat::FromToRotation(fwd, s.dir) * animated).Normalized();
            if (!Finite(desired)) continue;
            const float total = Quat::Angle(animated, desired);
            if (total > maxAngle && total > 1e-4f) desired = Quat::Slerp(animated, desired, maxAngle / total);
            m_restore.Touch(b);
            BlendWorldRotation(b, animated, desired, w);
        }
        m_tgtChar = tgtChar;
        m_haveTgt = true;
        m_restore.Commit();
    }
private:
    /// A goal or bone that leaps this far in one frame (character space, metres
    /// squared) is a teleport: snap the look instead of trailing across the map.
    static constexpr float kSnapDist2 = 4.0f;

    struct Smooth {
        Transform* bone = nullptr;
        Vec3 dir = Vec3::Forward;
        Vec3 boneChar = Vec3::Zero;
        bool have = false;
    };

    Vec3 InCharacter(const Vec3& world) const {
        if (!transform) return world;
        return transform->Rotation().Inverse() * (world - transform->Position());
    }
    static bool Jumped(const Vec3& now, const Vec3& prev) {
        const Vec3 d = now - prev;
        return !Finite(d) || d.SqrMagnitude() > kSnapDist2;
    }
    void ForgetAim() { m_smooth.clear(); m_haveTgt = false; }

    IKFade m_fade;
    IKRestore m_restore;
    std::vector<Smooth> m_smooth;
    Vec3 m_tgtChar = Vec3::Zero;
    bool m_haveTgt = false;
};

} // namespace okay
