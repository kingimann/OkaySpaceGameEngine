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
// read from the pose every frame (a rescale or a height change just works). In
// a parented leg (knee under hip, foot under knee) the bones TURN: lengths stay
// exact at any weight and a skinned leg bends with them. Unparented joints are
// moved to the solved positions instead. Knees keep the animation's bend and
// ease toward the toes as the leg straightens (IKPole).
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Math/TwoBoneIK.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Math/IKMath.hpp"
#include "okay/Math/JointLimit.hpp"
#include "okay/Components/IKRestore.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/IKBones.hpp"
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
    /// Collider layers the feet stand on. The character's own colliders and
    /// triggers never count as ground.
    std::uint32_t groundMask = Physics3D::kAllLayers;

    // ---- Pelvis adjustment (plant on uneven ground) ----
    Transform* pelvis = nullptr;   ///< the hips/root bone to shift
    bool  adjustPelvis = false;    ///< lower the pelvis so a foot on lower ground reaches
    bool  plantDown    = false;    ///< also press feet DOWN onto lower ground (not just lift)
    float maxPelvisShift = 0.5f;   ///< clamp on how far the pelvis can move

    // ---- Knee limits + foot tilt ----
    float minKneeBend = 0.0f;      ///< min interior knee angle (deg); 0 = no limit
    float maxKneeBend = 178.0f;    ///< max knee angle (deg) — keeps knees from locking straight
    bool  alignToGround = false;   ///< tilt the planted foot to match the ground slope
    /// Each foot bone's local "up" (sole normal): footUpAxis for the left foot,
    /// footUpAxisR for the right. Left at Up (the default), a foot's axis is read
    /// from its own rest pose when the bones resolve, so both soles of a mirrored
    /// rig lie flat without a shared constant.
    Vec3  footUpAxis = Vec3::Up;
    Vec3  footUpAxisR = Vec3::Up;
    /// Joints that aren't parented in a chain: also rotate the thigh/shin bones to
    /// follow the planted knee/foot. A parented leg always turns (legacy there).
    bool  orient = false;
    /// Set by the loader for files saved before orient's migration: Start() turns
    /// orient on when this drives a Character's part rig. Runtime only.
    bool  orientMigrate = false;
    /// Optional limits around each bone's rest pose (JointLimit.hpp). Default None.
    /// They only keep the IK from pushing a bone out of range: an animated pose
    /// that's already outside it is left alone. A knee Hinge only stops twist
    /// (min/maxKneeBend decide the bend).
    IKJointLimit hipLimit, kneeLimit;

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
        m_restore.Clear();   // bones may be re-resolved below: forget the old ones
        m_pole[0].Reset(); m_pole[1].Reset();
        Scene* s = GetScene();
        if (!s) return;
        // Bones resolve inside this character first (two copies of one model share
        // bone names), then anywhere in the scene.
        auto R = [&](Transform*& t, const std::string& n) { if (!t) t = ResolveBone(s, gameObject, n); };
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
        CaptureSoleAxes();
        if (orientMigrate) {
            // Before orient's migration, a Character's part rig (joints that aren't
            // a parented chain) loaded with it off and its leg parts came apart.
            if (gameObject && gameObject->GetComponent<Character>()) orient = true;
            orientMigrate = false;
        }
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
            m_restore.Clear();
        }
        return found;
    }

    // Wire the six leg bones (+ pelvis) from a Character's part rig by bone index.
    void WireFromCharacter() {
        if (!gameObject) return;
        Character* pc = gameObject->GetComponent<Character>();
        if (!pc) return;
        if (!pc->PartsBuilt()) { pc->separateParts = true; pc->BuildParts(); }
        m_restore.Clear();
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
        m_restore.Restore();   // last frame's corrections off before anything reads the pose
        if (EffWeight() <= 0.0f) return;
        const Vec3 pole = (transform ? transform->Rotation() : Quat::Identity) * Vec3::Forward;
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
        SolveLeg(0, leftHip,  leftKnee,  leftFoot,  pole, s, k, dt, floorY, air);
        SolveLeg(1, rightHip, rightKnee, rightFoot, pole, s, k, dt, floorY, air);
        m_restore.Commit();
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
    IKRestore m_restore;          ///< pelvis + each leg's bones (local space; see IKRestore.hpp)
    IKPole m_pole[2];             ///< each knee's bend direction, eased as the leg straightens
    IKJointFrame m_hipFrame[2], m_kneeFrame[2];
    Vec3 m_upAuto[2] = {Vec3::Up, Vec3::Up};          ///< each sole's axis off its rest pose
    const Transform* m_upFor[2] = {nullptr, nullptr}; ///< the foot it was read from

    /// Each sole's local up, from the foot's rest rotation (when it's resolved).
    void CaptureSoleAxes() {
        Transform* feet[2] = {leftFoot, rightFoot};
        for (int i = 0; i < 2; ++i) {
            if (!feet[i] || m_upFor[i] == feet[i]) continue;
            const Vec3 a = feet[i]->Rotation().Inverse() * Vec3::Up;
            m_upAuto[i] = Finite(a) ? SafeNormalize(a, Vec3::Up) : Vec3::Up;
            m_upFor[i] = feet[i];
        }
    }
    Vec3 SoleAxis(int leg) const {
        const Vec3& set = leg == 0 ? footUpAxis : footUpAxisR;
        const bool isDefault = set.x == 0.0f && set.y == 1.0f && set.z == 0.0f;
        return isDefault ? m_upAuto[leg] : set;
    }

    float GroundAt(Scene* s, const Vec3& foot, Vec3* outNormal = nullptr) {
        if (outNormal) *outNormal = Vec3::Up;
        if (useRaycast && s) {
            Vec3 origin{foot.x, foot.y + maxRayUp, foot.z};
            const Transform* root = CharacterRootOf(transform);
            Physics3D::IgnoreHierarchy own(root ? root->gameObject : gameObject);
            RaycastHit3D hit = s->physics3D().Raycast(*s, origin, Vec3{0, -1, 0},
                                                      maxRayUp + maxRayDown, gameObject, groundMask, false);
            if (hit.hit && Finite(hit.point)) {
                if (outNormal) *outNormal = Finite(hit.normal) ? hit.normal : Vec3::Up;
                return hit.point.y;
            }
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
        if (!std::isfinite(m_pelvisOff) || !Finite(animP)) { m_pelvisOff = 0.0f; return; }
        m_restore.Touch(pelvis);
        pelvis->SetPosition(animP + Vec3{0, m_pelvisOff, 0});
    }

    // Where a straight knee points: toward the toes, the foot's child joint
    // farthest from it across the ground (a heel joint sits closer than the toe
    // tip on every rig, whichever way the model faces), else `fallback`. A fixed
    // axis alone folded imported characters' knees backwards: they face -Z, the
    // opposite of this object's forward.
    static Vec3 ToeDir(Transform* foot, const Vec3& fallback) {
        const Vec3 f = foot->Position();
        Vec3 best; float bestD = 1e-8f;
        for (Transform* c : foot->Children()) {
            if (!c) continue;
            Vec3 toe = c->Position() - f;
            toe.y = 0.0f;
            const float d = toe.SqrMagnitude();
            if (d > bestD && Finite(toe)) { best = toe; bestD = d; }
        }
        return bestD > 1e-8f ? best : fallback;
    }

    void SolveLeg(int leg, Transform* hip, Transform* knee, Transform* foot, const Vec3& pole, Scene* s,
                  float ease, float dt, float floorY, bool air) {
        if (!hip || !knee || !foot) return;
        LegState& L = m_leg[leg];
        const Vec3 hipP = hip->Position(), oldMid = knee->Position(), animFoot = foot->Position();
        // Lengths off this frame's pose (as the animation placed it, see Restore).
        const float upLen = (oldMid - hipP).Magnitude(), loLen = (animFoot - oldMid).Magnitude();
        if (!Finite(hipP) || !Finite(oldMid) || !Finite(animFoot) || upLen < 1e-5f || loLen < 1e-5f) return;
        Vec3 normal;
        float g = GroundAt(s, animFoot, &normal);
        if (!std::isfinite(g)) return;
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
        if ((target - animFoot).SqrMagnitude() < 1e-8f || !Finite(target)) return;   // nothing to correct

        // The knee keeps the animation's bend, easing toward the toes as the leg
        // straightens (no flip at a fixed threshold).
        const Vec3 bendDir = m_pole[leg].Direction(hipP, oldMid, animFoot, Quat::Identity, IKPoleMode::Auto,
                                                   ToeDir(foot, pole), nullptr);
        Vec3 mid, end;
        SolveTwoBoneIK(hipP, upLen, loLen, target, bendDir, mid, end, minKneeBend, maxKneeBend);
        if (!Finite(mid) || !Finite(end)) return;
        const float w = EffWeight();

        m_restore.Touch(hip); m_restore.Touch(knee); m_restore.Touch(foot);
        if (IsUnder(knee, hip) && IsUnder(foot, knee)) {
            // A parented leg TURNS: the thigh toward the solved knee, then the shin
            // (as turned by its parent) toward the solved foot. Rotations blend by
            // the weight, so lengths stay exact; the foot keeps its animated angle.
            const Quat rHip = hip->Rotation(), rKnee = knee->Rotation(), rFoot = foot->Rotation();
            const Quat lHip = hip->localRotation, lKnee = knee->localRotation;
            CaptureFrames(leg, hip, knee, foot, hipP, mid, end);
            hip->SetRotation(AimBone(hip, oldMid, mid));
            ClampIK(hip, lHip, m_hipFrame[leg], hipLimit, false);
            knee->SetRotation(AimBone(knee, foot->Position(), end));
            ClampIK(knee, lKnee, m_kneeFrame[leg], kneeLimit, kneeLimit.type == IKJointLimit::Type::Hinge);
            const Quat kneeSolved = knee->Rotation();
            BlendWorldRotation(hip, rHip, hip->Rotation(), w);
            BlendWorldRotation(knee, rKnee, kneeSolved, w);
            foot->SetRotation(rFoot);
            if (!Finite(hip->localRotation) || !Finite(knee->localRotation)) {   // never leave a broken leg
                hip->SetRotation(rHip); knee->SetRotation(rKnee); foot->SetRotation(rFoot);
                return;
            }
        } else {
            // Joints that aren't a parented chain are moved to the solve.
            const Vec3 newMid = Vec3::Lerp(oldMid, mid, w), newEnd = Vec3::Lerp(animFoot, end, w);
            if (orient) {
                // Rotate the thigh and shin parts so they follow the moved joints
                // (root to tip, re-reading the shin after the thigh turns).
                Vec3 oldUp = oldMid - hipP, newUp = newMid - hipP;
                if (oldUp.SqrMagnitude() > 1e-8f && newUp.SqrMagnitude() > 1e-8f)
                    hip->SetRotation(Quat::FromToRotation(oldUp, newUp) * hip->Rotation());
                Vec3 curMid = knee->Position(), curEnd = foot->Position();
                Vec3 oldLo = curEnd - curMid, newLo = newEnd - newMid;
                if (oldLo.SqrMagnitude() > 1e-8f && newLo.SqrMagnitude() > 1e-8f)
                    knee->SetRotation(Quat::FromToRotation(oldLo, newLo) * knee->Rotation());
            }
            knee->SetPosition(newMid);
            foot->SetPosition(newEnd);
        }

        // Tilt the foot so its sole follows the ground slope (only while a
        // correction is actually in play — never tilt a mid-air swing foot).
        if (alignToGround && std::fabs(wantLift) > 1e-3f && normal.SqrMagnitude() > 1e-6f) {
            const Vec3 up = SafeNormalize(foot->Rotation() * SoleAxis(leg), Vec3::Up);
            const Quat tilt = Quat::FromToRotation(up, normal.Normalized());
            const Quat desired = (tilt * foot->Rotation()).Normalized();
            if (Finite(desired)) foot->SetRotation(Quat::Slerp(foot->Rotation(), desired, w));
        }
    }

    /// Rest frames for this leg's limits, (re)captured when the bones change.
    void CaptureFrames(int leg, Transform* hip, Transform* knee, Transform* foot,
                       const Vec3& hipP, const Vec3& mid, const Vec3& end) {
        if (!hipLimit.Active() && !kneeLimit.Active()) return;
        if (!m_hipFrame[leg].CapturedFor(hip)) m_hipFrame[leg].Capture(hip, knee, foot);
        if (!m_kneeFrame[leg].CapturedFor(knee)) m_kneeFrame[leg].Capture(knee, foot, nullptr);
        if (!m_hipFrame[leg].hasHinge)  m_hipFrame[leg].SetHingeFromBend(hipP, mid, end);
        if (!m_kneeFrame[leg].hasHinge) m_kneeFrame[leg].SetHingeFromBend(hipP, mid, end);
    }

    /// Clamp what the IK wrote to `lim` — unless the animated pose was already
    /// outside it: a limit stops the IK pushing a bone out of range, it never
    /// fights the animation's own pose (a deep crouch, a kick).
    static void ClampIK(Transform* b, const Quat& animLocal, const IKJointFrame& fr, const IKJointLimit& lim, bool bendFree) {
        if (!lim.Active()) return;
        if (Quat::Angle(ApplyLimit(animLocal, fr, lim, bendFree), animLocal) > 0.5f) return;
        b->localRotation = ApplyLimit(b->localRotation, fr, lim, bendFree);
    }
};

} // namespace okay
