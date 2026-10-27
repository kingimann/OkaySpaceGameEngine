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
    const std::string clip = MiddleMotion(st);
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
    const std::string clip = MiddleMotion(st);
    const bool loop = st.blendTree || st.loop;
    if (!StartModel(sc, ma, clip, st.speed, loop, inPlay)) return false;
    Preview& p = Cur();
    p.kind = Kind::State;
    p.label = st.name;
    if (st.blendTree)
        for (const auto& m : st.motions) if (!m.clip.empty()) p.motions.push_back(m.clip);
    return true;
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

inline void PoseCharacter(Scene& sc, Character* ch, const Preview& p) {
    const AnimClip* pc = BodyClip(ch, p.clip);
    if (!pc) return;
    float sample = p.t;
    if (!p.loop) {
        const float hold = pc->keys.empty() ? 0.0f : pc->keys.back().time;
        if (sample > hold) sample = hold;
    }
    const int keep = ch->anim;
    ch->PreviewPose(pc->Sample(sample), pc->SampleRoot(sample) * ch->height);
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

inline void PoseModel(Scene& sc, ModelAnimator* ma, const Preview& p) {
    const int idx = ma->FindClip(p.clip);
    if (idx < 0 || !ma->gameObject) return;
    RestoreBones(sc, Cur());
    const ModelAnimator::Clip& clip = ma->clips[idx];
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
        if (p.kind == Kind::Model) {
            if (ModelAnimator* ma = go->GetComponent<ModelAnimator>()) SnapshotModel(sc, ma, p);
        } else if (Character* ch = go->GetComponent<Character>()) SnapshotCharacter(sc, ch, p);
        p.snapped = true;
    }
    if (p.playing) {
        p.t += dt * p.speed * p.clipRate;
        if (p.loop && p.length > 1e-4f) {
            p.t = std::fmod(p.t, p.length);
            if (p.t < 0.0f) p.t += p.length;
        } else if (p.length > 0.0f && p.t > p.length) {
            p.t = p.length;
            p.playing = false;
        }
    }
    if (p.kind == Kind::Model) {
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
