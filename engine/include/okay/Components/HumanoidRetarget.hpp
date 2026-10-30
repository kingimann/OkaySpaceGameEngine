#pragma once
#include <unordered_set>
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/SkinnedMesh.hpp"
#include "okay/Animation/HumanoidBones.hpp"
#include "okay/Animation/HumanoidMotion.hpp"
#include "okay/Math/Quat.hpp"
#include <string>
#include <vector>
#include <cctype>
#include <cmath>

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
/// The model's own imported clips stay playable: a clip a script plays on the
/// Model Animator (play_clip until stop_clip, play_clip_once until it ends)
/// suspends the retarget meanwhile.
class HumanoidRetarget : public Component {
public:
    float weight = 1.0f;   ///< 0 = off, 1 = full pose transfer (blend vs the bind pose)
    /// Body lean into acceleration and turns, as a multiple of the default (a few
    /// degrees on the hips and spine; 0 = upright). Starting off leans forward,
    /// stopping leans back, a turn leans into the curve. Built-in poses only: an
    /// imported clip carries its own.
    float leanScale = 1.0f;
    /// The lean currently applied (degrees: pitch forward, roll to the body's right).
    float LeanPitch() const { return m_leanPitch; }
    float LeanRoll() const { return m_leanRoll; }
    /// True while the built-in pose is easing in from an imported clip's pose.
    bool Blending() const { return m_fade < m_fadeDur; }
    /// Bones picked by hand, per engine bone (Animation/HumanoidBones.hpp's
    /// MapHumanoidBones): a bone name, "-" for none, "" for the automatic pick.
    std::string boneMap[humanoid::BoneCount];
    bool HasBoneMap() const { for (const auto& n : boneMap) if (!n.empty()) return true; return false; }

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
    /// The imported rig it drives (null until wired).
    GameObject* RigRoot() const { return m_rig && !m_rig->IsDestroyed() ? m_rig : nullptr; }
    void OnObjectsDestroyed(const std::unordered_set<const GameObject*>& dying) override {
        if (dying.count(m_animGo)) m_animGo = nullptr;   // the cached animator holder is going away
    }

    void LateUpdate(float dt) override {
        if (weight <= 0.0f || !gameObject) return;
        Scene* sc = gameObject->scene();
        if (!sc) return;
        Character* ch = gameObject->GetComponent<Character>();
        if (!ch || !ch->enabled) return;
        if (!m_init) { Wire(*sc); m_init = true; }
        if (m_map.empty()) return;
        GameObject* rig = (m_rig && !m_rig->IsDestroyed()) ? m_rig : sc->Find(m_rigRootName);
        if (!rig || !rig->transform) return;
        // The model's own clips take priority while a script holds one (play_clip /
        // the Play Clip node) or a one-shot plays (play_clip_once: attack, emote).
        ModelAnimator* ma = CachedAnimator(*sc, rig, dt);
        if (ma && ma->ClipOverridesPose()) { m_clipHadPose = true; return; }
        // The built-in pose takes over from an imported clip: ease from the pose
        // the clip left (a one-shot ending, a slot with no clip of its own).
        if (m_clipHadPose) {
            m_clipHadPose = false;
            m_fade = 0.0f;
            m_fadeDur = ma ? ma->blendTime : 0.25f;
            for (MapEntry& e : m_map)
                if (e.go && e.go->transform) { e.snapRot = e.go->transform->localRotation; e.snapPos = e.go->transform->localPosition; }
        }
        if (m_fade < m_fadeDur) m_fade += dt;
        const float fadeW = m_fade >= m_fadeDur ? 1.0f : m_fade / m_fadeDur;
        const float fadeE = fadeW * fadeW * (3.0f - 2.0f * fadeW);

        // Engine pose: per-bone Euler (local deltas, identity rest frames) —
        // accumulate down the engine hierarchy into root-frame rotations.
        std::vector<Vec3> pose = ch->CurrentPose();
        const std::vector<Character::Bone> sk = ch->Skeleton();
        int n = (int)sk.size();
        if ((int)pose.size() < n) pose.resize(n, Vec3{0, 0, 0});
        std::vector<Quat>& Re = m_rootRot;   // reused every frame (no per-frame allocation)
        Re.assign(n, Quat::Identity);
        UpdateLean(dt);
        if (m_leanPitch != 0.0f || m_leanRoll != 0.0f) {
            // Spread over the hips and spine so it reads as the whole body leaning.
            pose[HumanoidMotion::B_HIPS].x  += m_leanPitch * 0.3f;  pose[HumanoidMotion::B_HIPS].z  += m_leanRoll * 0.3f;
            pose[HumanoidMotion::B_TORSO].x += m_leanPitch * 0.7f;  pose[HumanoidMotion::B_TORSO].z += m_leanRoll * 0.7f;
        }
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
            Quat out = weight >= 1.0f ? local.Normalized() : Quat::Slerp(e.restLocal, local.Normalized(), weight);
            if (fadeW < 1.0f) {
                out = Quat::Slerp(e.snapRot, out, fadeE);
                // A clip leaves the hips wherever its walk had them: ease them back to the rest.
                if (e.bone == HumanoidMotion::B_HIPS && e.hasRestPos)
                    g->transform->localPosition = e.snapPos + (e.restPos - e.snapPos) * fadeE;
            }
            g->transform->localRotation = out;
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

        // ---- 1. name-match the imported bones (Animation/HumanoidBones.hpp) --
        const int N = Character::BoneCount();
        const humanoid::BoneMap bm = humanoid::MapHumanoidBones(rigRoot, boneMap);
        std::vector<GameObject*> node(N, nullptr);
        for (int b = 0; b < N && b < humanoid::BoneCount; ++b) node[b] = bm.bone[b];

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
        // Bind pose from the skin's inverse bind, when this bone is a joint.
        // The transform in a scene saved mid-play is the animated pose.
        Mat4 rootInv = rigRoot->transform->LocalToWorldMatrix().Inverse();
        auto bindOf = [&](GameObject* g, Quat& local, Vec3& rootPos, Vec3* localPos = nullptr) -> bool {
            if (!g || !g->transform) return false;
            for (const auto& up : s.Objects()) {
                if (!up || !up->IsSelfOrDescendantOf(rigRoot)) continue;
                auto* sm = up->GetComponent<SkinnedMesh>();
                if (!sm || !sm->transform) continue;
                Quat lr; Vec3 lp, meshPos;
                if (!sm->BindJoint(g->transform, lr, lp, meshPos)) continue;
                local = lr;
                if (localPos) *localPos = lp;
                rootPos = rootInv.MultiplyPoint(sm->transform->LocalToWorldMatrix().MultiplyPoint(meshPos));
                return true;
            }
            return false;
        };
        auto rootRelPos = [&](GameObject* g) {
            Quat lr; Vec3 rp;
            if (bindOf(g, lr, rp)) return rp;
            return rootInv.MultiplyPoint(g->transform->Position());
        };
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
            Quat bindLocal; Vec3 bindRoot, bindPos;
            if (bindOf(node[b], bindLocal, bindRoot, &bindPos)) { e.restLocal = bindLocal; e.restPos = bindPos; e.hasRestPos = true; }
            else e.restLocal = node[b]->transform->localRotation;
            // Parents saved mid-play are animated too: accumulate each bone's
            // bind local, and only fall back to the transform when it isn't a joint.
            Quat chain = Quat::Identity;
            for (Transform* t = node[b]->transform; t && t != rigRoot->transform; t = t->Parent()) {
                Quat local = t->localRotation;
                if (t->gameObject) {
                    Quat bl; Vec3 rp;
                    if (bindOf(t->gameObject, bl, rp)) local = bl;
                }
                chain = local * chain;
            }
            e.restRootRel = chain;
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
    static int ClassifyBone(const std::string& rawName, int& score) { return humanoid::ClassifyBone(rawName, score); }

private:
    struct MapEntry {
        int  bone = -1;          ///< engine bone index (Character::BoneIndex order)
        std::string node;        ///< imported bone object name
        GameObject* go = nullptr;  ///< that bone (resolved once; immune to same-named bones elsewhere)
        Quat restLocal;          ///< bind local rotation (weight blend target)
        Quat restRootRel;        ///< bind rotation relative to the rig root
        Quat align;              ///< rest-stance alignment (imported dir -> engine dir)
        Vec3 restPos{0, 0, 0};   ///< bind local position (the hips are eased back to it after a clip)
        bool hasRestPos = false;
        Quat snapRot;            ///< the pose an imported clip left, the fade-in's start
        Vec3 snapPos{0, 0, 0};
    };
    std::vector<MapEntry> m_map;
    std::vector<Quat> m_rootRot;            // per-frame scratch (root-frame bone rotations)
    GameObject* m_animGo = nullptr;         // object holding the rig's ModelAnimator (cached)
    float m_animRetry = 0.0f;               // seconds until the next search when there's none
    std::string m_rigRootName;
    GameObject* m_rig = nullptr;
    Vec3 m_rigRestPos{0, 0, 0};
    int  m_mapped = 0;
    bool m_init = false;
    bool  m_clipHadPose = false;   // last frame an imported clip owned the pose
    float m_fade = 1.0f, m_fadeDur = 0.0f;   // ease-in from that clip's pose (done when m_fade >= m_fadeDur)
    // Lean: the body's smoothed velocity and acceleration, from how the player moves.
    Vec3  m_leanPos{0, 0, 0}, m_leanVel{0, 0, 0}, m_leanAcc{0, 0, 0};
    bool  m_haveLean = false;
    float m_leanPitch = 0.0f, m_leanRoll = 0.0f;

    /// Lean into acceleration and turns, eased: a push forward from a standstill
    /// tips the body ahead, braking tips it back, a curve rolls it toward the
    /// centre. Capped at a few degrees so it never reads as a stagger.
    void UpdateLean(float dt) {
        Transform* t = gameObject ? gameObject->transform : nullptr;
        if (!t || dt <= 1e-5f) return;
        const Vec3 p = t->Position();
        if (m_haveLean) {
            Vec3 d = p - m_leanPos; d.y = 0.0f;
            Vec3 v = d.SqrMagnitude() > 4.0f ? m_leanVel : d * (1.0f / dt);   // a teleport isn't speed
            const Vec3 vPrev = m_leanVel;
            m_leanVel = m_leanVel + (v - m_leanVel) * (1.0f - std::exp(-dt / 0.08f));
            const Vec3 a = (m_leanVel - vPrev) * (1.0f / dt);
            m_leanAcc = m_leanAcc + (a - m_leanAcc) * (1.0f - std::exp(-dt / 0.15f));
        }
        m_leanPos = p; m_haveLean = true;
        const Quat q = t->Rotation();
        const Vec3 fwd = q * Vec3{0.0f, 0.0f, -1.0f}, right = q * Vec3{1.0f, 0.0f, 0.0f};   // controllers walk along local -Z
        const float along = Vec3::Dot(m_leanAcc, fwd), across = Vec3::Dot(m_leanAcc, right);
        auto cap = [](float v, float m) { return v > m ? m : (v < -m ? -m : v); };
        // Moving only: a body standing still doesn't tip because something shoved the capsule.
        const float moving = std::fmin(1.0f, std::sqrt(m_leanVel.x * m_leanVel.x + m_leanVel.z * m_leanVel.z) / 0.8f);
        const float k = leanScale > 0.0f ? leanScale : 0.0f;
        const float wantPitch = cap(along * 0.9f, 5.0f) * k;
        const float wantRoll  = cap(across * 1.2f, 6.0f) * k * moving;
        const float e = 1.0f - std::exp(-dt / 0.1f);
        m_leanPitch += (wantPitch - m_leanPitch) * e;
        m_leanRoll  += (wantRoll  - m_leanRoll)  * e;
    }

    /// Accumulated rotation of the parent chain from (exclusive) rigRoot down
    /// to `t` (inclusive) using the CURRENT local rotations.
    static Quat ChainRotation(Transform* t, GameObject* rigRoot) {
        Quat r = Quat::Identity;
        for (; t && (!rigRoot || t != rigRoot->transform); t = t->Parent())
            r = t->localRotation * r;
        return r;
    }

    /// FindAnimator, cached: the holder object is remembered, and when there is no
    /// animator the scene-wide search runs at most once a second, not every frame.
    ModelAnimator* CachedAnimator(Scene& s, GameObject* rig, float dt) {
        if (m_animGo && m_animGo->IsSelfOrDescendantOf(rig))
            if (ModelAnimator* ma = m_animGo->GetComponent<ModelAnimator>()) return ma;
        m_animGo = nullptr;
        if ((m_animRetry -= dt) > 0.0f) return nullptr;
        m_animRetry = 1.0f;
        ModelAnimator* ma = FindAnimator(s, rig);
        if (ma) m_animGo = ma->gameObject;
        return ma;
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
