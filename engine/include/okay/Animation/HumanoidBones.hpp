#pragma once
// ---------------------------------------------------------------------------
// HumanoidBones — the one place that reads a rig's bone names and decides
// which bone is the hips, the left thigh, the right hand... and whether the rig
// is a humanoid at all. HumanoidRetarget, FootIK, FullBodyIK, the model
// importer and the editor's Auto-Detect buttons all map through it, so a rig
// maps the same way everywhere.
//
// Names: Mixamo (mixamorig:LeftUpLeg), engine mannequin rigs (thigh_l,
// upperarm_l, spine_01..05), Blender / Rigify (upper_arm.L, DEF-thigh.L) and
// plain names. Helper bones that only look similar (ik_hand_l, twist, toes,
// fingers, *_end, poles, controls) never match, and an exact canonical name
// beats a substring hit, so node order in the file doesn't matter.
//
// Shape: names alone accept a dog with "front_leg_l" or a robot arm called
// "upperarm_l". MapHumanoidBones also checks the skeleton: each limb is a chain
// (shin under thigh, foot under shin; forearm under upper arm, hand under
// forearm), the legs and arms are separate chains, left and right agree between
// the legs and the arms, and the feet hang below the hips (opposite the head).
// ---------------------------------------------------------------------------
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Vec3.hpp"
#include <cctype>
#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace okay {
namespace humanoid {

/// The engine humanoid's 15 bones (Character::BoneIndex order).
enum Bone {
    Hips = 0, Torso, Head,
    LUpperArm, LForeArm, LHand,
    RUpperArm, RForeArm, RHand,
    LThigh, LShin, LFoot,
    RThigh, RShin, RFoot,
    BoneCount
};

/// A bone name without its rig prefix ("mixamorig:", "Armature|", "DEF-",
/// "Bip01 "...), lowercased.
inline std::string StripBoneName(const std::string& rawName) {
    std::string s0 = rawName, nm;
    std::size_t cut = s0.find_last_of(":|");
    if (cut != std::string::npos) s0 = s0.substr(cut + 1);
    for (char c : s0) nm += (char)std::tolower((unsigned char)c);
    for (const char* pre : {"def-", "def_", "org-", "mch-", "bip01 ", "bip01_", "bip001 ", "bip001_", "b_", "bone_"})
        if (nm.rfind(pre, 0) == 0) { nm = nm.substr(std::strlen(pre)); break; }
    if (nm.rfind("mixamorig", 0) == 0) nm = nm.substr(9);
    return nm;
}

/// Which of the 15 bones a name stands for, or -1. `score` = 2 for an exact
/// canonical name, 1 for a looser match.
inline int ClassifyBone(const std::string& rawName, int& score) {
    score = 0;
    const std::string nm = StripBoneName(rawName);
    auto has = [&](const char* k) { return nm.find(k) != std::string::npos; };
    // Helpers that are never the deforming limb bone itself.
    static const char* kSkip[] = {"ik_", "_ik", "ik.", ".ik", "twist", "roll", "ball", "toe", "thumb",
        "index", "middle", "ring", "pinky", "finger", "_end", ".end", "end_", "tip", "pole", "target",
        "ctrl", "control", "helper", "corrective", "weapon", "prop", "socket", "attach", "center_of_mass",
        "interaction", "dummy", "nub", "jaw", "eye", "tongue", "breast", "hair"};
    for (const char* k : kSkip) if (has(k)) return -1;
    // Normalise separators so "upper_arm.l", "upperarm_l", "UpperArm L" compare equal.
    std::string key;
    for (char c : nm) if (std::isalnum((unsigned char)c)) key += c;
    static const std::pair<const char*, int> kExact[] = {
        {"hips", 0}, {"pelvis", 0}, {"hip", 0},
        {"head", 2},
        {"leftarm", 3}, {"upperarml", 3}, {"larm", 3}, {"arml", 3}, {"lupperarm", 3}, {"leftupperarm", 3},
        {"leftforearm", 4}, {"lowerarml", 4}, {"forearml", 4}, {"lforearm", 4}, {"lelbow", 4}, {"leftlowerarm", 4},
        {"lefthand", 5}, {"handl", 5}, {"lhand", 5},
        {"rightarm", 6}, {"upperarmr", 6}, {"rarm", 6}, {"armr", 6}, {"rupperarm", 6}, {"rightupperarm", 6},
        {"rightforearm", 7}, {"lowerarmr", 7}, {"forearmr", 7}, {"rforearm", 7}, {"relbow", 7}, {"rightlowerarm", 7},
        {"righthand", 8}, {"handr", 8}, {"rhand", 8},
        {"leftupleg", 9}, {"thighl", 9}, {"lthigh", 9}, {"leftupperleg", 9}, {"leftthigh", 9}, {"upperlegl", 9},
        {"leftleg", 10}, {"calfl", 10}, {"shinl", 10}, {"lcalf", 10}, {"lshin", 10}, {"leftlowerleg", 10}, {"lowerlegl", 10}, {"leftknee", 10},
        {"leftfoot", 11}, {"footl", 11}, {"lfoot", 11}, {"leftankle", 11},
        {"rightupleg", 12}, {"thighr", 12}, {"rthigh", 12}, {"rightupperleg", 12}, {"rightthigh", 12}, {"upperlegr", 12},
        {"rightleg", 13}, {"calfr", 13}, {"shinr", 13}, {"rcalf", 13}, {"rshin", 13}, {"rightlowerleg", 13}, {"lowerlegr", 13}, {"rightknee", 13},
        {"rightfoot", 14}, {"footr", 14}, {"rfoot", 14}, {"rightankle", 14},
    };
    for (const auto& e : kExact) if (key == e.first) { score = 2; return e.second; }
    // Torso: any spine / chest link (the mapper keeps the whole chain).
    if (key.rfind("spine", 0) == 0 || key.rfind("chest", 0) == 0 || key == "upperchest" || key == "torso") { score = 2; return 1; }
    // Looser matches.
    auto endsW = [&](const char* k) {
        std::size_t kl = std::strlen(k);
        return nm.size() >= kl && nm.compare(nm.size() - kl, kl, k) == 0;
    };
    const bool isLeft  = has("left")  || nm.rfind("l_", 0) == 0 || endsW("_l") || endsW(".l") || endsW(" l");
    const bool isRight = has("right") || nm.rfind("r_", 0) == 0 || endsW("_r") || endsW(".r") || endsW(" r");
    score = 1;
    if (!isLeft && !isRight) {
        if (has("hips") || has("pelvis")) return 0;
        if (has("spine") || has("chest")) return 1;
        if (has("head") && !has("top")) return 2;
        return -1;
    }
    const int side = isLeft ? 0 : 1;
    if (has("shoulder") || has("clavicle") || has("collar")) return -1;
    if (has("hand") || has("wrist")) return 5 + side * 3;
    if (has("forearm") || has("lowerarm") || has("elbow") || (has("fore") && has("arm"))) return 4 + side * 3;
    if (has("upperarm") || has("uparm") || (has("arm") && !has("lower"))) return 3 + side * 3;
    if (has("upleg") || has("upperleg") || has("thigh") || has("hip")) return 9 + side * 3;
    if (has("foot") || has("ankle")) return 11 + side * 3;
    if (has("knee") || has("calf") || has("shin") || has("lowerleg") || has("leg")) return 10 + side * 3;
    return -1;
}

/// A neck link (not a humanoid bone the retarget drives, but Full Body IK turns it).
inline bool IsNeckName(const std::string& rawName) {
    const std::string nm = StripBoneName(rawName);
    return nm.find("neck") != std::string::npos && nm.find("twist") == std::string::npos;
}

/// What MapHumanoidBones found under a rig root.
struct BoneMap {
    GameObject* bone[BoneCount] = {};
    std::vector<GameObject*> spine;   ///< every spine / chest link from the hips up, shallowest first
    GameObject* neck = nullptr;
    int  found = 0;                   ///< how many of the 15 bones matched by name
    bool legsOk = false;              ///< both legs are thigh -> shin -> foot chains, below the hips
    bool humanoid = false;            ///< a two-legged, two-armed humanoid (see the header)
    std::string why;                  ///< why it isn't a humanoid ("" when it is)

    GameObject* Get(int b) const { return (b >= 0 && b < BoneCount) ? bone[b] : nullptr; }
    int LegBones() const { int n = 0; for (int b = LThigh; b <= RFoot; ++b) n += bone[b] != nullptr; return n; }
};

namespace detail {
inline int Depth(const Transform* t, const Transform* root) {
    int d = 0;
    for (const Transform* p = t ? t->Parent() : nullptr; p && p != root; p = p->Parent()) ++d;
    return d;
}
inline bool Under(const GameObject* child, const GameObject* parent) {   // strict descendant
    return child && parent && child != parent && child->IsSelfOrDescendantOf(parent);
}
inline Vec3 Pos(const GameObject* g) { return g && g->transform ? g->transform->Position() : Vec3{0, 0, 0}; }
inline float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline void Collect(Transform* t, std::vector<Transform*>& out) {
    if (!t) return;
    out.push_back(t);
    for (Transform* c : t->Children()) Collect(c, out);
}
} // namespace detail

/// Map the humanoid bones under `root` (its strict descendants) and check the
/// skeleton's shape.
inline BoneMap MapHumanoidBones(GameObject* root) {
    using namespace detail;
    BoneMap m;
    if (!root || !root->transform) return m;
    std::vector<Transform*> all;
    Collect(root->transform, all);
    int score[BoneCount], depth[BoneCount];
    for (int b = 0; b < BoneCount; ++b) score[b] = depth[b] = -1;
    std::vector<std::pair<int, GameObject*>> torso;   // (depth, link)
    bool fourLegged = false;
    int neckDepth = -1;
    for (Transform* t : all) {
        GameObject* g = t->gameObject;
        if (!g || t == root->transform) continue;
        const int d = Depth(t, root->transform);
        int sc = 0;
        const int b = ClassifyBone(g->name, sc);
        if (b < 0) {
            if (IsNeckName(g->name) && (!m.neck || d < neckDepth)) { m.neck = g; neckDepth = d; }
            continue;
        }
        if (b >= LThigh) {   // "front_leg_l", "hind_thigh_r": more than two legs
            const std::string nm = StripBoneName(g->name);
            for (const char* w : {"front", "back", "hind", "rear"}) if (nm.find(w) != std::string::npos) fourLegged = true;
        }
        if (b == Torso) torso.push_back({d, g});
        // Better score wins; on a tie the torso keeps the DEEPEST spine link
        // (spine_03 over spine_01, the one the arms hang from), everything else the shallowest.
        if (m.bone[b]) {
            if (sc < score[b]) continue;
            if (sc == score[b] && (b == Torso ? d <= depth[b] : d >= depth[b])) continue;
        }
        m.bone[b] = g; score[b] = sc; depth[b] = d;
    }
    for (GameObject* g : m.bone) m.found += g != nullptr;
    // The spine chain: the torso link and the spine links above it, shallowest first.
    if (GameObject* chest = m.bone[Torso]) {
        for (const auto& e : torso)
            if (e.second == chest || Under(chest, e.second)) m.spine.push_back(e.second);
        std::sort(m.spine.begin(), m.spine.end(), [&](GameObject* a, GameObject* b) {
            return Depth(a->transform, root->transform) < Depth(b->transform, root->transform);
        });
    }

    auto fail = [&](const char* why) { m.why = why; return m; };
    GameObject* const* B = m.bone;
    // Legs: thigh -> shin -> foot on each side, two separate chains.
    for (int s = 0; s < 2; ++s) {
        const int t = s ? RThigh : LThigh;
        if (!B[t] || !B[t + 1] || !B[t + 2]) return fail("it doesn't have two legs (thigh, shin and foot on each side)");
        if (!Under(B[t + 1], B[t]) || !Under(B[t + 2], B[t + 1]))
            return fail("its leg bones aren't chains (shin under thigh, foot under shin)");
    }
    if (Under(B[RThigh], B[LThigh]) || Under(B[LThigh], B[RThigh])) return fail("its left and right legs are one chain");
    if (fourLegged) return fail("it looks four-legged (front / hind legs)");
    // Up: from the hips toward the head (or the chest); the feet hang the other way.
    if (B[Hips] && (B[Head] || B[Torso])) {
        const Vec3 hips = Pos(B[Hips]);
        const Vec3 up = Pos(B[Head] ? B[Head] : B[Torso]) - hips;
        const Vec3 feet = (Pos(B[LFoot]) + Pos(B[RFoot])) * 0.5f - hips;
        if (Dot(up, up) > 1e-10f && Dot(feet, up) >= 0.0f) return fail("its feet aren't below its hips");
    }
    m.legsOk = true;

    // The rest of a humanoid: hips, a torso or head, and two arms.
    if (!B[Hips]) return fail("it has no hips / pelvis bone");
    if (!B[Torso] && !B[Head]) return fail("it has no spine or head");
    for (int s = 0; s < 2; ++s) {
        const int u = s ? RUpperArm : LUpperArm;
        if (!B[u]) return fail("it doesn't have two arms");
        if ((B[u + 1] && !Under(B[u + 1], B[u])) || (B[u + 2] && !Under(B[u + 2], B[u + 1] ? B[u + 1] : B[u])))
            return fail("its arm bones aren't chains (forearm under upper arm, hand under forearm)");
        for (int l = LThigh; l <= RFoot; ++l)
            if (Under(B[u], B[l]) || Under(B[l], B[u])) return fail("its arms and legs are one chain");
    }
    // Left and right agree: the left arm is on the same side as the left leg.
    {
        const Vec3 legs = Pos(B[LThigh]) - Pos(B[RThigh]);
        const Vec3 arms = Pos(B[LUpperArm]) - Pos(B[RUpperArm]);
        if (Dot(legs, legs) > 1e-10f && Dot(arms, arms) > 1e-10f && Dot(legs, arms) <= 0.0f)
            return fail("its left and right sides don't agree between the arms and the legs");
    }
    m.humanoid = true;
    return m;
}

} // namespace humanoid
} // namespace okay
