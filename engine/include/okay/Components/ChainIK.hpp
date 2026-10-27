#pragma once
// ---------------------------------------------------------------------------
// ChainIK — full-body / long-chain IK driven by FABRIK or CCD ("fancy" solver
// tier). Drags an arbitrarily long bone chain so its tip reaches a target while
// keeping the root pinned and bone lengths fixed: spider legs, tentacles, a spine
// reaching, planting both hands on a wall. For 2-bone limbs prefer the cheaper
// closed-form TwoBoneIK; reach for this when the chain is long.
//
// Assign `bones` root-to-tip and a `target` (or `targetObject`). A parented
// chain (each bone under the previous) TURNS its bones to the solve: lengths
// stay exact at any weight, a skinned mesh bends with the joints, and per-joint
// limits clamp each bone around its rest pose. Joints that aren't parented in a
// chain are moved to the solved positions instead (the old path). Lengths are
// read from the pose every frame, so a rescale or a new model just works.
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
#include "okay/Math/IKMath.hpp"
#include "okay/Math/JointLimit.hpp"
#include "okay/Components/IKRestore.hpp"
#include "okay/Components/IKBones.hpp"
#include <vector>
#include <string>

namespace okay {

class ChainIK : public Component {
public:
    int ExecutionOrder() const override { return 105; }   // after the body solvers (FootIK 85, FullBodyIK 100)
    enum class Solver { FABRIK, CCD };

    std::vector<Transform*> bones;     ///< root..tip
    Transform* targetObject = nullptr;
    std::vector<std::string> boneNames; ///< editor/serialized: resolve `bones` by object names
    std::string targetName;            ///< editor/serialized: resolve `targetObject` by name
    Vec3  target{0, 0, 0};
    float weight = 1.0f;               ///< blend IK over the animated pose
    bool  ikActive = true;             ///< switch IK on/off; the change eases over fadeTime
    float fadeTime = 0.0f;             ///< seconds to blend IK in/out (0 = instant)
    // The pole: which way the chain bends. poleObject overrides everything;
    // otherwise poleMode picks how `pole` / `poleDir` is read.
    Transform* poleObject = nullptr;   ///< bend toward this object (overrides the modes)
    std::string poleName;              ///< editor/serialized: resolve `poleObject` by name
    /// -1 = legacy point: bend toward `pole`, a world POINT, when usePole is set
    /// (what old scenes and `ik_set_pole` mean). 0..2 = IKPoleMode Auto /
    /// Character / World on `poleDir`, a DIRECTION — the same convention LimbIK
    /// uses, eased by IKPole so a straightening chain doesn't flip.
    int   poleMode = -1;
    bool  usePole = false;             ///< legacy mode: use `pole` (a world point)
    Vec3  pole{0, 0, 1};               ///< legacy mode: the world point
    Vec3  poleDir = Vec3::Forward;     ///< direction for the Auto/Character/World modes
    /// Per-joint limits, same size as `bones` (missing or None = free). When a
    /// joint has none and maxJointBend is set, it gets a Ball cone of that angle.
    std::vector<IKJointLimit> limits;
    float maxJointBend = 0.0f;         ///< max bend per joint in degrees (0 = unlimited)
    float tolerance = 1e-3f;           ///< stop iterating once the tip is this close
    int   iterations = 10;
    int   solver = (int)Solver::FABRIK; ///< FABRIK (reach whole-chain) or CCD (tip-first)
    /// Legacy: a parented chain always turns its bones now. Only the unparented
    /// position path still reads it (point each bone's forward down the chain).
    bool  orient = false;
    Vec3  forwardAxis = Vec3::Forward;
    /// Seconds to swing over when the goal switches (another target object, object
    /// <-> point) or jumps; measured on the character, so moving it never trails.
    float targetBlendTime = 0.15f;

    void Start() override {
        // Names resolve inside this character first (two copies of one model
        // share bone names), then anywhere in the scene.
        Scene* s = GetScene();
        if (bones.empty() && !boneNames.empty())
            for (const std::string& nm : boneNames)
                if (Transform* t = ResolveBone(s, gameObject, nm)) bones.push_back(t);
        if (!targetObject) targetObject = ResolveBone(s, gameObject, targetName);
        if (!poleObject) poleObject = ResolveBone(s, gameObject, poleName);
        m_pole.Reset();
    }

    // Solve in LateUpdate so the correction lands AFTER every animation driver
    // has posed the bones this frame (imported rigs create their per-node
    // Animators lazily, which puts them late in the Update order).
    void LateUpdate(float dt) override {
        m_restore.Restore();
        const int n = static_cast<int>(bones.size());
        const float w = weight * m_fade.Step(ikActive, fadeTime, dt);
        if (w <= 0.0f || n < 2) { m_goal.Reset(); return; }
        for (Transform* b : bones) if (!b) { m_goal.Reset(); return; }

        // Lengths live each frame: a height slider, a rescale or a swapped
        // model never leaves the solve on stale bones.
        std::vector<Vec3> p(n);
        std::vector<float> len(n - 1);
        float total = 0.0f;
        for (int i = 0; i < n; ++i) {
            p[i] = bones[i]->Position();
            if (!Finite(p[i])) return;   // a broken pose: leave the animated frame
        }
        for (int i = 0; i < n - 1; ++i) total += len[i] = (p[i + 1] - p[i]).Magnitude();
        const Transform* space = CharacterRootOf(transform);
        const Vec3 tgt = m_goal.Step(targetObject ? targetObject->Position() : target, (std::uintptr_t)targetObject,
                                     targetBlendTime, dt, space ? space : transform);
        if (!Finite(tgt) || !(total > 1e-6f)) return;

        // The pole as FABRIK's world point: an object or the legacy point as
        // given; a direction mode becomes a point a chain-length out.
        Vec3 poleAt; bool hasPole = false;
        if (poleObject) { poleAt = poleObject->Position(); hasPole = Finite(poleAt); }
        else if (poleMode >= 0) {
            const int mid = n > 2 ? (n - 1) / 2 : 0;   // n = 2 has no interior joint: Auto falls to its fallback
            const Vec3 dir = m_pole.Direction(p[0], p[mid], p[n - 1],
                                              transform ? transform->Rotation() : Quat::Identity,
                                              static_cast<IKPoleMode>(poleMode), poleDir, nullptr);
            poleAt = p[0] + SafeNormalize(dir, Vec3::Forward) * total;
            hasPole = true;
        } else if (usePole) { poleAt = pole; hasPole = Finite(poleAt); }

        if (Chained()) {
            for (Transform* b : bones) m_restore.Touch(b);
            CaptureFrames();
            std::vector<Quat> animRot(n);
            for (int i = 0; i < n; ++i) animRot[i] = bones[i]->Rotation();
            const bool ccd = solver == (int)Solver::CCD;
            const int rounds = iterations < 1 ? 1 : iterations;
            for (int it = 0; it < rounds; ++it) {
                if (ccd) {
                    // CCD rotation-native: each joint, tip to root, turns so the
                    // tip swings toward the target; its limit clamps right there.
                    for (int i = n - 2; i >= 0; --i) {
                        bones[i]->SetRotation(AimBone(bones[i], bones[n - 1]->Position(), tgt));
                        Clamp(i);
                    }
                } else {
                    // FABRIK finds positions, then a root-to-tip rotation pass
                    // turns each bone so its child lands on the solved joint,
                    // re-reading after each parent turns; limits clamp per bone.
                    for (int i = 0; i < n; ++i) p[i] = bones[i]->Position();
                    std::vector<Vec3> solved = p;
                    SolveFabrikConstrained(solved, len, tgt, 2, tolerance, hasPole ? &poleAt : nullptr, maxJointBend);
                    for (int i = 0; i < n - 1; ++i) {
                        bones[i]->SetRotation(AimBone(bones[i], bones[i + 1]->Position(), solved[i + 1]));
                        Clamp(i);
                    }
                }
                if ((bones[n - 1]->Position() - tgt).Magnitude() < tolerance) break;
            }
            if (ccd && hasPole) ApplyCCDPole(p[0], tgt, poleAt);
            m_error = (bones[n - 1]->Position() - tgt).Magnitude();
            // Blend WORLD rotations from the animated pose by the weight: a half
            // weight bends the chain halfway, with every length exact.
            for (int i = 0; i < n; ++i) BlendWorldRotation(bones[i], animRot[i], bones[i]->Rotation(), w);
            m_restore.Commit();
            return;
        }

        // Unparented joints: the old position path.
        std::vector<Vec3> orig = p;
        if (solver == (int)Solver::CCD) SolveCCD(p, tgt, iterations);
        else if (hasPole || maxJointBend > 0.0f)
            SolveFabrikConstrained(p, len, tgt, iterations, tolerance, hasPole ? &poleAt : nullptr, maxJointBend);
        else SolveFabrik(p, len, tgt, iterations, tolerance);
        m_error = (p[n - 1] - tgt).Magnitude();
        for (Transform* b : bones) m_restore.Touch(b);
        // Write positions root-to-tip so each child sees its parent's new pose.
        for (int i = 0; i < n; ++i)
            bones[i]->SetPosition(Vec3::Lerp(orig[i], p[i], w));
        if (orient) {
            // Root to tip, re-reading each position, so a bone parented to the
            // one just turned still aims at where its child now is.
            for (int i = 0; i < n - 1; ++i) {
                Vec3 dir = bones[i + 1]->Position() - bones[i]->Position();
                if (dir.SqrMagnitude() < 1e-8f) continue;
                Vec3 fwd = (bones[i]->Rotation() * forwardAxis).Normalized();
                bones[i]->SetRotation((Quat::FromToRotation(fwd, dir.Normalized()) * bones[i]->Rotation()).Normalized());
            }
        }
        m_restore.Commit();
    }

    /// Distance from the chain tip to the target after the last solve (0 = reached).
    float Error() const { return m_error; }

private:
    IKFade m_fade;
    IKGoalBlend m_goal;
    IKRestore m_restore;
    IKPole m_pole;
    std::vector<IKJointFrame> m_frames;   ///< rest frames the limits measure from
    float m_error = 0.0f;

    bool Chained() const {
        for (std::size_t i = 0; i + 1 < bones.size(); ++i)
            if (!IsUnder(bones[i + 1], bones[i])) return false;
        return true;
    }

    /// The limit joint `i` solves under: its own, else the maxJointBend cone.
    IKJointLimit EffLimit(int i) const {
        if (i < (int)limits.size() && limits[i].Active()) return limits[i];
        if (maxJointBend > 0.0f && maxJointBend < 180.0f) return IKJointLimit::Ball(maxJointBend, 180.0f);
        return {};
    }

    void Clamp(int i) {
        const IKJointLimit l = EffLimit(i);
        if (l.Active()) bones[i]->localRotation = ApplyLimit(bones[i]->localRotation, m_frames[i], l);
    }

    void CaptureFrames() {
        const int n = static_cast<int>(bones.size());
        if ((int)m_frames.size() != n) m_frames.assign(n, {});
        for (int i = 0; i < n; ++i)
            if (!m_frames[i].CapturedFor(bones[i]))
                m_frames[i].Capture(bones[i], i + 1 < n ? bones[i + 1] : nullptr, i + 2 < n ? bones[i + 2] : nullptr);
    }

    /// CCD solved without a pole in the loop: afterwards, twist the whole chain
    /// about the root->tip axis so its bend faces the pole point.
    void ApplyCCDPole(const Vec3& root, const Vec3& tgt, const Vec3& poleAt) {
        const int n = static_cast<int>(bones.size());
        Vec3 axis = bones[n - 1]->Position() - root;
        const float al = axis.Magnitude();
        if (al < 1e-5f) return;
        axis = axis * (1.0f / al);
        const int mid = n / 2;
        auto perp = [&](const Vec3& v) { Vec3 d = v - root; return d - axis * Vec3::Dot(d, axis); };
        const Vec3 bend = perp(bones[mid]->Position()), want = perp(poleAt);
        if (bend.SqrMagnitude() < 1e-8f || want.SqrMagnitude() < 1e-8f) return;
        const Vec3 a = bend.Normalized(), b = want.Normalized();
        float c = Vec3::Dot(a, b); c = c > 1.0f ? 1.0f : (c < -1.0f ? -1.0f : c);
        float deg = std::acos(c) * 57.29578f;
        if (Vec3::Dot(Vec3::Cross(a, b), axis) < 0.0f) deg = -deg;
        bones[0]->SetRotation((Quat::AngleAxis(deg, axis) * bones[0]->Rotation()).Normalized());
        Clamp(0);
    }
};

} // namespace okay
