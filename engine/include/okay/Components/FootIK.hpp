#pragma once
// ---------------------------------------------------------------------------
// FootIK — plant a biped's feet on the ground. Each frame it raycasts down
// under each foot and moves the foot by how far that ground sits above (or,
// with plantDown, below) the floor the animation was made for: this object's
// origin. A two-bone IK (hip -> knee -> foot) bends the knee to match — no more
// feet floating over slopes or sinking into steps, and on flat ground the
// animation's own steps are left alone. Blendable via `weight`.
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
#include "okay/Components/ModelAnimator.hpp"
#include <cmath>
#include <string>

namespace okay {

class FootIK : public Component {
public:
    int ExecutionOrder() const override { return 100; }   // after animation
    // Left leg chain.
    Transform* leftHip = nullptr;  Transform* leftKnee = nullptr;  Transform* leftFoot = nullptr;
    // Right leg chain.
    Transform* rightHip = nullptr; Transform* rightKnee = nullptr; Transform* rightFoot = nullptr;
    // Editor/serialized: resolve the bones above by object name at Start (when unset).
    std::string leftHipName, leftKneeName, leftFootName;
    std::string rightHipName, rightKneeName, rightFootName, pelvisName;

    float weight     = 1.0f;   ///< 0 = off (pure animation), 1 = fully planted
    /// Multiplies `weight`; set by the Animator state machine's per-state IK
    /// weight (feet free while jumping or rolling). Runtime only.
    float stateWeight = 1.0f;
    float EffWeight() const { return weight * stateWeight; }
    float footOffset = 0.05f;  ///< lowest the ankle may get above the ground surface
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
    bool  orient = false;          ///< also rotate the thigh/shin bones to follow the planted
                                   ///< knee/foot, so leg meshes bend with the joints

    // ---- Foot locking (no sliding) ----
    /// While the animation has a foot planted (barely moving, on the ground), pin it
    /// where it landed in the world until the animation lifts it — feet stop
    /// skating when the clip's stride doesn't match how fast the body moves.
    bool  footLock = false;
    float lockSpeed = 0.35f;       ///< foot slower than this (units/s) counts as planted
    float lockHeight = 0.08f;      ///< ... and within this height of the ground
    float lockRelease = 0.3f;      ///< let go once the animated foot is this far from the pin
    /// Whether each foot is pinned right now (0 left, 1 right) — for debugging / UI.
    bool FootLocked(int leg) const { return leg == 0 ? m_leg[0].locked : m_leg[1].locked; }

    void Start() override {
        Scene* s = GetScene();
        if (!s) return;
        // Bones resolve inside this character first (two copies of one model share
        // bone names), then anywhere in the scene.
        Transform* top = transform;
        while (top && top->Parent()) top = top->Parent();
        auto inTree = [&](const std::string& n) -> Transform* {
            if (!top) return nullptr;
            std::vector<Transform*> stack{top};
            while (!stack.empty()) {
                Transform* t = stack.back(); stack.pop_back();
                if (t->gameObject && t->gameObject->name == n) return t;
                for (Transform* c : t->Children()) stack.push_back(c);
            }
            return nullptr;
        };
        auto R = [&](Transform*& t, const std::string& n) {
            if (t || n.empty()) return;
            t = inTree(n);
            if (!t) if (GameObject* g = s->Find(n)) t = g->transform;
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

    /// Detect humanoid leg bones by NAME among `root`'s descendants (Mixamo
    /// "LeftUpLeg/LeftLeg/LeftFoot", thigh/shin/calf variants, l_/_l suffixes)
    /// and fill the *Name fields. Returns how many of the six leg bones matched
    /// (Start() resolves the names to transforms). Mirrors the editor's
    /// Auto-Detect so the character-swap can wire foot IK with no clicks.
    int DetectHumanoidBones(Scene& s, GameObject* root) {
        if (!root) return 0;
        auto lower = [](std::string v) { for (auto& c : v) c = (char)std::tolower((unsigned char)c); return v; };
        std::string hipL, kneeL, footL, hipR, kneeR, footR, pel;
        for (const auto& up : s.Objects()) {
            GameObject* g = up.get();
            if (!g || !g->IsSelfOrDescendantOf(root)) continue;
            std::string nm = lower(g->name);
            auto has = [&](const char* k) { return nm.find(k) != std::string::npos; };
            auto endsW = [&](const char* k) {
                std::size_t kl = std::char_traits<char>::length(k);
                return nm.size() >= kl && nm.compare(nm.size() - kl, kl, k) == 0;
            };
            bool isLeft  = has("left")  || nm.rfind("l_", 0) == 0 || endsW("_l") || endsW(".l");
            bool isRight = has("right") || nm.rfind("r_", 0) == 0 || endsW("_r") || endsW(".r");
            if (!isLeft && !isRight) {
                if (pel.empty() && (has("hips") || has("pelvis"))) pel = g->name;
                continue;
            }
            int part = -1;
            if (has("foot") || has("ankle")) part = 2;
            else if (has("upleg") || has("upperleg") || has("thigh") || has("hip")) part = 0;
            else if (has("knee") || has("calf") || has("shin") || has("lowerleg") || has("leg")) part = 1;
            if (part < 0) continue;
            std::string& slot = isLeft ? (part == 0 ? hipL : part == 1 ? kneeL : footL)
                                       : (part == 0 ? hipR : part == 1 ? kneeR : footR);
            if (slot.empty()) slot = g->name;
        }
        int found = (int)(!hipL.empty()) + (int)(!kneeL.empty()) + (int)(!footL.empty()) +
                    (int)(!hipR.empty()) + (int)(!kneeR.empty()) + (int)(!footR.empty());
        if (found > 0) {
            leftHipName = hipL;   leftKneeName = kneeL;   leftFootName = footL;
            rightHipName = hipR;  rightKneeName = kneeR;  rightFootName = footR;
            if (!pel.empty()) pelvisName = pel;
            leftHip = leftKnee = leftFoot = nullptr;    // resolve the names on Start
            rightHip = rightKnee = rightFoot = nullptr;
            pelvis = nullptr;
        }
        return found;
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
        RestorePose();
        if (EffWeight() <= 0.0f) return;
        if (!m_init) { Learn(); m_init = true; }
        Vec3 pole = (transform ? transform->Rotation() : Quat::Identity) * Vec3::Forward;
        Scene* s = GetScene();
        // Corrections are smoothed as persistent offsets (frame-rate independent
        // exponential ease) and re-applied on top of the animated pose each frame
        // (RestorePose took last frame's off again wherever nothing rewrote it).
        float k = smoothing > 0.0f ? (1.0f - std::exp(-smoothing * dt)) : 1.0f;
        // The animation lays the feet out over the body's own floor (this object's
        // origin). Each foot is moved by how far the real ground under it sits above
        // or below that floor, so a swinging foot keeps its lift and flat ground
        // changes nothing. In the air there's no ground to plant on.
        const float floorY = transform ? transform->Position().y : 0.0f;
        const Character* body = gameObject ? gameObject->GetComponent<Character>() : nullptr;
        // Airborne: the Character's jump / fall anim, or — on an imported model with
        // no Character — the ModelAnimator's own air state.
        const ModelAnimator* anim = (!body && gameObject) ? gameObject->GetComponent<ModelAnimator>() : nullptr;
        const bool air = body ? (body->anim == 5 || body->anim == 18) : (anim && anim->InAir());
        if (adjustPelvis && pelvis) AdjustPelvis(s, k, floorY, air);
        SolveLeg(m_leg[0], leftHip,  leftKnee,  leftFoot,  m_lUp, m_lLo, pole, s, k, dt, floorY, air);
        SolveLeg(m_leg[1], rightHip, rightKnee, rightFoot, m_rUp, m_rLo, pole, s, k, dt, floorY, air);
        for (int i = 0; i < m_touched; ++i) {
            m_saved[i].pos = m_saved[i].t->localPosition;
            m_saved[i].rot = m_saved[i].t->localRotation;
        }
    }

private:
    struct LegState {
        float lift = 0.0f;       ///< smoothed vertical correction
        bool  locked = false;
        Vec3  lockPos;
        float lockW = 0.0f;      ///< eased lock weight
        Vec3  prevAnim; bool havePrev = false;
    };
    LegState m_leg[2];
    float m_pelvisOff = 0.0f;

    // Bones moved this frame (pelvis + 3 per leg): the animated local pose before
    // the move, and what was written. Kept in LOCAL space so moving the body
    // doesn't hide that a bone still holds last frame's correction.
    struct Saved { Transform* t; Vec3 prePos, pos; Quat preRot, rot; };
    Saved m_saved[7];
    int   m_touched = 0;

    // Note a bone's animated pose before this frame first moves it.
    void Touch(Transform* t) {
        for (int i = 0; i < m_touched; ++i) if (m_saved[i].t == t) return;
        if (m_touched < 7) m_saved[m_touched++] = {t, t->localPosition, t->localPosition,
                                                   t->localRotation, t->localRotation};
    }
    // Take last frame's corrections back off wherever the animation didn't
    // rewrite the bone (a rotation-only retarget never rewrites positions; a
    // static rig rewrites nothing), so they never pile up.
    void RestorePose() {
        for (int i = 0; i < m_touched; ++i) {
            Saved& b = m_saved[i];
            const Vec3& p = b.t->localPosition; const Quat& q = b.t->localRotation;
            if (p.x == b.pos.x && p.y == b.pos.y && p.z == b.pos.z) b.t->localPosition = b.prePos;
            if (q.x == b.rot.x && q.y == b.rot.y && q.z == b.rot.z && q.w == b.rot.w) b.t->localRotation = b.preRot;
        }
        m_touched = 0;
    }

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
    // the other foot drops with the body and the per-leg IK re-plants it. The
    // shift eases in over a few frames instead of teleporting the body.
    // How far a foot the animation put at `animY` should move for ground at `g`:
    // keep its height over the body's floor, and never sink below footOffset.
    float Correction(float g, float animY, float floorY) const {
        return Mathf::Max(g - floorY, (g + footOffset) - animY);
    }

    void AdjustPelvis(Scene* s, float ease, float floorY, bool air) {
        Vec3 animP = pelvis->Position();
        float off = 0.0f; bool any = false;
        auto consider = [&](Transform* foot) {
            if (!foot || air) return;
            Vec3 p = foot->Position();
            float g = GroundAt(s, p);
            if (g < p.y - maxRayDown) return;          // no ground in range
            float d = Correction(g, p.y, floorY);      // + lift, - drop
            off = any ? Mathf::Min(off, d) : d; any = true;
        };
        consider(leftFoot); consider(rightFoot);
        float want = any ? Mathf::Clamp(Mathf::Min(off, 0.0f), -maxPelvisShift, maxPelvisShift) * EffWeight() : 0.0f;
        m_pelvisOff += (want - m_pelvisOff) * ease;
        Touch(pelvis);
        pelvis->SetPosition(animP + Vec3{0, m_pelvisOff, 0});
    }

    void SolveLeg(LegState& L, Transform* hip, Transform* knee, Transform* foot,
                  float upLen, float loLen, const Vec3& pole, Scene* s,
                  float ease, float dt, float floorY, bool air) {
        if (!hip || !knee || !foot || upLen <= 0.0f || loLen <= 0.0f) return;
        Vec3 animFoot = foot->Position();   // as the animation placed it (see RestorePose)
        Vec3 normal;
        float g = GroundAt(s, animFoot, &normal);
        float targetY = g + footOffset;
        // Lift the foot UP to meet ground (and, when plantDown/pelvis adjust is on,
        // also press it DOWN onto lower ground). Ignore ground out of reach below.
        bool down = plantDown || adjustPelvis;
        float want = Correction(g, animFoot.y, floorY);
        bool apply = !air && g >= animFoot.y - maxRayDown && (down || want > 0.0f);
        float wantLift = apply ? want : 0.0f;
        L.lift += (wantLift - L.lift) * ease;

        // Foot locking: pin a planted foot where it landed.
        if (footLock && dt > 0.0f) {
            float spd = 0.0f;
            if (L.havePrev) { Vec3 d = animFoot - L.prevAnim; d.y = 0.0f; spd = d.Magnitude() / dt; }
            L.prevAnim = animFoot; L.havePrev = true;
            bool onGround = g > animFoot.y - maxRayDown && animFoot.y - targetY < lockHeight;
            if (!L.locked && onGround && spd < lockSpeed) {
                L.locked = true;
                L.lockPos = Vec3{animFoot.x, targetY, animFoot.z};
            } else if (L.locked) {
                Vec3 d = animFoot - L.lockPos; d.y = 0.0f;
                if (!onGround || spd > lockSpeed * 2.5f || d.Magnitude() > lockRelease) L.locked = false;
            }
            L.lockW += ((L.locked ? 1.0f : 0.0f) - L.lockW) * (1.0f - std::exp(-15.0f * dt));
        } else { L.locked = false; L.lockW = 0.0f; }

        Vec3 target{animFoot.x, animFoot.y + L.lift, animFoot.z};
        if (L.lockW > 1e-3f) {
            target.x += (L.lockPos.x - target.x) * L.lockW;
            target.z += (L.lockPos.z - target.z) * L.lockW;
        }
        if ((target - animFoot).SqrMagnitude() < 1e-8f) return;   // nothing to correct

        Touch(hip); Touch(knee); Touch(foot);

        Vec3 oldMid = knee->Position(), oldEnd = foot->Position();
        Vec3 mid, end;
        SolveTwoBoneIK(hip->Position(), upLen, loLen, target, pole, mid, end,
                       minKneeBend, maxKneeBend);
        Vec3 newMid = Vec3::Lerp(oldMid, mid, EffWeight());
        Vec3 newEnd = Vec3::Lerp(oldEnd, end, EffWeight());
        // Optional: rotate the thigh (hip->knee) and shin (knee->foot) bones so a
        // leg mesh skinned to them bends with the joints, not just the joints
        // moving. Done before pinning positions; see LimbIK for the rationale.
        if (orient) {
            Vec3 oldUp = oldMid - hip->Position(), newUp = newMid - hip->Position();
            if (oldUp.SqrMagnitude() > 1e-8f && newUp.SqrMagnitude() > 1e-8f)
                hip->SetRotation(Quat::FromToRotation(oldUp, newUp) * hip->Rotation());
            Vec3 curMid = knee->Position(), curEnd = foot->Position();
            Vec3 oldLo = curEnd - curMid, newLo = newEnd - newMid;
            if (oldLo.SqrMagnitude() > 1e-8f && newLo.SqrMagnitude() > 1e-8f)
                knee->SetRotation(Quat::FromToRotation(oldLo, newLo) * knee->Rotation());
        }
        knee->SetPosition(newMid);
        foot->SetPosition(newEnd);

        // Tilt the foot so its sole follows the ground slope (only while a
        // correction is actually in play — never tilt a mid-air swing foot).
        if (alignToGround && std::fabs(wantLift) > 1e-3f && normal.SqrMagnitude() > 1e-6f) {
            Vec3 up = (foot->Rotation() * footUpAxis).Normalized();
            Quat tilt = Quat::FromToRotation(up, normal.Normalized());
            Quat desired = (tilt * foot->Rotation()).Normalized();
            foot->SetRotation(Quat::Slerp(foot->Rotation(), desired, EffWeight()));
        }
    }

    bool  m_init = false;
    float m_lUp = 0, m_lLo = 0, m_rUp = 0, m_rLo = 0;
};

} // namespace okay
