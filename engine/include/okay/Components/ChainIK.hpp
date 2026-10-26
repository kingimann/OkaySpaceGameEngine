#pragma once
// ---------------------------------------------------------------------------
// ChainIK — full-body / long-chain IK driven by FABRIK ("fancy" solver
// tier). Drags an arbitrarily long bone chain so its tip reaches a target while
// keeping the root pinned and bone lengths fixed: spider legs, tentacles, a spine
// reaching, planting both hands on a wall. For 2-bone limbs prefer the cheaper
// closed-form TwoBoneIK (Limb IK); reach for this when the chain is long.
//
// Assign `bones` root-to-tip and a `target` (or `targetObject`). In a bone chain
// (each bone under the one before) the bones TURN toward the solve, root to tip,
// so their lengths stay and a weight below 1 bends part of the way; bones that
// aren't parented in a chain are moved to the solved points. The lengths are read
// from the animated pose every frame. Both solvers keep to the pole, the bend cap
// and the per-joint limits.
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
#include "okay/Components/IKRestore.hpp"
#include <cmath>
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
    // Constraints (both solvers): bend the chain toward a pole (knee/elbow/curl
    // direction), cap how sharply any joint may bend, and per-joint limits.
    Transform* poleObject = nullptr;   ///< bend toward this object (overrides `pole`)
    std::string poleName;              ///< editor/serialized: resolve `poleObject` by name
    bool  usePole = false;             ///< use `pole` (a world point) when there's no poleObject
    Vec3  pole{0, 0, 1};
    float maxJointBend = 0.0f;         ///< max bend per joint in degrees (0 = unlimited)
    std::vector<JointLimit> limits;    ///< per joint, root..tip (a swing cone, a hinge); may be shorter than bones
    float tolerance = 1e-3f;           ///< stop iterating once the tip is this close
    int   iterations = 10;
    int   solver = (int)Solver::FABRIK; ///< FABRIK (position) or CCD (rotation-from-tip)
    bool  orient = false;              ///< bones not in a chain: point each bone's forward down the chain
    Vec3  forwardAxis = Vec3::Forward;

    void Start() override {
        Scene* s = GetScene();
        if (!s) return;
        if (bones.empty() && !boneNames.empty())
            for (const std::string& nm : boneNames)
                if (GameObject* g = s->Find(nm)) bones.push_back(g->transform);
        if (!targetObject && !targetName.empty())
            if (GameObject* g = s->Find(targetName)) targetObject = g->transform;
        if (!poleObject && !poleName.empty())
            if (GameObject* g = s->Find(poleName)) poleObject = g->transform;
    }

    // Solve in LateUpdate so the correction lands AFTER every animation driver
    // has posed the bones this frame (imported rigs create their per-node
    // Animators lazily, which puts them late in the Update order).
    void LateUpdate(float dt) override {
        m_restore.Restore();
        const int n = static_cast<int>(bones.size());
        const float w = weight * m_fade.Step(ikActive, fadeTime, dt);
        if (w <= 0.0f || n < 2) return;
        for (Transform* b : bones) if (!b) return;

        std::vector<Vec3> p(n), orig(n);
        std::vector<float> len(n - 1);
        float total = 0.0f;
        for (int i = 0; i < n; ++i) {
            p[i] = orig[i] = bones[i]->Position();
            if (!Finite(p[i])) return;
            if (i > 0) total += len[i - 1] = (p[i] - p[i - 1]).Magnitude();
        }
        const Vec3 tgt = targetObject ? targetObject->Position() : target;
        const Vec3 poleAt = poleObject ? poleObject->Position() : pole;
        // A broken target (NaN, infinity) or a chain with no length leaves the animated pose.
        if (!Finite(tgt) || total < 1e-5f || ((poleObject || usePole) && !Finite(poleAt))) return;

        ChainConstraints c;
        if (poleObject || usePole) c.pole = &poleAt;
        c.maxBendDeg = maxJointBend;
        std::vector<Quat> restRot;
        if (!limits.empty()) {
            restRot.resize(n);
            for (int i = 0; i < n; ++i) restRot[i] = bones[i]->Rotation();
            c.limits = &limits; c.rest = &orig; c.restRot = &restRot;
        }
        m_error = SolveChainConstrained(p, len, tgt, iterations, tolerance, c, solver == (int)Solver::CCD);

        for (Transform* b : bones) m_restore.Touch(b);
        if (Chained()) {
            // Root to tip: turn each bone so its child lies along the solved
            // segment, re-reading the child after the bones above it turned.
            for (int i = 0; i < n - 1; ++i) {
                const Vec3 cur = bones[i + 1]->Position() - bones[i]->Position(), want = p[i + 1] - p[i];
                if (cur.SqrMagnitude() < 1e-12f || want.SqrMagnitude() < 1e-12f) continue;
                const Quat r = bones[i]->Rotation();
                bones[i]->SetRotation(Quat::Slerp(r, (Quat::FromToRotation(cur, want) * r).Normalized(), w));
            }
        } else {
            for (int i = 0; i < n; ++i) bones[i]->SetPosition(Vec3::Lerp(orig[i], p[i], w));
            if (orient) {   // rotating a bone moves none of the others here
                for (int i = 0; i < n - 1; ++i) {
                    Vec3 dir = bones[i + 1]->Position() - bones[i]->Position();
                    if (dir.SqrMagnitude() < 1e-8f) continue;
                    Vec3 fwd = (bones[i]->Rotation() * forwardAxis).Normalized();
                    bones[i]->SetRotation((Quat::FromToRotation(fwd, dir.Normalized()) * bones[i]->Rotation()).Normalized());
                }
            }
        }
        m_restore.Commit();
    }

    /// Distance from the chain tip to the target after the last solve (0 = reached).
    float Error() const { return m_error; }

private:
    IKFade m_fade;
    IKRestore m_restore;
    float m_error = 0.0f;

    static bool Finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
    /// Each bone sits under the one before it (other bones may come between).
    bool Chained() const {
        for (std::size_t i = 1; i < bones.size(); ++i) {
            bool below = false;
            for (Transform* t = bones[i]->Parent(); t && !below; t = t->Parent()) below = t == bones[i - 1];
            if (!below) return false;
        }
        return true;
    }
};

} // namespace okay
