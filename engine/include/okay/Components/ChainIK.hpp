#pragma once
// ---------------------------------------------------------------------------
// ChainIK — full-body / long-chain IK driven by FABRIK ("fancy" solver
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
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Math/Fabrik.hpp"
#include "okay/Math/CCD.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/IKFade.hpp"
#include "okay/Components/IKBones.hpp"
#include <vector>
#include <string>

namespace okay {

class ChainIK : public Component {
public:
    int ExecutionOrder() const override { return 100; }   // after animation
    enum class Solver { FABRIK, CCD };

    std::vector<Transform*> bones;     ///< root..tip
    Transform* targetObject = nullptr;
    std::vector<std::string> boneNames; ///< editor/serialized: resolve `bones` by object names
    std::string targetName;            ///< editor/serialized: resolve `targetObject` by name
    Vec3  target{0, 0, 0};
    float weight = 1.0f;               ///< blend IK over the animated pose
    bool  ikActive = true;             ///< switch IK on/off; the change eases over fadeTime
    float fadeTime = 0.0f;             ///< seconds to blend IK in/out (0 = instant)
    // FABRIK constraints: bend the chain toward a pole (knee/elbow/curl direction)
    // and cap how sharply any joint may bend.
    Transform* poleObject = nullptr;   ///< bend toward this object (overrides `pole`)
    std::string poleName;              ///< editor/serialized: resolve `poleObject` by name
    bool  usePole = false;             ///< use `pole` (a world point) when there's no poleObject
    Vec3  pole{0, 0, 1};
    float maxJointBend = 0.0f;         ///< max bend per joint in degrees (0 = unlimited)
    float tolerance = 1e-3f;           ///< stop iterating once the tip is this close
    int   iterations = 10;
    int   solver = (int)Solver::FABRIK; ///< FABRIK (position) or CCD (rotation-from-tip)
    bool  orient = false;              ///< point each bone's forward down the chain
    Vec3  forwardAxis = Vec3::Forward;

    void Start() override {
        // Names resolve inside this character first (two copies of one model
        // share bone names), then anywhere in the scene.
        Scene* s = GetScene();
        if (bones.empty() && !boneNames.empty())
            for (const std::string& nm : boneNames)
                if (Transform* t = ResolveBone(s, gameObject, nm)) bones.push_back(t);
        if (!targetObject) targetObject = ResolveBone(s, gameObject, targetName);
        if (!poleObject) poleObject = ResolveBone(s, gameObject, poleName);
    }

    // Solve in LateUpdate so the correction lands AFTER every animation driver
    // has posed the bones this frame (imported rigs create their per-node
    // Animators lazily, which puts them late in the Update order).
    void LateUpdate(float dt) override {
        const int n = static_cast<int>(bones.size());
        const float w = weight * m_fade.Step(ikActive, fadeTime, dt);
        if (w <= 0.0f || n < 2) return;
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
        Vec3 poleAt = poleObject ? poleObject->Position() : pole;
        const bool hasPole = poleObject || usePole;
        if (solver == (int)Solver::CCD) SolveCCD(p, tgt, iterations);
        else if (hasPole || maxJointBend > 0.0f)
            m_error = SolveFabrikConstrained(p, m_len, tgt, iterations, tolerance, hasPole ? &poleAt : nullptr, maxJointBend);
        else                            SolveFabrik(p, m_len, tgt, iterations, tolerance);
        if (solver == (int)Solver::CCD || (!hasPole && maxJointBend <= 0.0f)) m_error = (p[n - 1] - tgt).Magnitude();

        // Write positions root-to-tip so each child sees its parent's new pose.
        for (int i = 0; i < n; ++i)
            bones[i]->SetPosition(Vec3::Lerp(orig[i], p[i], w));

        if (orient) {
            for (int i = 0; i < n - 1; ++i) {
                Vec3 dir = bones[i + 1]->Position() - bones[i]->Position();
                if (dir.SqrMagnitude() < 1e-8f) continue;
                Vec3 fwd = (bones[i]->Rotation() * forwardAxis).Normalized();
                bones[i]->SetRotation((Quat::FromToRotation(fwd, dir.Normalized()) * bones[i]->Rotation()).Normalized());
            }
        }
    }

    /// Distance from the chain tip to the target after the last solve (0 = reached).
    float Error() const { return m_error; }

private:
    bool m_init = false;
    std::vector<float> m_len;
    IKFade m_fade;
    float m_error = 0.0f;
};

} // namespace okay
