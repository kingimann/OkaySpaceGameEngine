#pragma once
// ---------------------------------------------------------------------------
// FootIK — plant a biped's feet on the ground (Unity's foot IK). Each frame it
// raycasts down under each foot, and if the ground is above where the animation
// put the foot, it solves a two-bone IK (hip -> knee -> foot) so the foot sits on
// the surface and the knee bends naturally — no more feet floating over slopes or
// sinking into steps. Blendable via `weight`.
//
// Assign the leg-bone Transforms (hips/knee/foot for each leg). Bone lengths are
// learned from the rig on the first solve. Knees bend toward the body's forward.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Math/TwoBoneIK.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Components/Character.hpp"
#include <cmath>
#include <string>

namespace okay {

class FootIK : public Component {
public:
    // Left leg chain.
    Transform* leftHip = nullptr;  Transform* leftKnee = nullptr;  Transform* leftFoot = nullptr;
    // Right leg chain.
    Transform* rightHip = nullptr; Transform* rightKnee = nullptr; Transform* rightFoot = nullptr;
    // Editor/serialized: resolve the bones above by object name at Start (when unset).
    std::string leftHipName, leftKneeName, leftFootName;
    std::string rightHipName, rightKneeName, rightFootName, pelvisName;

    float weight     = 1.0f;   ///< 0 = off (pure animation), 1 = fully planted
    float footOffset = 0.05f;  ///< ankle height to keep above the ground surface
    /// Correction ease speed (per second). The foot lift fades in/out toward its
    /// target instead of snapping the frame the ground appears/disappears under a
    /// foot — that snap was the visible "pop" on stairs and ledge edges. Higher =
    /// snappier; 0 = instant (the old behavior).
    float smoothing  = 12.0f;
    bool  useRaycast = true;   ///< raycast the scene for ground; else use groundY
    float groundY    = 0.0f;   ///< fallback ground height when not raycasting
    float maxRayUp   = 0.6f;   ///< how far above the foot to start the ray
    float maxRayDown = 0.8f;   ///< how far below the foot the ground may be

    // ---- Pelvis adjustment (plant on uneven ground) ----
    Transform* pelvis = nullptr;   ///< the hips/root bone to shift
    bool  adjustPelvis = false;    ///< lower the pelvis so a foot on lower ground reaches
    bool  plantDown    = false;    ///< also press feet DOWN onto lower ground (not just lift)
    float maxPelvisShift = 0.5f;   ///< clamp on how far the pelvis can move

    // ---- Knee limits + foot tilt ----
    float minKneeBend = 0.0f;      ///< min interior knee angle (deg); 0 = no limit
    float maxKneeBend = 178.0f;    ///< max knee angle (deg) — keeps knees from locking straight
    bool  alignToGround = false;   ///< tilt the planted foot to match the ground slope
    Vec3  footUpAxis = Vec3::Up;   ///< the foot bone's local "up" (sole normal)

    void Start() override {
        Scene* s = GetScene();
        if (!s) return;
        auto R = [&](Transform*& t, const std::string& n) {
            if (!t && !n.empty()) if (GameObject* g = s->Find(n)) t = g->transform;
        };
        R(leftHip, leftHipName);   R(leftKnee, leftKneeName);   R(leftFoot, leftFootName);
        R(rightHip, rightHipName); R(rightKnee, rightKneeName); R(rightFoot, rightFootName);
        R(pelvis, pelvisName);
        // Self-heal: if the leg chain is still unwired, pull the bones straight from a
        // Character on this object. The editor's "Humanoid" setup wires FootIK by raw
        // pointer, which the serializer DOESN'T persist (only the *Name fields are) — so
        // after a save/reload the pointers were null and foot IK silently did nothing.
        // Rebuilding from the Character every Start makes it survive save/reload and also
        // "just work" when you drop FootIK onto a Character by hand.
        if (!leftFoot || !rightFoot) WireFromCharacter();
    }

    // Wire the six leg bones (+ pelvis) from a Character's part rig by bone index.
    void WireFromCharacter() {
        if (!gameObject) return;
        Character* pc = gameObject->GetComponent<Character>();
        if (!pc) return;
        if (!pc->PartsBuilt()) { pc->separateParts = true; pc->BuildParts(); }
        auto T = [&](int b) -> Transform* { GameObject* g = pc->Part(b); return g ? g->transform : nullptr; };
        // Bone indices (Character.cpp): hips=0; L thigh/shin/foot=9/10/11; R=12/13/14.
        if (!leftHip)  leftHip  = T(9);  if (!leftKnee)  leftKnee  = T(10); if (!leftFoot)  leftFoot  = T(11);
        if (!rightHip) rightHip = T(12); if (!rightKnee) rightKnee = T(13); if (!rightFoot) rightFoot = T(14);
        if (!pelvis)   pelvis   = T(0);
    }

    // Solve in LateUpdate so it corrects the pose AFTER every animation driver has
    // written it. Imported rigs create their per-node Animators lazily (at Play
    // start), which lands them after FootIK in the Update order — solving in
    // Update let those Animators clobber the correction the same frame.
    void LateUpdate(float dt) override {
        if (weight <= 0.0f) return;
        if (!m_init) { Learn(); m_init = true; }
        Vec3 pole = (transform ? transform->Rotation() : Quat::Identity) * Vec3::Forward;
        Scene* s = GetScene();
        // Frame-rate-independent ease factor: each frame applies this fraction of
        // the REMAINING correction (exponential approach). The bone offsets persist
        // in the rig between frames, so the correction converges over a few frames
        // and then holds — smooth engage with no pop, no accumulation.
        float k = smoothing > 0.0f ? (1.0f - std::exp(-smoothing * dt)) : 1.0f;
        if (adjustPelvis && pelvis) AdjustPelvis(s, k);
        SolveLeg(leftHip,  leftKnee,  leftFoot,  m_lUp, m_lLo, pole, s, k);
        SolveLeg(rightHip, rightKnee, rightFoot, m_rUp, m_rLo, pole, s, k);
    }

private:
    void Learn() {
        if (leftHip && leftKnee && leftFoot) {
            m_lUp = (leftKnee->Position() - leftHip->Position()).Magnitude();
            m_lLo = (leftFoot->Position() - leftKnee->Position()).Magnitude();
        }
        if (rightHip && rightKnee && rightFoot) {
            m_rUp = (rightKnee->Position() - rightHip->Position()).Magnitude();
            m_rLo = (rightFoot->Position() - rightKnee->Position()).Magnitude();
        }
    }

    float GroundAt(Scene* s, const Vec3& foot, Vec3* outNormal = nullptr) {
        if (outNormal) *outNormal = Vec3::Up;
        if (useRaycast && s) {
            Vec3 origin{foot.x, foot.y + maxRayUp, foot.z};
            RaycastHit3D hit = s->physics3D().Raycast(*s, origin, Vec3{0, -1, 0},
                                                      maxRayUp + maxRayDown, gameObject);
            if (hit.hit) { if (outNormal) *outNormal = hit.normal; return hit.point.y; }
            return foot.y - 1e6f;       // nothing under the foot -> don't lift
        }
        return groundY;
    }

    // Lower the pelvis so the foot wanting to go lowest can still reach the ground;
    // the other foot drops with the body and the per-leg IK re-plants it. Applies
    // `ease` of the remaining shift per frame so stepping onto a ledge sinks the
    // body over a few frames instead of teleporting it (the shift persists in the
    // rig, so the fractional steps converge on the full correction and hold).
    void AdjustPelvis(Scene* s, float ease) {
        float off = 0.0f; bool any = false;
        auto consider = [&](Transform* foot) {
            if (!foot) return;
            Vec3 p = foot->Position();
            float g = GroundAt(s, p);
            if (g < p.y - maxRayDown) return;          // no ground in range
            float d = (g + footOffset) - p.y;          // + lift, - drop
            off = any ? Mathf::Min(off, d) : d; any = true;
        };
        consider(leftFoot); consider(rightFoot);
        if (!any) return;
        off = Mathf::Clamp(off, -maxPelvisShift, maxPelvisShift) * weight * ease;
        if (Mathf::Abs(off) < 1e-5f) return;
        pelvis->SetPosition(pelvis->Position() + Vec3{0, off, 0});
    }

    void SolveLeg(Transform* hip, Transform* knee, Transform* foot,
                  float upLen, float loLen, const Vec3& pole, Scene* s,
                  float ease) {
        if (!hip || !knee || !foot || upLen <= 0.0f || loLen <= 0.0f) return;
        Vec3 animFoot = foot->Position();
        Vec3 normal;
        float g = GroundAt(s, animFoot, &normal);
        float targetY = g + footOffset;
        // Lift the foot UP to meet ground (and, when plantDown/pelvis adjust is on,
        // also press it DOWN onto lower ground). Ignore ground out of reach below.
        bool down = plantDown || adjustPelvis;
        bool apply = !((!down && targetY <= animFoot.y) || g < animFoot.y - maxRayDown);
        // Smoothing: place the foot `ease` of the way toward the plant target. The
        // offset persists in the rig, so repeating this each frame converges on the
        // planted pose — a smooth engage instead of the old one-frame pop at step
        // edges — and holds there (the remaining correction shrinks to zero).
        float remaining = apply ? (targetY - animFoot.y) * ease : 0.0f;
        if (Mathf::Abs(remaining) < 1e-4f) return;

        Vec3 target{animFoot.x, animFoot.y + remaining, animFoot.z};
        Vec3 mid, end;
        SolveTwoBoneIK(hip->Position(), upLen, loLen, target, pole, mid, end,
                       minKneeBend, maxKneeBend);
        knee->SetPosition(Vec3::Lerp(knee->Position(), mid, weight));
        foot->SetPosition(Vec3::Lerp(foot->Position(), end, weight));

        // Tilt the foot so its sole follows the ground slope (only while a
        // correction is actually in play — never tilt a mid-air swing foot).
        if (alignToGround && apply && normal.SqrMagnitude() > 1e-6f) {
            Vec3 up = (foot->Rotation() * footUpAxis).Normalized();
            Quat tilt = Quat::FromToRotation(up, normal.Normalized());
            Quat desired = (tilt * foot->Rotation()).Normalized();
            foot->SetRotation(Quat::Slerp(foot->Rotation(), desired, weight));
        }
    }

    bool  m_init = false;
    float m_lUp = 0, m_lLo = 0, m_rUp = 0, m_rLo = 0;
};

} // namespace okay
