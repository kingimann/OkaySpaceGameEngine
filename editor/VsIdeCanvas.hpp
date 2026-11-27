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
