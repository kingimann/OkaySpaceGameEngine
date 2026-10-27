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
#include "okay/Math/IKMath.hpp"
#include "okay/Math/JointLimit.hpp"
#include "okay/Components/IKRestore.hpp"
#include "okay/Components/IKBones.hpp"
#include <cmath>
#include <string>

namespace okay {

class LimbIK : public Component {
public:
    int ExecutionOrder() const override { return 105; }   // after the body solvers (FootIK 85, FullBodyIK 100)
    Transform* upper = nullptr;        ///< shoulder / hip
    Transform* lower = nullptr;        ///< elbow / knee
    Transform* end   = nullptr;        ///< hand / foot

    Transform* targetObject = nullptr; ///< reach this object (overrides `target`)
    Vec3  target{0, 0, 0};
    Quat  targetRotation = Quat::Identity;   ///< the hand's rotation to match without a targetObject

    /// Where the elbow / knee bends when there's no poleObject.
    enum class PoleMode {
        Auto,        ///< keep the animation's bend; as the limb straightens it eases toward `pole` (character space)
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
    /// Per-bone swing/twist limits, measured from each bone's rest pose (see
    /// JointLimit.hpp). Default None = the old behaviour. A Hinge on the lower
    /// bone doesn't re-decide the bend (minBend/maxBend do); it stops the twist.
    IKJointLimit upperLimit, lowerLimit;
    float softness = 0.0f;             ///< ease the last stretch to full reach over this distance (no snap straight)
    bool  matchTargetRotation = false; ///< align the end bone to the target's rotation (grab)
    bool  orient   = false;            ///< joints not in a chain: also turn the upper/lower bones toward
                                       ///< the moved joints (a chain always turns)
    /// Seconds to swing over when the goal switches (another target object, object
    /// <-> point) or jumps; measured on the character, so moving it never trails.
    float targetBlendTime = 0.15f;

    void Start() override {
        // Names resolve inside this character first (two copies of one model
        // share bone names), then anywhere in the scene.
        Scene* s = GetScene();
        auto R = [&](Transform*& t, const std::string& n) { if (!t) t = ResolveBone(s, gameObject, n); };
        R(upper, upperName); R(lower, lowerName); R(end, endName);
        R(targetObject, targetName); R(poleObject, poleName);
        m_pole.Reset();
    }

    // Solve in LateUpdate so the correction lands AFTER every animation driver
    // has posed the bones this frame (imported rigs create their per-node
    // Animators lazily, which puts them late in the Update order).
    void LateUpdate(float dt) override {
        m_restore.Restore();
        const float w = weight * m_fade.Step(ikActive, fadeTime, dt);
        if (w <= 0.0f || !upper || !lower || !end) { m_goal.Reset(); return; }
        const Vec3 root = upper->Position(), oldMid = lower->Position(), oldEnd = end->Position();
        const Vec3 tgt = m_goal.Step(targetObject ? targetObject->Position() : target, (std::uintptr_t)targetObject,
                                     targetBlendTime, dt, CharacterSpace());
        const float upLen = (oldMid - root).Magnitude(), loLen = (oldEnd - oldMid).Magnitude();
        // A missing or broken target (NaN, infinity) or a zero-length bone leaves the animated pose.
        if (!Finite(tgt) || !Finite(root) || !Finite(oldMid) || !Finite(oldEnd) || upLen < 1e-5f || loLen < 1e-5f) return;

        const Vec3 bendDir = m_pole.Direction(root, oldMid, oldEnd, transform ? transform->Rotation() : Quat::Identity,
                                              static_cast<IKPoleMode>(static_cast<int>(poleMode)), pole, poleObject);
        Vec3 mid, e;
        SolveTwoBoneIKSoft(root, upLen, loLen, tgt, bendDir, softness, mid, e, minBend, maxBend);
        m_restore.Touch(upper); m_restore.Touch(lower); m_restore.Touch(end);
        if (Chained()) {
            // Turn the bones (rotations blend by the weight, so lengths stay): the
            // upper toward the solved elbow, then the lower, as turned by its
            // parent, toward the solved hand. Joint limits clamp each solved
            // rotation around its rest frame before the weight blend.
            const Quat ru = upper->Rotation(), rl = lower->Rotation(), endRot = end->Rotation();
            if (upperLimit.Active() || lowerLimit.Active()) CaptureLimitFrames(root, oldMid, oldEnd, mid, e);
            upper->SetRotation(AimBone(upper, oldMid, mid));
            if (upperLimit.Active())
                upper->localRotation = ApplyLimit(upper->localRotation, m_upFrame, upperLimit);
            lower->SetRotation(AimBone(lower, end->Position(), e));
            if (lowerLimit.Active())
                lower->localRotation = ApplyLimit(lower->localRotation, m_loFrame, lowerLimit,
                                                  lowerLimit.type == IKJointLimit::Type::Hinge);
            const Quat rlSolved = lower->Rotation();
            BlendWorldRotation(upper, ru, upper->Rotation(), w);
            BlendWorldRotation(lower, rl, rlSolved, w);
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
    IKPole m_pole;   ///< the bend direction, eased as the limb straightens (see IKMath.hpp)
    IKJointFrame m_upFrame, m_loFrame;   ///< rest frames the limits measure from
    IKGoalBlend m_goal;

    const Transform* CharacterSpace() const {
        const Transform* r = CharacterRootOf(transform);
        return r ? r : transform;
    }

    bool Chained() const { return IsUnder(lower, upper) && IsUnder(end, lower); }

    /// Capture the rest frames when the bones (re)resolve. A straight rest pose
    /// has no bend plane, so the hinge axis then comes from this solve's bend.
    void CaptureLimitFrames(const Vec3& root, const Vec3& oldMid, const Vec3& oldEnd,
                            const Vec3& mid, const Vec3& e) {
        if (!m_upFrame.CapturedFor(upper)) m_upFrame.Capture(upper, lower, end);
        if (!m_loFrame.CapturedFor(lower)) m_loFrame.Capture(lower, end, nullptr);
        if (!m_upFrame.hasHinge) m_upFrame.SetHingeFromBend(root, oldMid, oldEnd);
        if (!m_loFrame.hasHinge) m_loFrame.SetHingeFromBend(root, oldMid, oldEnd);
        if (!m_upFrame.hasHinge) m_upFrame.SetHingeFromBend(root, mid, e);
        if (!m_loFrame.hasHinge) m_loFrame.SetHingeFromBend(root, mid, e);
    }
};

} // namespace okay
