#pragma once
// The Visual Script canvas's IDE tools, drawn with ImGui: the commands (arrange, straighten, auto-arrange, collapse to
// function, bookmarks, find references, watch values, quick fixes), the right-click menu entries and shortcuts that run
// them, the Graph Navigator window (References / Bookmarks / Watches), the Compare window (a graph against an earlier
// save), and the knots (reroute dots) on wires. The rules live in the model headers (VsCanvasTools.hpp, VsCollapse.hpp,
// VsProjectRefs.hpp, VsDebugWatch.hpp, VsGraphDiff.hpp, VsIdeProviders.hpp); this file only shows and calls them.
// Included at the end of VisualScriptEditor.hpp's helpers, before the window's Draw. Written for this project.
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace okay::editor::vsed {

/// What the IDE tools keep per graph tab (kept beside VsEditState, keyed by its tab id).
struct VsIdeTab {
    VsWatchList watches;
    std::map<int, std::pair<float, float>> sizes;   // each block's size as last drawn, in stored units
    std::string pendingCmd;                         // a command picked in a menu this frame, run after the node loop
    int knotDrag = -1;                              // index into g.knots being dragged
};
inline std::map<int, VsIdeTab>& VsIdeTabs() { static std::map<int, VsIdeTab> m; return m; }
inline VsIdeTab& VsIde(const VsEditState& st) { return VsIdeTabs()[st.tabId]; }

/// The Graph Navigator window: references, bookmarks, watches.
struct VsNavigator {
    bool open = false, focusSearch = false;
    int tab = -1;                         // a tab to bring to the front next frame: 0 references, 1 bookmarks, 2 watches
    char text[160] = "";
    int kind = 0;                         // VsFindWhat
    std::vector<VsRefHit> hits;
    std::string searched, root;
};
inline VsNavigator& Navigator() { static VsNavigator n; return n; }

/// A block's box in stored units: its drawn size when it has been drawn, else the estimate.
inline VsBoxF VsLiveBox(const VsEditState& st, const VsEditNode& n) {
    const VsIdeTab& t = VsIde(st);
    const auto it = t.sizes.find(n.id);
    if (it != t.sizes.end()) return {n.x, n.y, it->second.first, it->second.second};
    return VsEstBox(st.g, n);
}

/// The row a plug sits on in its block (0 = the first row under the header). Inputs: flow first, then values.
/// Outputs: flow outputs first, then value outputs. The canvas lays rows out the same way on both sides.
inline int VsPinRow(const VsDef& d, bool exec, bool output, int pin) {
    if (!output) return exec ? 0 : (d.execIn ? 1 : 0) + pin;
    return exec ? pin : (int)d.execOut.size() + pin;
}

/// Straighten wires: each selected block that a wire reaches from another selected block (or, with one block selected,
/// from anything) moves up or down so that wire is level. Returns how many blocks moved.
inline int VsStraightenSelection(VsEditGraph& g, const std::set<int>& sel, float rowH) {
    int moved = 0;
    std::set<int> done;
    auto level = [&](int src, int srcRow, int dst, int dstRow) {
        if (src == dst || done.count(dst) || !sel.count(dst) || (sel.size() > 1 && !sel.count(src))) return;
        VsEditNode* s = g.find(src); VsEditNode* d = g.find(dst);
        if (!s || !d) return;
        const float ny = VsStraightenY(s->y, srcRow, d->y, dstRow, rowH);
        if (std::fabs(ny - d->y) > 0.01f) { d->y = ny; ++moved; g.dirty = true; }
        done.insert(dst);
    };
    for (const VsExecLink& l : g.execLinks) {
        const VsEditNode* s = g.find(l.from); const VsEditNode* d = g.find(l.to);
        const VsDef* sd = s ? VsDefFor(g, *s) : nullptr; const VsDef* dd = d ? VsDefFor(g, *d) : nullptr;
        if (sd && dd) level(l.from, VsPinRow(*sd, true, true, l.pin), l.to, VsPinRow(*dd, true, false, 0));
    }
    for (const VsDataLink& l : g.dataLinks) {
        const VsEditNode* s = g.find(l.producer); const VsEditNode* d = g.find(l.consumer);
        const VsDef* sd = s ? VsDefFor(g, *s) : nullptr; const VsDef* dd = d ? VsDefFor(g, *d) : nullptr;
        if (sd && dd) level(l.producer, VsPinRow(*sd, false, true, l.outPin), l.consumer, VsPinRow(*dd, false, false, l.inPin));
    }
    return moved;
}

/// Find references to `q` and show them in the Navigator.
inline void VsNavigatorSearch(const VsRefQuery& q, const std::string& root) {
    VsNavigator& nav = Navigator();
    std::map<std::string, std::string> open;
    for (const auto& t : Session().tabs) if (t && !t->g.path.empty()) open[t->g.path] = t->g.toText();
    nav.hits = VsFindRefs(root, q, open);
    nav.searched = std::string(VsFindWhatLabel(q.kind)) + " '" + q.name + "'";
    nav.root = root;
    nav.kind = (int)q.kind;
    std::snprintf(nav.text, sizeof nav.text, "%s", q.name.c_str());
    nav.open = true; nav.tab = 0;
}

/// Show block `node` of graph `path`: this tab, an open tab, or the file opened in a new tab.
inline void VsJumpTo(const std::string& path, int node) {
    VsEditState& cur = State();
    if (path.empty() || cur.g.path != path) {
        if (path.empty()) return;
        OpenFile(path);
    }
    VsEditState& st = State();
    if (node >= 0 && st.g.find(node)) { st.focusNode = node; st.sel = {node}; st.selected = node; }
}

/// Run a Visual Script command (VsCommands(), VsIdeProviders.hpp) on a graph tab. `root` is the project folder the
/// reference search looks in. True when the command did something.
inline bool VsRunCommand(VsEditState& st, const std::string& id, const std::string& root = "") {
    VsEditGraph& g = st.g;
    std::set<int> sel = st.sel;
    if (st.selected >= 0 && g.find(st.selected)) sel.insert(st.selected);
    const float rowH = oked::scriptui::Current().rowH / 1.4f;   // a plug row in stored units (the canvas draws at x1.4)
    auto boxes = [&](std::vector<int>& ids, std::vector<float>& xs, std::vector<float>& ys, std::vector<float>& ws, std::vector<float>& hs) {
        for (const auto& n : g.nodes) if (sel.count(n.id)) {
            const VsBoxF b = VsLiveBox(st, n);
            ids.push_back(n.id); xs.push_back(n.x); ys.push_back(n.y); ws.push_back(b.w); hs.push_back(b.h);
        }
    };
    auto put = [&](const std::vector<int>& ids, const std::vector<float>& xs, const std::vector<float>& ys) {
        for (std::size_t i = 0; i < ids.size(); ++i) if (VsEditNode* n = g.find(ids[i])) { n->x = xs[i]; n->y = ys[i]; }
        g.dirty = true;
    };
    static const std::map<std::string, VsAlign> kAlign = {
        {"vs.alignLeft", VsAlign::Left}, {"vs.alignRight", VsAlign::Right}, {"vs.alignTop", VsAlign::Top},
        {"vs.alignBottom", VsAlign::Bottom}, {"vs.alignCenter", VsAlign::CenterX}, {"vs.alignMiddle", VsAlign::CenterY}};
    if (auto a = kAlign.find(id); a != kAlign.end()) {
        if (sel.size() < 2) return false;
        std::vector<int> ids; std::vector<float> xs, ys, ws, hs;
        boxes(ids, xs, ys, ws, hs);
        VsAlignNodes(xs, ys, ws, hs, a->second);
        put(ids, xs, ys);
        return true;
    }
    if (id == "vs.distributeH" || id == "vs.distributeV") {
        if (sel.size() < 3) return false;
        std::vector<int> ids; std::vector<float> xs, ys, ws, hs;
        boxes(ids, xs, ys, ws, hs);
        VsDistributeNodes(xs, ys, ws, hs, id == "vs.distributeH");
        put(ids, xs, ys);
        return true;
    }
    if (id == "vs.straighten") return VsStraightenSelection(g, sel, rowH) > 0;
    if (id == "vs.autoArrange") {
        if (sel.size() < 2) return false;
        std::vector<int> ids; std::vector<float> xs, ys, ws, hs;
        boxes(ids, xs, ys, ws, hs);
        std::vector<VsArrangeEdge> edges;
        for (const auto& l : g.execLinks) if (sel.count(l.from) && sel.count(l.to)) edges.push_back({l.from, l.to});
        for (const auto& l : g.dataLinks) if (sel.count(l.producer) && sel.count(l.consumer)) edges.push_back({l.producer, l.consumer});
        const auto pos = VsAutoArrange(ids, xs, ys, ws, hs, edges, 60.0f, 24.0f);
        for (std::size_t i = 0; i < ids.size(); ++i) { xs[i] = pos[i].first; ys[i] = pos[i].second; }
        put(ids, xs, ys);
        return true;
    }
    if (id == "vs.collapseToFunction") {
        const VsCollapseResult r = VsCollapseToFunction(g, sel);
        if (!r.ok) { Log("Collapse to Function: " + r.why, 1); return false; }
        st.sel = {r.callNode}; st.selected = r.callNode; st.focusNode = r.callNode;
        Log("Made function '" + r.name + "' (" + std::to_string(r.params) + " in, " + std::to_string(r.returns) +
            " out). The Call Function block runs it; its body is below the graph.");
        return true;
    }
    if (id == "vs.toggleBookmark") {
        if (g.path.empty()) { Log("Save the graph to a file first: bookmarks are kept per graph file.", 1); return false; }
        if (sel.empty()) return false;
        for (int n : sel) {
            const VsEditNode* en = g.find(n);
            const VsDef* d = en ? VsDefFor(g, *en) : nullptr;
            Bookmarks().toggle(g.path, n, d && d->title ? d->title : en ? en->type : std::string());
        }
        SaveBookmarks();
        return true;
    }
    if (id == "vs.nextBookmark" || id == "vs.prevBookmark") {
        Bookmarks().prune(g.path, g);
        const int to = Bookmarks().next(g.path, st.selected, id == "vs.nextBookmark");
        if (to < 0) return false;
        st.sel = {to}; st.selected = to; st.focusNode = to;
        return true;
    }
    if (id == "vs.findReferences") {
        const VsEditNode* n = g.find(st.selected >= 0 ? st.selected : (sel.empty() ? -1 : *sel.begin()));
        if (!n) { Navigator().open = true; Navigator().focusSearch = true; Navigator().tab = 0; return false; }
        VsNavigatorSearch(VsRefQueryFor(*n), root);
        return true;
    }
    if (id == "vs.findInGraphs") { Navigator().open = true; Navigator().tab = 0; Navigator().focusSearch = true; Navigator().root = root; return true; }
    if (id == "vs.watchValues") {
        bool any = false;
        for (int n : sel) {
            const VsEditNode* en = g.find(n);
            const VsDef* d = en ? VsDefFor(g, *en) : nullptr;
            if (!d) continue;
            for (int p = 0; p < (int)d->dataOut.size(); ++p) { VsIde(st).watches.toggle(n, p); any = true; }
        }
        if (any) { Navigator().open = true; Navigator().tab = 2; }
        return any;
    }
    if (id == "vs.fixAll") {
        int fixed = 0;
        for (int round = 0; round < 8; ++round) {   // a fix can change what the checker finds: re-check between rounds
            const std::vector<okay::vs::VsIssue> issues = okay::vs::ValidateGraphText(g.toText(), "");
            const auto fixes = VsQuickFixesFor(g, issues);
            if (fixes.empty()) break;
            if (VsApplyQuickFix(g, fixes.front())) ++fixed; else break;
        }
        return fixed > 0;
    }
    return false;
}

/// The entries the IDE tools add to a block's right-click menu.
inline void VsIdeNodeMenu(VsEditState& st, const VsEditNode& n) {
    VsIdeTab& t = VsIde(st);
    const std::vector<VsQuickFix> fixes = VsQuickFixesFor(st.g, st.issues, n.id);
    if (!fixes.empty()) {
        ImGui::Separator();
        for (std::size_t i = 0; i < fixes.size(); ++i) {
            ImGui::PushID((int)i);
            if (ImGui::MenuItem(("Fix: " + fixes[i].title).c_str(), i == 0 ? "Alt+Enter" : nullptr)) VsApplyQuickFix(st.g, fixes[i]);
            ImGui::PopID();
        }
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Find References", "Shift+F12")) t.pendingCmd = "vs.findReferences";
    const bool marked = !st.g.path.empty() && Bookmarks().has(st.g.path, n.id);
    if (ImGui::MenuItem(marked ? "Remove Bookmark" : "Add Bookmark", "Ctrl+B")) t.pendingCmd = "vs.toggleBookmark";
    const VsDef* d = VsDefFor(st.g, n);
    if (d && !d->dataOut.empty()) {
        const bool watched = t.watches.has(n.id, 0);
        if (ImGui::MenuItem(watched ? "Stop Watching Values" : "Watch Values")) t.pendingCmd = "vs.watchValues";
    }
    if (ImGui::BeginMenu("Arrange")) {
        const std::size_t k = st.sel.size();
        if (ImGui::MenuItem("Straighten Wire", "Q")) t.pendingCmd = "vs.straighten";
        if (ImGui::MenuItem("Auto-Arrange", "Shift+L", false, k >= 2)) t.pendingCmd = "vs.autoArrange";
        if (ImGui::MenuItem("Distribute Horizontally", "Shift+H", false, k >= 3)) t.pendingCmd = "vs.distributeH";
        if (ImGui::MenuItem("Distribute Vertically", "Shift+V", false, k >= 3)) t.pendingCmd = "vs.distributeV";
        ImGui::EndMenu();
    }
    std::string why;
    const bool can = VsCanCollapse(st.g, st.sel.empty() ? std::set<int>{n.id} : st.sel, &why);
    if (ImGui::MenuItem("Collapse to Function", nullptr, false, can)) t.pendingCmd = "vs.collapseToFunction";
    if (!can && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", why.c_str());
}

/// The Edit menu's Arrange entries (the old "Align selection" menu, grown).
inline void VsIdeArrangeMenu(VsEditState& st) {
    VsIdeTab& t = VsIde(st);
    const std::size_t k = st.sel.size();
    if (ImGui::BeginMenu("Arrange selection", k >= 1)) {
        if (ImGui::MenuItem("Align Left", "Shift+A", false, k >= 2)) t.pendingCmd = "vs.alignLeft";
        if (ImGui::MenuItem("Align Right", "Shift+D", false, k >= 2)) t.pendingCmd = "vs.alignRight";
        if (ImGui::MenuItem("Align Top", "Shift+W", false, k >= 2)) t.pendingCmd = "vs.alignTop";
        if (ImGui::MenuItem("Align Bottom", "Shift+S", false, k >= 2)) t.pendingCmd = "vs.alignBottom";
        if (ImGui::MenuItem("Align Centers", nullptr, false, k >= 2)) t.pendingCmd = "vs.alignCenter";
        if (ImGui::MenuItem("Align Middles", nullptr, false, k >= 2)) t.pendingCmd = "vs.alignMiddle";
        ImGui::Separator();
        if (ImGui::MenuItem("Distribute Horizontally", "Shift+H", false, k >= 3)) t.pendingCmd = "vs.distributeH";
        if (ImGui::MenuItem("Distribute Vertically", "Shift+V", false, k >= 3)) t.pendingCmd = "vs.distributeV";
        if (ImGui::MenuItem("Straighten Wires", "Q")) t.pendingCmd = "vs.straighten";
        if (ImGui::MenuItem("Auto-Arrange", "Shift+L", false, k >= 2)) t.pendingCmd = "vs.autoArrange";
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Collapse to Function", nullptr, false, k >= 1)) t.pendingCmd = "vs.collapseToFunction";
    if (ImGui::MenuItem("Find in All Graphs...", "Ctrl+Shift+F")) t.pendingCmd = "vs.findInGraphs";
    if (ImGui::MenuItem("Graph Navigator", nullptr, Navigator().open)) Navigator().open = !Navigator().open;
}

/// The IDE tools' shortcuts (canvas focused, not typing in a box).
inline void VsIdeShortcuts(VsEditState& st) {
    const ImGuiIO& io = ImGui::GetIO();
    VsIdeTab& t = VsIde(st);
    const bool shift = io.KeyShift, ctrl = io.KeyCtrl, alt = io.KeyAlt;
    auto key = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
    if (!ctrl && !alt && shift) {
        if (key(ImGuiKey_A)) t.pendingCmd = "vs.alignLeft";
        if (key(ImGuiKey_D)) t.pendingCmd = "vs.alignRight";
        if (key(ImGuiKey_W)) t.pendingCmd = "vs.alignTop";
        if (key(ImGuiKey_S)) t.pendingCmd = "vs.alignBottom";
        if (key(ImGuiKey_H)) t.pendingCmd = "vs.distributeH";
        if (key(ImGuiKey_V)) t.pendingCmd = "vs.distributeV";
        if (key(ImGuiKey_L)) t.pendingCmd = "vs.autoArrange";
        if (key(ImGuiKey_F12)) t.pendingCmd = "vs.findReferences";
        if (key(ImGuiKey_F2)) t.pendingCmd = "vs.prevBookmark";
    }
    if (!ctrl && !alt && !shift) {
        if (key(ImGuiKey_Q)) t.pendingCmd = "vs.straighten";
        if (key(ImGuiKey_F2)) t.pendingCmd = "vs.nextBookmark";
    }
    if (ctrl && !shift && !alt && key(ImGuiKey_B)) t.pendingCmd = "vs.toggleBookmark";
    if (ctrl && shift && !alt && key(ImGuiKey_F)) t.pendingCmd = "vs.findInGraphs";
    if (alt && !ctrl && (key(ImGuiKey_Enter) || key(ImGuiKey_KeypadEnter)) && st.selected >= 0) {   // the first fix at the block
        const auto fixes = VsQuickFixesFor(st.g, st.issues, st.selected);
        if (!fixes.empty()) VsApplyQuickFix(st.g, fixes.front());
    }
}

/// Run the command picked this frame (menus and shortcuts set it; it runs once the node loop is done).
inline void VsIdeRunPending(VsEditState& st, const std::string& root) {
    VsIdeTab& t = VsIde(st);
    if (t.pendingCmd.empty()) return;
    const std::string cmd = t.pendingCmd;
    t.pendingCmd.clear();
    VsRunCommand(st, cmd, root);
}

/// Per block, while it is drawn: remember its size, and mark bookmarks and Compare changes.
inline void VsIdeNodeDecor(ImDrawList* dl, VsEditState& st, const VsEditNode& n, ImVec2 p0, ImVec2 p1, float z, float layoutScale,
                           const std::set<int>& diffAdded, const std::set<int>& diffChanged) {
    VsIde(st).sizes[n.id] = {(p1.x - p0.x) / (layoutScale * z), (p1.y - p0.y) / (layoutScale * z)};
    if (!st.g.path.empty() && Bookmarks().has(st.g.path, n.id)) {   // a blue tag on the top-left corner
        const float s = 11.0f * std::max(0.7f, z);
        dl->AddTriangleFilled(ImVec2(p0.x, p0.y), ImVec2(p0.x + s * 1.8f, p0.y), ImVec2(p0.x, p0.y + s * 1.8f), IM_COL32(80, 150, 255, 255));
    }
    const bool added = diffAdded.count(n.id) > 0, changed = diffChanged.count(n.id) > 0;
    if (added || changed)
        dl->AddRect(ImVec2(p0.x - 5 * z, p0.y - 5 * z), ImVec2(p1.x + 5 * z, p1.y + 5 * z),
                    added ? IM_COL32(90, 220, 120, 255) : IM_COL32(240, 185, 70, 255), 8.0f * z, 0, 3.0f * z);
}

/// The points a wire passes through (screen), its knots in order along the wire. `toScreen` maps stored units.
template <class ToScreen>
inline std::vector<ImVec2> VsKnotPoints(const VsEditGraph& g, bool exec, int a, int b, int c, ToScreen toScreen) {
    std::vector<std::pair<float, ImVec2>> pts;
    for (const VsKnot& k : g.knots)
        if (k.exec == exec && k.a == a && k.b == b && (!exec || k.c == c)) pts.push_back({k.x, toScreen(k.x, k.y)});
    std::stable_sort(pts.begin(), pts.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    std::vector<ImVec2> out;
    for (const auto& p : pts) out.push_back(p.second);
    return out;
}

/// Knots: drawn as dots; drag one to move it, right-click it to take it away.
template <class ToScreen, class ToWorld>
inline void VsIdeKnots(ImDrawList* dl, VsEditState& st, float z, ToScreen toScreen, ToWorld toWorld, bool canvasHovered) {
    VsEditGraph& g = st.g;
    VsIdeTab& t = VsIde(st);
    const ImGuiIO& io = ImGui::GetIO();
    const float r = 6.0f * std::max(0.8f, z);
    int hover = -1;
    for (int i = 0; i < (int)g.knots.size(); ++i) {
        const ImVec2 p = toScreen(g.knots[(std::size_t)i].x, g.knots[(std::size_t)i].y);
        const bool h = canvasHovered && std::fabs(io.MousePos.x - p.x) <= r + 3 && std::fabs(io.MousePos.y - p.y) <= r + 3;
        if (h) hover = i;
        dl->AddCircleFilled(p, r, h || t.knotDrag == i ? IM_COL32(255, 255, 255, 255) : IM_COL32(205, 208, 218, 255), 14);
        dl->AddCircle(p, r, IM_COL32(30, 32, 40, 255), 14, 1.5f);
    }
    if (hover >= 0 && t.knotDrag < 0) ImGui::SetTooltip("Knot: drag to move, right-click to remove");
    if (hover >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) t.knotDrag = hover;
    if (hover >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) { g.knots.erase(g.knots.begin() + hover); g.dirty = true; t.knotDrag = -1; return; }
    if (t.knotDrag >= 0 && t.knotDrag < (int)g.knots.size()) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const ImVec2 w = toWorld(io.MousePos);
            g.knots[(std::size_t)t.knotDrag].x = w.x; g.knots[(std::size_t)t.knotDrag].y = w.y; g.dirty = true;
        } else t.knotDrag = -1;
    } else t.knotDrag = -1;
}
/// True while a knot is being dragged (the canvas then does not pan or box-select).
inline bool VsIdeKnotBusy(const VsEditState& st) { return VsIde(st).knotDrag >= 0; }

/// Add a knot at stored-unit point (x, y) on the wire `ref` names (Shift+double-click on a wire).
inline void VsAddKnot(VsEditGraph& g, const VsWireRef& ref, float x, float y) {
    if (!ref.on) return;
    VsKnot k; k.exec = ref.exec; k.a = ref.a; k.b = ref.b; k.c = ref.exec ? ref.c : 0; k.x = x; k.y = y;
    g.knots.push_back(k);
    g.dirty = true;
}

/// The watch list as rows with a remove button (the Navigator's Watches tab and the paused debugger panel).
inline void VsIdeDrawWatches(VsEditState& st, const okay::vs::NodeGraph* live) {
    VsIdeTab& t = VsIde(st);
    t.watches.prune(st.g);
    if (t.watches.items.empty()) { ImGui::TextDisabled("No watches. Right-click a block > Watch Values."); return; }
    const auto rows = VsWatchRows(st.g, live, t.watches);
    int remove = -1;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        ImGui::PushID((int)i);
        if (ImGui::SmallButton("x")) remove = (int)i;
        ImGui::SameLine();
        if (ImGui::Selectable(rows[i].label.c_str(), false, 0, ImVec2(ImGui::GetContentRegionAvail().x * 0.55f, 0))) {
            const int node = t.watches.items[i].node;
            st.sel = {node}; st.selected = node; st.focusNode = node;
        }
        ImGui::SameLine();
        if (rows[i].known) ImGui::TextUnformatted(rows[i].value.c_str());
        else ImGui::TextDisabled("%s", rows[i].value.c_str());
        ImGui::PopID();
    }
    if (remove >= 0) t.watches.remove((std::size_t)remove);
}

/// The Graph Navigator window: find references in every graph of the project, the bookmarks, and the watches.
inline void VsIdeDrawNavigator(VsEditState& st, const okay::vs::NodeGraph* live, const std::string& root) {
    VsNavigator& nav = Navigator();
    if (!nav.open) return;
    ImGui::SetNextWindowSize(ImVec2(520, 380), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Graph Navigator##vsnav", &nav.open)) { ImGui::End(); return; }
    if (ImGui::BeginTabBar("##vsnavtabs")) {
        const int want = nav.tab;
        nav.tab = -1;
        if (ImGui::BeginTabItem("References", nullptr, want == 0 ? ImGuiTabItemFlags_SetSelected : 0)) {
            static const char* kinds[] = {"Block type", "Variable", "Event", "Function"};
            ImGui::SetNextItemWidth(120);
            ImGui::Combo("##vsnavkind", &nav.kind, kinds, 4);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(220);
            if (nav.focusSearch) { ImGui::SetKeyboardFocusHere(); nav.focusSearch = false; }
            const bool go = ImGui::InputText("##vsnavtext", nav.text, sizeof nav.text, ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if ((ImGui::Button("Find") || go) && nav.text[0])
                VsNavigatorSearch({(VsFindWhat)nav.kind, nav.text}, root.empty() ? nav.root : root);
            if (!nav.searched.empty()) {
                ImGui::TextDisabled("%d place(s) use %s", (int)nav.hits.size(), nav.searched.c_str());
                ImGui::BeginChild("##vsnavhits", ImVec2(0, 0), ImGuiChildFlags_Borders);
                for (std::size_t i = 0; i < nav.hits.size(); ++i) {
                    const VsRefHit& h = nav.hits[i];
                    std::string file = h.path;
                    if (const auto s = file.find_last_of("/\\"); s != std::string::npos) file = file.substr(s + 1);
                    const VsDef* d = FindDef(h.type);
                    const std::string row = file + (h.node >= 0 ? "  #" + std::to_string(h.node) : std::string("  (declared)")) + "  " + h.role + "  " +
                                            (d && d->title ? d->title : h.type.c_str()) + (h.arg.empty() ? "" : "  \"" + h.arg + "\"");
                    ImGui::PushID((int)i);
                    if (ImGui::Selectable(row.c_str())) VsJumpTo(h.path, h.node);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", h.path.c_str());
                    ImGui::PopID();
                }
                ImGui::EndChild();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Bookmarks", nullptr, want == 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
            ImGui::TextDisabled("Ctrl+B marks the selected block; F2 / Shift+F2 go to the next / previous one.");
            int n = 0;
            std::pair<std::string, int> drop{"", -1};
            for (const auto& kv : Bookmarks().byPath)
                for (const VsBookmark& b : kv.second) {
                    std::string file = kv.first;
                    if (const auto s = file.find_last_of("/\\"); s != std::string::npos) file = file.substr(s + 1);
                    ImGui::PushID(n++);
                    if (ImGui::SmallButton("x")) drop = {kv.first, b.node};
                    ImGui::SameLine();
                    if (ImGui::Selectable((file + "  #" + std::to_string(b.node) + "  " + b.label).c_str())) VsJumpTo(kv.first, b.node);
                    ImGui::PopID();
                }
            if (n == 0) ImGui::TextDisabled("No bookmarks yet.");
            if (drop.second >= 0) { Bookmarks().toggle(drop.first, drop.second); SaveBookmarks(); }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Watches", nullptr, want == 2 ? ImGuiTabItemFlags_SetSelected : 0)) {
            if (!live) ImGui::TextDisabled("Values show while the game plays (or is paused at a breakpoint).");
            VsIdeDrawWatches(st, live);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

/// What changed since the earlier save the Compare window is open on (empty when it is closed or on another graph).
inline const VsGraphDiff& VsIdeDiffNow(const VsEditState& st) {
    static const VsGraphDiff none;
    static std::string s_key; static VsGraphDiff s_diff;   // worked out again only when either side changes
    const VsDiffView& v = DiffView();
    if (!v.open || v.path != st.g.path) return none;
    std::string now = st.g.toText();
    std::string key = v.beforeText + '\x01' + now;
    if (key != s_key) { s_diff = VsDiffGraphTexts(v.beforeText, now); s_key = std::move(key); }
    return s_diff;
}

/// The Compare window: the graph on screen against an earlier save (opened from File > Local history > Compare).
inline void VsIdeDrawCompare(VsEditState& st) {
    VsDiffView& v = DiffView();
    if (!v.open || v.path != st.g.path) return;   // compares only with the graph it was opened for
    const VsGraphDiff outDiff = VsIdeDiffNow(st);
    ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Compare with earlier save##vsdiff", &v.open)) { ImGui::End(); return; }
    ImGui::Text("Now vs %s", v.label.c_str());
    ImGui::TextDisabled("Green outline: block added since then. Amber: block changed. Click a row to go to it.");
    if (outDiff.empty()) ImGui::TextUnformatted("No changes.");
    auto nodes = [&](const char* title, const std::vector<VsDiffNode>& list, bool canJump) {
        if (list.empty() || !ImGui::CollapsingHeader((std::string(title) + " (" + std::to_string(list.size()) + ")").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) return;
        for (const VsDiffNode& n : list) {
            const std::string row = "#" + std::to_string(n.id) + "  " + (n.before.empty() ? n.after : n.after.empty() ? n.before : n.before + "  ->  " + n.after);
            ImGui::PushID(n.id);
            if (ImGui::Selectable(row.c_str()) && canJump) { st.sel = {n.id}; st.selected = n.id; st.focusNode = n.id; }
            ImGui::PopID();
        }
    };
    nodes("Added", outDiff.added, true);
    nodes("Changed", outDiff.changed, true);
    nodes("Removed", outDiff.removed, false);
    nodes("Moved only", outDiff.moved, true);
    auto lines = [&](const char* title, const std::vector<std::string>& list) {
        if (list.empty() || !ImGui::CollapsingHeader((std::string(title) + " (" + std::to_string(list.size()) + ")").c_str())) return;
        for (const std::string& s : list) ImGui::BulletText("%s", s.c_str());
    };
    lines("Wires added", outDiff.wiresAdded);
    lines("Wires removed", outDiff.wiresRemoved);
    lines("Variables added", outDiff.varsAdded);
    lines("Variables removed", outDiff.varsRemoved);
    lines("Variables changed", outDiff.varsChanged);
    if (ImGui::Button("Restore that version")) {
        st.undo.push_back(st.g.toText());
        st.g.fromText(v.beforeText);
        VsRefreshDirty(st);
        v.open = false;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Undo (Ctrl+Z) brings this version back.");
    ImGui::End();
}

} // namespace okay::editor::vsed
