#pragma once
// ---------------------------------------------------------------------------
// FullBodyIK — one component that poses a whole humanoid toward goals, on top of
// whatever the animation did this frame:
//
//   * four limb effectors (hands + feet): reach a target with position and/or
//     rotation weight, bend toward a hint (or keep the animation's own bend),
//     with soft IK so a limb eases to full stretch instead of snapping straight;
//   * body pull: effectors out of reach drag the hips toward them (crouch to a
//     low foot target, lean the body after a far hand target), clamped;
//   * spine reach: what the hips can't cover, the chest bends toward;
//   * look-at: head (and some chest) turn toward a point, clamped per bone.
//
// Goals ease in and out (`blendSpeed`) when they're switched on or off, and can
// follow moving targets smoothly (`goalSmoothing`); elbows and knees never fold
// tighter than their limit; the chest bend is spread over the spine bones.
//
// Bones are found automatically from common rig names (Mixamo "mixamorig:LeftArm",
// Blender-style "UpperArm.L", "thigh_l", the built-in Character parts "L Hand"...)
// under this object, or assigned by name. Bones are ROTATED (so a skinned mesh
// bends with them, and every bone keeps its length); rigs whose joints aren't
// parented are positioned as well. Solve happens in LateUpdate, after every
// animation driver. Each frame starts from the animated pose: what this solver
// wrote last frame is taken back off first (IKRestore), so nothing builds up.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Math/TwoBoneIK.hpp"
#include "okay/Math/IKFade.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/IKMath.hpp"
#include "okay/Math/JointLimit.hpp"
#include "okay/Components/IKRestore.hpp"
#include "okay/Components/IKBones.hpp"
#include "okay/Components/FootIK.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>
#include <cmath>
#include <string>

namespace okay {

class FullBodyIK : public Component {
public:
    int ExecutionOrder() const override { return 100; }   // after FootIK (85) and holding (90), before limb IK (105)
    enum BoneId {
        Hips, Spine, Chest, Neck, Head,
        LUpperArm, LForeArm, LHand, RUpperArm, RForeArm, RHand,
        LThigh, LShin, LFoot, RThigh, RShin, RFoot,
        BoneCount
    };
    static const char* BoneLabel(int i) {
        static const char* n[BoneCount] = {"Hips", "Spine", "Chest", "Neck", "Head",
            "L Upper Arm", "L Forearm", "L Hand", "R Upper Arm", "R Forearm", "R Hand",
            "L Thigh", "L Shin", "L Foot", "R Thigh", "R Shin", "R Foot"};
        return (i >= 0 && i < BoneCount) ? n[i] : "?";
    }

    /// A limb goal (hand or foot).
    struct Effector {
        bool        enabled = false;
        Transform*  target = nullptr;       ///< reach this object (else `position`)
        std::string targetName;
        Vec3        position{0, 0, 0};
        float       positionWeight = 1.0f;
        float       rotationWeight = 0.0f;   ///< match the target object's rotation (grip / plant)
        Transform*  hint = nullptr;          ///< elbow/knee bend toward this (else keep the animated bend)
        std::string hintName;
        float       pull = 1.0f;             ///< how much this effector may drag the body
        /// Swing/twist limits on the upper (shoulder/hip) and lower (elbow/knee)
        /// bone, measured from the rest pose (JointLimit.hpp). None = unlimited.
        IKJointLimit upperLimit, lowerLimit;
        /// Seconds to swing over when the goal switches (another target object,
        /// object <-> point) or jumps; measured on the character, so moving it never trails.
        float       targetBlendTime = 0.15f;
    };
    enum Limb { LeftHand, RightHand, LeftFoot, RightFoot, LimbCount };
    Effector effectors[LimbCount];

    // Bones (auto-mapped at Start when empty; names are serialized).
    Transform*  bones[BoneCount] = {};
    std::string boneNames[BoneCount];
    bool        autoMap = true;

    float weight = 1.0f;
    float stateWeight = 1.0f;        ///< x weight; set per state by the Animator state machine
    bool  ikActive = true;           ///< eases over fadeTime
    float fadeTime = 0.0f;
    float softness = 0.03f;          ///< soft-IK band as a fraction of limb length (0 = off)
    float bodyPull = 0.6f;           ///< 0..1: how far out-of-reach effectors move the hips
    float maxBodyOffset = 0.6f;      ///< clamp on the hip shift (world units)
    float spineReach = 0.4f;         ///< 0..1: chest bend toward hand goals still out of reach
    float maxSpineAngle = 35.0f;     ///< degrees
    float blendSpeed = 8.0f;         ///< how fast goals fade in/out when enabled/disabled or reweighted (per second, 0 = instant)
    float goalSmoothing = 0.0f;      ///< follow moving goals smoothly (ease rate per second, 0 = exact)
    float armMinBend = 20.0f;        ///< elbows: the tightest fold allowed (interior angle, degrees)
    float legMinBend = 25.0f;        ///< knees: likewise

    // Look-at.
    bool        lookEnabled = false;
    Transform*  lookTarget = nullptr;
    std::string lookTargetName;
    Vec3        lookPosition{0, 0, 0};
    float       lookWeight = 1.0f;
    float       lookHeadShare = 0.7f;     ///< the head turns this share, the chest the rest
    float       maxLookAngle = 70.0f;
    Vec3        headForward{0, 0, 1};     ///< the head bone's local forward axis
    float       lookSmoothing = 10.0f;    ///< head swings toward a new look point (ease rate per second, 0 = snap)

    /// Weight an effector is applied with right now (eases toward its target weight).
    float EffectiveWeight(int l) const { return (l >= 0 && l < LimbCount) ? m_effW[l] : 0.0f; }
    /// True when the limb's end is within `tolerance` of its goal (enabled goals only).
    bool Reached(int l, float tolerance = 0.05f) const {
        if (l < 0 || l >= LimbCount || !effectors[l].enabled) return false;
        Transform *a, *b, *c; LimbBones(l, a, b, c);
        return c && (c->Position() - Goal(effectors[l])).Magnitude() <= tolerance;
    }

    void Start() override {
        Scene* s = GetScene();
        if (!s) return;
        for (int i = 0; i < BoneCount; ++i)
            if (!bones[i] && !boneNames[i].empty()) if (GameObject* g = FindUnder(boneNames[i])) bones[i] = g->transform;
        if (autoMap) MapBones(false);
        // Targets resolve inside this character first (copies share names), then the scene.
        auto R = [&](Transform*& t, const std::string& n) { if (!t && !n.empty()) if (GameObject* g = FindUnder(n)) t = g->transform; };
        for (auto& e : effectors) { R(e.target, e.targetName); R(e.hint, e.hintName); }
        R(lookTarget, lookTargetName);
        for (IKPole& p : m_pole) p.Reset();
    }

    /// Find bones by common rig names under this object. Keeps already-assigned
    /// bones unless `overwrite`. Returns how many bones are mapped afterwards.
    int MapBones(bool overwrite) {
        if (!gameObject || !gameObject->transform) return 0;
        std::vector<Transform*> all;
        Collect(gameObject->transform, all);
        for (Transform* t : all) {
            if (!t->gameObject || t == gameObject->transform) continue;
            int id = Classify(t->gameObject->name);
            if (id < 0) continue;
            if (bones[id] && !overwrite) continue;
            // Prefer the shallowest match (e.g. "Spine" over "Spine1" for Spine).
            if (bones[id] && overwrite && Depth(bones[id]) <= Depth(t)) continue;
            bones[id] = t;
            boneNames[id] = t->gameObject->name;
        }
        int n = 0;
        for (Transform* b : bones) if (b) ++n;
        return n;
    }

    /// An object by name inside this character (else anywhere in the scene).
    GameObject* FindBone(const std::string& name) const { return FindUnder(name); }

    /// Classify a bone name into a BoneId (-1 = not a humanoid bone we drive).
    static int Classify(const std::string& raw) {
        std::string n;
        for (char c : raw) n += (char)std::tolower((unsigned char)c);
        if (auto c = n.rfind(':'); c != std::string::npos) n = n.substr(c + 1);   // mixamorig:LeftArm
        auto has = [&](const char* k) { return n.find(k) != std::string::npos; };
        auto ends = [&](const char* k) { std::size_t l = std::strlen(k); return n.size() >= l && n.compare(n.size() - l, l, k) == 0; };
        const bool left  = has("left") || n.rfind("l ", 0) == 0 || n.rfind("l_", 0) == 0 || ends("_l") || ends(".l") || ends(" l");
        const bool right = has("right") || n.rfind("r ", 0) == 0 || n.rfind("r_", 0) == 0 || ends("_r") || ends(".r") || ends(" r");
        if (has("finger") || has("thumb") || has("index") || has("toe") || has("end") || has("twist") ||
            has("eye") || has("jaw") || has("shoulder") || has("clavicle")) return -1;
        if (left || right) {
            const int base = left ? 0 : 1;
            if (has("forearm") || has("lowerarm") || has("lower arm") || has("lower_arm") || has("elbow")) return base ? RForeArm : LForeArm;
            if (has("upperarm") || has("upper arm") || has("upper_arm") || ends("arm")) return base ? RUpperArm : LUpperArm;
            if (has("hand") || has("wrist")) return base ? RHand : LHand;
            if (has("upleg") || has("thigh") || has("upperleg") || has("upper leg") || has("upper_leg")) return base ? RThigh : LThigh;
            if (has("shin") || has("calf") || has("lowerleg") || has("lower leg") || has("lower_leg") || has("knee") || ends("leg")) return base ? RShin : LShin;
            if (has("foot") || has("ankle")) return base ? RFoot : LFoot;
            return -1;
        }
        if (has("hips") || has("pelvis")) return Hips;
        if (has("head")) return has("top") ? -1 : Head;
        if (has("neck")) return Neck;
        if (has("chest") || has("torso") || n == "spine2" || n == "spine_02" || n == "spine.002" || n == "spine1" || n == "spine_01")
            return Chest;
        if (n == "spine" || has("spine")) return Spine;
        return -1;
    }

    void Update(float) override { m_restore.Restore(); }   // before the animator blends (see IKRestore.hpp)
    void LateUpdate(float dt) override {
        // Corrections are layered on this frame's animated pose: last frame's writes
        // come off first wherever nothing re-posed the bone since (no animation on it).
        m_restore.Restore();
        Transform* spineBone = bones[Chest] ? bones[Chest] : bones[Spine];
        Transform* lowSpine = (bones[Chest] && bones[Spine]) ? bones[Spine] : nullptr;   // shares the chest bend
        // Per-goal weights ease toward their targets: switching a goal on/off (or a
        // script reaching then releasing) fades instead of popping. All easing is
        // frame-rate independent: the same numbers look the same at 30 and 144 fps.
        auto ease = [dt](float rate) { return rate > 0.0f ? 1.0f - std::exp(-rate * dt) : 1.0f; };
        const float k = ease(blendSpeed), gk = ease(goalSmoothing), lk = ease(lookSmoothing);
        for (int l = 0; l < LimbCount; ++l) {
            const Effector& e = effectors[l];
            const float want = e.enabled ? e.positionWeight : 0.0f;
            m_effW[l] += (want - m_effW[l]) * k;
            if (std::fabs(want - m_effW[l]) < 1e-3f) m_effW[l] = want;
            // A broken goal (NaN, infinity) sits this frame out; the limb keeps its animated pose.
            const Vec3 raw = Goal(e);
            m_goalOk[l] = Finite(raw);
            // A goal that isn't in play starts fresh when it comes in (the weight fade eases that).
            if (!e.enabled && m_effW[l] <= 1e-4f) m_blend[l].Reset();
            const Vec3 g = m_goalOk[l] ? m_blend[l].Step(raw, (std::uintptr_t)e.target, e.targetBlendTime, dt, CharacterSpace()) : raw;
            if (!m_goalOk[l]) continue;
            if (!m_goalValid[l] || gk >= 1.0f || m_effW[l] <= 1e-4f) m_goal[l] = g;
            else m_goal[l] = m_goal[l] + (g - m_goal[l]) * gk;
            m_goalValid[l] = true;
        }
        bool lookOk = false;
        {
            const float want = lookEnabled ? lookWeight : 0.0f;
            m_lookW += (want - m_lookW) * k;
            if (std::fabs(want - m_lookW) < 1e-3f) m_lookW = want;
            const Vec3 g = lookTarget ? lookTarget->Position() : lookPosition;
            if (Finite(g)) {
                if (!m_lookValid || m_lookW <= 1e-4f) m_lookGoal = g;
                else m_lookGoal = m_lookGoal + (g - m_lookGoal) * lk;
                m_lookValid = lookOk = true;
            }
        }
        const float w = weight * stateWeight * m_fade.Step(ikActive, fadeTime, dt);
        if (!(w > 0.0f)) return;
        const bool look = lookOk && m_lookW > 0.0f;
        // 0) Look-at, the chest's share: before the limbs, so the arms solve from
        // the turned chest and hand goals are still met.
        if (look && bones[Head] && spineBone && lookHeadShare < 1.0f)
            TurnToward(spineBone, bones[Head], m_lookGoal, (1.0f - lookHeadShare) * m_lookW * w);
        // 1) Body pull: effectors beyond their limb's reach drag the hips toward them.
        if (bodyPull > 0.0f && bones[Hips]) {
            Vec3 sum{0, 0, 0}; float wsum = 0.0f;
            for (int l = 0; l < LimbCount; ++l) {
                const Effector& e = effectors[l];
                Transform *a, *b, *c; LimbBones(l, a, b, c);
                if (!Active(l) || !a || !b || !c) continue;
                const float reach = LimbLength(l) * 0.995f;
                const Vec3 goal = m_goal[l], d = goal - a->Position();
                const float dist = d.Magnitude();
                float ew = m_effW[l] * e.pull;
                if (dist > reach && dist > 1e-5f) sum = sum + d * ((dist - reach) / dist) * ew;
                // Feet also pull DOWN onto lower ground (crouch to reach a low step).
                wsum += ew;
            }
            if (wsum > 0.0f) {
                Vec3 off = sum * (bodyPull * w / std::max(1.0f, wsum));
                const float m = off.Magnitude();
                if (m > maxBodyOffset && m > 1e-6f) off = off * (maxBodyOffset / m);
                if (m > 1e-6f && Finite(off)) {
                    // A FootIK on this character planted the feet already: the hip
                    // move goes on its books and it re-plants the legs (no foot slide).
                    FootIK* feet = Feet();
                    (feet ? feet->Restorer() : m_restore).Touch(bones[Hips]);
                    bones[Hips]->SetPosition(bones[Hips]->Position() + off);
                    if (feet) feet->ResolveLegs();
                }
            }
        }
        // 2) Spine reach: bend the chest toward hand goals still out of reach.
        // Spread over the spine bones (lower spine, then chest) so the back curves
        // instead of hinging at one joint.
        if (spineReach > 0.0f && spineBone) {
            for (int l = LeftHand; l <= RightHand; ++l) {
                Transform *a, *b, *c; LimbBones(l, a, b, c);
                if (!Active(l) || !a) continue;
                const Vec3 goal = m_goal[l];
                const float reach = LimbLength(l);
                const float dist = (goal - a->Position()).Magnitude();
                if (dist <= reach) continue;
                Transform* parts[2] = {lowSpine, spineBone};
                const int nParts = lowSpine ? 2 : 1;
                for (int pi = lowSpine ? 0 : 1; pi < 2; ++pi) {
                    Transform* sb = parts[pi];
                    const Vec3 sp = sb->Position();
                    Vec3 from = a->Position() - sp, to = goal - sp;
                    if (from.SqrMagnitude() < 1e-8f || to.SqrMagnitude() < 1e-8f) continue;
                    Quat q = Quat::FromToRotation(from.Normalized(), to.Normalized());
                    float ang = Quat::Angle(Quat::Identity, q);
                    float t = spineReach * w * m_effW[l] * std::min(1.0f, (dist - reach) / std::max(reach, 1e-3f) * 2.0f) / (float)nParts;
                    const float cap = maxSpineAngle / (float)nParts;
                    if (ang * t > cap && ang > 1e-4f) t = cap / ang;
                    m_restore.Touch(sb);
                    sb->SetRotation((Quat::Slerp(Quat::Identity, q, t) * sb->Rotation()).Normalized());
                }
            }
        }
        // 3) Limbs.
        for (int l = 0; l < LimbCount; ++l) SolveLimb(l, w);
        // 4) Look-at: the head's share (the chest turned before the limbs).
        if (look && bones[Head])
            TurnToward(bones[Head], bones[Head], m_lookGoal, lookHeadShare * m_lookW * w, true);
        m_restore.Commit();
    }

    /// World goal of an effector (its target object, else its position).
    static Vec3 Goal(const Effector& e) { return e.target ? e.target->Position() : e.position; }
    void LimbBones(int l, Transform*& a, Transform*& b, Transform*& c) const {
        const int base = l == LeftHand ? LUpperArm : l == RightHand ? RUpperArm : l == LeftFoot ? LThigh : RThigh;
        a = bones[base]; b = bones[base + 1]; c = bones[base + 2];
    }
    float LimbLength(int l) const {
        Transform *a, *b, *c; LimbBones(l, a, b, c);
        if (!a || !b || !c) return 0.0f;
        return (b->Position() - a->Position()).Magnitude() + (c->Position() - b->Position()).Magnitude();
    }

private:
    GameObject* FindUnder(const std::string& name) const {
        if (!gameObject || !gameObject->transform) return nullptr;
        std::vector<Transform*> all; Collect(gameObject->transform, all);
        for (Transform* t : all) if (t->gameObject && t->gameObject->name == name) return t->gameObject;
        if (Scene* s = gameObject->scene()) return s->Find(name);
        return nullptr;
    }
    static void Collect(Transform* t, std::vector<Transform*>& out) {
        out.push_back(t);
        for (Transform* c : t->Children()) if (c) Collect(c, out);
    }
    static int Depth(Transform* t) { int d = 0; for (; t; t = t->Parent()) ++d; return d; }

    bool Active(int l) const { return m_effW[l] > 0.0f && m_goalOk[l] && m_goalValid[l]; }

    const Transform* CharacterSpace() const {
        const Transform* r = CharacterRootOf(transform);
        return r ? r : transform;
    }
    /// The running FootIK on this character (on this object, else its character root).
    FootIK* Feet() const {
        FootIK* f = gameObject ? gameObject->GetComponent<FootIK>() : nullptr;
        if (!f) if (const Transform* r = CharacterRootOf(transform)) if (r->gameObject) f = r->gameObject->GetComponent<FootIK>();
        return f && f->enabled && f->gameObject && f->gameObject->active && f->EffWeight() > 0.0f ? f : nullptr;
    }

    void SolveLimb(int l, float w) {
        const Effector& e = effectors[l];
        Transform *a, *b, *c; LimbBones(l, a, b, c);
        if (!Active(l) || !a || !b || !c) return;
        const float pw = m_effW[l] * w;
        if (pw > 0.0f) {
            const Vec3 root = a->Position(), oldMid = b->Position(), oldEnd = c->Position();
            const float up = (oldMid - root).Magnitude(), lo = (oldEnd - oldMid).Magnitude();
            if (up < 1e-5f || lo < 1e-5f || !Finite(root) || !Finite(oldMid) || !Finite(oldEnd)) return;
            // Bend direction: toward the hint, else keep the animated bend, easing to
            // a character-space default (knees forward, elbows back) as the limb straightens.
            const Vec3 fallback = l >= LeftFoot ? Vec3{0, 0, 1} : Vec3{0, 0, -1};
            const Vec3 pole = m_pole[l].Direction(root, oldMid, oldEnd, transform ? transform->Rotation() : Quat::Identity,
                                                   e.hint ? IKPoleMode::Object : IKPoleMode::Auto, fallback, e.hint);
            // Partial weight blends the GOAL (from where the animation put the hand),
            // then solves exactly. Each frame starts from the animated pose, so half
            // weight stays half.
            Vec3 mid, end;
            const float minBend = l >= LeftFoot ? legMinBend : armMinBend;
            const Vec3 goal = pw >= 1.0f ? m_goal[l] : Vec3::Lerp(oldEnd, m_goal[l], pw);
            SolveTwoBoneIKSoft(root, up, lo, goal, pole, softness * (up + lo), mid, end, std::max(0.0f, minBend), 180.0f);
            if (!Finite(mid) || !Finite(end)) return;
            m_restore.Touch(a); m_restore.Touch(b); m_restore.Touch(c);
            const bool limited = e.upperLimit.Active() || e.lowerLimit.Active();
            if (limited) CaptureLimitFrames(l, a, b, c, root, oldMid, oldEnd, mid, end);
            // Turn the bones (a skinned mesh follows): the upper toward the solved
            // mid joint, then the lower, as turned by its parent, toward the solved end.
            a->SetRotation(AimBone(a, oldMid, mid));
            if (e.upperLimit.Active()) a->localRotation = ApplyLimit(a->localRotation, m_frame[l][0], e.upperLimit);
            b->SetRotation(AimBone(b, c->Position(), end));
            if (e.lowerLimit.Active())
                b->localRotation = ApplyLimit(b->localRotation, m_frame[l][1], e.lowerLimit,
                                              e.lowerLimit.type == IKJointLimit::Type::Hinge);
            // A bone chain keeps its lengths; joints that aren't parented are placed.
            if (!IsUnder(b, a)) b->SetPosition(mid);
            if (!IsUnder(c, b)) c->SetPosition(end);
        }
        if (e.target && e.rotationWeight > 0.0f && e.positionWeight > 1e-4f && Finite(e.target->Rotation())) {
            m_restore.Touch(c);
            c->SetRotation(Quat::Slerp(c->Rotation(), e.target->Rotation(), e.rotationWeight * w * std::min(1.0f, m_effW[l] / e.positionWeight)));
        }
    }
    /// The rest frames the joint limits measure from, captured when the bones (re)resolve.
    void CaptureLimitFrames(int l, Transform* a, Transform* b, Transform* c, const Vec3& root, const Vec3& oldMid,
                            const Vec3& oldEnd, const Vec3& mid, const Vec3& end) {
        IKJointFrame& fu = m_frame[l][0]; IKJointFrame& fl = m_frame[l][1];
        if (!fu.CapturedFor(a)) fu.Capture(a, b, c);
        if (!fl.CapturedFor(b)) fl.Capture(b, c, nullptr);
        if (!fu.hasHinge) fu.SetHingeFromBend(root, oldMid, oldEnd);
        if (!fl.hasHinge) fl.SetHingeFromBend(root, oldMid, oldEnd);
        if (!fu.hasHinge) fu.SetHingeFromBend(root, mid, end);
        if (!fl.hasHinge) fl.SetHingeFromBend(root, mid, end);
    }
    // Rotate `bone` so `aimFrom`'s forward turns toward `goal` by fraction t, clamped.
    void TurnToward(Transform* bone, Transform* aimFrom, const Vec3& goal, float t, bool clamp = false) {
        if (!bone || !aimFrom || t <= 0.0f) return;
        Vec3 fwd = (aimFrom->Rotation() * headForward).Normalized();
        Vec3 to = goal - aimFrom->Position();
        if (to.SqrMagnitude() < 1e-8f || fwd.SqrMagnitude() < 1e-8f) return;
        Quat q = Quat::FromToRotation(fwd, to.Normalized());
        float ang = Quat::Angle(Quat::Identity, q);
        if (clamp && ang * t > maxLookAngle && ang > 1e-4f) t = maxLookAngle / ang;
        m_restore.Touch(bone);
        bone->SetRotation((Quat::Slerp(Quat::Identity, q, std::min(t, 1.0f)) * bone->Rotation()).Normalized());
    }

    IKRestore m_restore;
    IKPole m_pole[LimbCount];
    IKGoalBlend m_blend[LimbCount];           ///< eases a switched or jumping goal
    IKJointFrame m_frame[LimbCount][2];       ///< upper / lower rest frames for the limits
    IKFade m_fade;
    float m_effW[LimbCount] = {};             ///< eased effector weights
    Vec3  m_goal[LimbCount];                  ///< smoothed goals
    bool  m_goalValid[LimbCount] = {};
    bool  m_goalOk[LimbCount] = {};           ///< this frame's goal is a real point
    float m_lookW = 0.0f;
    Vec3  m_lookGoal{0, 0, 0};
    bool  m_lookValid = false;
};

} // namespace okay
