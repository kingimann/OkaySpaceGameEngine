#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Math/Quat.hpp"
#include <string>
#include <vector>
#include <cctype>

namespace okay {

/// Plays the engine's BUILT-IN character animations on an imported humanoid
/// rig (Mixamo, glTF...) — walk/run/jump/crouch, gestures and emotions,
/// authored .okayanim clips, controller movement states, the lot — without
/// the model needing to ship any clips of its own.
///
/// Sits next to a Character (on the player); the Character runs in
/// drive-external mode (computes its pose, renders nothing) and each frame
/// this component transfers that pose onto the imported skeleton: the
/// engine bone's accumulated root-frame rotation is applied to the mapped
/// imported bone on top of a fixed rest-stance alignment (so a T-pose rig
/// adopts the engine's relaxed stance instead of walking with its arms out).
/// Bones are matched by name (the usual Mixamo / glTF conventions); anything
/// unmapped (fingers, twist bones, extra spine links) keeps its bind pose.
///
/// The model's own imported clips stay playable: a one-shot on the Model
/// Animator (play_clip_once) suspends the retarget until it finishes.
class HumanoidRetarget : public Component {
public:
    float weight = 1.0f;   ///< 0 = off, 1 = full pose transfer (blend vs the bind pose)

    /// How many of the 15 humanoid bones the last Wire() mapped (0 = not wired).
    int MappedBones() const { return m_mapped; }
    bool Wired() const { return m_init; }
    /// The imported bone mapped to engine bone `b` (nullptr if none).
    GameObject* MappedNode(int b) const {
        for (const MapEntry& e : m_map) if (e.bone == b) return e.go;
        return nullptr;
    }
    /// Drop the wiring so the next frame re-detects (after a model swap).
    void Rewire() { m_init = false; m_mapped = 0; m_map.clear(); }

    void LateUpdate(float dt) override {
        (void)dt;
        if (weight <= 0.0f || !gameObject) return;
        Scene* sc = gameObject->scene();
        if (!sc) return;
        Character* ch = gameObject->GetComponent<Character>();
        if (!ch || !ch->enabled) return;
        if (!m_init) { Wire(*sc); m_init = true; }
        if (m_map.empty()) return;
        GameObject* rig = (m_rig && !m_rig->IsDestroyed()) ? m_rig : sc->Find(m_rigRootName);
        if (!rig || !rig->transform) return;
        // The model's own clips take priority while a one-shot plays
        // (play_clip_once from a script — attack, emote from the file).
        if (ModelAnimator* ma = FindAnimator(*sc, rig))
            if (ma->OneShotActive()) return;

        // Engine pose: per-bone Euler (local deltas, identity rest frames) —
        // accumulate down the engine hierarchy into root-frame rotations.
        std::vector<Vec3> pose = ch->CurrentPose();
        const std::vector<Character::Bone> sk = ch->Skeleton();
        int n = (int)sk.size();
        if ((int)pose.size() < n) pose.resize(n, Vec3{0, 0, 0});
        std::vector<Quat> Re(n, Quat::Identity);
        for (int b = 0; b < n; ++b) {
            Quat q = Quat::Euler(pose[b]);
            Re[b] = sk[b].parent >= 0 ? Re[sk[b].parent] * q : q;
        }
        // Write the mapped imported bones top-down (parents first — the local
        // conversion below reads the ancestors' just-written rotations).
        for (const MapEntry& e : m_map) {
            GameObject* g = e.go;
            if (!g || g->IsDestroyed() || !g->transform || !g->IsSelfOrDescendantOf(rig)) continue;
            Quat desired = Re[e.bone] * e.align * e.restRootRel;      // root-frame target
            Quat rp = ChainRotation(g->transform->Parent(), rig);     // parent chain, current
            Quat local = rp.Inverse() * desired;
            g->transform->localRotation = weight >= 1.0f
                ? local.Normalized()
                : Quat::Slerp(e.restLocal, local.Normalized(), weight);
        }
        // Crouch / prone / landing dip: lower the whole model like the blocky
        // rig lowers its root (StanceOffset is in player-local units already).
        // (When the rig IS this object — built-in animations on a standalone
        // model — leave its position alone: that's where you placed it.)
        if (rig != gameObject) {
            Vec3 so = ch->StanceOffset();
            rig->transform->localPosition = m_rigRestPos + Vec3{0.0f, so.y * weight, 0.0f};
        }
    }

    /// Detect the humanoid bones by name under `rigRoot` and capture the rest
    /// pose. Returns how many of the 15 engine bones found a match.
    int DetectAndWire(Scene& s, GameObject* rigRoot) {
        m_map.clear(); m_mapped = 0;
        if (!rigRoot || !rigRoot->transform || !gameObject) return 0;
        Character* ch = gameObject->GetComponent<Character>();
        if (!ch) return 0;
        m_rigRootName = rigRoot->name;
        m_rig = rigRoot;
        m_rigRestPos  = rigRoot->transform->localPosition;

        // ---- 1. name-match the imported bones ----------------------------
        // Mixamo (mixamorig:LeftUpLeg), Unreal UE4/UE5 Mannequin (thigh_l,
        // upperarm_l, spine_01..05 — Quaternius' newer packs use it), Blender /
        // Rigify (upper_arm.L, DEF-thigh.L) and plain names. Helper bones that
        // only LOOK similar (ik_hand_l, upperarm_twist_01_l, ball_l, thumb_01_l,
        // *_end, poles, controls) are skipped, and an exact canonical name beats
        // a substring hit, so node order in the file doesn't matter.
        const int N = Character::BoneCount();
        std::vector<GameObject*> node(N, nullptr);
        std::vector<int> depth(N, -1), score(N, -1);
        for (const auto& up : s.Objects()) {
            GameObject* g = up.get();
            if (!g || g == rigRoot || !g->IsSelfOrDescendantOf(rigRoot) || !g->transform) continue;
            int sc = 0;
            const int bone = ClassifyBone(g->name, sc);
            if (bone < 0) continue;
            int d = 0;
            for (Transform* t = g->transform->Parent(); t && t != rigRoot->transform; t = t->Parent()) ++d;
            // Better score wins; on a tie the torso keeps the DEEPEST spine link
            // (spine_03 over spine_01), everything else the shallowest.
            if (node[bone]) {
                if (sc < score[bone]) continue;
                if (sc == score[bone] && (bone == 1 ? d <= depth[bone] : d >= depth[bone])) continue;
            }
            node[bone] = g; depth[bone] = d; score[bone] = sc;
        }

        // ---- 2. capture rest + build the alignment -----------------------
        const std::vector<Character::Bone> sk = ch->Skeleton();
        // Engine rest direction per bone (its child joint), leaves inherit later.
        auto engineDir = [&](int b) -> Vec3 {
            static const int childOf[15] = {1, 2, -1, 4, 5, -1, 7, 8, -1, 10, 11, -1, 13, 14, -1};
            int c = childOf[b];
            if (c < 0) return Vec3{0, 0, 0};
            Vec3 d = sk[c].joint - sk[b].joint;
            float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            return len > 1e-5f ? d * (1.0f / len) : Vec3{0, 0, 0};
        };
        Mat4 rootInv = rigRoot->transform->LocalToWorldMatrix().Inverse();
        auto rootRelPos = [&](GameObject* g) { return rootInv.MultiplyPoint(g->transform->Position()); };
        std::vector<Quat> align(N, Quat::Identity);
        static const int parentOf[15] = {-1, 0, 1, 1, 3, 4, 1, 6, 7, 0, 9, 10, 0, 12, 13};
        static const int childOf[15]  = {1, 2, -1, 4, 5, -1, 7, 8, -1, 10, 11, -1, 13, 14, -1};
        for (int b = 0; b < N; ++b) {
            if (!node[b]) continue;
            int c = childOf[b];
            if (c >= 0 && node[c]) {
                Vec3 di = rootRelPos(node[c]) - rootRelPos(node[b]);
                float len = std::sqrt(di.x * di.x + di.y * di.y + di.z * di.z);
                Vec3 de = engineDir(b);
                if (len > 1e-5f && !(de.x == 0 && de.y == 0 && de.z == 0))
                    align[b] = Quat::FromToRotation(di * (1.0f / len), de);
            } else if (parentOf[b] >= 0) {
                align[b] = align[parentOf[b]];   // leaves (head/hands/feet) follow their parent
            }
        }
        // Leaves computed before parents? parentOf is always < b in this table,
        // so the loop above already saw the parent. Build the map top-down.
        for (int b = 0; b < N; ++b) {
            if (!node[b]) continue;
            MapEntry e;
            e.bone = b;
            e.node = node[b]->name;
            e.go = node[b];
            e.restLocal = node[b]->transform->localRotation;
            e.restRootRel = ChainRotation(node[b]->transform->Parent(), rigRoot) *
                            node[b]->transform->localRotation;
            e.align = align[b];
            m_map.push_back(e);
            ++m_mapped;
        }
        return m_mapped;
    }

public:
    /// Which engine humanoid bone (0 hips, 1 torso, 2 head, 3-5 left arm,
    /// 6-8 right arm, 9-11 left leg, 12-14 right leg) a bone name stands for,
    /// or -1. `score` = 2 for an exact canonical name, 1 for a looser match.
    static int ClassifyBone(const std::string& rawName, int& score) {
        score = 0;
        std::string nm;
        {   // drop "mixamorig:" / "Armature|" / "DEF-" style prefixes, lowercase
            std::string s0 = rawName;
            std::size_t cut = s0.find_last_of(":|");
            if (cut != std::string::npos) s0 = s0.substr(cut + 1);
            for (char c : s0) nm += (char)std::tolower((unsigned char)c);
            for (const char* pre : {"def-", "def_", "org-", "mch-", "bip01 ", "bip01_", "bip001 ", "bip001_", "b_", "bone_"})
                if (nm.rfind(pre, 0) == 0) { nm = nm.substr(std::char_traits<char>::length(pre)); break; }
            if (nm.rfind("mixamorig", 0) == 0) nm = nm.substr(9);
        }
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
        // Torso: any spine / chest link (the deepest one wins in the caller).
        if (key.rfind("spine", 0) == 0 || key.rfind("chest", 0) == 0 || key == "upperchest" || key == "torso") { score = 2; return 1; }
        // Looser matches.
        auto endsW = [&](const char* k) {
            std::size_t kl = std::char_traits<char>::length(k);
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
        if (has("upleg") || has("upperleg") || has("thigh")) return 9 + side * 3;
        if (has("foot") || has("ankle")) return 11 + side * 3;
        if (has("knee") || has("calf") || has("shin") || has("lowerleg") || has("leg")) return 10 + side * 3;
        return -1;
    }

private:
    struct MapEntry {
        int  bone = -1;          ///< engine bone index (Character::BoneIndex order)
        std::string node;        ///< imported bone object name
        GameObject* go = nullptr;  ///< that bone (resolved once; immune to same-named bones elsewhere)
        Quat restLocal;          ///< bind local rotation (weight blend target)
        Quat restRootRel;        ///< bind rotation relative to the rig root
        Quat align;              ///< rest-stance alignment (imported dir -> engine dir)
    };
    std::vector<MapEntry> m_map;
    std::string m_rigRootName;
    GameObject* m_rig = nullptr;
    Vec3 m_rigRestPos{0, 0, 0};
    int  m_mapped = 0;
    bool m_init = false;

    /// Accumulated rotation of the parent chain from (exclusive) rigRoot down
    /// to `t` (inclusive) using the CURRENT local rotations.
    static Quat ChainRotation(Transform* t, GameObject* rigRoot) {
        Quat r = Quat::Identity;
        for (; t && (!rigRoot || t != rigRoot->transform); t = t->Parent())
            r = t->localRotation * r;
        return r;
    }

    /// Find the imported model under the player: the child subtree holding a
    /// ModelAnimator (skeleton root), used both to wire and to yield to clips.
    static ModelAnimator* FindAnimator(Scene& s, GameObject* rig) {
        if (ModelAnimator* ma = rig->GetComponent<ModelAnimator>()) return ma;
        for (const auto& up : s.Objects())
            if (up.get() && up->IsSelfOrDescendantOf(rig))
                if (ModelAnimator* ma = up->GetComponent<ModelAnimator>()) return ma;
        return nullptr;
    }

    /// Self-wire on first play frame: the rig root is the player child whose
    /// subtree carries a SkinnedMesh/ModelAnimator (the imported model).
    void Wire(Scene& s) {
        if (!m_rigRootName.empty()) {   // already wired once (or by AttachCharacterModel)
            if (GameObject* g = s.Find(m_rigRootName))
                if (m_map.empty()) DetectAndWire(s, g);
            if (!m_map.empty()) return;
        }
        if (!gameObject || !gameObject->transform) return;
        for (Transform* c : gameObject->transform->Children()) {
            GameObject* g = c ? c->gameObject : nullptr;
            if (!g || g->name == "Rig") continue;
            HumanoidRetarget probe;   // cheap name-detection dry run
            probe.gameObject = gameObject;
            if (probe.DetectAndWire(s, g) >= 8) { DetectAndWire(s, g); return; }
        }
    }
};

} // namespace okay
