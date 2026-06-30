#pragma once
// ---------------------------------------------------------------------------
// ChainIK — full-body / long-chain IK driven by FABRIK (Unity's "fancy" solver
// tier). Drags an arbitrarily long bone chain so its tip reaches a target while
// keeping the root pinned and bone lengths fixed: spider legs, tentacles, a spine
// reaching, planting both hands on a wall. For 2-bone limbs prefer the cheaper
// closed-form TwoBoneIK; reach for this when the chain is long.
//
// Assign `bones` root-to-tip and a `target` (or `targetObject`). Lengths are
// learned from the rig on the first solve. Optionally re-orients bones along the
// chain so attached meshes follow.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Fabrik.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include <vector>

namespace okay {

class ChainIK : public Component {
public:
    std::vector<Transform*> bones;     ///< root..tip
    Transform* targetObject = nullptr;
    Vec3  target{0, 0, 0};
    float weight = 1.0f;               ///< blend IK over the animated pose
    int   iterations = 10;
    bool  orient = false;              ///< point each bone's forward down the chain
    Vec3  forwardAxis = Vec3::Forward;

    void Update(float) override {
        const int n = static_cast<int>(bones.size());
        if (weight <= 0.0f || n < 2) return;
        for (Transform* b : bones) if (!b) return;

        if (!m_init) {
            m_len.resize(n - 1);
            for (int i = 0; i < n - 1; ++i)
                m_len[i] = (bones[i + 1]->Position() - bones[i]->Position()).Magnitude();
            m_init = true;
        }

        std::vector<Vec3> p(n), orig(n);
        for (int i = 0; i < n; ++i) p[i] = orig[i] = bones[i]->Position();

        Vec3 tgt = targetObject ? targetObject->Position() : target;
        SolveFabrik(p, m_len, tgt, iterations);

        // Write positions root-to-tip so each child sees its parent's new pose.
        for (int i = 0; i < n; ++i)
            bones[i]->SetPosition(Vec3::Lerp(orig[i], p[i], weight));

        if (orient) {
            for (int i = 0; i < n - 1; ++i) {
                Vec3 dir = bones[i + 1]->Position() - bones[i]->Position();
                if (dir.SqrMagnitude() < 1e-8f) continue;
                Vec3 fwd = (bones[i]->Rotation() * forwardAxis).Normalized();
                bones[i]->SetRotation((Quat::FromToRotation(fwd, dir.Normalized()) * bones[i]->Rotation()).Normalized());
            }
        }
    }

private:
    bool m_init = false;
    std::vector<float> m_len;
};

} // namespace okay
