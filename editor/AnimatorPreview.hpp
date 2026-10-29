#pragma once
// Animator live preview. Owned here, not by the Animation window: that window
// closing no longer stops it. One preview at a time — a Character state or
// clip, or a ModelAnimator clip — shown in the Scene view in edit and in Play.
// Stopping restores the pose that was on screen when it started. Nothing here
// marks the scene dirty or writes the state's speed / loop back.
#include "imgui.h"
#include "okay/Animation/HumanoidLibrary.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/BlendShapes.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/HumanoidRetarget.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/SkinnedMesh.hpp"
#include "okay/Scene/Scene.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace okay {
namespace editor {
namespace apreview {

enum class Kind { None, State, Clip, Model };

struct BoneSnap { GameObject* go; Vec3 p; Quat r; Vec3 s; };
struct ShapeSnap { BlendShapes* bs; std::vector<float> w; };

struct Preview {
    Kind kind = Kind::None;
    bool playing = true;
    bool loop = true;
    bool startedInPlay = false;
    bool snapped = false;       // bones are captured on the first tick, after other previews restore
    float t = 0.0f;
    float speed = 1.0f;     // preview rate; defaults to the state's speed
    float clipRate = 1.0f;  // the clip's own speed
    float length = 0.0f;
    int keepAnim = 1;
    GameObject* target = nullptr;
    std::string label;                  // state name, or the clip name
    std::string clip;
    std::vector<std::string> motions;   // blend-tree choices; empty otherwise
    // A transition preview: `clip` plays from `t0`; at `mixStart` `mixClip`
    // fades in over `mixDur` seconds, starting `mixOffset` into itself. Empty
    // mixClip = an ordinary one-clip preview.
    std::string mixClip;
    float t0 = 0.0f;
    float mixStart = 0.0f, mixDur = 0.0f, mixOffset = 0.0f;
    bool mixLoop = true;                // the second clip loops
    std::vector<BoneSnap> bones;
    std::vector<ShapeSnap> shapes;
};

inline Preview& Cur() {
    static Preview p;
    return p;
}

inline Character* ActiveCharacter() {
    Preview& p = Cur();
    if (!p.target || p.kind == Kind::None || p.kind == Kind::Model) return nullptr;
    return p.target->GetComponent<Character>();
}

inline GameObject* FindAlive(Scene& sc, GameObject* go) {
    if (!go) return nullptr;
    for (const auto& up : sc.Objects()) if (up.get() == go) return go;
    return nullptr;
}

inline bool InFamily(GameObject* sel, GameObject* target) {
    if (!sel || !target) return false;
    if (sel == target) return true;
    if (sel->IsSelfOrDescendantOf(target)) return true;
    // The graph is often opened with the player selected while the machine
    // sits on the model underneath it.
    if (target->IsSelfOrDescendantOf(sel)) return true;
    return false;
}

inline const AnimClip* BodyClip(Character* ch, const std::string& name) {
    if (!ch || name.empty()) return nullptr;
    if (const AnimClip* c = ch->FindClip(name)) return c;
    return HumanoidLibrary::Find(name);
}

inline std::string MiddleMotion(const AnimStateMachine::State& st) {
    if (!st.blendTree || st.motions.empty()) return st.clip;
    std::vector<float> along;
    along.reserve(st.motions.size());
    for (const auto& m : st.motions) along.push_back(st.blendDims == 2 ? m.posY : m.threshold);
    std::nth_element(along.begin(), along.begin() + (int)along.size() / 2, along.end());
    const float mid = along[along.size() / 2];
    const AnimStateMachine::Motion* best = nullptr;
    float bd = 1e30f;
    for (const auto& m : st.motions) {
        const float dx = st.blendDims == 2 ? m.threshold : 0.0f;
        const float dy = (st.blendDims == 2 ? m.posY : m.threshold) - mid;
        const float d = dx * dx + dy * dy;
        if (d < bd) { bd = d; best = &m; }
    }
    return best ? best->clip : st.clip;
}

inline void ApplyNodeClip(const AnimationClip& clip, Transform* tr, float time, bool loop) {
    const float len = clip.Length();
    float t = time;
    if (loop && len > 0.0f) t = std::fmod(std::fmod(time, len) + len, len);
    else if (len > 0.0f) t = std::min(std::max(time, 0.0f), len);
    Vec3 pos = tr->localPosition, scl = tr->localScale;
    bool f; float v;
    v = clip.Evaluate("position.x", t, f); if (f) pos.x = v;
    v = clip.Evaluate("position.y", t, f); if (f) pos.y = v;
    v = clip.Evaluate("position.z", t, f); if (f) pos.z = v;
    v = clip.Evaluate("scale.x", t, f);    if (f) scl.x = v;
    v = clip.Evaluate("scale.y", t, f);    if (f) scl.y = v;
    v = clip.Evaluate("scale.z", t, f);    if (f) scl.z = v;
    tr->localPosition = pos;
    tr->localScale = scl;
    bool qx, qy, qz, qw;
    float vqx = clip.Evaluate("rotation.qx", t, qx);
    float vqy = clip.Evaluate("rotation.qy", t, qy);
    float vqz = clip.Evaluate("rotation.qz", t, qz);
    float vqw = clip.Evaluate("rotation.qw", t, qw);
    if (qx || qy || qz || qw) {
        float ln = std::sqrt(vqx * vqx + vqy * vqy + vqz * vqz + vqw * vqw);
        if (ln < 1e-8f) { vqw = 1.0f; ln = 1.0f; }
        tr->localRotation = Quat{vqx / ln, vqy / ln, vqz / ln, vqw / ln};
    } else {
        bool fx, fy, fz;
        float rx = clip.Evaluate("rotation.x", t, fx);
        float ry = clip.Evaluate("rotation.y", t, fy);
        float rz = clip.Evaluate("rotation.z", t, fz);
        if (fx || fy || fz) tr->localRotation = Quat::Euler(fx ? rx : 0.0f, fy ? ry : 0.0f, fz ? rz : 0.0f);
    }
}

inline void ReskinUnder(Scene& sc, GameObject* root) {
    if (!root) return;
    for (const auto& up : sc.Objects()) {
        if (!up || !up->IsSelfOrDescendantOf(root)) continue;
        if (auto* sm = up->GetComponent<SkinnedMesh>()) { sm->ResolveJoints(); sm->Skin(); }
    }
}

inline void RestoreBones(Scene& sc, Preview& p) {
    std::vector<GameObject*> alive;
    alive.reserve(sc.Objects().size());
    for (const auto& up : sc.Objects()) if (up) alive.push_back(up.get());
    auto live = [&](GameObject* g) {
        return g && std::find(alive.begin(), alive.end(), g) != alive.end();
    };
    for (const BoneSnap& b : p.bones)
        if (live(b.go) && b.go->transform) {
            b.go->transform->localPosition = b.p;
            b.go->transform->localRotation = b.r;
            b.go->transform->localScale = b.s;
        }
    for (const ShapeSnap& s : p.shapes)
        if (s.bs && s.bs->gameObject && live(s.bs->gameObject)) {
            for (std::size_t i = 0; i < s.w.size() && i < s.bs->shapes.size(); ++i)
                s.bs->shapes[i].weight = s.w[i];
            s.bs->Apply();
        }
}

inline void Clear(Preview& p) {
    p = Preview{};
}

// Put the captured pose back. Safe when the target is already gone.
inline void Stop(Scene& sc) {
    Preview& p = Cur();
    if (p.kind == Kind::None) return;
    GameObject* go = FindAlive(sc, p.target);
    if (go) {
        if (p.kind != Kind::Model) {
            if (Character* ch = go->GetComponent<Character>()) {
                // Edit mode: the body should show the anim it had. Play mode:
                // the running machine owns `anim` and its current state; writing
                // the old value back would send it somewhere else.
                if (!p.startedInPlay) ch->anim = p.keepAnim;
                ch->StopPreview();
                if (!ch->driveExternal) {
                    if (!ch->separateParts) ch->Apply();
                    ch->EditorPreviewTick();
                }
            }
        }
        RestoreBones(sc, p);
        ReskinUnder(sc, go);
    }
    Clear(p);
}

inline void CaptureShapes(GameObject* g, std::vector<ShapeSnap>& out) {
    if (!g) return;
    auto take = [&](BlendShapes* bs) {
        if (!bs) return;
        ShapeSnap s; s.bs = bs;
        for (const auto& sh : bs->shapes) s.w.push_back(sh.weight);
        out.push_back(std::move(s));
    };
    take(g->GetComponent<BlendShapes>());
    if (g->transform)
        for (Transform* c : g->transform->Children())
            if (c && c->gameObject) take(c->gameObject->GetComponent<BlendShapes>());
}

inline void SnapshotCharacter(Scene& sc, Character* ch, Preview& p) {
    p.bones.clear();
    p.shapes.clear();
    if (!ch->driveExternal || !ch->gameObject) return;
    for (const auto& up : sc.Objects()) {
        GameObject* g = up.get();
        if (!g || g == ch->gameObject || !g->transform) continue;
        if (!g->IsSelfOrDescendantOf(ch->gameObject)) continue;
        p.bones.push_back({g, g->transform->localPosition, g->transform->localRotation, g->transform->localScale});
    }
}

inline void SnapshotModel(Scene& sc, ModelAnimator* ma, Preview& p) {
    p.bones.clear();
    p.shapes.clear();
    GameObject* root = ma->gameObject;
    if (!root) return;
    std::vector<GameObject*> seen;
    auto take = [&](GameObject* g) {
        if (!g || !g->transform) return;
        if (std::find(seen.begin(), seen.end(), g) != seen.end()) return;
        seen.push_back(g);
        p.bones.push_back({g, g->transform->localPosition, g->transform->localRotation, g->transform->localScale});
        CaptureShapes(g, p.shapes);
    };
    for (const auto& cl : ma->clips)
        for (const auto& nc : cl.nodes) {
            for (const auto& up : sc.Objects()) {
                GameObject* g = up.get();
                if (!g || !g->IsSelfOrDescendantOf(root) || g->name != nc.node) continue;
                take(g);
                break;
            }
        }
}

inline void AdoptClip(Character* ch, const std::string& name, Preview& p) {
    p.clip = name;
    p.length = 0.0f;
    p.clipRate = 1.0f;
    if (const AnimClip* c = BodyClip(ch, name)) {
        p.clipRate = c->speed > 1e-4f ? c->speed : 1.0f;
        if (p.loop) p.length = c->Duration();
        else p.length = c->keys.empty() ? 0.0f : c->keys.back().time;
    }
}

inline void Begin(Scene& sc, GameObject* target, Kind kind, const std::string& label, bool loop, float speed, bool inPlay) {
    Stop(sc);
    Preview& p = Cur();
    p.kind = kind;
    p.target = target;
    p.label = label;
    p.loop = loop;
    p.speed = speed > 0.0f ? speed : 1.0f;
    p.playing = true;
    p.t = 0.0f;
    p.startedInPlay = inPlay;
    p.snapped = false;
    p.motions.clear();
    if (Character* ch = target ? target->GetComponent<Character>() : nullptr)
        p.keepAnim = ch->anim;
    (void)sc;
}

inline bool StartCharacterClip(Scene& sc, Character* ch, const std::string& clip, bool inPlay) {
    const AnimClip* c = BodyClip(ch, clip);
    if (!ch || !ch->gameObject || !c) return false;
    Begin(sc, ch->gameObject, Kind::Clip, clip, c->loop, 1.0f, inPlay);
    AdoptClip(ch, clip, Cur());
    return true;
}

inline bool StartCharacterState(Scene& sc, Character* ch, const AnimStateMachine::State& st, bool inPlay) {
    if (!ch || !ch->gameObject) return false;
    std::string clip = MiddleMotion(st);
    if (!BodyClip(ch, clip) && !st.fallbackClip.empty() && BodyClip(ch, st.fallbackClip))
        clip = st.fallbackClip;
    if (!BodyClip(ch, clip)) return false;
    const bool loop = st.blendTree || st.loop;
    Begin(sc, ch->gameObject, Kind::State, st.name, loop, st.speed, inPlay);
    Preview& p = Cur();
    if (st.blendTree)
        for (const auto& m : st.motions) if (!m.clip.empty()) p.motions.push_back(m.clip);
    AdoptClip(ch, clip, p);
    return true;
}

inline bool StartModel(Scene& sc, ModelAnimator* ma, const std::string& clip, float speed, bool loop, bool inPlay) {
    if (!ma || !ma->gameObject) return false;
    const int idx = ma->FindClip(clip);
    if (idx < 0) return false;
    Begin(sc, ma->gameObject, Kind::Model, clip, loop, speed, inPlay);
    Preview& p = Cur();
    p.clip = clip;
    p.length = ma->ClipLength(idx);
    p.clipRate = 1.0f;
    (void)sc;
    return true;
}

inline bool StartModelState(Scene& sc, ModelAnimator* ma, const AnimStateMachine::State& st, bool inPlay) {
    if (!ma) return false;
    std::string clip = MiddleMotion(st);
    if (ma->FindClip(clip) < 0 && !st.fallbackClip.empty() && ma->FindClip(st.fallbackClip) >= 0)
        clip = st.fallbackClip;
    const bool loop = st.blendTree || st.loop;
    if (!StartModel(sc, ma, clip, st.speed, loop, inPlay)) return false;
    Preview& p = Cur();
    p.kind = Kind::State;
    p.label = st.name;
    if (st.blendTree)
        for (const auto& m : st.motions) if (!m.clip.empty()) p.motions.push_back(m.clip);
    return true;
}

/// Preview a transition on `target` (a Character body or an imported model):
/// `from` plays, and at `startAt` seconds into it `to` fades in over `blend`
/// seconds, starting `offset` (normalized) into itself. The preview loops over
/// a window around the blend. False when either clip is not there.
inline bool StartTransition(Scene& sc, GameObject* target, const std::string& label, const std::string& from,
                            const std::string& to, bool loopFrom, bool loopTo, float startAt, float blend,
                            float offset, float speed, bool inPlay) {
    if (!target) return false;
    Character* ch = target->GetComponent<Character>();
    ModelAnimator* ma = target->GetComponent<ModelAnimator>();
    float lenA = 0.0f, lenB = 0.0f;
    if (ma && !ch) {
        const int a = ma->FindClip(from), b = ma->FindClip(to);
        if (a < 0 || b < 0) return false;
        lenA = ma->ClipLength(a); lenB = ma->ClipLength(b);
        Begin(sc, target, Kind::Model, label, true, speed, inPlay);
    } else if (ch) {
        const AnimClip* a = BodyClip(ch, from);
        const AnimClip* b = BodyClip(ch, to);
        if (!a || !b) return false;
        lenA = a->Duration(); lenB = b->Duration();
        Begin(sc, target, Kind::State, label, true, speed, inPlay);
        AdoptClip(ch, from, Cur());
    } else return false;
    Preview& p = Cur();
    p.clip = from;
    p.mixClip = to;
    p.mixLoop = loopTo;
    p.mixDur = std::max(blend, 0.0f);
    p.mixOffset = std::min(std::max(offset, 0.0f), 0.999f) * lenB;
    // The window: a lead-in on the first clip, the blend, and a tail on the second.
    const float lead = std::min(0.6f, lenA * 0.5f), tail = std::min(0.6f, std::max(lenB, 0.1f));
    p.mixStart = loopFrom || lenA <= 0.0f ? startAt : std::min(startAt, lenA);
    p.t0 = std::max(0.0f, p.mixStart - lead);
    p.length = p.mixStart + p.mixDur + tail;
    p.t = p.t0;
    p.loop = true;
    return true;
}
inline bool IsTransition(GameObject* go, const std::string& label) {
    const Preview& p = Cur();
    return !p.mixClip.empty() && p.target == go && p.label == label;
}

inline void UseMotion(const std::string& clip) {
    Preview& p = Cur();
    if (p.kind == Kind::None || clip.empty() || clip == p.clip) return;
    p.clip = clip;
    p.t = 0.0f;
    if (p.kind == Kind::Model) {
        if (ModelAnimator* ma = p.target ? p.target->GetComponent<ModelAnimator>() : nullptr) {
            const int idx = ma->FindClip(clip);
            p.length = idx >= 0 ? ma->ClipLength(idx) : 0.0f;
        }
        return;
    }
    if (Character* ch = p.target ? p.target->GetComponent<Character>() : nullptr) AdoptClip(ch, clip, p);
}

inline bool IsState(GameObject* go, const std::string& state) {
    const Preview& p = Cur();
    return p.kind == Kind::State && p.target == go && p.label == state;
}
inline bool IsClip(Character* ch, const std::string& clip) {
    const Preview& p = Cur();
    return p.kind == Kind::Clip && ch && p.target == ch->gameObject && p.clip == clip;
}
inline bool IsModelClip(ModelAnimator* ma, const std::string& clip) {
    const Preview& p = Cur();
    return p.kind == Kind::Model && ma && p.target == ma->gameObject && p.clip == clip;
}

inline void NoteSelection(Scene& sc, GameObject* sel) {
    Preview& p = Cur();
    if (p.kind == Kind::None) return;
    if (!InFamily(sel, p.target)) Stop(sc);
}

// The preview poses through the Model Animator: a Model preview, or a state
// preview on an imported model that has no Character of its own.
inline bool PosesModel(GameObject* go, const Preview& p) {
    if (p.kind == Kind::Model) return true;
    return go && go->GetComponent<ModelAnimator>() && !go->GetComponent<Character>();
}

/// How far a transition preview has faded into its second clip at `t` (0..1).
inline float MixWeight(const Preview& p, float t) {
    if (p.mixClip.empty()) return 0.0f;
    if (p.mixDur <= 1e-4f) return t >= p.mixStart ? 1.0f : 0.0f;
    return std::min(std::max((t - p.mixStart) / p.mixDur, 0.0f), 1.0f);
}
/// Where the second clip is at preview time `t`.
inline float MixTime(const Preview& p, float t, float length) {
    float s = p.mixOffset + std::max(0.0f, t - p.mixStart);
    if (length > 1e-4f) s = p.mixLoop ? std::fmod(s, length) : std::min(s, length);
    return s;
}

inline void PoseCharacter(Scene& sc, Character* ch, const Preview& p) {
    const AnimClip* pc = BodyClip(ch, p.clip);
    if (!pc) return;
    float sample = p.t;
    if (!p.loop) {
        const float hold = pc->keys.empty() ? 0.0f : pc->keys.back().time;
        if (sample > hold) sample = hold;
    }
    std::vector<Vec3> pose = pc->Sample(sample);
    Vec3 root = pc->SampleRoot(sample);
    const float u = MixWeight(p, p.t);
    if (u > 0.0f) {
        if (const AnimClip* mc = BodyClip(ch, p.mixClip)) {
            const float lenB = p.mixLoop ? mc->Duration() : (mc->keys.empty() ? 0.0f : mc->keys.back().time);
            const float tb = MixTime(p, p.t, lenB);
            const std::vector<Vec3> b = mc->Sample(tb);
            for (std::size_t i = 0; i < pose.size() && i < b.size(); ++i) pose[i] = AnimClip::BlendEuler(pose[i], b[i], u);
            root = root + (mc->SampleRoot(tb) - root) * u;
        }
    }
    const int keep = ch->anim;
    ch->PreviewPose(pose, root * ch->height);
    if (ch->driveExternal && ch->gameObject) {
        if (auto* rt = ch->gameObject->GetComponent<HumanoidRetarget>()) rt->LateUpdate(0.0f);
        ReskinUnder(sc, ch->gameObject);
        ch->anim = keep;
    } else {
        ch->anim = keep;
        // Pose the body directly. The Animation window's draft flag stays
        // alone, so the two previews don't fight.
        ch->EditorPreviewTick();
    }
}

// Pose every node of `clip` at `t`, then blend `mix` in at `u` on the union of
// their nodes (position and scale lerped, rotation slerped). Blend shapes
// follow the first clip.
inline void PoseModelMixed(Scene& sc, ModelAnimator* ma, const ModelAnimator::Clip& clip, float t, bool loop,
                           const ModelAnimator::Clip& mix, float tb, bool loopB, float u) {
    struct Node { Transform* tr; Vec3 p0, s0; Quat r0; Vec3 pa, sa; Quat ra; };
    std::vector<Node> nodes;
    auto find = [&](const std::string& name) -> Transform* {
        for (const auto& up : sc.Objects()) {
            GameObject* g = up.get();
            if (!g || g->name != name || !g->IsSelfOrDescendantOf(ma->gameObject)) continue;
            return g->transform;
        }
        return nullptr;
    };
    auto slot = [&](Transform* tr) -> Node& {
        for (Node& n : nodes) if (n.tr == tr) return n;
        nodes.push_back({tr, tr->localPosition, tr->localScale, tr->localRotation,
                         tr->localPosition, tr->localScale, tr->localRotation});
        return nodes.back();
    };
    for (const auto& nc : clip.nodes) {
        Transform* tr = find(nc.node);
        if (!tr) continue;
        Node& n = slot(tr);
        ApplyNodeClip(nc.clip, tr, t, loop);
        n.pa = tr->localPosition; n.sa = tr->localScale; n.ra = tr->localRotation;
        tr->localPosition = n.p0; tr->localScale = n.s0; tr->localRotation = n.r0;
    }
    for (const auto& nc : mix.nodes) {
        Transform* tr = find(nc.node);
        if (!tr) continue;
        Node& n = slot(tr);
        ApplyNodeClip(nc.clip, tr, tb, loopB);
        n.p0 = tr->localPosition; n.s0 = tr->localScale; n.r0 = tr->localRotation;   // B's pose (rest when A alone drove it)
    }
    for (const Node& n : nodes) {
        n.tr->localPosition = n.pa + (n.p0 - n.pa) * u;
        n.tr->localScale = n.sa + (n.s0 - n.sa) * u;
        n.tr->localRotation = Quat::Slerp(n.ra, n.r0, u);
    }
}

inline void PoseModel(Scene& sc, ModelAnimator* ma, const Preview& p) {
    const int idx = ma->FindClip(p.clip);
    if (idx < 0 || !ma->gameObject) return;
    RestoreBones(sc, Cur());
    const ModelAnimator::Clip& clip = ma->clips[idx];
    const float u = MixWeight(p, p.t);
    const int mixIdx = u > 0.0f ? ma->FindClip(p.mixClip) : -1;
    if (mixIdx >= 0) {
        const float tb = MixTime(p, p.t, ma->ClipLength(mixIdx));
        PoseModelMixed(sc, ma, clip, p.t, p.loop, ma->clips[mixIdx], tb, p.mixLoop, u);
        ReskinUnder(sc, ma->gameObject);
        return;
    }
    for (const auto& nc : clip.nodes) {
        GameObject* node = nullptr;
        for (const auto& up : sc.Objects()) {
            GameObject* g = up.get();
            if (!g || g->name != nc.node || !g->IsSelfOrDescendantOf(ma->gameObject)) continue;
            node = g; break;
        }
        if (!node || !node->transform) continue;
        ApplyNodeClip(nc.clip, node->transform, p.t, p.loop);
        const auto& chan = nc.clip.Chan();
        if (chan.weights.empty()) continue;
        std::vector<BlendShapes*> targets;
        if (auto* b0 = node->GetComponent<BlendShapes>()) targets.push_back(b0);
        else if (node->transform)
            for (Transform* c : node->transform->Children())
                if (c && c->gameObject)
                    if (auto* b1 = c->gameObject->GetComponent<BlendShapes>()) targets.push_back(b1);
        for (BlendShapes* bs : targets) {
            for (const auto& w : chan.weights) bs->SetWeight(w.first, w.second->Evaluate(p.t));
            bs->Apply();
        }
    }
    ReskinUnder(sc, ma->gameObject);
}

inline void Tick(Scene& sc, float dt) {
    Preview& p = Cur();
    if (p.kind == Kind::None) return;
    GameObject* go = FindAlive(sc, p.target);
    if (!go) { Clear(p); return; }
    if (!p.snapped) {
        // Other previews restore in the editor tick before this runs, so the
        // snapshot is the pose that was really on screen.
        if (PosesModel(go, p)) {
            if (ModelAnimator* ma = go->GetComponent<ModelAnimator>()) SnapshotModel(sc, ma, p);
        } else if (Character* ch = go->GetComponent<Character>()) SnapshotCharacter(sc, ch, p);
        p.snapped = true;
    }
    if (p.playing) {
        p.t += dt * p.speed * p.clipRate;
        if (p.loop && p.length > 1e-4f) {
            const float span = std::max(p.length - p.t0, 1e-4f);
            if (p.t >= p.length || p.t < p.t0) {
                p.t = p.t0 + std::fmod(p.t - p.t0, span);
                if (p.t < p.t0) p.t += span;
            }
        } else if (p.length > 0.0f && p.t > p.length) {
            p.t = p.length;
            p.playing = false;
        }
    }
    if (PosesModel(go, p)) {
        if (ModelAnimator* ma = go->GetComponent<ModelAnimator>()) PoseModel(sc, ma, p);
    } else if (Character* ch = go->GetComponent<Character>()) {
        PoseCharacter(sc, ch, p);
    }
}

inline void DrawTransport(const char* id) {
    Preview& p = Cur();
    if (p.kind == Kind::None) return;
    ImGui::PushID(id);
    if (ImGui::SmallButton(p.playing ? "Pause##ap" : "Play##ap")) {
        if (!p.playing && !p.loop && p.length > 0.0f && p.t >= p.length - 1e-4f) p.t = 0.0f;
        p.playing = !p.playing;
    }
    ImGui::SameLine();
    ImGui::Text("%.2f / %.2fs", p.t, p.length);
    const float tmax = p.length > 0.05f ? p.length : 1.0f;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##apt", &p.t, 0.0f, tmax, "time %.2fs")) p.playing = false;
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderFloat("Speed##aps", &p.speed, 0.0f, 3.0f, "%.2f");
    ImGui::SameLine();
    if (ImGui::Checkbox("Loop##apl", &p.loop) && p.kind != Kind::Model && p.target)
        if (Character* ch = p.target->GetComponent<Character>()) AdoptClip(ch, p.clip, p);
    if (!p.motions.empty()) {
        const char* cur = p.clip.empty() ? "(motion)" : p.clip.c_str();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("Motion##apm", cur)) {
            for (const std::string& m : p.motions)
                if (ImGui::Selectable(m.c_str(), m == p.clip)) UseMotion(m);
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("One motion of this blend tree. Mixing several at once\n"
                              "belongs to the running Animator, not the preview.");
    }
    ImGui::PopID();
}

inline bool ToggleCharacterState(Scene& sc, Character* ch, const AnimStateMachine::State& st, bool inPlay, const char* id) {
    ImGui::PushID(id);
    const bool on = ch && IsState(ch->gameObject, st.name);
    if (ImGui::SmallButton(on ? "Stop##apst" : "Preview##apst")) {
        if (on) Stop(sc);
        else StartCharacterState(sc, ch, st, inPlay);
    }
    ImGui::PopID();
    return ch && IsState(ch->gameObject, st.name);
}

inline bool ToggleCharacterClip(Scene& sc, Character* ch, const std::string& clip, bool inPlay) {
    const bool on = IsClip(ch, clip);
    if (ImGui::SmallButton(on ? "Stop##apcl" : "Preview##apcl")) {
        if (on) Stop(sc);
        else StartCharacterClip(sc, ch, clip, inPlay);
    }
    return IsClip(ch, clip);
}

inline bool ToggleModelClip(Scene& sc, ModelAnimator* ma, const std::string& clip, float speed, bool loop, bool inPlay) {
    const bool on = IsModelClip(ma, clip);
    if (ImGui::SmallButton(on ? "Stop##apma" : "Preview##apma")) {
        if (on) Stop(sc);
        else StartModel(sc, ma, clip, speed, loop, inPlay);
    }
    return IsModelClip(ma, clip);
}

inline bool ToggleModelState(Scene& sc, ModelAnimator* ma, const AnimStateMachine::State& st, bool inPlay) {
    const bool on = ma && IsState(ma->gameObject, st.name);
    if (ImGui::SmallButton(on ? "Stop##apmst" : "Preview##apmst")) {
        if (on) Stop(sc);
        else StartModelState(sc, ma, st, inPlay);
    }
    return ma && IsState(ma->gameObject, st.name);
}

} // namespace apreview
} // namespace editor
} // namespace okay
