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
// Bones are found automatically from common rig names (Mixamo "mixamorig:LeftArm",
// Unity/Blender "UpperArm.L", "thigh_l", the built-in Character parts "L Hand"...)
// under this object, or assigned by name. Bones are ROTATED (so a skinned mesh
// bends with them); rigs whose joints aren't parented are positioned as well.
// Solve happens in LateUpdate, after every animation driver.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Math/TwoBoneIK.hpp"
#include "okay/Math/IKFade.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>
#include <cmath>
#include <string>

namespace okay {

class FullBodyIK : public Component {
public:
    int ExecutionOrder() const override { return 100; }   // after animation
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
    };
    enum Limb { LeftHand, RightHand, LeftFoot, RightFoot, LimbCount };
    Effector effectors[LimbCount];

    // Bones (auto-mapped at Start when empty; names are serialized).
    Transform*  bones[BoneCount] = {};
    std::string boneNames[BoneCount];
    bool        autoMap = true;

    float weight = 1.0f;
    bool  ikActive = true;           ///< eases over fadeTime
    float fadeTime = 0.0f;
    float softness = 0.03f;          ///< soft-IK band as a fraction of limb length (0 = off)
    float bodyPull = 0.6f;           ///< 0..1: how far out-of-reach effectors move the hips
    float maxBodyOffset = 0.6f;      ///< clamp on the hip shift (world units)
    float spineReach = 0.4f;         ///< 0..1: chest bend toward hand goals still out of reach
    float maxSpineAngle = 35.0f;     ///< degrees

    // Look-at.
    bool        lookEnabled = false;
    Transform*  lookTarget = nullptr;
    std::string lookTargetName;
    Vec3        lookPosition{0, 0, 0};
    float       lookWeight = 1.0f;
    float       lookHeadShare = 0.7f;     ///< the head turns this share, the chest the rest
    float       maxLookAngle = 70.0f;
    Vec3        headForward{0, 0, 1};     ///< the head bone's local forward axis

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

    void LateUpdate(float dt) override {
        // Corrections are layered on this frame's animated pose. If nothing re-posed a
        // bone since our last solve (no animation on it), take last frame's correction
        // off first so it doesn't compound frame after frame.
        Transform* spineBone = bones[Chest] ? bones[Chest] : bones[Spine];
        m_hips.Undo(); m_spine.Undo(); m_head.Undo();
        const float w = weight * m_fade.Step(ikActive, fadeTime, dt);
        m_hips.Begin(bones[Hips]); m_spine.Begin(spineBone); m_head.Begin(bones[Head]);
        if (w <= 0.0f) { m_hips.End(); m_spine.End(); m_head.End(); return; }
        // 1) Body pull: effectors beyond their limb's reach drag the hips toward them.
        if (bodyPull > 0.0f && bones[Hips]) {
            Vec3 sum{0, 0, 0}; float wsum = 0.0f;
            for (int l = 0; l < LimbCount; ++l) {
                const Effector& e = effectors[l];
                Transform *a, *b, *c; LimbBones(l, a, b, c);
                if (!e.enabled || e.positionWeight <= 0.0f || !a || !b || !c) continue;
                const float reach = LimbLength(l) * 0.995f;
                const Vec3 goal = Goal(e), d = goal - a->Position();
                const float dist = d.Magnitude();
                float ew = e.positionWeight * e.pull;
                if (dist > reach && dist > 1e-5f) sum = sum + d * ((dist - reach) / dist) * ew;
                // Feet also pull DOWN onto lower ground (crouch to reach a low step).
                wsum += ew;
            }
            if (wsum > 0.0f) {
                Vec3 off = sum * (bodyPull * w / std::max(1.0f, wsum));
                const float m = off.Magnitude();
                if (m > maxBodyOffset && m > 1e-6f) off = off * (maxBodyOffset / m);
                if (m > 1e-6f) bones[Hips]->SetPosition(bones[Hips]->Position() + off);
            }
        }
        // 2) Spine reach: bend the chest toward hand goals still out of reach.
        if (spineReach > 0.0f && spineBone) {
            for (int l = LeftHand; l <= RightHand; ++l) {
                const Effector& e = effectors[l];
                Transform *a, *b, *c; LimbBones(l, a, b, c);
                if (!e.enabled || e.positionWeight <= 0.0f || !a) continue;
                const Vec3 goal = Goal(e), sp = spineBone->Position();
                const float reach = LimbLength(l);
                const float dist = (goal - a->Position()).Magnitude();
                if (dist <= reach) continue;
                Vec3 from = a->Position() - sp, to = goal - sp;
                if (from.SqrMagnitude() < 1e-8f || to.SqrMagnitude() < 1e-8f) continue;
                Quat q = Quat::FromToRotation(from.Normalized(), to.Normalized());
                float ang = Quat::Angle(Quat::Identity, q);
                float t = spineReach * w * e.positionWeight * std::min(1.0f, (dist - reach) / std::max(reach, 1e-3f) * 2.0f);
                if (ang * t > maxSpineAngle && ang > 1e-4f) t = maxSpineAngle / ang;
                spineBone->SetRotation((Quat::Slerp(Quat::Identity, q, t) * spineBone->Rotation()).Normalized());
            }
        }
        // 3) Limbs.
        for (int l = 0; l < LimbCount; ++l) SolveLimb(l, w);
        // 4) Look-at.
        if (lookEnabled && lookWeight > 0.0f && bones[Head]) {
            const Vec3 goal = lookTarget ? lookTarget->Position() : lookPosition;
            if (spineBone && lookHeadShare < 1.0f) TurnToward(spineBone, bones[Head], goal, (1.0f - lookHeadShare) * lookWeight * w);
            TurnToward(bones[Head], bones[Head], goal, lookHeadShare * lookWeight * w, true);
        }
        m_hips.End(); m_spine.End(); m_head.End();
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

    void SolveLimb(int l, float w) {
        const Effector& e = effectors[l];
        Transform *a, *b, *c; LimbBones(l, a, b, c);
        if (!e.enabled || !a || !b || !c) return;
        const float pw = e.positionWeight * w;
        if (pw > 0.0f) {
            const Vec3 root = a->Position(), oldMid = b->Position(), oldEnd = c->Position();
            const float up = (oldMid - root).Magnitude(), lo = (oldEnd - oldMid).Magnitude();
            if (up < 1e-5f || lo < 1e-5f) return;
            // Bend direction: the hint, else keep the animated bend (the current mid
            // joint's offset from the root->end line), else a sensible default.
            Vec3 pole = e.hint ? e.hint->Position() - root : oldMid - root;
            const Vec3 axis = oldEnd - root;
            if (!e.hint && axis.SqrMagnitude() > 1e-8f) {
                Vec3 ax = axis.Normalized();
                Vec3 perp = pole - ax * Vec3::Dot(pole, ax);
                if (perp.SqrMagnitude() < 1e-6f) pole = (l >= LeftFoot) ? Vec3{0, 0, 1} : Vec3{0, 0, -1};
            }
            Vec3 mid, end;
            SolveTwoBoneIKSoft(root, up, lo, Goal(e), pole, softness * (up + lo), mid, end);
            const Vec3 newMid = Vec3::Lerp(oldMid, mid, pw), newEnd = Vec3::Lerp(oldEnd, end, pw);
            // Rotate the bones (a skinned mesh follows), then pin positions for rigs
            // whose joints aren't children of each other.
            Vec3 o1 = oldMid - root, n1 = newMid - root;
            if (o1.SqrMagnitude() > 1e-10f && n1.SqrMagnitude() > 1e-10f)
                a->SetRotation((Quat::FromToRotation(o1.Normalized(), n1.Normalized()) * a->Rotation()).Normalized());
            Vec3 curMid = b->Position(), curEnd = c->Position();
            Vec3 o2 = curEnd - curMid, n2 = newEnd - newMid;
            if (o2.SqrMagnitude() > 1e-10f && n2.SqrMagnitude() > 1e-10f)
                b->SetRotation((Quat::FromToRotation(o2.Normalized(), n2.Normalized()) * b->Rotation()).Normalized());
            if ((b->Position() - newMid).SqrMagnitude() > 1e-6f) b->SetPosition(newMid);
            if ((c->Position() - newEnd).SqrMagnitude() > 1e-6f) c->SetPosition(newEnd);
        }
        if (e.target && e.rotationWeight > 0.0f)
            c->SetRotation(Quat::Slerp(c->Rotation(), e.target->Rotation(), e.rotationWeight * w));
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
        bone->SetRotation((Quat::Slerp(Quat::Identity, q, std::min(t, 1.0f)) * bone->Rotation()).Normalized());
    }

    // Remembers a bone's pose before/after our correction, to undo it next frame
    // when no animation re-posed the bone in between.
    struct Snap {
        Transform* t = nullptr;
        Vec3 pb, pa; Quat rb, ra; bool valid = false;
        void Begin(Transform* b) { t = b; if (t) { pb = t->Position(); rb = t->Rotation(); } }
        void End() { if (t) { pa = t->Position(); ra = t->Rotation(); valid = true; } else valid = false; }
        void Undo() {
            if (!valid || !t) return;
            const Quat r = t->Rotation();
            const bool same = (t->Position() - pa).SqrMagnitude() < 1e-10f &&
                              std::fabs(r.x * ra.x + r.y * ra.y + r.z * ra.z + r.w * ra.w) > 0.999999f;
            if (same) { t->SetRotation(rb); t->SetPosition(pb); }
        }
    };
    Snap m_hips, m_spine, m_head;
    IKFade m_fade;
};

} // namespace okay
