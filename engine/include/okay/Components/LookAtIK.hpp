#pragma once
// ---------------------------------------------------------------------------
// LookAtIK — head/eye tracking. Rotates a chain of bones
// (e.g. spine -> neck -> head) so a forward axis points at a target: NPCs that
// watch the player, a character that looks where you aim. Weighted and angle-
// clamped so it eases in and never snaps the neck past a believable limit.
//
// Assign `chain` root-to-tip and either `target` (world point) or `targetObject`.
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
#include <vector>
#include <string>

namespace okay {

class LookAtIK : public Component {
public:
    int ExecutionOrder() const override { return 110; }   // last: looks from the final body pose
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
    /// Ease speed (per second) toward the look target: heads swing smoothly to a
    /// new target instead of snapping the frame it changes. 0 = instant (old
    /// behavior). ~10 reads like a natural head turn.
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

    void Update(float) override { m_restore.Restore(); }   // before the animator blends (see IKRestore.hpp)

    // Solve in LateUpdate so the correction lands AFTER every animation driver
    // has posed the bones this frame (imported rigs create their per-node
    // Animators lazily, which puts them late in the Update order).
    void LateUpdate(float dt) override {
        const float w = weight * m_fade.Step(ikActive, fadeTime, dt);
        if (w <= 0.0f || chain.empty()) return;
        Vec3 tgt = targetObject ? targetObject->Position() : target;
        // Bone rotations persist between frames, so applying `ease` of the way per
        // frame converges on the target — a smooth swing instead of a snap when
        // the target (or targetObject) jumps.
        float ease = smoothing > 0.0f ? (1.0f - std::exp(-smoothing * dt)) : 1.0f;
        // Root-to-tip: each bone aims its forward at the target. Processing in order
        // means later bones see the positions their parents just moved them to.
        if (m_base.size() != chain.size()) { m_base.assign(chain.size(), Quat::Identity); m_written.assign(chain.size(), Quat{0, 0, 0, 0}); }
        for (std::size_t i = 0; i < chain.size(); ++i) {
            Transform* b = chain[i];
            if (!b) continue;
            // maxAngle limits each bone's TOTAL turn from its pre-IK pose (this
            // frame's animated pose, or the pose IK took over from), not the step.
            if (!SameRot(b->localRotation, m_written[i])) m_base[i] = b->localRotation;
            Vec3 dir = tgt - b->Position();
            if (dir.SqrMagnitude() < 1e-8f) continue;
            dir = dir.Normalized();
            const Quat baseW = (b->Parent() ? b->Parent()->Rotation() : Quat::Identity) * m_base[i];
            Vec3 fwd = (baseW * forwardAxis).Normalized();
            Quat desired = (Quat::FromToRotation(fwd, dir) * baseW).Normalized();
            const float total = Quat::Angle(baseW, desired);
            if (total > maxAngle && total > 1e-4f) desired = Quat::Slerp(baseW, desired, maxAngle / total);
            b->SetRotation(Quat::Slerp(b->Rotation(), desired, w * ease));
            m_written[i] = b->localRotation;
        }
    }
private:
    static bool SameRot(const Quat& a, const Quat& b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; }
    IKFade m_fade;
    std::vector<Quat> m_base, m_written;   // per bone: pre-IK local rotation / what IK last wrote
};

} // namespace okay
