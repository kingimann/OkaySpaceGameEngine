#pragma once
// ---------------------------------------------------------------------------
// LimbIK — general two-bone IK for one limb (arm or leg): reach a target with the
// hand/foot, bend the elbow/knee toward a pole hint, respect joint limits, and
// optionally match the end bone's rotation to the target (grab a handle the right
// way up, plant a hand flat on a wall). FootIK is the two-legs-on-ground special
// case; this is the single-limb "arms grabbing" tool.
//
// Assign upper (shoulder/hip), lower (elbow/knee) and end (hand/foot) bones.
// Bone lengths are learned from the rig on the first solve.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Math/TwoBoneIK.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/IKFade.hpp"
#include <string>

namespace okay {

class LimbIK : public Component {
public:
    Transform* upper = nullptr;        ///< shoulder / hip
    Transform* lower = nullptr;        ///< elbow / knee
    Transform* end   = nullptr;        ///< hand / foot

    Transform* targetObject = nullptr; ///< reach this object (overrides `target`)
    Vec3  target{0, 0, 0};

    Transform* poleObject = nullptr;   ///< bend the elbow/knee toward this object
    Vec3  pole = Vec3::Forward;        ///< or a world bend direction when no poleObject

    // Editor/serialized: resolve the references above by object name at Start.
    std::string upperName, lowerName, endName, targetName, poleName;

    float weight   = 1.0f;
    bool  ikActive = true;             ///< switch IK on/off; the change eases over fadeTime
    float fadeTime = 0.0f;             ///< seconds to blend IK in/out (0 = instant)
    float minBend  = 0.0f;             ///< joint limits (interior angle, degrees)
    float maxBend  = 180.0f;
    bool  matchTargetRotation = false; ///< align the end bone to the target's rotation (grab)
    bool  orient   = false;            ///< also rotate the upper/lower bones to follow the
                                       ///< new joint positions (so meshes skinned to the bones
                                       ///< bend with the limb instead of only the joints moving)

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
        const float w = weight * m_fade.Step(ikActive, fadeTime, dt);
        if (w <= 0.0f || !upper || !lower || !end) return;
        if (!m_init) {
            m_up = (lower->Position() - upper->Position()).Magnitude();
            m_lo = (end->Position() - lower->Position()).Magnitude();
            m_init = true;
        }
        if (m_up <= 0.0f || m_lo <= 0.0f) return;

        Vec3 root = upper->Position();
        Vec3 tgt = targetObject ? targetObject->Position() : target;
        Vec3 poleDir = poleObject ? (poleObject->Position() - root) : pole;

        Vec3 oldMid = lower->Position();
        Vec3 oldEnd = end->Position();
        Vec3 mid, e;
        SolveTwoBoneIK(root, m_up, m_lo, tgt, poleDir, mid, e, minBend, maxBend);
        Vec3 newMid = Vec3::Lerp(oldMid, mid, w);
        Vec3 newEnd = Vec3::Lerp(oldEnd, e, w);

        // Optional: rotate the bones so a mesh skinned to them bends with the
        // joint, not just the joint positions moving. Do this BEFORE pinning the
        // joint positions: rotate the upper bone to point root->newMid, then
        // (recomputing, since rotating the upper moved its children) rotate the
        // lower bone to point newMid->newEnd. FromToRotation preserves each
        // bone's roll about its own axis.
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

        if (matchTargetRotation && targetObject)
            end->SetRotation(Quat::Slerp(end->Rotation(), targetObject->Rotation(), w));
    }

private:
    bool  m_init = false;
    float m_up = 0.0f, m_lo = 0.0f;
    IKFade m_fade;
};

} // namespace okay
