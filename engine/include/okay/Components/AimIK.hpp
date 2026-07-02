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
#include <string>

namespace okay {

class AimIK : public Component {
public:
    Transform* bone = nullptr;         ///< the bone to aim (defaults to this object's transform)
    Transform* targetObject = nullptr;
    std::string boneName;              ///< editor/serialized: resolve `bone` by object name at Start
    std::string targetName;            ///< editor/serialized: resolve `targetObject` by object name
    Vec3  target{0, 0, 0};
    Vec3  aimAxis = Vec3::Forward;     ///< the bone-local axis that should point at the target
    Vec3  upAxis  = Vec3::Up;          ///< keeps the bone from rolling around the aim axis
    float weight  = 1.0f;
    float maxAngle = 180.0f;           ///< clamp the turn from the rest pose (degrees)

    void Start() override {
        Scene* s = GetScene();
        if (!s) return;
        if (!bone && !boneName.empty())
            if (GameObject* g = s->Find(boneName)) bone = g->transform;
        if (!targetObject && !targetName.empty())
            if (GameObject* g = s->Find(targetName)) targetObject = g->transform;
    }

    void Update(float) override {
        if (weight <= 0.0f) return;
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

        float ang = Quat::Angle(b->Rotation(), desired);
        float t = weight;
        if (ang > maxAngle && ang > 1e-4f) t *= maxAngle / ang;
        b->SetRotation(Quat::Slerp(b->Rotation(), desired, t));
    }
};

} // namespace okay
