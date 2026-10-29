// The Behavior Tree window: edits `.okaybt` assets. The canvas shows the tree
// top-down (root at the top, children left to right in the order they run) with
// pan, zoom, box select, drag to move / reorder / reparent, inline rename and a
// minimap. A palette adds nodes by category, the side panel edits the selected
// node's parameters, the blackboard keys and lists the problems. In Play the
// nodes light up as the runner ticks them and the blackboard shows live values.
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "BehaviorTreeWindow.hpp"
#include "ConsolePanel.hpp"             // ConsoleLog
#include "EditorWidgets.hpp"            // AccentCol
#include "okay/AI/BehaviorTree.hpp"
#include "okay/AI/Navigation.hpp"
#include "okay/Components/BehaviorTreeRunner.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/NPCController.hpp"
#include "okay/Components/NavMeshAgent.hpp"
#include "okay/Components/NavMeshSurface.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "tinyfiledialogs.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::editor;

namespace oked {

bool g_showBehaviorTree = false;

namespace {

constexpr float kNodeW = 176.0f, kNodeH = 56.0f, kServiceRowH = 18.0f;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
std::string Fmt(const char* f, double v) { char b[64]; std::snprintf(b, sizeof(b), f, v); return b; }
float Len(ImVec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }
ImU32 Col(float r, float g, float b, float a = 1.0f) { return ImGui::GetColorU32(ImVec4(r, g, b, a)); }
ImU32 WarnCol(float a = 1.0f) { return Col(0.95f, 0.75f, 0.25f, a); }
ImU32 ErrCol(float a = 1.0f) { return Col(0.95f, 0.38f, 0.35f, a); }
ImU32 OkCol(float a = 1.0f) { return Col(0.35f, 0.85f, 0.45f, a); }
ImU32 PlayCol(float a = 1.0f) { return Col(0.95f, 0.70f, 0.20f, a); }
ImU32 AbortCol(float a = 1.0f) { return Col(0.80f, 0.45f, 0.95f, a); }
ImVec4 KindTint(BtNodeKind k) {
    switch (k) {
        case BtNodeKind::Composite: return ImVec4(0.35f, 0.62f, 0.90f, 1);
        case BtNodeKind::Decorator: return ImVec4(0.62f, 0.48f, 0.88f, 1);
        case BtNodeKind::Task:      return ImVec4(0.30f, 0.75f, 0.55f, 1);
        case BtNodeKind::Service:   return ImVec4(0.90f, 0.60f, 0.30f, 1);
    }
    return ImVec4(0.5f, 0.5f, 0.5f, 1);
}
const char* KindName(BtNodeKind k) {
    switch (k) {
        case BtNodeKind::Composite: return "Composites";
        case BtNodeKind::Decorator: return "Decorators";
        case BtNodeKind::Task:      return "Tasks";
        case BtNodeKind::Service:   return "Services";
    }
    return "";
}
void Tip(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", text);
}
// A small warning / error triangle with "!" in it.
void DrawWarnGlyph(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    dl->AddTriangleFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r * 0.95f, c.y + r * 0.75f), ImVec2(c.x - r * 0.95f, c.y + r * 0.75f), col);
    const ImU32 ink = Col(0.1f, 0.1f, 0.1f);
    dl->AddLine(ImVec2(c.x, c.y - r * 0.35f), ImVec2(c.x, c.y + r * 0.2f), ink, std::max(1.0f, r * 0.22f));
    dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.45f), std::max(1.0f, r * 0.14f), ink);
}
std::vector<std::string> SplitList(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        std::size_t a = 0, b = item.size();
        while (a < b && std::isspace((unsigned char)item[a])) ++a;
        while (b > a && std::isspace((unsigned char)item[b - 1])) --b;
        if (b > a) out.push_back(item.substr(a, b - a));
    }
    return out;
}
std::string Join(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& x : v) { if (!s.empty()) s += ", "; s += x; }
    return s;
}
// A path relative to the project folder when it lies inside it (what a runner stores).
std::string ProjectRelative(EditorState& ed, const std::string& path) {
    namespace fs = std::filesystem;
    if (ed.projectDir().empty() || path.empty()) return path;
    std::error_code ec;
    const fs::path rel = fs::path(path).lexically_relative(fs::path(ed.projectDir()));
    const std::string r = rel.generic_string();
    if (r.empty() || r.rfind("..", 0) == 0) return path;
    return r;
}
std::string ProjectAssetsDir(EditorState& ed) {
    namespace fs = std::filesystem;
    if (ed.projectDir().empty()) return std::string();
    std::error_code ec;
    const fs::path a = fs::path(ed.projectDir()) / "Assets";
    if (fs::is_directory(a, ec)) return a.string();
    return ed.projectDir();
}

// What a node card says under its title.
std::string Summary(const BehaviorTree& t, const BtNode& n) {
    auto s = [&](const char* k) { return n.Str(k); };
    switch (n.type) {
        case BtNodeType::Parallel: return "succeed: " + s("success") + "  fail: " + s("failure");
        case BtNodeType::Repeat: { const int c = n.Int("count"); return c > 0 ? "x " + std::to_string(c) : "forever"; }
        case BtNodeType::Cooldown: return Fmt("%g s", n.Num("seconds"));
        case BtNodeType::TimeLimit: return Fmt("%g s", n.Num("seconds"));
        case BtNodeType::Condition:
        case BtNodeType::Guard: {
            const std::string kind = s("kind");
            if (kind == "distance") return "dist(" + s("target") + ") " + ((s("op") == ">" || s("op") == ">=") ? "> " : "< ") + Fmt("%g", n.Num("distance"));
            if (kind == "lineOfSight") return "sees " + s("target");
            if (kind == "health") return Fmt("health < %g", n.Num("fraction"));
            const std::string op = s("op");
            if (op == "set" || op == "unset") return s("key") + " is " + op;
            return s("key") + " " + op + " " + s("value");
        }
        case BtNodeType::Wait: { const float lo = n.Num("min"), hi = n.Num("max"); return hi > lo ? Fmt("%g", lo) + Fmt(" - %g s", hi) : Fmt("%g s", lo); }
        case BtNodeType::MoveTo: return s("target").empty() ? "to " + s("position") : "to " + s("target");
        case BtNodeType::Patrol: { const auto w = SplitList(s("waypoints")); return std::to_string(w.size()) + (w.size() == 1 ? " waypoint, " : " waypoints, ") + s("mode"); }
        case BtNodeType::Wander: return Fmt("radius %g", n.Num("radius"));
        case BtNodeType::Chase: return s("target") + Fmt("  stop %g", n.Num("stopDistance"));
        case BtNodeType::Flee: return "from " + s("target");
        case BtNodeType::LookAt: return s("target");
        case BtNodeType::PlayAnimation: return !s("clip").empty() ? s("clip") : !s("trigger").empty() ? "trigger " + s("trigger") : !s("param").empty() ? s("param") + " = " + s("value") : "(nothing)";
        case BtNodeType::Attack: return s("target") + (n.Num("range") > 0.0f ? Fmt("  range %g", n.Num("range")) : "");
        case BtNodeType::SetBlackboard: return s("key") + " = " + s("value");
        case BtNodeType::SendEvent: return s("event") + (n.Flag("broadcast") ? " (broadcast)" : s("target").empty() ? "" : " -> " + s("target"));
        case BtNodeType::RunScript:
        case BtNodeType::Custom: return s("function") + "()";
        case BtNodeType::PlaySound: return s("clip").empty() ? "the Audio Source" : s("clip");
        case BtNodeType::Perception: return s("targetName") + Fmt("  %g m", n.Num("range"));
        case BtNodeType::HealthWatcher: return "-> " + s("healthKey");
        default: {
            const std::size_t c = t.FlowChildren(n.id).size();
            return c == 0 ? "(empty)" : std::to_string(c) + (c == 1 ? " child" : " children");
        }
    }
}

// ---------------------------------------------------------------------------
// The document: the tree being edited, its file and its undo history.
// ---------------------------------------------------------------------------
struct Doc {
    BehaviorTree tree;
    std::string path;                 // the file ("" until saved)
    bool dirty = false;
    std::vector<std::string> undo, redo;
    std::string loadedText;           // what the file held when opened / saved
};
Doc& D() { static Doc d; return d; }

void DocPush() {
    Doc& d = D();
    d.undo.push_back(d.tree.Serialize());
    if (d.undo.size() > 100) d.undo.erase(d.undo.begin());
    d.redo.clear();
}
void DocTouch() { D().dirty = true; }
bool DocUndo() {
    Doc& d = D();
    if (d.undo.empty()) return false;
    d.redo.push_back(d.tree.Serialize());
    BehaviorTree t;
    if (t.Deserialize(d.undo.back())) d.tree = t;
    d.undo.pop_back();
    d.dirty = d.tree.Serialize() != d.loadedText;
    return true;
}
bool DocRedo() {
    Doc& d = D();
    if (d.redo.empty()) return false;
    d.undo.push_back(d.tree.Serialize());
    BehaviorTree t;
    if (t.Deserialize(d.redo.back())) d.tree = t;
    d.redo.pop_back();
    d.dirty = d.tree.Serialize() != d.loadedText;
    return true;
}
void DocNew(const BehaviorTree& t, const std::string& path) {
    Doc& d = D();
    d.tree = t; d.path = path; d.dirty = path.empty() || !std::filesystem::exists(path);
    d.undo.clear(); d.redo.clear();
    d.loadedText = d.dirty ? std::string() : t.Serialize();
}
bool DocOpen(const std::string& path, std::string* err) {
    BehaviorTree t;
    if (!t.LoadFromFile(path, err)) return false;
    bool anyPlaced = false;
    for (const auto& n : t.nodes) if (n.x != 0.0f || n.y != 0.0f) { anyPlaced = true; break; }
    if (!anyPlaced && t.nodes.size() > 1) t.Layout(kNodeW, kNodeH, 22.0f, 64.0f, kServiceRowH);
    DocNew(t, path);
    D().dirty = false;
    D().loadedText = t.Serialize();
    return true;
}
bool DocSave(const std::string& path, std::string* err) {
    Doc& d = D();
    if (!d.tree.SaveToFile(path, err)) return false;
    d.path = path; d.dirty = false; d.loadedText = d.tree.Serialize();
    BehaviorTree::Forget(path);   // runners read the new file next time
    return true;
}

// Undo-aware widget: one undo step per edit session on an item.
ImGuiID g_editItem = 0;
template <class T, class W> bool Tweak(T& v, W&& widget) {
    const T before = v;
    const bool changed = widget(v);
    const ImGuiID id = ImGui::GetItemID();
    if (changed) {
        if (!(ImGui::IsItemActive() && g_editItem == id)) {
            T after = v;
            v = before;
            DocPush();
            v = after;
            g_editItem = ImGui::IsItemActive() ? id : 0;
        }
        DocTouch();
    }
    if (g_editItem == id && !ImGui::IsItemActive()) g_editItem = 0;
    return changed;
}
void EditPush() {
    const ImGuiID id = ImGui::GetItemID();
    if (ImGui::IsItemActive() && g_editItem == id) return;
    DocPush();
    g_editItem = ImGui::IsItemActive() ? id : 0;
}

// ---------------------------------------------------------------------------
// Window state
// ---------------------------------------------------------------------------
struct Ui {
    ImVec2 pan{60.0f, 40.0f};
    float zoom = 1.0f;
    bool frameReq = true, minimap = true, tidy = true;
    bool panning = false, boxing = false;
    ImVec2 boxStart{}, rightDown{};
    std::vector<int> sel;             // selected node ids; `primary` is the one the inspector shows
    int primary = -1;
    int hov = -1;
    int renaming = -1; char renameBuf[64] = ""; bool renameFocus = false;
    int dragNode = -1; bool dragPushed = false, dragMoved = false; int dropTarget = -1;
    float sideW = 360.0f, paletteW = 180.0f;
    int forceSideTab = -1;            // 0 Node, 1 Blackboard, 2 Issues, 3 Runner
    char find[48] = "";
    std::string clipboard;            // a subtree, as tree text
    float focusX = 0.0f;              // bring the primary node into view
    std::string liveObj;              // the runner shown in Play (object name)
    bool showPalette = true;
};
Ui& U() { static Ui u; return u; }
bool g_focused = false;

bool IsSel(const Ui& u, int id) { return std::find(u.sel.begin(), u.sel.end(), id) != u.sel.end(); }
void ClearSel(Ui& u) { u.sel.clear(); u.primary = -1; }
void Select(Ui& u, int id, int mode = 0) {   // 0 only, 1 add, 2 toggle
    if (mode == 0) { u.sel.assign(1, id); u.primary = id; return; }
    auto it = std::find(u.sel.begin(), u.sel.end(), id);
    if (mode == 2 && it != u.sel.end()) { u.sel.erase(it); u.primary = u.sel.empty() ? -1 : u.sel.back(); return; }
    if (it == u.sel.end()) u.sel.push_back(id);
    u.primary = id;
}
void ClampSel(Ui& u, const BehaviorTree& t) {
    u.sel.erase(std::remove_if(u.sel.begin(), u.sel.end(), [&](int id) { return !t.Find(id); }), u.sel.end());
    if (u.primary >= 0 && !t.Find(u.primary)) u.primary = u.sel.empty() ? -1 : u.sel.back();
    if (u.renaming >= 0 && !t.Find(u.renaming)) u.renaming = -1;
}

// ---------------------------------------------------------------------------
// What the window edits this frame
// ---------------------------------------------------------------------------
struct Ctx {
    EditorState& ed;
    BehaviorTree& t;
    std::vector<BehaviorTree::Issue> issues;
    std::map<int, int> severity;        // per node: 1 warning, 2 error
    BehaviorTreeRunner* live = nullptr; // the runner feeding the live view
    bool playing = false;
    explicit Ctx(EditorState& e) : ed(e), t(D().tree) {}
    void Validate() {
        issues = t.Validate(&ed.scene());
        severity.clear();
        for (const auto& is : issues) if (is.node >= 0) severity[is.node] = std::max(severity[is.node], is.severity);
    }
    int Severity(int id) const { auto it = severity.find(id); return it == severity.end() ? 0 : it->second; }
    std::string Issues(int id) const {
        std::string out;
        for (const auto& is : issues) if (is.node == id) { if (!out.empty()) out += "\n"; out += is.text; }
        return out;
    }
    int ProblemCount() const { int n = 0; for (const auto& is : issues) n += is.severity >= 2 ? 1 : 0; return n; }
};

// Every runner in the scene whose tree is the file being edited (any runner when
// nothing is edited yet).
std::vector<BehaviorTreeRunner*> RunnersFor(EditorState& ed) {
    std::vector<BehaviorTreeRunner*> out;
    const std::string mine = D().path.empty() ? std::string() : std::filesystem::path(D().path).filename().string();
    for (BehaviorTreeRunner* r : ed.scene().FindObjectsOfType<BehaviorTreeRunner>()) {
        if (!r || !r->gameObject) continue;
        if (mine.empty() || std::filesystem::path(r->treePath).filename().string() == mine) out.push_back(r);
    }
    return out;
}

void Layout(Ctx& c) { c.t.Layout(kNodeW, kNodeH, 22.0f, 64.0f, kServiceRowH); }
void Tidy(Ctx& c) { if (U().tidy) Layout(c); }

// Add a node of `type` relative to the selection: under a composite / decorator,
// beside a task, attached to any node when it is a service.
int AddFromPalette(Ctx& c, BtNodeType type, int near) {
    BehaviorTree& t = c.t;
    DocPush();
    int parent = -1, at = -1;
    if (const BtNode* n = t.Find(near)) {
        if (BtIsService(type)) parent = near;
        else if (n->Kind() == BtNodeKind::Composite || n->Kind() == BtNodeKind::Decorator) parent = near;
        else if (n->parent >= 0) {
            parent = n->parent;
            const std::vector<int> kids = t.FlowChildren(parent);
            auto it = std::find(kids.begin(), kids.end(), near);
            at = it == kids.end() ? -1 : (int)(it - kids.begin()) + 1;
            if (at >= 0) {   // position among all children (services included)
                const BtNode* p = t.Find(parent);
                auto pit = std::find(p->children.begin(), p->children.end(), near);
                at = pit == p->children.end() ? -1 : (int)(pit - p->children.begin()) + 1;
            }
        }
    } else if (t.root >= 0 && !BtIsService(type)) parent = t.root;
    const int id = t.AddNode(type, parent, at);
    if (BtNode* nn = t.Find(id); nn && parent >= 0) {
        const BtNode* p = t.Find(parent);
        nn->x = p->x; nn->y = p->y + kNodeH + 64.0f;
    }
    Tidy(c);
    DocTouch();
    Select(U(), id);
    U().forceSideTab = 0;
    return id;
}

void DeleteNodes(Ctx& c, std::vector<int> ids) {
    if (ids.empty()) return;
    DocPush();
    for (int id : ids) c.t.RemoveSubtree(id);
    ClearSel(U());
    Tidy(c);
    DocTouch();
}

// Copy a subtree as text (a small tree of its own) and paste it under a parent.
std::string CopySubtree(const BehaviorTree& t, int id) {
    BehaviorTree sub;
    sub.keys = t.keys;
    std::function<void(int, int)> copy = [&](int from, int toParent) {
        const BtNode* n = t.Find(from);
        if (!n) return;
        const int nid = sub.AddNode(n->type, toParent);
        BtNode* nn = sub.Find(nid);
        nn->name = n->name; nn->params = n->params; nn->x = n->x; nn->y = n->y;
        for (int ch : n->children) copy(ch, nid);
    };
    copy(id, -1);
    return sub.Serialize();
}
int PasteSubtree(Ctx& c, const std::string& text, int parent, ImVec2 at) {
    BehaviorTree sub;
    if (!sub.Deserialize(text) || sub.root < 0) return -1;
    DocPush();
    std::map<int, int> ids;
    std::function<void(int, int)> paste = [&](int from, int toParent) {
        const BtNode* n = sub.Find(from);
        if (!n) return;
        const int nid = c.t.AddNode(n->type, toParent);
        BtNode* nn = c.t.Find(nid);
        nn->name = n->name; nn->params = n->params;
        nn->x = n->x - sub.Find(sub.root)->x + at.x; nn->y = n->y - sub.Find(sub.root)->y + at.y;
        ids[from] = nid;
        for (int ch : n->children) paste(ch, nid);
    };
    paste(sub.root, parent);
    for (const auto& k : sub.keys) if (!c.t.Key(k.name)) c.t.DeclareKey(k.name, k.value);
    Tidy(c);
    DocTouch();
    const int nid = ids[sub.root];
    Select(U(), nid);
    return nid;
}

// ---------------------------------------------------------------------------
// The canvas
// ---------------------------------------------------------------------------
struct Card { int id; ImRect r; };

void DrawCanvas(Ctx& c, Ui& u, ImVec2 size) {
    BehaviorTree& t = c.t;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::BeginChild("##btcanvas", size, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 org = ImGui::GetCursorScreenPos();
    const ImVec2 csz = ImGui::GetContentRegionAvail();
    const ImRect canvasRect(org, org + csz);
    const bool canvasHovered = ImGui::IsWindowHovered();
    const bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const bool keys = windowFocused && !io.WantTextInput;
    const ImVec2 mouse = io.MousePos;

    // ---- view ----
    if (canvasHovered && io.MouseWheel != 0.0f && !io.KeyCtrl) {
        const ImVec2 g = (mouse - org - u.pan) / u.zoom;
        u.zoom = std::clamp(u.zoom * (io.MouseWheel > 0.0f ? 1.12f : 1.0f / 1.12f), 0.25f, 2.5f);
        u.pan = mouse - org - g * u.zoom;
    }
    auto bounds = [&](float& x0, float& y0, float& x1, float& y1) {
        x0 = 1e9f; y0 = 1e9f; x1 = -1e9f; y1 = -1e9f;
        for (const auto& n : t.nodes) {
            if (BtIsService(n.type)) continue;
            x0 = std::min(x0, n.x); y0 = std::min(y0, n.y);
            x1 = std::max(x1, n.x + kNodeW); y1 = std::max(y1, n.y + BehaviorTree::CardHeight(t, n.id, kNodeH, kServiceRowH));
        }
        return x1 >= x0;
    };
    auto frameAll = [&]() {
        float x0, y0, x1, y1;
        if (!bounds(x0, y0, x1, y1)) { u.pan = ImVec2(csz.x * 0.5f - kNodeW * 0.5f, 40.0f); u.zoom = 1.0f; return; }
        u.zoom = std::clamp(std::min((csz.x - 80.0f) / std::max(x1 - x0, 1.0f), (csz.y - 80.0f) / std::max(y1 - y0, 1.0f)), 0.25f, 1.25f);
        u.pan = ImVec2(csz.x * 0.5f - (x0 + x1) * 0.5f * u.zoom, csz.y * 0.5f - (y0 + y1) * 0.5f * u.zoom);
    };
    if (u.frameReq) { u.frameReq = false; frameAll(); }
    const float Z = u.zoom;
    auto toScreen = [&](ImVec2 g) { return org + u.pan + g * Z; };
    auto toGraph = [&](ImVec2 s) { return (s - org - u.pan) / Z; };
    auto centreOn = [&](ImVec2 g) { u.pan = ImVec2(csz.x * 0.5f - g.x * Z, csz.y * 0.5f - g.y * Z); };
    if (u.focusX > 0.0f && u.primary >= 0) {
        u.focusX = 0.0f;
        if (const BtNode* n = t.Find(u.primary)) centreOn(ImVec2(n->x + kNodeW * 0.5f, n->y + kNodeH * 0.5f));
    }
    auto findMatch = [&](const BtNode& n) {
        if (!u.find[0]) return false;
        const std::string f = Lower(u.find);
        return Lower(n.name).find(f) != std::string::npos || Lower(n.Def().title).find(f) != std::string::npos;
    };

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

    // ---- cards ----
    std::vector<Card> cards;
    for (const auto& n : t.nodes) {
        if (BtIsService(n.type)) continue;
        const ImVec2 p = toScreen(ImVec2(n.x, n.y));
        cards.push_back({n.id, ImRect(p, p + ImVec2(kNodeW * Z, BehaviorTree::CardHeight(t, n.id, kNodeH, kServiceRowH) * Z))});
    }
    auto cardOf = [&](int id) -> const Card* { for (const Card& k : cards) if (k.id == id) return &k; return nullptr; };
    ImFont* font = ImGui::GetFont();
    const float fs = ImGui::GetFontSize() * std::clamp(Z, 0.55f, 1.3f);
    u.hov = -1;
    for (int i = (int)cards.size() - 1; i >= 0; --i) if (cards[(std::size_t)i].r.Contains(mouse)) { u.hov = cards[(std::size_t)i].id; break; }

    // ---- live state ----
    const BehaviorTreeRunner* live = c.live;
    const float now = live ? live->Clock() : 0.0f;
    auto liveState = [&](int id) -> const BehaviorTreeRunner::NodeState* { return live ? live->State(id) : nullptr; };
    auto statusCol = [&](BtStatus s, float a) {
        switch (s) {
            case BtStatus::Success: return OkCol(a);
            case BtStatus::Failure: return ErrCol(a);
            case BtStatus::Aborted: return AbortCol(a);
            case BtStatus::Running: return PlayCol(a);
            default: return ImGui::GetColorU32(ImGuiCol_Border, a);
        }
    };

    // ---- edges: parent bottom to child top, an elbow half way ----
    for (const Card& k : cards) {
        const BtNode* n = t.Find(k.id);
        const Card* pc = n && n->parent >= 0 ? cardOf(n->parent) : nullptr;
        if (!pc) continue;
        const ImVec2 a(pc->r.GetCenter().x, pc->r.Max.y), b(k.r.GetCenter().x, k.r.Min.y);
        const float midY = (a.y + b.y) * 0.5f;
        const auto* st = liveState(k.id);
        const bool running = st && st->running;
        const bool recent = st && st->lastTick >= 0.0f && now - st->lastTick < 0.8f;
        ImU32 col = running ? PlayCol(0.95f) : recent ? statusCol(st->status, 0.8f)
                  : IsSel(u, k.id) ? ImGui::GetColorU32(AccentCol()) : ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.8f);
        const float th = running ? 3.0f : IsSel(u, k.id) ? 2.2f : 1.6f;
        dl->AddLine(a, ImVec2(a.x, midY), col, th);
        dl->AddLine(ImVec2(a.x, midY), ImVec2(b.x, midY), col, th);
        dl->AddLine(ImVec2(b.x, midY), b, col, th);
        dl->AddCircleFilled(b, std::max(2.5f, 3.5f * Z), col);
    }

    // ---- nodes ----
    bool itemHovered = false;
    int ctxNode = -1;
    static int s_ctxNode = -1;
    int dropNow = -1;   // where the dragged node(s) would be dropped
    if (u.dragNode >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left) && u.dragMoved && u.hov >= 0 && !IsSel(u, u.hov)) {
        bool ok = true;
        for (int s : u.sel) if (s == u.hov || t.IsAncestor(s, u.hov)) ok = false;
        if (ok) dropNow = u.hov;
    }
    for (const Card& k : cards) {
        BtNode& n = *t.Find(k.id);
        const ImRect r = k.r;
        if (!canvasRect.Overlaps(r)) continue;
        ImGui::SetCursorScreenPos(r.Min);
        ImGui::PushID(k.id);
        ImGui::InvisibleButton("##node", r.GetSize());
        const bool hov = ImGui::IsItemHovered();
        itemHovered |= hov;
        if (ImGui::IsItemActivated()) {
            if (io.KeyShift || io.KeyCtrl) Select(u, k.id, 2);
            else if (!IsSel(u, k.id)) Select(u, k.id);
            else u.primary = k.id;
            u.forceSideTab = 0;
            u.dragNode = k.id; u.dragPushed = false; u.dragMoved = false; u.dropTarget = -1;
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f) && u.renaming != k.id) {
            if (!u.dragPushed) { DocPush(); u.dragPushed = true; }
            const ImVec2 d = io.MouseDelta / Z;
            auto moveTree = [&](int id, const ImVec2& dd) {
                std::function<void(int)> mv = [&](int x) {
                    if (BtNode* nn = t.Find(x)) { nn->x += dd.x; nn->y += dd.y; for (int ch : nn->children) mv(ch); }
                };
                mv(id);
            };
            if (IsSel(u, k.id)) { for (int s : u.sel) { bool under = false; for (int o : u.sel) if (o != s && t.IsAncestor(o, s)) under = true; if (!under) moveTree(s, d); } }
            else moveTree(k.id, d);
            u.dragMoved = true;
            DocTouch();
        }
        if (ImGui::IsItemDeactivated() && u.dragNode == k.id) {
            if (u.dragMoved) {
                // Dropped on another node: it becomes the parent. Otherwise the
                // new x order among the siblings is the new run order.
                if (dropNow >= 0) {
                    for (int s : u.sel) { bool under = false; for (int o : u.sel) if (o != s && t.IsAncestor(o, s)) under = true; if (!under) t.Reparent(s, dropNow); }
                    t.SortChildrenByX(dropNow);
                } else {
                    std::set<int> parents;
                    for (int s : u.sel) if (const BtNode* nn = t.Find(s); nn && nn->parent >= 0) parents.insert(nn->parent);
                    for (int p : parents) t.SortChildrenByX(p);
                }
                Tidy(c);
                DocTouch();
            } else if (!(io.KeyShift || io.KeyCtrl) && u.sel.size() > 1 && IsSel(u, k.id)) Select(u, k.id);
            u.dragNode = -1; u.dragMoved = false;
        }
        if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            u.renaming = k.id; u.renameFocus = true;
            std::snprintf(u.renameBuf, sizeof(u.renameBuf), "%s", n.name.c_str());
        }
        if (hov && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && Len(mouse - u.rightDown) < 4.0f) {
            if (!IsSel(u, k.id)) Select(u, k.id);
            ctxNode = k.id;
        }
        ImGui::PopID();

        // ---- visuals ----
        const ImVec4 tint = KindTint(n.Kind());
        const bool isSel = IsSel(u, k.id);
        const bool isRoot = k.id == t.root;
        const auto* st = liveState(k.id);
        const bool running = st && st->running;
        const bool recent = st && !running && st->lastTick >= 0.0f && now - st->lastTick < 0.8f;
        const int sev = c.Severity(k.id);
        ImU32 fill = running ? PlayCol(0.92f) : ImGui::GetColorU32(hov || isSel ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
        dl->AddRectFilled(r.Min, r.Max, fill, 6.0f * Z);
        if (!running) dl->AddRectFilled(r.Min, ImVec2(r.Max.x, r.Min.y + std::max(3.0f, 4.0f * Z)), ImGui::GetColorU32(ImVec4(tint.x, tint.y, tint.z, 0.95f)), 6.0f * Z, ImDrawFlags_RoundCornersTop);
        ImU32 border = isSel ? ImGui::GetColorU32(AccentCol()) : isRoot ? ImGui::GetColorU32(ImVec4(tint.x, tint.y, tint.z, 1.0f)) : ImGui::GetColorU32(ImGuiCol_Border);
        if (recent) border = statusCol(st->status, 0.9f);
        dl->AddRect(r.Min, r.Max, border, 6.0f * Z, 0, isSel ? 2.5f : (isRoot || recent) ? 2.0f : 1.0f);
        if (dropNow == k.id) dl->AddRect(r.Min - ImVec2(4, 4), r.Max + ImVec2(4, 4), ImGui::GetColorU32(AccentCol(0.9f)), 8.0f * Z, 0, 2.5f);
        if (findMatch(n)) dl->AddRect(r.Min - ImVec2(4, 4), r.Max + ImVec2(4, 4), Col(0.30f, 0.85f, 1.0f, 0.9f), 8.0f * Z, 0, 2.0f);
        if (running) {
            const float pulse = 0.5f + 0.5f * std::sin((float)ImGui::GetTime() * 5.0f);
            dl->AddRect(r.Min - ImVec2(3, 3), r.Max + ImVec2(3, 3), PlayCol(0.30f + 0.45f * pulse), 8.0f * Z, 0, 3.0f);
        }
        const ImU32 txt = running ? Col(0.08f, 0.08f, 0.08f) : ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32 sub = running ? Col(0.18f, 0.18f, 0.18f) : ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const ImVec4 clip(r.Min.x, r.Min.y, r.Max.x - 4.0f, r.Max.y);
        // Order among the siblings (what runs first), top left.
        if (const BtNode* p = t.Find(n.parent)) {
            const std::vector<int> kids = t.FlowChildren(p->id);
            if (kids.size() > 1) {
                auto it = std::find(kids.begin(), kids.end(), k.id);
                const std::string num = std::to_string(it == kids.end() ? 0 : (int)(it - kids.begin()) + 1);
                dl->AddText(font, fs * 0.8f, ImVec2(r.Min.x + 5 * Z, r.Min.y + 6 * Z), sub, num.c_str());
            }
        }
        const std::string title = n.name.empty() ? std::string(n.Def().title) : n.name;
        if (u.renaming != k.id)
            dl->AddText(font, fs * 1.02f, ImVec2(r.Min.x + 16 * Z, r.Min.y + 7 * Z), txt, title.c_str(), nullptr, 0.0f, &clip);
        std::string line = n.name.empty() ? Summary(t, n) : std::string(n.Def().title) + ": " + Summary(t, n);
        dl->AddText(font, fs * 0.86f, ImVec2(r.Min.x + 10 * Z, r.Min.y + 28 * Z), sub, line.c_str(), nullptr, 0.0f, &clip);
        // Services: a row each, inside the card.
        {
            float sy = r.Min.y + kNodeH * Z;
            for (int sv : t.Services(k.id)) {
                const BtNode* s = t.Find(sv);
                if (!s) continue;
                const ImRect sr(ImVec2(r.Min.x + 6 * Z, sy), ImVec2(r.Max.x - 6 * Z, sy + (kServiceRowH - 2.0f) * Z));
                ImGui::SetCursorScreenPos(sr.Min);
                ImGui::PushID(sv);
                ImGui::InvisibleButton("##svc", sr.GetSize());
                const bool shov = ImGui::IsItemHovered();
                itemHovered |= shov;
                if (ImGui::IsItemActivated()) { Select(u, sv); u.forceSideTab = 0; u.dragNode = -1; }
                if (shov && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && Len(mouse - u.rightDown) < 4.0f) { Select(u, sv); ctxNode = sv; }
                ImGui::PopID();
                const auto* sst = liveState(sv);
                const bool srun = sst && sst->running;
                const ImVec4 stint = KindTint(BtNodeKind::Service);
                dl->AddRectFilled(sr.Min, sr.Max, srun ? ImGui::GetColorU32(ImVec4(stint.x, stint.y, stint.z, 0.55f)) : ImGui::GetColorU32(ImVec4(stint.x, stint.y, stint.z, IsSel(u, sv) ? 0.45f : shov ? 0.35f : 0.2f)), 3.0f * Z);
                if (IsSel(u, sv)) dl->AddRect(sr.Min, sr.Max, ImGui::GetColorU32(AccentCol()), 3.0f * Z, 0, 1.5f);
                const std::string sl = std::string(s->name.empty() ? s->Def().title : s->name.c_str()) + "  " + Summary(t, *s);
                dl->AddText(font, fs * 0.8f, ImVec2(sr.Min.x + 5 * Z, sr.Min.y + 1.5f * Z), txt, sl.c_str(), nullptr, 0.0f, &clip);
                if (c.Severity(sv) > 0) {
                    DrawWarnGlyph(dl, ImVec2(sr.Max.x - 8.0f * Z, sr.GetCenter().y), 5.0f * std::max(Z, 0.7f), c.Severity(sv) >= 2 ? ErrCol() : WarnCol());
                    if (shov) ImGui::SetTooltip("%s", c.Issues(sv).c_str());
                }
                sy += kServiceRowH * Z;
            }
        }
        // Badges, top right: the last result, problems.
        float bx = r.Max.x - 9.0f * Z;
        if (st && st->lastTick >= 0.0f && st->status != BtStatus::Invalid) {
            dl->AddCircleFilled(ImVec2(bx, r.Min.y + 11.0f * Z), 4.0f * Z, statusCol(st->status, 1.0f));
            dl->AddCircle(ImVec2(bx, r.Min.y + 11.0f * Z), 4.0f * Z, Col(0.1f, 0.1f, 0.1f, 0.6f));
            bx -= 12.0f * Z;
        }
        if (sev > 0) {
            const ImVec2 gc(bx, r.Min.y + 11.0f * Z);
            DrawWarnGlyph(dl, gc, 6.5f * std::max(Z, 0.7f), sev >= 2 ? ErrCol() : WarnCol());
            if (hov && Len(mouse - gc) < 10.0f) ImGui::SetTooltip("%s", c.Issues(k.id).c_str());
        }
        if (hov && u.renaming != k.id && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::GetCurrentContext()->HoveredIdTimer > 0.6f) {
            std::string tip = std::string(n.Def().title) + (n.name.empty() ? "" : " '" + n.name + "'") + "\n" + n.Def().doc;
            if (st && st->lastTick >= 0.0f) tip += std::string("\n\nLast: ") + BtStatusName(st->status) + (st->running ? " (running)" : "");
            if (sev > 0) tip += "\n\n" + c.Issues(k.id);
            tip += "\n\nDrag to move / reorder, drop on a node to reparent, double-click to rename, right-click for more";
            ImGui::SetTooltip("%s", tip.c_str());
        }
        // Inline rename.
        if (u.renaming == k.id) {
            ImGui::SetCursorScreenPos(ImVec2(r.Min.x + 6 * Z, r.Min.y + 4 * Z));
            ImGui::SetNextItemWidth(kNodeW * Z - 12 * Z);
            if (u.renameFocus) { ImGui::SetKeyboardFocusHere(); u.renameFocus = false; }
            ImGui::PushID(k.id);
            const bool enter = ImGui::InputText("##rename", u.renameBuf, sizeof(u.renameBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            const bool done = ImGui::IsItemDeactivated();
            ImGui::PopID();
            itemHovered |= ImGui::IsItemHovered();
            if (enter || done) {
                const std::string nn = u.renameBuf;
                if (!ImGui::IsKeyPressed(ImGuiKey_Escape) && nn != n.name) { DocPush(); n.name = nn; DocTouch(); }
                u.renaming = -1;
            }
        }
    }
    if (u.dragNode >= 0 && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) u.dragNode = -1;

    // ---- an empty tree ----
    if (t.nodes.empty()) {
        const char* msg = "Empty tree: pick a node from the palette, or right-click here.";
        const ImVec2 ts = font->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.0f, msg);
        dl->AddText(ImVec2(org.x + (csz.x - ts.x) * 0.5f, org.y + csz.y * 0.4f), ImGui::GetColorU32(ImGuiCol_TextDisabled), msg);
    }

    // ---- node context menu ----
    if (ctxNode >= 0) { s_ctxNode = ctxNode; ImGui::OpenPopup("##btnodectx"); }
    if (ImGui::BeginPopup("##btnodectx")) {
        const int id = s_ctxNode;
        BtNode* n = t.Find(id);
        if (!n) ImGui::CloseCurrentPopup();
        else {
            ImGui::TextDisabled("%s", n->Def().title);
            auto addMenu = [&](const char* label, BtNodeKind kind, bool asService) {
                if (ImGui::BeginMenu(label)) {
                    for (const BtNodeDef& d : BtNodeDefs())
                        if (d.kind == kind && ImGui::MenuItem(d.title)) {
                            DocPush();
                            const int nid = t.AddNode(d.type, id);
                            if (!asService) if (BtNode* nn = t.Find(nid)) { nn->x = n->x; nn->y = n->y + kNodeH + 64.0f; }
                            Tidy(c); DocTouch(); Select(u, nid); u.forceSideTab = 0;
                        }
                    ImGui::EndMenu();
                }
            };
            if (!BtIsService(n->type)) {
                addMenu("Add composite", BtNodeKind::Composite, false);
                addMenu("Add decorator", BtNodeKind::Decorator, false);
                addMenu("Add task", BtNodeKind::Task, false);
                addMenu("Attach service", BtNodeKind::Service, true);
                if (ImGui::BeginMenu("Wrap in decorator")) {
                    for (const BtNodeDef& d : BtNodeDefs())
                        if (d.kind == BtNodeKind::Decorator && ImGui::MenuItem(d.title)) {
                            DocPush();
                            const int w = t.InsertParent(id, d.type);
                            Tidy(c); DocTouch(); Select(u, w); u.forceSideTab = 0;
                        }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Set as root", nullptr, id == t.root, n->parent < 0)) { DocPush(); t.root = id; DocTouch(); Tidy(c); }
                if (n->parent >= 0 && ImGui::MenuItem("Detach from parent")) { DocPush(); t.Reparent(id, -1); n = t.Find(id); n->y += 120.0f; DocTouch(); }
                if (ImGui::MenuItem("Rename", "double-click")) { u.renaming = id; u.renameFocus = true; std::snprintf(u.renameBuf, sizeof(u.renameBuf), "%s", n->name.c_str()); }
                ImGui::Separator();
                if (ImGui::MenuItem("Copy subtree", "Ctrl+C")) u.clipboard = CopySubtree(t, id);
                if (ImGui::MenuItem("Duplicate subtree", "Ctrl+D")) { const std::string txt = CopySubtree(t, id); PasteSubtree(c, txt, n->parent, ImVec2(n->x + 40.0f, n->y + 40.0f)); }
                if (ImGui::MenuItem("Paste as child", "Ctrl+V", false, !u.clipboard.empty())) PasteSubtree(c, u.clipboard, id, ImVec2(n->x, n->y + kNodeH + 64.0f));
                ImGui::Separator();
                if (ImGui::MenuItem("Delete node only", nullptr, false, n->parent >= 0 || t.FlowChildren(id).size() <= 1)) {
                    // Children move up to the parent, in place.
                    DocPush();
                    BtNode* nn = t.Find(id);
                    const int parent = nn->parent;
                    std::vector<int> kids = nn->children;
                    int at = -1;
                    if (const BtNode* p = t.Find(parent)) { auto it = std::find(p->children.begin(), p->children.end(), id); at = it == p->children.end() ? -1 : (int)(it - p->children.begin()); }
                    for (int ch : kids) { t.Reparent(ch, parent, at); if (at >= 0) ++at; }
                    if (parent < 0 && !kids.empty()) t.root = kids.front();
                    t.RemoveSubtree(id);
                    ClearSel(u); Tidy(c); DocTouch();
                }
                const std::string what = u.sel.size() > 1 && IsSel(u, id) ? std::to_string(u.sel.size()) + " nodes" : "subtree";
                if (ImGui::MenuItem(("Delete " + what).c_str(), "Del")) DeleteNodes(c, IsSel(u, id) ? u.sel : std::vector<int>{id});
            } else {
                if (ImGui::MenuItem("Remove service", "Del")) DeleteNodes(c, {id});
            }
        }
        ImGui::EndPopup();
    }

    // ---- minimap ----
    const bool spaceHeld = keys && ImGui::IsKeyDown(ImGuiKey_Space);
    if (u.minimap && csz.x > 360.0f && csz.y > 220.0f && !cards.empty()) {
        const ImVec2 msz(170.0f, 110.0f);
        const ImVec2 m0(org.x + csz.x - msz.x - 10.0f, org.y + csz.y - msz.y - 10.0f);
        const ImRect mr(m0, m0 + msz);
        float x0, y0, x1, y1;
        bounds(x0, y0, x1, y1);
        const ImVec2 v0 = toGraph(org), v1 = toGraph(org + csz);
        x0 = std::min(x0, v0.x); y0 = std::min(y0, v0.y); x1 = std::max(x1, v1.x); y1 = std::max(y1, v1.y);
        const float s = std::min((msz.x - 10.0f) / std::max(x1 - x0, 1.0f), (msz.y - 10.0f) / std::max(y1 - y0, 1.0f));
        const ImVec2 off(m0.x + (msz.x - (x1 - x0) * s) * 0.5f, m0.y + (msz.y - (y1 - y0) * s) * 0.5f);
        auto toMap = [&](ImVec2 g) { return ImVec2(off.x + (g.x - x0) * s, off.y + (g.y - y0) * s); };
        ImGui::SetCursorScreenPos(m0);
        ImGui::InvisibleButton("##btminimap", msz);
        const bool mhov = ImGui::IsItemHovered();
        itemHovered |= mhov;
        if (ImGui::IsItemActive()) centreOn(ImVec2((mouse.x - off.x) / s + x0, (mouse.y - off.y) / s + y0));
        dl->AddRectFilled(mr.Min, mr.Max, ImGui::GetColorU32(ImGuiCol_PopupBg, mhov ? 0.95f : 0.8f), 4.0f);
        dl->AddRect(mr.Min, mr.Max, ImGui::GetColorU32(ImGuiCol_Border), 4.0f);
        for (const Card& k : cards) {
            const BtNode* n = t.Find(k.id);
            const auto* st = liveState(k.id);
            const ImVec2 a = toMap(ImVec2(n->x, n->y)), b = toMap(ImVec2(n->x + kNodeW, n->y + kNodeH));
            dl->AddRectFilled(a, b, (st && st->running) ? PlayCol() : IsSel(u, k.id) ? ImGui::GetColorU32(AccentCol()) : ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.7f), 1.0f);
        }
        dl->AddRect(toMap(v0), toMap(v1), ImGui::GetColorU32(ImGuiCol_Text, 0.8f), 1.0f);
        if (mhov) ImGui::SetTooltip("Minimap: click or drag to move the view");
    }

    // ---- canvas-level input: box select, pan, context menu, palette drops ----
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) u.rightDown = mouse;
    const bool empty = canvasHovered && !itemHovered && u.hov < 0;
    if (empty && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !spaceHeld) { u.boxing = true; u.boxStart = mouse; }
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
                for (const Card& k : cards) if (box.Overlaps(k.r)) Select(u, k.id, 1);
                if (!u.sel.empty()) u.forceSideTab = 0;
            } else if (!add) ClearSel(u);
            u.boxing = false;
        }
    }
    const bool panDrag = canvasHovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 1.0f) ||
                                           ImGui::IsMouseDragging(ImGuiMouseButton_Right, 4.0f) ||
                                           (spaceHeld && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f)));
    if (panDrag || (u.panning && (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Left)))) {
        u.pan = u.pan + io.MouseDelta;
        u.panning = true;
        u.boxing = false;
    } else u.panning = false;
    if (empty && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && Len(mouse - u.rightDown) < 4.0f) ImGui::OpenPopup("##btcanvasctx");
    if (ImGui::BeginPopup("##btcanvasctx")) {
        const ImVec2 at = toGraph(ImGui::GetMousePosOnOpeningCurrentPopup());
        auto addFree = [&](const char* label, BtNodeKind kind) {
            if (ImGui::BeginMenu(label)) {
                for (const BtNodeDef& d : BtNodeDefs())
                    if (d.kind == kind && ImGui::MenuItem(d.title)) {
                        DocPush();
                        const int nid = t.AddNode(d.type, -1, -1, at.x, at.y);
                        if (t.root == nid) Tidy(c);
                        DocTouch(); Select(u, nid); u.forceSideTab = 0;
                    }
                ImGui::EndMenu();
            }
        };
        addFree(t.root < 0 ? "Add root composite" : "Add free composite", BtNodeKind::Composite);
        addFree(t.root < 0 ? "Add root decorator" : "Add free decorator", BtNodeKind::Decorator);
        addFree(t.root < 0 ? "Add root task" : "Add free task", BtNodeKind::Task);
        if (ImGui::MenuItem("Paste", "Ctrl+V", false, !u.clipboard.empty())) PasteSubtree(c, u.clipboard, -1, at);
        ImGui::Separator();
        if (ImGui::MenuItem("Select all", "Ctrl+A")) { ClearSel(u); for (const Card& k : cards) Select(u, k.id, 1); }
        if (ImGui::MenuItem("Frame all", "F")) u.frameReq = true;
        if (ImGui::MenuItem("Reset zoom", "Ctrl+0")) { const ImVec2 g = toGraph(org + csz * 0.5f); u.zoom = 1.0f; centreOn(g); }
        if (ImGui::MenuItem("Auto layout")) { DocPush(); Layout(c); DocTouch(); u.frameReq = true; }
        ImGui::EndPopup();
    }
    // A node type dragged from the palette: a child of the card it lands on, else a free node.
    if (ImGui::BeginDragDropTargetCustom(canvasRect, ImGui::GetID("##btdrop"))) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("BT_PALETTE")) {
            const int ti = *(const int*)pl->Data;
            if (ti >= 0 && ti < (int)BtNodeDefs().size()) {
                const BtNodeDef& d = BtNodeDefs()[(std::size_t)ti];
                if (u.hov >= 0) AddFromPalette(c, d.type, u.hov);
                else if (d.kind != BtNodeKind::Service) {
                    DocPush();
                    const ImVec2 at = toGraph(mouse) - ImVec2(kNodeW * 0.5f, kNodeH * 0.5f);
                    const int nid = t.AddNode(d.type, -1, -1, at.x, at.y);
                    if (t.root == nid) Tidy(c);
                    DocTouch(); Select(u, nid); u.forceSideTab = 0;
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    // ---- keyboard ----
    if (keys) {
        const bool ctrl = io.KeyCtrl;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { if (u.renaming >= 0) u.renaming = -1; else ClearSel(u); }
        if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) u.frameReq = true;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_0, false)) { const ImVec2 g = toGraph(org + csz * 0.5f); u.zoom = 1.0f; centreOn(g); }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) { ClearSel(u); for (const Card& k : cards) Select(u, k.id, 1); }
        if (ctrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) { if (DocUndo()) ClampSel(u, t); }
        if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_Y, false) || (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)))) { if (DocRedo()) ClampSel(u, t); }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && u.primary >= 0) u.clipboard = CopySubtree(t, u.primary);
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false) && !u.clipboard.empty()) {
            const BtNode* p = t.Find(u.primary);
            PasteSubtree(c, u.clipboard, p ? p->id : -1, p ? ImVec2(p->x, p->y + kNodeH + 64.0f) : toGraph(org + csz * 0.5f));
        }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false) && u.primary >= 0) {
            const BtNode* n = t.Find(u.primary);
            if (n) { const std::string txt = CopySubtree(t, u.primary); PasteSubtree(c, txt, n->parent, ImVec2(n->x + 40.0f, n->y + 40.0f)); }
        }
        if ((ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) && u.renaming < 0 && !u.sel.empty())
            DeleteNodes(c, u.sel);
        const float step = io.KeyShift ? 25.0f : 1.0f;
        ImVec2 nudge(0, 0);
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) nudge.x -= step;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) nudge.x += step;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) nudge.y -= step;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) nudge.y += step;
        if ((nudge.x != 0.0f || nudge.y != 0.0f) && !u.sel.empty()) {
            DocPush();
            for (int s : u.sel) if (BtNode* nn = t.Find(s)) { nn->x += nudge.x; nn->y += nudge.y; }
            DocTouch();
        }
    }

    // ---- status line ----
    {
        std::string s;
        if (c.playing && c.live) {
            s = "Live: " + c.live->gameObject->name + (c.live->IsRunning() ? "" : " (stopped)") + Fmt("  \xC2\xB7  %.1f s", (double)c.live->Clock());
            const std::vector<int> path = c.live->ActivePath();
            if (!path.empty()) {
                s += "  \xC2\xB7  ";
                for (std::size_t i = 0; i < path.size(); ++i) {
                    const BtNode* n = t.Find(path[i]);
                    if (i) s += " > ";
                    s += n ? (n->name.empty() ? n->Def().title : n->name) : "?";
                }
            }
            if (c.live->Completions() > 0) s += std::string("  \xC2\xB7  last run ") + BtStatusName(c.live->LastResult());
        } else if (u.sel.size() > 1) s = std::to_string(u.sel.size()) + " nodes selected  \xC2\xB7  drag to move, Delete, Ctrl+C / Ctrl+D";
        else s = "drag: move / reorder  \xC2\xB7  drop on a node: reparent  \xC2\xB7  drag empty: box select  \xC2\xB7  wheel: zoom  \xC2\xB7  middle / right drag: pan  \xC2\xB7  double-click: rename";
        const float ofs = ImGui::GetFontSize() * 0.92f;
        const ImVec2 ts = font->CalcTextSizeA(ofs, FLT_MAX, 0.0f, s.c_str());
        const ImVec2 at(org.x + 8.0f, org.y + csz.y - ts.y - 6.0f);
        dl->AddRectFilled(at - ImVec2(4, 2), at + ts + ImVec2(4, 2), ImGui::GetColorU32(ImGuiCol_WindowBg, 0.75f), 3.0f);
        dl->AddText(font, ofs, at, c.playing && c.live ? PlayCol() : ImGui::GetColorU32(ImGuiCol_TextDisabled), s.c_str());
        const std::string zs = std::to_string((int)std::lround(u.zoom * 100.0f)) + "%";
        const ImVec2 zts = font->CalcTextSizeA(ofs, FLT_MAX, 0.0f, zs.c_str());
        dl->AddText(font, ofs, ImVec2(org.x + csz.x - zts.x - 10.0f, org.y + 6.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), zs.c_str());
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// The palette
// ---------------------------------------------------------------------------
void DrawPalette(Ctx& c, Ui& u, ImVec2 size) {
    ImGui::BeginChild("##btpalette", size, ImGuiChildFlags_Borders);
    ImGui::TextDisabled("Nodes");
    Tip("Click to add under the selected node (beside a task), drag onto the canvas, or right-click a node.");
    static char filter[32] = "";
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##btpalfilter", "Filter", filter, sizeof(filter));
    const std::string f = Lower(filter);
    for (BtNodeKind kind : {BtNodeKind::Composite, BtNodeKind::Decorator, BtNodeKind::Task, BtNodeKind::Service}) {
        const ImVec4 tint = KindTint(kind);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(tint.x, tint.y, tint.z, 1.0f));
        const bool open = ImGui::CollapsingHeader(KindName(kind), ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::PopStyleColor();
        if (!open) continue;
        for (std::size_t i = 0; i < BtNodeDefs().size(); ++i) {
            const BtNodeDef& d = BtNodeDefs()[i];
            if (d.kind != kind) continue;
            if (!f.empty() && Lower(d.title).find(f) == std::string::npos) continue;
            ImGui::PushID((int)i);
            if (ImGui::Selectable(d.title, false, ImGuiSelectableFlags_AllowDoubleClick)) AddFromPalette(c, d.type, u.primary);
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                const int ti = (int)i;
                ImGui::SetDragDropPayload("BT_PALETTE", &ti, sizeof(ti));
                ImGui::TextUnformatted(d.title);
                ImGui::TextDisabled("Drop on a node or the canvas");
                ImGui::EndDragDropSource();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", d.doc);
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Inspector widgets
// ---------------------------------------------------------------------------
// A parameter field by the catalogue's kind; returns true when it changed.
bool ParamField(Ctx& c, BtNode& n, const BtParamDef& p) {
    BehaviorTree& t = c.t;
    const std::string label = std::string("##p_") + p.key;
    std::string v = n.Str(p.key);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(p.label);
    if (p.hint) Tip(p.hint);
    ImGui::TableNextColumn();
    bool changed = false;
    auto set = [&](const std::string& nv) {
        EditPush();
        if (p.def && nv == p.def) n.params.erase(p.key); else n.params[p.key] = nv;
        DocTouch();
        changed = true;
    };
    auto keyCombo = [&](const char* typesFilter) {
        std::vector<std::string> names;
        for (const auto& k : t.keys) {
            if (typesFilter && std::string(typesFilter).find(BtValue::TypeName(k.value.type)) == std::string::npos) continue;
            names.push_back(k.name);
        }
        const std::string shown = v.empty() ? "(none)" : v;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo(label.c_str(), shown.c_str())) {
            if (ImGui::Selectable("(none)", v.empty())) set("");
            for (const auto& nm : names) if (ImGui::Selectable(nm.c_str(), nm == v)) set(nm);
            if (!v.empty() && !t.Key(v)) ImGui::TextDisabled("'%s' is not a key", v.c_str());
            ImGui::EndCombo();
        }
    };
    switch (p.kind) {
        case BtParamDef::Number: {
            float f = n.Num(p.key);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragFloat(label.c_str(), &f, 0.05f, -1e6f, 1e6f, "%.2f")) set(vs::VsValue::FormatNumber(f));
            break;
        }
        case BtParamDef::Int: {
            int i = n.Int(p.key);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragInt(label.c_str(), &i, 0.1f, -1000000, 1000000)) set(std::to_string(i));
            break;
        }
        case BtParamDef::Bool: {
            bool b = n.Flag(p.key);
            if (ImGui::Checkbox(label.c_str(), &b)) set(b ? "1" : "0");
            break;
        }
        case BtParamDef::Vector: {
            Vec3 x = n.Vec(p.key);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragFloat3(label.c_str(), &x.x, 0.05f)) set(vs::VsValue::FormatNumber(x.x) + " " + vs::VsValue::FormatNumber(x.y) + " " + vs::VsValue::FormatNumber(x.z));
            break;
        }
        case BtParamDef::Key:
            keyCombo(p.choices);
            break;
        case BtParamDef::Choice: {
            std::vector<std::string> opts;
            std::stringstream ss(p.choices ? p.choices : "");
            std::string o;
            while (std::getline(ss, o, '|')) opts.push_back(o);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo(label.c_str(), v.c_str())) {
                for (const auto& x : opts) if (ImGui::Selectable(x.c_str(), x == v)) set(x);
                ImGui::EndCombo();
            }
            break;
        }
        case BtParamDef::Target:
        case BtParamDef::Object:
        case BtParamDef::Waypoints:
        case BtParamDef::File:
        case BtParamDef::Text:
        case BtParamDef::Event:
        case BtParamDef::Function: {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "%s", v.c_str());
            const bool pick = p.kind == BtParamDef::Target || p.kind == BtParamDef::Object || p.kind == BtParamDef::Waypoints || p.kind == BtParamDef::File;
            ImGui::SetNextItemWidth(pick ? -30.0f : -1.0f);
            if (ImGui::InputText(label.c_str(), buf, sizeof(buf))) set(buf);
            if (pick) {
                ImGui::SameLine(0, 4);
                const std::string pid = "...##pick_" + std::string(p.key);
                if (ImGui::SmallButton(pid.c_str())) {
                    if (p.kind == BtParamDef::File) {
                        const char* filt[] = {"*.wav", "*.ogg", "*.mp3"};
                        const std::string dir = ProjectAssetsDir(c.ed);
                        if (const char* f = tinyfd_openFileDialog("Pick a sound", dir.empty() ? nullptr : (dir + "/").c_str(), 3, filt, "sounds", 0))
                            set(ProjectRelative(c.ed, f));
                    } else ImGui::OpenPopup(("##pickpop_" + std::string(p.key)).c_str());
                }
                if (ImGui::BeginPopup(("##pickpop_" + std::string(p.key)).c_str())) {
                    if (p.kind == BtParamDef::Target) {
                        ImGui::TextDisabled("Blackboard keys");
                        for (const auto& k : t.keys)
                            if (k.value.type == BtValue::Type::Object || k.value.type == BtValue::Type::Vector)
                                if (ImGui::Selectable((k.name + "  (" + BtValue::TypeName(k.value.type) + ")").c_str())) { set(k.name); ImGui::CloseCurrentPopup(); }
                        ImGui::Separator();
                    }
                    ImGui::TextDisabled(p.kind == BtParamDef::Waypoints ? "Add a scene object" : "Scene objects");
                    static char objFilter[32] = "";
                    ImGui::SetNextItemWidth(180);
                    ImGui::InputTextWithHint("##objf", "Filter", objFilter, sizeof(objFilter));
                    const std::string of = Lower(objFilter);
                    int shown = 0;
                    for (const auto& up : c.ed.scene().Objects()) {
                        if (!up || up->transient) continue;
                        if (!of.empty() && Lower(up->name).find(of) == std::string::npos) continue;
                        if (++shown > 60) { ImGui::TextDisabled("..."); break; }
                        if (ImGui::Selectable(up->name.c_str())) {
                            if (p.kind == BtParamDef::Waypoints) { std::vector<std::string> l = SplitList(v); l.push_back(up->name); set(Join(l)); }
                            else { set(up->name); ImGui::CloseCurrentPopup(); }
                        }
                    }
                    ImGui::EndPopup();
                }
            }
            break;
        }
    }
    if (p.hint) Tip(p.hint);
    return changed;
}
// The two-column layout ParamField rows go in.
bool BeginParamTable(const char* id) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp)) return false;
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 0.8f);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 1.2f);
    return true;
}

void DrawNodeInspector(Ctx& c, Ui& u, int id) {
    BehaviorTree& t = c.t;
    BtNode* n = t.Find(id);
    if (!n) return;
    const BtNodeDef& d = n->Def();
    const ImVec4 tint = KindTint(d.kind);
    ImGui::TextColored(tint, "%s", d.title);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", d.kind == BtNodeKind::Composite ? "composite" : d.kind == BtNodeKind::Decorator ? "decorator" : d.kind == BtNodeKind::Task ? "task" : "service");
    ImGui::TextWrapped("%s", d.doc);
    ImGui::Spacing();
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s", n->name.c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputTextWithHint("##btname", "Label (optional)", buf, sizeof(buf))) { EditPush(); n->name = buf; DocTouch(); }
        Tip("A label shown on the card instead of the node type.");
    }
    if (const std::string iss = c.Issues(id); !iss.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, c.Severity(id) >= 2 ? ImVec4(0.95f, 0.38f, 0.35f, 1) : ImVec4(0.95f, 0.75f, 0.25f, 1));
        ImGui::TextWrapped("%s", iss.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    if (!d.params.empty()) ImGui::SeparatorText("Parameters");
    if (!d.params.empty() && BeginParamTable("##btparams")) {
        const std::string kind = n->Str("kind");
        for (const BtParamDef& p : d.params) {
            // Condition / Guard: only the fields the chosen test uses.
            if (n->type == BtNodeType::Condition || n->type == BtNodeType::Guard) {
                const std::string k = p.key;
                if (kind == "blackboard" && (k == "target" || k == "distance" || k == "fraction")) continue;
                if (kind == "distance" && (k == "key" || k == "value" || k == "fraction")) continue;
                if (kind == "distance" && k == "op") {
                    // closer / farther than
                    const bool farther = n->Str("op") == ">" || n->Str("op") == ">=";
                    int sel = farther ? 1 : 0;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Compare");
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::Combo("##dist", &sel, "closer than\0farther than\0")) { EditPush(); n->Set("op", sel ? ">" : "<"); DocTouch(); }
                    continue;
                }
                if (kind == "lineOfSight" && (k == "key" || k == "value" || k == "op" || k == "fraction")) continue;
                if (kind == "health" && (k == "key" || k == "value" || k == "op" || k == "target" || k == "distance")) continue;
            }
            if (n->type == BtNodeType::MoveTo && std::string(p.key) == "position" && !n->Str("target").empty()) continue;
            ParamField(c, *n, p);
        }
        ImGui::EndTable();
    }
    // A child of a random selector: its weight.
    if (const BtNode* p = t.Find(n->parent); p && p->type == BtNodeType::RandomSelector && !BtIsService(n->type)) {
        ImGui::SeparatorText("Random Selector");
        float w = n->Has("weight") ? n->Num("weight") : 1.0f;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat("Weight##btw", &w, 0.05f, 0.0f, 1000.0f, "%.2f")) { EditPush(); n->Set("weight", std::max(0.0f, w)); DocTouch(); }
        Tip("How likely this child is tried first (0 = last).");
    }
    // Live state.
    if (c.live) {
        if (const auto* st = c.live->State(id)) {
            ImGui::SeparatorText("Live");
            ImGui::Text("%s%s", BtStatusName(st->status), st->running ? " (running)" : "");
            if (st->lastTick >= 0.0f) ImGui::TextDisabled("ticked %u times, last %.1f s ago", st->ticks, (double)(c.live->Clock() - st->lastTick));
        }
    }
    ImGui::Spacing();
    ImGui::SeparatorText("Structure");
    if (n->parent >= 0) {
        const BtNode* p = t.Find(n->parent);
        ImGui::TextDisabled("Under: %s", p ? (p->name.empty() ? p->Def().title : p->name.c_str()) : "?");
        ImGui::SameLine();
        if (ImGui::SmallButton("Select##btparent")) { Select(u, n->parent); u.focusX = 1.0f; }
    } else ImGui::TextDisabled(id == t.root ? "The root" : "Not connected");
    if (!BtIsService(n->type)) {
        const std::vector<int> kids = t.FlowChildren(id);
        if (!kids.empty()) {
            ImGui::TextDisabled("Children (run order):");
            for (std::size_t i = 0; i < kids.size(); ++i) {
                const BtNode* k = t.Find(kids[i]);
                ImGui::PushID((int)i);
                if (ImGui::SmallButton("^") && i > 0) { DocPush(); t.MoveChild(kids[i], (int)i - 1); Tidy(c); DocTouch(); }
                ImGui::SameLine();
                if (ImGui::SmallButton("v") && i + 1 < kids.size()) { DocPush(); t.MoveChild(kids[i], (int)i + 1); Tidy(c); DocTouch(); }
                ImGui::SameLine();
                if (ImGui::Selectable((std::to_string(i + 1) + ". " + (k->name.empty() ? k->Def().title : k->name)).c_str(), false)) { Select(u, kids[i]); u.focusX = 1.0f; }
                ImGui::PopID();
            }
        }
        const std::vector<int> svcs = t.Services(id);
        if (!svcs.empty()) {
            ImGui::TextDisabled("Services:");
            for (int sv : svcs) {
                const BtNode* k = t.Find(sv);
                ImGui::PushID(sv);
                if (ImGui::Selectable((std::string("  ") + (k->name.empty() ? k->Def().title : k->name)).c_str(), false)) Select(u, sv);
                ImGui::PopID();
            }
        }
    }
}

void DrawBlackboardTab(Ctx& c, Ui& u) {
    BehaviorTree& t = c.t;
    ImGui::TextWrapped("Keys the tree reads and writes. Each NPC's runner starts from these defaults (its own overrides on top).");
    ImGui::Spacing();
    const bool live = c.live != nullptr;
    if (ImGui::BeginTable("##btkeys", live ? 5 : 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Default", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        if (live) ImGui::TableSetupColumn("Live", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("##x", ImGuiTableColumnFlags_WidthFixed, 22.0f);
        ImGui::TableHeadersRow();
        std::string remove;
        for (std::size_t i = 0; i < t.keys.size(); ++i) {
            Blackboard::Entry& k = t.keys[i];
            ImGui::PushID((int)i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%s", k.name.c_str());
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputText("##kn", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue) || (ImGui::IsItemDeactivatedAfterEdit() && std::string(buf) != k.name)) {
                    if (buf[0] && std::string(buf) != k.name && !t.Key(buf)) { DocPush(); t.RenameKey(k.name, buf); DocTouch(); }
                }
                Tip("Rename: every node that names this key follows.");
            }
            ImGui::TableNextColumn();
            {
                int ty = (int)k.value.type;
                ImGui::SetNextItemWidth(-1);
                if (ImGui::Combo("##kt", &ty, "number\0bool\0string\0vector\0object\0")) {
                    DocPush();
                    k.value = BtValue::FromText((BtValue::Type)ty, k.value.ToText());
                    DocTouch();
                }
            }
            ImGui::TableNextColumn();
            {
                ImGui::SetNextItemWidth(-1);
                switch (k.value.type) {
                    case BtValue::Type::Number: Tweak(k.value.number, [&](float& x) { return ImGui::DragFloat("##kv", &x, 0.05f); }); break;
                    case BtValue::Type::Bool: Tweak(k.value.boolean, [&](bool& x) { return ImGui::Checkbox("##kv", &x); }); break;
                    case BtValue::Type::Vector: Tweak(k.value.vector, [&](Vec3& x) { return ImGui::DragFloat3("##kv", &x.x, 0.05f); }); break;
                    default: {
                        char buf[128];
                        std::snprintf(buf, sizeof(buf), "%s", k.value.text.c_str());
                        Tweak(k.value.text, [&](std::string& x) { const bool ch = ImGui::InputTextWithHint("##kv", k.value.type == BtValue::Type::Object ? "object name" : "", buf, sizeof(buf)); if (ch) x = buf; return ch; });
                        break;
                    }
                }
            }
            if (live) {
                ImGui::TableNextColumn();
                const BtValue* lv = c.live->blackboard.Find(k.name);
                if (lv) {
                    BtValue tmp = *lv;
                    ImGui::SetNextItemWidth(-1);
                    switch (lv->type) {
                        case BtValue::Type::Number: if (ImGui::DragFloat("##lv", &tmp.number, 0.05f)) c.live->blackboard.Set(k.name, tmp); break;
                        case BtValue::Type::Bool: if (ImGui::Checkbox("##lv", &tmp.boolean)) c.live->blackboard.Set(k.name, tmp); break;
                        default: ImGui::TextUnformatted(lv->AsString().c_str()); break;
                    }
                } else ImGui::TextDisabled("-");
            }
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("x")) remove = k.name;
            Tip("Remove the key");
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (!remove.empty()) { DocPush(); t.RemoveKey(remove); DocTouch(); }
    }
    if (ImGui::Button("+ Key")) {
        DocPush();
        std::string nm = "key";
        for (int i = 1; t.Key(nm); ++i) nm = "key" + std::to_string(i);
        t.DeclareKey(nm, BtValue::Number(0.0f));
        DocTouch();
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Perception keys")) {
        DocPush();
        if (!t.Key("target")) t.DeclareKey("target", BtValue::Object(""));
        if (!t.Key("canSee")) t.DeclareKey("canSee", BtValue::Bool(false));
        if (!t.Key("lastSeen")) t.DeclareKey("lastSeen", BtValue::Vector({0, 0, 0}));
        if (!t.Key("sinceSeen")) t.DeclareKey("sinceSeen", BtValue::Number(9999.0f));
        DocTouch();
    }
    Tip("The keys the Perception service writes by default.");
    // Keys the nodes name but the tree lacks.
    std::set<std::string> missing;
    for (const BtNode& n : t.nodes)
        for (const BtParamDef& p : n.Def().params)
            if (p.kind == BtParamDef::Key) { const std::string v = n.Str(p.key); if (!v.empty() && !t.Key(v)) missing.insert(v); }
    if (!missing.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1), "Named by nodes but not declared:");
        for (const std::string& m : missing) {
            ImGui::PushID(m.c_str());
            ImGui::BulletText("%s", m.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Declare")) { DocPush(); t.DeclareKey(m, BtValue::Number(0.0f)); DocTouch(); }
            ImGui::PopID();
        }
    }
    (void)u;
}

void DrawIssuesTab(Ctx& c, Ui& u) {
    if (c.issues.empty()) { ImGui::TextDisabled("No problems."); return; }
    for (std::size_t i = 0; i < c.issues.size(); ++i) {
        const auto& is = c.issues[i];
        ImGui::PushID((int)i);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        DrawWarnGlyph(dl, ImVec2(p.x + 8.0f, p.y + ImGui::GetTextLineHeight() * 0.5f), 6.0f, is.severity >= 2 ? ErrCol() : WarnCol());
        ImGui::Dummy(ImVec2(18.0f, 0.0f));
        ImGui::SameLine();
        if (ImGui::Selectable(is.text.c_str(), false, 0, ImVec2(0, 0)) && is.node >= 0) { Select(u, is.node); u.focusX = 1.0f; u.forceSideTab = 0; }
        ImGui::PopID();
    }
}

void DrawRunnerTab(Ctx& c, Ui& u) {
    EditorState& ed = c.ed;
    std::vector<BehaviorTreeRunner*> runners = RunnersFor(ed);
    if (runners.empty()) {
        ImGui::TextWrapped("No object in the scene runs this tree. Select an NPC and press 'Use on selected object' in the toolbar, or add a Behavior Tree Runner (Add Component > Gameplay).");
        return;
    }
    const std::string shown = c.live ? c.live->gameObject->name : u.liveObj;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##btrunners", shown.empty() ? "(pick a runner)" : shown.c_str())) {
        for (BehaviorTreeRunner* r : runners)
            if (ImGui::Selectable(r->gameObject->name.c_str(), r == c.live)) { u.liveObj = r->gameObject->name; ed.Select(r->gameObject); }
        ImGui::EndCombo();
    }
    Tip("Which NPC's runner the canvas and the blackboard follow in Play.");
    BehaviorTreeRunner* r = c.live;
    if (!r) { ImGui::TextDisabled("Press Play to watch it run."); return; }
    ImGui::Text("%s", r->IsRunning() ? "Running" : "Stopped");
    ImGui::SameLine();
    ImGui::TextDisabled("%.1f s, %d run%s finished%s%s", (double)r->Clock(), r->Completions(), r->Completions() == 1 ? "" : "s",
                        r->Completions() ? ", last " : "", r->Completions() ? BtStatusName(r->LastResult()) : "");
    if (!r->LastError().empty()) ImGui::TextColored(ImVec4(0.95f, 0.38f, 0.35f, 1), "%s", r->LastError().c_str());
    if (ImGui::SmallButton(r->IsRunning() ? "Stop##btlive" : "Run##btlive")) { if (r->IsRunning()) r->Stop(); else r->Run(); }
    ImGui::SameLine();
    if (ImGui::SmallButton("Restart##btlive")) r->Run();
    if (D().dirty) ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1), "The runner plays the saved file; save to see your edits.");
    ImGui::SeparatorText("Active path");
    const std::vector<int> path = r->ActivePath();
    if (path.empty()) ImGui::TextDisabled("(between runs)");
    for (std::size_t i = 0; i < path.size(); ++i) {
        const BtNode* n = c.t.Find(path[i]);
        ImGui::PushID((int)i);
        std::string lb = std::string((int)i * 2, ' ') + (n ? (n->name.empty() ? n->Def().title : n->name) : "?");
        if (ImGui::Selectable(lb.c_str(), u.primary == path[i])) { Select(u, path[i]); u.focusX = 1.0f; }
        ImGui::PopID();
    }
    ImGui::SeparatorText("Blackboard");
    if (ImGui::BeginTable("##btlivebb", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        for (const auto& e : r->blackboard.Entries()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(e.name.c_str());
            ImGui::TableNextColumn(); ImGui::TextDisabled("%s", e.value.AsString().c_str());
        }
        ImGui::EndTable();
    }
}

// ---------------------------------------------------------------------------
// Toolbar
// ---------------------------------------------------------------------------
void NewGuardSample(Ctx& c) {
    const std::string dir = ProjectAssetsDir(c.ed);
    DocNew(BehaviorTree::GuardSample(), dir.empty() ? std::string() : (std::filesystem::path(dir) / "Guard.okaybt").string());
    ClearSel(U());
    U().frameReq = true;
}

void UseOnSelected(Ctx& c) {
    EditorState& ed = c.ed;
    GameObject* go = ed.selected();
    if (!go) { ConsoleLog("Behavior Tree: select an object first", 1); return; }
    if (D().path.empty()) { ConsoleLog("Behavior Tree: save the tree first (it needs a file the runner can load)", 1); return; }
    ed.PushUndo();
    BehaviorTreeRunner* r = go->GetComponent<BehaviorTreeRunner>();
    if (!r) r = go->AddComponent<BehaviorTreeRunner>();
    r->treePath = ProjectRelative(ed, D().path);
    ed.dirty = true;
    U().liveObj = go->name;
    ConsoleLog("Behavior Tree: '" + go->name + "' runs " + r->treePath);
}

void DrawToolbar(Ctx& c, Ui& u) {
    EditorState& ed = c.ed;
    Doc& d = D();
    if (ImGui::SmallButton("New##bt")) ImGui::OpenPopup("##btnew");
    if (ImGui::BeginPopup("##btnew")) {
        if (ImGui::MenuItem("Empty tree")) { BehaviorTree t; t.name = "Tree"; DocNew(t, std::string()); ClearSel(u); u.frameReq = true; }
        if (ImGui::MenuItem("Guard sample (patrol, see, chase, attack)")) NewGuardSample(c);
        ImGui::EndPopup();
    }
    Tip("Start a new tree: empty, or the Guard sample");
    ImGui::SameLine();
    if (ImGui::SmallButton("Open##bt")) {
        const char* filt[] = {"*.okaybt"};
        const std::string dir = ProjectAssetsDir(ed);
        if (const char* f = tinyfd_openFileDialog("Open behavior tree", dir.empty() ? nullptr : (dir + "/").c_str(), 1, filt, "behavior trees", 0)) {
            std::string err;
            if (DocOpen(f, &err)) { ClearSel(u); u.frameReq = true; ConsoleLog(std::string("Opened ") + f); }
            else ConsoleLog("Behavior Tree: " + err, 2);
        }
    }
    ImGui::SameLine();
    auto saveAs = [&]() {
        const char* filt[] = {"*.okaybt"};
        const std::string dir = ProjectAssetsDir(ed);
        const std::string def = d.path.empty() ? (dir.empty() ? d.tree.name + ".okaybt" : (std::filesystem::path(dir) / (d.tree.name.empty() ? "Tree" : d.tree.name)).string() + ".okaybt") : d.path;
        if (const char* f = tinyfd_saveFileDialog("Save behavior tree", def.c_str(), 1, filt, "behavior trees")) {
            std::string path = f;
            if (std::filesystem::path(path).extension() != ".okaybt") path += ".okaybt";
            std::string err;
            if (DocSave(path, &err)) ConsoleLog("Saved " + path); else ConsoleLog("Behavior Tree: " + err, 2);
        }
    };
    if (ImGui::SmallButton(d.dirty ? "Save*##bt" : "Save##bt")) {
        if (d.path.empty()) saveAs();
        else { std::string err; if (DocSave(d.path, &err)) ConsoleLog("Saved " + d.path); else ConsoleLog("Behavior Tree: " + err, 2); }
    }
    Tip("Write the .okaybt file (Ctrl+S). Runners in Play read the saved file.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Save As##bt")) saveAs();
    ImGui::SameLine();
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s", d.tree.name.c_str());
        ImGui::SetNextItemWidth(140);
        if (ImGui::InputTextWithHint("##btname", "Tree name", buf, sizeof(buf))) { EditPush(); d.tree.name = buf; DocTouch(); }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", d.path.empty() ? "(not saved)" : std::filesystem::path(d.path).filename().string().c_str());
    if (!d.path.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", d.path.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Use on selected object##bt")) UseOnSelected(c);
    Tip("Give the selected object a Behavior Tree Runner that plays this file.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Auto Layout##bt")) { DocPush(); Layout(c); DocTouch(); u.frameReq = true; }
    Tip("Arrange the tree top-down, children left to right");
    ImGui::SameLine();
    if (ImGui::SmallButton("Frame##bt")) u.frameReq = true;
    Tip("Zoom and pan to fit the tree (F)");
    ImGui::SameLine();
    ImGui::Checkbox("Tidy##bt", &u.tidy);
    Tip("Re-arrange the tree after every structural edit");
    ImGui::SameLine();
    ImGui::Checkbox("Minimap##bt", &u.minimap);
    ImGui::SameLine();
    ImGui::Checkbox("Palette##bt", &u.showPalette);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130);
    const bool findGo = ImGui::InputTextWithHint("##btfind", "Find node", u.find, sizeof(u.find), ImGuiInputTextFlags_EnterReturnsTrue);
    Tip("Matching nodes light up; Enter selects and centres the first");
    if (findGo) {
        const std::string f = Lower(u.find);
        for (const BtNode& n : c.t.nodes)
            if (Lower(n.name).find(f) != std::string::npos || Lower(n.Def().title).find(f) != std::string::npos) { Select(u, n.id); u.focusX = 1.0f; break; }
    }
    const int probs = c.ProblemCount();
    if (probs > 0) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.95f, 0.38f, 0.35f, 1), "%d problem%s", probs, probs == 1 ? "" : "s");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("See the Issues tab");
    }
    if (c.playing && c.live) {
        ImGui::SameLine();
        ImGui::TextColored(AccentCol(1.0f), "live: %s", c.live->gameObject->name.c_str());
    }
}

} // namespace

// ---------------------------------------------------------------------------
// The window
// ---------------------------------------------------------------------------
bool BehaviorTreeWindowFocused() { return g_showBehaviorTree && g_focused; }

void OpenBehaviorTreeFile(EditorState& ed, const std::string& path) {
    (void)ed;
    std::string err;
    if (DocOpen(path, &err)) { ClearSel(U()); U().frameReq = true; g_showBehaviorTree = true; ConsoleLog("Opened " + path); }
    else ConsoleLog("Behavior Tree: " + err, 2);
}

void DrawBehaviorTreeWindow(EditorState& ed) {
    g_focused = false;
    if (!g_showBehaviorTree) return;
    if (std::getenv("OKAY_UI_BT")) {
        ImGui::SetNextWindowPos(ImVec2(24, 48), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(1400, 800), ImGuiCond_Always);
    } else {
        ImGui::SetNextWindowSize(ImVec2(1180, 680), ImGuiCond_FirstUseEver);
    }
    Doc& d = D();
    if (!ed.projectDir().empty()) BehaviorTree::SetAssetBaseDir(ed.projectDir());   // relative tree paths resolve
    const std::string title = std::string("Behavior Tree") + (d.dirty ? "*" : "") + "###BehaviorTree";
    if (!ImGui::Begin(title.c_str(), &g_showBehaviorTree)) { ImGui::End(); return; }
    g_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    Ui& u = U();
    Ctx c(ed);
    c.playing = ed.isPlaying();

    // The selected object's runner opens its tree (when nothing unsaved is being edited).
    if (GameObject* go = ed.selected()) {
        if (auto* r = go->GetComponent<BehaviorTreeRunner>(); r && !r->treePath.empty()) {
            const std::string full = BehaviorTree::ResolvePath(r->treePath);
            static std::string s_lastAuto;
            if (full != d.path && s_lastAuto != full && !d.dirty && std::filesystem::exists(full)) {
                s_lastAuto = full;
                std::string err;
                if (DocOpen(full, &err)) { ClearSel(u); u.frameReq = true; }
            }
            u.liveObj = go->name;
        }
    }
    // The runner the live view follows.
    if (c.playing) {
        for (BehaviorTreeRunner* r : RunnersFor(ed))
            if (r->gameObject->name == u.liveObj) { c.live = r; break; }
        if (!c.live) { const auto rs = RunnersFor(ed); if (!rs.empty()) { c.live = rs.front(); u.liveObj = c.live->gameObject->name; } }
        if (c.live && !c.live->Tree()) c.live = nullptr;
    }
    ClampSel(u, c.t);
    c.Validate();
    {   // OKAY_UI_BTSEL=<node name> / OKAY_UI_BTTAB=<n>: dev screenshots.
        static bool s_devSel = false;
        if (!s_devSel && !c.t.nodes.empty()) {
            if (const char* e = std::getenv("OKAY_UI_BTSEL")) {
                for (const BtNode& n : c.t.nodes)
                    if (n.name == e || std::string(n.Def().title) == e) { Select(u, n.id); s_devSel = true; break; }
            }
            if (const char* e = std::getenv("OKAY_UI_BTTAB")) { u.forceSideTab = std::atoi(e); s_devSel = true; }
        }
    }

    DrawToolbar(c, u);
    ClampSel(u, c.t);

    // ---- palette | canvas | splitter | side panel ----
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    u.sideW = std::clamp(u.sideW, 240.0f, std::max(240.0f, avail.x * 0.5f));
    const float palW = u.showPalette ? u.paletteW : 0.0f;
    if (u.showPalette) { DrawPalette(c, u, ImVec2(palW, avail.y)); ImGui::SameLine(0, 4); }
    DrawCanvas(c, u, ImVec2(avail.x - u.sideW - 8.0f - (u.showPalette ? palW + 4.0f : 0.0f), avail.y));
    ImGui::SameLine(0, 0);
    ImGui::InvisibleButton("##btsplit", ImVec2(8.0f, avail.y));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive()) u.sideW = std::clamp(u.sideW - ImGui::GetIO().MouseDelta.x, 240.0f, std::max(240.0f, avail.x * 0.5f));
    {
        const ImRect r(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImGui::GetWindowDrawList()->AddLine(ImVec2(r.Min.x + 4, r.Min.y + 8), ImVec2(r.Min.x + 4, r.Max.y - 8),
                                            ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_SeparatorActive : ImGui::IsItemHovered() ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator), 2.0f);
    }
    ImGui::SameLine(0, 0);
    ImGui::BeginChild("##btside", ImVec2(u.sideW, avail.y), ImGuiChildFlags_Borders);
    ClampSel(u, c.t);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, ImGui::GetStyle().FramePadding.y));
    const bool sideTabs = ImGui::BeginTabBar("##btsidetabs", ImGuiTabBarFlags_FittingPolicyResizeDown);
    ImGui::PopStyleVar();
    if (sideTabs) {
        const int probs = c.ProblemCount();
        const int warns = (int)c.issues.size() - probs;
        const std::string issueLabel = (probs + warns) > 0 ? "Issues " + std::to_string(probs + warns) + "###btt2" : "Issues###btt2";
        auto tab = [&](const char* label, int idx) { return ImGui::BeginTabItem(label, nullptr, u.forceSideTab == idx ? ImGuiTabItemFlags_SetSelected : 0); };
        if (tab("Node###btt0", 0)) {
            if (u.primary >= 0 && c.t.Find(u.primary)) DrawNodeInspector(c, u, u.primary);
            else {
                ImGui::TextDisabled("Select a node on the canvas.");
                ImGui::TextWrapped("A tree runs from its root every tick: composites decide which children run, decorators change a child's result, tasks move, look, attack, wait or talk to scripts, and services keep the blackboard fresh while their node runs.");
            }
            ImGui::EndTabItem();
        }
        if (tab("Blackboard###btt1", 1)) { DrawBlackboardTab(c, u); ImGui::EndTabItem(); }
        if (probs > 0) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.38f, 0.35f, 1.0f));
        else if (warns > 0) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.75f, 0.25f, 1.0f));
        const bool issOpen = tab(issueLabel.c_str(), 2);
        if (probs > 0 || warns > 0) ImGui::PopStyleColor();
        if (issOpen) { DrawIssuesTab(c, u); ImGui::EndTabItem(); }
        if (tab(c.playing && c.live ? "Runner (live)###btt3" : "Runner###btt3", 3)) { DrawRunnerTab(c, u); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
        u.forceSideTab = -1;
    }
    ImGui::EndChild();
    // Ctrl+S saves while the window has focus.
    if (g_focused && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false) && !d.path.empty()) {
        std::string err;
        if (DocSave(d.path, &err)) ConsoleLog("Saved " + d.path); else ConsoleLog("Behavior Tree: " + err, 2);
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Inspector / Add Component / dev hooks
// ---------------------------------------------------------------------------
void DrawBehaviorTreeInspector(EditorState& ed, GameObject* go, Component* comp, Component*& toRemove, BtCompHeaderFn header) {
    if (!go || !comp) return;
    auto* r = dynamic_cast<BehaviorTreeRunner*>(comp);
    if (!r) return;
    if (!header("Behavior Tree Runner", r, &toRemove, true)) return;
    if (!ed.projectDir().empty()) BehaviorTree::SetAssetBaseDir(ed.projectDir());
    {
        char buf[260];
        std::snprintf(buf, sizeof(buf), "%s", r->treePath.c_str());
        ImGui::SetNextItemWidth(-60);
        if (ImGui::InputText("Tree##btr", buf, sizeof(buf))) { r->treePath = buf; ed.dirty = true; }
        Tip("The .okaybt file, relative to the project folder (Assets/Guard.okaybt).");
        ImGui::SameLine();
        if (ImGui::SmallButton("...##btrpick")) {
            const char* filt[] = {"*.okaybt"};
            const std::string dir = ProjectAssetsDir(ed);
            if (const char* f = tinyfd_openFileDialog("Pick a behavior tree", dir.empty() ? nullptr : (dir + "/").c_str(), 1, filt, "behavior trees", 0)) {
                ed.PushUndo(); r->treePath = ProjectRelative(ed, f); ed.dirty = true;
            }
        }
    }
    if (ImGui::SmallButton("Open in Behavior Tree window##btr")) {
        if (r->treePath.empty()) { g_showBehaviorTree = true; }
        else OpenBehaviorTreeFile(ed, BehaviorTree::ResolvePath(r->treePath));
        U().liveObj = go->name;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Guard sample##btr")) {
        // Write the sample into the project and point the runner at it.
        const std::string dir = ProjectAssetsDir(ed);
        const std::string path = dir.empty() ? std::string("Guard.okaybt") : (std::filesystem::path(dir) / "Guard.okaybt").string();
        std::string err;
        if (BehaviorTree::GuardSample().SaveToFile(path, &err)) {
            ed.PushUndo();
            r->treePath = ProjectRelative(ed, path);
            ed.dirty = true;
            BehaviorTree::Forget(path);
            OpenBehaviorTreeFile(ed, path);
            U().liveObj = go->name;
        } else ConsoleLog("Behavior Tree: " + err, 2);
    }
    Tip("Write Assets/Guard.okaybt (patrol, see the player, chase, attack, back to the patrol) and use it here.");
    if (ImGui::DragFloat("Tick Rate##btr", &r->tickRate, 0.5f, 0.0f, 120.0f, "%.0f /s")) ed.dirty = true;
    Tip("Ticks per second (0 = every frame).");
    if (ImGui::Checkbox("Run On Start##btr", &r->runOnStart)) ed.dirty = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Debug Log##btr", &r->debug)) ed.dirty = true;
    Tip("Log every node that finishes and every run of the tree.");
    if (ImGui::Checkbox("Home Point##btr", &r->homeOverride)) ed.dirty = true;
    Tip("Where Wander roams around (off: where the NPC starts).");
    if (r->homeOverride) { ImGui::SameLine(); ImGui::SetNextItemWidth(-1); if (ImGui::DragFloat3("##btrhome", &r->homePoint.x, 0.05f)) ed.dirty = true; }
    // Blackboard overrides.
    ImGui::TextDisabled("Blackboard overrides");
    Tip("Values that replace the tree's defaults on this NPC.");
    std::shared_ptr<const BehaviorTree> tree = r->treePath.empty() ? nullptr : BehaviorTree::Load(r->treePath);
    int removeAt = -1;
    for (std::size_t i = 0; i < r->overrides.size(); ++i) {
        ImGui::PushID((int)i);
        auto& ov = r->overrides[i];
        char kb[64], vb[128];
        std::snprintf(kb, sizeof(kb), "%s", ov.first.c_str());
        std::snprintf(vb, sizeof(vb), "%s", ov.second.c_str());
        ImGui::SetNextItemWidth(110);
        if (tree && !tree->keys.empty()) {
            if (ImGui::BeginCombo("##ok", ov.first.empty() ? "(key)" : ov.first.c_str())) {
                for (const auto& k : tree->keys) if (ImGui::Selectable(k.name.c_str(), k.name == ov.first)) { ov.first = k.name; ed.dirty = true; }
                ImGui::EndCombo();
            }
        } else if (ImGui::InputTextWithHint("##ok", "key", kb, sizeof(kb))) { ov.first = kb; ed.dirty = true; }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-30);
        if (ImGui::InputTextWithHint("##ov", "value", vb, sizeof(vb))) { ov.second = vb; ed.dirty = true; }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) removeAt = (int)i;
        ImGui::PopID();
    }
    if (removeAt >= 0) { ed.PushUndo(); r->overrides.erase(r->overrides.begin() + removeAt); ed.dirty = true; }
    if (ImGui::SmallButton("+ Override##btr")) { ed.PushUndo(); r->overrides.emplace_back(tree && !tree->keys.empty() ? tree->keys.front().name : std::string(), std::string()); ed.dirty = true; }
    if (!tree && !r->treePath.empty()) ImGui::TextColored(ImVec4(0.95f, 0.38f, 0.35f, 1), "Cannot read %s", r->treePath.c_str());
    if (go->GetComponent<NavMeshAgent>()) ImGui::TextDisabled("Moves through the Nav Mesh Agent.");
    else if (go->GetComponent<NPCController>()) ImGui::TextDisabled("Moves through the NPC Controller (its states stand down).");
    else ImGui::TextDisabled("Moves over the navigation on its own (add a Nav Mesh Agent for avoidance).");
    if (ed.isPlaying()) {
        ImGui::Text("%s", r->IsRunning() ? "Running" : "Stopped");
        ImGui::SameLine();
        ImGui::TextDisabled("%.1f s, %d run%s%s%s", (double)r->Clock(), r->Completions(), r->Completions() == 1 ? "" : "s",
                            r->Completions() ? ", last " : "", r->Completions() ? BtStatusName(r->LastResult()) : "");
        const std::vector<int> path = r->ActivePath();
        if (!path.empty() && r->Tree()) {
            std::string s;
            for (std::size_t i = 0; i < path.size(); ++i) { const BtNode* n = r->Tree()->Find(path[i]); if (i) s += " > "; s += n ? (n->name.empty() ? n->Def().title : n->name) : "?"; }
            ImGui::TextWrapped("%s", s.c_str());
        }
        for (const auto& e : r->blackboard.Entries()) ImGui::TextDisabled("%s = %s", e.name.c_str(), e.value.AsString().c_str());
        if (ImGui::SmallButton(r->IsRunning() ? "Stop##btrlive" : "Run##btrlive")) { if (r->IsRunning()) r->Stop(); else r->Run(); }
    }
    if (ImGui::SmallButton("Remove##btrunner")) toRemove = r;
}

void BehaviorTreeAddComponentItems(EditorState& ed, GameObject* go, const std::function<bool(bool, const char*)>& item) {
    if (!go) return;
    if (item(!go->GetComponent<BehaviorTreeRunner>(), "Behavior Tree Runner (NPC AI from a tree)")) {
        ed.PushUndo();
        go->AddComponent<BehaviorTreeRunner>();
        ed.dirty = true;
        g_showBehaviorTree = true;
        U().liveObj = go->name;
    }
}

void BehaviorTreeDevHooks(EditorState& ed) {
    static int s_frame = 0;
    if (s_frame < 100000) ++s_frame;
    if (std::getenv("OKAY_UI_BT")) g_showBehaviorTree = true;
    if (std::getenv("OKAY_UI_BTGUARD") && s_frame == 34) {
        Scene& s = ed.scene();
        // Waypoints around the ground, a guard crate with an agent and the runner.
        const Vec3 wps[3] = {{8, 0, 8}, {-8, 0, 8}, {-8, 0, -8}};
        for (int i = 0; i < 3; ++i) {
            GameObject* w = s.CreateGameObject("Waypoint " + std::to_string(i + 1));
            w->transform->localPosition = wps[i];
        }
        GameObject* guard = s.CreateGameObject("Guard");
        guard->transform->localPosition = {8, 0.5f, 8};
        guard->transform->localRotation = Quat::LookRotation({-1, 0, 0});
        auto* mr = guard->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Cube();
        mr->color = Color::FromBytes(200, 70, 60);
        auto* agent = guard->AddComponent<NavMeshAgent>();
        agent->speed = 3.0f; agent->baseOffset = 0.5f; agent->radius = 0.5f;
        const std::string dir = ProjectAssetsDir(ed);
        const std::string path = dir.empty() ? std::string("Guard.okaybt") : (std::filesystem::path(dir) / "Guard.okaybt").string();
        std::string err;
        if (!BehaviorTree::GuardSample().SaveToFile(path, &err)) ConsoleLog("Behavior Tree: " + err, 2);
        BehaviorTree::Forget(path);
        BehaviorTree::SetAssetBaseDir(ed.projectDir());
        auto* r = guard->AddComponent<BehaviorTreeRunner>();
        r->treePath = ProjectRelative(ed, path);
        r->overrides = {{"health", "30"}};
        // A navmesh to walk on.
        NavMeshSurface* ns = s.FindObjectOfType<NavMeshSurface>();
        if (!ns) ns = s.CreateGameObject("Nav Mesh")->AddComponent<NavMeshSurface>();
        NavMeshSurface::SetAssetBaseDir(ed.projectDir());
        if (ns->navPath.empty()) ns->navPath = "Assets/" + std::filesystem::path(ed.path().empty() ? std::string("Scene") : ed.path()).stem().string() + ".okaynav";
        if (ns->Bake(s, nullptr, &err)) ns->Save(BehaviorTree::ResolvePath(ns->navPath)); else ConsoleLog("Behavior Tree: bake failed: " + err, 2);
        ed.Select(guard);
        OpenBehaviorTreeFile(ed, path);
        U().liveObj = "Guard";
        ed.dirty = true;
        // OKAY_UI_BTBREAK=1: a few problems in the opened tree, for the validation glyphs.
        if (std::getenv("OKAY_UI_BTBREAK")) {
            BehaviorTree& t = D().tree;
            if (t.root >= 0) {
                t.Find(t.AddNode(BtNodeType::Sequence, t.root))->name = "Empty";
                BtNode* sb = t.Find(t.AddNode(BtNodeType::SetBlackboard, t.root));
                sb->Set("key", "mood"); sb->Set("value", "angry");
                for (BtNode& n : t.nodes) if (n.type == BtNodeType::Patrol) n.Set("waypoints", "Waypoint 1, Waypoint 2, Waypoint 9");
                t.Layout(kNodeW, kNodeH, 22.0f, 64.0f, kServiceRowH);
                D().dirty = true;
                U().frameReq = true;
            }
        }
    }
}

} // namespace oked
