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
    float minBend  = 0.0f;             ///< joint limits (interior angle, degrees)
    float maxBend  = 180.0f;
    bool  matchTargetRotation = false; ///< align the end bone to the target's rotation (grab)

    void Start() override {
        Scene* s = GetScene();
        if (!s) return;
        auto R = [&](Transform*& t, const std::string& n) {
            if (!t && !n.empty()) if (GameObject* g = s->Find(n)) t = g->transform;
        };
        R(upper, upperName); R(lower, lowerName); R(end, endName);
        R(targetObject, targetName); R(poleObject, poleName);
    }

    void Update(float) override {
        if (weight <= 0.0f || !upper || !lower || !end) return;
        if (!m_init) {
            m_up = (lower->Position() - upper->Position()).Magnitude();
            m_lo = (end->Position() - lower->Position()).Magnitude();
            m_init = true;
        }
        if (m_up <= 0.0f || m_lo <= 0.0f) return;

        Vec3 root = upper->Position();
        Vec3 tgt = targetObject ? targetObject->Position() : target;
        Vec3 poleDir = poleObject ? (poleObject->Position() - root) : pole;

        Vec3 mid, e;
        SolveTwoBoneIK(root, m_up, m_lo, tgt, poleDir, mid, e, minBend, maxBend);
        lower->SetPosition(Vec3::Lerp(lower->Position(), mid, weight));
        end->SetPosition(Vec3::Lerp(end->Position(), e, weight));

        if (matchTargetRotation && targetObject)
            end->SetRotation(Quat::Slerp(end->Rotation(), targetObject->Rotation(), weight));
    }

private:
    bool  m_init = false;
    float m_up = 0.0f, m_lo = 0.0f;
};

} // namespace okay
