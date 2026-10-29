// The Animator window: the state-machine graph editor for AnimStateMachine.
// States are nodes on a canvas (pan, zoom, box-select, inline rename, a
// minimap), transitions are arrows between them, one curve per pair of
// states. The side panel edits the selection (state, transition, Any State
// transition or the machine), the parameters (live in Play), the layer (its
// mask as a bone tree) and lists the graph's problems. Play mode shows the
// running state, its progress, and the transitions that fire.
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "AnimatorWindow.hpp"
#include "AnimatorPreview.hpp"          // live preview of a state / transition on the selected character
#include "ConsolePanel.hpp"             // ConsoleLog
#include "EditorWidgets.hpp"            // AccentCol, SearchField
#include "MainShared.hpp"               // AddAnimationsFromFilesUI
#include "okay/Animation/ClipGraph.hpp"       // an Animator graph from an imported model's clips
#include "okay/Animation/HumanoidLibrary.hpp" // built-in clip names, the default humanoid graph
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Render/ModelImport.hpp"        // AutoMapLocomotion
#include "imgui.h"
#include "imgui_internal.h"             // BeginDragDropTargetCustom (dropping a clip on the canvas)
#include "tinyfiledialogs.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::editor;

namespace oked {
namespace {

using SM = AnimStateMachine;

// Node sizes in graph units (zoom 1).
constexpr float kNodeW = 190.0f, kNodeH = 52.0f;
constexpr float kAnyW = 120.0f, kAnyH = 40.0f;

// ---------------------------------------------------------------------------
// Text helpers
// ---------------------------------------------------------------------------
std::string Lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

const char* kConds[] = {"On Clip End", "Float >", "Float <", "Bool true", "Bool false", "Trigger",
                        "Always (use exit time)", "Int ==", "Int !="};
const char* kParamTypes[] = {"Float", "Bool", "Trigger", "Int"};

bool NeedsParam(SM::Cond c) { return c != SM::Cond::OnClipEnd && c != SM::Cond::Always; }
bool IsFloatCond(SM::Cond c) { return c == SM::Cond::FloatGreater || c == SM::Cond::FloatLess; }
bool IsIntCond(SM::Cond c) { return c == SM::Cond::IntEquals || c == SM::Cond::IntNotEquals; }
// The parameter type a condition reads (0 float, 1 bool, 2 trigger, 3 int).
int CondParamType(SM::Cond c) {
    return c == SM::Cond::Trigger ? 2
         : (c == SM::Cond::BoolTrue || c == SM::Cond::BoolFalse) ? 1
         : IsIntCond(c) ? 3 : 0;
}

// One condition in a few characters: "speed > 2", "not crouch", "hit".
std::string CondText(SM::Cond c, const std::string& p, float v) {
    char b[96];
    const char* pn = p.empty() ? "?" : p.c_str();
    switch (c) {
        case SM::Cond::OnClipEnd:    return "clip end";
        case SM::Cond::FloatGreater: std::snprintf(b, sizeof(b), "%s > %g", pn, v); return b;
        case SM::Cond::FloatLess:    std::snprintf(b, sizeof(b), "%s < %g", pn, v); return b;
        case SM::Cond::BoolTrue:     return pn;
        case SM::Cond::BoolFalse:    return std::string("not ") + pn;
        case SM::Cond::Trigger:      return std::string(pn) + "!";
        case SM::Cond::Always:       return "always";
        case SM::Cond::IntEquals:    std::snprintf(b, sizeof(b), "%s = %d", pn, (int)std::lround(v)); return b;
        case SM::Cond::IntNotEquals: std::snprintf(b, sizeof(b), "%s != %d", pn, (int)std::lround(v)); return b;
    }
    return "?";
}

// A transition's conditions only: "speed > 2 & grounded".
std::string ConditionsText(const SM::Transition& tr) {
    std::string out = CondText(tr.cond, tr.param, tr.value);
    for (const auto& c : tr.extra) out += " & " + CondText(c.cond, c.param, c.value);
    return out;
}

// The compact label on an arrow: "speed > 2 · exit 0.9 · 0.25s".
std::string TransitionLabel(const SM::Transition& tr) {
    std::string out = ConditionsText(tr);
    char b[48];
    if (tr.hasExitTime) { std::snprintf(b, sizeof(b), " \xC2\xB7 exit %.2g", tr.exitTime); out += b; }
    std::snprintf(b, sizeof(b), " \xC2\xB7 %.2gs", tr.blend); out += b;
    if (tr.muted) out = "(muted) " + out;
    return out;
}

std::string Fmt(const char* fmt, double v) { char b[48]; std::snprintf(b, sizeof(b), fmt, v); return b; }

// ---------------------------------------------------------------------------
// Undo-aware widgets. A drag or a typing session on one item pushes a single
// undo step holding the value from before it; every change marks the scene dirty.
// ---------------------------------------------------------------------------
ImGuiID g_editItem = 0;   // the item whose running edit already pushed its undo step

// Run `widget` on `v` (a lambda taking T& and returning whether it changed it).
template <class T, class W> bool Tweak(EditorState& ed, T& v, W&& widget) {
    const T before = v;
    const bool changed = widget(v);
    const ImGuiID id = ImGui::GetItemID();
    if (changed) {
        if (!(ImGui::IsItemActive() && g_editItem == id)) {
            T after = v;
            v = before;
            ed.PushUndo();
            v = after;
            g_editItem = ImGui::IsItemActive() ? id : 0;
        }
        ed.dirty = true;
    }
    if (g_editItem == id && !ImGui::IsItemActive()) g_editItem = 0;
    return changed;
}
// Call right after a widget reported a change and before applying it (for
// widgets that report instead of writing in place: text fields, combos).
void EditPush(EditorState& ed) {
    const ImGuiID id = ImGui::GetItemID();
    if (ImGui::IsItemActive() && g_editItem == id) return;
    ed.PushUndo();
    g_editItem = ImGui::IsItemActive() ? id : 0;
}
void Tip(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", text);
}
bool Check(EditorState& ed, const char* label, bool& v) {
    return Tweak(ed, v, [&](bool& x) { return ImGui::Checkbox(label, &x); });
}
bool DragF(EditorState& ed, const char* label, float& v, float speed, float lo, float hi, const char* fmt = "%.2f") {
    return Tweak(ed, v, [&](float& x) { return ImGui::DragFloat(label, &x, speed, lo, hi, fmt); });
}
bool SliderF(EditorState& ed, const char* label, float& v, float lo, float hi, const char* fmt = "%.2f") {
    return Tweak(ed, v, [&](float& x) { return ImGui::SliderFloat(label, &x, lo, hi, fmt); });
}
bool TextEdit(EditorState& ed, const char* label, std::string& s, const char* hint = nullptr) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s", s.c_str());
    return Tweak(ed, s, [&](std::string& v) {
        const bool ch = hint ? ImGui::InputTextWithHint(label, hint, buf, sizeof(buf)) : ImGui::InputText(label, buf, sizeof(buf));
        if (ch) v = buf;
        return ch;
    });
}
// A section title in the inspector.
bool Section(const char* label, bool open = true) {
    ImGui::Spacing();
    return ImGui::CollapsingHeader(label, open ? ImGuiTreeNodeFlags_DefaultOpen : 0);
}
ImU32 Col(float r, float g, float b, float a = 1.0f) { return ImGui::GetColorU32(ImVec4(r, g, b, a)); }
ImU32 WarnCol(float a = 1.0f) { return Col(0.95f, 0.75f, 0.25f, a); }
ImU32 ErrCol(float a = 1.0f) { return Col(0.95f, 0.38f, 0.35f, a); }
ImU32 EntryCol(float a = 1.0f) { return Col(0.25f, 0.80f, 0.35f, a); }
ImU32 PlayCol(float a = 1.0f) { return Col(0.95f, 0.70f, 0.20f, a); }
ImU32 FiredCol(float a = 1.0f) { return Col(0.35f, 0.95f, 0.55f, a); }
ImU32 AnyCol(float a = 1.0f) { return Col(0.62f, 0.48f, 0.88f, a); }
// A layer's tint (the base layer uses the theme accent).
ImVec4 LayerTint(int layer) {
    static const ImVec4 k[] = {ImVec4(0.62f, 0.45f, 0.85f, 1), ImVec4(0.25f, 0.70f, 0.65f, 1), ImVec4(0.85f, 0.55f, 0.30f, 1),
                               ImVec4(0.80f, 0.40f, 0.55f, 1), ImVec4(0.45f, 0.70f, 0.30f, 1)};
    if (layer <= 0) return AccentCol();
    return k[(layer - 1) % (int)(sizeof(k) / sizeof(k[0]))];
}
// A small warning / error triangle with "!" in it.
void DrawWarnGlyph(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    dl->AddTriangleFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r * 0.95f, c.y + r * 0.75f), ImVec2(c.x - r * 0.95f, c.y + r * 0.75f), col);
    const ImU32 ink = Col(0.1f, 0.1f, 0.1f);
    dl->AddLine(ImVec2(c.x, c.y - r * 0.35f), ImVec2(c.x, c.y + r * 0.2f), ink, std::max(1.0f, r * 0.22f));
    dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.45f), std::max(1.0f, r * 0.14f), ink);
}

// ---------------------------------------------------------------------------
// What the window edits this frame.
// ---------------------------------------------------------------------------
struct Ctx {
    EditorState& ed;
    GameObject* obj = nullptr;          // the object holding the machines
    std::vector<SM*> machines;          // base first
    SM* sm = nullptr;                   // the layer being edited
    SM* base = nullptr;                 // layer 0 (owns the parameters)
    ModelAnimator* ma = nullptr;        // the backend on an imported model...
    Character* body = nullptr;          // ...or the Character body the machine plays
    std::vector<std::string> clips;     // the backend's own clips
    std::vector<std::string> slotNames; // clips + the built-in library + imported clips under the object
    std::vector<ModelAnimator*> imported;   // child models with their own clips (missing names play there)
    bool playing = false;
    std::vector<SM::Issue> issues;      // Validate() for sm
    std::vector<int> stateSeverity;     // per state: 0 none, 1 warning, 2 error (its transitions included)
    int anySeverity = 0;

    explicit Ctx(EditorState& e) : ed(e) {}

    // Can this name play on the machine (the backend, the library, or a child model's clips)?
    bool Playable(const std::string& n) const {
        if (n.empty()) return false;
        if (body && apreview::BodyClip(body, n)) return true;
        if (ma && ma->FindClip(n) >= 0) return true;
        for (ModelAnimator* im : imported) if (im->FindClip(n) >= 0) return true;
        return false;
    }
    bool ParamDeclared(const std::string& n) const {
        for (const auto& p : base->params) if (p.name == n) return true;
        return false;
    }
    // The declared parameter's type, or -1.
    int ParamType(const std::string& n) const {
        for (const auto& p : base->params) if (p.name == n) return p.type;
        return -1;
    }
    bool ParamUsed(const std::string& n) const {
        for (SM* m : machines) if (m->UsesParam(n)) return true;
        return false;
    }
    int StateIndex(const std::string& n) const {
        for (int i = 0; i < (int)sm->states.size(); ++i) if (sm->states[i].name == n) return i;
        return -1;
    }
    void Validate() {
        issues = sm->Validate([this](const std::string& n) { return Playable(n); });
        // Problems first, notes after.
        std::stable_sort(issues.begin(), issues.end(), [](const SM::Issue& a, const SM::Issue& b) { return (a.severity > 0) > (b.severity > 0); });
        stateSeverity.assign(sm->states.size(), 0);
        anySeverity = 0;
        for (const auto& is : issues) {
            if (is.any) anySeverity = std::max(anySeverity, is.severity);
            else if (is.state >= 0 && is.state < (int)stateSeverity.size())
                stateSeverity[is.state] = std::max(stateSeverity[is.state], is.severity);
        }
    }
    int ProblemCount() const {
        int n = 0;
        for (const auto& is : issues) n += is.severity > 0 ? 1 : 0;
        return n;
    }
    // The issues about one state (and its transitions), one per line.
    std::string StateIssues(int st) const {
        std::string out;
        for (const auto& is : issues)
            if (!is.any && is.state == st) { if (!out.empty()) out += "\n"; out += is.text; }
        return out;
    }
    int TransitionSeverity(int st, int tr, bool any) const {
        int sev = 0;
        for (const auto& is : issues)
            if (is.any == any && is.transition == tr && (any || is.state == st)) sev = std::max(sev, is.severity);
        return sev;
    }
};

// Every clip the machine could play: its backend's, the built-in library on a
// Character, and imported takes on models under the object.
void BuildClipLists(Ctx& c) {
    c.clips = c.ma ? c.ma->ClipNames() : c.body ? c.body->ClipNames() : std::vector<std::string>{};
    c.slotNames = c.clips;
    auto add = [&](const std::string& n) {
        if (n.empty()) return;
        for (const auto& e : c.slotNames) if (e == n) return;
        c.slotNames.push_back(n);
    };
    if (c.body) for (const std::string& n : HumanoidLibrary::Names()) add(n);
    c.imported.clear();
    if (c.obj)
        for (const auto& up : c.ed.scene().Objects()) {
            GameObject* g = up.get();
            if (!g || g == c.obj || !g->IsSelfOrDescendantOf(c.obj)) continue;
            if (auto* im = g->GetComponent<ModelAnimator>(); im && im->ClipCount() > 0) {
                c.imported.push_back(im);
                for (const std::string& n : im->ClipNames()) add(n);
            }
        }
}

// ---------------------------------------------------------------------------
// Window state (one window; keyed by the machine it shows).
// ---------------------------------------------------------------------------
struct Ui {
    std::string key;                    // the object and layer the selection belongs to (survives undo)
    GameObject* layerObj = nullptr;
    int layer = 0, forceLayerTab = -1;
    // canvas
    ImVec2 pan{60.0f, 60.0f};
    float zoom = 1.0f;
    bool frameReq = false, labels = true, minimap = true;
    bool panning = false;
    bool boxing = false; ImVec2 boxStart{};
    bool dragPushed = false;
    bool anyDragPushed = false;
    ImVec2 rightDown{};
    // selection
    std::vector<int> sel;               // selected states; `primary` is the one the inspector shows
    int primary = -1;
    int selTrFrom = -1, selTrIdx = -1;  // a state's transition
    int selAny = -1;                    // an Any State transition
    int pendingFrom = -1;               // connecting: source state, -2 = Any State, -1 = none
    int renaming = -1; char renameBuf[64] = ""; bool renameFocus = false;
    int hovState = -1;
    // side panel
    float sideW = 380.0f;
    int forceSideTab = -1;              // 0 Inspector, 1 Parameters, 2 Layer, 3 Problems, 4 Clips
    char find[48] = "";
    char paramFilter[48] = "";
    char clipFilter[48] = "";
    std::vector<char> clipPick;         // Clips tab: which rows are ticked (by slotNames index)
    std::vector<SM::State> clipboard;   // Ctrl+C / Ctrl+V
    float lastFocusX = 0.0f;
    std::string lastObj;                // the object shown last, for when nothing is selected (Play)
};
Ui& U() { static Ui u; return u; }
bool g_focused = false;   // the window had keyboard focus this frame

bool IsSel(const Ui& u, int i) { return std::find(u.sel.begin(), u.sel.end(), i) != u.sel.end(); }
void ClearSel(Ui& u) { u.sel.clear(); u.primary = -1; u.selTrFrom = -1; u.selTrIdx = -1; u.selAny = -1; }
// Select one state (mode 0 = only it, 1 = add, 2 = toggle).
void SelectState(Ui& u, int i, int mode = 0) {
    u.selTrFrom = -1; u.selTrIdx = -1; u.selAny = -1;
    if (mode == 0) { u.sel.assign(1, i); u.primary = i; return; }
    auto it = std::find(u.sel.begin(), u.sel.end(), i);
    if (mode == 2 && it != u.sel.end()) {
        u.sel.erase(it);
        u.primary = u.sel.empty() ? -1 : u.sel.back();
        return;
    }
    if (it == u.sel.end()) u.sel.push_back(i);
    u.primary = i;
}
void SelectTransition(Ui& u, int from, int idx) {
    u.sel.clear(); u.primary = -1; u.selAny = -1;
    u.selTrFrom = from; u.selTrIdx = idx;
    u.forceSideTab = 0;
}
void SelectAny(Ui& u, int idx) {
    u.sel.clear(); u.primary = -1; u.selTrFrom = -1; u.selTrIdx = -1;
    u.selAny = idx;
    u.forceSideTab = 0;
}
// Keep the selection inside the machine after states come and go.
void ClampSel(Ui& u, const SM& sm) {
    const int n = (int)sm.states.size();
    u.sel.erase(std::remove_if(u.sel.begin(), u.sel.end(), [&](int i) { return i < 0 || i >= n; }), u.sel.end());
    if (u.primary >= n) u.primary = u.sel.empty() ? -1 : u.sel.back();
    if (u.primary < 0 && !u.sel.empty()) u.primary = u.sel.back();
    if (u.selTrFrom >= n || (u.selTrFrom >= 0 && u.selTrIdx >= (int)sm.states[u.selTrFrom].transitions.size())) { u.selTrFrom = -1; u.selTrIdx = -1; }
    if (u.selAny >= (int)sm.anyState.size()) u.selAny = -1;
    if (u.pendingFrom >= n) u.pendingFrom = -1;
    if (u.renaming >= n) u.renaming = -1;
}

// ---------------------------------------------------------------------------
// Graph edits
// ---------------------------------------------------------------------------
// A state name not used yet ("Run" -> "Run 2").
std::string UniqueName(const SM& sm, const std::string& base, int except = -1) {
    auto used = [&](const std::string& nm) {
        for (int i = 0; i < (int)sm.states.size(); ++i) if (i != except && sm.states[i].name == nm) return true;
        return false;
    };
    if (!used(base)) return base;
    for (int k = 2;; ++k) { std::string c = base + " " + std::to_string(k); if (!used(c)) return c; }
}
// Rename a state, keeping transitions and the entry pointing at it.
void RenameState(SM& sm, int i, const std::string& newName) {
    const std::string oldName = sm.states[i].name;
    if (newName.empty() || newName == oldName) return;
    sm.states[i].name = newName;
    for (auto& s2 : sm.states)
        for (auto& tr : s2.transitions) if (tr.to == oldName) tr.to = newName;
    for (auto& tr : sm.anyState) if (tr.to == oldName) tr.to = newName;
    if (sm.entry == oldName) sm.entry = newName;
}
// Where new states go: right of everything placed so far, on a grid.
ImVec2 FreeSpot(const SM& sm) {
    if (sm.states.empty()) return ImVec2(40.0f, 40.0f);
    float x1 = -1e9f, y0 = 1e9f;
    for (const auto& st : sm.states) { x1 = std::max(x1, st.nx); y0 = std::min(y0, st.ny); }
    const int n = (int)sm.states.size();
    return ImVec2(x1 + (n % 3 == 0 ? 240.0f : 0.0f), y0 + (float)(n % 3) * 90.0f);
}
int AddState(Ctx& c, Ui& u, const std::string& clip, ImVec2 at, const std::string& nameHint = "") {
    c.ed.PushUndo();
    SM::State st;
    st.name = UniqueName(*c.sm, nameHint.empty() ? (clip.empty() ? "State" : clip) : nameHint);
    st.clip = clip;
    st.nx = at.x; st.ny = at.y;
    c.sm->states.push_back(std::move(st));
    c.ed.dirty = true;
    const int i = (int)c.sm->states.size() - 1;
    SelectState(u, i);
    u.forceSideTab = 0;
    return i;
}
// Delete states (indices), dropping transitions into them and clearing the entry.
void DeleteStates(Ctx& c, Ui& u, std::vector<int> idx) {
    if (idx.empty()) return;
    c.ed.PushUndo();
    std::sort(idx.begin(), idx.end());
    idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
    std::set<std::string> dead;
    for (int i : idx) if (i >= 0 && i < (int)c.sm->states.size()) dead.insert(c.sm->states[i].name);
    for (auto& s2 : c.sm->states) {
        auto& v = s2.transitions;
        v.erase(std::remove_if(v.begin(), v.end(), [&](const SM::Transition& t) { return dead.count(t.to) > 0; }), v.end());
    }
    auto& av = c.sm->anyState;
    av.erase(std::remove_if(av.begin(), av.end(), [&](const SM::Transition& t) { return dead.count(t.to) > 0; }), av.end());
    if (dead.count(c.sm->entry)) c.sm->entry.clear();
    for (int k = (int)idx.size() - 1; k >= 0; --k)
        if (idx[k] >= 0 && idx[k] < (int)c.sm->states.size()) c.sm->states.erase(c.sm->states.begin() + idx[k]);
    ClearSel(u);
    c.ed.dirty = true;
    ConsoleLog("Animator: deleted " + std::to_string(dead.size()) + " state(s)");
}
// Paste copies of `src` at an offset (or at `at` for the first, keeping their
// layout). Transitions between the pasted states follow the copies; those to
// other existing states stay; the rest are dropped.
void PasteStates(Ctx& c, Ui& u, const std::vector<SM::State>& src, const ImVec2* at = nullptr) {
    if (src.empty()) return;
    c.ed.PushUndo();
    std::vector<std::string> newNames;
    float minX = 1e9f, minY = 1e9f;
    for (const auto& s : src) { minX = std::min(minX, s.nx); minY = std::min(minY, s.ny); }
    std::vector<SM::State> copies = src;
    for (auto& s : copies) {
        s.name = UniqueName(*c.sm, s.name);
        // The copy's name is also taken for the next copies.
        SM::State tmp = s; c.sm->states.push_back(tmp);
        newNames.push_back(s.name);
        if (at) { s.nx = at->x + (s.nx - minX); s.ny = at->y + (s.ny - minY); }
        else { s.nx += 40.0f; s.ny += 40.0f; }
    }
    // Replace the placeholders with the final copies.
    c.sm->states.resize(c.sm->states.size() - copies.size());
    const int first = (int)c.sm->states.size();
    for (std::size_t k = 0; k < copies.size(); ++k) {
        SM::State& s = copies[k];
        for (int t = (int)s.transitions.size() - 1; t >= 0; --t) {
            std::string& to = s.transitions[t].to;
            bool kept = false;
            for (std::size_t j = 0; j < src.size(); ++j)
                if (src[j].name == to) { to = newNames[j]; kept = true; break; }
            if (!kept) for (const auto& o : c.sm->states) if (o.name == to) { kept = true; break; }
            if (!kept) s.transitions.erase(s.transitions.begin() + t);
        }
        c.sm->states.push_back(std::move(s));
    }
    u.sel.clear();
    for (int i = first; i < (int)c.sm->states.size(); ++i) u.sel.push_back(i);
    u.primary = u.sel.empty() ? -1 : u.sel.back();
    u.selTrFrom = -1; u.selTrIdx = -1; u.selAny = -1;
    c.ed.dirty = true;
}
// Columns by distance from the entry (Any State targets count as reached),
// rows in the order states are found; states nothing leads to go last.
void AutoLayout(Ctx& c, Ui& u) {
    SM& sm = *c.sm;
    const int n = (int)sm.states.size();
    if (n == 0) return;
    c.ed.PushUndo();
    std::vector<int> depth(n, -1), order;
    auto idx = [&](const std::string& nm) { for (int i = 0; i < n; ++i) if (sm.states[i].name == nm) return i; return -1; };
    int entry = sm.entry.empty() ? 0 : idx(sm.entry);
    if (entry < 0) entry = 0;
    std::vector<int> queue{entry};
    depth[entry] = 0; order.push_back(entry);
    for (const auto& tr : sm.anyState) { int t = idx(tr.to); if (t >= 0 && depth[t] < 0) { depth[t] = 1; queue.push_back(t); order.push_back(t); } }
    for (std::size_t q = 0; q < queue.size(); ++q) {
        const int i = queue[q];
        for (const auto& tr : sm.states[i].transitions) {
            int t = idx(tr.to);
            if (t >= 0 && depth[t] < 0) { depth[t] = depth[i] + 1; queue.push_back(t); order.push_back(t); }
        }
    }
    int maxDepth = 0;
    for (int d : depth) maxDepth = std::max(maxDepth, d);
    for (int i = 0; i < n; ++i) if (depth[i] < 0) { depth[i] = maxDepth + 1; order.push_back(i); }
    std::vector<int> rows(maxDepth + 2, 0);
    for (int i : order) {
        const int d = depth[i];
        sm.states[i].nx = 60.0f + (float)d * (kNodeW + 70.0f);
        sm.states[i].ny = 40.0f + (float)rows[d]++ * (kNodeH + 44.0f);
    }
    sm.anyX = sm.anyY = 0.0f;   // back to its automatic spot
    c.ed.dirty = true;
    u.frameReq = true;
}
// Where the Any State node sits: its saved spot, else left of the states.
ImVec2 AnyPos(const SM& sm) {
    if (sm.anyX != 0.0f || sm.anyY != 0.0f) return ImVec2(sm.anyX, sm.anyY);
    float x0 = 1e9f, y0 = 1e9f;
    for (const auto& st : sm.states) { x0 = std::min(x0, st.nx); y0 = std::min(y0, st.ny); }
    if (sm.states.empty()) return ImVec2(40.0f, 40.0f);
    return ImVec2(x0 - kAnyW - 80.0f, y0 + 6.0f);
}
// Live float range for a parameter: around the values conditions compare it with.
void ParamRange(const Ctx& c, const std::string& name, float& lo, float& hi) {
    lo = 0.0f; hi = 1.0f;
    auto take = [&](SM::Cond cd, const std::string& p, float v) {
        if (p != name || !IsFloatCond(cd)) return;
        lo = std::min(lo, v); hi = std::max(hi, v);
    };
    for (SM* m : c.machines) {
        for (const auto& tr : m->anyState) { take(tr.cond, tr.param, tr.value); for (const auto& x : tr.extra) take(x.cond, x.param, x.value); }
        for (const auto& st : m->states) {
            for (const auto& tr : st.transitions) { take(tr.cond, tr.param, tr.value); for (const auto& x : tr.extra) take(x.cond, x.param, x.value); }
            if (st.blendTree)
                for (const auto& mo : st.motions) {
                    if (st.blendParam == name) { lo = std::min(lo, mo.threshold); hi = std::max(hi, mo.threshold); }
                    if (st.blendDims == 2 && st.blendParamY == name) { lo = std::min(lo, mo.posY); hi = std::max(hi, mo.posY); }
                }
        }
    }
    const float pad = std::max(1.0f, (hi - lo) * 0.25f);
    lo -= lo < 0.0f ? pad : 0.0f;
    hi += pad;
}

// ---------------------------------------------------------------------------
// Canvas geometry
// ---------------------------------------------------------------------------
ImVec2 Bez(const ImVec2* p, float t) {
    const float v = 1.0f - t;
    return ImVec2(v*v*v*p[0].x + 3*v*v*t*p[1].x + 3*v*t*t*p[2].x + t*t*t*p[3].x,
                  v*v*v*p[0].y + 3*v*v*t*p[1].y + 3*v*t*t*p[2].y + t*t*t*p[3].y);
}
ImVec2 Norm(ImVec2 d) { const float l = std::sqrt(d.x * d.x + d.y * d.y); return l > 1e-5f ? ImVec2(d.x / l, d.y / l) : ImVec2(1, 0); }
float Len(ImVec2 d) { return std::sqrt(d.x * d.x + d.y * d.y); }
// The point on the border of `r` where a ray from its centre along `dir` leaves.
ImVec2 Anchor(const ImRect& r, ImVec2 dir) {
    const ImVec2 c = r.GetCenter();
    const float hw = r.GetWidth() * 0.5f, hh = r.GetHeight() * 0.5f;
    const float sx = std::fabs(dir.x) > 1e-5f ? hw / std::fabs(dir.x) : 1e9f;
    const float sy = std::fabs(dir.y) > 1e-5f ? hh / std::fabs(dir.y) : 1e9f;
    const float s = std::min(sx, sy);
    return ImVec2(c.x + dir.x * s, c.y + dir.y * s);
}
// A curve from rect A to rect B; `bend` (pixels) pushes it sideways so the
// return curve of a two-way pair runs on the other side. A self loop arcs
// above the node.
void EdgeCurve(const ImRect& A, const ImRect& B, float bend, float Z, ImVec2 out[4], ImVec2* nrmOut) {
    const ImVec2 ca = A.GetCenter(), cb = B.GetCenter();
    if (Len(cb - ca) < 1.0f) {   // self loop
        out[0] = ImVec2(ca.x + 28.0f * Z, A.Min.y);
        out[3] = ImVec2(ca.x - 28.0f * Z, A.Min.y);
        out[1] = ImVec2(ca.x + 60.0f * Z, A.Min.y - 70.0f * Z);
        out[2] = ImVec2(ca.x - 60.0f * Z, A.Min.y - 70.0f * Z);
        if (nrmOut) *nrmOut = ImVec2(0, -1);
        return;
    }
    const ImVec2 d = Norm(cb - ca);
    const ImVec2 n(-d.y, d.x);
    ImVec2 a = Anchor(A, d), b = Anchor(B, ImVec2(-d.x, -d.y));
    a = a + n * (bend * 0.5f); b = b + n * (bend * 0.5f);
    const float l = Len(b - a);
    out[0] = a; out[3] = b;
    out[1] = a + d * (l * 0.33f) + n * bend;
    out[2] = b - d * (l * 0.33f) + n * bend;
    if (nrmOut) *nrmOut = n;
}
void Arrowhead(ImDrawList* dl, ImVec2 tip, ImVec2 dir, float size, ImU32 col) {
    const ImVec2 n(-dir.y, dir.x);
    dl->AddTriangleFilled(tip, tip - dir * size + n * (size * 0.5f), tip - dir * size - n * (size * 0.5f), col);
}

// Every drawn edge: one curve per (from, to) pair carrying all its transitions.
struct Edge {
    int from = -1;                 // state index, or -2 for Any State
    int to = -1;
    std::vector<int> trs;          // indices into the source's transition list
    ImVec2 p[4];
    ImVec2 nrm;
    bool hovered = false;
};

// ---------------------------------------------------------------------------
// The canvas
// ---------------------------------------------------------------------------
void DrawCanvas(Ctx& c, Ui& u, ImVec2 size) {
    EditorState& ed = c.ed;
    SM& sm = *c.sm;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::BeginChild("##agcanvas", size, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 org = ImGui::GetCursorScreenPos();
    const ImVec2 csz = ImGui::GetContentRegionAvail();
    const ImRect canvasRect(org, org + csz);
    const bool canvasHovered = ImGui::IsWindowHovered();
    const bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const bool keys = windowFocused && !io.WantTextInput;
    const ImVec2 mouse = io.MousePos;
    int n = (int)sm.states.size();

    // ---- view: wheel zoom about the mouse, frame, find ----
    if (canvasHovered && io.MouseWheel != 0.0f && !io.KeyCtrl) {
        const ImVec2 g = (mouse - org - u.pan) / u.zoom;
        u.zoom = std::clamp(u.zoom * (io.MouseWheel > 0.0f ? 1.12f : 1.0f / 1.12f), 0.25f, 2.5f);
        u.pan = mouse - org - g * u.zoom;
    }
    auto frameAll = [&]() {
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (const auto& st : sm.states) { x0 = std::min(x0, st.nx); y0 = std::min(y0, st.ny); x1 = std::max(x1, st.nx + kNodeW); y1 = std::max(y1, st.ny + kNodeH); }
        const ImVec2 ap = AnyPos(sm);
        x0 = std::min(x0, ap.x); y0 = std::min(y0, ap.y); x1 = std::max(x1, ap.x + kAnyW); y1 = std::max(y1, ap.y + kAnyH);
        if (x1 < x0) return;
        u.zoom = std::clamp(std::min((csz.x - 80.0f) / std::max(x1 - x0, 1.0f), (csz.y - 80.0f) / std::max(y1 - y0, 1.0f)), 0.25f, 1.25f);
        u.pan = ImVec2(csz.x * 0.5f - (x0 + x1) * 0.5f * u.zoom, csz.y * 0.5f - (y0 + y1) * 0.5f * u.zoom);
    };
    if (u.frameReq) { u.frameReq = false; frameAll(); }
    const float Z = u.zoom;
    auto toScreen = [&](ImVec2 g) { return org + u.pan + g * Z; };
    auto toGraph = [&](ImVec2 s) { return (s - org - u.pan) / Z; };
    auto centreOn = [&](ImVec2 g) { u.pan = ImVec2(csz.x * 0.5f - g.x * Z, csz.y * 0.5f - g.y * Z); };
    auto findMatch = [&](const std::string& nm) { return u.find[0] && Lower(nm).find(Lower(u.find)) != std::string::npos; };

    // ---- grid ----
    {
        const ImU32 minor = ImGui::GetColorU32(ImGuiCol_Border, 0.18f), major = ImGui::GetColorU32(ImGuiCol_Border, 0.45f);
        const float step = 25.0f * Z;
        if (step >= 6.0f) {
            int k = (int)std::floor(-u.pan.x / step);
            for (float gx = u.pan.x + (float)k * step; gx < csz.x; gx += step, ++k)
                dl->AddLine(ImVec2(org.x + gx, org.y), ImVec2(org.x + gx, org.y + csz.y), (k % 4 == 0) ? major : minor);
            k = (int)std::floor(-u.pan.y / step);
            for (float gy = u.pan.y + (float)k * step; gy < csz.y; gy += step, ++k)
                dl->AddLine(ImVec2(org.x, org.y + gy), ImVec2(org.x + csz.x, org.y + gy), (k % 4 == 0) ? major : minor);
        }
    }

    // ---- node rectangles ----
    const ImVec2 nodeSz(kNodeW * Z, kNodeH * Z);
    auto nodeRect = [&](int i) { const ImVec2 p = toScreen(ImVec2(sm.states[i].nx, sm.states[i].ny)); return ImRect(p, p + nodeSz); };
    if (sm.anyX == 0.0f && sm.anyY == 0.0f && n > 0) { const ImVec2 ap = AnyPos(sm); sm.anyX = ap.x == 0.0f ? 0.01f : ap.x; sm.anyY = ap.y; }
    const ImVec2 anyScreen = toScreen(AnyPos(sm));
    const ImRect anyRect(anyScreen, anyScreen + ImVec2(kAnyW * Z, kAnyH * Z));
    auto rectOf = [&](int i) { return i == -2 ? anyRect : nodeRect(i); };
    ImFont* font = ImGui::GetFont();
    const float fs = ImGui::GetFontSize() * std::clamp(Z, 0.55f, 1.3f);

    // ---- Play state ----
    int curIdx = -1, fromIdx = -1;
    float trProgress = -1.0f;
    const bool playing = c.playing;
    if (playing) {
        curIdx = c.StateIndex(sm.Current());
        float left = 0.0f;
        if (sm.InTransition(&left)) {
            fromIdx = c.StateIndex(sm.TransitionSource());
            const SM::HistoryEntry* h = sm.History().empty() ? nullptr : &sm.History().back();
            float blend = 0.25f;
            if (h && fromIdx >= 0)
                for (const auto& tr : sm.states[fromIdx].transitions) if (tr.to == h->to) { blend = tr.blend; break; }
            trProgress = blend > 1e-4f ? 1.0f - left / blend : 1.0f;
        }
    }

    // ---- edges: one curve per (from, to) pair ----
    std::vector<Edge> edges;
    auto edgeFor = [&](int from, int to) -> Edge& {
        for (Edge& e : edges) if (e.from == from && e.to == to) return e;
        edges.push_back(Edge{}); edges.back().from = from; edges.back().to = to;
        return edges.back();
    };
    for (int i = 0; i < n; ++i)
        for (int t = 0; t < (int)sm.states[i].transitions.size(); ++t) {
            const int j = c.StateIndex(sm.states[i].transitions[t].to);
            if (j >= 0) edgeFor(i, j).trs.push_back(t);
        }
    for (int t = 0; t < (int)sm.anyState.size(); ++t) {
        const int j = c.StateIndex(sm.anyState[t].to);
        if (j >= 0) edgeFor(-2, j).trs.push_back(t);
    }
    auto hasEdge = [&](int from, int to) { for (const Edge& e : edges) if (e.from == from && e.to == to) return true; return false; };
    int hovEdge = -1;
    float hovEdgeDist = 1e9f;
    for (int ei = 0; ei < (int)edges.size(); ++ei) {
        Edge& e = edges[ei];
        const bool twoWay = e.from >= 0 && e.to >= 0 && e.from != e.to && hasEdge(e.to, e.from);
        EdgeCurve(rectOf(e.from), rectOf(e.to), twoWay ? 22.0f * Z : 0.0f, Z, e.p, &e.nrm);
        if (!canvasHovered) continue;
        for (int s = 0; s <= 24; ++s) {
            const ImVec2 q = Bez(e.p, s / 24.0f);
            const float d2 = (q.x - mouse.x) * (q.x - mouse.x) + (q.y - mouse.y) * (q.y - mouse.y);
            if (d2 < 49.0f && d2 < hovEdgeDist) { hovEdgeDist = d2; hovEdge = ei; }
        }
    }
    // A node under the mouse hides the edges under it.
    u.hovState = -1;
    for (int i = n - 1; i >= 0; --i) if (nodeRect(i).Contains(mouse)) { u.hovState = i; break; }
    const bool anyHovered = anyRect.Contains(mouse);
    if (u.hovState >= 0 || anyHovered || !canvasHovered) hovEdge = -1;
    struct Label { ImVec2 pos; std::string text; ImU32 col; int sev; };
    std::vector<Label> labels;
    for (int ei = 0; ei < (int)edges.size(); ++ei) {
        Edge& e = edges[ei];
        e.hovered = ei == hovEdge;
        const bool any = e.from == -2;
        const SM::Transition* first = nullptr;
        bool selected = false, allMuted = true, fired = false;
        int sev = 0;
        for (int t : e.trs) {
            const SM::Transition& tr = any ? sm.anyState[t] : sm.states[e.from].transitions[t];
            if (!first) first = &tr;
            if (!tr.muted) allMuted = false;
            if (any ? u.selAny == t : (u.selTrFrom == e.from && u.selTrIdx == t)) { selected = true; first = &tr; }
            sev = std::max(sev, c.TransitionSeverity(e.from, t, any));
        }
        if (playing && !sm.History().empty()) {
            const auto& h = sm.History().back();
            fired = h.to == sm.states[e.to].name && sm.Clock() - h.time < 0.7f &&
                    (any ? (c.StateIndex(h.from) < 0 || !hasEdge(c.StateIndex(h.from), e.to) || h.from.empty()) : h.from == sm.states[e.from].name);
        }
        const bool live = playing && !any && e.from == curIdx;
        ImU32 col = selected ? ImGui::GetColorU32(ImVec4(1.0f, 0.8f, 0.2f, 1.0f))
                  : fired ? FiredCol()
                  : e.hovered ? ImGui::GetColorU32(ImGuiCol_Text)
                  : allMuted ? ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.3f)
                  : live ? PlayCol(0.9f)
                  : any ? AnyCol(0.75f)
                  : ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.9f);
        const float th = (selected || fired || live) ? 3.0f : e.hovered ? 2.5f : 1.8f;
        dl->AddBezierCubic(e.p[0], e.p[1], e.p[2], e.p[3], col, th);
        // Direction: the head at the target, a chevron half way.
        const ImVec2 tip = e.p[3], dirEnd = Norm(e.p[3] - e.p[2]);
        Arrowhead(dl, tip, dirEnd, std::max(7.0f, 11.0f * Z), col);
        const ImVec2 mid = Bez(e.p, 0.5f), dirMid = Norm(Bez(e.p, 0.53f) - Bez(e.p, 0.47f));
        Arrowhead(dl, mid + dirMid * (4.0f * Z), dirMid, std::max(6.0f, 9.0f * Z), col);
        if (e.trs.size() > 1) {   // more than one transition on this pair
            const std::string cnt = "x" + std::to_string(e.trs.size());
            const ImVec2 ts = font->CalcTextSizeA(fs * 0.8f, FLT_MAX, 0.0f, cnt.c_str());
            const ImVec2 at = mid - e.nrm * (10.0f * Z) - ImVec2(ts.x * 0.5f, ts.y * 0.5f);
            dl->AddRectFilled(at - ImVec2(3, 1), at + ts + ImVec2(3, 1), ImGui::GetColorU32(ImGuiCol_WindowBg, 0.9f), 3.0f);
            dl->AddText(font, fs * 0.8f, at, col, cnt.c_str());
        }
        // The blend in progress: a dot travelling from the source to the target.
        if (playing && trProgress >= 0.0f && !any && e.from == fromIdx && e.to == curIdx)
            dl->AddCircleFilled(Bez(e.p, trProgress), std::max(3.0f, 5.0f * Z), PlayCol());
        if (first && ((u.labels && Z >= 0.55f) || selected || e.hovered)) {
            std::string lb = TransitionLabel(*first);
            if (e.trs.size() > 1 && !selected) lb += "  (+" + std::to_string(e.trs.size() - 1) + ")";
            const float lfs = fs * 0.82f;
            const ImVec2 ts = font->CalcTextSizeA(lfs, FLT_MAX, 0.0f, lb.c_str());
            const ImVec2 lp = mid + e.nrm * (9.0f + ts.y * 0.5f) - ImVec2(ts.x * 0.5f, ts.y * 0.5f);
            labels.push_back({lp, lb, (selected || e.hovered || fired) ? col : ImGui::GetColorU32(ImGuiCol_TextDisabled), sev});
        }
    }
    // The transition being drawn follows the mouse.
    if (u.pendingFrom >= -2 && u.pendingFrom != -1 && (u.pendingFrom == -2 || u.pendingFrom < n)) {
        const ImVec2 from = rectOf(u.pendingFrom).GetCenter();
        dl->AddLine(from, mouse, ImGui::GetColorU32(ImVec4(1.0f, 0.8f, 0.2f, 1.0f)), 2.0f);
        Arrowhead(dl, mouse, Norm(mouse - from), 10.0f, ImGui::GetColorU32(ImVec4(1.0f, 0.8f, 0.2f, 1.0f)));
    }

    // ---- nodes ----
    bool itemHovered = false;
    int ctxNode = -1;   // right-click target this frame
    static int s_ctxNode = -1;
    const ImVec4 tint = LayerTint(sm.layer);
    const int entryIdx = sm.entry.empty() ? (n > 0 ? 0 : -1) : c.StateIndex(sm.entry);
    auto completePending = [&](int target) {
        ed.PushUndo();
        SM::Transition tr;
        tr.to = sm.states[target].name;
        if (u.pendingFrom == -2) {
            sm.anyState.push_back(tr);
            SelectAny(u, (int)sm.anyState.size() - 1);
        } else {
            tr.cond = SM::Cond::Trigger;
            sm.states[u.pendingFrom].transitions.push_back(tr);
            SelectTransition(u, u.pendingFrom, (int)sm.states[u.pendingFrom].transitions.size() - 1);
        }
        u.pendingFrom = -1;
        ed.dirty = true;
    };
    for (int i = 0; i < n; ++i) {
        SM::State& st = sm.states[i];
        const ImRect r = nodeRect(i);
        if (!canvasRect.Overlaps(r)) continue;
        ImGui::SetCursorScreenPos(r.Min);
        ImGui::PushID(i);
        ImGui::InvisibleButton("##node", nodeSz);
        const bool hov = ImGui::IsItemHovered();
        itemHovered |= hov;
        static bool s_dragMoved = false;
        if (ImGui::IsItemActivated()) {
            if (u.pendingFrom != -1) completePending(i);
            else {
                if (io.KeyShift || io.KeyCtrl) SelectState(u, i, 2);
                else if (!IsSel(u, i)) SelectState(u, i);
                else { u.primary = i; u.selTrFrom = -1; u.selTrIdx = -1; u.selAny = -1; }
                u.forceSideTab = 0;
                u.dragPushed = false; s_dragMoved = false;
            }
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f) && u.pendingFrom == -1 && u.renaming != i) {
            if (!u.dragPushed) { ed.PushUndo(); u.dragPushed = true; }
            const ImVec2 d = io.MouseDelta / Z;
            if (IsSel(u, i)) for (int k : u.sel) { sm.states[k].nx += d.x; sm.states[k].ny += d.y; }
            else { st.nx += d.x; st.ny += d.y; }
            s_dragMoved = true;
            ed.dirty = true;
        }
        if (ImGui::IsItemDeactivated() && !s_dragMoved && !(io.KeyShift || io.KeyCtrl) && u.sel.size() > 1 && IsSel(u, i))
            SelectState(u, i);   // a plain click on one of several selected: just it
        if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && u.pendingFrom == -1) {
            u.renaming = i; u.renameFocus = true;
            std::snprintf(u.renameBuf, sizeof(u.renameBuf), "%s", st.name.c_str());
        }
        if (hov && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && Len(mouse - u.rightDown) < 4.0f) {
            if (u.pendingFrom != -1) u.pendingFrom = -1;
            else { if (!IsSel(u, i)) SelectState(u, i); ctxNode = i; }
        }
        ImGui::PopID();

        // ---- visuals ----
        const bool isEntry = i == entryIdx;
        const bool isCurrent = playing && i == curIdx;
        const bool isFrom = playing && i == fromIdx && trProgress >= 0.0f;
        const bool isSel = IsSel(u, i);
        const bool found = findMatch(st.name);
        const int sev = i < (int)c.stateSeverity.size() ? c.stateSeverity[i] : 0;
        ImU32 fill = isCurrent ? PlayCol(0.92f) : ImGui::GetColorU32(hov || isSel ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
        dl->AddRectFilled(r.Min, r.Max, fill, 6.0f * Z);
        // Layer tint on the title strip; an additive layer's nodes carry a "+".
        if (!isCurrent)
            dl->AddRectFilled(r.Min, ImVec2(r.Max.x, r.Min.y + std::max(3.0f, 4.0f * Z)), ImGui::GetColorU32(ImVec4(tint.x, tint.y, tint.z, 0.9f)), 6.0f * Z, ImDrawFlags_RoundCornersTop);
        if (isFrom) dl->AddRect(r.Min, r.Max, PlayCol(0.9f * (1.0f - trProgress)), 6.0f * Z, 0, 3.0f);
        ImU32 border = isSel ? ImGui::GetColorU32(AccentCol()) : isEntry ? EntryCol() : ImGui::GetColorU32(ImGuiCol_Border);
        dl->AddRect(r.Min, r.Max, border, 6.0f * Z, 0, isSel ? 2.5f : isEntry ? 2.0f : 1.0f);
        if (found) dl->AddRect(r.Min - ImVec2(4, 4), r.Max + ImVec2(4, 4), Col(0.30f, 0.85f, 1.0f, 0.9f), 8.0f * Z, 0, 2.0f);
        if (isCurrent) {   // pulsing halo
            const float pulse = 0.5f + 0.5f * std::sin((float)ImGui::GetTime() * 5.0f);
            dl->AddRect(r.Min - ImVec2(3, 3), r.Max + ImVec2(3, 3), PlayCol(0.30f + 0.45f * pulse), 8.0f * Z, 0, 3.0f);
        }
        if (isEntry) {   // the entry badge: a play triangle on the left edge
            const ImVec2 m(r.Min.x - 1.0f, r.GetCenter().y);
            dl->AddTriangleFilled(ImVec2(m.x - 10.0f * Z, m.y - 7.0f * Z), ImVec2(m.x - 10.0f * Z, m.y + 7.0f * Z), m, EntryCol());
        }
        const ImU32 txt = isCurrent ? Col(0.08f, 0.08f, 0.08f) : ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32 sub = isCurrent ? Col(0.18f, 0.18f, 0.18f) : ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const ImVec4 clip(r.Min.x, r.Min.y, r.Max.x - 4.0f, r.Max.y);
        if (u.renaming != i)
            dl->AddText(font, fs * 1.05f, ImVec2(r.Min.x + 10 * Z, r.Min.y + 7 * Z), txt, st.name.c_str(), nullptr, 0.0f, &clip);
        std::string line;
        if (st.blendTree) {
            line = st.blendDims == 2 ? "2D blend: " + (st.blendParam.empty() ? std::string("?") : st.blendParam) + " / " + (st.blendParamY.empty() ? std::string("?") : st.blendParamY)
                                     : "Blend: " + (st.blendParam.empty() ? std::string("?") : st.blendParam);
            line += " (" + std::to_string(st.motions.size()) + ")";
        } else line = st.clip.empty() ? (sm.layer > 0 ? "(plays nothing)" : "(no clip)") : st.clip;
        if (st.mirror) line += " \xC2\xB7 mirrored";
        if (st.ikWeight < 0.999f) line += " \xC2\xB7 IK " + std::to_string((int)std::lround(st.ikWeight * 100.0f)) + "%";
        if (!st.loop && !st.blendTree) line += " \xC2\xB7 once";
        dl->AddText(font, fs * 0.9f, ImVec2(r.Min.x + 10 * Z, r.Min.y + 27 * Z), sub, line.c_str(), nullptr, 0.0f, &clip);
        // Badges, top right: events, additive layer, problems.
        float bx = r.Max.x - 9.0f * Z;
        if (!st.enterEvent.empty() || !st.exitEvent.empty()) {
            dl->AddCircleFilled(ImVec2(bx, r.Min.y + 11.0f * Z), 3.5f * Z, Col(0.95f, 0.55f, 0.85f));
            bx -= 11.0f * Z;
        }
        if (sm.additive) {
            dl->AddText(font, fs * 0.9f, ImVec2(bx - 4.0f * Z, r.Min.y + 5.0f * Z), ImGui::GetColorU32(ImVec4(tint.x, tint.y, tint.z, 1.0f)), "+");
            bx -= 11.0f * Z;
        }
        if (sev > 0) {
            const ImVec2 gc(bx, r.Min.y + 11.0f * Z);
            DrawWarnGlyph(dl, gc, 6.5f * std::max(Z, 0.7f), sev >= 2 ? ErrCol() : WarnCol());
            if (hov && Len(mouse - gc) < 10.0f) ImGui::SetTooltip("%s", c.StateIssues(i).c_str());
        }
        if (isCurrent) {   // progress through the state's clip (per loop)
            float nt = sm.NormalizedTime(); nt -= std::floor(nt);
            dl->AddRectFilled(ImVec2(r.Min.x + 6, r.Max.y - 6), ImVec2(r.Min.x + 6 + (r.GetWidth() - 12) * nt, r.Max.y - 3), Col(0.1f, 0.1f, 0.1f, 0.85f), 1.0f);
        }
        if (hov && u.renaming != i && sev == 0 && u.pendingFrom == -1 && !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            ImGui::GetCurrentContext()->HoveredIdTimer > 0.6f) {
            std::string tip = st.name + "\n" + line;
            if (isEntry) tip += "\nEntry state";
            tip += "\n\nDrag to move \xC2\xB7 double-click to rename \xC2\xB7 right-click for more";
            ImGui::SetTooltip("%s", tip.c_str());
        }
        // Inline rename.
        if (u.renaming == i) {
            ImGui::SetCursorScreenPos(ImVec2(r.Min.x + 6 * Z, r.Min.y + 4 * Z));
            ImGui::SetNextItemWidth(nodeSz.x - 12 * Z);
            if (u.renameFocus) { ImGui::SetKeyboardFocusHere(); u.renameFocus = false; }
            ImGui::PushID(i);
            const bool enter = ImGui::InputText("##rename", u.renameBuf, sizeof(u.renameBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            const bool done = ImGui::IsItemDeactivated();
            ImGui::PopID();
            itemHovered |= ImGui::IsItemHovered();
            if (enter || done) {
                const std::string nn = u.renameBuf;
                if (!ImGui::IsKeyPressed(ImGuiKey_Escape) && !nn.empty() && nn != st.name) {
                    ed.PushUndo();
                    RenameState(sm, i, UniqueName(sm, nn, i));
                    ed.dirty = true;
                }
                u.renaming = -1;
            }
        }
    }

    // ---- the Any State node ----
    {
        ImGui::SetCursorScreenPos(anyRect.Min);
        ImGui::InvisibleButton("##anynode", anyRect.GetSize());
        const bool hov = ImGui::IsItemHovered();
        itemHovered |= hov;
        if (ImGui::IsItemActivated()) { u.anyDragPushed = false; if (u.pendingFrom != -1 && u.pendingFrom != -2) u.pendingFrom = -1; }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
            if (!u.anyDragPushed) { ed.PushUndo(); u.anyDragPushed = true; }
            const ImVec2 cur = AnyPos(sm) + io.MouseDelta / Z;
            sm.anyX = cur.x == 0.0f ? 0.01f : cur.x; sm.anyY = cur.y;
            ed.dirty = true;
        }
        if (hov && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && Len(mouse - u.rightDown) < 4.0f) {
            if (u.pendingFrom != -1) u.pendingFrom = -1; else ImGui::OpenPopup("##aganyctx");
        }
        if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) u.pendingFrom = -2;
        const bool sel = u.selAny >= 0;
        dl->AddRectFilled(anyRect.Min, anyRect.Max, AnyCol(hov ? 0.35f : 0.22f), 20.0f * Z);
        dl->AddRect(anyRect.Min, anyRect.Max, sel ? ImGui::GetColorU32(AccentCol()) : AnyCol(0.9f), 20.0f * Z, 0, sel ? 2.5f : 1.5f);
        const char* lbl = "Any State";
        const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, lbl);
        dl->AddText(font, fs, anyRect.GetCenter() - ts * 0.5f, ImGui::GetColorU32(ImGuiCol_Text), lbl);
        if (c.anySeverity > 0) DrawWarnGlyph(dl, ImVec2(anyRect.Max.x - 9.0f * Z, anyRect.Min.y + 10.0f * Z), 6.5f * std::max(Z, 0.7f), c.anySeverity >= 2 ? ErrCol() : WarnCol());
        if (hov && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            ImGui::SetTooltip("Any State: its transitions are tested from every state, before\nthe state's own.\n\nDouble-click to add a transition, drag to move.");
        if (ImGui::BeginPopup("##aganyctx")) {
            if (ImGui::MenuItem("Add transition from Any State")) u.pendingFrom = -2;
            ImGui::Separator();
            for (int t = 0; t < (int)sm.anyState.size(); ++t) {
                const std::string lb = "Any -> " + sm.anyState[t].to + "  " + ConditionsText(sm.anyState[t]) + "##a" + std::to_string(t);
                if (ImGui::MenuItem(lb.c_str())) SelectAny(u, t);
            }
            ImGui::EndPopup();
        }
    }

    // ---- labels over the nodes, warning glyph beside a transition with a problem ----
    for (const Label& l : labels) {
        const float lfs = fs * 0.82f;
        const ImVec2 ts = font->CalcTextSizeA(lfs, FLT_MAX, 0.0f, l.text.c_str());
        dl->AddRectFilled(l.pos - ImVec2(3, 1), l.pos + ts + ImVec2(3, 1), ImGui::GetColorU32(ImGuiCol_WindowBg, 0.88f), 3.0f);
        dl->AddText(font, lfs, l.pos, l.col, l.text.c_str());
        if (l.sev > 0) DrawWarnGlyph(dl, ImVec2(l.pos.x - 9.0f, l.pos.y + ts.y * 0.5f), 5.5f, l.sev >= 2 ? ErrCol() : WarnCol());
    }

    // ---- node context menu ----
    if (ctxNode >= 0) { s_ctxNode = ctxNode; ImGui::OpenPopup("##agnodectx"); }
    if (ImGui::BeginPopup("##agnodectx")) {
        const int i = s_ctxNode;
        if (i < 0 || i >= n) ImGui::CloseCurrentPopup();
        else {
            SM::State& st = sm.states[i];
            ImGui::TextDisabled("%s", st.name.c_str());
            if (ImGui::MenuItem("Add transition from here")) { u.pendingFrom = i; }
            if (ImGui::MenuItem("Set as entry state", nullptr, i == entryIdx)) { ed.PushUndo(); sm.entry = st.name; ed.dirty = true; }
            if (ImGui::MenuItem("Rename", "double-click")) { u.renaming = i; u.renameFocus = true; std::snprintf(u.renameBuf, sizeof(u.renameBuf), "%s", st.name.c_str()); }
            ImGui::Separator();
            const std::string what = u.sel.size() > 1 && IsSel(u, i) ? std::to_string(u.sel.size()) + " states" : "state";
            if (ImGui::MenuItem(("Copy " + what).c_str(), "Ctrl+C")) {
                u.clipboard.clear();
                if (IsSel(u, i)) for (int k : u.sel) u.clipboard.push_back(sm.states[k]); else u.clipboard.push_back(st);
            }
            if (ImGui::MenuItem(("Duplicate " + what).c_str(), "Ctrl+D")) {
                std::vector<SM::State> src;
                if (IsSel(u, i)) for (int k : u.sel) src.push_back(sm.states[k]); else src.push_back(st);
                PasteStates(c, u, src);
            }
            if (ImGui::MenuItem(("Delete " + what).c_str(), "Del")) {
                std::vector<int> idx = IsSel(u, i) ? u.sel : std::vector<int>{i};
                DeleteStates(c, u, idx);
                n = (int)sm.states.size();
            }
        }
        ImGui::EndPopup();
    }

    // ---- minimap ----
    const bool spaceHeld = keys && ImGui::IsKeyDown(ImGuiKey_Space);
    if (u.minimap && csz.x > 360.0f && csz.y > 220.0f && (n > 0)) {
        const ImVec2 msz(170.0f, 110.0f);
        const ImVec2 m0(org.x + csz.x - msz.x - 10.0f, org.y + csz.y - msz.y - 10.0f);
        const ImRect mr(m0, m0 + msz);
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (const auto& st : sm.states) { x0 = std::min(x0, st.nx); y0 = std::min(y0, st.ny); x1 = std::max(x1, st.nx + kNodeW); y1 = std::max(y1, st.ny + kNodeH); }
        const ImVec2 ap = AnyPos(sm);
        x0 = std::min(x0, ap.x); y0 = std::min(y0, ap.y); x1 = std::max(x1, ap.x + kAnyW); y1 = std::max(y1, ap.y + kAnyH);
        // The view rectangle counts too, so the map never loses it.
        const ImVec2 v0 = toGraph(org), v1 = toGraph(org + csz);
        x0 = std::min(x0, v0.x); y0 = std::min(y0, v0.y); x1 = std::max(x1, v1.x); y1 = std::max(y1, v1.y);
        const float s = std::min((msz.x - 10.0f) / std::max(x1 - x0, 1.0f), (msz.y - 10.0f) / std::max(y1 - y0, 1.0f));
        const ImVec2 off(m0.x + (msz.x - (x1 - x0) * s) * 0.5f, m0.y + (msz.y - (y1 - y0) * s) * 0.5f);
        auto toMap = [&](ImVec2 g) { return ImVec2(off.x + (g.x - x0) * s, off.y + (g.y - y0) * s); };
        ImGui::SetCursorScreenPos(m0);
        ImGui::InvisibleButton("##agminimap", msz);
        const bool mhov = ImGui::IsItemHovered();
        itemHovered |= mhov;
        if (ImGui::IsItemActive()) {
            const ImVec2 g((mouse.x - off.x) / s + x0, (mouse.y - off.y) / s + y0);
            centreOn(g);
        }
        dl->AddRectFilled(mr.Min, mr.Max, ImGui::GetColorU32(ImGuiCol_PopupBg, mhov ? 0.95f : 0.8f), 4.0f);
        dl->AddRect(mr.Min, mr.Max, ImGui::GetColorU32(ImGuiCol_Border), 4.0f);
        for (int i = 0; i < n; ++i) {
            const ImVec2 a = toMap(ImVec2(sm.states[i].nx, sm.states[i].ny)), b = toMap(ImVec2(sm.states[i].nx + kNodeW, sm.states[i].ny + kNodeH));
            dl->AddRectFilled(a, b, (playing && i == curIdx) ? PlayCol() : IsSel(u, i) ? ImGui::GetColorU32(AccentCol()) : ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.7f), 1.0f);
        }
        dl->AddRectFilled(toMap(ap), toMap(ap + ImVec2(kAnyW, kAnyH)), AnyCol(0.7f), 4.0f);
        dl->AddRect(toMap(v0), toMap(v1), ImGui::GetColorU32(ImGuiCol_Text, 0.8f), 1.0f);
        if (mhov) ImGui::SetTooltip("Minimap: click or drag to move the view");
    }

    // ---- canvas-level input: select edges, box select, pan, context menu ----
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) u.rightDown = mouse;
    const bool empty = canvasHovered && !itemHovered && u.hovState < 0 && !anyHovered;
    if (empty && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !spaceHeld) {
        if (u.pendingFrom != -1) u.pendingFrom = -1;
        else if (hovEdge >= 0) {
            // Click an arrow: its first transition; again cycles through the bundle.
            const Edge& e = edges[hovEdge];
            int pick = e.trs.front();
            for (std::size_t k = 0; k < e.trs.size(); ++k) {
                const bool isSel = e.from == -2 ? u.selAny == e.trs[k] : (u.selTrFrom == e.from && u.selTrIdx == e.trs[k]);
                if (isSel) { pick = e.trs[(k + 1) % e.trs.size()]; break; }
            }
            if (e.from == -2) SelectAny(u, pick); else SelectTransition(u, e.from, pick);
        } else { u.boxing = true; u.boxStart = mouse; }
    }
    if (empty && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hovEdge < 0 && !spaceHeld) {
        u.boxing = false;
        AddState(c, u, c.clips.empty() ? std::string() : c.clips.front(), toGraph(mouse) - ImVec2(kNodeW * 0.5f, kNodeH * 0.5f));
        n = (int)sm.states.size();
    }
    if (u.boxing) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const ImRect box(ImMin(u.boxStart, mouse), ImMax(u.boxStart, mouse));
            if (box.GetWidth() > 3.0f || box.GetHeight() > 3.0f) {
                dl->AddRectFilled(box.Min, box.Max, ImGui::GetColorU32(AccentCol(0.15f)));
                dl->AddRect(box.Min, box.Max, ImGui::GetColorU32(AccentCol(0.9f)));
            }
        } else {
            const ImRect box(ImMin(u.boxStart, mouse), ImMax(u.boxStart, mouse));
            const bool add = io.KeyShift || io.KeyCtrl;
            if (box.GetWidth() > 3.0f || box.GetHeight() > 3.0f) {
                if (!add) ClearSel(u);
                for (int i = 0; i < n; ++i) if (box.Overlaps(nodeRect(i))) SelectState(u, i, 1);
                if (!u.sel.empty()) u.forceSideTab = 0;
            } else if (!add) ClearSel(u);
            u.boxing = false;
        }
    }
    // Pan: middle drag, right drag, or Space + left drag.
    const bool panDrag = canvasHovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 1.0f) ||
                                           ImGui::IsMouseDragging(ImGuiMouseButton_Right, 4.0f) ||
                                           (spaceHeld && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f)));
    if (panDrag || (u.panning && (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Left)))) {
        u.pan = u.pan + io.MouseDelta;
        u.panning = true;
        u.boxing = false;
    } else u.panning = false;
    if (empty && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && Len(mouse - u.rightDown) < 4.0f) {
        if (u.pendingFrom != -1) u.pendingFrom = -1;
        else ImGui::OpenPopup("##agcanvasctx");
    }
    if (ImGui::BeginPopup("##agcanvasctx")) {
        const ImVec2 at = toGraph(ImGui::GetMousePosOnOpeningCurrentPopup());
        if (ImGui::MenuItem("Add state here")) { AddState(c, u, c.clips.empty() ? std::string() : c.clips.front(), at); n = (int)sm.states.size(); }
        if (ImGui::BeginMenu("Add state for clip", !c.slotNames.empty())) {
            for (const std::string& cn : c.slotNames)
                if (ImGui::MenuItem(cn.c_str())) { AddState(c, u, cn, at); n = (int)sm.states.size(); }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Paste", "Ctrl+V", false, !u.clipboard.empty())) PasteStates(c, u, u.clipboard, &at);
        if (ImGui::MenuItem("Add transition from Any State")) u.pendingFrom = -2;
        ImGui::Separator();
        if (ImGui::MenuItem("Select all", "Ctrl+A")) { ClearSel(u); for (int i = 0; i < n; ++i) SelectState(u, i, 1); }
        if (ImGui::MenuItem("Frame all", "F")) u.frameReq = true;
        if (ImGui::MenuItem("Reset zoom", "Ctrl+0")) { const ImVec2 g = toGraph(org + csz * 0.5f); u.zoom = 1.0f; centreOn(g); }
        if (ImGui::MenuItem("Auto layout")) AutoLayout(c, u);
        ImGui::EndPopup();
    }
    // A clip dragged from the Clips tab makes a state where it lands.
    if (ImGui::BeginDragDropTargetCustom(canvasRect, ImGui::GetID("##agdrop"))) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("AG_CLIP")) {
            const char* data = (const char*)pl->Data;
            std::size_t len = 0;
            while (len < (std::size_t)pl->DataSize && data[len]) ++len;
            std::string cn(data, len);
            AddState(c, u, cn, toGraph(mouse) - ImVec2(kNodeW * 0.5f, kNodeH * 0.5f));
            n = (int)sm.states.size();
        }
        ImGui::EndDragDropTarget();
    }

    // ---- keyboard ----
    if (keys) {
        const bool ctrl = io.KeyCtrl;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            if (u.pendingFrom != -1) u.pendingFrom = -1;
            else if (u.renaming >= 0) u.renaming = -1;
            else ClearSel(u);
        }
        if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) u.frameReq = true;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_0, false)) { const ImVec2 g = toGraph(org + csz * 0.5f); u.zoom = 1.0f; centreOn(g); }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) { ClearSel(u); for (int i = 0; i < n; ++i) SelectState(u, i, 1); }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && !u.sel.empty()) {
            u.clipboard.clear();
            for (int k : u.sel) if (k >= 0 && k < n) u.clipboard.push_back(sm.states[k]);
        }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false) && !u.clipboard.empty()) {
            const ImVec2 at = canvasHovered ? toGraph(mouse) : toGraph(org + csz * 0.5f);
            PasteStates(c, u, u.clipboard, &at);
            n = (int)sm.states.size();
        }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false) && !u.sel.empty()) {
            std::vector<SM::State> src;
            for (int k : u.sel) if (k >= 0 && k < n) src.push_back(sm.states[k]);
            PasteStates(c, u, src);
            n = (int)sm.states.size();
        }
        if ((ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) && u.renaming < 0) {
            if (!u.sel.empty()) { DeleteStates(c, u, u.sel); n = (int)sm.states.size(); }
            else if (u.selTrFrom >= 0 && u.selTrFrom < n && u.selTrIdx >= 0 && u.selTrIdx < (int)sm.states[u.selTrFrom].transitions.size()) {
                ed.PushUndo();
                sm.states[u.selTrFrom].transitions.erase(sm.states[u.selTrFrom].transitions.begin() + u.selTrIdx);
                u.selTrFrom = -1; u.selTrIdx = -1; ed.dirty = true;
            } else if (u.selAny >= 0 && u.selAny < (int)sm.anyState.size()) {
                ed.PushUndo();
                sm.anyState.erase(sm.anyState.begin() + u.selAny);
                u.selAny = -1; ed.dirty = true;
            }
        }
        // Arrow keys nudge the selection (Shift: by a grid step).
        const float step = io.KeyShift ? 25.0f : 1.0f;
        ImVec2 nudge(0, 0);
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) nudge.x -= step;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) nudge.x += step;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) nudge.y -= step;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) nudge.y += step;
        if ((nudge.x != 0.0f || nudge.y != 0.0f) && !u.sel.empty()) {
            ed.PushUndo();
            for (int k : u.sel) if (k >= 0 && k < n) { sm.states[k].nx += nudge.x; sm.states[k].ny += nudge.y; }
            ed.dirty = true;
        }
    }

    // ---- status line (bottom left) ----
    {
        std::string s;
        if (u.pendingFrom != -1) s = "Click the target state to connect the transition (right-click or Esc cancels)";
        else if (playing) {
            float left = 0.0f;
            s = "Playing: " + (sm.Current().empty() ? std::string("(entering)") : sm.Current());
            if (sm.InTransition(&left)) s += "  \xC2\xB7  blending from " + sm.TransitionSource() + Fmt(" (%.2fs left)", left);
            if (sm.Interruptions() > 0) s += Fmt("  \xC2\xB7  %g interrupted", (double)sm.Interruptions());
        } else if (!u.sel.empty() && u.sel.size() > 1) s = std::to_string(u.sel.size()) + " states selected  \xC2\xB7  drag to move, Delete, Ctrl+C / Ctrl+D";
        else s = "drag: move  \xC2\xB7  drag empty: box select  \xC2\xB7  wheel: zoom  \xC2\xB7  middle / right / Space drag: pan  \xC2\xB7  double-click: rename / add";
        const float ofs = ImGui::GetFontSize() * 0.92f;
        const ImVec2 ts = font->CalcTextSizeA(ofs, FLT_MAX, 0.0f, s.c_str());
        const ImVec2 at(org.x + 8.0f, org.y + csz.y - ts.y - 6.0f);
        dl->AddRectFilled(at - ImVec2(4, 2), at + ts + ImVec2(4, 2), ImGui::GetColorU32(ImGuiCol_WindowBg, 0.75f), 3.0f);
        dl->AddText(font, ofs, at, playing ? PlayCol() : ImGui::GetColorU32(ImGuiCol_TextDisabled), s.c_str());
        const std::string zs = std::to_string((int)std::lround(u.zoom * 100.0f)) + "%";
        const ImVec2 zts = font->CalcTextSizeA(ofs, FLT_MAX, 0.0f, zs.c_str());
        dl->AddText(font, ofs, ImVec2(org.x + csz.x - zts.x - 10.0f, org.y + 6.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), zs.c_str());
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Inspector widgets
// ---------------------------------------------------------------------------
// A clip slot: every clip the machine can play, searchable once the list is
// long. Names that cannot play here are marked. Pushes undo itself.
bool ClipCombo(Ctx& c, const char* label, std::string& value, const char* noneLabel = nullptr) {
    bool changed = false;
    const bool missing = !value.empty() && !c.Playable(value);
    std::string shown = value.empty() ? (noneLabel ? noneLabel : "(none)") : value;
    if (missing) shown += "  (missing)";
    if (missing) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.75f, 0.25f, 1.0f));
    const bool open = ImGui::BeginCombo(label, shown.c_str());
    if (missing) ImGui::PopStyleColor();
    if (open) {
        static char s_filter[48] = "";
        if (ImGui::IsWindowAppearing()) { s_filter[0] = 0; ImGui::SetKeyboardFocusHere(); }
        if (c.slotNames.size() > 10) {
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##cf", "filter", s_filter, sizeof(s_filter));
        }
        auto pick = [&](const std::string& n) {
            EditPush(c.ed);
            value = n; c.ed.dirty = true; changed = true;
        };
        if (noneLabel && ImGui::Selectable(noneLabel, value.empty())) pick(std::string());
        if (!value.empty() && missing && ImGui::Selectable((value + "  (missing)").c_str(), true)) {}
        for (const std::string& n : c.slotNames) {
            if (s_filter[0] && Lower(n).find(Lower(s_filter)) == std::string::npos) continue;
            const bool playable = c.Playable(n);
            if (!playable) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::Selectable(n.c_str(), n == value)) pick(n);
            if (!playable) ImGui::PopStyleColor();
            if (!playable && ImGui::IsItemHovered()) ImGui::SetTooltip("Not in this Animator's library: it plays only if a child model has it.");
        }
        ImGui::EndCombo();
    }
    return changed;
}
// A parameter slot: declared parameters of the wanted type, or free text when
// none is declared yet.
bool ParamPicker(Ctx& c, const char* id, std::string& param, int wantType) {
    bool changed = false;
    bool anyOfType = false;
    for (const auto& p : c.base->params) if (p.type == wantType) { anyOfType = true; break; }
    const bool undeclared = !param.empty() && !c.ParamDeclared(param);
    if (anyOfType) {
        std::string shown = param.empty() ? "(pick)" : param;
        if (undeclared) shown += "  (not declared)";
        if (ImGui::BeginCombo(id, shown.c_str())) {
            for (const auto& p : c.base->params)
                if (p.type == wantType && ImGui::Selectable(p.name.c_str(), param == p.name)) { EditPush(c.ed); param = p.name; c.ed.dirty = true; changed = true; }
            if (undeclared && ImGui::Selectable(("Declare \"" + param + "\"").c_str())) {
                c.ed.PushUndo(); c.base->params.push_back({param, wantType}); c.ed.dirty = true;
            }
            ImGui::EndCombo();
        }
    } else {
        changed = TextEdit(c.ed, id, param, "parameter name");
    }
    return changed;
}
// The clip a state's preview starts with (the middle motion of a blend tree).
std::string PreviewClip(const Ctx& c, const SM::State& st) {
    std::string clip = apreview::MiddleMotion(st);
    if (!c.Playable(clip) && c.Playable(st.fallbackClip)) clip = st.fallbackClip;
    return clip;
}
float ClipSeconds(const Ctx& c, const std::string& clip) {
    if (c.ma) { const int i = c.ma->FindClip(clip); return i >= 0 ? c.ma->ClipLength(i) : 0.0f; }
    if (c.body) if (const AnimClip* k = apreview::BodyClip(c.body, clip)) return k->Duration();
    return 0.0f;
}
// Preview / Stop for a state, with the transport while it runs.
void StatePreview(Ctx& c, const SM::State& st) {
    const std::string clip = PreviewClip(c, st);
    const bool can = c.body ? apreview::BodyClip(c.body, clip) != nullptr : (c.ma && c.ma->FindClip(clip) >= 0);
    if (!can) {
        ImGui::BeginDisabled();
        ImGui::SmallButton("Preview##agprev");
        ImGui::EndDisabled();
        Tip("Nothing to preview: the clip is not in this Animator's library.");
        return;
    }
    bool on = false;
    if (c.body) on = apreview::ToggleCharacterState(c.ed.scene(), c.body, st, c.playing, "agprev");
    else on = apreview::ToggleModelState(c.ed.scene(), c.ma, st, c.playing);
    Tip("Play this state on the character in the Scene view, without entering Play.\nStopping puts the pose back.");
    if (on) apreview::DrawTransport("agprevt");
}
// Load a clip file straight into a state's slot.
void ImportIntoSlot(Ctx& c, SM::State& st, std::string& slot) {
    if (c.ma) {
        std::vector<std::string> before = c.ma->ClipNames();
        AddAnimationsFromFilesUI(c.ed, c.obj);
        for (const auto& n : c.ma->ClipNames()) {
            bool had = false;
            for (const auto& b : before) if (b == n) { had = true; break; }
            if (!had) { slot = n; c.ed.dirty = true; ConsoleLog("Imported '" + n + "' into " + st.name); break; }
        }
    } else if (c.body) {
        const char* filt[] = {"*.okayanim"};
        if (const char* p = tinyfd_openFileDialog("Import clip into this state", "", 1, filt, "OkaySpace animation", 0)) {
            c.ed.PushUndo();
            std::vector<std::string> before = c.body->ClipNames();
            std::string err;
            if (!c.body->LoadClipsFromFile(p, &err, /*replaceExisting=*/false)) {
                ConsoleLog(err.empty() ? "Import clip failed" : err, 2);
                return;
            }
            std::string picked;
            for (const auto& n : c.body->ClipNames()) {
                bool had = false;
                for (const auto& b : before) if (b == n) { had = true; break; }
                if (!had) { picked = n; break; }
            }
            if (picked.empty()) {
                std::ifstream fin(p);
                std::stringstream ss; if (fin) ss << fin.rdbuf();
                auto parsed = AnimClip::ParseAll(ss.str(), &Character::BoneIndex, Character::BoneCount(), nullptr);
                if (!parsed.empty()) picked = parsed.front().name;
            }
            if (!picked.empty()) {
                slot = picked; c.ed.dirty = true;
                ConsoleLog("Imported '" + picked + "' into " + st.name);
            }
        }
    }
}

// ---- 1D blend tree: the bar with draggable thresholds and live weights ----
void DrawBlend1D(Ctx& c, SM::State& st) {
    EditorState& ed = c.ed;
    SM& sm = *c.sm;
    ImGui::SetNextItemWidth(-1);
    ParamPicker(c, "##agblendp", st.blendParam, 0);
    Tip("The float parameter that picks the mix: idle at 0, walk at 2, run at 6.");
    const bool live = c.playing && sm.Current() == st.name;
    const float liveV = live ? sm.GetFloat(st.blendParam) : 0.0f;
    const std::vector<float>& lw = sm.BlendWeights();
    const int nm = (int)st.motions.size();
    // Sorted order and the axis range.
    std::vector<int> order(nm);
    for (int i = 0; i < nm; ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return st.motions[a].threshold < st.motions[b].threshold; });
    float lo = 0.0f, hi = 1.0f;
    if (nm > 0) { lo = st.motions[order.front()].threshold; hi = st.motions[order.back()].threshold; }
    if (live) { lo = std::min(lo, liveV); hi = std::max(hi, liveV); }
    float pad = std::max(0.5f, (hi - lo) * 0.12f);
    lo -= pad; hi += pad;
    const float w = ImGui::GetContentRegionAvail().x, h = 92.0f;
    const ImVec2 o = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float top = o.y + 18.0f, bottom = o.y + h - 22.0f;   // the weight area
    auto toX = [&](float v) { return o.x + 10.0f + (v - lo) / (hi - lo) * (w - 20.0f); };
    auto fromX = [&](float x) { return lo + (x - o.x - 10.0f) / (w - 20.0f) * (hi - lo); };
    dl->AddRectFilled(o, ImVec2(o.x + w, o.y + h), ImGui::GetColorU32(ImGuiCol_FrameBg), 4.0f);
    dl->AddLine(ImVec2(o.x + 6, bottom), ImVec2(o.x + w - 6, bottom), ImGui::GetColorU32(ImGuiCol_Border));
    // Each motion's influence: 1 at its threshold, 0 at its neighbours'.
    for (int k = 0; k < nm; ++k) {
        const int mi = order[k];
        const float t = st.motions[mi].threshold;
        const float l = k > 0 ? st.motions[order[k - 1]].threshold : lo;
        const float r = k + 1 < nm ? st.motions[order[k + 1]].threshold : hi;
        const ImVec4 tint = LayerTint(k % 2 == 0 ? 0 : 1);
        const float wt = live && mi < (int)lw.size() ? lw[mi] : 0.0f;
        const ImU32 fillCol = ImGui::GetColorU32(ImVec4(tint.x, tint.y, tint.z, 0.18f + wt * 0.45f));
        ImVec2 pts[4] = {ImVec2(toX(l), k > 0 ? bottom : top), ImVec2(toX(t), top), ImVec2(toX(r), k + 1 < nm ? bottom : top), ImVec2(toX(r), bottom)};
        if (k > 0 && k + 1 < nm) dl->AddTriangleFilled(ImVec2(toX(l), bottom), ImVec2(toX(t), top), ImVec2(toX(r), bottom), fillCol);
        else {
            // The ends stay fully on beyond the last threshold.
            ImVec2 poly[5]; int np = 0;
            if (k == 0) { poly[np++] = ImVec2(toX(lo), bottom); poly[np++] = ImVec2(toX(lo), top); poly[np++] = ImVec2(toX(t), top); }
            else { poly[np++] = ImVec2(toX(l), bottom); poly[np++] = ImVec2(toX(t), top); }
            if (k + 1 < nm) poly[np++] = ImVec2(toX(r), bottom);
            else { poly[np++] = ImVec2(toX(hi), top); poly[np++] = ImVec2(toX(hi), bottom); }
            dl->AddConvexPolyFilled(poly, np, fillCol);
            (void)pts;
        }
        dl->AddLine(ImVec2(toX(t), top), ImVec2(toX(t), bottom), ImGui::GetColorU32(ImVec4(tint.x, tint.y, tint.z, 0.9f)), 1.0f);
    }
    // Thresholds: draggable markers under the axis, the clip name above.
    static int s_drag = -1;
    int del = -1;
    for (int k = 0; k < nm; ++k) {
        const int mi = order[k];
        SM::Motion& m = st.motions[mi];
        const float x = toX(m.threshold);
        ImGui::PushID(mi);
        ImGui::SetCursorScreenPos(ImVec2(x - 8.0f, bottom - 2.0f));
        ImGui::InvisibleButton("##thr", ImVec2(16.0f, 20.0f));
        if (ImGui::IsItemActivated()) { ed.PushUndo(); s_drag = mi; }
        if (ImGui::IsItemActive() && s_drag == mi && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f)) {
            m.threshold = std::round(fromX(ImGui::GetIO().MousePos.x) * 20.0f) / 20.0f;
            ed.dirty = true;
        }
        if (ImGui::IsItemDeactivated()) s_drag = -1;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s at %.2f\nDrag to move \xC2\xB7 right-click to remove", m.clip.empty() ? "(no clip)" : m.clip.c_str(), m.threshold);
        if (ImGui::BeginPopupContextItem("##thrctx")) {
            if (ImGui::MenuItem("Remove motion")) del = mi;
            ImGui::EndPopup();
        }
        const bool hot = ImGui::IsItemHovered() || s_drag == mi;
        const ImU32 mc = hot ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled);
        dl->AddTriangleFilled(ImVec2(x, bottom + 1), ImVec2(x - 6, bottom + 11), ImVec2(x + 6, bottom + 11), mc);
        const std::string tl = Fmt("%.2g", m.threshold);
        const ImVec2 tsz = ImGui::CalcTextSize(tl.c_str());
        dl->AddText(ImVec2(x - tsz.x * 0.5f, bottom + 11), mc, tl.c_str());
        // The clip name above, staggered so neighbours don't collide.
        const char* nm2 = m.clip.empty() ? "(no clip)" : m.clip.c_str();
        const ImVec2 ns = ImGui::CalcTextSize(nm2);
        ImVec2 np(std::clamp(x - ns.x * 0.5f, o.x + 2, o.x + w - ns.x - 2), o.y + 2.0f + (k % 2 ? 0.0f : 0.0f));
        dl->AddText(np, hot ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled), nm2);
        ImGui::PopID();
    }
    if (live) {   // the parameter now
        const float x = toX(liveV);
        dl->AddLine(ImVec2(x, top - 4), ImVec2(x, bottom + 2), PlayCol(), 2.0f);
        const std::string lv = Fmt("%.2f", liveV);
        dl->AddText(ImVec2(x + 4, top - 2), PlayCol(), lv.c_str());
    }
    ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + h + 4.0f));
    if (del >= 0) { ed.PushUndo(); st.motions.erase(st.motions.begin() + del); ed.dirty = true; }
    // The motion list: clip, threshold, and the live weight.
    for (int mi = 0; mi < (int)st.motions.size(); ++mi) {
        SM::Motion& m = st.motions[mi];
        ImGui::PushID(2000 + mi);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
        ClipCombo(c, "##mc", m.clip, "(clip)");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 6.0f);
        DragF(ed, "##mt", m.threshold, 0.05f, 0.0f, 0.0f, "at %.2f");
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) del = mi;
        if (live) {
            const float wt = mi < (int)lw.size() ? lw[mi] : 0.0f;
            ImGui::ProgressBar(wt, ImVec2(-1, 4.0f), "");
        }
        ImGui::PopID();
    }
    if (del >= 0 && del < (int)st.motions.size()) { ed.PushUndo(); st.motions.erase(st.motions.begin() + del); ed.dirty = true; }
    if (ImGui::SmallButton("+ Motion##ag")) {
        ed.PushUndo();
        float next = 0.0f;
        for (const auto& m : st.motions) next = std::max(next, m.threshold + 1.0f);
        st.motions.push_back({c.clips.empty() ? std::string() : c.clips.front(), next});
        ed.dirty = true;
    }
    if (c.ma && !st.motions.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Thresholds from clip speed##ag")) {
            ed.PushUndo();
            int set = 0;
            for (auto& m : st.motions) {
                const int ci = c.ma->FindClip(m.clip);
                if (ci < 0) continue;
                m.threshold = std::round(c.ma->ClipRootSpeed(ci) * 100.0f) / 100.0f; ++set;
            }
            ed.dirty = true;
            ConsoleLog("Blend tree '" + st.name + "': thresholds set from " + std::to_string(set) + " clip(s)' root speed (m/s)");
        }
        Tip("Set each motion's threshold to how fast its clip actually moves (root\n"
            "bone, m/s), so 'speed' = the character's real velocity plays the clip\n"
            "whose feet match it: no sliding. In-place clips read 0.");
    }
    if (live) ImGui::TextDisabled("%s = %.2f", st.blendParam.c_str(), liveV);
}

// ---- 2D blend tree: the map with draggable motions and the live point ----
void DrawBlend2D(Ctx& c, SM::State& st) {
    EditorState& ed = c.ed;
    SM& sm = *c.sm;
    ParamPicker(c, "X by##ag2", st.blendParam, 0);
    ParamPicker(c, "Y by##ag2", st.blendParamY, 0);
    const bool live = c.playing && sm.Current() == st.name;
    const std::vector<float>& lw = sm.BlendWeights();
    const float mw = ImGui::GetContentRegionAvail().x, mh = std::min(mw, 190.0f);
    const ImVec2 mo = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(mo, ImVec2(mo.x + mw, mo.y + mh), ImGui::GetColorU32(ImGuiCol_FrameBg), 4.0f);
    float ext = 1.0f;
    for (const auto& m : st.motions) ext = std::max(ext, std::max(std::fabs(m.threshold), std::fabs(m.posY)));
    if (live) ext = std::max(ext, std::max(std::fabs(sm.GetFloat(st.blendParam)), std::fabs(sm.GetFloat(st.blendParamY))));
    ext *= 1.25f;
    auto toMap = [&](float x, float y) { return ImVec2(mo.x + (x / ext * 0.5f + 0.5f) * mw, mo.y + (0.5f - y / ext * 0.5f) * mh); };
    auto fromMap = [&](ImVec2 p) { return ImVec2(((p.x - mo.x) / mw - 0.5f) * 2.0f * ext, (0.5f - (p.y - mo.y) / mh) * 2.0f * ext); };
    // Grid every unit, the axes brighter, the parameter names at the ends.
    const float stepU = ext > 6.0f ? 2.0f : ext > 2.5f ? 1.0f : 0.5f;
    for (float g = -std::floor(ext / stepU) * stepU; g <= ext; g += stepU) {
        const ImU32 gc = ImGui::GetColorU32(ImGuiCol_Border, std::fabs(g) < 1e-4f ? 0.9f : 0.3f);
        dl->AddLine(toMap(g, -ext), toMap(g, ext), gc);
        dl->AddLine(toMap(-ext, g), toMap(ext, g), gc);
    }
    dl->AddText(ImVec2(mo.x + mw - ImGui::CalcTextSize(st.blendParam.c_str()).x - 4, mo.y + mh * 0.5f + 2), ImGui::GetColorU32(ImGuiCol_TextDisabled), st.blendParam.c_str());
    dl->AddText(ImVec2(mo.x + mw * 0.5f + 4, mo.y + 2), ImGui::GetColorU32(ImGuiCol_TextDisabled), st.blendParamY.c_str());
    // The map itself: right-click adds a motion, double-click too.
    ImGui::SetCursorScreenPos(mo);
    ImGui::InvisibleButton("##map2d", ImVec2(mw, mh));
    const bool mapHov = ImGui::IsItemHovered();
    auto addAt = [&](ImVec2 p) {
        ed.PushUndo();
        const ImVec2 v = fromMap(p);
        st.motions.push_back({c.clips.empty() ? std::string() : c.clips.front(), std::round(v.x * 10.0f) / 10.0f, std::round(v.y * 10.0f) / 10.0f});
        ed.dirty = true;
    };
    if (ImGui::BeginPopupContextItem("##map2dctx")) {
        if (ImGui::MenuItem("Add motion here")) addAt(ImGui::GetMousePosOnOpeningCurrentPopup());
        ImGui::EndPopup();
    }
    static int s_dragMotion = -1;
    int del = -1;
    for (int mi = 0; mi < (int)st.motions.size(); ++mi) {
        SM::Motion& m = st.motions[mi];
        const ImVec2 cp = toMap(m.threshold, m.posY);
        ImGui::SetCursorScreenPos(ImVec2(cp.x - 8, cp.y - 8));
        ImGui::PushID(3000 + mi);
        ImGui::InvisibleButton("##mdot", ImVec2(16, 16));
        if (ImGui::IsItemActivated()) { ed.PushUndo(); s_dragMotion = mi; }
        if (ImGui::IsItemActive() && s_dragMotion == mi && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f)) {
            const ImVec2 v = fromMap(ImGui::GetIO().MousePos);
            m.threshold = std::round(v.x * 10.0f) / 10.0f; m.posY = std::round(v.y * 10.0f) / 10.0f;
            ed.dirty = true;
        }
        if (ImGui::IsItemDeactivated()) s_dragMotion = -1;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s at (%.1f, %.1f)\nDrag to move \xC2\xB7 right-click to remove", m.clip.empty() ? "(no clip)" : m.clip.c_str(), m.threshold, m.posY);
        if (ImGui::BeginPopupContextItem("##mdotctx")) {
            if (ImGui::MenuItem("Remove motion")) del = mi;
            ImGui::EndPopup();
        }
        const bool hot = ImGui::IsItemHovered() || s_dragMotion == mi;
        ImGui::PopID();
        const float wgt = live && mi < (int)lw.size() ? lw[mi] : 0.0f;
        const ImVec4 tint = AccentCol();
        dl->AddCircleFilled(cp, 4.5f + wgt * 7.0f, ImGui::GetColorU32(ImVec4(tint.x, tint.y, tint.z, 0.45f + wgt * 0.55f)));
        dl->AddCircle(cp, 4.5f + wgt * 7.0f, hot ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImVec4(tint.x, tint.y, tint.z, 1.0f)), 0, hot ? 2.0f : 1.0f);
        dl->AddText(ImVec2(cp.x + 8, cp.y - 8), hot ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled), m.clip.empty() ? "(no clip)" : m.clip.c_str());
    }
    if (mapHov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && s_dragMotion < 0 && !ImGui::IsAnyItemActive()) addAt(ImGui::GetIO().MousePos);
    if (live) {
        const ImVec2 lp = toMap(sm.GetFloat(st.blendParam), sm.GetFloat(st.blendParamY));
        dl->AddCircle(lp, 7.0f, PlayCol(), 16, 2.0f);
        dl->AddLine(ImVec2(lp.x - 10, lp.y), ImVec2(lp.x + 10, lp.y), PlayCol(0.6f));
        dl->AddLine(ImVec2(lp.x, lp.y - 10), ImVec2(lp.x, lp.y + 10), PlayCol(0.6f));
    }
    ImGui::SetCursorScreenPos(ImVec2(mo.x, mo.y + mh + 4));
    ImGui::TextDisabled("Right-click or double-click the map to add a motion.");
    for (int mi = 0; mi < (int)st.motions.size(); ++mi) {
        SM::Motion& m = st.motions[mi];
        ImGui::PushID(4000 + mi);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
        ClipCombo(c, "##mc2", m.clip, "(clip)");
        ImGui::SameLine();
        float xy[2] = {m.threshold, m.posY};
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 6.0f);
        if (ImGui::DragFloat2("##mxy", xy, 0.05f)) { EditPush(ed); m.threshold = xy[0]; m.posY = xy[1]; ed.dirty = true; }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) del = mi;
        if (live) ImGui::ProgressBar(mi < (int)lw.size() ? lw[mi] : 0.0f, ImVec2(-1, 4.0f), "");
        ImGui::PopID();
    }
    if (del >= 0 && del < (int)st.motions.size()) { ed.PushUndo(); st.motions.erase(st.motions.begin() + del); ed.dirty = true; }
    if (ImGui::SmallButton("+ Motion##ag2")) {
        ed.PushUndo();
        st.motions.push_back({c.clips.empty() ? std::string() : c.clips.front(), 0.0f, 0.0f});
        ed.dirty = true;
    }
    if (live) ImGui::TextDisabled("%s = %.2f   %s = %.2f", st.blendParam.c_str(), sm.GetFloat(st.blendParam), st.blendParamY.c_str(), sm.GetFloat(st.blendParamY));
}

// ---- IK weight curve over the clip ----
void DrawIkCurve(Ctx& c, SM::State& st, const char* id, std::vector<SM::State::IkKey>& keys, const char* tip) {
    EditorState& ed = c.ed;
    ImGui::PushID(id);
    if (keys.empty()) {
        if (ImGui::SmallButton("Add curve")) {
            ed.PushUndo();
            keys.push_back({0.0f, 1.0f});
            keys.push_back({1.0f, 1.0f});
            ed.dirty = true;
        }
        Tip(tip);
    } else {
        const float mw = ImGui::GetContentRegionAvail().x, mh = 64.0f;
        const ImVec2 mo = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##plot", ImVec2(mw, mh));
        ImDrawList* pdl = ImGui::GetWindowDrawList();
        pdl->AddRectFilled(mo, ImVec2(mo.x + mw, mo.y + mh), ImGui::GetColorU32(ImGuiCol_FrameBg), 3.0f);
        auto toP = [&](float t, float w) { return ImVec2(mo.x + t * mw, mo.y + (1.0f - w) * mh); };
        auto fromP = [&](ImVec2 p) {
            const float t = mw > 1.0f ? (p.x - mo.x) / mw : 0.0f;
            const float w = mh > 1.0f ? 1.0f - (p.y - mo.y) / mh : 0.0f;
            return ImVec2(std::clamp(t, 0.0f, 1.0f), std::clamp(w, 0.0f, 1.0f));
        };
        std::vector<ImVec2> pts;
        for (const auto& k : keys) pts.push_back(toP(k.t, k.w));
        if ((int)pts.size() >= 2) pdl->AddPolyline(pts.data(), (int)pts.size(), ImGui::GetColorU32(ImGuiCol_PlotLines), ImDrawFlags_None, 2.0f);
        if (c.playing && c.sm->Current() == st.name) {
            float u = c.sm->NormalizedTime(); u -= std::floor(u); if (u < 0.0f) u += 1.0f;
            pdl->AddLine(ImVec2(mo.x + u * mw, mo.y), ImVec2(mo.x + u * mw, mo.y + mh), PlayCol());
        }
        static int s_drag = -1;
        static std::vector<SM::State::IkKey>* s_owner = nullptr;
        const ImVec2 mp = ImGui::GetIO().MousePos;
        int hit = -1; float best = 9.0f;
        for (int i = 0; i < (int)keys.size(); ++i) {
            const ImVec2 p = toP(keys[i].t, keys[i].w);
            const float d = std::hypot(p.x - mp.x, p.y - mp.y);
            if (d < best) { best = d; hit = i; }
        }
        auto resort = [&](float t, float w) {
            std::stable_sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) { return a.t < b.t; });
            for (int i = 0; i < (int)keys.size(); ++i) if (keys[i].t == t && keys[i].w == w) return i;
            return 0;
        };
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && hit >= 0) {
            ed.PushUndo();
            keys.erase(keys.begin() + hit); ed.dirty = true;
            if (s_owner == &keys) { s_drag = -1; s_owner = nullptr; }
        } else if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            ed.PushUndo();
            if (hit < 0) {
                const ImVec2 tw = fromP(mp);
                keys.push_back({tw.x, tw.y});
                hit = resort(tw.x, tw.y);
                ed.dirty = true;
            }
            s_drag = hit; s_owner = &keys;
        }
        if (s_owner == &keys && s_drag >= 0 && s_drag < (int)keys.size() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const ImVec2 tw = fromP(mp);
            keys[s_drag].t = tw.x; keys[s_drag].w = tw.y;
            s_drag = resort(tw.x, tw.y);
            ed.dirty = true;
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) { s_drag = -1; s_owner = nullptr; }
        for (int i = 0; i < (int)keys.size(); ++i)
            pdl->AddCircleFilled(toP(keys[i].t, keys[i].w), 4.0f, ImGui::GetColorU32(i == s_drag && s_owner == &keys ? ImGuiCol_PlotHistogramHovered : ImGuiCol_PlotLinesHovered));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nClick to add a key, drag to move, right-click to remove.", tip);
        if (ImGui::SmallButton("Clear curve")) { ed.PushUndo(); keys.clear(); ed.dirty = true; }
    }
    ImGui::PopID();
}

// ---- a transition's conditions and settings (state and Any State ones) ----
void DrawTransitionFields(Ctx& c, SM::Transition& tr, bool isAny) {
    EditorState& ed = c.ed;
    SM& sm = *c.sm;
    if (isAny) {
        if (ImGui::BeginCombo("To##agany", tr.to.empty() ? "(pick)" : tr.to.c_str())) {
            for (const auto& st : sm.states)
                if (ImGui::Selectable(st.name.c_str(), tr.to == st.name)) { EditPush(ed); tr.to = st.name; ed.dirty = true; }
            ImGui::EndCombo();
        }
    }
    ImGui::TextDisabled("Conditions (all must pass)");
    auto condRow = [&](SM::Cond& cond, std::string& param, float& value, const char* idc, const char* idp, const char* idv) {
        int cd = (int)cond;
        ImGui::SetNextItemWidth(NeedsParam(cond) ? ImGui::GetContentRegionAvail().x * 0.46f : -1.0f);
        if (ImGui::Combo(idc, &cd, kConds, SM::kCondCount)) { EditPush(ed); cond = (SM::Cond)cd; ed.dirty = true; }
        if (NeedsParam(cond)) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            ParamPicker(c, idp, param, CondParamType(cond));
        }
        if (IsFloatCond(cond)) { ImGui::SetNextItemWidth(120); DragF(ed, idv, value, 0.05f, 0.0f, 0.0f, "value %.2f"); }
        if (IsIntCond(cond)) {
            int iv = (int)std::lround(value);
            ImGui::SetNextItemWidth(120);
            if (ImGui::DragInt(idv, &iv, 0.1f, 0, 0, "value %d")) { EditPush(ed); value = (float)iv; ed.dirty = true; }
        }
    };
    condRow(tr.cond, tr.param, tr.value, "##agc", "##agp", "##agv");
    int delC = -1;
    for (int ci = 0; ci < (int)tr.extra.size(); ++ci) {
        SM::Condition& x = tr.extra[ci];
        ImGui::PushID(1000 + ci);
        ImGui::TextDisabled("and");
        ImGui::SameLine();
        if (ImGui::SmallButton("x##xdel")) delC = ci;
        Tip("Remove this condition");
        ImGui::SameLine();
        ImGui::BeginGroup();
        condRow(x.cond, x.param, x.value, "##xc", "##xp", "##xv");
        ImGui::EndGroup();
        ImGui::PopID();
    }
    if (delC >= 0) { ed.PushUndo(); tr.extra.erase(tr.extra.begin() + delC); ed.dirty = true; }
    if (ImGui::SmallButton("+ Condition##ag")) { ed.PushUndo(); tr.extra.push_back({SM::Cond::BoolTrue, "", 0.0f}); ed.dirty = true; }
    Tip("Another test ANDed with the ones above.");
    ImGui::Separator();
    Check(ed, "Exit time##ag", tr.hasExitTime);
    Tip("Only fire once the current state is this far through its clip\n(0.9 = 90%%; on looping states, within each loop).");
    if (tr.hasExitTime) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        SliderF(ed, "##agexit", tr.exitTime, 0.0f, 1.0f, "%.2f");
    }
    ImGui::SetNextItemWidth(120);
    DragF(ed, "Blend (s)##ag", tr.blend, 0.01f, 0.0f, 3.0f);
    Tip("How long the crossfade into the target takes.");
    Check(ed, "Keep phase##ag", tr.syncPhase);
    Tip("Start the target state where the current one is in its cycle,\nso walk <-> run between two looping states keeps the feet in step.");
    if (!tr.syncPhase) {
        ImGui::SetNextItemWidth(120);
        SliderF(ed, "Start at##ag", tr.offset, 0.0f, 0.99f, "%.2f");
        Tip("Start the target state part-way into its clip (0.5 = halfway).\nLine up the feet when switching between walk/run cycles.");
    }
    if (isAny) { Check(ed, "Can re-enter the current state##ag", tr.canInterruptSelf); Tip("Fire even when the target is the state already playing (restart it)."); }
    else {
        static const char* kInt[] = {"Next state", "None", "Current state", "Current, then next", "Next, then current"};
        int iv = (int)tr.interruption;
        ImGui::SetNextItemWidth(170);
        if (ImGui::Combo("Interruption##ag", &iv, kInt, IM_ARRAYSIZE(kInt))) { EditPush(ed); tr.interruption = (SM::Transition::Interrupt)iv; ed.dirty = true; }
        Tip("While this blend runs, whose transitions may cut it short:\n"
            "  Next state - the target state's (default)\n"
            "  None - nothing but Any State until the blend ends\n"
            "  Current state - the state being left (e.g. cancel a dodge back to Idle)\n"
            "  Current, then next / Next, then current - both, in that order");
    }
    Check(ed, "Mute##ag", tr.muted);
    Tip("Never fire: try the machine without this transition, keep it for later.");
}

// ---- transition preview: A plays, B fades in over the blend time ----
void DrawTransitionPreview(Ctx& c, const SM::State* from, const SM::Transition& tr, const std::string& label) {
    const int to = c.StateIndex(tr.to);
    if (to < 0) return;
    const SM::State& tgt = c.sm->states[to];
    const std::string clipA = from ? PreviewClip(c, *from) : std::string();
    const std::string clipB = PreviewClip(c, tgt);
    GameObject* target = c.obj;
    const bool can = from && c.Playable(clipA) && c.Playable(clipB) && (c.body ? apreview::BodyClip(c.body, clipA) && apreview::BodyClip(c.body, clipB)
                                                                                  : c.ma && c.ma->FindClip(clipA) >= 0 && c.ma->FindClip(clipB) >= 0);
    const bool on = apreview::IsTransition(target, label);
    if (!can) {
        if (!from) ImGui::TextDisabled("Any State transitions preview from a state: pick one in its Transitions list.");
        else ImGui::TextDisabled("Preview needs both clips in this Animator's library.");
        return;
    }
    if (ImGui::SmallButton(on ? "Stop preview##agtp" : "Preview blend##agtp")) {
        if (on) apreview::Stop(c.ed.scene());
        else {
            const float lenA = ClipSeconds(c, clipA);
            float startAt = tr.hasExitTime ? tr.exitTime * lenA : (tr.cond == SM::Cond::OnClipEnd ? lenA : lenA * 0.5f);
            if (!from->loop && !from->blendTree) startAt = std::min(startAt, lenA);
            const float offset = tr.syncPhase ? (lenA > 1e-4f ? std::fmod(startAt / lenA, 1.0f) : 0.0f) : tr.offset;
            apreview::StartTransition(c.ed.scene(), target, label, clipA, clipB, from->loop || from->blendTree, tgt.loop || tgt.blendTree,
                                      startAt, tr.blend, offset, from->speed, c.playing);
        }
    }
    Tip("Play the switch on the character in the Scene view: the current state's clip,\n"
        "then the target fading in over the blend time. Drag the playhead to scrub.");
    if (!on) return;
    apreview::Preview& p = apreview::Cur();
    ImGui::SameLine();
    if (ImGui::SmallButton(p.playing ? "Pause##agtpp" : "Play##agtpp")) p.playing = !p.playing;
    ImGui::SameLine();
    ImGui::Text("%.2fs", p.t);
    // The timeline: A's bar, B's bar from the blend start, the blend hatched.
    const float w = ImGui::GetContentRegionAvail().x, h = 44.0f;
    const ImVec2 o = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float t0 = p.t0, t1 = std::max(p.length, t0 + 0.01f);
    auto toX = [&](float t) { return o.x + 6.0f + (t - t0) / (t1 - t0) * (w - 12.0f); };
    dl->AddRectFilled(o, ImVec2(o.x + w, o.y + h), ImGui::GetColorU32(ImGuiCol_FrameBg), 4.0f);
    const ImVec4 ta = LayerTint(0), tb = LayerTint(1);
    const float bEnd = p.mixStart + p.mixDur;
    dl->AddRectFilled(ImVec2(toX(t0), o.y + 6), ImVec2(toX(bEnd), o.y + 20), ImGui::GetColorU32(ImVec4(ta.x, ta.y, ta.z, 0.75f)), 2.0f);
    dl->AddRectFilled(ImVec2(toX(p.mixStart), o.y + 24), ImVec2(toX(t1), o.y + 38), ImGui::GetColorU32(ImVec4(tb.x, tb.y, tb.z, 0.75f)), 2.0f);
    for (float x = toX(p.mixStart); x < toX(bEnd); x += 6.0f)   // the crossfade
        dl->AddLine(ImVec2(x, o.y + 4), ImVec2(x - 6.0f, o.y + h - 4), ImGui::GetColorU32(ImGuiCol_Text, 0.25f));
    dl->AddText(ImVec2(toX(t0) + 4, o.y + 6), ImGui::GetColorU32(ImGuiCol_Text), from->name.c_str());
    dl->AddText(ImVec2(toX(p.mixStart) + 4, o.y + 24), ImGui::GetColorU32(ImGuiCol_Text), tgt.name.c_str());
    ImGui::SetCursorScreenPos(o);
    ImGui::InvisibleButton("##agtpscrub", ImVec2(w, h));
    if (ImGui::IsItemActive()) {
        p.t = std::clamp(t0 + (ImGui::GetIO().MousePos.x - o.x - 6.0f) / (w - 12.0f) * (t1 - t0), t0, t1);
        p.playing = false;
    }
    const float px = toX(p.t);
    dl->AddLine(ImVec2(px, o.y + 2), ImVec2(px, o.y + h - 2), PlayCol(), 2.0f);
    ImGui::TextDisabled("%s fades into %s over %.2fs (mix %.0f%%)", from->name.c_str(), tgt.name.c_str(), p.mixDur, apreview::MixWeight(p, p.t) * 100.0f);
}

// ---------------------------------------------------------------------------
// Inspectors
// ---------------------------------------------------------------------------
void DrawStateInspector(Ctx& c, Ui& u, int i) {
    EditorState& ed = c.ed;
    SM& sm = *c.sm;
    SM::State& st = sm.states[i];
    const int n = (int)sm.states.size();
    const int entryIdx = sm.entry.empty() ? (n > 0 ? 0 : -1) : c.StateIndex(sm.entry);
    // Name, entry flag, problems.
    ImGui::TextDisabled(u.sel.size() > 1 ? "State (%d selected)" : "State", (int)u.sel.size());
    {
        char nb[64]; std::snprintf(nb, sizeof(nb), "%s", st.name.c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##agname", nb, sizeof(nb))) {
            const std::string nn = nb;
            if (!nn.empty() && nn != st.name) { EditPush(ed); RenameState(sm, i, UniqueName(sm, nn, i)); ed.dirty = true; }
        }
        Tip("The state's name (transitions follow a rename).");
    }
    {
        bool entry = i == entryIdx;
        if (ImGui::Checkbox("Entry state##ag", &entry)) {
            ed.PushUndo();
            sm.entry = entry ? st.name : (n > 0 && i != 0 ? sm.states[0].name : std::string());
            ed.dirty = true;
        }
        Tip("The state the machine starts in.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Add transition##ag")) u.pendingFrom = i;
        Tip("Then click the target state in the graph.");
    }
    for (const auto& is : c.issues) {
        if (is.any || is.state != i) continue;
        ImGui::PushStyleColor(ImGuiCol_Text, is.severity >= 2 ? ImVec4(0.95f, 0.38f, 0.35f, 1) : is.severity == 1 ? ImVec4(0.95f, 0.75f, 0.25f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", is.text.c_str());
        ImGui::PopStyleColor();
    }

    // ---- Motion ----
    if (Section("Motion")) {
        int mode = st.blendTree ? 1 : 0;
        if (ImGui::RadioButton("Single clip##ag", &mode, 0) && st.blendTree) { ed.PushUndo(); st.blendTree = false; ed.dirty = true; }
        ImGui::SameLine();
        if (ImGui::RadioButton("Blend tree##ag", &mode, 1) && !st.blendTree) {
            ed.PushUndo();
            st.blendTree = true;
            if (st.motions.empty() && !st.clip.empty()) st.motions.push_back({st.clip, 0.0f});
            ed.dirty = true;
        }
        Tip("A blend tree mixes several clips by a parameter (idle / walk / run by speed).");
        if (!st.blendTree) {
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.0f);
            ClipCombo(c, "##agclip", st.clip, "(none)");
            ImGui::SameLine();
            StatePreview(c, st);
        } else {
            int dims = st.blendDims == 2 ? 1 : 0;
            if (ImGui::RadioButton("1D##agbd", &dims, 0) && st.blendDims != 1) { ed.PushUndo(); st.blendDims = 1; ed.dirty = true; }
            ImGui::SameLine();
            if (ImGui::RadioButton("2D (strafe)##agbd", &dims, 1) && st.blendDims != 2) {
                ed.PushUndo();
                st.blendDims = 2;
                if (st.blendParam.empty() || st.blendParam == "speed") st.blendParam = "moveX";
                if (st.blendParamY.empty()) st.blendParamY = "moveZ";
                ed.dirty = true;
            }
            Tip("Blend over two parameters: e.g. moveX / moveZ (sideways / forward speed)\n"
                "with idle in the middle and walk forward, back, left and right around it.\n"
                "The player controllers send moveX / moveZ.");
            ImGui::SameLine();
            StatePreview(c, st);
            if (st.blendDims == 2) DrawBlend2D(c, st); else DrawBlend1D(c, st);
            Check(ed, "Match speed##ag", st.matchSpeed);
            Tip("Play the tree as fast as the body moves: the motions' positions are their\n"
                "travel speeds, so the feet keep pace with the ground between and beyond them.");
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::TextDisabled("If missing");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        ClipCombo(c, "##agfb", st.fallbackClip, "(none)");
        Tip("Played when the clip is not in this Animator's library.\n"
            "A name that exists only on a child model's clips plays there\n"
            "(Humanoid Retarget yields until the state changes).");
        if (c.ma || c.body) {
            if (ImGui::SmallButton("Import into clip##ag")) ImportIntoSlot(c, st, st.clip);
            Tip("Load a clip file and put its name in the Clip slot.\nA character takes .okayanim. An imported model takes FBX, glTF or BVH.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Import as fallback##ag")) ImportIntoSlot(c, st, st.fallbackClip);
        }
    }

    // ---- Playback ----
    if (Section("Playback")) {
        ImGui::SetNextItemWidth(110);
        DragF(ed, "Speed##ag", st.speed, 0.02f, 0.05f, 5.0f);
        {
            bool useSp = !st.speedParam.empty();
            ImGui::SameLine();
            if (ImGui::Checkbox("x parameter##agsp", &useSp)) { ed.PushUndo(); st.speedParam = useSp ? "speedMultiplier" : ""; ed.dirty = true; }
            Tip("Multiply the speed by a float parameter (e.g. a playback rate that\nfollows how fast the character moves).");
            if (useSp) { ImGui::SetNextItemWidth(-1); ParamPicker(c, "##agspp", st.speedParam, 0); }
        }
        if (!st.blendTree) { Check(ed, "Loop##ag", st.loop); ImGui::SameLine(); }
        Check(ed, "Mirror##ag", st.mirror);
        Tip("Play this state on the other side: left and right swap.\nAuthor one wave or punch and mirror it for the other hand.");
        {
            static const char* kRoot[] = {"Inherit", "Off", "On"};
            int rm = st.rootMotion < 0 ? 0 : (st.rootMotion == 0 ? 1 : 2);
            ImGui::SetNextItemWidth(110);
            if (ImGui::Combo("Root motion##ag", &rm, kRoot, 3)) { EditPush(ed); st.rootMotion = rm == 0 ? -1 : (rm == 1 ? 0 : 1); ed.dirty = true; }
            Tip("Inherit uses the Model Animator's own root-motion toggle.\nOff strips this state's root travel. On turns it on for this state only.");
        }
        Check(ed, "Any State may interrupt##ag", st.interruptible);
        Tip("Off locks the state: Any State transitions (hit, jump, fall...) cannot cut it\nshort. Its own transitions still run. Death uses this.");
    }

    // ---- IK ----
    if (Section("IK", st.ikWeight < 0.999f || !st.ikCurve.empty() || st.fullBodyIkWeight >= 0.0f)) {
        SliderF(ed, "IK weight##ag", st.ikWeight, 0.0f, 1.0f);
        Tip("Foot planting in this state (and Full Body IK, unless given its own weight below).\n"
            "Eases over the transition. 0 for jumps, rolls and falls (feet leave the ground).");
        DrawIkCurve(c, st, "foot", st.ikCurve, "Foot IK weight over this clip (0 at the bottom, 1 at the top),\nmultiplied by the IK weight slider. Empty = that constant.");
        bool follow = st.fullBodyIkWeight < 0.0f;
        if (ImGui::Checkbox("Full-body IK follows##ag", &follow)) { ed.PushUndo(); st.fullBodyIkWeight = follow ? -1.0f : 1.0f; ed.dirty = true; }
        Tip("Off: Full Body IK gets its own weight here, so a state can stop foot\nplanting while the hands keep holding (prone, crawling).");
        if (!follow) {
            SliderF(ed, "Full-body IK weight##ag", st.fullBodyIkWeight, 0.0f, 1.0f);
            DrawIkCurve(c, st, "body", st.fullBodyIkCurve, "Full Body IK weight over this clip, multiplied by its slider.\nEmpty = the constant. Foot IK keeps its own curve.");
        }
    }

    // ---- Events ----
    if (Section("Events", !st.enterEvent.empty() || !st.exitEvent.empty())) {
        TextEdit(ed, "On enter##agev", st.enterEvent, "event name");
        Tip("Animation event fired when the machine enters this state:\nanim_event() in scripts, and visual scripts' animation event nodes.");
        TextEdit(ed, "On exit##agev", st.exitEvent, "event name");
        Tip("Animation event fired when the machine leaves this state.");
    }

    // ---- Transitions ----
    const std::string trTitle = "Transitions (" + std::to_string(st.transitions.size()) + ")###agtrs";
    if (Section(trTitle.c_str())) {
        ImGui::TextDisabled("Tested top to bottom; the first that passes wins.");
        int moveFrom = -1, moveTo = -1, del = -1;
        for (int ti = 0; ti < (int)st.transitions.size(); ++ti) {
            const auto& tr = st.transitions[ti];
            ImGui::PushID(5000 + ti);
            ImGui::BeginDisabled(ti == 0);
            if (ImGui::ArrowButton("##up", ImGuiDir_Up)) { moveFrom = ti; moveTo = ti - 1; }
            ImGui::EndDisabled();
            ImGui::SameLine(0, 2);
            ImGui::BeginDisabled(ti + 1 >= (int)st.transitions.size());
            if (ImGui::ArrowButton("##dn", ImGuiDir_Down)) { moveFrom = ti; moveTo = ti + 1; }
            ImGui::EndDisabled();
            ImGui::SameLine();
            const int sev = c.TransitionSeverity(i, ti, false);
            std::string lbl = (sev > 0 ? "! " : "") + ("-> " + tr.to) + "   " + TransitionLabel(tr) + "##t";
            const bool isSel = u.selTrFrom == i && u.selTrIdx == ti;
            if (tr.muted) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            else if (sev > 0) ImGui::PushStyleColor(ImGuiCol_Text, sev >= 2 ? ImVec4(0.95f, 0.38f, 0.35f, 1) : ImVec4(0.95f, 0.75f, 0.25f, 1));
            if (ImGui::Selectable(lbl.c_str(), isSel)) SelectTransition(u, i, ti);
            if (tr.muted || sev > 0) ImGui::PopStyleColor();
            if (ImGui::BeginPopupContextItem("##trctx")) {
                if (ImGui::MenuItem("Edit")) SelectTransition(u, i, ti);
                if (ImGui::MenuItem(tr.muted ? "Unmute" : "Mute")) { ed.PushUndo(); st.transitions[ti].muted = !tr.muted; ed.dirty = true; }
                if (ImGui::MenuItem("Delete")) del = ti;
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if (moveFrom >= 0) {
            ed.PushUndo();
            std::swap(st.transitions[moveFrom], st.transitions[moveTo]);
            if (u.selTrFrom == i && u.selTrIdx == moveFrom) u.selTrIdx = moveTo;
            ed.dirty = true;
        }
        if (del >= 0) { ed.PushUndo(); st.transitions.erase(st.transitions.begin() + del); if (u.selTrFrom == i) { u.selTrFrom = -1; u.selTrIdx = -1; } ed.dirty = true; }
        // Transitions into this state, for the way back.
        int inbound = 0;
        for (int k = 0; k < n; ++k) for (const auto& tr : sm.states[k].transitions) if (tr.to == st.name) ++inbound;
        for (const auto& tr : sm.anyState) if (tr.to == st.name) ++inbound;
        if (inbound > 0 && ImGui::TreeNode("##agin", "Incoming (%d)", inbound)) {
            for (int k = 0; k < n; ++k)
                for (int ti = 0; ti < (int)sm.states[k].transitions.size(); ++ti)
                    if (sm.states[k].transitions[ti].to == st.name) {
                        const std::string lbl = sm.states[k].name + " ->   " + TransitionLabel(sm.states[k].transitions[ti]) + "##in" + std::to_string(k * 1000 + ti);
                        if (ImGui::Selectable(lbl.c_str(), false)) SelectTransition(u, k, ti);
                    }
            for (int ti = 0; ti < (int)sm.anyState.size(); ++ti)
                if (sm.anyState[ti].to == st.name) {
                    const std::string lbl = "Any State ->   " + TransitionLabel(sm.anyState[ti]) + "##ain" + std::to_string(ti);
                    if (ImGui::Selectable(lbl.c_str(), false)) SelectAny(u, ti);
                }
            ImGui::TreePop();
        }
    }
    ImGui::Spacing();
    if (ImGui::Button(u.sel.size() > 1 ? "Delete selected states##ag" : "Delete state##ag")) DeleteStates(c, u, u.sel.empty() ? std::vector<int>{i} : u.sel);
}

void DrawTransitionInspector(Ctx& c, Ui& u) {
    EditorState& ed = c.ed;
    SM& sm = *c.sm;
    SM::State& from = sm.states[u.selTrFrom];
    SM::Transition& tr = from.transitions[u.selTrIdx];
    const int fromIdx = u.selTrFrom, trIdx = u.selTrIdx;
    ImGui::TextDisabled("Transition %d of %d from", trIdx + 1, (int)from.transitions.size());
    if (ImGui::SmallButton((from.name + "##agfrom").c_str())) SelectState(u, fromIdx);
    Tip("Select the source state");
    ImGui::SameLine(); ImGui::TextUnformatted("->"); ImGui::SameLine();
    const int to = c.StateIndex(tr.to);
    if (to >= 0) { if (ImGui::SmallButton((tr.to + "##agto").c_str())) SelectState(u, to); Tip("Select the target state"); }
    else ImGui::TextColored(ImVec4(0.95f, 0.38f, 0.35f, 1), "%s (missing)", tr.to.c_str());
    // Retarget.
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##agretarget", "Change target...")) {
        for (const auto& st : sm.states)
            if (ImGui::Selectable(st.name.c_str(), tr.to == st.name)) { EditPush(ed); tr.to = st.name; ed.dirty = true; }
        ImGui::EndCombo();
    }
    for (const auto& is : c.issues)
        if (!is.any && is.state == fromIdx && is.transition == trIdx) {
            ImGui::PushStyleColor(ImGuiCol_Text, is.severity >= 2 ? ImVec4(0.95f, 0.38f, 0.35f, 1) : ImVec4(0.95f, 0.75f, 0.25f, 1));
            ImGui::TextWrapped("%s", is.text.c_str());
            ImGui::PopStyleColor();
        }
    ImGui::Separator();
    DrawTransitionFields(c, tr, false);
    ImGui::Separator();
    // Priority among the source's transitions.
    ImGui::TextDisabled("Priority");
    ImGui::SameLine();
    ImGui::BeginDisabled(trIdx == 0);
    if (ImGui::ArrowButton("##agprup", ImGuiDir_Up)) { ed.PushUndo(); std::swap(from.transitions[trIdx], from.transitions[trIdx - 1]); u.selTrIdx = trIdx - 1; ed.dirty = true; }
    ImGui::EndDisabled();
    Tip("Test this transition earlier");
    ImGui::SameLine(0, 2);
    ImGui::BeginDisabled(trIdx + 1 >= (int)from.transitions.size());
    if (ImGui::ArrowButton("##agprdn", ImGuiDir_Down)) { ed.PushUndo(); std::swap(from.transitions[trIdx], from.transitions[trIdx + 1]); u.selTrIdx = trIdx + 1; ed.dirty = true; }
    ImGui::EndDisabled();
    Tip("Test this transition later");
    if (u.selTrIdx == trIdx) {
        ImGui::Separator();
        DrawTransitionPreview(c, &from, tr, from.name + " -> " + tr.to + " #" + std::to_string(trIdx));
        ImGui::Separator();
        if (ImGui::Button("Delete transition##ag")) {
            ed.PushUndo();
            from.transitions.erase(from.transitions.begin() + trIdx);
            u.selTrFrom = -1; u.selTrIdx = -1;
            ed.dirty = true;
        }
    }
}

void DrawAnyInspector(Ctx& c, Ui& u) {
    EditorState& ed = c.ed;
    SM& sm = *c.sm;
    SM::Transition& tr = sm.anyState[u.selAny];
    const int idx = u.selAny;
    ImGui::TextDisabled("Any State transition %d of %d", idx + 1, (int)sm.anyState.size());
    Tip("Checked every frame from whatever state is playing, before its own transitions.");
    for (const auto& is : c.issues)
        if (is.any && is.transition == idx) {
            ImGui::PushStyleColor(ImGuiCol_Text, is.severity >= 2 ? ImVec4(0.95f, 0.38f, 0.35f, 1) : ImVec4(0.95f, 0.75f, 0.25f, 1));
            ImGui::TextWrapped("%s", is.text.c_str());
            ImGui::PopStyleColor();
        }
    DrawTransitionFields(c, tr, true);
    ImGui::Separator();
    ImGui::TextDisabled("Priority");
    ImGui::SameLine();
    ImGui::BeginDisabled(idx == 0);
    if (ImGui::ArrowButton("##agaup", ImGuiDir_Up)) { ed.PushUndo(); std::swap(sm.anyState[idx], sm.anyState[idx - 1]); u.selAny = idx - 1; ed.dirty = true; }
    ImGui::EndDisabled();
    ImGui::SameLine(0, 2);
    ImGui::BeginDisabled(idx + 1 >= (int)sm.anyState.size());
    if (ImGui::ArrowButton("##agadn", ImGuiDir_Down)) { ed.PushUndo(); std::swap(sm.anyState[idx], sm.anyState[idx + 1]); u.selAny = idx + 1; ed.dirty = true; }
    ImGui::EndDisabled();
    if (u.selAny == idx) {
        ImGui::Separator();
        // Preview from the state that is selected in the graph, else the entry.
        const SM::State* from = nullptr;
        if (!sm.states.empty()) {
            const int e = sm.entry.empty() ? 0 : std::max(0, c.StateIndex(sm.entry));
            from = &sm.states[e];
            static int s_from = -1;
            if (s_from >= 0 && s_from < (int)sm.states.size()) from = &sm.states[s_from];
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##agafrom", ("Preview from: " + from->name).c_str())) {
                for (int k = 0; k < (int)sm.states.size(); ++k)
                    if (ImGui::Selectable(sm.states[k].name.c_str(), from == &sm.states[k])) s_from = k;
                ImGui::EndCombo();
            }
        }
        DrawTransitionPreview(c, from, tr, "Any -> " + tr.to + " #" + std::to_string(idx));
        ImGui::Separator();
        if (ImGui::Button("Delete transition##agany")) {
            ed.PushUndo();
            sm.anyState.erase(sm.anyState.begin() + idx);
            u.selAny = -1; ed.dirty = true;
        }
    }
}

// Nothing selected: the machine itself.
void DrawMachineInspector(Ctx& c, Ui& u) {
    EditorState& ed = c.ed;
    SM& sm = *c.sm;
    int trCount = 0;
    for (const auto& st : sm.states) trCount += (int)st.transitions.size();
    ImGui::TextDisabled("Animator on %s", c.obj->name.c_str());
    ImGui::Text("%d states \xC2\xB7 %d transitions \xC2\xB7 %d Any State \xC2\xB7 %d layer%s", (int)sm.states.size(), trCount, (int)sm.anyState.size(),
                (int)c.machines.size(), c.machines.size() == 1 ? "" : "s");
    if (c.ProblemCount() > 0) {
        int err = 0;
        for (const auto& is : c.issues) err += is.severity >= 2 ? 1 : 0;
        ImGui::PushStyleColor(ImGuiCol_Text, err ? ImVec4(0.95f, 0.38f, 0.35f, 1) : ImVec4(0.95f, 0.75f, 0.25f, 1));
        if (ImGui::SmallButton(Fmt("%g issue(s)##agprob", (double)c.ProblemCount()).c_str())) u.forceSideTab = 3;
        ImGui::PopStyleColor();
        Tip("Open the Issues tab");
    }
    ImGui::Separator();
    ImGui::TextDisabled("Entry state");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##agentry", sm.entry.empty() ? "(first state)" : sm.entry.c_str())) {
        if (ImGui::Selectable("(first state)", sm.entry.empty())) { EditPush(ed); sm.entry.clear(); ed.dirty = true; }
        for (const auto& st : sm.states)
            if (ImGui::Selectable(st.name.c_str(), sm.entry == st.name)) { EditPush(ed); sm.entry = st.name; ed.dirty = true; }
        ImGui::EndCombo();
    }
    ImGui::Separator();
    ImGui::TextDisabled("Any State transitions");
    Tip("Fire from every state: hit reactions, death, jumps.");
    for (int ai = 0; ai < (int)sm.anyState.size(); ++ai) {
        const auto& tr = sm.anyState[ai];
        const int sev = c.TransitionSeverity(-1, ai, true);
        std::string lbl = (sev > 0 ? "! " : "") + ("-> " + (tr.to.empty() ? std::string("?") : tr.to)) + "   " + TransitionLabel(tr) + "##any" + std::to_string(ai);
        if (tr.muted) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        if (ImGui::Selectable(lbl.c_str(), false)) SelectAny(u, ai);
        if (tr.muted) ImGui::PopStyleColor();
    }
    if (ImGui::SmallButton("+ Any State transition##ag")) {
        ed.PushUndo();
        SM::Transition tr;
        if (!sm.states.empty()) tr.to = sm.states.front().name;
        sm.anyState.push_back(tr);
        SelectAny(u, (int)sm.anyState.size() - 1);
        ed.dirty = true;
    }
    if (c.playing) {
        ImGui::Separator();
        ImGui::TextDisabled("Playing");
        float left = 0.0f;
        if (sm.InTransition(&left))
            ImGui::TextColored(AccentCol(), "Blending %s -> %s  (%.2fs left)", sm.TransitionSource().c_str(), sm.Current().c_str(), left);
        else ImGui::Text("In %s", sm.Current().empty() ? "(entering)" : sm.Current().c_str());
        ImGui::TextDisabled("state time %.2fs \xC2\xB7 normalized %.2f", sm.StateTime(), sm.NormalizedTime());
        if (sm.Interruptions() > 0) ImGui::TextDisabled("%d transition%s interrupted", sm.Interruptions(), sm.Interruptions() == 1 ? "" : "s");
        if (!sm.History().empty()) {
            ImGui::TextDisabled("Recent state changes");
            const auto& hist = sm.History();
            for (int hi = (int)hist.size() - 1; hi >= 0 && hi >= (int)hist.size() - 8; --hi) {
                const auto& h = hist[hi];
                ImGui::TextDisabled("%6.2fs", h.time);
                ImGui::SameLine();
                ImGui::Text("%s -> %s", h.from.empty() ? "(start)" : h.from.c_str(), h.to.c_str());
            }
        }
        ImGui::Separator();
        ImGui::TextDisabled("Set from scripts:");
        ImGui::TextWrapped("anim_set_float(\"speed\", v)\nanim_set_bool(\"armed\", 1)\nanim_trigger(\"attack\")");
    } else {
        ImGui::Separator();
        ImGui::TextDisabled("Click a state or an arrow to edit it. Drag on empty\ncanvas to box-select; double-click it to add a state.");
    }
}

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------
void DrawParameters(Ctx& c, Ui& u) {
    EditorState& ed = c.ed;
    SM& base = *c.base;
    SearchField("##agpf", u.paramFilter, sizeof(u.paramFilter), "Filter parameters", -90.0f);
    ImGui::SameLine();
    if (ImGui::Button("+ Add##agpadd", ImVec2(-1, 0))) ImGui::OpenPopup("##agpaddmenu");
    if (ImGui::BeginPopup("##agpaddmenu")) {
        auto add = [&](const char* nm, int type) {
            ed.PushUndo();
            std::string name = nm;
            for (int k = 2;; ++k) { if (!c.ParamDeclared(name)) break; name = std::string(nm) + std::to_string(k); }
            base.params.push_back({name, type});
            ed.dirty = true;
        };
        if (ImGui::MenuItem("Float")) add("param", 0);
        if (ImGui::MenuItem("Bool")) add("flag", 1);
        if (ImGui::MenuItem("Int")) add("mode", 3);
        if (ImGui::MenuItem("Trigger")) add("go", 2);
        ImGui::EndPopup();
    }
    // Parameters declared on a layer (older scenes) belong with the base layer's.
    int onLayers = 0;
    for (SM* m : c.machines) if (m != c.base) onLayers += (int)m->params.size();
    if (onLayers > 0) {
        ImGui::TextDisabled("%d parameter(s) are declared on a layer.", onLayers);
        ImGui::SameLine();
        if (ImGui::SmallButton("Move to base##agpm")) {
            ed.PushUndo();
            for (SM* m : c.machines) {
                if (m == c.base) continue;
                for (const auto& p : m->params) if (!c.ParamDeclared(p.name)) base.params.push_back(p);
                m->params.clear();
            }
            ed.dirty = true;
        }
    }
    if (base.params.empty()) {
        ImGui::TextDisabled("No parameters yet. Transitions test them, blend trees are\ndriven by them; gameplay sets them (anim_set_float, ...).");
        return;
    }
    const bool playing = c.playing;
    const int cols = playing ? 7 : 6;
    if (!ImGui::BeginTable("##agparams", cols, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_NoSavedSettings)) return;
    ImGui::TableSetupColumn("##h", ImGuiTableColumnFlags_WidthFixed, 14.0f);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 76.0f);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.4f);
    ImGui::TableSetupColumn("Default", ImGuiTableColumnFlags_WidthStretch, 0.8f);
    if (playing) ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.3f);
    ImGui::TableSetupColumn("Used", ImGuiTableColumnFlags_WidthFixed, 44.0f);
    ImGui::TableSetupColumn("##x", ImGuiTableColumnFlags_WidthFixed, 20.0f);
    ImGui::TableHeadersRow();
    int del = -1, moveFrom = -1, moveTo = -1;
    for (int pi = 0; pi < (int)base.params.size(); ++pi) {
        SM::Param& p = base.params[pi];
        if (u.paramFilter[0] && Lower(p.name).find(Lower(u.paramFilter)) == std::string::npos) continue;
        ImGui::PushID(pi);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Selectable(":##drag", false, ImGuiSelectableFlags_None, ImVec2(12.0f, 0));
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
            ImGui::SetDragDropPayload("AG_PARAM", &pi, sizeof(int));
            ImGui::Text("Move %s", p.name.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag onto another row to reorder");
        auto dropTarget = [&]() {
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("AG_PARAM")) { moveFrom = *(const int*)pl->Data; moveTo = pi; }
                ImGui::EndDragDropTarget();
            }
        };
        dropTarget();
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1);
        {
            int t = p.type;
            if (ImGui::Combo("##pt", &t, kParamTypes, 4)) { EditPush(ed); p.type = t; if (t == 2) p.def = 0.0f; ed.dirty = true; }
        }
        ImGui::TableSetColumnIndex(2);
        ImGui::SetNextItemWidth(-1);
        {
            char pb[48]; std::snprintf(pb, sizeof(pb), "%s", p.name.c_str());
            if (ImGui::InputText("##pn", pb, sizeof(pb))) {
                EditPush(ed);
                // A rename follows into everything that reads the parameter.
                const std::string oldName = p.name, newName = pb;
                p.name = newName;
                if (!oldName.empty() && !newName.empty())
                    for (SM* m : c.machines) {
                        auto fix = [&](SM::Transition& tr) {
                            if (tr.param == oldName) tr.param = newName;
                            for (auto& x : tr.extra) if (x.param == oldName) x.param = newName;
                        };
                        for (auto& tr : m->anyState) fix(tr);
                        for (auto& st : m->states) {
                            for (auto& tr : st.transitions) fix(tr);
                            if (st.blendParam == oldName) st.blendParam = newName;
                            if (st.blendParamY == oldName) st.blendParamY = newName;
                            if (st.speedParam == oldName) st.speedParam = newName;
                        }
                    }
                ed.dirty = true;
            }
            dropTarget();
        }
        ImGui::TableSetColumnIndex(3);
        ImGui::SetNextItemWidth(-1);
        if (p.type == 0) DragF(ed, "##pd", p.def, 0.05f, 0.0f, 0.0f, "%.2f");
        else if (p.type == 1) { bool b = p.def != 0.0f; if (ImGui::Checkbox("##pdb", &b)) { EditPush(ed); p.def = b ? 1.0f : 0.0f; ed.dirty = true; } }
        else if (p.type == 3) { int v = (int)std::lround(p.def); if (ImGui::DragInt("##pdi", &v, 0.1f)) { EditPush(ed); p.def = (float)v; ed.dirty = true; } }
        else ImGui::TextDisabled("-");
        if (ImGui::IsItemHovered() && p.type != 2) ImGui::SetTooltip("The value when the machine starts (unless gameplay set one already).");
        if (playing) {
            ImGui::TableSetColumnIndex(4);
            ImGui::SetNextItemWidth(-1);
            if (p.type == 0) {
                float lo, hi; ParamRange(c, p.name, lo, hi);
                float v = base.GetFloat(p.name);
                if (ImGui::SliderFloat("##pv", &v, lo, hi, "%.2f")) base.SetFloat(p.name, v);
            } else if (p.type == 1) { bool b = base.GetBool(p.name); if (ImGui::Checkbox("##pvb", &b)) base.SetBool(p.name, b); }
            else if (p.type == 3) { int v = base.GetInt(p.name); if (ImGui::DragInt("##pvi", &v, 0.1f)) base.SetInt(p.name, v); }
            else if (ImGui::SmallButton("Fire##pf")) base.SetTrigger(p.name);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Live value: drive the machine by hand while it plays.");
        }
        ImGui::TableSetColumnIndex(playing ? 5 : 4);
        if (c.ParamUsed(p.name)) { ImGui::TextDisabled("yes"); }
        else {
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f), "no");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Unused: no transition, blend tree or speed multiplier reads it.");
        }
        ImGui::TableSetColumnIndex(playing ? 6 : 5);
        if (ImGui::SmallButton("x##pd")) del = pi;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove the declaration (transitions keep the name).");
        ImGui::PopID();
    }
    ImGui::EndTable();
    if (del >= 0) { ed.PushUndo(); base.params.erase(base.params.begin() + del); ed.dirty = true; }
    if (moveFrom >= 0 && moveTo >= 0 && moveFrom != moveTo && moveFrom < (int)base.params.size() && moveTo < (int)base.params.size()) {
        ed.PushUndo();
        SM::Param moved = base.params[moveFrom];
        base.params.erase(base.params.begin() + moveFrom);
        base.params.insert(base.params.begin() + moveTo, moved);
        ed.dirty = true;
    }
    ImGui::TextDisabled("Defaults apply when the machine starts. Rename a parameter and every\ncondition, blend tree and speed multiplier that reads it follows.");
}

// ---------------------------------------------------------------------------
// Layer: name, weight, additive, and the mask as a bone tree
// ---------------------------------------------------------------------------
// A bone tree node with a tick box: a ticked bone is a mask root, the bones
// under it are in the mask too (shown ticked and tinted, not editable).
struct BoneNode { std::string name; std::string token; std::vector<int> children; };
void DrawBoneTree(Ctx& c, SM& sm, const std::vector<BoneNode>& nodes, int i, bool underRoot) {
    EditorState& ed = c.ed;
    const BoneNode& b = nodes[i];
    auto same = [&](const std::string& tok) { return Lower(tok) == Lower(b.token); };
    const bool isRoot = same(sm.maskBone) || std::find_if(sm.maskBones.begin(), sm.maskBones.end(), same) != sm.maskBones.end();
    const bool inMask = isRoot || underRoot;
    ImGui::PushID(i);
    bool on = inMask;
    if (underRoot) ImGui::BeginDisabled();
    if (inMask) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(LayerTint(sm.layer).x, LayerTint(sm.layer).y, LayerTint(sm.layer).z, 1.0f));
    if (ImGui::Checkbox("##m", &on)) {
        ed.PushUndo();
        if (on) { if (sm.maskBone.empty()) sm.maskBone = b.token; else sm.maskBones.push_back(b.token); }
        else {
            if (same(sm.maskBone)) {
                sm.maskBone.clear();
                if (!sm.maskBones.empty()) { sm.maskBone = sm.maskBones.front(); sm.maskBones.erase(sm.maskBones.begin()); }
            } else sm.maskBones.erase(std::remove_if(sm.maskBones.begin(), sm.maskBones.end(), same), sm.maskBones.end());
        }
        ed.dirty = true;
    }
    if (inMask) ImGui::PopStyleColor();
    if (underRoot) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(underRoot ? "In the mask: under a ticked bone." : isRoot ? "A mask root: this bone and everything under it." : "Tick to drive this bone and the bones under it.");
    ImGui::SameLine();
    ImGuiTreeNodeFlags fl = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
    if (b.children.empty()) fl |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (inMask) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(LayerTint(sm.layer).x, LayerTint(sm.layer).y, LayerTint(sm.layer).z, 1.0f));
    const bool open = ImGui::TreeNodeEx("##t", fl, "%s", b.name.c_str());
    if (inMask) ImGui::PopStyleColor();
    if (open && !b.children.empty()) {
        for (int ch : b.children) DrawBoneTree(c, sm, nodes, ch, inMask);
        ImGui::TreePop();
    }
    ImGui::PopID();
}
// The bones a mask can name: the Character rig, or the model's animated nodes.
std::vector<BoneNode> BoneNodes(const Ctx& c, std::vector<int>& roots) {
    std::vector<BoneNode> nodes;
    roots.clear();
    if (c.body) {
        const std::vector<Character::Bone> sk = c.body->Skeleton();
        nodes.resize(sk.size());
        for (int i = 0; i < (int)sk.size(); ++i) {
            nodes[i].name = Character::BoneName(i);
            nodes[i].token = Character::BoneToken(i);
            if (sk[i].parent >= 0 && sk[i].parent < (int)sk.size()) nodes[sk[i].parent].children.push_back(i);
            else roots.push_back(i);
        }
        return nodes;
    }
    if (!c.ma || !c.ma->gameObject || !c.ma->gameObject->transform) return nodes;
    std::set<std::string> animated;
    for (const auto& cl : c.ma->clips) for (const auto& nc : cl.nodes) animated.insert(nc.node);
    // Transforms under the model root; branches with no animated node are left out.
    std::function<int(Transform*, int)> walk = [&](Transform* t, int parent) -> int {
        if (!t || !t->gameObject) return -1;
        std::vector<int> kids;
        const int self = (int)nodes.size();
        nodes.push_back({t->gameObject->name, t->gameObject->name, {}});
        for (Transform* ch : t->Children()) { const int k = walk(ch, self); if (k >= 0) kids.push_back(k); }
        const bool keep = animated.count(t->gameObject->name) > 0 || !kids.empty() || parent < 0;
        if (!keep) { nodes.resize(self); return -1; }
        nodes[self].children = kids;
        return self;
    };
    const int r = walk(c.ma->gameObject->transform, -1);
    if (r >= 0) roots.push_back(r);
    return nodes;
}
void DrawLayerTab(Ctx& c, Ui& u) {
    EditorState& ed = c.ed;
    SM& sm = *c.sm;
    if (sm.layer == 0) {
        ImGui::TextDisabled("Base layer");
        ImGui::TextWrapped("Plays on the whole body. Layers added with the + tab play their own states over it on part of the skeleton.");
        if (ImGui::Button("+ Add layer##agl")) u.forceLayerTab = -2;
        Tip("A layer: its own states, playing on part of the skeleton\n(e.g. the spine and up) over the base layer.");
        ImGui::Separator();
        for (SM* m : c.machines) {
            if (m == c.base) continue;
            const std::string nm = m->layerName.empty() ? "Layer " + std::to_string(m->layer) : m->layerName;
            ImGui::BulletText("%s  \xC2\xB7  weight %.2f  \xC2\xB7  %s%s", nm.c_str(), m->layerWeight, m->maskBone.empty() ? "whole body" : m->maskBone.c_str(), m->additive ? "  \xC2\xB7  additive" : "");
        }
        return;
    }
    ImGui::TextDisabled("Layer %d", sm.layer);
    TextEdit(ed, "Name##agl", sm.layerName, ("Layer " + std::to_string(sm.layer)).c_str());
    SliderF(ed, "Weight##agl", sm.layerWeight, 0.0f, 1.0f);
    Tip("How strongly this layer mixes in over the base pose.");
    Check(ed, "Additive##agl", sm.additive);
    Tip("Add this layer onto the base pose instead of replacing the masked bones.\nA recoil, aim offset, or lean while the body keeps walking.");
    ImGui::Separator();
    ImGui::TextDisabled("Mask: the bones this layer drives");
    if (sm.maskBone.empty() && sm.maskBones.empty()) ImGui::TextWrapped("Whole body. Tick a bone to limit the layer to it and the bones under it (the spine for upper-body actions).");
    std::vector<int> roots;
    const std::vector<BoneNode> nodes = BoneNodes(c, roots);
    if (nodes.empty()) ImGui::TextDisabled("No bones to pick from: the model has no animated nodes yet.");
    else if (ImGui::BeginChild("##agmask", ImVec2(0, std::min(320.0f, 22.0f * (float)nodes.size() + 8.0f)), ImGuiChildFlags_Borders)) {
        for (int r : roots) DrawBoneTree(c, sm, nodes, r, false);
    }
    if (!nodes.empty()) ImGui::EndChild();
    // Mask roots the rig does not have (typed in or from another model).
    auto known = [&](const std::string& tok) { for (const auto& b : nodes) if (Lower(b.token) == Lower(tok)) return true; return false; };
    std::vector<std::string> stray;
    if (!sm.maskBone.empty() && !known(sm.maskBone)) stray.push_back(sm.maskBone);
    for (const auto& b : sm.maskBones) if (!b.empty() && !known(b)) stray.push_back(b);
    for (const std::string& s : stray) {
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f), "\"%s\" is not a bone of this rig", s.c_str());
        ImGui::SameLine();
        ImGui::PushID(s.c_str());
        if (ImGui::SmallButton("remove")) {
            ed.PushUndo();
            if (sm.maskBone == s) sm.maskBone.clear(); else sm.maskBones.erase(std::remove(sm.maskBones.begin(), sm.maskBones.end(), s), sm.maskBones.end());
            ed.dirty = true;
        }
        ImGui::PopID();
    }
    if ((!sm.maskBone.empty() || !sm.maskBones.empty()) && ImGui::SmallButton("Whole body##agl")) { ed.PushUndo(); sm.maskBone.clear(); sm.maskBones.clear(); ed.dirty = true; }
    ImGui::Separator();
    if (ImGui::Button("Remove layer##agl")) u.forceLayerTab = -3;
    Tip("Delete this layer and its states (undo brings it back).");
}

// ---------------------------------------------------------------------------
// Problems
// ---------------------------------------------------------------------------
void DrawProblems(Ctx& c, Ui& u) {
    if (c.ProblemCount() == 0) {
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "No issues in this layer.");
        ImGui::TextDisabled("Checked: every state plays a clip that exists, and every\ntransition has a target, a parameter and a way to fire.");
    } else ImGui::TextDisabled("Click an issue to select what it is about.");
    bool notes = false;
    for (int k = 0; k < (int)c.issues.size(); ++k) {
        const SM::Issue& is = c.issues[k];
        if (is.severity == 0 && !notes) {
            notes = true;
            ImGui::Separator();
            ImGui::TextDisabled("Notes");
            Tip("Not wrong, worth knowing.");
        }
        ImGui::PushID(k);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (is.severity > 0) DrawWarnGlyph(ImGui::GetWindowDrawList(), ImVec2(p.x + 8.0f, p.y + ImGui::GetTextLineHeight() * 0.55f), 6.0f, is.severity >= 2 ? ErrCol() : WarnCol());
        else ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 8.0f, p.y + ImGui::GetTextLineHeight() * 0.55f), 3.0f, ImGui::GetColorU32(ImGuiCol_TextDisabled));
        ImGui::Dummy(ImVec2(18.0f, 0));
        ImGui::SameLine();
        if (ImGui::Selectable(is.text.c_str(), false, ImGuiSelectableFlags_None)) {
            if (is.any && is.transition >= 0) SelectAny(u, is.transition);
            else if (is.state >= 0 && is.transition >= 0) SelectTransition(u, is.state, is.transition);
            else if (is.state >= 0) { SelectState(u, is.state); u.forceSideTab = 0; }
            if (is.state >= 0 && is.state < (int)c.sm->states.size()) u.lastFocusX = 1.0f;   // frame it
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", is.text.c_str());
        ImGui::PopID();
    }
}

// ---------------------------------------------------------------------------
// Clips: every clip the machine can play; drag one onto the canvas, or tick
// several and make states for them in one go.
// ---------------------------------------------------------------------------
void DrawClipsTab(Ctx& c, Ui& u) {
    EditorState& ed = c.ed;
    SM& sm = *c.sm;
    if (u.clipPick.size() != c.slotNames.size()) u.clipPick.assign(c.slotNames.size(), 0);
    SearchField("##agcf", u.clipFilter, sizeof(u.clipFilter), "Filter clips", -1.0f);
    int ticked = 0;
    for (char t : u.clipPick) ticked += t ? 1 : 0;
    if (ImGui::SmallButton("All##agcall")) for (std::size_t i = 0; i < u.clipPick.size(); ++i) u.clipPick[i] = (!u.clipFilter[0] || Lower(c.slotNames[i]).find(Lower(u.clipFilter)) != std::string::npos) ? 1 : 0;
    ImGui::SameLine();
    if (ImGui::SmallButton("None##agcnone")) std::fill(u.clipPick.begin(), u.clipPick.end(), 0);
    ImGui::SameLine();
    ImGui::BeginDisabled(ticked == 0);
    if (ImGui::SmallButton(("Create " + std::to_string(ticked) + " state(s)##agcmk").c_str())) {
        ed.PushUndo();
        ImVec2 at = FreeSpot(sm);
        int made = 0;
        for (std::size_t i = 0; i < u.clipPick.size(); ++i) {
            if (!u.clipPick[i]) continue;
            SM::State st;
            st.name = UniqueName(sm, c.slotNames[i]);
            st.clip = c.slotNames[i];
            st.nx = at.x; st.ny = at.y + (float)(made % 6) * (kNodeH + 40.0f);
            if (made % 6 == 5) at.x += kNodeW + 60.0f;
            sm.states.push_back(std::move(st));
            ++made;
        }
        std::fill(u.clipPick.begin(), u.clipPick.end(), 0);
        ed.dirty = true;
        u.frameReq = true;
        ConsoleLog("Animator: created " + std::to_string(made) + " state(s) from clips");
    }
    ImGui::EndDisabled();
    Tip("One state per ticked clip, named after it.");
    ImGui::Separator();
    if (ImGui::BeginChild("##agcliplist", ImVec2(0, 0))) {
        for (std::size_t i = 0; i < c.slotNames.size(); ++i) {
            const std::string& n = c.slotNames[i];
            if (u.clipFilter[0] && Lower(n).find(Lower(u.clipFilter)) == std::string::npos) continue;
            ImGui::PushID((int)i);
            bool t = u.clipPick[i] != 0;
            if (ImGui::Checkbox("##ck", &t)) u.clipPick[i] = t ? 1 : 0;
            ImGui::SameLine();
            const bool playable = c.Playable(n);
            bool used = false;
            for (const auto& st : sm.states) {
                if (st.clip == n) used = true;
                for (const auto& m : st.motions) if (m.clip == n) used = true;
            }
            if (!playable) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::Selectable(n.c_str(), used, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(ImGui::GetContentRegionAvail().x - 26.0f, 0));
            if (!playable) ImGui::PopStyleColor();
            if (ImGui::BeginDragDropSource()) {
                char buf[96]; std::snprintf(buf, sizeof(buf), "%s", n.c_str());
                ImGui::SetDragDropPayload("AG_CLIP", buf, sizeof(buf));
                ImGui::Text("State for %s", n.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::IsItemHovered()) {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) AddState(c, u, n, FreeSpot(sm));
                ImGui::SetTooltip("%s%s\nDrag onto the canvas, or double-click, to add a state that plays it.", n.c_str(),
                                  !playable ? "  (only a child model has it)" : used ? "  (used by a state)" : "");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("+##mk")) AddState(c, u, n, FreeSpot(sm));
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Layer tabs across the top; + adds a layer.
// ---------------------------------------------------------------------------
void DrawLayerTabs(Ctx& c, Ui& u) {
    EditorState& ed = c.ed;
    if (!ImGui::BeginTabBar("##aglayers", ImGuiTabBarFlags_FittingPolicyScroll)) return;
    for (int li = 0; li < (int)c.machines.size(); ++li) {
        SM* m = c.machines[li];
        std::string lbl = li == 0 ? std::string("Base") : (m->layerName.empty() ? "Layer " + std::to_string(li) : m->layerName);
        if (li > 0 && m->additive) lbl += " +";
        lbl += "###agl" + std::to_string(li);
        if (li > 0) ImGui::PushStyleColor(ImGuiCol_Text, LayerTint(li));
        const bool open = ImGui::BeginTabItem(lbl.c_str(), nullptr, u.forceLayerTab == li ? ImGuiTabItemFlags_SetSelected : 0);
        if (li > 0) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered() && li > 0)
            ImGui::SetTooltip("Weight %.2f \xC2\xB7 %s%s", m->layerWeight, m->maskBone.empty() && m->maskBones.empty() ? "whole body" : (m->maskBone + (m->maskBones.empty() ? "" : " +" + std::to_string(m->maskBones.size()))).c_str(), m->additive ? " \xC2\xB7 additive" : "");
        if (open) {
            if (u.forceLayerTab < 0 || u.forceLayerTab == li) u.layer = li;
            ImGui::EndTabItem();
        }
    }
    bool addLayer = ImGui::TabItemButton("+##agaddlayer", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add a layer: its own states, playing on part of the skeleton\n(e.g. the spine and up) over the base layer.");
    if (u.forceLayerTab == -2) { addLayer = true; u.forceLayerTab = -1; }
    if (addLayer) {
        ed.PushUndo();
        auto* lay = c.obj->AddComponent<SM>();
        lay->layer = (int)c.machines.size();
        SM::State empty; empty.name = "Empty"; empty.nx = 60; empty.ny = 40;
        lay->states.push_back(empty);
        // A sensible default mask: the spine (upper body), if the rig has one.
        if (c.body) lay->maskBone = "torso";
        if (c.ma)
            for (const auto& cl : c.ma->clips)
                for (const auto& nc : cl.nodes)
                    if (lay->maskBone.empty() && Lower(nc.node).find("spine") != std::string::npos) lay->maskBone = nc.node;
        c.machines.push_back(lay);
        u.layer = (int)c.machines.size() - 1;
        u.forceLayerTab = u.layer;
        ed.dirty = true;
        ConsoleLog("Animator: added layer " + std::to_string(lay->layer));
    } else if (u.forceLayerTab >= 0 && u.layer == u.forceLayerTab) u.forceLayerTab = -1;
    ImGui::EndTabBar();
    if (u.forceLayerTab == -3) {   // remove the current layer
        u.forceLayerTab = -1;
        if (u.layer > 0 && u.layer < (int)c.machines.size()) {
            ed.PushUndo();
            SM* gone = c.machines[u.layer];
            c.obj->RemoveComponent(gone);
            c.machines.erase(c.machines.begin() + u.layer);
            // Layers after it move up one.
            for (int li = 1; li < (int)c.machines.size(); ++li) c.machines[li]->layer = li;
            u.layer = 0;
            u.forceLayerTab = 0;
            ed.dirty = true;
            ConsoleLog("Animator: removed a layer");
        }
    }
    if (u.layer >= (int)c.machines.size()) u.layer = 0;
    c.sm = c.machines[u.layer];
}

// ---------------------------------------------------------------------------
// Toolbar
// ---------------------------------------------------------------------------
void DrawToolbar(Ctx& c, Ui& u) {
    EditorState& ed = c.ed;
    SM& sm = *c.sm;
    ImGui::TextUnformatted(c.obj->name.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The object this Animator is on");
    ImGui::SameLine();
    if (ImGui::SmallButton("+ State##ag")) ImGui::OpenPopup("##agaddmenu");
    Tip("Add a state (or double-click empty canvas)");
    if (ImGui::BeginPopup("##agaddmenu")) {
        if (ImGui::MenuItem("Empty state")) AddState(c, u, c.clips.empty() ? std::string() : c.clips.front(), FreeSpot(sm));
        if (ImGui::BeginMenu("State for clip", !c.slotNames.empty())) {
            for (const std::string& cn : c.slotNames) if (ImGui::MenuItem(cn.c_str())) AddState(c, u, cn, FreeSpot(sm));
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("States from clips...")) u.forceSideTab = 4;
        if (ImGui::MenuItem("Blend tree")) {
            const int i = AddState(c, u, std::string(), FreeSpot(sm), "Blend");
            SM::State& bt = sm.states[i];
            bt.blendTree = true;
            bt.blendParam = c.ParamDeclared("speed") ? "speed" : (c.base->params.empty() ? "speed" : c.base->params.front().name);
            auto have = [&](const char* nm) { for (const auto& n : c.slotNames) if (Lower(n) == Lower(nm)) return n; return std::string(); };
            // Idle at 0 with walk and run, or whatever the library offers first.
            float at = 0.0f;
            for (const char* nm : {"Idle", "Walk", "Run"})
                if (std::string clip = have(nm); !clip.empty()) { bt.motions.push_back({clip, at}); at += at == 0.0f ? 2.0f : 4.0f; }
            for (std::size_t k = 0; k < c.slotNames.size() && bt.motions.size() < 3; ++k) {
                bool used = false;
                for (const auto& m : bt.motions) if (m.clip == c.slotNames[k]) used = true;
                if (!used) { bt.motions.push_back({c.slotNames[k], at}); at += 2.0f; }
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Any State transition")) u.pendingFrom = -2;
        if (ImGui::MenuItem("Layer")) u.forceLayerTab = -2;
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Auto Layout##ag")) AutoLayout(c, u);
    Tip("Arrange the states in columns by how far they are from the entry");
    ImGui::SameLine();
    if (ImGui::SmallButton("Frame##ag")) u.frameReq = true;
    Tip("Zoom and pan to fit every state (F)");
    ImGui::SameLine();
    ImGui::Checkbox("Labels##ag", &u.labels);
    Tip("Show each transition's conditions on its arrow (hover or select an arrow to see them anyway)");
    ImGui::SameLine();
    ImGui::Checkbox("Minimap##ag", &u.minimap);
    if (c.ma && sm.layer == 0 && !c.clips.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Locomotion setup##ag")) ImGui::OpenPopup("##aglocosetup");
        Tip("Build the usual character setup from this model's clips:\n"
            "idle/walk/run blended by speed, jump, fall and land,\n"
            "with the parameters the player controllers send.");
        if (ImGui::BeginPopup("##aglocosetup")) {
            ModelAnimator* ma = c.ma;
            auto shown = [&](const std::string& cn) { return cn.empty() || ma->FindClip(cn) < 0 ? "-" : cn.c_str(); };
            ImGui::TextDisabled("Clips mapped (empty slots are filled from the clip names):");
            ImGui::Text("Idle: %s\nWalk: %s\nRun:  %s\nJump: %s\nFall: %s\nLand: %s", shown(ma->idleClip), shown(ma->walkClip),
                        shown(ma->runClip), shown(ma->jumpClip), shown(ma->fallClip), shown(ma->landClip));
            ImGui::Text("Crouch: %s\nCrouch walk: %s\nProne: %s\nCrawl: %s\nGet up: %s", shown(ma->crouchClip),
                        shown(ma->crouchWalkClip), shown(ma->proneClip), shown(ma->crawlClip), shown(ma->getUpClip));
            if (!sm.states.empty()) ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1), "Replaces the %d existing state(s).", (int)sm.states.size());
            if (ImGui::Button("Build it", ImVec2(-1, 0))) {
                ed.PushUndo();
                okay::AutoMapLocomotion(ma);   // fills only the clip slots that are still empty
                ClipGraph::Build(sm, *ma);
                ma->driveByMovement = false;   // the machine drives the clips now
                ClearSel(u);
                u.frameReq = true;
                ed.dirty = true;
                ConsoleLog("Animator: built a locomotion setup with " + std::to_string(sm.states.size()) + " states");
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    const bool findGo = ImGui::InputTextWithHint("##agfind", "Find state", u.find, sizeof(u.find), ImGuiInputTextFlags_EnterReturnsTrue);
    Tip("Matching states light up; Enter selects and centres the first");
    if (findGo)
        for (int i = 0; i < (int)sm.states.size(); ++i)
            if (Lower(sm.states[i].name).find(Lower(u.find)) != std::string::npos) {
                SelectState(u, i);
                u.lastFocusX = 1.0f;
                break;
            }
    if (c.playing) {
        ImGui::SameLine();
        ImGui::TextColored(AccentCol(1.0f), "playing: %s", sm.Current().empty() ? "(entering)" : sm.Current().c_str());
    }
}

} // namespace

// ---------------------------------------------------------------------------
// The window
// ---------------------------------------------------------------------------
bool AnimatorGraphFocused() { return g_showAnimatorGraph && g_focused; }

void DrawAnimatorGraph(EditorState& ed) {
    g_focused = false;
    if (!g_showAnimatorGraph) return;
    if (std::getenv("OKAY_UI_ANIMGRAPH")) {
        ImGui::SetNextWindowPos(ImVec2(24, 48), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(1280, 780), ImGuiCond_Always);
    } else {
        ImGui::SetNextWindowSize(ImVec2(1100, 640), ImGuiCond_FirstUseEver);
    }
    if (!ImGui::Begin("Animator", &g_showAnimatorGraph)) { ImGui::End(); return; }
    g_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    // Resolve the machine from the selection: self, then ancestors, then
    // descendants (a player selected with the machine on the model root).
    SM* sm = nullptr; GameObject* smObj = nullptr;
    if (GameObject* go = ed.selected()) {
        sm = go->GetComponent<SM>(); smObj = go;
        if (!sm && go->transform)
            for (Transform* t = go->transform->Parent(); t && !sm; t = t->Parent())
                if (t->gameObject) { sm = t->gameObject->GetComponent<SM>(); smObj = t->gameObject; }
        if (!sm && go->transform)
            for (const auto& up : ed.scene().Objects())
                if (up && up->IsSelfOrDescendantOf(go))
                    if (auto* m = up->GetComponent<SM>()) { sm = m; smObj = up.get(); break; }
    }
    if (!sm && !U().lastObj.empty() && !ed.selected()) {
        // Nothing selected (Play clears the selection): stay on the object shown last.
        for (const auto& up : ed.scene().Objects())
            if (up && up->name == U().lastObj)
                if (auto* m = up->GetComponent<SM>()) { sm = m; smObj = up.get(); break; }
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
                if (auto* ch = up->GetComponent<Character>(); ch && !bodyGo && !up->GetComponent<ModelAnimator>()) bodyGo = ch;
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
            Tip("Every state and transition then shows here to edit and preview.\n"
                "It takes the parameters the player controllers send (speed, grounded, crouch, prone, jump).");
            ImGui::End(); return;
        }
        ImGui::TextDisabled("Select an object with an Animator (Add Component > Animation).");
        ImGui::TextDisabled("States name a clip (the Model Animator's, or the Character's); transitions switch\nbetween them when a condition passes (clip end, float compare, bool, trigger).");
        ImGui::End(); return;
    }

    Ctx c(ed);
    c.obj = smObj;
    U().lastObj = smObj->name;
    c.machines = smObj->GetComponents<SM>();
    std::stable_sort(c.machines.begin(), c.machines.end(), [](SM* a, SM* b) { return a->layer < b->layer; });
    if (c.machines.empty()) { ImGui::End(); return; }
    c.base = c.machines.front();
    for (SM* m : c.machines) if (m->layer == 0) { c.base = m; break; }
    c.ma = smObj->GetComponent<ModelAnimator>();
    c.body = c.ma ? nullptr : smObj->GetComponent<Character>();
    c.playing = ed.isPlaying();
    BuildClipLists(c);

    Ui& u = U();
    if (u.layerObj != smObj) { u.layerObj = smObj; u.layer = 0; }
    if (const char* e = std::getenv("OKAY_UI_AGLAYER")) {   // dev screenshots: open on a layer
        static bool once = false;
        if (!once) { u.layer = std::atoi(e); u.forceLayerTab = u.layer; once = true; }
    }
    DrawLayerTabs(c, u);
    sm = c.sm;
    const std::string key = smObj->name + "#" + std::to_string(u.layer);
    if (u.key != key) {
        u.key = key;
        ClearSel(u);
        u.pendingFrom = -1; u.renaming = -1;
        u.pan = ImVec2(60, 60); u.zoom = 1.0f;
        u.frameReq = true;
        u.clipPick.clear();
    }
    ClampSel(u, *sm);
    {   // OKAY_UI_AGSEL=<state> / OKAY_UI_AGTR=<state>:<n> / OKAY_UI_AGTAB=<n>: dev screenshots.
        static bool s_devSel = false;
        if (!s_devSel) {
            if (const char* e = std::getenv("OKAY_UI_AGSEL"))
                for (int i = 0; i < (int)sm->states.size(); ++i)
                    if (sm->states[i].name == e) { SelectState(u, i); u.lastFocusX = 1.0f; s_devSel = true; break; }
            if (const char* e = std::getenv("OKAY_UI_AGTR")) {
                const std::string s = e;
                const std::size_t colon = s.find(':');
                const int st = c.StateIndex(s.substr(0, colon)), ti = colon == std::string::npos ? 0 : std::atoi(s.c_str() + colon + 1);
                if (st >= 0 && ti < (int)sm->states[st].transitions.size()) { SelectTransition(u, st, ti); s_devSel = true; }
            }
            if (const char* e = std::getenv("OKAY_UI_AGTAB")) { u.forceSideTab = std::atoi(e); s_devSel = true; }
        }
    }
    // Auto-layout states that have never been placed (all at 0,0).
    bool anyPlaced = false;
    for (const auto& st : sm->states) if (st.nx != 0.0f || st.ny != 0.0f) { anyPlaced = true; break; }
    if (!anyPlaced && sm->states.size() > 1) AutoLayout(c, u);
    c.Validate();

    DrawToolbar(c, u);
    ClampSel(u, *sm);

    // ---- canvas | splitter | side panel ----
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    u.sideW = std::clamp(u.sideW, 240.0f, std::max(240.0f, avail.x * 0.6f));
    DrawCanvas(c, u, ImVec2(avail.x - u.sideW - 8.0f, avail.y));
    ImGui::SameLine(0, 0);
    ImGui::InvisibleButton("##agsplit", ImVec2(8.0f, avail.y));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive()) u.sideW = std::clamp(u.sideW - ImGui::GetIO().MouseDelta.x, 240.0f, std::max(240.0f, avail.x * 0.6f));
    {
        const ImRect r(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImGui::GetWindowDrawList()->AddLine(ImVec2(r.Min.x + 4, r.Min.y + 8), ImVec2(r.Min.x + 4, r.Max.y - 8),
                                            ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_SeparatorActive : ImGui::IsItemHovered() ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator), 2.0f);
    }
    ImGui::SameLine(0, 0);
    ImGui::BeginChild("##agside", ImVec2(u.sideW, avail.y), ImGuiChildFlags_Borders);
    ClampSel(u, *sm);
    // A state chosen through Find / Problems: bring it into view.
    if (u.lastFocusX > 0.0f && u.primary >= 0) {
        u.lastFocusX = 0.0f;
        const ImVec2 canvasSz(avail.x - u.sideW - 8.0f, avail.y);
        u.pan = ImVec2(canvasSz.x * 0.5f - (sm->states[u.primary].nx + kNodeW * 0.5f) * u.zoom, canvasSz.y * 0.5f - (sm->states[u.primary].ny + kNodeH * 0.5f) * u.zoom);
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, ImGui::GetStyle().FramePadding.y));
    const bool sideTabs = ImGui::BeginTabBar("##agsidetabs", ImGuiTabBarFlags_FittingPolicyResizeDown);
    ImGui::PopStyleVar();
    if (sideTabs) {
        int probs = 0;
        for (const auto& is : c.issues) probs += is.severity > 0 ? 1 : 0;
        const char* selLabel = u.primary >= 0 ? "State###agt0" : u.selTrFrom >= 0 ? "Transition###agt0" : u.selAny >= 0 ? "Any State###agt0" : "Animator###agt0";
        const std::string probLabel = probs > 0 ? "Issues " + std::to_string(probs) + "###agt3" : "Issues###agt3";
        auto tab = [&](const char* label, int idx) {
            return ImGui::BeginTabItem(label, nullptr, u.forceSideTab == idx ? ImGuiTabItemFlags_SetSelected : 0);
        };
        if (tab(selLabel, 0)) {
            if (u.primary >= 0 && u.primary < (int)sm->states.size()) DrawStateInspector(c, u, u.primary);
            else if (u.selTrFrom >= 0) DrawTransitionInspector(c, u);
            else if (u.selAny >= 0) DrawAnyInspector(c, u);
            else DrawMachineInspector(c, u);
            ImGui::EndTabItem();
        }
        if (tab("Params###agt1", 1)) { DrawParameters(c, u); ImGui::EndTabItem(); }
        if (tab("Layer###agt2", 2)) { DrawLayerTab(c, u); ImGui::EndTabItem(); }
        if (probs > 0) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.75f, 0.25f, 1.0f));
        const bool probOpen = tab(probLabel.c_str(), 3);
        if (probs > 0) ImGui::PopStyleColor();
        if (probOpen) { DrawProblems(c, u); ImGui::EndTabItem(); }
        if (tab("Clips###agt4", 4)) { DrawClipsTab(c, u); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
        u.forceSideTab = -1;
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace oked
