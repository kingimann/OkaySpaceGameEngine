#pragma once
// ---------------------------------------------------------------------------
// LookAtIK — head/eye tracking (Unity's Look-At IK). Rotates a chain of bones
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
#include <vector>
#include <string>

namespace okay {

class LookAtIK : public Component {
public:
    std::vector<Transform*> chain;     ///< bones root..tip (e.g. spine, neck, head)
    Transform* targetObject = nullptr; ///< look at this object (overrides `target`)
    std::vector<std::string> chainNames; ///< editor/serialized: resolve `chain` by object names
    std::string targetName;            ///< editor/serialized: resolve `targetObject` by name
    Vec3  target{0, 0, 0};             ///< world point to look at (if no targetObject)
    Vec3  forwardAxis = Vec3::Forward; ///< the bones' local "forward"
    float weight   = 1.0f;             ///< 0 = off, 1 = full look
    float maxAngle = 80.0f;            ///< max turn per bone (degrees), keeps it natural

    void Start() override {
        Scene* s = GetScene();
        if (!s) return;
        if (chain.empty() && !chainNames.empty())
            for (const std::string& n : chainNames)
                if (GameObject* g = s->Find(n)) chain.push_back(g->transform);
        if (!targetObject && !targetName.empty())
            if (GameObject* g = s->Find(targetName)) targetObject = g->transform;
    }

    void Update(float) override {
        if (weight <= 0.0f || chain.empty()) return;
        Vec3 tgt = targetObject ? targetObject->Position() : target;
        // Root-to-tip: each bone aims its forward at the target. Processing in order
        // means later bones see the positions their parents just moved them to.
        for (Transform* b : chain) {
            if (!b) continue;
            Vec3 dir = tgt - b->Position();
            if (dir.SqrMagnitude() < 1e-8f) continue;
            dir = dir.Normalized();
            Vec3 fwd = (b->Rotation() * forwardAxis).Normalized();
            Quat desired = (Quat::FromToRotation(fwd, dir) * b->Rotation()).Normalized();
            float ang = Quat::Angle(b->Rotation(), desired);
            float t = weight;
            if (ang > maxAngle && ang > 1e-4f) t *= maxAngle / ang;   // clamp the turn
            b->SetRotation(Quat::Slerp(b->Rotation(), desired, t));
        }
    }
};

} // namespace okay
