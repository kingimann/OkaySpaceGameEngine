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
        GameObject* rig = sc->Find(m_rigRootName);
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
            GameObject* g = sc->Find(e.node);
            if (!g || !g->transform || !g->IsSelfOrDescendantOf(rig)) continue;
            Quat desired = Re[e.bone] * e.align * e.restRootRel;      // root-frame target
            Quat rp = ChainRotation(g->transform->Parent(), rig);     // parent chain, current
            Quat local = rp.Inverse() * desired;
            g->transform->localRotation = weight >= 1.0f
                ? local.Normalized()
                : Quat::Slerp(e.restLocal, local.Normalized(), weight);
        }
        // Crouch / prone / landing dip: lower the whole model like the blocky
        // rig lowers its root (StanceOffset is in player-local units already).
        Vec3 so = ch->StanceOffset();
        rig->transform->localPosition = m_rigRestPos + Vec3{0.0f, so.y * weight, 0.0f};
    }

    /// Detect the humanoid bones by name under `rigRoot` and capture the rest
    /// pose. Returns how many of the 15 engine bones found a match.
    int DetectAndWire(Scene& s, GameObject* rigRoot) {
        m_map.clear(); m_mapped = 0;
        if (!rigRoot || !rigRoot->transform || !gameObject) return 0;
        Character* ch = gameObject->GetComponent<Character>();
        if (!ch) return 0;
        m_rigRootName = rigRoot->name;
        m_rigRestPos  = rigRoot->transform->localPosition;

        // ---- 1. name-match the imported bones ----------------------------
        const int N = Character::BoneCount();
        std::vector<GameObject*> node(N, nullptr);
        std::vector<int> depth(N, -1);
        auto lower = [](std::string v) { for (auto& c : v) c = (char)std::tolower((unsigned char)c); return v; };
        for (const auto& up : s.Objects()) {
            GameObject* g = up.get();
            if (!g || g == rigRoot || !g->IsSelfOrDescendantOf(rigRoot) || !g->transform) continue;
            std::string nm = lower(g->name);
            auto has = [&](const char* k) { return nm.find(k) != std::string::npos; };
            auto endsW = [&](const char* k) {
                std::size_t kl = std::char_traits<char>::length(k);
                return nm.size() >= kl && nm.compare(nm.size() - kl, kl, k) == 0;
            };
            bool isLeft  = has("left")  || nm.rfind("l_", 0) == 0 || endsW("_l") || endsW(".l");
            bool isRight = has("right") || nm.rfind("r_", 0) == 0 || endsW("_r") || endsW(".r");
            int bone = -1;
            if (!isLeft && !isRight) {
                if (has("hips") || has("pelvis")) bone = 0;                                   // B_HIPS
                else if (has("spine") || has("chest")) bone = 1;                              // B_TORSO (deepest wins below)
                else if (has("head") && !has("top") && !has("end")) bone = 2;                 // B_HEAD
            } else {
                int side = isLeft ? 0 : 1;   // engine: L block first (3..5, 9..11), R next
                if (has("hand") && !has("thumb") && !has("index") && !has("middle") &&
                    !has("ring") && !has("pinky") && !has("finger") && !has("end"))            bone = 5 + side * 3;
                else if (has("forearm") || has("lowerarm") || has("elbow") ||
                         (has("fore") && has("arm")))                                          bone = 4 + side * 3;
                else if (has("upperarm") || has("uparm") ||
                         (has("arm") && !has("fore") && !has("lower") && !has("hand")))        bone = 3 + side * 3;
                else if (has("upleg") || has("upperleg") || has("thigh"))                      bone = 9 + side * 3;
                else if (has("foot") || has("ankle"))                                          { if (!has("toe")) bone = 11 + side * 3; }
                else if (has("knee") || has("calf") || has("shin") || has("lowerleg") ||
                         has("leg"))                                                           bone = 10 + side * 3;
            }
            if (bone < 0) continue;
            // shoulders/clavicles must not steal the upper-arm slot
            if ((bone == 3 || bone == 6) && (has("shoulder") || has("clavicle") || has("collar"))) continue;
            int d = 0;
            for (Transform* t = g->transform->Parent(); t && t != rigRoot->transform; t = t->Parent()) ++d;
            // torso: keep the DEEPEST spine/chest link (Spine2 over Spine) —
            // everything else: first match wins.
            if (node[bone] && !(bone == 1 && d > depth[bone])) continue;
            node[bone] = g; depth[bone] = d;
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
            e.restLocal = node[b]->transform->localRotation;
            e.restRootRel = ChainRotation(node[b]->transform->Parent(), rigRoot) *
                            node[b]->transform->localRotation;
            e.align = align[b];
            m_map.push_back(e);
            ++m_mapped;
        }
        return m_mapped;
    }

private:
    struct MapEntry {
        int  bone = -1;          ///< engine bone index (Character::BoneIndex order)
        std::string node;        ///< imported bone object name
        Quat restLocal;          ///< bind local rotation (weight blend target)
        Quat restRootRel;        ///< bind rotation relative to the rig root
        Quat align;              ///< rest-stance alignment (imported dir -> engine dir)
    };
    std::vector<MapEntry> m_map;
    std::string m_rigRootName;
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
