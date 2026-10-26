#pragma once
// ---------------------------------------------------------------------------
// LimbIK — general two-bone IK for one limb (arm or leg): reach a target with the
// hand/foot, bend the elbow/knee toward a pole hint, respect joint limits, and
// optionally match the end bone's rotation to the target (grab a handle the right
// way up, plant a hand flat on a wall). FootIK is the two-legs-on-ground special
// case; this is the single-limb "arms grabbing" tool.
//
// Assign upper (shoulder/hip), lower (elbow/knee) and end (hand/foot) bones.
// In a bone chain (lower under upper, end under lower) the bones TURN to reach:
// their lengths stay, a skinned mesh bends with them, and a weight below 1 bends
// the limb part of the way. Joints that aren't parented in a chain are moved to
// the solved positions instead. The bone lengths are read from the animated pose
// every frame (a height change, a new model or a rescale just works).
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Math/TwoBoneIK.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/IKFade.hpp"
#include "okay/Components/IKRestore.hpp"
#include <cmath>
#include <string>

namespace okay {

class LimbIK : public Component {
public:
    int ExecutionOrder() const override { return 100; }   // after animation
    Transform* upper = nullptr;        ///< shoulder / hip
    Transform* lower = nullptr;        ///< elbow / knee
    Transform* end   = nullptr;        ///< hand / foot

    Transform* targetObject = nullptr; ///< reach this object (overrides `target`)
    Vec3  target{0, 0, 0};
    Quat  targetRotation = Quat::Identity;   ///< the hand's rotation to match without a targetObject

    /// Where the elbow / knee bends when there's no poleObject.
    enum class PoleMode {
        Auto,        ///< keep the animation's bend; a straight limb bends toward `pole` (character space)
        Character,   ///< toward `pole`, in this object's space (turns with the character)
        World        ///< toward `pole`, a world direction
    };
    Transform* poleObject = nullptr;   ///< bend the elbow/knee toward this object
    Vec3  pole = Vec3::Forward;        ///< the bend direction (see poleMode)
    PoleMode poleMode = PoleMode::Auto;

    // Editor/serialized: resolve the references above by object name at Start.
    std::string upperName, lowerName, endName, targetName, poleName;

    float weight   = 1.0f;
    bool  ikActive = true;             ///< switch IK on/off; the change eases over fadeTime
    float fadeTime = 0.0f;             ///< seconds to blend IK in/out (0 = instant)
    float minBend  = 0.0f;             ///< joint limits (interior angle, degrees)
    float maxBend  = 180.0f;
    float softness = 0.0f;             ///< ease the last stretch to full reach over this distance (no snap straight)
    bool  matchTargetRotation = false; ///< align the end bone to the target's rotation (grab)
    bool  orient   = false;            ///< joints not in a chain: also turn the upper/lower bones toward
                                       ///< the moved joints (a chain always turns)

    void Start() override {
        Scene* s = GetScene();
        if (!s) return;
        auto R = [&](Transform*& t, const std::string& n) {
            if (!t && !n.empty()) if (GameObject* g = s->Find(n)) t = g->transform;
        };
        R(upper, upperName); R(lower, lowerName); R(end, endName);
        R(targetObject, targetName); R(poleObject, poleName);
    }

    // Solve in LateUpdate so the correction lands AFTER every animation driver
    // has posed the bones this frame (imported rigs create their per-node
    // Animators lazily, which puts them late in the Update order).
    void LateUpdate(float dt) override {
        m_restore.Restore();
        const float w = weight * m_fade.Step(ikActive, fadeTime, dt);
        if (w <= 0.0f || !upper || !lower || !end) return;
        const Vec3 root = upper->Position(), oldMid = lower->Position(), oldEnd = end->Position();
        const Vec3 tgt = targetObject ? targetObject->Position() : target;
        const float upLen = (oldMid - root).Magnitude(), loLen = (oldEnd - oldMid).Magnitude();
        // A missing or broken target (NaN, infinity) or a zero-length bone leaves the animated pose.
        if (!Finite(tgt) || !Finite(root) || !Finite(oldMid) || !Finite(oldEnd) || upLen < 1e-5f || loLen < 1e-5f) return;

        Vec3 mid, e;
        SolveTwoBoneIKSoft(root, upLen, loLen, tgt, PoleDir(root, oldMid, oldEnd), softness, mid, e, minBend, maxBend);
        m_restore.Touch(upper); m_restore.Touch(lower); m_restore.Touch(end);
        if (Chained()) {
            // Turn the bones (rotations blend by the weight, so lengths stay): the
            // upper toward the solved elbow, then the lower, as turned by its
            // parent, toward the solved hand.
            const Quat du = Quat::FromToRotation(oldMid - root, mid - root);
            const Quat ru = upper->Rotation(), rl = lower->Rotation();
            const Quat ruSolved = (du * ru).Normalized();
            const Quat rlSolved = (Quat::FromToRotation(du * (oldEnd - oldMid), e - mid) * du * rl).Normalized();
            const Quat endRot = end->Rotation();
            upper->SetRotation(Quat::Slerp(ru, ruSolved, w));
            lower->SetRotation(Quat::Slerp(rl, rlSolved, w));
            if (matchTargetRotation) end->SetRotation(endRot);   // matched below from the animated rotation
        } else {
            const Vec3 newMid = Vec3::Lerp(oldMid, mid, w), newEnd = Vec3::Lerp(oldEnd, e, w);
            // Rotate the bones so a mesh skinned to them bends with the joint, not
            // just the joint positions moving: the upper bone toward newMid, then
            // (re-read, since rotating the upper may have moved its children) the
            // lower toward newEnd. FromToRotation keeps each bone's roll.
            if (orient) {
                Vec3 oldUp = oldMid - root, newUp = newMid - root;
                if (oldUp.SqrMagnitude() > 1e-8f && newUp.SqrMagnitude() > 1e-8f)
                    upper->SetRotation(Quat::FromToRotation(oldUp, newUp) * upper->Rotation());
                Vec3 curMid = lower->Position(), curEnd = end->Position();
                Vec3 oldLo = curEnd - curMid, newLo = newEnd - newMid;
                if (oldLo.SqrMagnitude() > 1e-8f && newLo.SqrMagnitude() > 1e-8f)
                    lower->SetRotation(Quat::FromToRotation(oldLo, newLo) * lower->Rotation());
            }
            lower->SetPosition(newMid);
            end->SetPosition(newEnd);
        }
        if (matchTargetRotation)
            end->SetRotation(Quat::Slerp(end->Rotation(), targetObject ? targetObject->Rotation() : targetRotation, w));
        m_restore.Commit();
    }

private:
    IKFade m_fade;
    IKRestore m_restore;

    static bool Finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
    static bool Below(Transform* t, Transform* ancestor) {
        for (Transform* p = t ? t->Parent() : nullptr; p; p = p->Parent()) if (p == ancestor) return true;
        return false;
    }
    bool Chained() const { return Below(lower, upper) && Below(end, lower); }

    /// The direction the elbow / knee bends toward, this frame.
    Vec3 PoleDir(const Vec3& root, const Vec3& mid, const Vec3& e) const {
        if (poleObject) return poleObject->Position() - root;
        const Quat character = transform ? transform->Rotation() : Quat::Identity;
        if (poleMode == PoleMode::World) return pole;
        if (poleMode == PoleMode::Auto) {   // the animation's own bend plane, while it has one
            Vec3 ax = e - root;
            float len2 = ax.SqrMagnitude();
            if (len2 > 1e-10f) {
                Vec3 bend = (mid - root) - ax * (Vec3::Dot(mid - root, ax) / len2);
                if (bend.SqrMagnitude() > 1e-4f * len2) return bend;   // bent by over 1% of the limb
            }
        }
        return character * pole;
    }
};

} // namespace okay
