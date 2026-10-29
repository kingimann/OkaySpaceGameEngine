#pragma once
// ---------------------------------------------------------------------------
// LookAtIK — head/eye tracking. Rotates a chain of bones
// (e.g. spine -> neck -> head) so a forward axis points at a target: NPCs that
// watch the player, a character that looks where you aim. Weighted and angle-
// clamped so it eases in and never snaps the neck past a believable limit.
//
// Assign `chain` root-to-tip and either `target` (world point) or `targetObject`.
// Each bone takes a share of the turn (`boneWeights`, a ramp by default) and the
// tip finishes it; `clampAngle` keeps the whole look within a cone of where the
// animation points the chain root, so a target behind the character is watched
// from the corner of the eye instead of the head turning round. Optional `eyes`
// aim straight at the target ahead of the head (no easing), within eyeMaxAngle.
//
// Smoothing lives on the component (a world look direction per bone), not in the
// bone. An animated bone is a fresh pose every frame, so easing the bone's own
// rotation would shrink with the frame time and never arrive. The direction
// eases with 1-exp(-smoothing*dt); each frame the animated pose turns toward
// that direction and `weight` blends the turn.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Math/IKFade.hpp"
#include "okay/Math/IKMath.hpp"
#include "okay/Components/IKBones.hpp"
#include "okay/Components/IKRestore.hpp"
#include <cmath>
#include <vector>
#include <string>

namespace okay {

class LookAtIK : public Component {
public:
    int ExecutionOrder() const override { return 110; }   // last: looks from the final body pose
    std::vector<Transform*> chain;     ///< bones root..tip (e.g. spine, neck, head)
    Transform* targetObject = nullptr; ///< look at this object (overrides `target`)
    std::vector<std::string> chainNames; ///< editor/serialized: resolve `chain` by object names
    std::string targetName;            ///< editor/serialized: resolve `targetObject` by name
    Vec3  target{0, 0, 0};             ///< world point to look at (if no targetObject)
    Vec3  forwardAxis = Vec3::Forward; ///< the bones' local "forward"
    float weight   = 1.0f;             ///< 0 = off, 1 = full look
    bool  ikActive = true;             ///< switch IK on/off; the change eases over fadeTime
    float fadeTime = 0.0f;             ///< seconds to blend IK in/out (0 = instant)
    float maxAngle = 80.0f;            ///< max turn per bone (degrees), keeps it natural
    /// Ease speed (per second) of the look direction toward the target. The bone
    /// still reaches the target at any frame rate; only the swing takes time.
    /// 0 = instant. ~10 reads like a natural head turn.
    float smoothing = 10.0f;
    /// How much of its own turn each bone takes, root..tip (0..1). The tip always
    /// finishes the look; bones without an entry ramp up to it, so an empty list
    /// spreads a spine/neck/head turn a third, two thirds, the rest.
    std::vector<float> boneWeights;
    /// Furthest the look swings from where the animation points the chain root
    /// (degrees; 0 = no limit). A target behind the character is then watched
    /// from the corner of the eye instead of the head turning round.
    float clampAngle = 120.0f;
    /// Eye bones (optional). They aim straight at the target with no easing —
    /// the eyes lead, the head follows — within eyeMaxAngle of where the
    /// animation points them.
    std::vector<Transform*> eyes;
    std::vector<std::string> eyeNames; ///< editor/serialized: resolve `eyes` by object names
    float eyeMaxAngle = 35.0f;
    float eyeWeight = 1.0f;
    /// Stop looking at a target further than this (metres; 0 = any distance).
    /// The look fades out over the last fadeDistance of it, so nothing pops.
    float maxDistance = 0.0f;
    float fadeDistance = 1.0f;

    /// The bone-local axis that points where the character faces (`facingWorld`,
    /// a unit vector) in the bone's current pose: the `forwardAxis` a rig needs.
    /// Rigs differ here — one head bone points its +Z at the character's front,
    /// another's +Z points out the back of the skull — so the editor's
    /// Auto-Detect reads it off the rest pose instead of assuming +Z.
    static Vec3 RestForwardAxis(const Transform* bone, const Vec3& facingWorld) {
        if (!bone) return Vec3::Forward;
        return SafeNormalize(bone->Rotation().Inverse() * facingWorld, Vec3::Forward);
    }

    /// The share of its turn bone `i` of `n` takes: its boneWeights entry, else a
    /// ramp up to the tip, which always finishes the look.
    static float ShareOf(const std::vector<float>& weights, std::size_t i, std::size_t n) {
        if (n == 0 || i + 1 >= n) return 1.0f;
        if (i < weights.size()) return Mathf::Clamp01(weights[i]);
        return (float)(i + 1) / (float)n;
    }

    void Start() override {
        // Names resolve inside this character first (two copies of one model
        // share bone names), then anywhere in the scene.
        Scene* s = GetScene();
        if (chain.empty() && !chainNames.empty())
            for (const std::string& n : chainNames)
                if (Transform* t = ResolveBone(s, gameObject, n)) chain.push_back(t);
        if (eyes.empty() && !eyeNames.empty())
            for (const std::string& n : eyeNames)
                if (Transform* t = ResolveBone(s, gameObject, n)) eyes.push_back(t);
        if (!targetObject) targetObject = ResolveBone(s, gameObject, targetName);
    }

    void Update(float) override { m_restore.Restore(); }   // before the animator blends (see IKRestore.hpp)

    // Solve in LateUpdate so the correction lands AFTER every animation driver
    // has posed the bones this frame (imported rigs create their per-node
    // Animators lazily, which puts them late in the Update order).
    void LateUpdate(float dt) override {
        // Last frame's writes come off first, so a still bone (and a rotation-only
        // clip) shows the animated pose before this solve. Weight 0 leaves it there.
        m_restore.Restore();
        float w = weight * m_fade.Step(ikActive, fadeTime, dt);
        const Vec3 tgt = targetObject ? targetObject->Position() : target;
        if (w > 0.0f && maxDistance > 0.0f && Finite(tgt)) w *= RangeWeight(tgt);
        if (w <= 0.0f || (chain.empty() && eyes.empty())) { ForgetAim(); return; }

        if (m_smooth.size() != chain.size()) { m_smooth.assign(chain.size(), {}); m_haveTgt = false; }

        if (!Finite(tgt)) return;
        const Vec3 tgtChar = InCharacter(tgt);
        const bool tgtJump = m_haveTgt && Jumped(tgtChar, m_tgtChar);
        const float ease = smoothing > 0.0f ? (1.0f - std::exp(-smoothing * dt)) : 1.0f;

        // The cone the look stays in: around where the animation points the
        // chain root (the body's forward for a spine..head chain).
        const Transform* rootBone = chain.empty() ? nullptr : chain.front();
        const Vec3 bodyFwd = rootBone ? SafeNormalize(rootBone->Rotation() * forwardAxis, Vec3::Forward) : Vec3::Forward;

        // Root-to-tip: each bone aims its forward at the target. Later bones see
        // the positions their parents just moved them to.
        for (std::size_t i = 0; i < chain.size(); ++i) {
            Transform* b = chain[i];
            if (!b) continue;
            Smooth& s = m_smooth[i];
            if (s.bone != b) { s = {}; s.bone = b; }

            const Vec3 raw = tgt - b->Position();
            if (!Finite(raw) || raw.SqrMagnitude() < 1e-8f) continue;
            const Quat animated = b->Rotation();
            const Vec3 fwd = SafeNormalize(animated * forwardAxis, Vec3::Forward);
            Vec3 goal = SafeNormalize(raw, Vec3::Forward);
            // A target straight behind has no nearer side: keep coming round the
            // way the look already points.
            if (clampAngle > 0.0f) goal = ClampDir(goal, bodyFwd, clampAngle, s.have ? s.dir : fwd);
            const Vec3 boneChar = InCharacter(b->Position());
            const bool snap = !s.have || smoothing <= 0.0f || tgtJump || (s.have && Jumped(boneChar, s.boneChar));
            s.dir = snap ? goal : SafeNormalize(Vec3::LerpUnclamped(s.dir, goal, ease), goal);
            s.have = true;
            s.boneChar = boneChar;

            Quat desired = (Quat::FromToRotation(fwd, s.dir) * animated).Normalized();
            if (!Finite(desired)) continue;
            const float share = ShareOf(boneWeights, i, chain.size());
            if (share < 1.0f) desired = Quat::Slerp(animated, desired, share);
            const float total = Quat::Angle(animated, desired);
            if (total > maxAngle && total > 1e-4f) desired = Quat::Slerp(animated, desired, maxAngle / total);
            m_restore.Touch(b);
            BlendWorldRotation(b, animated, desired, w);
        }
        // Eyes: straight at the target, no easing (the eyes lead, the head
        // follows), within eyeMaxAngle of where the animation points them.
        for (Transform* e : eyes) {
            if (!e) continue;
            const Vec3 raw = tgt - e->Position();
            if (!Finite(raw) || raw.SqrMagnitude() < 1e-8f) continue;
            const Quat animated = e->Rotation();
            const Vec3 fwd = SafeNormalize(animated * forwardAxis, Vec3::Forward);
            const Vec3 dir = ClampDir(SafeNormalize(raw, fwd), fwd, eyeMaxAngle, Vec3::Up);
            const Quat desired = (Quat::FromToRotation(fwd, dir) * animated).Normalized();
            if (!Finite(desired)) continue;
            m_restore.Touch(e);
            BlendWorldRotation(e, animated, desired, w * eyeWeight);
        }
        m_tgtChar = tgtChar;
        m_haveTgt = true;
        m_restore.Commit();
    }

    /// 1 inside maxDistance less the fade band, 0 beyond maxDistance, measured
    /// from the chain root (else this object).
    float RangeWeight(const Vec3& tgt) const {
        const Transform* from = (!chain.empty() && chain.front()) ? chain.front() : transform;
        if (!from) return 1.0f;
        const float d = (tgt - from->Position()).Magnitude();
        if (!std::isfinite(d)) return 0.0f;
        return Mathf::Clamp01((maxDistance - d) / Mathf::Max(fadeDistance, 1e-4f));
    }

    /// `dir` pulled back to within `maxDeg` of `ref` (both unit). A direction
    /// straight behind `ref` has no nearest side: it comes round via `side`.
    static Vec3 ClampDir(const Vec3& dir, const Vec3& ref, float maxDeg, const Vec3& side) {
        if (maxDeg >= 180.0f) return dir;
        if (maxDeg <= 0.0f) return ref;
        const float ang = std::acos(Mathf::Clamp(Vec3::Dot(dir, ref), -1.0f, 1.0f)) * Mathf::Rad2Deg;
        if (ang <= maxDeg) return dir;
        // Within ~5 degrees of straight behind there is no nearer side: come
        // round the way the look already points, else level about the up axis.
        Vec3 axis = Vec3::Dot(dir, ref) < -0.996f ? Vec3::Cross(ref, side) : Vec3::Cross(ref, dir);
        if (axis.SqrMagnitude() < 1e-8f) axis = std::fabs(Vec3::Dot(ref, Vec3::Up)) < 0.9f ? Vec3::Up : Vec3::Right;
        return SafeNormalize(Quat::AngleAxis(maxDeg, axis.Normalized()) * ref, ref);
    }
private:
    /// A goal or bone that leaps this far in one frame (character space, metres
    /// squared) is a teleport: snap the look instead of trailing across the map.
    static constexpr float kSnapDist2 = 4.0f;

    struct Smooth {
        Transform* bone = nullptr;
        Vec3 dir = Vec3::Forward;
        Vec3 boneChar = Vec3::Zero;
        bool have = false;
    };

    Vec3 InCharacter(const Vec3& world) const {
        if (!transform) return world;
        return transform->Rotation().Inverse() * (world - transform->Position());
    }
    static bool Jumped(const Vec3& now, const Vec3& prev) {
        const Vec3 d = now - prev;
        return !Finite(d) || d.SqrMagnitude() > kSnapDist2;
    }
    void ForgetAim() { m_smooth.clear(); m_haveTgt = false; }

    IKFade m_fade;
    IKRestore m_restore;
    std::vector<Smooth> m_smooth;
    Vec3 m_tgtChar = Vec3::Zero;
    bool m_haveTgt = false;
};

} // namespace okay
