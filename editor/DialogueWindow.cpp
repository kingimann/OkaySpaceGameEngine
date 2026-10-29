// The Dialogue window: a node-graph editor for `.okaydialog` conversations and a
// Quests tab for `.okayquest` files. The canvas works like the Animator's (pan,
// zoom, box select, drag, inline text edit, minimap); links are dragged from a
// node's output port onto another node. The side panel edits the selected node,
// lists the graph's problems, and plays the graph in a Test panel with its own
// story variables, items and quests. Also the inspectors of the story components.
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "DialogueWindow.hpp"
#include "ConsolePanel.hpp"             // ConsoleLog
#include "EditorWidgets.hpp"            // AccentCol
#include "okay/Components/DialogueGraph.hpp"
#include "okay/Components/DialogueRunner.hpp"
#include "okay/Components/QuestLog.hpp"
#include "okay/Components/StoryState.hpp"
#include "okay/Inventory/ItemDatabase.hpp"
#include "okay/Input/Input.hpp"
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
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::editor;

namespace oked {

bool g_showDialogue = false;

namespace {

using Kind = DialogueNode::Kind;
namespace fs = std::filesystem;

constexpr float kNodeW = 230.0f, kHeadH = 24.0f, kRowH = 20.0f, kPortR = 5.0f;
constexpr int kKinds = 11;

// ---------------------------------------------------------------------------
// Small widgets
// ---------------------------------------------------------------------------
bool TextEdit(const char* label, std::string& s, const char* hint = nullptr, float width = 0.0f) {
    char buf[1024];
    std::snprintf(buf, sizeof(buf), "%s", s.c_str());
    if (width > 0.0f) ImGui::SetNextItemWidth(width);
    const bool ch = hint ? ImGui::InputTextWithHint(label, hint, buf, sizeof(buf)) : ImGui::InputText(label, buf, sizeof(buf));
    if (ch) s = buf;
    return ch;
}
bool TextEditMulti(const char* label, std::string& s, float h) {
    char buf[4096];
    std::snprintf(buf, sizeof(buf), "%s", s.c_str());
    const bool ch = ImGui::InputTextMultiline(label, buf, sizeof(buf), ImVec2(-FLT_MIN, h));
    if (ch) s = buf;
    return ch;
}
void Tip(const char* text) { if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", text); }
std::string Lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
std::string Clip(const std::string& s, std::size_t n) {
    std::string t = s;
    for (auto& c : t) if (c == '\n' || c == '\r') c = ' ';
    return t.size() > n ? t.substr(0, n - 1) + "\xE2\x80\xA6" : t;
}
ImU32 Col(float r, float g, float b, float a = 1.0f) { return ImGui::GetColorU32(ImVec4(r, g, b, a)); }
ImU32 WarnCol() { return Col(0.95f, 0.75f, 0.25f); }
ImU32 ErrCol() { return Col(0.95f, 0.35f, 0.3f); }
ImU32 KindCol(Kind k) {
    switch (k) {
        case Kind::Line: return Col(0.30f, 0.55f, 0.85f);
        case Kind::Choice: return Col(0.85f, 0.60f, 0.25f);
        case Kind::Condition: return Col(0.60f, 0.45f, 0.85f);
        case Kind::Set: return Col(0.35f, 0.70f, 0.55f);
        case Kind::Event: return Col(0.90f, 0.45f, 0.70f);
        case Kind::GiveItem: return Col(0.45f, 0.75f, 0.35f);
        case Kind::TakeItem: return Col(0.75f, 0.55f, 0.30f);
        case Kind::StartQuest: return Col(0.85f, 0.75f, 0.30f);
        case Kind::CompleteObjective: return Col(0.65f, 0.80f, 0.35f);
        case Kind::Jump: return Col(0.55f, 0.55f, 0.60f);
        case Kind::End: return Col(0.70f, 0.30f, 0.30f);
    }
    return Col(0.5f, 0.5f, 0.5f);
}
void DrawWarnGlyph(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    dl->AddTriangleFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y + r * 0.8f), ImVec2(c.x - r, c.y + r * 0.8f), col);
    dl->AddLine(ImVec2(c.x, c.y - r * 0.35f), ImVec2(c.x, c.y + r * 0.2f), Col(0.1f, 0.1f, 0.1f), 1.5f);
    dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.5f), 1.0f, Col(0.1f, 0.1f, 0.1f));
}

// An item from the Item Database (typed ids stay allowed).
bool ItemCombo(const char* label, std::string& id, float width = 180.0f) {
    ItemDatabase& db = ItemDatabase::Get();
    const ItemDefinition* cur = db.Find(id);
    const bool unknown = !id.empty() && (!cur || cur->loose);
    bool changed = false;
    if (unknown) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.3f, 1.0f));
    ImGui::SetNextItemWidth(width);
    const bool open = ImGui::BeginCombo(label, id.empty() ? "(item)" : (cur && !cur->loose ? cur->DisplayName().c_str() : id.c_str()), ImGuiComboFlags_HeightLarge);
    if (unknown) ImGui::PopStyleColor();
    if (unknown && ImGui::IsItemHovered()) ImGui::SetTooltip("\"%s\" isn't in the Item Database (it still works as a plain id).", id.c_str());
    if (open) {
        static char filter[64] = "";
        if (ImGui::IsWindowAppearing()) { filter[0] = 0; ImGui::SetKeyboardFocusHere(); }
        ImGui::InputTextWithHint("##dlgitemflt", "Search or type an id", filter, sizeof(filter));
        const std::string f = Lower(filter);
        for (const auto& d : db.Items()) {
            if (d.loose) continue;
            if (!f.empty() && Lower(d.id + " " + d.name).find(f) == std::string::npos) continue;
            if (ImGui::Selectable((d.DisplayName() + "##" + d.id).c_str(), d.id == id)) { id = d.id; changed = true; }
        }
        if (filter[0] && !db.Find(filter) && ImGui::Selectable((std::string("Use \"") + filter + "\"").c_str())) { id = filter; changed = true; }
        ImGui::EndCombo();
    }
    return changed;
}

// ---------------------------------------------------------------------------
// Window state
// ---------------------------------------------------------------------------
struct TestHost final : DialogueHost {
    std::map<std::string, vs::VsValue> vars;
    std::set<std::string> flags;
    std::map<std::string, int> items;
    std::map<std::string, std::string> quests;
    std::vector<std::string> log;
    vs::VsValue GetVar(const std::string& n) override { auto it = vars.find(n); return it == vars.end() ? vs::VsValue{} : it->second; }
    void SetVar(const std::string& n, const vs::VsValue& v) override { vars[n] = v; log.push_back("set " + n + " = " + v.AsString()); }
    bool HasFlag(const std::string& n) override { return flags.count(n) != 0; }
    void SetFlag(const std::string& n, bool on) override { if (on) flags.insert(n); else flags.erase(n); log.push_back(std::string(on ? "flag " : "clear flag ") + n); }
    std::string QuestState(const std::string& id) override { auto it = quests.find(id); return it == quests.end() ? "inactive" : it->second; }
    int  ItemCount(const std::string& it) override { auto i = items.find(it); return i == items.end() ? 0 : i->second; }
    bool GiveItem(const std::string& it, int n) override { items[it] += n; log.push_back("give " + it + " x" + std::to_string(n)); return true; }
    bool TakeItem(const std::string& it, int n) override {
        const bool ok = ItemCount(it) >= n;
        if (ok) { items[it] -= n; if (items[it] <= 0) items.erase(it); }
        log.push_back(std::string(ok ? "take " : "take (not enough!) ") + it + " x" + std::to_string(n));
        return ok;
    }
    void StartQuest(const std::string& id) override { quests[id] = "active"; log.push_back("start quest " + id); }
    void CompleteObjective(const std::string& id, int i) override { log.push_back("complete objective " + id + " #" + std::to_string(i)); }
    void Event(const std::string& n) override { log.push_back("event " + n); }
    void Sound(const std::string& p) override { log.push_back("sound " + p); }
};

struct Ui {
    int tab = 0;                       // 0 Dialogue, 1 Quests
    // ---- the graph ----
    DialogueGraph graph;
    std::string path;
    bool dirty = false;
    std::vector<std::string> undo, redo;
    std::vector<DialogueGraph::Issue> issues;
    std::map<int, int> severity;        // node id -> worst severity
    bool validated = false;
    // ---- canvas ----
    ImVec2 pan{60.0f, 60.0f};
    float zoom = 1.0f;
    bool frameReq = true, minimap = true;
    bool panning = false, boxing = false;
    ImVec2 boxStart{}, rightDown{};
    bool dragPushed = false;
    std::vector<int> sel;               // node ids
    int primary = -1;
    int linkFrom = -1, linkPort = -1;   // a link being dragged
    int newLinkFrom = -1, newLinkPort = -1; ImVec2 newLinkAt{};   // dropped on empty space: add + link
    int selLinkNode = -1, selLinkPort = -1;
    int editing = -1; bool editFocus = false; char editBuf[2048] = "";
    std::vector<DialogueNode> clipboard;
    int hovNode = -1;
    // ---- side panel ----
    int sideTab = 0;                    // 0 Node, 1 Graph, 2 Problems, 3 Test
    int forceSideTab = -1;
    float sideW = 400.0f;
    char find[48] = "";
    // ---- test ----
    DialogueSession test;
    TestHost host;
    std::vector<std::string> transcript;
    char varName[64] = "", varValue[64] = "", itemName[64] = "", questName[64] = "";
    int itemCount = 1;
    // ---- quests ----
    std::vector<QuestDef> quests;
    std::string questPath;
    bool questDirty = false;
    int questSel = -1;
    std::vector<std::string> questUndo, questRedo;
    std::vector<QuestFile::Issue> questIssues;
    std::vector<std::string> questFiles;   // .okayquest files in the project
    std::vector<std::string> dialogFiles;  // .okaydialog files in the project
    double lastScan = -1.0;
    std::string pendingOpen;
};
Ui& U() { static Ui u; return u; }
bool g_focused = false;

// ---------------------------------------------------------------------------
// Project files
// ---------------------------------------------------------------------------
std::string AssetsDir(EditorState& ed) {
    if (ed.projectDir().empty()) return "";
    const fs::path a = fs::path(ed.projectDir()) / "Assets";
    std::error_code ec;
    return fs::exists(a, ec) ? a.string() : ed.projectDir();
}
std::string ProjectRelative(EditorState& ed, const std::string& path) {
    if (ed.projectDir().empty()) return path;
    std::error_code ec;
    const fs::path rel = fs::relative(fs::path(path), fs::path(ed.projectDir()), ec);
    if (ec || rel.empty() || rel.string().rfind("..", 0) == 0) return path;
    return rel.generic_string();
}
void ScanFiles(EditorState& ed, bool force = false) {
    Ui& u = U();
    const double now = ImGui::GetTime();
    if (!force && u.lastScan >= 0.0 && now - u.lastScan < 3.0) return;
    u.lastScan = now;
    u.questFiles.clear(); u.dialogFiles.clear();
    if (ed.projectDir().empty()) return;
    std::error_code ec;
    for (auto& e : fs::recursive_directory_iterator(fs::path(ed.projectDir()), fs::directory_options::skip_permission_denied, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const std::string x = Lower(e.path().extension().string());
        if (x == ".okayquest") u.questFiles.push_back(e.path().string());
        else if (x == ".okaydialog") u.dialogFiles.push_back(e.path().string());
    }
    std::sort(u.questFiles.begin(), u.questFiles.end());
    std::sort(u.dialogFiles.begin(), u.dialogFiles.end());
}
// Every quest id the project knows: the Quests tab's list plus the project's files.
std::vector<std::string> KnownQuestIds(EditorState& ed) {
    Ui& u = U();
    ScanFiles(ed);
    std::vector<std::string> ids;
    for (const auto& q : u.quests) ids.push_back(q.id);
    for (const auto& f : u.questFiles) {
        if (f == u.questPath) continue;
        std::vector<QuestDef> qs;
        if (QuestFile::Load(f, qs)) for (const auto& q : qs) ids.push_back(q.id);
    }
    for (const auto& up : ed.scene().Objects())
        if (up) if (auto* ql = up->GetComponent<QuestLog>()) for (const auto& q : ql->quests) ids.push_back(q.id);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}
std::vector<std::string> KnownItemIds() {
    std::vector<std::string> ids;
    for (const auto& d : ItemDatabase::Get().Items()) if (!d.loose) ids.push_back(d.id);
    return ids;
}
bool QuestCombo(EditorState& ed, const char* label, std::string& id, float width = 180.0f) {
    const std::vector<std::string> ids = KnownQuestIds(ed);
    const bool unknown = !id.empty() && std::find(ids.begin(), ids.end(), id) == ids.end();
    bool changed = false;
    if (unknown) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.3f, 1.0f));
    ImGui::SetNextItemWidth(width);
    const bool open = ImGui::BeginCombo(label, id.empty() ? "(quest)" : id.c_str());
    if (unknown) ImGui::PopStyleColor();
    if (unknown && ImGui::IsItemHovered()) ImGui::SetTooltip("No quest with this id in the project's .okayquest files or Quest Logs.");
    if (open) {
        static char filter[64] = "";
        if (ImGui::IsWindowAppearing()) { filter[0] = 0; ImGui::SetKeyboardFocusHere(); }
        ImGui::InputTextWithHint("##dlgqflt", "Search or type an id", filter, sizeof(filter));
        for (const auto& q : ids) {
            if (filter[0] && Lower(q).find(Lower(filter)) == std::string::npos) continue;
            if (ImGui::Selectable(q.c_str(), q == id)) { id = q; changed = true; }
        }
        if (filter[0] && ImGui::Selectable((std::string("Use \"") + filter + "\"").c_str())) { id = filter; changed = true; }
        ImGui::EndCombo();
    }
    return changed;
}

// ---------------------------------------------------------------------------
// Graph edits (undo by snapshot)
// ---------------------------------------------------------------------------
void Revalidate(EditorState& ed) {
    Ui& u = U();
    const std::vector<std::string> quests = KnownQuestIds(ed), items = KnownItemIds();
    u.issues = u.graph.Validate(&quests, &items);
    u.severity.clear();
    for (const auto& i : u.issues) if (i.node >= 0) u.severity[i.node] = std::max(u.severity[i.node], i.severity);
    u.validated = true;
}
void PushUndo() {
    Ui& u = U();
    u.undo.push_back(u.graph.Write());
    if (u.undo.size() > 100) u.undo.erase(u.undo.begin());
    u.redo.clear();
}
void Changed(EditorState& ed) { U().dirty = true; Revalidate(ed); }
void Undo(EditorState& ed) {
    Ui& u = U();
    if (u.undo.empty()) return;
    u.redo.push_back(u.graph.Write());
    u.graph.Parse(u.undo.back());
    u.undo.pop_back();
    u.editing = -1;
    Changed(ed);
}
void Redo(EditorState& ed) {
    Ui& u = U();
    if (u.redo.empty()) return;
    u.undo.push_back(u.graph.Write());
    u.graph.Parse(u.redo.back());
    u.redo.pop_back();
    u.editing = -1;
    Changed(ed);
}
bool IsSel(const Ui& u, int id) { return std::find(u.sel.begin(), u.sel.end(), id) != u.sel.end(); }
void ClearSel(Ui& u) { u.sel.clear(); u.primary = -1; u.selLinkNode = -1; u.selLinkPort = -1; }
void Select(Ui& u, int id, int mode = 0) {   // 0 only, 1 add, 2 toggle
    if (mode == 0) { u.sel.clear(); u.sel.push_back(id); }
    else if (mode == 1) { if (!IsSel(u, id)) u.sel.push_back(id); }
    else { if (IsSel(u, id)) u.sel.erase(std::find(u.sel.begin(), u.sel.end(), id)); else u.sel.push_back(id); }
    u.primary = IsSel(u, id) ? id : (u.sel.empty() ? -1 : u.sel.back());
    u.selLinkNode = -1; u.selLinkPort = -1;
}
int AddNode(EditorState& ed, Kind kind, ImVec2 at) {
    Ui& u = U();
    PushUndo();
    DialogueNode& n = u.graph.Add(kind, at.x, at.y);
    if (u.graph.nodes.size() == 1) u.graph.start = n.id;
    const int id = n.id;
    Select(u, id);
    u.forceSideTab = 0;
    Changed(ed);
    return id;
}
void DeleteNodes(EditorState& ed, std::vector<int> ids) {
    Ui& u = U();
    if (ids.empty()) return;
    PushUndo();
    for (int id : ids) u.graph.Remove(id);
    ClearSel(u);
    u.editing = -1;
    Changed(ed);
}
void PasteNodes(EditorState& ed, const std::vector<DialogueNode>& src, const ImVec2* at) {
    Ui& u = U();
    if (src.empty()) return;
    PushUndo();
    std::map<int, int> remap;
    float minX = 1e9f, minY = 1e9f;
    for (const auto& n : src) { minX = std::min(minX, n.x); minY = std::min(minY, n.y); }
    const ImVec2 off = at ? ImVec2(at->x - minX, at->y - minY) : ImVec2(40.0f, 40.0f);
    int next = u.graph.NewId();
    for (const auto& n : src) remap[n.id] = next++;
    auto map = [&](int t) { auto it = remap.find(t); return it == remap.end() ? -1 : it->second; };
    u.sel.clear();
    for (const auto& n : src) {
        DialogueNode c = n;
        c.id = remap[n.id];
        c.x += off.x; c.y += off.y;
        c.next = map(n.next); c.onTrue = map(n.onTrue); c.onFalse = map(n.onFalse);
        for (auto& ch : c.choices) ch.target = map(ch.target);
        u.graph.nodes.push_back(c);
        u.sel.push_back(c.id);
    }
    u.primary = u.sel.back();
    Changed(ed);
}
void NewGraph(EditorState& ed) {
    Ui& u = U();
    u.graph = DialogueGraph{};
    u.graph.name = "New Dialogue";
    u.graph.speakers = {"NPC", "Player"};
    DialogueNode& n = u.graph.Add(Kind::Line, 80.0f, 120.0f);
    n.speaker = "NPC"; n.text = "Hello there.";
    u.graph.start = n.id;
    u.path.clear(); u.dirty = false; u.undo.clear(); u.redo.clear();
    ClearSel(u); u.frameReq = true; u.editing = -1;
    u.test.Stop(); u.transcript.clear();
    Revalidate(ed);
}
bool LoadGraphFile(EditorState& ed, const std::string& path) {
    Ui& u = U();
    DialogueGraph g;
    std::string err;
    if (!g.Load(path, &err)) { ConsoleLog("Couldn't open " + path + ": " + err, 2); return false; }
    u.graph = g; u.path = path; u.dirty = false; u.undo.clear(); u.redo.clear();
    ClearSel(u); u.frameReq = true; u.editing = -1;
    u.test.Stop(); u.transcript.clear();
    Revalidate(ed);
    return true;
}
bool SaveGraphFile(EditorState& ed, bool as) {
    Ui& u = U();
    std::string path = u.path;
    if (as || path.empty()) {
        const char* filters[] = {"*.okaydialog"};
        const std::string def = (fs::path(AssetsDir(ed)) / ((u.graph.name.empty() ? std::string("dialogue") : u.graph.name) + ".okaydialog")).string();
        const char* p = tinyfd_saveFileDialog("Save dialogue", def.c_str(), 1, filters, "Dialogue graph (.okaydialog)");
        if (!p || !*p) return false;
        path = p;
        if (fs::path(path).extension() != ".okaydialog") path += ".okaydialog";
    }
    if (!u.graph.Save(path)) { ConsoleLog("Couldn't save " + path, 2); return false; }
    u.path = path; u.dirty = false;
    ConsoleLog("Saved " + path);
    ScanFiles(ed, true);
    return true;
}

// ---------------------------------------------------------------------------
// Node geometry and text
// ---------------------------------------------------------------------------
int Outputs(const DialogueNode& n) { return n.OutputCount(); }
float NodeH(const DialogueNode& n) {
    if (n.kind == Kind::Choice) return kHeadH + 6.0f + kRowH * (float)std::max(1, (int)n.choices.size()) + 6.0f;
    if (n.kind == Kind::Condition) return kHeadH + 6.0f + kRowH * 2.0f + 6.0f;
    if (n.kind == Kind::Line) return kHeadH + 6.0f + kRowH * 2.5f + 6.0f;
    if (n.kind == Kind::End) return kHeadH + 10.0f;
    return kHeadH + 6.0f + kRowH + 6.0f;
}
// The y (graph units, from the node's top) of output port i.
float PortY(const DialogueNode& n, int i) {
    if (n.kind == Kind::Choice || n.kind == Kind::Condition) return kHeadH + 6.0f + kRowH * ((float)i + 0.5f);
    return NodeH(n) * 0.5f + kHeadH * 0.25f;
}
std::string NodeTitle(const DialogueNode& n) {
    switch (n.kind) {
        case Kind::Line: return n.speaker.empty() ? "Line" : n.speaker;
        case Kind::Set: return n.setScope == 1 ? "Set Flag" : n.setScope == 2 ? "Set Local" : "Set Story Var";
        default: return DialogueNode::KindLabel(n.kind);
    }
}
std::string NodeBody(const DialogueGraph& g, const DialogueNode& n) {
    switch (n.kind) {
        case Kind::Line: return n.text.empty() ? "(empty line)" : n.text;
        case Kind::Choice: return "";
        case Kind::Condition: return n.condition.Empty() ? "(no test)" : n.condition.Text();
        case Kind::Set: return n.name.empty() ? "(no variable)" : n.name + " = " + (n.value.empty() ? "\"\"" : n.value);
        case Kind::Event: return n.name.empty() ? "(no name)" : n.name;
        case Kind::GiveItem: case Kind::TakeItem: return (n.item.empty() ? "(no item)" : n.item) + " x" + std::to_string(n.count);
        case Kind::StartQuest: return n.name.empty() ? "(no quest)" : n.name;
        case Kind::CompleteObjective: return (n.name.empty() ? "(no quest)" : n.name) + " #" + std::to_string(n.objective + 1);
        case Kind::Jump: { const DialogueNode* t = g.Find(n.next); return t ? "to " + NodeTitle(*t) + " (#" + std::to_string(t->id) + ")" : "(no target)"; }
        case Kind::End: return "";
    }
    return "";
}
std::string NodeSummary(const DialogueGraph& g, const DialogueNode& n) {
    return "#" + std::to_string(n.id) + " " + NodeTitle(n) + (NodeBody(g, n).empty() ? "" : ": " + Clip(NodeBody(g, n), 28));
}
bool TargetCombo(EditorState& ed, const char* label, int& target, bool allowNone = true) {
    Ui& u = U();
    const DialogueNode* cur = u.graph.Find(target);
    const std::string preview = target < 0 ? std::string(allowNone ? "(end)" : "(none)") : cur ? NodeSummary(u.graph, *cur) : "missing #" + std::to_string(target);
    bool changed = false;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo(label, preview.c_str())) {
        if (ImGui::Selectable(allowNone ? "(end the conversation)" : "(none)", target < 0)) { PushUndo(); target = -1; changed = true; }
        for (const auto& n : u.graph.nodes)
            if (ImGui::Selectable(NodeSummary(u.graph, n).c_str(), n.id == target)) { PushUndo(); target = n.id; changed = true; }
        ImGui::EndCombo();
    }
    if (changed) Changed(ed);
    return changed;
}

// ---------------------------------------------------------------------------
// The condition editor (shared by Condition nodes and choice options)
// ---------------------------------------------------------------------------
bool ConditionEditor(EditorState& ed, DialogueCondition& c, const char* id) {
    bool changed = false;
    ImGui::PushID(id);
    int kind = (int)c.kind;
    const char* kinds[] = {"Always", "Story variable", "Quest state", "Has item", "Flag"};
    ImGui::SetNextItemWidth(118);
    if (ImGui::Combo("##ck", &kind, kinds, 5)) { PushUndo(); c.kind = (DialogueCondition::Kind)kind; changed = true; }
    if (c.kind == DialogueCondition::Kind::StoryVar) {
        ImGui::SameLine();
        if (TextEdit("##cn", c.name, "variable", 96)) { changed = true; }
        if (ImGui::IsItemActivated()) PushUndo();
        ImGui::SameLine();
        const char* ops[] = {"==", "!=", "<", "<=", ">", ">="};
        int op = 0; for (int i = 0; i < 6; ++i) if (c.op == ops[i]) op = i;
        ImGui::SetNextItemWidth(48);
        if (ImGui::Combo("##co", &op, ops, 6)) { PushUndo(); c.op = ops[op]; changed = true; }
        ImGui::SameLine();
        if (TextEdit("##cv", c.value, "value", 70)) changed = true;
        if (ImGui::IsItemActivated()) PushUndo();
    } else if (c.kind == DialogueCondition::Kind::QuestState) {
        ImGui::SameLine();
        if (QuestCombo(ed, "##cq", c.name, 92)) { PushUndo(); changed = true; }
        ImGui::SameLine();
        const char* ops[] = {"is", "is not"};
        int op = c.op == "!=" ? 1 : 0;
        ImGui::SetNextItemWidth(58);
        if (ImGui::Combo("##cqo", &op, ops, 2)) { PushUndo(); c.op = op ? "!=" : "=="; changed = true; }
        ImGui::SameLine();
        const char* states[] = {"inactive", "active", "completed", "failed"};
        int st = 0; for (int i = 0; i < 4; ++i) if (c.value == states[i]) st = i;
        ImGui::SetNextItemWidth(82);
        if (ImGui::Combo("##cqs", &st, states, 4)) { PushUndo(); c.value = states[st]; changed = true; }
        if (c.value.empty()) c.value = "inactive";
    } else if (c.kind == DialogueCondition::Kind::HasItem) {
        ImGui::SameLine();
        if (ItemCombo("##ci", c.name, 140)) { PushUndo(); changed = true; }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        if (ImGui::DragInt("x##cc", &c.count, 0.2f, 1, 9999)) { changed = true; }
        if (ImGui::IsItemActivated()) PushUndo();
    } else if (c.kind == DialogueCondition::Kind::Flag) {
        ImGui::SameLine();
        const char* ops[] = {"is set", "is not set"};
        int op = c.op == "!=" ? 1 : 0;
        ImGui::SetNextItemWidth(90);
        if (ImGui::Combo("##cfo", &op, ops, 2)) { PushUndo(); c.op = op ? "!=" : "=="; changed = true; }
        ImGui::SameLine();
        if (TextEdit("##cf", c.name, "flag", 120)) changed = true;
        if (ImGui::IsItemActivated()) PushUndo();
    }
    ImGui::PopID();
    if (changed) Changed(ed);
    return changed;
}

// ---------------------------------------------------------------------------
// The node inspector
// ---------------------------------------------------------------------------
void DrawNodeInspector(EditorState& ed, DialogueNode& n) {
    Ui& u = U();
    ImGui::PushID(n.id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(p, ImVec2(p.x + 4.0f, p.y + ImGui::GetFrameHeight()), KindCol(n.kind), 2.0f);
    ImGui::Indent(10.0f);
    ImGui::Text("%s  #%d", DialogueNode::KindLabel(n.kind), n.id);
    ImGui::SameLine();
    if (u.graph.start == n.id) ImGui::TextDisabled("(start)");
    else if (ImGui::SmallButton("Make start")) { PushUndo(); u.graph.start = n.id; Changed(ed); }
    ImGui::Unindent(10.0f);
    auto it = u.severity.find(n.id);
    if (it != u.severity.end())
        for (const auto& i : u.issues)
            if (i.node == n.id) { ImGui::PushStyleColor(ImGuiCol_Text, i.severity >= 2 ? ImVec4(0.95f, 0.4f, 0.35f, 1) : ImVec4(0.95f, 0.75f, 0.3f, 1)); ImGui::TextWrapped("%s", i.text.c_str()); ImGui::PopStyleColor(); }
    ImGui::Separator();
    switch (n.kind) {
        case Kind::Line: {
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##speaker", n.speaker.empty() ? "(speaker)" : n.speaker.c_str())) {
                for (const auto& s : u.graph.speakers)
                    if (ImGui::Selectable(s.c_str(), s == n.speaker)) { PushUndo(); n.speaker = s; Changed(ed); }
                ImGui::Separator();
                static char newSpeaker[64] = "";
                ImGui::SetNextItemWidth(160);
                if (ImGui::InputTextWithHint("##newspk", "new speaker", newSpeaker, sizeof(newSpeaker), ImGuiInputTextFlags_EnterReturnsTrue) && newSpeaker[0]) {
                    PushUndo(); u.graph.speakers.push_back(newSpeaker); n.speaker = newSpeaker; newSpeaker[0] = 0; Changed(ed);
                }
                ImGui::EndCombo();
            }
            Tip("Who says it. Speakers are kept in the graph (the Graph tab edits the list).");
            if (TextEditMulti("##text", n.text, 96.0f)) Changed(ed);
            if (ImGui::IsItemActivated()) PushUndo();
            if (TextEdit("Portrait##p", n.portrait, "image path (optional)")) Changed(ed);
            if (ImGui::IsItemActivated()) PushUndo();
            ImGui::SameLine();
            if (ImGui::SmallButton("...##pb")) {
                const char* f[] = {"*.png", "*.jpg", "*.jpeg", "*.tga", "*.bmp"};
                if (const char* sel = tinyfd_openFileDialog("Portrait image", AssetsDir(ed).c_str(), 5, f, "Images", 0)) { PushUndo(); n.portrait = ProjectRelative(ed, sel); Changed(ed); }
            }
            if (TextEdit("Sound##s", n.sound, "audio path (optional)")) Changed(ed);
            if (ImGui::IsItemActivated()) PushUndo();
            ImGui::SameLine();
            if (ImGui::SmallButton("...##sb")) {
                const char* f[] = {"*.wav", "*.ogg", "*.mp3", "*.flac"};
                if (const char* sel = tinyfd_openFileDialog("Line sound", AssetsDir(ed).c_str(), 4, f, "Audio", 0)) { PushUndo(); n.sound = ProjectRelative(ed, sel); Changed(ed); }
            }
            if (ImGui::DragFloat("Auto-advance (s)##a", &n.autoAdvance, 0.05f, 0.0f, 60.0f, "%.2f")) Changed(ed);
            if (ImGui::IsItemActivated()) PushUndo();
            Tip("Seconds before the next node. 0 waits for the confirm key or a click.");
            TargetCombo(ed, "Next##n", n.next);
            break;
        }
        case Kind::Choice: {
            ImGui::TextDisabled("Options (2 to 6). Each can have a condition.");
            for (std::size_t i = 0; i < n.choices.size(); ++i) {
                ImGui::PushID((int)i);
                ImGui::Text("%d.", (int)i + 1);
                ImGui::SameLine();
                if (TextEdit("##t", n.choices[i].text, "option text", -1.0f)) Changed(ed);
                if (ImGui::IsItemActivated()) PushUndo();
                ImGui::Indent(18.0f);
                ConditionEditor(ed, n.choices[i].condition, "cond");
                TargetCombo(ed, "##to", n.choices[i].target);
                if (n.choices.size() > 2 && ImGui::SmallButton("Remove option")) { PushUndo(); n.choices.erase(n.choices.begin() + (long)i); Changed(ed); ImGui::Unindent(18.0f); ImGui::PopID(); break; }
                ImGui::Unindent(18.0f);
                ImGui::PopID();
            }
            if (n.choices.size() < 6 && ImGui::Button("+ Option")) { PushUndo(); n.choices.push_back(DialogueChoice{"Option " + std::to_string(n.choices.size() + 1), {}, -1}); Changed(ed); }
            break;
        }
        case Kind::Condition:
            ImGui::TextDisabled("Test, then take the true or the false output.");
            ConditionEditor(ed, n.condition, "cond");
            TargetCombo(ed, "True##t", n.onTrue);
            TargetCombo(ed, "False##f", n.onFalse);
            break;
        case Kind::Set: {
            const char* scopes[] = {"Story variable", "Flag", "Graph local"};
            if (ImGui::Combo("Scope##sc", &n.setScope, scopes, 3)) { Changed(ed); }
            if (ImGui::IsItemActivated()) PushUndo();
            Tip("Story variables and flags live in the scene's Story State (saved with the game); locals last one conversation.");
            if (TextEdit("Name##nm", n.name, "variable")) Changed(ed);
            if (ImGui::IsItemActivated()) PushUndo();
            if (n.setScope == 1) {
                bool on = DialogueValueFromText(n.value).AsBool();
                if (ImGui::Checkbox("Set (off clears)##flag", &on)) { PushUndo(); n.value = on ? "true" : "false"; Changed(ed); }
            } else {
                if (TextEdit("Value##v", n.value, "number, text or true/false")) Changed(ed);
                if (ImGui::IsItemActivated()) PushUndo();
            }
            TargetCombo(ed, "Next##n", n.next);
            break;
        }
        case Kind::Event:
            ImGui::TextDisabled("Scripts get on_dialogue_event(name); visual scripts On Dialogue Event.");
            if (TextEdit("Name##ev", n.name, "event name")) Changed(ed);
            if (ImGui::IsItemActivated()) PushUndo();
            TargetCombo(ed, "Next##n", n.next);
            break;
        case Kind::GiveItem:
        case Kind::TakeItem:
            ImGui::TextDisabled(n.kind == Kind::GiveItem ? "Puts items in the player's inventory." : "Takes items out of the player's inventory.");
            if (ItemCombo("Item##it", n.item, -FLT_MIN)) { PushUndo(); Changed(ed); }
            if (ImGui::DragInt("Count##ic", &n.count, 0.2f, 1, 9999)) Changed(ed);
            if (ImGui::IsItemActivated()) PushUndo();
            TargetCombo(ed, "Next##n", n.next);
            break;
        case Kind::StartQuest:
            ImGui::TextDisabled("Starts the quest in the scene's Quest Log.");
            if (QuestCombo(ed, "Quest##q", n.name, -FLT_MIN)) { PushUndo(); Changed(ed); }
            TargetCombo(ed, "Next##n", n.next);
            break;
        case Kind::CompleteObjective:
            ImGui::TextDisabled("Marks one objective of an active quest done.");
            if (QuestCombo(ed, "Quest##q", n.name, -FLT_MIN)) { PushUndo(); Changed(ed); }
            {
                int shown = n.objective + 1;
                if (ImGui::DragInt("Objective##o", &shown, 0.2f, 1, 99)) { n.objective = std::max(0, shown - 1); Changed(ed); }
                if (ImGui::IsItemActivated()) PushUndo();
            }
            TargetCombo(ed, "Next##n", n.next);
            break;
        case Kind::Jump:
            ImGui::TextDisabled("Continues at another node (loops, shared endings).");
            TargetCombo(ed, "Target##j", n.next, false);
            break;
        case Kind::End:
            ImGui::TextDisabled("The conversation ends here.");
            break;
    }
    ImGui::Separator();
    if (ImGui::Button("Delete node")) DeleteNodes(ed, {n.id});
    ImGui::PopID();
}

// ---------------------------------------------------------------------------
// The Graph tab: name, start, speakers, locals, file
// ---------------------------------------------------------------------------
void DrawGraphTab(EditorState& ed) {
    Ui& u = U();
    if (TextEdit("Name##gname", u.graph.name, "graph name")) Changed(ed);
    if (ImGui::IsItemActivated()) PushUndo();
    Tip("What \"talk to\" objectives and start_dialogue() may call this conversation by.");
    TargetCombo(ed, "Start##gstart", u.graph.start, false);
    ImGui::TextDisabled("%s", u.path.empty() ? "(not saved yet)" : ProjectRelative(ed, u.path).c_str());
    ImGui::SeparatorText("Speakers");
    for (std::size_t i = 0; i < u.graph.speakers.size(); ++i) {
        ImGui::PushID((int)i);
        std::string s = u.graph.speakers[i];
        if (TextEdit("##spk", s, nullptr, -40.0f)) {
            PushUndo();
            for (auto& n : u.graph.nodes) if (n.speaker == u.graph.speakers[i]) n.speaker = s;
            u.graph.speakers[i] = s;
            Changed(ed);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) { PushUndo(); u.graph.speakers.erase(u.graph.speakers.begin() + (long)i); Changed(ed); ImGui::PopID(); break; }
        ImGui::PopID();
    }
    if (ImGui::SmallButton("+ Speaker")) { PushUndo(); u.graph.speakers.push_back("Speaker " + std::to_string(u.graph.speakers.size() + 1)); Changed(ed); }
    ImGui::SeparatorText("Graph locals");
    ImGui::TextDisabled("Variables that last one conversation, with their starting values.");
    for (std::size_t i = 0; i < u.graph.locals.size(); ++i) {
        ImGui::PushID(1000 + (int)i);
        if (TextEdit("##ln", u.graph.locals[i].name, "name", 120)) Changed(ed);
        if (ImGui::IsItemActivated()) PushUndo();
        ImGui::SameLine();
        if (TextEdit("##lv", u.graph.locals[i].value, "value", 90)) Changed(ed);
        if (ImGui::IsItemActivated()) PushUndo();
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) { PushUndo(); u.graph.locals.erase(u.graph.locals.begin() + (long)i); Changed(ed); ImGui::PopID(); break; }
        ImGui::PopID();
    }
    if (ImGui::SmallButton("+ Local")) { PushUndo(); u.graph.locals.push_back({"local" + std::to_string(u.graph.locals.size() + 1), "0"}); Changed(ed); }
    ImGui::SeparatorText("Stats");
    int lines = 0, choices = 0;
    for (const auto& n : u.graph.nodes) { if (n.kind == Kind::Line) ++lines; if (n.kind == Kind::Choice) ++choices; }
    ImGui::Text("%d nodes: %d lines, %d choices", (int)u.graph.nodes.size(), lines, choices);
}

// ---------------------------------------------------------------------------
// The Problems tab
// ---------------------------------------------------------------------------
void DrawProblems(EditorState& ed) {
    Ui& u = U();
    if (ImGui::Button("Validate again")) Revalidate(ed);
    ImGui::SameLine();
    int errs = 0, warns = 0;
    for (const auto& i : u.issues) (i.severity >= 2 ? errs : warns)++;
    if (u.issues.empty()) ImGui::TextColored(ImVec4(0.5f, 0.85f, 0.55f, 1), "No problems.");
    else ImGui::TextDisabled("%d errors, %d warnings", errs, warns);
    ImGui::Separator();
    for (std::size_t k = 0; k < u.issues.size(); ++k) {
        const auto& i = u.issues[k];
        ImGui::PushID((int)k);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        DrawWarnGlyph(dl, ImVec2(p.x + 7.0f, p.y + ImGui::GetTextLineHeight() * 0.55f), 5.5f, i.severity >= 2 ? ErrCol() : WarnCol());
        ImGui::Dummy(ImVec2(16, 0)); ImGui::SameLine();
        std::string label = i.node >= 0 ? "#" + std::to_string(i.node) + ": " + i.text : i.text;
        if (ImGui::Selectable(label.c_str(), i.node >= 0 && u.primary == i.node) && i.node >= 0) {
            Select(u, i.node); u.forceSideTab = 0;
            if (const DialogueNode* n = u.graph.Find(i.node)) { u.pan = ImVec2(200.0f - n->x * u.zoom, 200.0f - n->y * u.zoom); }
        }
        ImGui::PopID();
    }
}

// ---------------------------------------------------------------------------
// The Test tab: play the graph with its own story
// ---------------------------------------------------------------------------
void TestPresent() {
    Ui& u = U();
    const DialogueNode* n = u.test.Node();
    if (!n) return;
    if (n->kind == Kind::Line) u.transcript.push_back((n->speaker.empty() ? std::string("?") : n->speaker) + ": " + n->text);
    else if (n->kind == Kind::Choice) u.transcript.push_back(u.test.Choices().empty() ? "(no option passes: the conversation ends)" : "(choose)");
    if (!u.test.Active()) u.transcript.push_back("- end -");
}
void TestStart() {
    Ui& u = U();
    u.transcript.clear();
    u.host.log.clear();
    u.test.Begin(&u.graph, &u.host);
    TestPresent();
}
void DrawTest(EditorState& ed) {
    Ui& u = U();
    (void)ed;
    if (ImGui::Button(u.test.Active() ? "Restart" : "Start")) TestStart();
    ImGui::SameLine();
    if (ImGui::Button("Stop")) { u.test.Stop(); if (!u.transcript.empty()) u.transcript.push_back("- stopped -"); }
    ImGui::SameLine();
    if (ImGui::Button("Reset story")) { u.host = TestHost{}; }
    Tip("Clears the test's variables, flags, items and quests.");
    ImGui::Separator();
    const float h = ImGui::GetContentRegionAvail().y;
    if (ImGui::BeginChild("##dlgtranscript", ImVec2(0, std::max(120.0f, h * 0.38f)), ImGuiChildFlags_Borders)) {
        for (const auto& t : u.transcript) ImGui::TextWrapped("%s", t.c_str());
        if (u.test.Active()) {
            const DialogueNode* n = u.test.Node();
            if (n && n->kind == Kind::Choice) {
                for (std::size_t i = 0; i < u.test.Choices().size(); ++i) {
                    const std::string lb = std::to_string(i + 1) + ". " + u.test.Choices()[i].text + "##c" + std::to_string(i);
                    if (ImGui::Button(lb.c_str())) {
                        u.transcript.push_back("> " + u.test.Choices()[i].text);
                        u.test.Choose((int)i);
                        TestPresent();
                        break;
                    }
                }
            } else if (n) {
                if (ImGui::Button("Continue")) { u.test.Advance(); TestPresent(); }
            }
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();
    if (ImGui::BeginChild("##dlgstory", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        ImGui::SeparatorText("Story variables");
        for (auto it = u.host.vars.begin(); it != u.host.vars.end();) {
            ImGui::PushID(it->first.c_str());
            std::string v = it->second.AsString();
            ImGui::Text("%s", it->first.c_str()); ImGui::SameLine(140);
            if (TextEdit("##v", v, nullptr, 110)) it->second = DialogueValueFromText(v);
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) { it = u.host.vars.erase(it); ImGui::PopID(); continue; }
            ImGui::PopID();
            ++it;
        }
        ImGui::SetNextItemWidth(130); ImGui::InputTextWithHint("##nv", "name", u.varName, sizeof(u.varName));
        ImGui::SameLine(); ImGui::SetNextItemWidth(90); ImGui::InputTextWithHint("##nvv", "value", u.varValue, sizeof(u.varValue));
        ImGui::SameLine();
        if (ImGui::SmallButton("Set") && u.varName[0]) { u.host.vars[u.varName] = DialogueValueFromText(u.varValue); }
        ImGui::SeparatorText("Flags");
        std::string flags;
        for (const auto& f : u.host.flags) { if (!flags.empty()) flags += ", "; flags += f; }
        ImGui::TextWrapped("%s", flags.empty() ? "(none)" : flags.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Set flag") && u.varName[0]) u.host.flags.insert(u.varName);
        ImGui::SeparatorText("Items");
        for (auto it = u.host.items.begin(); it != u.host.items.end();) {
            ImGui::PushID(it->first.c_str());
            ImGui::Text("%s x%d", it->first.c_str(), it->second); ImGui::SameLine();
            if (ImGui::SmallButton("x")) { it = u.host.items.erase(it); ImGui::PopID(); continue; }
            ImGui::PopID();
            ++it;
        }
        ImGui::SetNextItemWidth(130); ImGui::InputTextWithHint("##ni", "item id", u.itemName, sizeof(u.itemName));
        ImGui::SameLine(); ImGui::SetNextItemWidth(60); ImGui::DragInt("##nic", &u.itemCount, 0.2f, 1, 999);
        ImGui::SameLine();
        if (ImGui::SmallButton("Give") && u.itemName[0]) u.host.items[u.itemName] += u.itemCount;
        ImGui::SeparatorText("Quests");
        for (auto& q : u.host.quests) {
            ImGui::PushID(q.first.c_str());
            const char* states[] = {"inactive", "active", "completed", "failed"};
            int st = 0; for (int i = 0; i < 4; ++i) if (q.second == states[i]) st = i;
            ImGui::Text("%s", q.first.c_str()); ImGui::SameLine(140);
            ImGui::SetNextItemWidth(110);
            if (ImGui::Combo("##qs", &st, states, 4)) q.second = states[st];
            ImGui::PopID();
        }
        ImGui::SetNextItemWidth(130); ImGui::InputTextWithHint("##nq", "quest id", u.questName, sizeof(u.questName));
        ImGui::SameLine();
        if (ImGui::SmallButton("Add") && u.questName[0]) u.host.quests[u.questName] = "inactive";
        ImGui::SeparatorText("What happened");
        for (const auto& l : u.host.log) ImGui::TextDisabled("%s", l.c_str());
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// The canvas
// ---------------------------------------------------------------------------
void DrawCanvas(EditorState& ed, ImVec2 size) {
    Ui& u = U();
    DialogueGraph& g = u.graph;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::BeginChild("##dlgcanvas", size, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 org = ImGui::GetCursorScreenPos();
    const ImVec2 csz = ImGui::GetContentRegionAvail();
    const ImRect canvasRect(org, org + csz);
    const bool canvasHovered = ImGui::IsWindowHovered();
    const bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const bool keys = windowFocused && !io.WantTextInput;
    const ImVec2 mouse = io.MousePos;

    if (canvasHovered && io.MouseWheel != 0.0f && !io.KeyCtrl) {
        const ImVec2 gpt = (mouse - org - u.pan) / u.zoom;
        u.zoom = std::clamp(u.zoom * (io.MouseWheel > 0.0f ? 1.12f : 1.0f / 1.12f), 0.25f, 2.5f);
        u.pan = mouse - org - gpt * u.zoom;
    }
    auto frameAll = [&]() {
        if (g.nodes.empty()) { u.pan = ImVec2(60, 60); u.zoom = 1.0f; return; }
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (const auto& n : g.nodes) { x0 = std::min(x0, n.x); y0 = std::min(y0, n.y); x1 = std::max(x1, n.x + kNodeW); y1 = std::max(y1, n.y + NodeH(n)); }
        u.zoom = std::clamp(std::min((csz.x - 80.0f) / std::max(x1 - x0, 1.0f), (csz.y - 80.0f) / std::max(y1 - y0, 1.0f)), 0.25f, 1.25f);
        u.pan = ImVec2(csz.x * 0.5f - (x0 + x1) * 0.5f * u.zoom, csz.y * 0.5f - (y0 + y1) * 0.5f * u.zoom);
    };
    if (u.frameReq && csz.x > 10.0f) { u.frameReq = false; frameAll(); }
    const float Z = u.zoom;
    auto toScreen = [&](ImVec2 p) { return org + u.pan + p * Z; };
    auto toGraph = [&](ImVec2 s) { return (s - org - u.pan) / Z; };
    auto centreOn = [&](ImVec2 p) { u.pan = ImVec2(csz.x * 0.5f - p.x * Z, csz.y * 0.5f - p.y * Z); };

    // grid
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

    ImFont* font = ImGui::GetFont();
    const float fs = ImGui::GetFontSize() * std::clamp(Z, 0.55f, 1.3f);
    auto nodeRect = [&](const DialogueNode& n) { const ImVec2 p = toScreen(ImVec2(n.x, n.y)); return ImRect(p, p + ImVec2(kNodeW * Z, NodeH(n) * Z)); };
    auto inPort = [&](const DialogueNode& n) { return toScreen(ImVec2(n.x, n.y + kHeadH * 0.5f)); };
    auto outPort = [&](const DialogueNode& n, int i) { return toScreen(ImVec2(n.x + kNodeW, n.y + PortY(n, i))); };
    auto findMatch = [&](const DialogueNode& n) {
        if (!u.find[0]) return false;
        const std::string f = Lower(u.find);
        return Lower(n.text).find(f) != std::string::npos || Lower(n.speaker).find(f) != std::string::npos ||
               Lower(n.name).find(f) != std::string::npos || Lower(n.item).find(f) != std::string::npos;
    };

    // ---- links ----
    int hovLinkNode = -1, hovLinkPort = -1;
    float hovDist = 1e9f;
    const int testId = u.test.Active() && u.test.Node() ? u.test.Node()->id : -1;
    for (const auto& n : g.nodes) {
        for (int i = 0; i < Outputs(n); ++i) {
            const int t = n.Output(i);
            const DialogueNode* tn = g.Find(t);
            if (!tn) continue;
            const ImVec2 a = outPort(n, i), b = inPort(*tn);
            const float bend = std::max(40.0f * Z, std::fabs(b.x - a.x) * 0.4f);
            const ImVec2 p1(a.x + bend, a.y), p2(b.x - bend, b.y);
            const bool selected = u.selLinkNode == n.id && u.selLinkPort == i;
            const bool live = testId >= 0 && t == testId && (std::find(u.test.Trail().begin(), u.test.Trail().end(), n.id) != u.test.Trail().end());
            ImU32 col = selected ? Col(1.0f, 0.8f, 0.2f) : live ? Col(0.4f, 0.9f, 0.5f) : n.kind == Kind::Jump ? ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.6f) : ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.9f);
            if (canvasHovered) for (int s = 0; s <= 20; ++s) {
                const float tt = s / 20.0f;
                const ImVec2 q = ImBezierCubicCalc(a, p1, p2, b, tt);
                const float d2 = (q.x - mouse.x) * (q.x - mouse.x) + (q.y - mouse.y) * (q.y - mouse.y);
                if (d2 < 49.0f && d2 < hovDist) { hovDist = d2; hovLinkNode = n.id; hovLinkPort = i; }
            }
            const bool hovered = hovLinkNode == n.id && hovLinkPort == i;
            if (hovered) col = ImGui::GetColorU32(ImGuiCol_Text);
            dl->AddBezierCubic(a, p1, p2, b, col, (selected || live) ? 3.0f : hovered ? 2.5f : 1.8f);
            dl->AddCircleFilled(b, kPortR * std::max(Z, 0.6f) * 0.7f, col);
        }
    }
    // the link being dragged
    if (u.linkFrom >= 0) {
        if (const DialogueNode* n = g.Find(u.linkFrom)) {
            const ImVec2 a = outPort(*n, u.linkPort);
            dl->AddBezierCubic(a, ImVec2(a.x + 60.0f * Z, a.y), ImVec2(mouse.x - 60.0f * Z, mouse.y), mouse, Col(1.0f, 0.8f, 0.2f), 2.5f);
        }
    }

    // ---- nodes ----
    bool itemHovered = false;
    u.hovNode = -1;
    int ctxNode = -1;
    static int s_ctxNode = -1;
    int dropTarget = -1;
    for (std::size_t ni = 0; ni < g.nodes.size(); ++ni) {
        DialogueNode& n = g.nodes[ni];
        const ImRect r = nodeRect(n);
        if (!canvasRect.Overlaps(r)) continue;
        if (r.Contains(mouse)) { u.hovNode = n.id; if (u.linkFrom >= 0 && u.linkFrom != n.id) dropTarget = n.id; }
        ImGui::SetCursorScreenPos(r.Min);
        ImGui::PushID(n.id);
        ImGui::InvisibleButton("##node", r.GetSize());
        const bool hov = ImGui::IsItemHovered();
        itemHovered |= hov;
        static bool s_dragMoved = false;
        if (ImGui::IsItemActivated()) {
            if (io.KeyShift || io.KeyCtrl) Select(u, n.id, 2);
            else if (!IsSel(u, n.id)) Select(u, n.id);
            else { u.primary = n.id; u.selLinkNode = -1; u.selLinkPort = -1; }
            u.forceSideTab = 0;
            u.dragPushed = false; s_dragMoved = false;
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f) && u.linkFrom < 0 && u.editing != n.id) {
            if (!u.dragPushed) { PushUndo(); u.dragPushed = true; }
            const ImVec2 d = io.MouseDelta / Z;
            if (IsSel(u, n.id)) {
                for (int k : u.sel) if (DialogueNode* m = g.Find(k)) { m->x += d.x; m->y += d.y; }
            } else { n.x += d.x; n.y += d.y; }
            s_dragMoved = true;
            u.dirty = true;
        }
        if (ImGui::IsItemDeactivated() && !s_dragMoved && !(io.KeyShift || io.KeyCtrl) && u.sel.size() > 1 && IsSel(u, n.id)) Select(u, n.id);
        if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && u.linkFrom < 0 && (n.kind == Kind::Line || n.kind == Kind::Event || n.kind == Kind::Set)) {
            u.editing = n.id; u.editFocus = true;
            std::snprintf(u.editBuf, sizeof(u.editBuf), "%s", n.kind == Kind::Line ? n.text.c_str() : n.name.c_str());
        }
        if (hov && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && ImLengthSqr(mouse - u.rightDown) < 16.0f) {
            if (u.linkFrom >= 0) u.linkFrom = -1;
            else { if (!IsSel(u, n.id)) Select(u, n.id); ctxNode = n.id; }
        }
        ImGui::PopID();

        // output ports: drag a link from them
        for (int i = 0; i < Outputs(n); ++i) {
            const ImVec2 pp = outPort(n, i);
            const float pr = kPortR * std::max(Z, 0.7f);
            ImGui::SetCursorScreenPos(pp - ImVec2(pr + 3, pr + 3));
            ImGui::PushID(n.id * 16 + i);
            ImGui::InvisibleButton("##port", ImVec2(pr * 2 + 6, pr * 2 + 6));
            const bool ph = ImGui::IsItemHovered();
            itemHovered |= ph;
            if (ImGui::IsItemActivated()) { u.linkFrom = n.id; u.linkPort = i; }
            if (ph && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && n.Output(i) >= 0) { u.selLinkNode = n.id; u.selLinkPort = i; }
            ImGui::PopID();
            const bool linked = n.Output(i) >= 0;
            dl->AddCircleFilled(pp, pr, ph || (u.linkFrom == n.id && u.linkPort == i) ? Col(1.0f, 0.8f, 0.2f) : linked ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled));
            if (!linked) dl->AddCircle(pp, pr, ImGui::GetColorU32(ImGuiCol_Text), 0, 1.0f);
            if (ph) ImGui::SetTooltip("%s%s", DialogueNode::OutputName(n.kind, i), linked ? "  (drag to relink, right-click to select)" : "  (drag onto a node)");
        }

        // visuals
        const bool isSel = IsSel(u, n.id);
        const bool isStart = g.start == n.id;
        const bool isLive = n.id == testId;
        const bool found = findMatch(n);
        const int sev = u.severity.count(n.id) ? u.severity[n.id] : 0;
        const float rd = 6.0f * Z;
        dl->AddRectFilled(r.Min, r.Max, ImGui::GetColorU32(hov || isSel ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), rd);
        dl->AddRectFilled(r.Min, ImVec2(r.Max.x, r.Min.y + kHeadH * Z), KindCol(n.kind), rd, ImDrawFlags_RoundCornersTop);
        ImU32 border = isSel ? ImGui::GetColorU32(AccentCol()) : isStart ? Col(0.4f, 0.9f, 0.5f) : dropTarget == n.id ? Col(1.0f, 0.8f, 0.2f) : ImGui::GetColorU32(ImGuiCol_Border);
        dl->AddRect(r.Min, r.Max, border, rd, 0, isSel || dropTarget == n.id ? 2.5f : isStart ? 2.0f : 1.0f);
        if (found) dl->AddRect(r.Min - ImVec2(4, 4), r.Max + ImVec2(4, 4), Col(0.30f, 0.85f, 1.0f, 0.9f), rd + 2.0f, 0, 2.0f);
        if (isLive) {
            const float pulse = 0.5f + 0.5f * std::sin((float)ImGui::GetTime() * 5.0f);
            dl->AddRect(r.Min - ImVec2(3, 3), r.Max + ImVec2(3, 3), Col(0.4f, 0.9f, 0.5f, 0.3f + 0.5f * pulse), rd + 2.0f, 0, 3.0f);
        }
        // input port
        dl->AddCircleFilled(inPort(n), kPortR * std::max(Z, 0.7f), ImGui::GetColorU32(ImGuiCol_Text));
        if (isStart) {
            const ImVec2 m(r.Min.x - 1.0f, r.Min.y + kHeadH * Z * 0.5f);
            dl->AddTriangleFilled(ImVec2(m.x - 16.0f * Z, m.y - 7.0f * Z), ImVec2(m.x - 16.0f * Z, m.y + 7.0f * Z), ImVec2(m.x - 6.0f * Z, m.y), Col(0.4f, 0.9f, 0.5f));
        }
        const ImVec4 clip(r.Min.x, r.Min.y, r.Max.x - 4.0f, r.Max.y);
        const ImU32 txt = ImGui::GetColorU32(ImGuiCol_Text), sub = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const std::string title = NodeTitle(n) + "  #" + std::to_string(n.id);
        dl->AddText(font, fs, ImVec2(r.Min.x + 10 * Z, r.Min.y + 5 * Z), Col(0.05f, 0.05f, 0.08f), title.c_str(), nullptr, 0.0f, &clip);
        if (u.editing != n.id) {
            if (n.kind == Kind::Choice) {
                for (std::size_t i = 0; i < n.choices.size(); ++i) {
                    const float y = r.Min.y + (kHeadH + 6.0f + kRowH * (float)i + 3.0f) * Z;
                    std::string line = std::to_string(i + 1) + ". " + Clip(n.choices[i].text, 30);
                    if (!n.choices[i].condition.Empty()) line += "  [" + Clip(n.choices[i].condition.Text(), 18) + "]";
                    dl->AddText(font, fs * 0.85f, ImVec2(r.Min.x + 10 * Z, y), txt, line.c_str(), nullptr, 0.0f, &clip);
                }
            } else if (n.kind == Kind::Condition) {
                dl->AddText(font, fs * 0.85f, ImVec2(r.Min.x + 10 * Z, r.Min.y + (kHeadH + 6.0f + 3.0f) * Z), txt, Clip(NodeBody(g, n), 32).c_str(), nullptr, 0.0f, &clip);
                dl->AddText(font, fs * 0.8f, ImVec2(r.Max.x - 44 * Z, r.Min.y + (kHeadH + 6.0f + 3.0f) * Z), sub, "true");
                dl->AddText(font, fs * 0.8f, ImVec2(r.Max.x - 44 * Z, r.Min.y + (kHeadH + 6.0f + kRowH + 3.0f) * Z), sub, "false");
            } else if (n.kind != Kind::End) {
                const std::string body = NodeBody(g, n);
                if (n.kind == Kind::Line) {
                    // two lines of the text, word-wrapped to the node
                    const float maxW = (kNodeW - 20.0f) * Z;
                    std::string l1, l2, rest = body;
                    for (auto& c : rest) if (c == '\n') c = ' ';
                    auto fit = [&](std::string& out, std::string& src) {
                        std::string acc;
                        while (!src.empty()) {
                            const std::size_t sp = src.find(' ');
                            const std::string w = sp == std::string::npos ? src : src.substr(0, sp);
                            const std::string trial = acc.empty() ? w : acc + " " + w;
                            if (!acc.empty() && font->CalcTextSizeA(fs * 0.85f, FLT_MAX, 0.0f, trial.c_str()).x > maxW) break;
                            acc = trial;
                            src = sp == std::string::npos ? std::string() : src.substr(sp + 1);
                        }
                        out = acc;
                    };
                    fit(l1, rest); fit(l2, rest);
                    if (!rest.empty()) l2 += "\xE2\x80\xA6";
                    dl->AddText(font, fs * 0.85f, ImVec2(r.Min.x + 10 * Z, r.Min.y + (kHeadH + 6.0f + 2.0f) * Z), txt, l1.c_str(), nullptr, 0.0f, &clip);
                    dl->AddText(font, fs * 0.85f, ImVec2(r.Min.x + 10 * Z, r.Min.y + (kHeadH + 6.0f + kRowH + 2.0f) * Z), txt, l2.c_str(), nullptr, 0.0f, &clip);
                    std::string meta;
                    if (!n.portrait.empty()) meta += "portrait ";
                    if (!n.sound.empty()) meta += "sound ";
                    if (n.autoAdvance > 0.0f) meta += "auto " + std::to_string((int)std::lround(n.autoAdvance)) + "s";
                    if (!meta.empty()) dl->AddText(font, fs * 0.75f, ImVec2(r.Min.x + 10 * Z, r.Min.y + (kHeadH + 6.0f + kRowH * 2.0f + 2.0f) * Z), sub, meta.c_str(), nullptr, 0.0f, &clip);
                } else {
                    dl->AddText(font, fs * 0.85f, ImVec2(r.Min.x + 10 * Z, r.Min.y + (kHeadH + 6.0f + 3.0f) * Z), txt, Clip(body, 34).c_str(), nullptr, 0.0f, &clip);
                }
            }
        }
        if (sev > 0) {
            const ImVec2 gc(r.Max.x - 10.0f * Z, r.Min.y + kHeadH * Z * 0.5f);
            DrawWarnGlyph(dl, gc, 6.0f * std::max(Z, 0.7f), sev >= 2 ? ErrCol() : WarnCol());
            if (hov && ImLengthSqr(mouse - gc) < 100.0f) {
                std::string tip;
                for (const auto& i : u.issues) if (i.node == n.id) { if (!tip.empty()) tip += "\n"; tip += i.text; }
                ImGui::SetTooltip("%s", tip.c_str());
            }
        }
        // inline edit of a line's text (or an event's / set's name)
        if (u.editing == n.id) {
            ImGui::SetCursorScreenPos(ImVec2(r.Min.x + 6 * Z, r.Min.y + (kHeadH + 4.0f) * Z));
            ImGui::SetNextItemWidth(kNodeW * Z - 12 * Z);
            if (u.editFocus) { ImGui::SetKeyboardFocusHere(); u.editFocus = false; }
            ImGui::PushID(n.id);
            bool enter = false, done = false;
            if (n.kind == Kind::Line) {
                ImGui::InputTextMultiline("##inline", u.editBuf, sizeof(u.editBuf), ImVec2(kNodeW * Z - 12 * Z, (NodeH(n) - kHeadH - 8.0f) * Z), ImGuiInputTextFlags_CtrlEnterForNewLine);
                done = ImGui::IsItemDeactivated();
                enter = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !io.KeyCtrl;
            } else {
                enter = ImGui::InputText("##inline", u.editBuf, sizeof(u.editBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                done = ImGui::IsItemDeactivated();
            }
            itemHovered |= ImGui::IsItemHovered();
            ImGui::PopID();
            if (enter || done) {
                std::string nn = u.editBuf;
                if (enter && n.kind == Kind::Line) while (!nn.empty() && (nn.back() == '\n' || nn.back() == '\r')) nn.pop_back();
                std::string& target = n.kind == Kind::Line ? n.text : n.name;
                if (!ImGui::IsKeyPressed(ImGuiKey_Escape) && nn != target) { PushUndo(); target = nn; Changed(ed); }
                u.editing = -1;
            }
        }
    }

    // ---- finish a link drag ----
    if (u.linkFrom >= 0 && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        DialogueNode* from = g.Find(u.linkFrom);
        if (from && dropTarget >= 0) {
            if (from->Output(u.linkPort) != dropTarget) { PushUndo(); from->SetOutput(u.linkPort, dropTarget); Changed(ed); }
        } else if (from && canvasHovered) {
            // dropped on empty space: add a node there, linked
            u.newLinkFrom = u.linkFrom; u.newLinkPort = u.linkPort; u.newLinkAt = toGraph(mouse);
            ImGui::OpenPopup("##dlgnewlinked");
        }
        u.linkFrom = -1; u.linkPort = -1;
    }
    if (ImGui::BeginPopup("##dlgnewlinked")) {
        ImGui::TextDisabled("Add and link");
        for (int k = 0; k < kKinds; ++k) {
            if (ImGui::MenuItem(DialogueNode::KindLabel((Kind)k))) {
                const int id = AddNode(ed, (Kind)k, u.newLinkAt);
                if (DialogueNode* from = g.Find(u.newLinkFrom)) { from->SetOutput(u.newLinkPort, id); Changed(ed); }
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Unlink")) { if (DialogueNode* from = g.Find(u.newLinkFrom)) { PushUndo(); from->SetOutput(u.newLinkPort, -1); Changed(ed); } }
        ImGui::EndPopup();
    }

    // ---- node context menu ----
    if (ctxNode >= 0) { s_ctxNode = ctxNode; ImGui::OpenPopup("##dlgnodectx"); }
    if (ImGui::BeginPopup("##dlgnodectx")) {
        DialogueNode* n = g.Find(s_ctxNode);
        if (!n) ImGui::CloseCurrentPopup();
        else {
            ImGui::TextDisabled("%s #%d", DialogueNode::KindLabel(n->kind), n->id);
            if (ImGui::MenuItem("Set as start node", nullptr, g.start == n->id)) { PushUndo(); g.start = n->id; Changed(ed); }
            if (n->kind == Kind::Line && ImGui::MenuItem("Edit text", "double-click")) { u.editing = n->id; u.editFocus = true; std::snprintf(u.editBuf, sizeof(u.editBuf), "%s", n->text.c_str()); }
            if (ImGui::BeginMenu("Change kind")) {
                for (int k = 0; k < kKinds; ++k)
                    if (ImGui::MenuItem(DialogueNode::KindLabel((Kind)k), nullptr, (int)n->kind == k) && (int)n->kind != k) {
                        PushUndo(); n->kind = (Kind)k;
                        if (n->kind == Kind::Choice && n->choices.size() < 2) n->choices = {DialogueChoice{"Yes", {}, -1}, DialogueChoice{"No", {}, -1}};
                        Changed(ed);
                    }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            const std::string what = u.sel.size() > 1 && IsSel(u, n->id) ? std::to_string(u.sel.size()) + " nodes" : "node";
            if (ImGui::MenuItem(("Copy " + what).c_str(), "Ctrl+C")) {
                u.clipboard.clear();
                if (IsSel(u, n->id)) for (int k : u.sel) { if (const DialogueNode* m = g.Find(k)) u.clipboard.push_back(*m); } else u.clipboard.push_back(*n);
            }
            if (ImGui::MenuItem(("Duplicate " + what).c_str(), "Ctrl+D")) {
                std::vector<DialogueNode> src;
                if (IsSel(u, n->id)) for (int k : u.sel) { if (const DialogueNode* m = g.Find(k)) src.push_back(*m); } else src.push_back(*n);
                PasteNodes(ed, src, nullptr);
            }
            if (ImGui::MenuItem(("Delete " + what).c_str(), "Del")) DeleteNodes(ed, IsSel(u, n->id) ? u.sel : std::vector<int>{n->id});
        }
        ImGui::EndPopup();
    }

    // ---- minimap ----
    const bool spaceHeld = keys && ImGui::IsKeyDown(ImGuiKey_Space);
    if (u.minimap && csz.x > 360.0f && csz.y > 220.0f && !g.nodes.empty()) {
        const ImVec2 msz(170.0f, 110.0f);
        const ImVec2 m0(org.x + csz.x - msz.x - 10.0f, org.y + csz.y - msz.y - 10.0f);
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (const auto& n : g.nodes) { x0 = std::min(x0, n.x); y0 = std::min(y0, n.y); x1 = std::max(x1, n.x + kNodeW); y1 = std::max(y1, n.y + NodeH(n)); }
        const ImVec2 v0 = toGraph(org), v1 = toGraph(org + csz);
        x0 = std::min(x0, v0.x); y0 = std::min(y0, v0.y); x1 = std::max(x1, v1.x); y1 = std::max(y1, v1.y);
        const float s = std::min((msz.x - 10.0f) / std::max(x1 - x0, 1.0f), (msz.y - 10.0f) / std::max(y1 - y0, 1.0f));
        const ImVec2 off(m0.x + (msz.x - (x1 - x0) * s) * 0.5f, m0.y + (msz.y - (y1 - y0) * s) * 0.5f);
        auto toMap = [&](ImVec2 p) { return ImVec2(off.x + (p.x - x0) * s, off.y + (p.y - y0) * s); };
        ImGui::SetCursorScreenPos(m0);
        ImGui::InvisibleButton("##dlgminimap", msz);
        const bool mhov = ImGui::IsItemHovered();
        itemHovered |= mhov;
        if (ImGui::IsItemActive()) centreOn(ImVec2((mouse.x - off.x) / s + x0, (mouse.y - off.y) / s + y0));
        dl->AddRectFilled(m0, m0 + msz, ImGui::GetColorU32(ImGuiCol_PopupBg, mhov ? 0.95f : 0.8f), 4.0f);
        dl->AddRect(m0, m0 + msz, ImGui::GetColorU32(ImGuiCol_Border), 4.0f);
        for (const auto& n : g.nodes)
            dl->AddRectFilled(toMap(ImVec2(n.x, n.y)), toMap(ImVec2(n.x + kNodeW, n.y + NodeH(n))), IsSel(u, n.id) ? ImGui::GetColorU32(AccentCol()) : KindCol(n.kind), 1.0f);
        dl->AddRect(toMap(v0), toMap(v1), ImGui::GetColorU32(ImGuiCol_Text, 0.8f), 1.0f);
        if (mhov) ImGui::SetTooltip("Minimap: click or drag to move the view");
    }

    // ---- canvas-level input ----
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) u.rightDown = mouse;
    const bool empty = canvasHovered && !itemHovered && u.hovNode < 0;
    if (empty && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !spaceHeld && u.linkFrom < 0) {
        if (hovLinkNode >= 0) { ClearSel(u); u.selLinkNode = hovLinkNode; u.selLinkPort = hovLinkPort; }
        else { u.boxing = true; u.boxStart = mouse; }
    }
    if (empty && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hovLinkNode < 0 && !spaceHeld) {
        u.boxing = false;
        AddNode(ed, Kind::Line, toGraph(mouse) - ImVec2(kNodeW * 0.5f, 30.0f));
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
                for (const auto& n : g.nodes) if (box.Overlaps(nodeRect(n))) Select(u, n.id, 1);
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
    if (empty && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && ImLengthSqr(mouse - u.rightDown) < 16.0f) {
        if (u.linkFrom >= 0) u.linkFrom = -1; else ImGui::OpenPopup("##dlgcanvasctx");
    }
    if (ImGui::BeginPopup("##dlgcanvasctx")) {
        const ImVec2 at = toGraph(ImGui::GetMousePosOnOpeningCurrentPopup());
        if (ImGui::BeginMenu("Add node")) {
            for (int k = 0; k < kKinds; ++k) if (ImGui::MenuItem(DialogueNode::KindLabel((Kind)k))) AddNode(ed, (Kind)k, at);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Paste", "Ctrl+V", false, !u.clipboard.empty())) PasteNodes(ed, u.clipboard, &at);
        ImGui::Separator();
        if (ImGui::MenuItem("Select all", "Ctrl+A")) { ClearSel(u); for (const auto& n : g.nodes) Select(u, n.id, 1); }
        if (ImGui::MenuItem("Frame all", "F")) u.frameReq = true;
        if (ImGui::MenuItem("Reset zoom", "Ctrl+0")) { const ImVec2 c = toGraph(org + csz * 0.5f); u.zoom = 1.0f; centreOn(c); }
        ImGui::EndPopup();
    }

    // ---- keyboard ----
    if (keys) {
        const bool ctrl = io.KeyCtrl;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            if (u.linkFrom >= 0) u.linkFrom = -1;
            else if (u.editing >= 0) u.editing = -1;
            else ClearSel(u);
        }
        if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) u.frameReq = true;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_0, false)) { const ImVec2 c = toGraph(org + csz * 0.5f); u.zoom = 1.0f; centreOn(c); }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) { ClearSel(u); for (const auto& n : g.nodes) Select(u, n.id, 1); }
        if (ctrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) Undo(ed);
        if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_Y, false) || (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)))) Redo(ed);
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) SaveGraphFile(ed, false);
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && !u.sel.empty()) {
            u.clipboard.clear();
            for (int k : u.sel) if (const DialogueNode* m = g.Find(k)) u.clipboard.push_back(*m);
        }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false) && !u.clipboard.empty()) {
            const ImVec2 at = canvasHovered ? toGraph(mouse) : toGraph(org + csz * 0.5f);
            PasteNodes(ed, u.clipboard, &at);
        }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false) && !u.sel.empty()) {
            std::vector<DialogueNode> src;
            for (int k : u.sel) if (const DialogueNode* m = g.Find(k)) src.push_back(*m);
            PasteNodes(ed, src, nullptr);
        }
        if ((ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) && u.editing < 0) {
            if (!u.sel.empty()) DeleteNodes(ed, u.sel);
            else if (u.selLinkNode >= 0) {
                if (DialogueNode* n = g.Find(u.selLinkNode)) { PushUndo(); n->SetOutput(u.selLinkPort, -1); Changed(ed); }
                u.selLinkNode = -1; u.selLinkPort = -1;
            }
        }
        const float step = io.KeyShift ? 25.0f : 1.0f;
        ImVec2 nudge(0, 0);
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) nudge.x -= step;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) nudge.x += step;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) nudge.y -= step;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) nudge.y += step;
        if ((nudge.x != 0.0f || nudge.y != 0.0f) && !u.sel.empty() && u.editing < 0) {
            PushUndo();
            for (int k : u.sel) if (DialogueNode* m = g.Find(k)) { m->x += nudge.x; m->y += nudge.y; }
            u.dirty = true;
        }
    }

    // ---- status line ----
    {
        std::string s;
        if (u.linkFrom >= 0) s = "Drop the link on a node (or on empty space to add one there); Esc cancels";
        else if (u.test.Active()) s = "Testing: the highlighted node is where the conversation is";
        else if (u.sel.size() > 1) s = std::to_string(u.sel.size()) + " nodes selected  \xC2\xB7  drag to move, Delete, Ctrl+C / Ctrl+D";
        else s = "drag: move  \xC2\xB7  drag a port: link  \xC2\xB7  drag empty: box select  \xC2\xB7  wheel: zoom  \xC2\xB7  middle / right / Space drag: pan  \xC2\xB7  double-click: edit / add";
        const float ofs = ImGui::GetFontSize() * 0.92f;
        const ImVec2 ts = font->CalcTextSizeA(ofs, FLT_MAX, 0.0f, s.c_str());
        const ImVec2 at(org.x + 8.0f, org.y + csz.y - ts.y - 6.0f);
        dl->AddRectFilled(at - ImVec2(4, 2), at + ts + ImVec2(4, 2), ImGui::GetColorU32(ImGuiCol_WindowBg, 0.75f), 3.0f);
        dl->AddText(font, ofs, at, ImGui::GetColorU32(ImGuiCol_TextDisabled), s.c_str());
        const std::string zs = std::to_string((int)std::lround(u.zoom * 100.0f)) + "%";
        const ImVec2 zts = font->CalcTextSizeA(ofs, FLT_MAX, 0.0f, zs.c_str());
        dl->AddText(font, ofs, ImVec2(org.x + csz.x - zts.x - 10.0f, org.y + 6.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), zs.c_str());
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// The Dialogue tab (toolbar + palette + canvas + side panel)
// ---------------------------------------------------------------------------
void DrawDialogueTab(EditorState& ed) {
    Ui& u = U();
    // toolbar
    if (ImGui::Button("New")) NewGraph(ed);
    ImGui::SameLine();
    if (ImGui::Button("Open...")) {
        const char* filters[] = {"*.okaydialog"};
        if (const char* p = tinyfd_openFileDialog("Open dialogue", AssetsDir(ed).c_str(), 1, filters, "Dialogue graph (.okaydialog)", 0)) LoadGraphFile(ed, p);
    }
    ImGui::SameLine();
    if (ImGui::Button(u.dirty ? "Save*" : "Save")) SaveGraphFile(ed, false);
    Tip("Ctrl+S");
    ImGui::SameLine();
    if (ImGui::Button("Save As...")) SaveGraphFile(ed, true);
    ImGui::SameLine();
    ScanFiles(ed);
    ImGui::SetNextItemWidth(220);
    if (ImGui::BeginCombo("##dlgfiles", u.path.empty() ? (u.graph.name.empty() ? "(unsaved graph)" : u.graph.name.c_str()) : fs::path(u.path).filename().string().c_str())) {
        if (u.dialogFiles.empty()) ImGui::TextDisabled("No .okaydialog files in the project yet.");
        for (const auto& f : u.dialogFiles)
            if (ImGui::Selectable(ProjectRelative(ed, f).c_str(), f == u.path)) LoadGraphFile(ed, f);
        ImGui::EndCombo();
    }
    Tip("The project's dialogue files.");
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    ImGui::InputTextWithHint("##dlgfind", "find", u.find, sizeof(u.find));
    ImGui::SameLine();
    int errs = 0, warns = 0;
    for (const auto& i : u.issues) (i.severity >= 2 ? errs : warns)++;
    if (errs || warns) {
        ImGui::PushStyleColor(ImGuiCol_Text, errs ? ImVec4(0.95f, 0.4f, 0.35f, 1) : ImVec4(0.95f, 0.75f, 0.3f, 1));
        if (ImGui::SmallButton((std::to_string(errs) + " errors, " + std::to_string(warns) + " warnings").c_str())) u.forceSideTab = 2;
        ImGui::PopStyleColor();
    } else ImGui::TextColored(ImVec4(0.5f, 0.85f, 0.55f, 1), "valid");
    ImGui::SameLine();
    if (ImGui::SmallButton(u.test.Active() ? "Testing..." : "Test")) { u.forceSideTab = 3; TestStart(); }
    Tip("Play the graph here, with the Test panel's own story variables, items and quests.");
    // The palette on its own row, so a narrow window never hides it.
    ImGui::TextDisabled("Add:");
    for (int k = 0; k < kKinds; ++k) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(KindCol((Kind)k)) * ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
        if (ImGui::SmallButton(DialogueNode::KindLabel((Kind)k))) {
            float x = 80.0f, y = 80.0f;
            for (const auto& n : u.graph.nodes) { x = std::max(x, n.x + kNodeW + 40.0f); y = std::max(y, n.y); }
            AddNode(ed, (Kind)k, ImVec2(x, y));
        }
        ImGui::PopStyleColor();
    }

    // canvas + side panel
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float sideW = std::clamp(u.sideW, 260.0f, std::max(260.0f, avail.x - 300.0f));
    DrawCanvas(ed, ImVec2(avail.x - sideW - 6.0f, avail.y));
    ImGui::SameLine();
    ImGui::InvisibleButton("##dlgsplit", ImVec2(6.0f, avail.y));
    if (ImGui::IsItemActive()) u.sideW = std::clamp(sideW - ImGui::GetIO().MouseDelta.x, 260.0f, 800.0f);
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    ImGui::SameLine();
    ImGui::BeginChild("##dlgside", ImVec2(sideW, avail.y), ImGuiChildFlags_Borders);
    if (ImGui::BeginTabBar("##dlgsidetabs")) {
        const char* names[] = {"Node", "Graph", "Problems", "Test"};
        for (int t = 0; t < 4; ++t) {
            const ImGuiTabItemFlags fl = u.forceSideTab == t ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(names[t], nullptr, fl)) {
                u.sideTab = t;
                ImGui::BeginChild("##dlgsidebody");
                if (t == 0) {
                    DialogueNode* n = u.graph.Find(u.primary);
                    if (n) DrawNodeInspector(ed, *n);
                    else {
                        ImGui::TextDisabled("Select a node to edit it.");
                        ImGui::TextWrapped("Double-click the canvas to add a line, or use the Add buttons. Drag a node's right-hand port onto another node to link them.");
                    }
                } else if (t == 1) DrawGraphTab(ed);
                else if (t == 2) DrawProblems(ed);
                else DrawTest(ed);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    u.forceSideTab = -1;
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Quests: a shared definition editor (the Quests tab and the Quest Log inspector)
// ---------------------------------------------------------------------------
// Returns true when anything changed; `before` runs once before the first change of a widget.
bool QuestDefEditor(EditorState& ed, QuestDef& q, const std::vector<std::string>& ids, const std::function<void()>& before) {
    bool changed = false;
    auto act = [&]() { if (ImGui::IsItemActivated()) before(); };
    if (TextEdit("Id##qid", q.id, "unique id (scripts use it)")) changed = true;
    act();
    if (TextEdit("Title##qt", q.title, "shown to the player")) changed = true;
    act();
    ImGui::TextDisabled("Description");
    if (TextEditMulti("##qd", q.description, 60.0f)) changed = true;
    act();
    if (ImGui::Checkbox("Repeatable##qr", &q.repeatable)) { before(); changed = true; }
    Tip("Can be started again after it was completed.");
    ImGui::SeparatorText("Prerequisites");
    for (std::size_t i = 0; i < q.prerequisites.size(); ++i) {
        ImGui::PushID((int)i);
        ImGui::SetNextItemWidth(-40.0f);
        if (ImGui::BeginCombo("##pre", q.prerequisites[i].c_str())) {
            for (const auto& id : ids) if (id != q.id && ImGui::Selectable(id.c_str(), id == q.prerequisites[i])) { before(); q.prerequisites[i] = id; changed = true; }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) { before(); q.prerequisites.erase(q.prerequisites.begin() + (long)i); changed = true; ImGui::PopID(); break; }
        ImGui::PopID();
    }
    if (ImGui::SmallButton("+ Prerequisite")) {
        before();
        std::string pick;
        for (const auto& id : ids) if (id != q.id && std::find(q.prerequisites.begin(), q.prerequisites.end(), id) == q.prerequisites.end()) { pick = id; break; }
        q.prerequisites.push_back(pick);
        changed = true;
    }
    Tip("Quests that must be completed before this one can start.");
    ImGui::SeparatorText("Objectives");
    const char* kinds[] = {"Talk to", "Collect", "Kill", "Reach", "Custom"};
    for (std::size_t i = 0; i < q.objectives.size(); ++i) {
        QuestObjective& ob = q.objectives[i];
        ImGui::PushID(100 + (int)i);
        ImGui::Text("%d.", (int)i + 1);
        ImGui::SameLine();
        int kind = (int)ob.kind;
        ImGui::SetNextItemWidth(100);
        if (ImGui::Combo("##ok", &kind, kinds, 5)) { before(); ob.kind = (QuestObjective::Kind)kind; changed = true; }
        ImGui::SameLine();
        if (TextEdit("##od", ob.description, "description", -1.0f)) changed = true;
        act();
        ImGui::Indent(22.0f);
        switch (ob.kind) {
            case QuestObjective::Kind::Talk:
                if (TextEdit("NPC / graph##ot", ob.target, "object name or dialogue name")) changed = true;
                act();
                Tip("Completes when a conversation on that object (or that dialogue graph) ends, or when the graph's Complete Objective node runs.");
                break;
            case QuestObjective::Kind::Collect:
                if (ItemCombo("Item##oi", ob.target, 150)) { before(); changed = true; }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70);
                if (ImGui::DragInt("x##oc", &ob.count, 0.2f, 1, 99999)) changed = true;
                act();
                Tip("Counted from the player's inventory while the quest is active.");
                break;
            case QuestObjective::Kind::Kill:
                if (TextEdit("Tag##okt", ob.target, "tag (or name) of what dies", 150)) changed = true;
                act();
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70);
                if (ImGui::DragInt("x##okc", &ob.count, 0.2f, 1, 99999)) changed = true;
                act();
                Tip("Counts Health Stats that die with this tag.");
                break;
            case QuestObjective::Kind::Reach: {
                std::vector<std::string> zones;
                for (const auto& up : ed.scene().Objects()) if (up) if (auto* qt = up->GetComponent<QuestTrigger>()) zones.push_back(qt->ZoneName());
                ImGui::SetNextItemWidth(150);
                if (ImGui::BeginCombo("Zone##oz", ob.target.empty() ? "(zone)" : ob.target.c_str())) {
                    for (const auto& z : zones) if (ImGui::Selectable(z.c_str(), z == ob.target)) { before(); ob.target = z; changed = true; }
                    static char zbuf[64] = "";
                    ImGui::SetNextItemWidth(140);
                    if (ImGui::InputTextWithHint("##zn", "type a zone name", zbuf, sizeof(zbuf), ImGuiInputTextFlags_EnterReturnsTrue) && zbuf[0]) { before(); ob.target = zbuf; zbuf[0] = 0; changed = true; }
                    ImGui::EndCombo();
                }
                Tip("A Quest Trigger's zone name in the scene (its object name unless it sets one).");
                break;
            }
            case QuestObjective::Kind::Custom:
                ImGui::TextDisabled("Completed by a script: complete_objective(\"%s\", %d).", q.id.c_str(), (int)i);
                break;
        }
        if (ImGui::Checkbox("Optional##oo", &ob.optional)) { before(); changed = true; }
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove##orm")) { before(); q.objectives.erase(q.objectives.begin() + (long)i); changed = true; ImGui::Unindent(22.0f); ImGui::PopID(); break; }
        if (i > 0) { ImGui::SameLine(); if (ImGui::SmallButton("Up##oup")) { before(); std::swap(q.objectives[i], q.objectives[i - 1]); changed = true; } }
        ImGui::Unindent(22.0f);
        ImGui::PopID();
    }
    if (ImGui::SmallButton("+ Objective")) { before(); QuestObjective ob; ob.kind = QuestObjective::Kind::Collect; ob.description = "Objective " + std::to_string(q.objectives.size() + 1); q.objectives.push_back(ob); changed = true; }
    ImGui::SeparatorText("Rewards");
    for (std::size_t i = 0; i < q.reward.items.size(); ++i) {
        ImGui::PushID(200 + (int)i);
        if (ItemCombo("##ri", q.reward.items[i].first, 150)) { before(); changed = true; }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        if (ImGui::DragInt("x##rc", &q.reward.items[i].second, 0.2f, 1, 99999)) changed = true;
        act();
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) { before(); q.reward.items.erase(q.reward.items.begin() + (long)i); changed = true; ImGui::PopID(); break; }
        ImGui::PopID();
    }
    for (std::size_t i = 0; i < q.reward.vars.size(); ++i) {
        ImGui::PushID(300 + (int)i);
        if (TextEdit("##rv", q.reward.vars[i].first, "story variable", 120)) changed = true;
        act();
        ImGui::SameLine();
        if (TextEdit("##rvv", q.reward.vars[i].second, "value", 80)) changed = true;
        act();
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) { before(); q.reward.vars.erase(q.reward.vars.begin() + (long)i); changed = true; ImGui::PopID(); break; }
        ImGui::PopID();
    }
    if (ImGui::SmallButton("+ Item")) { before(); q.reward.items.push_back({"", 1}); changed = true; }
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Story variable")) { before(); q.reward.vars.push_back({"", "1"}); changed = true; }
    if (TextEdit("Event##re", q.reward.event, "script event on completion (optional)")) changed = true;
    act();
    Tip("Scripts get on_quest_event(name); visual scripts On Quest Event.");
    return changed;
}

void QuestPushUndo() {
    Ui& u = U();
    u.questUndo.push_back(QuestFile::Write(u.quests));
    if (u.questUndo.size() > 100) u.questUndo.erase(u.questUndo.begin());
    u.questRedo.clear();
}
void QuestChanged() {
    Ui& u = U();
    u.questDirty = true;
    u.questIssues = QuestFile::Validate(u.quests, nullptr);
}
bool LoadQuestFile(const std::string& path) {
    Ui& u = U();
    std::vector<QuestDef> qs;
    std::string err;
    if (!QuestFile::Load(path, qs, &err)) { ConsoleLog("Couldn't open " + path + ": " + err, 2); return false; }
    u.quests = qs; u.questPath = path; u.questDirty = false; u.questUndo.clear(); u.questRedo.clear();
    u.questSel = qs.empty() ? -1 : 0;
    u.questIssues = QuestFile::Validate(u.quests, nullptr);
    return true;
}
bool SaveQuestFile(EditorState& ed, bool as) {
    Ui& u = U();
    std::string path = u.questPath;
    if (as || path.empty()) {
        const char* filters[] = {"*.okayquest"};
        const std::string def = (fs::path(AssetsDir(ed)) / "quests.okayquest").string();
        const char* p = tinyfd_saveFileDialog("Save quests", def.c_str(), 1, filters, "Quest file (.okayquest)");
        if (!p || !*p) return false;
        path = p;
        if (fs::path(path).extension() != ".okayquest") path += ".okayquest";
    }
    if (!QuestFile::Save(path, u.quests)) { ConsoleLog("Couldn't save " + path, 2); return false; }
    u.questPath = path; u.questDirty = false;
    ConsoleLog("Saved " + path);
    ScanFiles(ed, true);
    // Quest Logs in the scene pointing at this file see the new definitions at once.
    for (const auto& up : ed.scene().Objects())
        if (up) if (auto* ql = up->GetComponent<QuestLog>()) if (!ql->questFile.empty() && ql->ResolvedFile() == path) ql->LoadFile(nullptr);
    return true;
}

void DrawQuestsTab(EditorState& ed) {
    Ui& u = U();
    ScanFiles(ed);
    if (ImGui::Button("New file")) { u.quests.clear(); u.questPath.clear(); u.questDirty = false; u.questUndo.clear(); u.questRedo.clear(); u.questSel = -1; u.questIssues.clear(); }
    ImGui::SameLine();
    if (ImGui::Button("Open...")) {
        const char* filters[] = {"*.okayquest"};
        if (const char* p = tinyfd_openFileDialog("Open quests", AssetsDir(ed).c_str(), 1, filters, "Quest file (.okayquest)", 0)) LoadQuestFile(p);
    }
    ImGui::SameLine();
    if (ImGui::Button(u.questDirty ? "Save*" : "Save")) SaveQuestFile(ed, false);
    ImGui::SameLine();
    if (ImGui::Button("Save As...")) SaveQuestFile(ed, true);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(240);
    if (ImGui::BeginCombo("##qfiles", u.questPath.empty() ? "(unsaved quest file)" : fs::path(u.questPath).filename().string().c_str())) {
        if (u.questFiles.empty()) ImGui::TextDisabled("No .okayquest files in the project yet.");
        for (const auto& f : u.questFiles)
            if (ImGui::Selectable(ProjectRelative(ed, f).c_str(), f == u.questPath)) LoadQuestFile(f);
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    int errs = 0, warns = 0;
    for (const auto& i : u.questIssues) (i.severity >= 2 ? errs : warns)++;
    if (errs || warns) ImGui::TextColored(errs ? ImVec4(0.95f, 0.4f, 0.35f, 1) : ImVec4(0.95f, 0.75f, 0.3f, 1), "%d errors, %d warnings", errs, warns);
    else ImGui::TextColored(ImVec4(0.5f, 0.85f, 0.55f, 1), "valid");
    ImGui::SameLine();
    ImGui::TextDisabled("Ctrl+Z / Ctrl+Y undo and redo");

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::BeginChild("##qlist", ImVec2(260.0f, avail.y), ImGuiChildFlags_Borders);
    if (ImGui::Button("+ Quest")) {
        QuestPushUndo();
        QuestDef q; q.id = "quest" + std::to_string(u.quests.size() + 1); q.title = "New quest";
        u.quests.push_back(q); u.questSel = (int)u.quests.size() - 1; QuestChanged();
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate") && u.questSel >= 0 && u.questSel < (int)u.quests.size()) {
        QuestPushUndo();
        QuestDef q = u.quests[u.questSel]; q.id += "_copy";
        u.quests.insert(u.quests.begin() + u.questSel + 1, q); ++u.questSel; QuestChanged();
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove") && u.questSel >= 0 && u.questSel < (int)u.quests.size()) {
        QuestPushUndo();
        u.quests.erase(u.quests.begin() + u.questSel);
        if (u.questSel >= (int)u.quests.size()) u.questSel = (int)u.quests.size() - 1;
        QuestChanged();
    }
    ImGui::Separator();
    for (std::size_t i = 0; i < u.quests.size(); ++i) {
        ImGui::PushID((int)i);
        int sev = 0;
        for (const auto& is : u.questIssues) if (is.quest == u.quests[i].id) sev = std::max(sev, is.severity);
        if (sev) { ImDrawList* dl = ImGui::GetWindowDrawList(); const ImVec2 p = ImGui::GetCursorScreenPos(); DrawWarnGlyph(dl, ImVec2(p.x + 7.0f, p.y + ImGui::GetTextLineHeight() * 0.55f), 5.0f, sev >= 2 ? ErrCol() : WarnCol()); }
        ImGui::Dummy(ImVec2(16, 0)); ImGui::SameLine();
        const std::string label = (u.quests[i].title.empty() ? u.quests[i].id : u.quests[i].title) + "  (" + u.quests[i].id + ")";
        if (ImGui::Selectable(label.c_str(), (int)i == u.questSel)) u.questSel = (int)i;
        ImGui::PopID();
    }
    if (u.quests.empty()) ImGui::TextDisabled("No quests. Add one.");
    ImGui::Separator();
    ImGui::TextDisabled("Problems");
    for (const auto& is : u.questIssues) {
        ImGui::PushStyleColor(ImGuiCol_Text, is.severity >= 2 ? ImVec4(0.95f, 0.4f, 0.35f, 1) : ImVec4(0.95f, 0.75f, 0.3f, 1));
        ImGui::TextWrapped("%s: %s", is.quest.c_str(), is.text.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##qinspector", ImVec2(0, avail.y), ImGuiChildFlags_Borders);
    if (u.questSel >= 0 && u.questSel < (int)u.quests.size()) {
        std::vector<std::string> ids;
        for (const auto& q : u.quests) ids.push_back(q.id);
        static bool s_pushed = false;
        if (!ImGui::IsAnyItemActive()) s_pushed = false;
        if (QuestDefEditor(ed, u.quests[u.questSel], ids, [&]() { if (!s_pushed) { QuestPushUndo(); s_pushed = true; } })) QuestChanged();
    } else ImGui::TextDisabled("Select a quest.");
    ImGui::EndChild();
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput && io.KeyCtrl) {
        if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false) && !u.questUndo.empty()) {
            u.questRedo.push_back(QuestFile::Write(u.quests));
            std::vector<QuestDef> qs; QuestFile::Parse(u.questUndo.back(), qs); u.quests = qs; u.questUndo.pop_back(); QuestChanged();
        }
        if ((ImGui::IsKeyPressed(ImGuiKey_Y, false) || (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false))) && !u.questRedo.empty()) {
            u.questUndo.push_back(QuestFile::Write(u.quests));
            std::vector<QuestDef> qs; QuestFile::Parse(u.questRedo.back(), qs); u.quests = qs; u.questRedo.pop_back(); QuestChanged();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_S, false)) SaveQuestFile(ed, false);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Public: the window
// ---------------------------------------------------------------------------
bool DialogueWindowFocused() { return g_focused; }

void OpenStoryAsset(const std::string& path) {
    Ui& u = U();
    u.pendingOpen = path;
    g_showDialogue = true;
}

// Dev screenshots (OKAY_UI_DIALOGUE_PLAY=<graph>, OKAY_UI_QUEST_FILE=<quests>, with
// OKAY_UI_PLAY=1): a Dialogue Runner goes on the template's crate and a Quest Log
// in the scene before Play; once playing, the conversation starts and advances to
// its first choice, a quest is tracked, and OKAY_UI_JOURNAL=1 opens the journal.
void PlayDevHooks(EditorState& ed) {
    const char* graph = std::getenv("OKAY_UI_DIALOGUE_PLAY");
    if (!graph || !*graph) return;
    static int phase = 0;
    static double t0 = 0.0;
    if (phase == 0 && !ed.isPlaying()) {
        GameObject* npc = ed.scene().Find("Carry Crate");
        if (!npc) npc = ed.scene().Find("Crate");
        if (!npc) return;
        auto* dr = npc->AddComponent<DialogueRunner>();
        dr->graphFile = graph;
        dr->typewriterSpeed = 0.0f;
        QuestLog* ql = QuestLog::Get(&ed.scene(), true);
        if (const char* q = std::getenv("OKAY_UI_QUEST_FILE")) ql->questFile = q;
        StoryState::Get(&ed.scene(), true)->Set("gold", vs::VsValue(20.0f));
        phase = 1;
        return;
    }
    if (phase == 1 && ed.isPlaying()) { phase = 2; t0 = ImGui::GetTime(); return; }
    if (phase < 2 || !ed.isPlaying()) return;
    const double t = ImGui::GetTime() - t0;
    DialogueRunner* dr = DialogueRunner::FindByName(&ed.scene(), "Carry Crate");
    if (!dr) dr = DialogueRunner::FindByName(&ed.scene(), "Crate");
    QuestLog* ql = QuestLog::Get(&ed.scene(), false);
    if (phase == 2 && t > 3.0) { if (ql) ql->StartQuest("visit"); if (dr) dr->StartDialogue(); phase = 3; }
    else if (phase == 3 && t > 6.0 && dr && dr->IsActive()) { dr->Advance(); phase = 4; }
    else if (phase == 4 && t > 9.0 && dr && dr->IsActive()) { if (!dr->CurrentLine().choice) dr->Advance(); phase = 5; }
    else if (phase == 5 && t > 40.0 && ql && std::getenv("OKAY_UI_JOURNAL")) { if (dr) dr->Stop(); ql->StartQuest("gather"); ql->journalOpen = true; phase = 6; }
}

void DrawDialogueWindow(EditorState& ed) {
    g_focused = false;
    PlayDevHooks(ed);
    if (!g_showDialogue) return;
    Ui& u = U();
    static bool s_init = false;
    if (!s_init) { s_init = true; NewGraph(ed); }
    if (!u.pendingOpen.empty()) {
        const std::string p = u.pendingOpen;
        u.pendingOpen.clear();
        if (Lower(fs::path(p).extension().string()) == ".okayquest") { if (LoadQuestFile(p)) u.tab = 1; }
        else if (LoadGraphFile(ed, p)) u.tab = 0;
        if (const char* sel = std::getenv("OKAY_UI_DIALOGUE_SELECT"); sel && u.tab == 0) {   // dev screenshots: a node's inspector
            if (u.graph.Find(std::atoi(sel))) { Select(u, std::atoi(sel)); u.forceSideTab = 0; }
        }
        if (u.tab == 0 && std::getenv("OKAY_UI_DIALOGUE_TEST")) {   // dev screenshots: the Test panel at the first choice
            u.forceSideTab = 3;
            u.host.vars["gold"] = vs::VsValue(20.0f);
            u.host.items["gold"] = 20;
            TestStart();
            for (int i = 0; i < 10 && u.test.Active() && !u.test.AtChoice(); ++i) { u.test.Advance(); TestPresent(); }
        }
    }
    if (const char* f = std::getenv("OKAY_UI_DIALOGUE_FILE"); f && *f) {   // dev screenshots: open a file once
        static bool once = false;
        if (!once) { once = true; OpenStoryAsset(f); }
    }
    if (std::getenv("OKAY_UI_DIALOGUE")) {
        ImGui::SetNextWindowPos(ImVec2(24, 48), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(1400, 800), ImGuiCond_Always);
    } else ImGui::SetNextWindowSize(ImVec2(1200, 700), ImGuiCond_FirstUseEver);
    const std::string title = std::string("Dialogue") + (u.dirty || u.questDirty ? "*" : "") + "###Dialogue";
    if (!ImGui::Begin(title.c_str(), &g_showDialogue)) { ImGui::End(); return; }
    g_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (ImGui::BeginTabBar("##dlgtabs")) {
        const bool forceQ = std::getenv("OKAY_UI_DIALOGUE_TAB") && std::atoi(std::getenv("OKAY_UI_DIALOGUE_TAB")) == 1;
        static int s_want = -1;
        if (s_want < 0) s_want = forceQ ? 1 : u.tab;
        if (ImGui::BeginTabItem("Dialogue", nullptr, s_want == 0 ? ImGuiTabItemFlags_SetSelected : 0)) { u.tab = 0; DrawDialogueTab(ed); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Quests", nullptr, s_want == 1 ? ImGuiTabItemFlags_SetSelected : 0)) { u.tab = 1; DrawQuestsTab(ed); ImGui::EndTabItem(); }
        s_want = -2;   // only the first frame (and an OpenStoryAsset) picks the tab
        if (!u.pendingOpen.empty()) s_want = -1;
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Public: component inspectors
// ---------------------------------------------------------------------------
void DialogueRunnerInspector(EditorState& ed, DialogueRunner& dr) {
    ImGui::TextDisabled("Talk to this object: E in range (like the pickups), or a script's start_dialogue().");
    if (TextEdit("Graph##drf", dr.graphFile, "Assets/....okaydialog")) ed.dirty = true;
    if (ImGui::IsItemActivated()) ed.PushUndo();
    ImGui::SameLine();
    if (ImGui::SmallButton("...##drb")) {
        const char* f[] = {"*.okaydialog"};
        if (const char* p = tinyfd_openFileDialog("Dialogue graph", AssetsDir(ed).c_str(), 1, f, "Dialogue graph (.okaydialog)", 0)) { ed.PushUndo(); dr.graphFile = ProjectRelative(ed, p); ed.dirty = true; }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Edit##dre")) {
        if (dr.graphFile.empty()) { g_showDialogue = true; }
        else OpenStoryAsset(dr.ResolvedGraphFile());
    }
    Tip("Open it in the Dialogue window (View > Editors & Tools > Dialogue & Quests).");
    {
        std::error_code ec;
        if (!dr.graphFile.empty() && !fs::exists(dr.ResolvedGraphFile(), ec)) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "File not found.");
    }
    const char* modes[] = {"Interact key in range", "Scripts only"};
    if (ImGui::Combo("Starts##drm", &dr.startMode, modes, 2)) ed.dirty = true;
    if (ImGui::IsItemActivated()) ed.PushUndo();
    if (dr.startMode == 0) {
        char k[2] = {dr.interactKey, 0};
        ImGui::SetNextItemWidth(60);
        if (ImGui::InputText("Key##drk", k, sizeof(k))) { ed.PushUndo(); dr.interactKey = k[0]; ed.dirty = true; }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        if (ImGui::DragFloat("Range##drr", &dr.interactRange, 0.05f, 0.1f, 50.0f, "%.1f m")) ed.dirty = true;
        if (ImGui::IsItemActivated()) ed.PushUndo();
        if (TextEdit("Prompt##drp", dr.promptText, "(empty = \"E to talk\")")) ed.dirty = true;
        if (ImGui::IsItemActivated()) ed.PushUndo();
        Tip("Shown on the Hold Prompt line while in range.");
    }
    if (ImGui::Checkbox("Pause player##drpp", &dr.pausePlayer)) { ed.PushUndo(); ed.dirty = true; }
    Tip("Controllers stop looking and moving while the conversation runs; the cursor is free for the choices.");
    if (ImGui::DragFloat("Typewriter (chars/s)##drt", &dr.typewriterSpeed, 1.0f, 0.0f, 500.0f, "%.0f")) ed.dirty = true;
    if (ImGui::IsItemActivated()) ed.PushUndo();
    Tip("0 shows each line at once.");
    if (ImGui::DragFloat("Auto-advance (s)##dra", &dr.autoAdvance, 0.05f, 0.0f, 60.0f, "%.2f")) ed.dirty = true;
    if (ImGui::IsItemActivated()) ed.PushUndo();
    Tip("Lines without their own timing move on after this many seconds (0 = wait for confirm).");
    if (ImGui::Checkbox("Built-in dialogue box##drui", &dr.drawUI)) { ed.PushUndo(); ed.dirty = true; }
    Tip("Off: draw your own from CurrentLine() and the on_dialogue_* events.");
    if (ImGui::Checkbox("Escape cancels##drc", &dr.canCancel)) { ed.PushUndo(); ed.dirty = true; }
    if (ImGui::DragInt("Start node##drs", &dr.startNode, 0.2f, -1, 9999)) ed.dirty = true;
    if (ImGui::IsItemActivated()) ed.PushUndo();
    Tip("-1 uses the graph's start node.");
    if (TextEdit("Player##drpl", dr.playerName, "(empty = the player)")) ed.dirty = true;
    if (ImGui::IsItemActivated()) ed.PushUndo();
    if (ed.isPlaying()) {
        ImGui::Separator();
        if (dr.IsActive()) {
            const DialogueRunner::Line l = dr.CurrentLine();
            ImGui::TextColored(ImVec4(0.5f, 0.85f, 0.55f, 1), "Talking: node #%d", l.nodeId);
            ImGui::TextWrapped("%s: %s", l.speaker.c_str(), l.text.c_str());
            if (ImGui::SmallButton("Advance")) dr.Advance();
            ImGui::SameLine();
            if (ImGui::SmallButton("Stop")) dr.Stop();
        } else if (ImGui::SmallButton("Start conversation")) dr.StartDialogue();
    }
}

void QuestLogInspector(EditorState& ed, QuestLog& ql) {
    ImGui::TextDisabled("One per scene: quest states and progress (saved with the game).");
    if (TextEdit("Quest file##qlf", ql.questFile, "Assets/....okayquest (optional)")) ed.dirty = true;
    if (ImGui::IsItemActivated()) ed.PushUndo();
    ImGui::SameLine();
    if (ImGui::SmallButton("...##qlb")) {
        const char* f[] = {"*.okayquest"};
        if (const char* p = tinyfd_openFileDialog("Quest file", AssetsDir(ed).c_str(), 1, f, "Quest file (.okayquest)", 0)) { ed.PushUndo(); ql.questFile = ProjectRelative(ed, p); ql.LoadFile(nullptr); ed.dirty = true; }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Edit##qle")) { if (ql.questFile.empty()) g_showDialogue = true; else OpenStoryAsset(ql.ResolvedFile()); }
    Tip("Open it in the Dialogue window's Quests tab.");
    {
        std::error_code ec;
        if (!ql.questFile.empty() && !fs::exists(ql.ResolvedFile(), ec)) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "File not found.");
    }
    if (ImGui::Checkbox("Tracker overlay##qlt", &ql.showTracker)) { ed.PushUndo(); ed.dirty = true; }
    ImGui::SameLine();
    if (ImGui::Checkbox("Notifications##qln", &ql.showNotifications)) { ed.PushUndo(); ed.dirty = true; }
    if (ImGui::Checkbox("Journal##qlj", &ql.allowJournal)) { ed.PushUndo(); ed.dirty = true; }
    ImGui::SameLine();
    {
        char k[2] = {ql.journalKey, 0};
        ImGui::SetNextItemWidth(50);
        if (ImGui::InputText("key##qlk", k, sizeof(k))) { ed.PushUndo(); ql.journalKey = k[0]; ed.dirty = true; }
    }
    if (ImGui::Checkbox("Built-in overlays##qlui", &ql.drawUI)) { ed.PushUndo(); ed.dirty = true; }
    Tip("Off: draw your own tracker / journal from the records and the on_quest_* events.");
    if (TextEdit("Tracked##qltr", ql.tracked, "(empty = every active quest)")) ed.dirty = true;
    if (ImGui::IsItemActivated()) ed.PushUndo();
    if (TextEdit("Player##qlp", ql.playerName, "(empty = the player)")) ed.dirty = true;
    if (ImGui::IsItemActivated()) ed.PushUndo();
    ImGui::SeparatorText("Quests kept in the scene");
    ImGui::TextDisabled("%d from the file, %d inline.", (int)ql.Defs().size() - (int)ql.quests.size(), (int)ql.quests.size());
    static int s_sel = -1;
    for (std::size_t i = 0; i < ql.quests.size(); ++i) {
        ImGui::PushID((int)i);
        const std::string label = (ql.quests[i].title.empty() ? ql.quests[i].id : ql.quests[i].title) + "  (" + ql.quests[i].id + ")";
        if (ImGui::Selectable(label.c_str(), (int)i == s_sel)) s_sel = (int)i;
        ImGui::PopID();
    }
    if (ImGui::SmallButton("+ Inline quest")) { ed.PushUndo(); QuestDef q; q.id = "quest" + std::to_string(ql.quests.size() + 1); q.title = "New quest"; ql.quests.push_back(q); s_sel = (int)ql.quests.size() - 1; ed.dirty = true; }
    ImGui::SameLine();
    if (ImGui::SmallButton("Remove##qlrm") && s_sel >= 0 && s_sel < (int)ql.quests.size()) { ed.PushUndo(); ql.quests.erase(ql.quests.begin() + s_sel); s_sel = -1; ed.dirty = true; }
    if (s_sel >= 0 && s_sel < (int)ql.quests.size()) {
        ImGui::Indent(8.0f);
        std::vector<std::string> ids = KnownQuestIds(ed);
        static bool s_pushed = false;
        if (!ImGui::IsAnyItemActive()) s_pushed = false;
        if (QuestDefEditor(ed, ql.quests[s_sel], ids, [&]() { if (!s_pushed) { ed.PushUndo(); s_pushed = true; } })) ed.dirty = true;
        ImGui::Unindent(8.0f);
    }
    if (ed.isPlaying() || !ql.records.empty()) {
        ImGui::SeparatorText("State");
        for (const QuestDef* q : ql.Defs()) {
            ImGui::PushID(q->id.c_str());
            const QuestStatus st = ql.Status(q->id);
            const ImVec4 c = st == QuestStatus::Active ? ImVec4(0.5f, 0.85f, 0.55f, 1) : st == QuestStatus::Completed ? ImVec4(0.6f, 0.7f, 1.0f, 1) : st == QuestStatus::Failed ? ImVec4(0.95f, 0.45f, 0.4f, 1) : ImVec4(0.6f, 0.6f, 0.65f, 1);
            ImGui::TextColored(c, "%s", QuestStatusName(st));
            ImGui::SameLine(90);
            ImGui::Text("%s", q->id.c_str());
            if (ed.isPlaying()) {
                ImGui::SameLine();
                if (st == QuestStatus::Active) {
                    if (ImGui::SmallButton("Complete")) ql.CompleteQuest(q->id);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Fail")) ql.FailQuest(q->id);
                } else if (ImGui::SmallButton("Start")) ql.StartQuest(q->id);
                ImGui::SameLine();
                if (ImGui::SmallButton("Reset")) ql.ResetQuest(q->id);
            }
            if (st == QuestStatus::Active)
                for (std::size_t i = 0; i < q->objectives.size(); ++i) {
                    const auto& ob = q->objectives[i];
                    ImGui::Indent(12.0f);
                    ImGui::TextDisabled("%s %s  %d/%d", ql.ObjectiveDone(q->id, (int)i) ? "[x]" : "[ ]", ob.description.c_str(), ql.Progress(q->id, (int)i), std::max(1, ob.count));
                    if (ed.isPlaying() && !ql.ObjectiveDone(q->id, (int)i)) { ImGui::SameLine(); ImGui::PushID((int)i); if (ImGui::SmallButton("done")) ql.CompleteObjective(q->id, (int)i); ImGui::PopID(); }
                    ImGui::Unindent(12.0f);
                }
            ImGui::PopID();
        }
    }
}

void QuestTriggerInspector(EditorState& ed, QuestTrigger& qt) {
    ImGui::TextDisabled("A zone: entering it completes \"reach\" objectives that name it. Needs a trigger collider.");
    if (TextEdit("Zone##qtz", qt.zone, qt.gameObject ? ("(empty = \"" + qt.gameObject->name + "\")").c_str() : "zone name")) ed.dirty = true;
    if (ImGui::IsItemActivated()) ed.PushUndo();
    if (TextEdit("Who##qtt", qt.triggerTag, "tag or name (empty = anyone)")) ed.dirty = true;
    if (ImGui::IsItemActivated()) ed.PushUndo();
    if (ImGui::Checkbox("Once##qto", &qt.once)) { ed.PushUndo(); ed.dirty = true; }
    if (QuestCombo(ed, "Also complete##qtq", qt.questId, 160)) { ed.PushUndo(); ed.dirty = true; }
    Tip("Optional: an objective of this quest completes directly, whatever its kind.");
    if (!qt.questId.empty()) {
        int shown = qt.objective + 1;
        ImGui::SetNextItemWidth(80);
        if (ImGui::DragInt("objective##qtob", &shown, 0.2f, 0, 99)) { qt.objective = shown - 1; ed.dirty = true; }
        if (ImGui::IsItemActivated()) ed.PushUndo();
    }
    if (ed.isPlaying() && qt.Fired()) { ImGui::TextDisabled("Fired."); ImGui::SameLine(); if (ImGui::SmallButton("Reset##qtr")) qt.Reset(); }
}

void StoryStateInspector(EditorState& ed, StoryState& st) {
    ImGui::TextDisabled("The story's global variables and flags (dialogues, quests and scripts share them).");
    const bool playing = ed.isPlaying();
    if (playing) ImGui::TextDisabled("Live values (read-only while playing).");
    if (st.vars.empty()) ImGui::TextDisabled("No variables yet.");
    for (auto it = st.vars.begin(); it != st.vars.end();) {
        ImGui::PushID(it->first.c_str());
        const char* type = it->second.IsBool() ? "bool" : it->second.IsString() ? "text" : "number";
        if (playing) {
            ImGui::Text("%s", it->first.c_str()); ImGui::SameLine(150);
            ImGui::TextDisabled("%s", it->second.AsString().c_str()); ImGui::SameLine();
            ImGui::TextDisabled("(%s)", type);
            ++it;
        } else {
            ImGui::Text("%s", it->first.c_str()); ImGui::SameLine(150);
            std::string v = it->second.AsString();
            if (TextEdit("##v", v, nullptr, 120)) { it->second = DialogueValueFromText(v); ed.dirty = true; }
            if (ImGui::IsItemActivated()) ed.PushUndo();
            ImGui::SameLine(); ImGui::TextDisabled("(%s)", type); ImGui::SameLine();
            if (ImGui::SmallButton("x")) { ed.PushUndo(); it = st.vars.erase(it); ed.dirty = true; ImGui::PopID(); continue; }
            ++it;
        }
        ImGui::PopID();
    }
    if (!playing) {
        static char name[64] = "", value[64] = "";
        ImGui::SetNextItemWidth(130); ImGui::InputTextWithHint("##ssn", "name", name, sizeof(name));
        ImGui::SameLine(); ImGui::SetNextItemWidth(100); ImGui::InputTextWithHint("##ssv", "value", value, sizeof(value));
        ImGui::SameLine();
        if (ImGui::SmallButton("Add##ssa") && name[0]) { ed.PushUndo(); st.Set(name, DialogueValueFromText(value)); name[0] = 0; value[0] = 0; ed.dirty = true; }
    }
    ImGui::SeparatorText("Flags");
    std::string flags;
    for (const auto& f : st.flags) { if (!flags.empty()) flags += ", "; flags += f; }
    ImGui::TextWrapped("%s", flags.empty() ? "(none)" : flags.c_str());
    if (!playing) {
        static char flag[64] = "";
        ImGui::SetNextItemWidth(130); ImGui::InputTextWithHint("##ssf", "flag", flag, sizeof(flag));
        ImGui::SameLine();
        if (ImGui::SmallButton("Set##ssfs") && flag[0]) { ed.PushUndo(); st.SetFlag(flag, true); ed.dirty = true; }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear##ssfc") && flag[0]) { ed.PushUndo(); st.SetFlag(flag, false); ed.dirty = true; }
    }
}

} // namespace oked
