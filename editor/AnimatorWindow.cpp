// The Animator window: the state-machine graph editor for AnimStateMachine.
// States are nodes on a canvas, transitions arrows between them; the side
// panel edits the selection, the parameters and the layers. Play mode shows
// the running state and the transitions that fire.
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "AnimatorWindow.hpp"
#include "AnimatorPreview.hpp"          // live preview of a state on the selected character
#include "ConsolePanel.hpp"             // ConsoleLog
#include "EditorWidgets.hpp"            // AccentCol
#include "MainShared.hpp"               // AddAnimationsFromFilesUI
#include "okay/Animation/ClipGraph.hpp"       // an Animator graph from an imported model's clips
#include "okay/Animation/HumanoidLibrary.hpp" // built-in clip names, the default humanoid graph
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Render/ModelImport.hpp"        // AutoMapLocomotion
#include "imgui.h"
#include "tinyfiledialogs.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::editor;

namespace oked {
namespace {

// Lowercase a copy of a string (for case-insensitive compares).
std::string Lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// ---------------------------------------------------------------------------
// Animator window — a visual node-graph editor for the AnimStateMachine
// drag state nodes, draw transitions between them, edit each
// transition's condition in the side panel, and watch the ACTIVE state light
// up during Play. Node positions save with the scene (animsmpos record).
// ---------------------------------------------------------------------------
// A transition's conditions in a few characters, for its arrow in the graph:
// "speed > 0.1 & grounded @75%".
std::string TransitionSummary(const AnimStateMachine::Transition& tr) {
    using C = AnimStateMachine::Cond;
    auto one = [](C c, const std::string& p, float v) -> std::string {
        char b[96];
        const char* pn = p.empty() ? "?" : p.c_str();
        switch (c) {
            case C::OnClipEnd:    return "end";
            case C::FloatGreater: std::snprintf(b, sizeof(b), "%s > %g", pn, v); return b;
            case C::FloatLess:    std::snprintf(b, sizeof(b), "%s < %g", pn, v); return b;
            case C::BoolTrue:     return pn;
            case C::BoolFalse:    return std::string("not ") + pn;
            case C::Trigger:      return std::string(pn) + " (trigger)";
            case C::Always:       return "always";
            case C::IntEquals:    std::snprintf(b, sizeof(b), "%s = %d", pn, (int)std::lround(v)); return b;
            case C::IntNotEquals: std::snprintf(b, sizeof(b), "%s != %d", pn, (int)std::lround(v)); return b;
        }
        return "?";
    };
    std::string out = one(tr.cond, tr.param, tr.value);
    for (const auto& c : tr.extra) out += " & " + one(c.cond, c.param, c.value);
    if (tr.hasExitTime) out += " @" + std::to_string((int)std::lround(tr.exitTime * 100.0f)) + "%";
    return out;
}


} // namespace

void DrawAnimatorGraph(EditorState& ed) {
    if (!g_showAnimatorGraph) return;
    if (std::getenv("OKAY_UI_ANIMGRAPH")) {
        ImGui::SetNextWindowPos(ImVec2(24, 48), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(1280, 780), ImGuiCond_Always);
    } else {
        ImGui::SetNextWindowSize(ImVec2(860, 520), ImGuiCond_FirstUseEver);
    }
    if (!ImGui::Begin("Animator", &g_showAnimatorGraph)) { ImGui::End(); return; }

    // Resolve the machine from the selection: self, then ancestors, then
    // descendants (a player selected with the machine on the model root).
    AnimStateMachine* sm = nullptr; GameObject* smObj = nullptr;
    if (GameObject* go = ed.selected()) {
        sm = go->GetComponent<AnimStateMachine>(); smObj = go;
        if (!sm && go->transform)
            for (Transform* t = go->transform->Parent(); t && !sm; t = t->Parent())
                if (t->gameObject) { sm = t->gameObject->GetComponent<AnimStateMachine>(); smObj = t->gameObject; }
        if (!sm && go->transform)
            for (const auto& up : ed.scene().Objects())
                if (up && up->IsSelfOrDescendantOf(go))
                    if (auto* m = up->GetComponent<AnimStateMachine>()) { sm = m; smObj = up.get(); break; }
    }
    if (!sm) {
        // A character without one yet: the built-in body gets the default humanoid
        // graph, an imported model a graph from its own clips.
        GameObject* sel = ed.selected();
        GameObject* modelGo = nullptr; Character* bodyGo = nullptr;
        if (sel)
            for (const auto& up : ed.scene().Objects()) {
                if (!up || !up->IsSelfOrDescendantOf(sel)) continue;
                if (auto* m = up->GetComponent<ModelAnimator>(); m && !m->clips.empty() && !modelGo) modelGo = up.get();
                if (auto* c = up->GetComponent<Character>(); c && !bodyGo && !up->GetComponent<ModelAnimator>()) bodyGo = c;
            }
        if (bodyGo || modelGo) {
            ImGui::TextDisabled("'%s' has no Animator yet.", sel->name.c_str());
            if (ImGui::Button(bodyGo ? "Create Animator (default humanoid graph)##agnew" : "Create Animator from this model's clips##agnew")) {
                ed.PushUndo();
                if (bodyGo) HumanoidLibrary::EnsureAnimator(bodyGo->gameObject);
                else {
                    okay::AutoMapLocomotion(modelGo->GetComponent<ModelAnimator>());
                    ClipGraph::Ensure(modelGo);
                }
                ed.dirty = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Every state and transition then shows here to edit and preview.\n"
                                  "It takes the parameters the player controllers send (speed, grounded, crouch, prone, jump).");
            ImGui::End(); return;
        }
        ImGui::TextDisabled("Select an object with an Animator (Add Component > Animation).");
        ImGui::TextDisabled("States name a clip (the Model Animator's, or the Character's); transitions switch\nbetween them when a condition passes (clip end, float compare, bool, trigger).");
        ImGui::End(); return;
    }
    ModelAnimator* ma = smObj ? smObj->GetComponent<ModelAnimator>() : nullptr;
    Character* body = (smObj && !ma) ? smObj->GetComponent<Character>() : nullptr;   // the Animator plays its clips
    std::vector<std::string> clipNames = ma ? ma->ClipNames() : body ? body->ClipNames() : std::vector<std::string>{};

    // ---- Layers: every machine on the object, base first; tabs pick which one
    // the graph edits. A layer plays on part of the skeleton over the base.
    {
        static GameObject* s_layerObj = nullptr;
        static int s_layer = 0, s_forceTab = -1;
        if (s_layerObj != smObj) { s_layerObj = smObj; s_layer = 0; }
        if (const char* e = std::getenv("OKAY_UI_AGLAYER")) {
            static bool once = false;
            if (!once) { s_layer = std::atoi(e); s_forceTab = s_layer; once = true; }
        }
        std::vector<AnimStateMachine*> machines = smObj->GetComponents<AnimStateMachine>();
        if (s_layer >= (int)machines.size()) s_layer = 0;
        if (ImGui::BeginTabBar("##aglayers", ImGuiTabBarFlags_FittingPolicyScroll)) {
            for (int li = 0; li < (int)machines.size(); ++li) {
                std::string lbl = li == 0 ? std::string("Base Layer") :
                                  ("Layer " + std::to_string(li) + (machines[li]->maskBone.empty() ? std::string() : " (" + machines[li]->maskBone + ")"));
                lbl += "##agl" + std::to_string(li);
                if (ImGui::BeginTabItem(lbl.c_str(), nullptr, s_forceTab == li ? ImGuiTabItemFlags_SetSelected : 0)) {
                    if (s_forceTab < 0 || s_forceTab == li) s_layer = li;
                    ImGui::EndTabItem();
                }
            }
            if (ImGui::TabItemButton("+##agaddlayer", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip)) {
                ed.PushUndo();
                auto* lay = smObj->AddComponent<AnimStateMachine>();
                lay->layer = (int)machines.size();
                AnimStateMachine::State empty; empty.name = "Empty"; empty.nx = 20; empty.ny = 20;
                lay->states.push_back(empty);
                // A sensible default mask: the spine (upper body), if the rig has one.
                if (body) lay->maskBone = "torso";
                if (ma)
                    for (const auto& c : ma->clips)
                        for (const auto& nc : c.nodes) {
                            std::string lo = nc.node; for (auto& ch : lo) ch = (char)std::tolower((unsigned char)ch);
                            if (lay->maskBone.empty() && lo.find("spine") != std::string::npos) lay->maskBone = nc.node;
                        }
                s_layer = (int)machines.size();
                s_forceTab = s_layer;
                machines.push_back(lay);
                ed.dirty = true;
            } else if (s_forceTab >= 0 && s_layer == s_forceTab) s_forceTab = -1;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add a layer: its own states, playing on part of the skeleton\n(e.g. Spine and up) over the base layer.");
            ImGui::EndTabBar();
        }
        sm = machines[s_layer];
        if (sm->layer > 0) {
            ImGui::SetNextItemWidth(120);
            if (ImGui::SliderFloat("Weight##agl", &sm->layerWeight, 0.0f, 1.0f, "%.2f")) ed.dirty = true;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(170);
            if (ImGui::BeginCombo("Mask bone##agl", sm->maskBone.empty() ? "(whole body)" : sm->maskBone.c_str())) {
                if (ImGui::Selectable("(whole body)", sm->maskBone.empty())) { sm->maskBone.clear(); ed.dirty = true; }
                std::set<std::string> seen;
                if (ma)
                    for (const auto& c : ma->clips)
                        for (const auto& nc : c.nodes)
                            if (seen.insert(nc.node).second && ImGui::Selectable(nc.node.c_str(), sm->maskBone == nc.node)) { sm->maskBone = nc.node; ed.dirty = true; }
                if (body)
                    for (int b = 0; b < Character::BoneCount(); ++b)
                        if (ImGui::Selectable(Character::BoneName(b), sm->maskBone == Character::BoneToken(b))) { sm->maskBone = Character::BoneToken(b); ed.dirty = true; }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("This layer drives this bone and everything under it.\nPick the spine for upper-body actions (attack while walking).");
            ImGui::SameLine();
            if (ImGui::Checkbox("Additive##agl", &sm->additive)) ed.dirty = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Add this layer onto the base pose instead of replacing the masked bones.\n"
                                  "A recoil, aim offset, or lean while the body keeps walking.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove layer##agl")) {
                ed.PushUndo();
                smObj->RemoveComponent(sm);
                ed.dirty = true;
                ImGui::End(); return;
            }
            auto maskCombo = [&](const char* id, std::string& bone) {
                ImGui::SetNextItemWidth(170);
                if (ImGui::BeginCombo(id, bone.empty() ? "(pick a bone)" : bone.c_str())) {
                    std::set<std::string> seen;
                    if (ma)
                        for (const auto& c : ma->clips)
                            for (const auto& nc : c.nodes)
                                if (seen.insert(nc.node).second && ImGui::Selectable(nc.node.c_str(), bone == nc.node)) { bone = nc.node; ed.dirty = true; }
                    if (body)
                        for (int b = 0; b < Character::BoneCount(); ++b)
                            if (ImGui::Selectable(Character::BoneName(b), bone == Character::BoneToken(b))) { bone = Character::BoneToken(b); ed.dirty = true; }
                    ImGui::EndCombo();
                }
            };
            int delMask = -1;
            for (int mi = 0; mi < (int)sm->maskBones.size(); ++mi) {
                ImGui::PushID(mi);
                maskCombo("Also##agl", sm->maskBones[mi]);
                ImGui::SameLine();
                if (ImGui::SmallButton("x##agmx")) delMask = mi;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("This bone and the ones under it are in the mask too.");
                ImGui::PopID();
            }
            if (delMask >= 0) { sm->maskBones.erase(sm->maskBones.begin() + delMask); ed.dirty = true; }
            if (ImGui::SmallButton("+ Mask bone##agl")) { sm->maskBones.push_back(std::string()); ed.dirty = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add another bone subtree to this layer's mask (an avatar mask).");
        }
    }

    // Per-machine UI state.
    static void* s_key = nullptr;
    static int s_selState = -1, s_selTrFrom = -1, s_selTrIdx = -1, s_pendingFrom = -1;
    static int s_selAny = -1;   // selected Any State transition (side panel)
    static ImVec2 s_pan{40.0f, 40.0f};
    static float s_zoom = 1.0f;
    static bool s_dragPushed = false, s_frameReq = false, s_labels = true;
    static char s_find[48] = "";
    static AnimStateMachine::State s_stateClip;   // Ctrl+C / Ctrl+V
    static bool s_haveStateClip = false;
    if (s_key != (void*)sm) { s_key = (void*)sm; s_selState = -1; s_selTrFrom = -1; s_selTrIdx = -1; s_pendingFrom = -1; s_selAny = -1; s_pan = ImVec2(40, 40); s_zoom = 1.0f; }
    {   // OKAY_UI_AGSEL=<state>: select it once it exists (dev screenshots).
        static bool s_devSel = false;
        if (const char* e = std::getenv("OKAY_UI_AGSEL"); e && !s_devSel)
            for (int i = 0; i < (int)sm->states.size(); ++i)
                if (sm->states[i].name == e) { s_selState = i; s_devSel = true; break; }
    }
    if (s_selState >= 0 || s_selTrFrom >= 0) s_selAny = -1;
    if (s_selAny >= (int)sm->anyState.size()) s_selAny = -1;
    int n = (int)sm->states.size();
    if (s_selState >= n) s_selState = -1;
    if (s_selTrFrom >= n) { s_selTrFrom = -1; s_selTrIdx = -1; }
    if (s_pendingFrom >= n) s_pendingFrom = -1;

    // Auto-layout states that have never been placed (all at 0,0).
    bool anyPlaced = false;
    for (const auto& st : sm->states) if (st.nx != 0.0f || st.ny != 0.0f) { anyPlaced = true; break; }
    if (!anyPlaced && n > 1)
        for (int i = 0; i < n; ++i) { sm->states[i].nx = 20.0f + (i % 3) * 230.0f; sm->states[i].ny = 20.0f + (i / 3) * 110.0f; }

    // ---- toolbar ----
    ImGui::Text("%s", smObj->name.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Add State##ag")) {
        ed.PushUndo();
        AnimStateMachine::State st;
        st.name = "State " + std::to_string(n + 1);
        if (!clipNames.empty()) st.clip = clipNames.front();
        st.nx = 20.0f + (n % 3) * 230.0f; st.ny = 20.0f + (n / 3) * 110.0f;
        sm->states.push_back(std::move(st));
        s_selState = n; s_selTrFrom = -1; s_selTrIdx = -1;
        ed.dirty = true;
        n = (int)sm->states.size();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Auto Layout##ag")) {
        // Re-grid every node and recentre the pan — untangles a messy graph.
        ed.PushUndo();
        for (int i = 0; i < n; ++i) { sm->states[i].nx = 20.0f + (i % 3) * 230.0f; sm->states[i].ny = 20.0f + (i / 3) * 110.0f; }
        s_pan = ImVec2(40, 40);
        ed.dirty = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Arrange all states back into a tidy grid");
    if (ma && sm->layer == 0 && !clipNames.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Locomotion setup##ag")) ImGui::OpenPopup("##aglocosetup");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Build the usual character setup from this model's clips:\n"
                                                      "idle/walk/run blended by speed, jump, fall and land,\n"
                                                      "with the parameters the player controllers send.");
        if (ImGui::BeginPopup("##aglocosetup")) {
            auto shown = [&](const std::string& c) { return c.empty() || ma->FindClip(c) < 0 ? "-" : c.c_str(); };
            ImGui::TextDisabled("Clips mapped (empty slots are filled from the clip names):");
            ImGui::Text("Idle: %s\nWalk: %s\nRun:  %s\nJump: %s\nFall: %s\nLand: %s", shown(ma->idleClip), shown(ma->walkClip),
                        shown(ma->runClip), shown(ma->jumpClip), shown(ma->fallClip), shown(ma->landClip));
            ImGui::Text("Crouch: %s\nCrouch walk: %s\nProne: %s\nCrawl: %s\nGet up: %s", shown(ma->crouchClip),
                        shown(ma->crouchWalkClip), shown(ma->proneClip), shown(ma->crawlClip), shown(ma->getUpClip));
            if (!sm->states.empty()) ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1), "Replaces the %d existing state(s).", (int)sm->states.size());
            if (ImGui::Button("Build it", ImVec2(-1, 0))) {
                ed.PushUndo();
                okay::AutoMapLocomotion(ma);   // fills only the clip slots that are still empty
                ClipGraph::Build(*sm, *ma);
                // The machine drives the clips now; the animator's own speed switching stands down.
                ma->driveByMovement = false;
                ed.dirty = true;
                ConsoleLog("Animator: built a locomotion setup with " + std::to_string(sm->states.size()) + " states");
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Frame##ag")) s_frameReq = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zoom and pan to fit every state (F)");
    ImGui::SameLine();
    ImGui::Checkbox("Labels##ag", &s_labels);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show each transition's conditions on its arrow");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    bool findGo = ImGui::InputTextWithHint("##agfind", "Find state", s_find, sizeof(s_find), ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Matching states light up; Enter selects and centres the first");
    auto findMatch = [&](const std::string& nm) {
        if (!s_find[0]) return false;
        return Lower(nm).find(Lower(s_find)) != std::string::npos;
    };
    ImGui::SameLine();
    ImGui::TextDisabled("%d%%", (int)std::lround(s_zoom * 100.0f));
    // A state name not used yet ("Run" -> "Run 2").
    auto uniqueStateName = [&](const std::string& base) {
        auto used = [&](const std::string& nm) { for (const auto& st : sm->states) if (st.name == nm) return true; return false; };
        if (!used(base)) return base;
        for (int k = 2;; ++k) { std::string c = base + " " + std::to_string(k); if (!used(c)) return c; }
    };
    auto pasteState = [&](const AnimStateMachine::State& src) {
        ed.PushUndo();
        AnimStateMachine::State st = src;
        st.name = uniqueStateName(src.name);
        st.nx += 30.0f; st.ny += 30.0f;
        // Outgoing transitions only point at states that exist here.
        for (int t2 = (int)st.transitions.size() - 1; t2 >= 0; --t2) {
            bool ok = false;
            for (const auto& o : sm->states) if (o.name == st.transitions[t2].to) { ok = true; break; }
            if (!ok) st.transitions.erase(st.transitions.begin() + t2);
        }
        sm->states.push_back(std::move(st));
        s_selState = (int)sm->states.size() - 1; s_selTrFrom = -1; s_selTrIdx = -1;
        ed.dirty = true;
        n = (int)sm->states.size();
    };
    {
        const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput;
        const bool ctrl = ImGui::GetIO().KeyCtrl;
        if (focused && ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false) && s_selState >= 0 && s_selState < n)
            pasteState(AnimStateMachine::State(sm->states[s_selState]));
        if (focused && ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && s_selState >= 0 && s_selState < n) {
            s_stateClip = sm->states[s_selState]; s_haveStateClip = true;
        }
        if (focused && ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false) && s_haveStateClip) pasteState(s_stateClip);
        if (focused && !ctrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) s_frameReq = true;
    }
    // Delete key removes the selected state (same cleanup as the context menu:
    // drop inbound transitions and clear the entry if it pointed here).
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
        ImGui::IsKeyPressed(ImGuiKey_Delete, false) && s_selState >= 0 && s_selState < n) {
        ed.PushUndo();
        std::string dead = sm->states[s_selState].name;
        for (auto& s2 : sm->states) {
            auto& v = s2.transitions;
            for (int t2 = (int)v.size() - 1; t2 >= 0; --t2) if (v[t2].to == dead) v.erase(v.begin() + t2);
        }
        if (sm->entry == dead) sm->entry.clear();
        sm->states.erase(sm->states.begin() + s_selState);
        s_selState = -1; s_selTrFrom = -1; s_selTrIdx = -1;
        ed.dirty = true;
        n = (int)sm->states.size();
    }
    ImGui::SameLine();
    if (ed.isPlaying()) ImGui::TextColored(AccentCol(1.0f), "playing: %s", sm->Current().empty() ? "(entering)" : sm->Current().c_str());
    else ImGui::TextDisabled(s_pendingFrom >= 0 ? "click a state to connect the transition (right-click cancels)"
                                                : "drag nodes | right-click: transitions/entry | drag empty space: pan | wheel: zoom | Ctrl+D duplicate");

    const float sideW = 250.0f;
    ImVec2 avail = ImGui::GetContentRegionAvail();

    // ---- canvas ----
    ImGui::BeginChild("##agcanvas", ImVec2(avail.x - sideW - 8.0f, avail.y), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 org = ImGui::GetCursorScreenPos();
    ImVec2 csz = ImGui::GetContentRegionAvail();
    // subtle grid
    // Wheel zoom about the mouse; F / "Frame" fits every state in view.
    if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f) {
        ImVec2 mp = ImGui::GetIO().MousePos;
        float wx = (mp.x - org.x - s_pan.x) / s_zoom, wy = (mp.y - org.y - s_pan.y) / s_zoom;
        s_zoom = std::clamp(s_zoom * (ImGui::GetIO().MouseWheel > 0.0f ? 1.12f : 1.0f / 1.12f), 0.35f, 2.0f);
        s_pan = ImVec2(mp.x - org.x - wx * s_zoom, mp.y - org.y - wy * s_zoom);
    }
    if (s_frameReq && n > 0) {
        s_frameReq = false;
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (const auto& st : sm->states) { x0 = std::min(x0, st.nx); y0 = std::min(y0, st.ny); x1 = std::max(x1, st.nx + 180.0f); y1 = std::max(y1, st.ny + 46.0f); }
        s_zoom = std::clamp(std::min((csz.x - 60.0f) / std::max(x1 - x0, 1.0f), (csz.y - 60.0f) / std::max(y1 - y0, 1.0f)), 0.35f, 1.5f);
        s_pan = ImVec2(csz.x * 0.5f - (x0 + x1) * 0.5f * s_zoom, csz.y * 0.5f - (y0 + y1) * 0.5f * s_zoom);
    }
    if (findGo)
        for (int i = 0; i < n; ++i)
            if (findMatch(sm->states[i].name)) {
                s_selState = i; s_selTrFrom = -1; s_selTrIdx = -1;
                s_pan = ImVec2(csz.x * 0.5f - (sm->states[i].nx + 90.0f) * s_zoom, csz.y * 0.5f - (sm->states[i].ny + 23.0f) * s_zoom);
                break;
            }
    const float Z = s_zoom;
    ImU32 gridCol = ImGui::GetColorU32(ImGuiCol_Border, 0.25f);
    const float gstep = 40.0f * Z;
    for (float gx = std::fmod(s_pan.x, gstep); gx < csz.x; gx += gstep) dl->AddLine(ImVec2(org.x + gx, org.y), ImVec2(org.x + gx, org.y + csz.y), gridCol);
    for (float gy = std::fmod(s_pan.y, gstep); gy < csz.y; gy += gstep) dl->AddLine(ImVec2(org.x, org.y + gy), ImVec2(org.x + csz.x, org.y + gy), gridCol);

    const ImVec2 nodeSz(180.0f * Z, 46.0f * Z);
    ImFont* gFont = ImGui::GetFont();
    const float gFs = ImGui::GetFontSize() * std::clamp(Z, 0.6f, 1.4f);
    auto nodePos = [&](int i) { return ImVec2(org.x + s_pan.x + sm->states[i].nx * Z, org.y + s_pan.y + sm->states[i].ny * Z); };
    auto nodeCenter = [&](int i) { ImVec2 p = nodePos(i); return ImVec2(p.x + nodeSz.x * 0.5f, p.y + nodeSz.y * 0.5f); };
    int stateIdx[64]; (void)stateIdx;

    // ---- transitions (drawn under the nodes) ----
    ImVec2 mouse = ImGui::GetIO().MousePos;
    int hovTrFrom = -1, hovTrIdx = -1;
    // The running state's outgoing edges draw brighter so the possible next
    // moves stand out while watching the machine in Play.
    int curIdx = -1;
    if (ed.isPlaying())
        for (int k = 0; k < n; ++k) if (sm->states[k].name == sm->Current()) { curIdx = k; break; }
    // Arrow labels draw after the nodes so a node never hides one.
    struct ArrowLabel { ImVec2 pos; std::string text; ImU32 col; };
    std::vector<ArrowLabel> arrowLabels;
    for (int i = 0; i < n; ++i) {
        int outIdx = 0;
        for (int t = 0; t < (int)sm->states[i].transitions.size(); ++t) {
            const auto& tr = sm->states[i].transitions[t];
            int j = -1;
            for (int k = 0; k < n; ++k) if (sm->states[k].name == tr.to) { j = k; break; }
            if (j < 0) continue;
            ImVec2 a = nodeCenter(i), b = nodeCenter(j);
            // Two-way pairs (A -> B and B -> A): each runs on its own side.
            if (i != j) {
                bool back = false;
                for (const auto& rt : sm->states[j].transitions) if (rt.to == sm->states[i].name) { back = true; break; }
                if (back) {
                    float dx = b.x - a.x, dy = b.y - a.y, l = std::sqrt(dx * dx + dy * dy);
                    if (l > 1e-3f) {
                        ImVec2 side(-dy / l * 7.0f * Z, dx / l * 7.0f * Z);
                        a = ImVec2(a.x + side.x, a.y + side.y); b = ImVec2(b.x + side.x, b.y + side.y);
                    }
                }
            }
            float off = 10.0f * (float)outIdx++;
            ImVec2 c1(a.x + 70.0f * Z, a.y + off * Z), c2(b.x - 70.0f * Z, b.y + off * Z);
            if (i == j) { c1 = ImVec2(a.x + 120 * Z, a.y - 70 * Z); c2 = ImVec2(a.x - 120 * Z, a.y - 70 * Z); }   // self loop
            bool sel = (s_selTrFrom == i && s_selTrIdx == t);
            // Just fired (Play): flash for a moment so you see what happened.
            bool fired = false;
            if (ed.isPlaying() && !sm->History().empty()) {
                const auto& h = sm->History().back();
                fired = h.from == sm->states[i].name && h.to == tr.to && sm->Clock() - h.time < 0.6f;
            }
            // hit test along the curve
            bool hov = false;
            for (int sIt = 0; sIt <= 24 && !hov; ++sIt) {
                float u = sIt / 24.0f, v = 1.0f - u;
                ImVec2 p(v*v*v*a.x + 3*v*v*u*c1.x + 3*v*u*u*c2.x + u*u*u*b.x,
                         v*v*v*a.y + 3*v*v*u*c1.y + 3*v*u*u*c2.y + u*u*u*b.y);
                float dx = p.x - mouse.x, dy = p.y - mouse.y;
                if (dx * dx + dy * dy < 49.0f) hov = true;
            }
            if (hov) { hovTrFrom = i; hovTrIdx = t; }
            ImU32 col = sel ? ImGui::GetColorU32(ImVec4(1.0f, 0.8f, 0.2f, 1.0f))
                      : fired ? ImGui::GetColorU32(ImVec4(0.35f, 0.95f, 0.55f, 1.0f))
                      : hov ? ImGui::GetColorU32(ImGuiCol_Text)
                      : tr.muted ? ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.35f)
                      : i == curIdx ? ImGui::GetColorU32(ImVec4(0.95f, 0.70f, 0.20f, 0.85f))
                                    : ImGui::GetColorU32(ImGuiCol_TextDisabled);
            dl->AddBezierCubic(a, c1, c2, b, col, sel || fired || i == curIdx ? 3.0f : 2.0f);
            // arrowhead at the middle of the curve
            float u = 0.55f, v = 1.0f - u;
            ImVec2 p(v*v*v*a.x + 3*v*v*u*c1.x + 3*v*u*u*c2.x + u*u*u*b.x,
                     v*v*v*a.y + 3*v*v*u*c1.y + 3*v*u*u*c2.y + u*u*u*b.y);
            float u2 = 0.60f, v2 = 1.0f - u2;
            ImVec2 q(v2*v2*v2*a.x + 3*v2*v2*u2*c1.x + 3*v2*u2*u2*c2.x + u2*u2*u2*b.x,
                     v2*v2*v2*a.y + 3*v2*v2*u2*c1.y + 3*v2*u2*u2*c2.y + u2*u2*u2*b.y);
            ImVec2 d(q.x - p.x, q.y - p.y);
            float len = std::sqrt(d.x * d.x + d.y * d.y);
            if (len > 1e-3f) {
                d.x /= len; d.y /= len;
                ImVec2 nrm(-d.y, d.x);
                dl->AddTriangleFilled(ImVec2(p.x + d.x * 9, p.y + d.y * 9),
                                      ImVec2(p.x + nrm.x * 5, p.y + nrm.y * 5),
                                      ImVec2(p.x - nrm.x * 5, p.y - nrm.y * 5), col);
                // What makes it fire, next to the arrow.
                if ((s_labels && Z >= 0.6f) || sel || hov) {
                    std::string lb = tr.muted ? "(muted) " + TransitionSummary(tr) : TransitionSummary(tr);
                    const float lfs = gFs * 0.85f;
                    ImVec2 ts = gFont->CalcTextSizeA(lfs, FLT_MAX, 0.0f, lb.c_str());
                    // Centred beside the arrowhead, on the side its normal points to.
                    ImVec2 lp(p.x + nrm.x * (8.0f + ts.x * 0.5f) - ts.x * 0.5f, p.y + nrm.y * (6.0f + ts.y * 0.5f) - ts.y * 0.5f);
                    arrowLabels.push_back({lp, lb, sel || hov ? col : ImGui::GetColorU32(ImGuiCol_TextDisabled)});
                }
            }
        }
    }
    // pending new transition follows the mouse
    if (s_pendingFrom >= 0 && s_pendingFrom < n)
        dl->AddLine(nodeCenter(s_pendingFrom), mouse, ImGui::GetColorU32(ImVec4(1.0f, 0.8f, 0.2f, 1.0f)), 2.0f);

    // ---- nodes ----
    bool nodeHovered = false;
    for (int i = 0; i < n; ++i) {
        AnimStateMachine::State& st = sm->states[i];
        ImVec2 p = nodePos(i);
        ImGui::SetCursorScreenPos(p);
        ImGui::PushID(i);
        ImGui::InvisibleButton("##agnode", nodeSz);
        bool hov = ImGui::IsItemHovered();
        nodeHovered |= hov;
        if (ImGui::IsItemActivated()) {
            if (s_pendingFrom >= 0) {   // complete the pending transition onto this node
                ed.PushUndo();
                AnimStateMachine::Transition tr;
                tr.to = st.name;
                sm->states[s_pendingFrom].transitions.push_back(tr);
                s_selTrFrom = s_pendingFrom; s_selTrIdx = (int)sm->states[s_pendingFrom].transitions.size() - 1;
                s_selState = -1; s_pendingFrom = -1;
                ed.dirty = true;
            } else { s_selState = i; s_selTrFrom = -1; s_selTrIdx = -1; s_dragPushed = false; }
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f) && s_pendingFrom < 0) {
            if (!s_dragPushed) { ed.PushUndo(); s_dragPushed = true; }
            st.nx += ImGui::GetIO().MouseDelta.x / Z;
            st.ny += ImGui::GetIO().MouseDelta.y / Z;
            ed.dirty = true;
        }
        if (ImGui::BeginPopupContextItem("##agnodectx")) {
            s_selState = i; s_selTrFrom = -1; s_selTrIdx = -1;
            if (ImGui::MenuItem("Add Transition from here")) s_pendingFrom = i;
            if (ImGui::MenuItem("Set as Entry")) { ed.PushUndo(); sm->entry = st.name; ed.dirty = true; }
            if (ImGui::MenuItem("Duplicate", "Ctrl+D")) { AnimStateMachine::State cp = st; ImGui::EndPopup(); ImGui::PopID(); pasteState(cp); break; }
            if (ImGui::MenuItem("Copy", "Ctrl+C")) { s_stateClip = st; s_haveStateClip = true; }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete State")) {
                ed.PushUndo();
                std::string dead = st.name;
                for (auto& s2 : sm->states) {
                    auto& v = s2.transitions;
                    for (int t2 = (int)v.size() - 1; t2 >= 0; --t2) if (v[t2].to == dead) v.erase(v.begin() + t2);
                }
                if (sm->entry == dead) sm->entry.clear();
                sm->states.erase(sm->states.begin() + i);
                s_selState = -1; s_selTrFrom = -1; s_selTrIdx = -1;
                ed.dirty = true;
                ImGui::EndPopup(); ImGui::PopID();
                break;   // container changed — redraw next frame
            }
            ImGui::EndPopup();
        }
        // visuals
        bool isEntry = (sm->entry == st.name) || (sm->entry.empty() && i == 0);
        bool isCurrent = ed.isPlaying() && sm->Current() == st.name;
        bool isSel = (s_selState == i);
        const bool found = findMatch(st.name);
        ImU32 fill = isCurrent ? ImGui::GetColorU32(ImVec4(0.95f, 0.70f, 0.20f, 0.90f))
                               : ImGui::GetColorU32(isSel ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
        dl->AddRectFilled(p, ImVec2(p.x + nodeSz.x, p.y + nodeSz.y), fill, 6.0f);
        ImU32 border = isEntry ? ImGui::GetColorU32(ImVec4(0.25f, 0.80f, 0.35f, 1.0f))
                               : ImGui::GetColorU32(isSel ? ImGuiCol_Text : ImGuiCol_Border);
        dl->AddRect(p, ImVec2(p.x + nodeSz.x, p.y + nodeSz.y), border, 6.0f, 0, isEntry || isSel ? 2.5f : 1.0f);
        if (found)
            dl->AddRect(ImVec2(p.x - 4.0f, p.y - 4.0f), ImVec2(p.x + nodeSz.x + 4.0f, p.y + nodeSz.y + 4.0f),
                        ImGui::GetColorU32(ImVec4(0.30f, 0.85f, 1.0f, 0.9f)), 8.0f, 0, 2.0f);
        // Running state: a soft pulsing halo so the live state is unmissable.
        if (isCurrent) {
            float pulse = 0.5f + 0.5f * std::sin((float)ImGui::GetTime() * 5.0f);
            dl->AddRect(ImVec2(p.x - 3.0f, p.y - 3.0f),
                        ImVec2(p.x + nodeSz.x + 3.0f, p.y + nodeSz.y + 3.0f),
                        ImGui::GetColorU32(ImVec4(0.95f, 0.70f, 0.20f, 0.30f + 0.45f * pulse)),
                        8.0f, 0, 3.0f);
        }
        // Entry state: a small green play-triangle badge on the left edge.
        if (isEntry)
            dl->AddTriangleFilled(ImVec2(p.x - 9.0f, p.y + nodeSz.y * 0.5f - 7.0f),
                                  ImVec2(p.x - 9.0f, p.y + nodeSz.y * 0.5f + 7.0f),
                                  ImVec2(p.x + 1.0f, p.y + nodeSz.y * 0.5f),
                                  ImGui::GetColorU32(ImVec4(0.25f, 0.80f, 0.35f, 1.0f)));
        ImU32 txt = isCurrent ? ImGui::GetColorU32(ImVec4(0.05f, 0.05f, 0.05f, 1.0f)) : ImGui::GetColorU32(ImGuiCol_Text);
        const ImVec4 clipR(p.x, p.y, p.x + nodeSz.x - 4.0f, p.y + nodeSz.y);
        dl->AddText(gFont, gFs, ImVec2(p.x + 10 * Z, p.y + 6 * Z), txt, st.name.c_str(), nullptr, 0.0f, &clipR);
        std::string sub = st.blendTree ? (st.blendDims == 2
                              ? "2D blend: " + st.blendParam + " / " + st.blendParamY
                              : "Blend tree: " + (st.blendParam.empty() ? std::string("?") : st.blendParam)) +
                                          " (" + std::to_string(st.motions.size()) + ")"
                        : st.clip.empty() ? "(no clip)" : st.clip;
        if (isEntry) sub += "   [entry]";
        if (st.ikWeight < 0.999f) sub += "  IK " + std::to_string((int)std::lround(st.ikWeight * 100.0f)) + "%";
        dl->AddText(gFont, gFs * 0.92f, ImVec2(p.x + 10 * Z, p.y + 24 * Z),
                    isCurrent ? ImGui::GetColorU32(ImVec4(0.15f, 0.15f, 0.15f, 1.0f)) : ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    sub.c_str(), nullptr, 0.0f, &clipR);
        if (!st.enterEvent.empty() || !st.exitEvent.empty())   // event marker, top-right
            dl->AddCircleFilled(ImVec2(p.x + nodeSz.x - 9 * Z, p.y + 9 * Z), 3.5f * Z, ImGui::GetColorU32(ImVec4(0.95f, 0.55f, 0.85f, 1.0f)));
        if (isCurrent) {   // how far through the state's clip (per loop)
            float nt = sm->NormalizedTime(); nt -= std::floor(nt);
            dl->AddRectFilled(ImVec2(p.x + 6, p.y + nodeSz.y - 5), ImVec2(p.x + 6 + (nodeSz.x - 12) * nt, p.y + nodeSz.y - 3),
                              ImGui::GetColorU32(ImVec4(0.1f, 0.1f, 0.1f, 0.85f)), 1.0f);
        }
        ImGui::PopID();
    }

    for (const ArrowLabel& al : arrowLabels) {
        const float lfs = gFs * 0.85f;
        ImVec2 ts = gFont->CalcTextSizeA(lfs, FLT_MAX, 0.0f, al.text.c_str());
        dl->AddRectFilled(ImVec2(al.pos.x - 3, al.pos.y - 1), ImVec2(al.pos.x + ts.x + 3, al.pos.y + ts.y + 1),
                          ImGui::GetColorU32(ImGuiCol_WindowBg, 0.9f), 3.0f);
        dl->AddText(gFont, lfs, al.pos, al.col, al.text.c_str());
    }

    // canvas-level interactions (below nodes): select transitions, pan, context menu
    if (ImGui::IsWindowHovered() && !nodeHovered) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (hovTrFrom >= 0) { s_selTrFrom = hovTrFrom; s_selTrIdx = hovTrIdx; s_selState = -1; }
            else { s_selState = -1; s_selTrFrom = -1; s_selTrIdx = -1; }
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            s_pan.x += ImGui::GetIO().MouseDelta.x;
            s_pan.y += ImGui::GetIO().MouseDelta.y;
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            if (s_pendingFrom >= 0) s_pendingFrom = -1;   // cancel pending connect
            else ImGui::OpenPopup("##agcanvasctx");
        }
    }
    if (ImGui::BeginPopup("##agcanvasctx")) {
        if (s_haveStateClip && ImGui::MenuItem("Paste State", "Ctrl+V")) {
            AnimStateMachine::State cp = s_stateClip;
            cp.nx = (mouse.x - org.x - s_pan.x) / Z - 30.0f; cp.ny = (mouse.y - org.y - s_pan.y) / Z - 30.0f;
            pasteState(cp);
        }
        if (ImGui::MenuItem("Frame All", "F")) s_frameReq = true;
        if (ImGui::MenuItem("Add State Here")) {
            ed.PushUndo();
            AnimStateMachine::State st;
            st.name = "State " + std::to_string(n + 1);
            if (!clipNames.empty()) st.clip = clipNames.front();
            st.nx = (mouse.x - org.x - s_pan.x) / Z; st.ny = (mouse.y - org.y - s_pan.y) / Z;
            sm->states.push_back(std::move(st));
            s_selState = (int)sm->states.size() - 1;
            ed.dirty = true;
        }
        ImGui::EndPopup();
    }
    ImGui::EndChild();

    // ---- side panel ----
    ImGui::SameLine();
    ImGui::BeginChild("##agside", ImVec2(sideW, avail.y), true);
    n = (int)sm->states.size();
    // Parameter picker (declared params of the wanted type, else free text).
    auto paramPicker = [&](const char* id, std::string& param, int wantType) {
        bool anyOfType = false;
        for (const auto& p : sm->params) if (p.type == wantType) { anyOfType = true; break; }
        if (anyOfType) {
            if (ImGui::BeginCombo(id, param.empty() ? "(pick)" : param.c_str())) {
                for (const auto& p : sm->params)
                    if (p.type == wantType && ImGui::Selectable(p.name.c_str(), param == p.name)) { param = p.name; ed.dirty = true; }
                ImGui::EndCombo();
            }
        } else {
            char pb[48]; std::snprintf(pb, sizeof(pb), "%s", param.c_str());
            if (ImGui::InputText(id, pb, sizeof(pb))) { param = pb; ed.dirty = true; }
        }
    };
    static const char* kConds[] = {"On Clip End", "Float >", "Float <", "Bool true", "Bool false", "Trigger",
                                   "Always (use exit time)", "Int ==", "Int !="};
    auto condType = [](AnimStateMachine::Cond c) {
        return c == AnimStateMachine::Cond::Trigger ? 2
             : (c == AnimStateMachine::Cond::BoolTrue || c == AnimStateMachine::Cond::BoolFalse) ? 1
             : (c == AnimStateMachine::Cond::IntEquals || c == AnimStateMachine::Cond::IntNotEquals) ? 3 : 0;
    };
    auto needsParam = [](AnimStateMachine::Cond c) { return c != AnimStateMachine::Cond::OnClipEnd && c != AnimStateMachine::Cond::Always; };
    auto isFloatCond = [](AnimStateMachine::Cond c) { return c == AnimStateMachine::Cond::FloatGreater || c == AnimStateMachine::Cond::FloatLess; };
    auto isIntCond = [](AnimStateMachine::Cond c) { return c == AnimStateMachine::Cond::IntEquals || c == AnimStateMachine::Cond::IntNotEquals; };
    // Shared editor for a transition (state transitions and Any State ones).
    auto editTransition = [&](AnimStateMachine::Transition& tr, bool isAny) {
        if (isAny) {
            if (ImGui::BeginCombo("To##agany", tr.to.empty() ? "(pick)" : tr.to.c_str())) {
                for (const auto& st : sm->states)
                    if (ImGui::Selectable(st.name.c_str(), tr.to == st.name)) { tr.to = st.name; ed.dirty = true; }
                ImGui::EndCombo();
            }
        }
        int cd = (int)tr.cond;
        if (ImGui::Combo("Condition##ag", &cd, kConds, AnimStateMachine::kCondCount)) { tr.cond = (AnimStateMachine::Cond)cd; ed.dirty = true; }
        if (needsParam(tr.cond)) paramPicker("Parameter##ag", tr.param, condType(tr.cond));
        if (isFloatCond(tr.cond))
            if (ImGui::DragFloat("Value##ag", &tr.value, 0.05f)) ed.dirty = true;
        if (isIntCond(tr.cond)) {
            int iv = (int)std::lround(tr.value);
            if (ImGui::DragInt("Value##agi", &iv, 0.1f)) { tr.value = (float)iv; ed.dirty = true; }
        }
        // Extra ANDed conditions.
        int delC = -1;
        for (int ci = 0; ci < (int)tr.extra.size(); ++ci) {
            AnimStateMachine::Condition& c = tr.extra[ci];
            ImGui::PushID(1000 + ci);
            ImGui::TextDisabled("and");
            ImGui::SameLine();
            int ccd = (int)c.cond;
            ImGui::SetNextItemWidth(-ImGui::GetFrameHeight() - 6.0f);
            if (ImGui::Combo("##xc", &ccd, kConds, AnimStateMachine::kCondCount)) { c.cond = (AnimStateMachine::Cond)ccd; ed.dirty = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) delC = ci;
            if (needsParam(c.cond)) paramPicker("##xp", c.param, condType(c.cond));
            if (isFloatCond(c.cond)) if (ImGui::DragFloat("##xv", &c.value, 0.05f)) ed.dirty = true;
            if (isIntCond(c.cond)) { int iv = (int)std::lround(c.value); if (ImGui::DragInt("##xvi", &iv, 0.1f)) { c.value = (float)iv; ed.dirty = true; } }
            ImGui::PopID();
        }
        if (delC >= 0) { tr.extra.erase(tr.extra.begin() + delC); ed.dirty = true; }
        if (ImGui::SmallButton("+ Condition##ag")) { tr.extra.push_back({AnimStateMachine::Cond::BoolTrue, "", 0.0f}); ed.dirty = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Every condition must pass for the transition to fire.");
        ImGui::Separator();
        if (ImGui::Checkbox("Exit time##ag", &tr.hasExitTime)) ed.dirty = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Only fire once the current state is this far through its clip\n(0.9 = 90%; on looping states, within each loop).");
        if (tr.hasExitTime) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##agexit", &tr.exitTime, 0.0f, 1.0f, "%.2f")) ed.dirty = true;
        }
        if (ImGui::DragFloat("Blend (s)##ag", &tr.blend, 0.01f, 0.0f, 2.0f)) ed.dirty = true;
        if (ImGui::Checkbox("Mute##ag", &tr.muted)) ed.dirty = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Never fire: try the machine without this transition, keep it for later.");
        if (ImGui::Checkbox("Keep phase##ag", &tr.syncPhase)) ed.dirty = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Start the target state where the current one is in its cycle,\n"
                                                      "so walk <-> run between two looping states keeps the feet in step.");
        if (!tr.syncPhase) {
            if (ImGui::SliderFloat("Start at##ag", &tr.offset, 0.0f, 0.99f, "%.2f")) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Start the target state part-way into its clip (0.5 = halfway).\n"
                                                          "Line up the feet when switching between walk/run cycles.");
        }
        if (isAny && ImGui::Checkbox("Can re-enter itself##ag", &tr.canInterruptSelf)) ed.dirty = true;
        if (!isAny) {
            static const char* kInt[] = {"Next State", "None", "Current State", "Current, then Next", "Next, then Current"};
            int iv = (int)tr.interruption;
            ImGui::SetNextItemWidth(-90);
            if (ImGui::Combo("Interruption##ag", &iv, kInt, IM_ARRAYSIZE(kInt))) {
                tr.interruption = (AnimStateMachine::Transition::Interrupt)iv; ed.dirty = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("While this blend runs, whose transitions may cut it short:\n"
                                  "  Next State - the target state's (default)\n"
                                  "  None - nothing but Any State until the blend ends\n"
                                  "  Current State - the state being left (e.g. cancel a dodge back to Idle)\n"
                                  "  Current, then Next / Next, then Current - both, in that order");
        }
    };
    if (s_selState >= 0 && s_selState < n) {
        AnimStateMachine::State& st = sm->states[s_selState];
        ImGui::TextDisabled("State");
        char nb[48]; std::snprintf(nb, sizeof(nb), "%s", st.name.c_str());
        if (ImGui::InputText("Name##ag", nb, sizeof(nb))) {
            // keep transitions + entry pointing at the renamed state
            std::string oldName = st.name, newName = nb;
            if (!newName.empty() && newName != oldName) {
                st.name = newName;
                for (auto& s2 : sm->states)
                    for (auto& tr : s2.transitions) if (tr.to == oldName) tr.to = newName;
                for (auto& tr : sm->anyState) if (tr.to == oldName) tr.to = newName;
                if (sm->entry == oldName) sm->entry = newName;
                ed.dirty = true;
            }
        }
        // Clip slot: this backend's clips, the built-in library, and any imported
        // clips on this object or under it. The running machine plays `fallbackClip`
        // when `clip` is not in its own library.
        std::vector<std::string> slotNames = clipNames;
        auto addSlot = [&](const std::string& n) {
            if (n.empty()) return;
            for (const auto& e : slotNames) if (e == n) return;
            slotNames.push_back(n);
        };
        if (body)
            for (const std::string& n : okay::HumanoidLibrary::Names()) addSlot(n);
        if (smObj)
            for (const auto& up : ed.scene().Objects()) {
                GameObject* g = up.get();
                if (!g || !g->IsSelfOrDescendantOf(smObj)) continue;
                if (auto* im = g->GetComponent<ModelAnimator>())
                    for (const std::string& n : im->ClipNames()) addSlot(n);
            }
        addSlot(st.clip);
        addSlot(st.fallbackClip);
        auto slotPlayable = [&](const std::string& n) {
            if (n.empty()) return false;
            if (body && okay::editor::apreview::BodyClip(body, n)) return true;
            if (ma && ma->FindClip(n) >= 0) return true;
            return false;
        };
        std::string previewMotion = okay::editor::apreview::MiddleMotion(st);
        if (!slotPlayable(previewMotion) && slotPlayable(st.fallbackClip)) previewMotion = st.fallbackClip;
        // Live preview of this state (edit and Play). A blend tree offers its motions.
        if (body && okay::editor::apreview::BodyClip(body, previewMotion)) {
            const bool prevOn = okay::editor::apreview::ToggleCharacterState(ed.scene(), body, st, ed.isPlaying(), "agprev");
            if (prevOn) okay::editor::apreview::DrawTransport("agprev");
        } else if (ma && ma->FindClip(previewMotion) >= 0) {
            const bool prevOn = okay::editor::apreview::ToggleModelState(ed.scene(), ma, st, ed.isPlaying());
            if (prevOn) okay::editor::apreview::DrawTransport("agmaprev");
        }
        auto importIntoSlot = [&](std::string& slot) {
            if (ma) {
                std::vector<std::string> before = ma->ClipNames();
                AddAnimationsFromFilesUI(ed, smObj);
                for (const auto& n : ma->ClipNames()) {
                    bool had = false;
                    for (const auto& b : before) if (b == n) { had = true; break; }
                    if (!had) { slot = n; ed.dirty = true; ConsoleLog("Imported '" + n + "' into " + st.name); break; }
                }
            } else if (body) {
                const char* filt[] = {"*.okayanim"};
                if (const char* p = tinyfd_openFileDialog("Import clip into this state", "", 1, filt, "OkaySpace animation", 0)) {
                    std::vector<std::string> before = body->ClipNames();
                    std::string err;
                    if (!body->LoadClipsFromFile(p, &err, /*replaceExisting=*/false)) {
                        ConsoleLog(err.empty() ? "Import clip failed" : err, 2);
                        return;
                    }
                    std::string picked;
                    for (const auto& n : body->ClipNames()) {
                        bool had = false;
                        for (const auto& b : before) if (b == n) { had = true; break; }
                        if (!had) { picked = n; break; }
                    }
                    if (picked.empty()) {
                        std::ifstream fin(p);
                        std::stringstream ss; if (fin) ss << fin.rdbuf();
                        auto parsed = okay::AnimClip::ParseAll(ss.str(), &okay::Character::BoneIndex, okay::Character::BoneCount(), nullptr);
                        if (!parsed.empty()) picked = parsed.front().name;
                    }
                    if (!picked.empty()) {
                        slot = picked; ed.dirty = true;
                        ConsoleLog("Imported '" + picked + "' into " + st.name);
                    }
                }
            }
        };
        if (!slotNames.empty()) {
            if (ImGui::BeginCombo("Clip##ag", st.clip.empty() ? "(none)" : st.clip.c_str())) {
                for (const auto& cn : slotNames)
                    if (ImGui::Selectable(cn.c_str(), cn == st.clip)) { st.clip = cn; ed.dirty = true; }
                ImGui::EndCombo();
            }
        } else {
            char cb[48]; std::snprintf(cb, sizeof(cb), "%s", st.clip.c_str());
            if (ImGui::InputText("Clip##ag", cb, sizeof(cb))) { st.clip = cb; ed.dirty = true; }
        }
        if (ImGui::BeginCombo("If missing##ag", st.fallbackClip.empty() ? "(none)" : st.fallbackClip.c_str())) {
            if (ImGui::Selectable("(none)", st.fallbackClip.empty())) { st.fallbackClip.clear(); ed.dirty = true; }
            for (const auto& cn : slotNames)
                if (ImGui::Selectable(cn.c_str(), cn == st.fallbackClip)) { st.fallbackClip = cn; ed.dirty = true; }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Played when Clip is not in this Animator's library.\n"
                              "A name that exists only on a child model's clips plays there\n"
                              "(Humanoid Retarget yields until the state changes).");
        if ((ma || body) && ImGui::SmallButton("Import into clip##ag")) importIntoSlot(st.clip);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Load a clip file and put its name in the Clip slot.\n"
                              "A character takes .okayanim. An imported model takes FBX, glTF or BVH.");
        if (ma || body) { ImGui::SameLine(); }
        if ((ma || body) && ImGui::SmallButton("Import as fallback##ag")) importIntoSlot(st.fallbackClip);
        if (ImGui::DragFloat("Speed##ag", &st.speed, 0.02f, 0.05f, 5.0f)) ed.dirty = true;
        {
            bool useSp = !st.speedParam.empty();
            if (ImGui::Checkbox("x parameter##agsp", &useSp)) { st.speedParam = useSp ? "speedMultiplier" : ""; ed.dirty = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Multiply the speed by a float parameter (e.g. playback rate that\nfollows how fast the character moves).");
            if (useSp) { ImGui::SameLine(); ImGui::SetNextItemWidth(-1); paramPicker("##agspp", st.speedParam, 0); }
        }
        if (!st.blendTree && ImGui::Checkbox("Loop##ag", &st.loop)) ed.dirty = true;
        if (ImGui::Checkbox("Mirror##ag", &st.mirror)) ed.dirty = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Play this state on the other side: left and right swap.\n"
                              "Author one wave or punch and mirror it for the other hand.");
        {
            static const char* kRoot[] = {"Inherit", "Off", "On"};
            int rm = st.rootMotion < 0 ? 0 : (st.rootMotion == 0 ? 1 : 2);
            ImGui::SetNextItemWidth(120);
            if (ImGui::Combo("Root motion##ag", &rm, kRoot, 3)) {
                st.rootMotion = rm == 0 ? -1 : (rm == 1 ? 0 : 1);
                ed.dirty = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Inherit uses the Model Animator's own root-motion toggle.\n"
                                  "Off strips this state's root travel. On turns it on for this state only.");
        }
        if (ImGui::SliderFloat("IK weight##ag", &st.ikWeight, 0.0f, 1.0f, "%.2f")) ed.dirty = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Foot planting in this state (and Full Body IK, unless given its own weight below).\n"
                                                      "A curve below multiplies this over the clip.");
        auto drawIkCurve = [&](const char* id, std::vector<AnimStateMachine::State::IkKey>& keys, const char* tip) {
            ImGui::PushID(id);
            if (keys.empty()) {
                if (ImGui::SmallButton("Add curve")) {
                    keys.push_back({0.0f, 1.0f});
                    keys.push_back({1.0f, 1.0f});
                    ed.dirty = true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
            } else {
                const float mw = ImGui::GetContentRegionAvail().x, mh = 64.0f;
                const ImVec2 mo = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("##plot", ImVec2(mw, mh));
                ImDrawList* pdl = ImGui::GetWindowDrawList();
                pdl->AddRectFilled(mo, ImVec2(mo.x + mw, mo.y + mh), ImGui::GetColorU32(ImGuiCol_FrameBg), 3.0f);
                auto toP = [&](float t, float w) { return ImVec2(mo.x + t * mw, mo.y + (1.0f - w) * mh); };
                auto fromP = [&](ImVec2 p) {
                    float t = mw > 1.0f ? (p.x - mo.x) / mw : 0.0f;
                    float w = mh > 1.0f ? 1.0f - (p.y - mo.y) / mh : 0.0f;
                    return ImVec2(std::min(std::max(t, 0.0f), 1.0f), std::min(std::max(w, 0.0f), 1.0f));
                };
                std::vector<ImVec2> pts;
                for (const auto& k : keys) pts.push_back(toP(k.t, k.w));
                if ((int)pts.size() >= 2)
                    pdl->AddPolyline(pts.data(), (int)pts.size(), ImGui::GetColorU32(ImGuiCol_PlotLines), ImDrawFlags_None, 2.0f);
                if (ed.isPlaying() && sm->Current() == st.name) {
                    float u = sm->NormalizedTime(); u -= std::floor(u); if (u < 0.0f) u += 1.0f;
                    const float x = mo.x + u * mw;
                    pdl->AddLine(ImVec2(x, mo.y), ImVec2(x, mo.y + mh), ImGui::GetColorU32(ImGuiCol_PlotHistogram));
                }
                static int s_drag = -1;
                static std::vector<AnimStateMachine::State::IkKey>* s_owner = nullptr;
                const ImVec2 mp = ImGui::GetIO().MousePos;
                int hit = -1; float best = 9.0f;
                for (int i = 0; i < (int)keys.size(); ++i) {
                    const ImVec2 p = toP(keys[i].t, keys[i].w);
                    const float d = std::hypot(p.x - mp.x, p.y - mp.y);
                    if (d < best) { best = d; hit = i; }
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && hit >= 0) {
                    keys.erase(keys.begin() + hit); ed.dirty = true;
                    if (s_owner == &keys) { s_drag = -1; s_owner = nullptr; }
                } else if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                    if (hit < 0) {
                        const ImVec2 tw = fromP(mp);
                        keys.push_back({tw.x, tw.y});
                        std::stable_sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) { return a.t < b.t; });
                        hit = 0;
                        for (int i = 0; i < (int)keys.size(); ++i) if (keys[i].t == tw.x && keys[i].w == tw.y) { hit = i; break; }
                        ed.dirty = true;
                    }
                    s_drag = hit; s_owner = &keys;
                }
                if (s_owner == &keys && s_drag >= 0 && s_drag < (int)keys.size() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    const ImVec2 tw = fromP(mp);
                    keys[s_drag].t = tw.x; keys[s_drag].w = tw.y;
                    const float t = tw.x, w = tw.y;
                    std::stable_sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) { return a.t < b.t; });
                    s_drag = 0;
                    for (int i = 0; i < (int)keys.size(); ++i) if (keys[i].t == t && keys[i].w == w) { s_drag = i; break; }
                    ed.dirty = true;
                }
                if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) { s_drag = -1; s_owner = nullptr; }
                for (int i = 0; i < (int)keys.size(); ++i) {
                    const ImVec2 p = toP(keys[i].t, keys[i].w);
                    pdl->AddCircleFilled(p, 4.0f, ImGui::GetColorU32(i == s_drag && s_owner == &keys ? ImGuiCol_PlotHistogramHovered : ImGuiCol_PlotLinesHovered));
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nClick to add a key, drag to move, right-click to remove.", tip);
                if (ImGui::SmallButton("Clear curve")) { keys.clear(); ed.dirty = true; }
            }
            ImGui::PopID();
        };
        drawIkCurve("foot", st.ikCurve, "Foot IK weight over this clip (0 at the bottom, 1 at the top),\nmultiplied by the IK weight slider. Empty = that constant.");
        {
            bool follow = st.fullBodyIkWeight < 0.0f;
            if (ImGui::Checkbox("Full-body IK follows##ag", &follow)) { st.fullBodyIkWeight = follow ? -1.0f : 1.0f; ed.dirty = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Off: Full Body IK gets its own weight here, so a state can stop foot\nplanting while the hands keep holding (prone, crawling).");
            if (!follow && ImGui::SliderFloat("Full-body IK weight##ag", &st.fullBodyIkWeight, 0.0f, 1.0f, "%.2f")) ed.dirty = true;
            if (!follow) drawIkCurve("body", st.fullBodyIkCurve, "Full Body IK weight over this clip, multiplied by its slider.\nEmpty = the constant. Foot IK keeps its own curve.");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("How much Foot IK / Full Body IK apply in this state; eases over\n"
                                                      "the transition. 0 for jumps, rolls and falls (feet leave the ground).");
        {
            char eb[48]; std::snprintf(eb, sizeof(eb), "%s", st.enterEvent.c_str());
            if (ImGui::InputTextWithHint("On enter##agev", "event name", eb, sizeof(eb))) { st.enterEvent = eb; ed.dirty = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Animation event fired when the machine enters this state:\n"
                                                          "anim_event() in scripts, and visual scripts' animation event nodes.");
            char xb[48]; std::snprintf(xb, sizeof(xb), "%s", st.exitEvent.c_str());
            if (ImGui::InputTextWithHint("On exit##agev", "event name", xb, sizeof(xb))) { st.exitEvent = xb; ed.dirty = true; }
        }
        // Motion: one clip, or a 1D blend tree driven by a float parameter.
        ImGui::Separator();
        int mode = st.blendTree ? 1 : 0;
        if (ImGui::RadioButton("Single clip##ag", &mode, 0)) { st.blendTree = false; ed.dirty = true; }
        ImGui::SameLine();
        if (ImGui::RadioButton("Blend tree##ag", &mode, 1)) {
            st.blendTree = true;
            if (st.motions.empty() && !st.clip.empty()) st.motions.push_back({st.clip, 0.0f});
            ed.dirty = true;
        }
        if (st.blendTree) {
            int dims = st.blendDims == 2 ? 1 : 0;
            if (ImGui::RadioButton("1D##agbd", &dims, 0)) { st.blendDims = 1; ed.dirty = true; }
            ImGui::SameLine();
            if (ImGui::RadioButton("2D (strafe)##agbd", &dims, 1)) {
                st.blendDims = 2;
                if (st.blendParam.empty() || st.blendParam == "speed") st.blendParam = "moveX";
                if (st.blendParamY.empty()) st.blendParamY = "moveZ";
                ed.dirty = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Blend over two parameters: e.g. moveX / moveZ (sideways / forward speed)\n"
                                                          "with idle in the middle and walk forward, back, left and right around it.\n"
                                                          "The player controllers send moveX / moveZ.");
        }
        if (st.blendTree && st.blendDims == 2) {
            paramPicker("X by##ag2", st.blendParam, 0);
            paramPicker("Y by##ag2", st.blendParamY, 0);
            const bool live2 = ed.isPlaying() && sm->Current() == st.name;
            const std::vector<float>& lw = sm->BlendWeights();
            // The 2D map: motions as dots (drag to move), the live parameter point.
            float mw = ImGui::GetContentRegionAvail().x, mh = std::min(mw, 170.0f);
            ImVec2 mo = ImGui::GetCursorScreenPos();
            ImDrawList* pdl = ImGui::GetWindowDrawList();
            pdl->AddRectFilled(mo, ImVec2(mo.x + mw, mo.y + mh), ImGui::GetColorU32(ImGuiCol_FrameBg), 4.0f);
            float ext = 1.0f;
            for (const auto& m : st.motions) ext = std::max(ext, std::max(std::fabs(m.threshold), std::fabs(m.posY)));
            ext *= 1.25f;
            auto toMap = [&](float x, float y) { return ImVec2(mo.x + (x / ext * 0.5f + 0.5f) * mw, mo.y + (0.5f - y / ext * 0.5f) * mh); };
            auto fromMap = [&](ImVec2 p) { return ImVec2(((p.x - mo.x) / mw - 0.5f) * 2.0f * ext, (0.5f - (p.y - mo.y) / mh) * 2.0f * ext); };
            pdl->AddLine(toMap(-ext, 0), toMap(ext, 0), ImGui::GetColorU32(ImGuiCol_Border));
            pdl->AddLine(toMap(0, -ext), toMap(0, ext), ImGui::GetColorU32(ImGuiCol_Border));
            static int s_dragMotion = -1;
            for (int mi = 0; mi < (int)st.motions.size(); ++mi) {
                AnimStateMachine::Motion& m = st.motions[mi];
                ImVec2 c = toMap(m.threshold, m.posY);
                ImGui::SetCursorScreenPos(ImVec2(c.x - 7, c.y - 7));
                ImGui::PushID(3000 + mi);
                ImGui::InvisibleButton("##mdot", ImVec2(14, 14));
                if (ImGui::IsItemActivated()) { ed.PushUndo(); s_dragMotion = mi; }
                if (ImGui::IsItemActive() && s_dragMotion == mi) {
                    ImVec2 v = fromMap(ImGui::GetIO().MousePos);
                    m.threshold = std::round(v.x * 10.0f) / 10.0f; m.posY = std::round(v.y * 10.0f) / 10.0f;
                    ed.dirty = true;
                }
                if (ImGui::IsItemDeactivated()) s_dragMotion = -1;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s at (%.1f, %.1f)\nDrag to move", m.clip.c_str(), m.threshold, m.posY);
                ImGui::PopID();
                float wgt = live2 && mi < (int)lw.size() ? lw[mi] : 0.0f;
                pdl->AddCircleFilled(c, 4.0f + wgt * 6.0f, ImGui::GetColorU32(ImVec4(0.45f, 0.65f, 1.0f, 0.5f + wgt * 0.5f)));
                pdl->AddText(ImVec2(c.x + 7, c.y - 7), ImGui::GetColorU32(ImGuiCol_TextDisabled), m.clip.c_str());
            }
            if (live2) {
                ImVec2 lp = toMap(sm->GetFloat(st.blendParam), sm->GetFloat(st.blendParamY));
                pdl->AddCircle(lp, 6.0f, ImGui::GetColorU32(ImVec4(1.0f, 0.75f, 0.2f, 1.0f)), 16, 2.0f);
            }
            ImGui::SetCursorScreenPos(ImVec2(mo.x, mo.y + mh + 4));
            int delM = -1;
            for (int mi = 0; mi < (int)st.motions.size(); ++mi) {
                AnimStateMachine::Motion& m = st.motions[mi];
                ImGui::PushID(4000 + mi);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
                if (ImGui::BeginCombo("##mc2", m.clip.empty() ? "(clip)" : m.clip.c_str())) {
                    for (const auto& cn : clipNames)
                        if (ImGui::Selectable(cn.c_str(), cn == m.clip)) { m.clip = cn; ed.dirty = true; }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                float xy[2] = {m.threshold, m.posY};
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 6.0f);
                if (ImGui::DragFloat2("##mxy", xy, 0.05f)) { m.threshold = xy[0]; m.posY = xy[1]; ed.dirty = true; }
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) delM = mi;
                ImGui::PopID();
            }
            if (delM >= 0) { st.motions.erase(st.motions.begin() + delM); ed.dirty = true; }
            if (ImGui::SmallButton("+ Motion##ag2")) {
                st.motions.push_back({clipNames.empty() ? std::string() : clipNames.front(), 0.0f, 0.0f});
                ed.dirty = true;
            }
        }
        if (st.blendTree && st.blendDims != 2) {
            paramPicker("Blend by##ag", st.blendParam, 0);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("A float parameter, e.g. speed: idle at 0, walk at 2, run at 6.");
            int lo = -1, hi = -1; float bw = 0.0f;
            const bool live = ed.isPlaying() && sm->Current() == st.name;
            if (live) sm->BlendInfo(lo, hi, bw);
            int delM = -1;
            for (int mi = 0; mi < (int)st.motions.size(); ++mi) {
                AnimStateMachine::Motion& m = st.motions[mi];
                ImGui::PushID(2000 + mi);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
                if (!clipNames.empty()) {
                    if (ImGui::BeginCombo("##mc", m.clip.empty() ? "(clip)" : m.clip.c_str())) {
                        for (const auto& cn : clipNames)
                            if (ImGui::Selectable(cn.c_str(), cn == m.clip)) { m.clip = cn; ed.dirty = true; }
                        ImGui::EndCombo();
                    }
                } else {
                    char cb[48]; std::snprintf(cb, sizeof(cb), "%s", m.clip.c_str());
                    if (ImGui::InputText("##mc", cb, sizeof(cb))) { m.clip = cb; ed.dirty = true; }
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 6.0f);
                if (ImGui::DragFloat("##mt", &m.threshold, 0.05f, 0.0f, 0.0f, "at %.2f")) ed.dirty = true;
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) delM = mi;
                if (live) {   // live weight bar
                    float wt = mi == lo && mi == hi ? 1.0f : mi == lo ? 1.0f - bw : mi == hi ? bw : 0.0f;
                    ImGui::ProgressBar(wt, ImVec2(-1, 4.0f), "");
                }
                ImGui::PopID();
            }
            if (delM >= 0) { st.motions.erase(st.motions.begin() + delM); ed.dirty = true; }
            if (ma && !st.motions.empty()) {
                if (ImGui::SmallButton("Thresholds from clip speed##ag")) {
                    ed.PushUndo();
                    int set = 0;
                    for (auto& m : st.motions) {
                        int ci = ma->FindClip(m.clip);
                        if (ci < 0) continue;
                        float v = ma->ClipRootSpeed(ci);
                        m.threshold = std::round(v * 100.0f) / 100.0f; ++set;
                    }
                    ed.dirty = true;
                    ConsoleLog("Blend tree '" + st.name + "': thresholds set from " + std::to_string(set) + " clip(s)' root speed (m/s)");
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Set each motion's threshold to how fast its clip actually moves (root\n"
                                      "bone, m/s), so 'speed' = your character's real velocity plays the clip\n"
                                      "whose feet match it: no sliding. In-place clips read 0.");
                ImGui::SameLine();
            }
            if (ImGui::SmallButton("+ Motion##ag")) {
                float next = st.motions.empty() ? 0.0f : st.motions.back().threshold + 1.0f;
                st.motions.push_back({clipNames.empty() ? std::string() : clipNames.front(), next});
                ed.dirty = true;
            }
            if (live) ImGui::TextDisabled("%s = %.2f", st.blendParam.c_str(), sm->GetFloat(st.blendParam));
        }
        ImGui::Separator();
        if (ImGui::Button("Add Transition##ag")) s_pendingFrom = s_selState;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Then click the target state in the graph.");
        // Outgoing transitions in the order they're tested (the first that passes wins).
        if (!st.transitions.empty()) {
            ImGui::TextDisabled("Outgoing (tested top to bottom):");
            int moveFrom = -1, moveTo = -1;
            for (int ti = 0; ti < (int)st.transitions.size(); ++ti) {
                const auto& tr = st.transitions[ti];
                ImGui::PushID(5000 + ti);
                if (ImGui::SmallButton("^") && ti > 0) { moveFrom = ti; moveTo = ti - 1; }
                ImGui::SameLine();
                if (ImGui::SmallButton("v") && ti + 1 < (int)st.transitions.size()) { moveFrom = ti; moveTo = ti + 1; }
                ImGui::SameLine();
                std::string lbl = "-> " + tr.to + "  " + TransitionSummary(tr);
                if (tr.muted) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                if (ImGui::Selectable(lbl.c_str(), false)) { s_selTrFrom = s_selState; s_selTrIdx = ti; }
                if (tr.muted) ImGui::PopStyleColor();
                ImGui::PopID();
            }
            if (moveFrom >= 0) {
                ed.PushUndo();
                std::swap(st.transitions[moveFrom], st.transitions[moveTo]);
                ed.dirty = true;
            }
        }
    } else if (s_selTrFrom >= 0 && s_selTrFrom < n &&
               s_selTrIdx >= 0 && s_selTrIdx < (int)sm->states[s_selTrFrom].transitions.size()) {
        AnimStateMachine::State& from = sm->states[s_selTrFrom];
        AnimStateMachine::Transition& tr = from.transitions[s_selTrIdx];
        ImGui::TextDisabled("Transition");
        ImGui::Text("%s -> %s", from.name.c_str(), tr.to.c_str());
        editTransition(tr, false);
        ImGui::Separator();
        if (ImGui::Button("Delete Transition##ag")) {
            ed.PushUndo();
            from.transitions.erase(from.transitions.begin() + s_selTrIdx);
            s_selTrFrom = -1; s_selTrIdx = -1;
            ed.dirty = true;
        }
    } else if (s_selAny >= 0) {
        AnimStateMachine::Transition& tr = sm->anyState[s_selAny];
        ImGui::TextDisabled("Any State transition");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Checked every frame from whatever state is playing.");
        editTransition(tr, true);
        ImGui::Separator();
        if (ImGui::Button("Delete##agany")) {
            ed.PushUndo();
            sm->anyState.erase(sm->anyState.begin() + s_selAny);
            s_selAny = -1; ed.dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Done##agany")) s_selAny = -1;
    } else {
        ImGui::TextDisabled("Click a state or a transition\narrow to edit it here.");
        // Any State transitions (hit reactions, death...): fire from every state.
        ImGui::Separator();
        ImGui::TextDisabled("Any State transitions:");
        for (int ai = 0; ai < (int)sm->anyState.size(); ++ai) {
            const auto& tr = sm->anyState[ai];
            std::string lbl = "Any -> " + (tr.to.empty() ? std::string("?") : tr.to) +
                              (tr.param.empty() ? std::string() : "  (" + tr.param + ")") + "##any" + std::to_string(ai);
            if (ImGui::Selectable(lbl.c_str(), false)) s_selAny = ai;
        }
        if (ImGui::SmallButton("+ Any State transition##ag")) {
            ed.PushUndo();
            AnimStateMachine::Transition tr;
            if (!sm->states.empty()) tr.to = sm->states.front().name;
            sm->anyState.push_back(tr);
            s_selAny = (int)sm->anyState.size() - 1;
            ed.dirty = true;
        }
        ImGui::Separator();
        ImGui::TextDisabled("Entry state:");
        const char* cur = sm->entry.empty() ? "(first state)" : sm->entry.c_str();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##agentry", cur)) {
            if (ImGui::Selectable("(first state)", sm->entry.empty())) { sm->entry.clear(); ed.dirty = true; }
            for (const auto& st : sm->states)
                if (ImGui::Selectable(st.name.c_str(), sm->entry == st.name)) { sm->entry = st.name; ed.dirty = true; }
            ImGui::EndCombo();
        }
        // ---- Parameters: declare once, pick from
        //      dropdowns in transitions; live values shown in Play mode. ----
        ImGui::Separator();
        ImGui::TextDisabled("Parameters:");
        static const char* kPTypes[] = {"Float", "Bool", "Trigger", "Int"};
        int delP = -1;
        for (int pi = 0; pi < (int)sm->params.size(); ++pi) {
            AnimStateMachine::Param& p = sm->params[pi];
            ImGui::PushID(pi);
            ImGui::SetNextItemWidth(96);
            char pb[40]; std::snprintf(pb, sizeof(pb), "%s", p.name.c_str());
            if (ImGui::InputText("##pn", pb, sizeof(pb))) { p.name = pb; ed.dirty = true; }
            ImGui::SameLine(); ImGui::SetNextItemWidth(70);
            if (ImGui::Combo("##pt", &p.type, kPTypes, 4)) ed.dirty = true;
            ImGui::SameLine();
            if (ed.isPlaying()) {
                // Live values, editable: try the machine by hand while playing.
                ImGui::SetNextItemWidth(-1);
                if (p.type == 0)      { float v = sm->GetFloat(p.name); if (ImGui::DragFloat("##pv", &v, 0.05f)) sm->SetFloat(p.name, v); }
                else if (p.type == 1) { bool b = sm->GetBool(p.name); if (ImGui::Checkbox("##pv", &b)) sm->SetBool(p.name, b); }
                else if (p.type == 3) { int v = sm->GetInt(p.name); if (ImGui::DragInt("##pv", &v, 0.1f)) sm->SetInt(p.name, v); }
                else                  { if (ImGui::SmallButton("fire##pf")) sm->SetTrigger(p.name); }
            } else if (ImGui::SmallButton("x##pd")) delP = pi;
            ImGui::PopID();
        }
        if (delP >= 0) { ed.PushUndo(); sm->params.erase(sm->params.begin() + delP); ed.dirty = true; }
        if (ImGui::SmallButton("+ Float##pa"))   { ed.PushUndo(); sm->params.push_back({"param", 0}); ed.dirty = true; }
        ImGui::SameLine();
        if (ImGui::SmallButton("+ Bool##pb"))    { ed.PushUndo(); sm->params.push_back({"flag", 1}); ed.dirty = true; }
        ImGui::SameLine();
        if (ImGui::SmallButton("+ Trigger##pc")) { ed.PushUndo(); sm->params.push_back({"go", 2}); ed.dirty = true; }
        ImGui::SameLine();
        if (ImGui::SmallButton("+ Int##pi"))     { ed.PushUndo(); sm->params.push_back({"mode", 3}); ed.dirty = true; }
        if (ed.isPlaying()) {
            float left = 0.0f;
            if (sm->InTransition(&left))
                ImGui::TextColored(AccentCol(), "Blending %s -> %s  (%.2fs left)", sm->TransitionSource().c_str(),
                                   sm->Current().c_str(), left);
            else
                ImGui::TextDisabled("In %s (no transition running)", sm->Current().c_str());
            if (sm->Interruptions() > 0) ImGui::TextDisabled("%d transition%s interrupted", sm->Interruptions(), sm->Interruptions() == 1 ? "" : "s");
        }
        if (ed.isPlaying() && !sm->History().empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("Recent state changes:");
            const auto& hist = sm->History();
            for (int hi = (int)hist.size() - 1; hi >= 0 && hi >= (int)hist.size() - 8; --hi) {
                const auto& h = hist[hi];
                ImGui::TextDisabled("%6.2fs", h.time);
                ImGui::SameLine();
                ImGui::Text("%s -> %s", h.from.empty() ? "(start)" : h.from.c_str(), h.to.c_str());
            }
        }
        if (ed.isPlaying()) {
            ImGui::Separator();
            ImGui::TextDisabled("Set from scripts:");
            ImGui::TextWrapped("anim_set_float(\"speed\", v)\nanim_set_bool(\"armed\", 1)\nanim_trigger(\"attack\")");
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace oked
