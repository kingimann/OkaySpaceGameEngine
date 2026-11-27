#pragma once
// A node-based visual scripting editor for OkayVS graphs (.okayvs). You drag
// nodes onto a canvas, wire execution (triangles) and data (circles) pins, edit
// per-node values, and save a .okayvs file the engine's VisualScriptComponent
// runs at play time. Self-contained: it keeps its own editable model and writes
// the same text format the engine parses (node/pos/exec/data/entry lines).
// The window here is drawn with Dear ImGui; the model it edits is VisualScriptModel.hpp (no drawing), which the Visual Script
// app (shared/VisualScriptApp.hpp) draws with OkayUI.
#include "imgui.h"
#include "VisualScriptModel.hpp"
#include "ScriptingDensityUi.hpp"
#include "VsVariablePanel.hpp"

namespace okay::editor::vsed {

// The key pressed this frame, as a runtime name ("" if none).
inline std::string VsCapturedKey() {
    for (int i = 0; i < 26; ++i)
        if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_A + i), false)) return std::string(1, (char)('a' + i));
    for (int i = 0; i < 10; ++i)
        if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_0 + i), false) ||
            ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_Keypad0 + i), false)) return std::string(1, (char)('0' + i));
    struct M { ImGuiKey k; const char* n; };
    const M named[] = {{ImGuiKey_Space, "space"}, {ImGuiKey_Enter, "enter"}, {ImGuiKey_KeypadEnter, "enter"},
                       {ImGuiKey_Tab, "tab"}, {ImGuiKey_Backspace, "backspace"},
                       {ImGuiKey_LeftShift, "shift"}, {ImGuiKey_RightShift, "shift"},
                       {ImGuiKey_LeftCtrl, "ctrl"}, {ImGuiKey_RightCtrl, "ctrl"},
                       {ImGuiKey_UpArrow, "up"}, {ImGuiKey_DownArrow, "down"},
                       {ImGuiKey_LeftArrow, "left"}, {ImGuiKey_RightArrow, "right"}};
    for (const M& m : named) if (ImGui::IsKeyPressed(m.k, false)) return m.n;
    return {};
}

inline bool KeyPicker(std::string& arg, float width) {
    bool changed = false;
    std::string lbl = "Key: " + VsKeyLabel(arg) + "##keypick";
    if (ImGui::Button(lbl.c_str(), ImVec2(width, 0))) ImGui::OpenPopup("##keypickpop");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click, then press the key you want (or pick it from the list).");
    if (ImGui::BeginPopup("##keypickpop")) {
        ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f), "Press any key...");
        ImGui::TextDisabled("or pick one below (Esc closes)");
        if (!ImGui::IsWindowAppearing()) {   // not the click that opened it
            std::string got = VsCapturedKey();
            if (!got.empty()) { arg = got; changed = true; ImGui::CloseCurrentPopup(); }
        }
        ImGui::Separator();
        ImGui::BeginChild("##keylist", ImVec2(250, 230));
        for (const auto& k : VsNamedKeys())
            if (ImGui::Selectable(k.label, arg == k.name)) { arg = k.name; changed = true; ImGui::CloseCurrentPopup(); }
        ImGui::Separator();
        auto grid = [&](char from, char to) {
            int col = 0;
            for (char c = from; c <= to; ++c) {
                char nm[2] = {c, 0}, shown[2] = {(char)std::toupper((unsigned char)c), 0};
                if (col++ % 8) ImGui::SameLine();
                ImGui::PushID(c);
                if (ImGui::Selectable(shown, arg == nm, 0, ImVec2(20, 20))) { arg = nm; changed = true; ImGui::CloseCurrentPopup(); }
                ImGui::PopID();
            }
        };
        grid('a', 'z');
        ImGui::Separator();
        grid('0', '9');
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    return changed;
}

// Read a Hierarchy / Scene object or a Project file dropped on a slot.
// True when `value` changed. A wrong type sets st.refHint and leaves `value`.
inline bool VsApplyDrop(EditorState& ed, VsEditState& st, okay::vs::VsRefKind kind, std::string& value) {
    using okay::vs::VsRefKind;
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("GO_PTR")) {
        okay::GameObject* go = p->Data ? *(okay::GameObject**)p->Data : nullptr;
        if (kind != VsRefKind::Object) { st.refHint = "That is an object. This slot wants a file from the Project window."; return false; }
        if (!go) return false;
        value = go->Guid().empty() ? go->name : okay::vs::VsGuidRef(go->Guid());
        st.refHint.clear();
        return true;
    }
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
        const std::string path = p->Data ? std::string((const char*)p->Data) : std::string();
        if (kind == VsRefKind::Object) { st.refHint = "That is a file. Drag an object from the Hierarchy or Scene view."; return false; }
        if (!okay::vs::VsExtOk(kind, path)) {
            st.refHint = std::string("Wrong file. This slot wants ") + okay::vs::VsRefKindName(kind) + ".";
            return false;
        }
        value = okay::vs::VsAssetRef(kind, VsProjectRelative(path, ed.projectDir()));
        st.refHint.clear();
        return true;
    }
    return false;
}

// One reference slot: name, picker, clear, and drag-drop. True when `value` changed.
/// One field of a Quick block, drawn with the widget for its kind: a number you drag or type, a key to press, an object
/// to pick, a dropdown, a colour swatch, or the presets list. Returns true when the value changed. Plain text fields
/// return false (the caller draws the text box).
inline bool VsDrawQuickField(EditorState& ed, VsEditGraph& g, VsEditNode& n, const okay::vs::QuickBlock& qb, int i, std::string& dv, float width) {
    using okay::vs::QbKind;
    const std::vector<okay::vs::QbField> fields = okay::vs::QbAllFields(qb);
    if (i < 0 || i >= (int)fields.size()) return false;
    const okay::vs::QbField& f = fields[(std::size_t)i];
    bool changed = false;
    char id[24]; std::snprintf(id, sizeof id, "##qf%d", i);
    ImGui::SetNextItemWidth(width);
    switch (f.kind) {
        case QbKind::Number: {
            float v = (float)std::atof((dv.empty() ? std::string(f.def) : dv).c_str());
            if (ImGui::DragFloat(id, &v, 0.1f, 0.0f, 0.0f, "%g")) { char b[32]; std::snprintf(b, sizeof b, "%g", v); dv = b; changed = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s: drag, or Ctrl+click to type a number", f.name);
            return changed;
        }
        case QbKind::Key: {
            std::string k = dv.empty() ? std::string(f.def) : dv;
            if (KeyPicker(k, width)) { dv = k; VsQuickContext().lastKey = k; changed = true; }
            return changed;
        }
        case QbKind::Object: {
            const std::string shown = dv.empty() ? std::string("This object") : dv;
            if (ImGui::BeginCombo(id, shown.c_str())) {
                if (ImGui::Selectable("This object", dv.empty())) { dv.clear(); changed = true; }
                for (const auto& up : ed.scene().Objects())
                    if (up && ImGui::Selectable(up->name.c_str(), dv == up->name)) { dv = up->name; changed = true; }
                ImGui::EndCombo();
            }
            return changed;
        }
        case QbKind::Choice: {
            const std::string cur = dv.empty() ? std::string(f.def) : dv;
            std::string label = cur;
            for (const auto& c : okay::vs::QbChoices(f.choices)) if (c.second == cur) label = c.first;
            if (ImGui::BeginCombo(id, label.c_str())) {
                for (const auto& c : okay::vs::QbChoices(f.choices))
                    if (ImGui::Selectable(c.first.c_str(), c.second == cur)) { dv = c.second; changed = true; }
                ImGui::EndCombo();
            }
            return changed;
        }
        case QbKind::Colour: {
            const std::string cur = dv.empty() ? std::string(f.def) : dv;
            float col[3] = {1, 1, 1};
            if (okay::vs::QbValidate(QbKind::Colour, cur))
                for (int k = 0; k < 3; ++k) col[k] = (float)std::strtol(cur.substr(1 + (std::size_t)k * 2, 2).c_str(), nullptr, 16) / 255.0f;
            if (ImGui::ColorEdit3(id, col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
                char b[16]; std::snprintf(b, sizeof b, "#%02x%02x%02x", (int)(col[0] * 255.0f + 0.5f), (int)(col[1] * 255.0f + 0.5f), (int)(col[2] * 255.0f + 0.5f));
                dv = b; changed = true;
            }
            return changed;
        }
        case QbKind::Name: {
            // Type a name (it is made on the spot) or pick one the script already uses, so a typo cannot make a new one.
            // The small button says who can see it: only this object (the default) or everyone (every graph in the scene).
            const std::string whole = dv.empty() ? std::string(f.def) : dv;
            const bool shared = okay::vs::QbNameShared(whole);
            char buf[64]; std::snprintf(buf, sizeof buf, "%s", okay::vs::QbNameBare(whole).c_str());
            const float btn = 22.0f, scopeW = 34.0f;
            ImGui::SetNextItemWidth(std::max(30.0f, width - btn - scopeW - 8.0f));
            if (ImGui::InputTextWithHint(id, "name, like score", buf, sizeof buf)) {
                dv = okay::vs::QbNameWithScope(okay::vs::QbNormalizeName(buf), shared); changed = true;
            }
            ImGui::SameLine(0, 4);
            if (ImGui::Button("v##names", ImVec2(btn, 0))) ImGui::OpenPopup("##namelist");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pick a saved value this script already uses");
            ImGui::SameLine(0, 4);
            if (ImGui::Button(shared ? "All##scope" : "Me##scope", ImVec2(scopeW, 0))) {
                dv = okay::vs::QbNameWithScope(buf, !shared); changed = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(shared ? "Everyone can see this. Click for only this object." : "Only this object. Click for everyone can see this.");
            if (ImGui::BeginPopup("##namelist")) {
                const std::vector<std::string> used = VsUsedValueNames(g);
                if (used.empty()) ImGui::TextDisabled("No saved values yet. Type a name.");
                for (const std::string& u : used) if (ImGui::Selectable(u.c_str())) { dv = okay::vs::QbNameWithScope(u, shared); changed = true; }
                ImGui::EndPopup();
            }
            return changed;
        }
        case QbKind::Preset: {
            if (ImGui::BeginCombo(id, "Choose a preset")) {
                for (const auto& c : okay::vs::QbChoices(f.choices))
                    if (ImGui::Selectable(c.first.c_str())) {
                        std::vector<std::string> values;
                        for (std::size_t k = 0; k < qb.fields.size(); ++k) {
                            auto it = n.pinDefault.find((int)k);
                            values.push_back(it != n.pinDefault.end() ? it->second : std::string(qb.fields[k].def));
                        }
                        if (okay::vs::QbApplyPreset(qb, c.second, values))
                            for (std::size_t k = 0; k < values.size(); ++k) n.pinDefault[(int)k] = values[k];
                        dv.clear(); changed = true;
                    }
                ImGui::EndCombo();
            }
            return changed;
        }
        default: return false;
    }
}

inline bool VsDrawRefSlot(EditorState& ed, VsEditState& st, std::string& value, okay::vs::VsRefKind kind,
                          bool defaultSelf, float width, const char* id) {
    using okay::vs::VsRefKind;
    bool changed = false;
    const std::string label = VsRefLabel(value, kind, defaultSelf, ed.scene());
    std::string shown = std::string(VsRefIcon(kind)) + " " + label;
    const float xw = 22.0f;
    ImGui::PushID(id);
    if (ImGui::Button(shown.c_str(), ImVec2(std::max(40.0f, width - xw - 4.0f), 0)))
        ImGui::OpenPopup("pick");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(kind == VsRefKind::Object
            ? "Drag an object from the Hierarchy or the Scene view, or click to pick.\nSelf is this object."
            : "Drag a file from the Project window, or click to pick.");
    if (ImGui::BeginDragDropTarget()) {
        if (VsApplyDrop(ed, st, kind, value)) changed = true;
        ImGui::EndDragDropTarget();
    }
    ImGui::SameLine(0, 4);
    if (ImGui::Button("x", ImVec2(xw, 0))) { value.clear(); changed = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(defaultSelf ? "Clear (back to Self)" : "Clear");
    if (ImGui::BeginPopup("pick")) {
        static char q[64] = "";
        ImGui::SetNextItemWidth(220);
        ImGui::InputTextWithHint("##q", "Search", q, sizeof(q));
        std::string needle = q;
        for (char& c : needle) c = (char)std::tolower((unsigned char)c);
        auto row = [&](const char* name, const std::string& stored) {
            std::string h = name;
            for (char& c : h) c = (char)std::tolower((unsigned char)c);
            if (!needle.empty() && h.find(needle) == std::string::npos) return;
            if (ImGui::Selectable(name)) { value = stored; changed = true; ImGui::CloseCurrentPopup(); }
        };
        if (kind == VsRefKind::Object) {
            row("Self", "@self");
            row("Other", "@other");
            for (const auto& up : ed.scene().Objects()) if (up) row(up->name.c_str(), okay::vs::VsGuidRef(up->Guid()));
        } else {
            namespace fs = std::filesystem;
            fs::path assets = ed.projectDir().empty() ? fs::path("Assets") : fs::path(ed.projectDir()) / "Assets";
            std::error_code ec; int shownN = 0;
            if (fs::exists(assets, ec))
                for (auto it = fs::recursive_directory_iterator(assets, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
                    if (ec || !it->is_regular_file(ec)) continue;
                    const std::string full = it->path().string();
                    if (!okay::vs::VsExtOk(kind, full)) continue;
                    const std::string rel = VsProjectRelative(full, ed.projectDir());
                    row(rel.c_str(), okay::vs::VsAssetRef(kind, rel));
                    if (++shownN > 400) break;
                }
            if (!shownN) ImGui::TextDisabled("Nothing of that type in Assets.");
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

inline bool VsDrawChoice(EditorState& ed, const VsEditGraph& g, std::string& value, VsChoiceKind kind, float width) {
    // Every node with a dropdown draws it every frame: the lists of one kind are gathered once a frame, not once per node (a graph
    // with hundreds of variable nodes gathered every variable of every node for each of them).
    struct Gathered { VsChoiceLists lists; std::vector<std::string> rows; };
    static int s_frame = -1;
    static const void* s_graph = nullptr;
    static std::map<int, Gathered> s_byKind;
    if (s_frame != ImGui::GetFrameCount() || s_graph != &g) { s_frame = ImGui::GetFrameCount(); s_graph = &g; s_byKind.clear(); }
    auto found = s_byKind.find((int)kind);
    if (found == s_byKind.end()) {
        Gathered gd;
        gd.lists = VsGatherChoices(ed, g, kind);
        gd.rows = VsChoiceRows(kind, gd.lists);
        found = s_byKind.emplace((int)kind, std::move(gd)).first;
    }
    const VsChoiceLists& lists = found->second.lists;
    const std::vector<std::string>& rows = found->second.rows;
    const bool missing = VsChoiceMissing(value, rows);
    bool changed = false;
    const std::string preview = value.empty() ? std::string("(choose a ") + VsChoiceKindName(kind) + ")" : value;
    ImGui::SetNextItemWidth(width);
    if (missing) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.45f, 1.0f));
    bool colorPushed = missing;
    if (ImGui::BeginCombo("##choice", preview.c_str())) {
        if (colorPushed) { ImGui::PopStyleColor(); colorPushed = false; }   // the open list uses the normal text colour
        auto section = [&](const char* title, const std::vector<std::string>& items) {
            if (items.empty()) return;
            ImGui::TextDisabled("%s", title);
            for (const auto& c : items)
                if (ImGui::Selectable(c.c_str(), c == value)) { value = c; changed = true; }
        };
        if (kind == VsChoiceKind::Clip && !lists.onTarget.empty()) {
            section("On this object", lists.onTarget);
            section("Other clips", lists.fallback);
        } else {
            section(kind == VsChoiceKind::Clip ? "Clips" : "Choices", rows);
        }
        if (rows.empty()) ImGui::TextDisabled("Nothing to pick yet. You can still drop a file.");
        ImGui::EndCombo();
    }
    if (colorPushed) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
        if (missing) ImGui::SetTooltip("\"%s\" is not in the list any more. Pick another, or drop a file.", value.c_str());
        else ImGui::SetTooltip("Pick a %s, or drop one from the Project window.", VsChoiceKindName(kind));
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
            const std::string path = p->Data ? (const char*)p->Data : "";
            const bool clip = kind == VsChoiceKind::Clip && (okay::vs::VsExtOk(okay::vs::VsRefKind::Animation, path));
            const bool sound = kind == VsChoiceKind::Sound && okay::vs::VsExtOk(okay::vs::VsRefKind::Audio, path);
            const bool scene = kind == VsChoiceKind::Scene && okay::vs::VsExtOk(okay::vs::VsRefKind::Scene, path);
            if (clip || sound || scene) {
                value = std::filesystem::path(path).stem().string();
                changed = true;
            } else {
                State().refHint = std::string("That file is not a ") + VsChoiceKindName(kind) + ".";
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (missing) {   // drawn on the canvas under the box (a widget here would land at the window's left edge)
        const ImVec2 r0 = ImGui::GetItemRectMin(), r1 = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddText(ImVec2(r0.x, r1.y + 1.0f), IM_COL32(255, 140, 115, 255), "Not in the list");
    }
    return changed;
}

}  // namespace okay::editor::vsed
#include "VisualScriptPalette.hpp"
#include "VisualScriptStartDraw.hpp"  // the big recipe cards shown for an empty graph
#include "VisualScriptGuideDraw.hpp"  // the "What is this?" card, next-step chips, Try this node
#include "VsIdeCanvas.hpp"           // the IDE tools on the canvas: arrange, collapse, navigator, compare, knots, watches
namespace okay::editor::vsed {

// ---- The editor window -----------------------------------------------------
// Returns nothing; manages its own state. `ed` is used for "Attach to selected".
inline void Draw(EditorState& ed, bool* open) {
    ImGui::SetNextWindowSize(ImVec2(1180, 760), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Visual Script", open)) { ImGui::End(); return; }
    // A floating window remembered tiny (or squashed to its toolbar) has no room
    // for the canvas: give it a usable size.
    if (!ImGui::IsWindowDocked() && (ImGui::GetWindowSize().y < 300.0f || ImGui::GetWindowSize().x < 500.0f))
        ImGui::SetWindowSize(ImVec2(std::max(ImGui::GetWindowSize().x, 980.0f), 600.0f));
    VsRestoreTabs();
    // Closes are applied on the next frame, before any reference to a tab is taken.
    static std::vector<int> s_closeIds;
    static bool s_closeRemember = false;
    if (!s_closeIds.empty()) {
        std::sort(s_closeIds.begin(), s_closeIds.end(), std::greater<int>());
        for (int id : s_closeIds) VsCloseTab(id, s_closeRemember);
        s_closeIds.clear();
    }
    for (auto& tab : Session().tabs) if (tab) VsRefreshDirty(*tab);
    if (open && !*open && VsAnyUnsaved()) { *open = true; Session().ask = VsSession::Ask::CloseWindow; }
    // Tabs first, so a click this frame selects that graph before the canvas is drawn.
    // SetSelected every frame kept the old tab and the canvas never followed the click.
    ImGuiIO& io = ImGui::GetIO();
    const bool winFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const bool typing = io.WantTextInput;
    {
        VsSession& ses = Session();
        const oked::scriptui::Metrics dm = oked::scriptui::Current();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(dm.tabPad.x, dm.tabPad.y));
        if (ImGui::BeginTabBar("##vstabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
            for (int i = 0; i < (int)ses.tabs.size(); ++i) {
                VsEditState& tab = *ses.tabs[(std::size_t)i];
                std::string title = VsTabTitle(tab);
                if (tab.missing) title += " (missing)";
                if (tab.g.dirty) title += " \xE2\x97\x8F";
                title += "###vstab" + std::to_string(tab.tabId);
                bool openTab = true;
                ImGuiTabItemFlags fl = (ses.forceSelect && i == ses.active) ? ImGuiTabItemFlags_SetSelected : 0;
                if (ImGui::BeginTabItem(title.c_str(), &openTab, fl)) {
                    ses.active = i;
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", tab.g.path.empty() ? "Not saved yet" : tab.g.path.c_str());
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
                            if (tab.g.dirty) { ses.ask = VsSession::Ask::CloseTab; ses.askTab = i; }
                            else s_closeIds.push_back(i);
                        }
                        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 12.0f)) {
                            const float dx = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).x;
                            if (dx > 56.0f && i + 1 < (int)ses.tabs.size()) {
                                std::swap(ses.tabs[(std::size_t)i], ses.tabs[(std::size_t)i + 1]);
                                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
                                VsSaveTabList();
                            } else if (dx < -56.0f && i > 0) {
                                std::swap(ses.tabs[(std::size_t)i], ses.tabs[(std::size_t)i - 1]);
                                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
                                VsSaveTabList();
                            }
                        }
                    }
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Close")) {
                            if (tab.g.dirty) { ses.ask = VsSession::Ask::CloseTab; ses.askTab = i; }
                            else s_closeIds.push_back(i);
                        }
                        if (ImGui::MenuItem("Close others")) { ses.ask = VsSession::Ask::CloseOthers; ses.askTab = i; }
                        if (ImGui::MenuItem("Close all")) ses.ask = VsSession::Ask::CloseAll;
                        if (ImGui::MenuItem("Reopen closed tab", "Ctrl+Shift+T", false, !okay::editor::docrec::ClosedStack().empty()))
                            VsReopenClosedGraph();
                        ImGui::EndPopup();
                    }
                    ImGui::EndTabItem();
                }
                if (!openTab) {
                    if (tab.g.dirty) { ses.ask = VsSession::Ask::CloseTab; ses.askTab = i; }
                    else { s_closeRemember = false; s_closeIds.push_back(i); }
                }
            }
            ses.forceSelect = false;
            if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip)) RequestNewGraph(true);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("New graph");
            ImGui::EndTabBar();
        }
        ImGui::PopStyleVar();
        if (winFocused && !typing && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Tab)) {
            const int n = (int)ses.tabs.size();
            if (n > 1) { ses.active = (ses.active + (io.KeyShift ? n - 1 : 1)) % n; ses.forceSelect = true; }
        }
        if (winFocused && !typing && io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_T)) VsReopenClosedGraph();
        if (winFocused && !typing && io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_W)) {
            if (ses.active >= 0 && ses.active < (int)ses.tabs.size() && ses.tabs[(std::size_t)ses.active]->g.dirty) {
                ses.ask = VsSession::Ask::CloseTab; ses.askTab = ses.active;
            } else if (ses.tabs.size() > 1) { s_closeRemember = false; s_closeIds.push_back(ses.active); }
        }
    }
    VsEditState& st = State();
    VsEditGraph& g = st.g;
    // Names and paths already in the file become references when the scene
    // (or the file extension) tells us what they point at. Older files still run.
    if (VsUpgradeGraph(g, ed.scene())) g.dirty = true;
    // Record exec counts + last values so a running graph can be watched live.
    okay::vs::NodeGraph::Tracing() = true;
    if (!st.trackInit) { st.lastText = g.toText(); st.trackInit = true; }

    auto doUndo = [&] { VsUndo(st); };   // VisualScriptModel.hpp: the same undo the Visual Script app runs
    auto doRedo = [&] { VsRedo(st); };

    // ---- Toolbar: File / Edit / + Add / Templates ... Fit / zoom / View --------------
    // Kept short on purpose: everything else is a shortcut, the right-click menu,
    // or one of these menus.
    auto doSave = [&]() {
        // Saving writes the file and reloads every object that runs this graph,
        // including ones whose path was stored differently and embedded copies.
        if (!PublishOpenGraph(ed, false)) { Log("Save failed", 2); return; }
        if (!st.issues.empty()) st.showIssues = true;
        int n = 0;
        for (const auto& up : ed.scene().Objects())
            for (auto* vc : up->GetComponents<VisualScriptComponent>())
                if (!g.path.empty() && vc->Path() == g.path) ++n;
        Log("Saved visual script " + (g.path.empty() ? std::string("(empty)") : g.path)
            + (n ? " (updated " + std::to_string(n) + " object(s))" : std::string()));
    };
    auto doAttach = [&](GameObject* o) {
        // Save what's on screen first: attaching loads the file, and an unsaved
        // edit would otherwise silently not be what runs.
        BindObject(o);
        if (!PublishOpenGraph(ed, false)) { Log("Couldn't save the graph", 2); return; }
        auto* vc = o->GetComponent<VisualScriptComponent>();
        if (!vc) vc = o->AddComponent<VisualScriptComponent>();
        if (vc->Path() != g.path) vc->LoadFromFile(g.path);
        ed.dirty = true;
        Log("Attached visual script to " + o->name);
    };
    bool addComment = false;
    bool openTpl = false;
    const oked::scriptui::Metrics dm = oked::scriptui::Current();   // Density (Graph > Density): every pane reads this one table
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(dm.framePad.x, dm.framePad.y));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(dm.itemSpacing.x, dm.itemSpacing.y));
    if (ImGui::Button("Graph")) ImGui::OpenPopup("##vsfile");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("New, open, save, undo, align, and the panels");
    if (ImGui::BeginPopup("##vsfile")) {
        if (ImGui::MenuItem("New graph")) RequestNewGraph(true);
        if (ImGui::MenuItem("New empty graph")) RequestNewGraph(false);
        if (ImGui::MenuItem("Save all graphs")) VsSaveAllTabs(ed.projectDir());
        if (ImGui::BeginMenu("Restore previous version", !g.path.empty())) {
            // Filled by the host when backups are wired. The menu stays so the
            // command is visible; the host draws the versions via VsBackupMenu.
            if (VsBackupMenu) VsBackupMenu(g.path);
            else ImGui::TextDisabled("No backups yet.");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Open")) {
            namespace fs = std::filesystem;
            fs::path assets = ed.projectDir().empty() ? fs::path("Assets") : fs::path(ed.projectDir()) / "Assets";
            std::error_code ec; int shown = 0;
            if (fs::exists(assets, ec))
                for (auto& e : fs::recursive_directory_iterator(assets, ec)) {
                    if (!e.is_regular_file() || e.path().extension() != ".okayvs") continue;
                    ++shown;
                    if (ImGui::MenuItem(fs::relative(e.path(), assets, ec).string().c_str())) OpenFile(e.path().string());
                }
            if (!shown) ImGui::TextDisabled("No graphs in Assets yet.");
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Save", "Ctrl+S")) doSave();
        ImGui::Separator();
        GameObject* selObj = ed.selected();
        std::string attachLbl = selObj ? "Attach to " + selObj->name : std::string("Attach to selected object");
        if (ImGui::MenuItem(attachLbl.c_str(), nullptr, false, selObj != nullptr)) doAttach(selObj);
        ImGui::Separator();
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !st.undo.empty())) doUndo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !st.redo.empty())) doRedo();
        if (ImGui::MenuItem("Select all", "Ctrl+A")) { st.sel.clear(); for (const auto& n : g.nodes) st.sel.insert(n.id); }
        if (ImGui::MenuItem("Comment around selection", "C", false, !st.sel.empty())) addComment = true;
        ImGui::MenuItem("Snap to grid", nullptr, &st.snapGrid);
        VsIdeArrangeMenu(st);   // align / distribute / straighten / auto-arrange, collapse to function, find in all graphs
        if (ImGui::MenuItem("Templates...")) st.openTemplates = true;
        ImGui::Separator();
        ImGui::MenuItem("Outline", nullptr, &st.showOutline);
        ImGui::MenuItem("Node details", nullptr, &st.showDetails);
        oked::scriptui::DrawDensityMenu();   // Compact / Comfortable / Spacious, shared with the Script editor
        ImGui::MenuItem("Minimap", nullptr, &st.showMinimap);
        ImGui::MenuItem("Live values while playing", nullptr, &st.liveValues);
        ImGui::MenuItem("Show as OkayScript", nullptr, &st.showScript);
        ImGui::MenuItem("Canvas hint", nullptr, &st.showHint);
        ImGui::MenuItem("Scroll moves the view (trackpad)", nullptr, &st.scrollPans);
        ImGui::Separator();
        ImGui::TextDisabled("Right-click or Space adds a node");
        ImGui::TextDisabled("Scroll zooms (Ctrl+scroll always). Arrow keys, middle/right-drag or Space+drag move the view. Home fits everything. F fits the selection.");
        ImGui::EndPopup();
    }
    if (st.openTemplates) { ImGui::OpenPopup("##vsgraphtemplates"); st.openTemplates = false; }
    ImGui::SetNextWindowSize(ImVec2(560, 620), ImGuiCond_Appearing);
    if (ImGui::BeginPopup("##vsgraphtemplates")) {
        ImGui::TextUnformatted("Templates");
        ImGui::TextDisabled("Replaces this graph. If an object is selected, it is attached and missing parts are added.");
        static char tf[64] = "";
        static int tlevel = 0;
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##tplfind", "Search templates", tf, sizeof(tf));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 2));
        for (int i = 0; i < 4; ++i) {
            if (i) ImGui::SameLine();
            const bool on = tlevel == i;
            if (on) ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 110, 160, 255));
            if (ImGui::Button(VsLevelFilterName((VsLevelFilter)i), ImVec2(120, 0))) tlevel = i;
            if (on) ImGui::PopStyleColor();
        }
        ImGui::PopStyleVar();
        std::string needle = tf;
        for (char& c : needle) c = (char)std::tolower((unsigned char)c);
        ImGui::BeginChild("##tpllist", ImVec2(0, 460), ImGuiChildFlags_Borders);
        const char* group = nullptr;
        for (const VsTemplate& t : VsTemplates()) {
            if (!VsLevelPasses(t.level, (VsLevelFilter)tlevel)) continue;
            std::string hay = std::string(t.name) + " " + t.blurb + " " + t.category;
            for (char& c : hay) c = (char)std::tolower((unsigned char)c);
            if (!needle.empty() && hay.find(needle) == std::string::npos) continue;
            if (!group || std::strcmp(group, t.category) != 0) {
                group = t.category;
                ImGui::Spacing();
                ImGui::TextDisabled("%s", t.category);
            }
            ImGui::PushID(t.name);
            const bool pick = ImGui::Selectable("##tpl", false, 0, ImVec2(0, ImGui::GetTextLineHeight() + 8));
            const ImVec2 p = ImGui::GetItemRectMin();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddText(ImVec2(p.x + 4, p.y + 2), ImGui::GetColorU32(ImGuiCol_Text), t.name);
            const char* badge = VsLevelBadge(t.level);
            const float bw = ImGui::CalcTextSize(badge).x + 10;
            const ImU32 bg = t.level == VsLevel::Basic ? IM_COL32(46, 110, 78, 255)
                           : t.level == VsLevel::Advanced ? IM_COL32(96, 72, 120, 255)
                           : IM_COL32(52, 78, 120, 255);
            dl->AddRectFilled(ImVec2(p.x + 220, p.y + 3), ImVec2(p.x + 220 + bw, p.y + ImGui::GetTextLineHeight() + 4), bg, 3);
            dl->AddText(ImVec2(p.x + 225, p.y + 3), IM_COL32(230, 236, 244, 255), badge);
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(320);
                ImGui::TextUnformatted(t.blurb);
                if (t.needs && t.needs[0]) ImGui::TextDisabled("%s", t.needs);
                // A small sketch of the graph: one block per node, in file order.
                int shown = 0;
                float x = 0;
                for (std::size_t i = 0; i + 5 < t.graph.size() && shown < 8; ++i) {
                    if (t.graph.compare(i, 5, "node ") != 0) continue;
                    dl = ImGui::GetWindowDrawList();
                    const ImVec2 a = ImGui::GetCursorScreenPos();
                    ImGui::Dummy(ImVec2(36, 16));
                    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(a.x + x, a.y), ImVec2(a.x + x + 28, a.y + 14),
                                                              IM_COL32(80, 120, 160, 255), 2);
                    x += 32; ++shown; i += 4;
                }
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            if (pick) {
                g.fromText(t.graph);
                g.path.clear();   // don't overwrite whichever file was open before
                ResetEditState(st);
                st.boundGuids.clear();
                st.frameAll = true;
                g.dirty = true;
                st.publishedText = g.toText();
                if (GameObject* go = ed.selected()) {
                    auto* vs = go->GetComponent<VisualScriptComponent>();
                    if (!vs) vs = go->AddComponent<VisualScriptComponent>();
                    std::string err;
                    vs->LoadFromText(g.toText(), &err);
                    vs->SetPath("");
                    BindObject(go);
                    VsAddTemplateComponents(go, t.add);
                    ed.dirty = true;
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    if (openTpl) ImGui::OpenPopup("##vstemplates");
    if (TemplatePicker) {
        okay::ActionList tmp;
        if (TemplatePicker("##vstemplates", tmp)) {
            st.templateText = okay::vs::ActionListToGraph(tmp);
            st.pasteTemplate = true;
        }
    }
    if (winFocused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) doSave();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.40f, 0.34f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.50f, 0.42f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.34f, 0.28f, 1.0f));
    if (ImGui::Button("Add node")) st.addAtCenter = true;
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add a node in the middle of the view.\nRight-click the canvas, or press Space, to add one where you click.");
    ImGui::SameLine();
    if (ImGui::Button("Save")) doSave();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save this graph (Ctrl+S)");
    ImGui::SameLine();
    if (ImGui::Button("Pick a recipe")) ImGui::OpenPopup("##vsrecipes");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Start here. Drop in a ready-made graph: move with arrow keys, jump, coins and score, a door, an enemy that follows you.");
    if (ImGui::BeginPopup("##vsrecipes")) {
        struct Rec { const char* name; const char* text; };
        static const Rec recs[] = {
            {"Collect coins and score",
             "node 1 OnStart\npos 1 40 40\nentry OnStart 1\nnode 2 ScoreAdd\npos 2 280 40\ndefault 2 0 1\nexec 1 0 2\n"
             "node 3 OnTagEnter coin\npos 3 40 220\nentry OnTagEnter:coin 3\nnode 4 Collect\npos 4 300 220\ndefault 4 0 1\nexec 3 0 4\n"},
            {"Enemy chases and damages",
             "node 1 OnUpdate\npos 1 40 40\nentry OnUpdate 1\nnode 2 FindWithTag Player\npos 2 40 200\nnode 3 Chase\npos 3 300 40\n"
             "exec 1 0 3\ndata 3 0 2 0\nnode 4 OnTagEnter Player\npos 4 40 380\nentry OnTagEnter:Player 4\n"
             "node 5 Damage\npos 5 320 380\ndefault 5 0 10\nexec 4 0 5\n"},
            {"Door with a key",
             "node 1 OnStart\npos 1 40 40\nentry OnStart 1\nnode 2 Const true\npos 2 40 200\nnode 3 SetVar has_key\npos 3 280 40\nexec 1 0 3\ndata 3 0 2 0\n"
             "node 4 OnInteract\npos 4 40 400\nentry OnInteract 4\nnode 5 GetVar has_key\npos 5 40 560\nnode 6 Branch\npos 6 280 400\n"
             "exec 4 0 6\ndata 6 0 5 0\nnode 7 DoorOpen\npos 7 540 400\ndefault 7 0 90\ndefault 7 1 0.35\nexec 6 0 7\n"},
            {"Health pickup",
             "node 1 OnTagEnter Player\npos 1 40 40\nentry OnTagEnter:Player 1\nnode 2 Heal\npos 2 300 40\ndefault 2 0 25\nexec 1 0 2\n"
             "node 3 Destroy\npos 3 540 40\nexec 2 0 3\n"},
            {"Countdown then win",
             "node 1 OnStart\npos 1 40 40\nentry OnStart 1\nnode 2 Countdown 0.4\npos 2 300 40\nexec 1 0 2\nnode 3 Win\npos 3 560 40\nexec 2 0 3\n"},
        };
        // The first things a beginner sees: small ready-made recipes (VisualScriptRecipes.hpp), one click each.
        ImGui::TextDisabled("Start here. One click adds a small working graph.");
        for (const VsRecipe& r : VsRecipes()) {
            if (ImGui::MenuItem(r.name)) {
                st.templateText = r.graph; st.pasteTemplate = true;
                if (GameObject* go = ed.selected()) VsAddTemplateComponents(go, r.add);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(320);
                ImGui::TextUnformatted(r.blurb);
                if (r.needs[0]) ImGui::TextDisabled("%s", r.needs);
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
        ImGui::Separator();
        ImGui::TextDisabled("More recipes");
        for (const Rec& r : recs)
            if (ImGui::MenuItem(r.name)) { st.templateText = r.text; st.pasteTemplate = true; }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    {
        const std::string nm = g.path.empty() ? std::string("Untitled graph")
                                              : std::filesystem::path(g.path).filename().string();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(nm.c_str());
        if (g.dirty) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.93f, 0.78f, 0.45f, 1.0f), "Unsaved"); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", g.path.empty() ? "Not saved yet. Save puts it in Assets." : g.path.c_str());
    }
    // The graph checker: re-run whenever the graph changes (wires that can't
    // work, nodes nothing runs, unset variables, bad key names, missing prefabs).
    {
        VsRefreshChecks(st, g, g.toText(), ed.projectDir());
    }
    // Right side of the one toolbar: problems, fit, zoom, the two panels.
    {
        int nErr = 0, nWarn = 0;
        for (const auto& is : st.issues) (is.severity == 0 ? nErr : nWarn)++;
        // The right cluster is as wide as its buttons (padding follows the Density). When the window is too narrow for all of
        // it beside the left buttons, the Outline / Details buttons drop out (both are in the Graph menu as well) instead of
        // the clusters running into each other.
        auto btnW = [&](const char* t) { return ImGui::CalcTextSize(t, nullptr, true).x + 2.0f * dm.framePad.x + dm.itemSpacing.x; };
        char probLbl[64];
        if (nErr + nWarn == 0) std::snprintf(probLbl, sizeof probLbl, "No problems");
        else std::snprintf(probLbl, sizeof probLbl, "%d problem%s", nErr + nWarn, nErr + nWarn == 1 ? "" : "s");
        const float coreW = btnW(probLbl) + btnW("Fit") + btnW("Pan") + btnW("-") + btnW("100%") + btnW("+");
        const float panelW = btnW("Outline") + btnW("Details");
        const float avail = ImGui::GetWindowContentRegionMax().x;
        const bool panelBtns = avail - ImGui::GetCursorPosX() >= coreW + panelW + 24.0f;
        const float rightW = coreW + (panelBtns ? panelW : 0.0f);
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 16.0f, avail - rightW));
        {
            char lbl[64];
            if (nErr + nWarn == 0) std::snprintf(lbl, sizeof lbl, "No problems##vsissues");
            else std::snprintf(lbl, sizeof lbl, "%d problem%s##vsissues", nErr + nWarn, nErr + nWarn == 1 ? "" : "s");
            const ImVec4 tc = nErr ? ImVec4(1.0f, 0.55f, 0.48f, 1) : nWarn ? ImVec4(0.95f, 0.78f, 0.42f, 1) : ImVec4(0.62f, 0.82f, 0.68f, 1);
            ImGui::PushStyleColor(ImGuiCol_Text, tc);
            if (ImGui::Button(lbl) && nErr + nWarn > 0) st.showIssues = !st.showIssues;
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(nErr + nWarn == 0 ? "The graph checker found nothing wrong." :
                                  "%d error(s), %d warning(s). Click to list them. Click a row to jump to its node.", nErr, nWarn);
            ImGui::SameLine();
        }
        if (ImGui::Button("Fit")) st.frameAll = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show every block (Home, or F with nothing selected). F frames the selected blocks.");
        ImGui::SameLine();
        {   // the hand: while on, dragging with the left button moves the view instead of selecting
            if (st.panTool) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.46f, 0.78f, 1.0f));
            if (ImGui::Button("Pan##vspantool")) st.panTool = !st.panTool;
            if (st.panTool) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Hand tool. While on, drag with the left button to move the view.\nAlways works: middle-drag, right-drag on empty space, hold Space and drag, arrow keys.");
            ImGui::SameLine();
        }
        if (ImGui::Button("-##vszoomout")) st.zoom = std::clamp(st.zoom / 1.12f, 0.28f, 2.2f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zoom out");
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%d%%", (int)std::lround(st.zoom * 100.0f));
        if (ImGui::IsItemClicked()) st.zoom = 1.0f;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scroll to zoom. Click for 100%%.");
        ImGui::SameLine();
        if (ImGui::Button("+##vszoomin")) st.zoom = std::clamp(st.zoom * 1.12f, 0.28f, 2.2f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zoom in");
        ImGui::SameLine();
        if (panelBtns) {
            if (ImGui::Button(st.showOutline ? "Outline" : "Outline##off")) st.showOutline = !st.showOutline;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Events, variables and parameters");
            ImGui::SameLine();
            if (ImGui::Button(st.showDetails ? "Details" : "Details##off")) st.showDetails = !st.showDetails;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("What the selected node does");
        }
    }
    ImGui::PopStyleVar(3);
    // The IDE tools' command picked last frame (menus, the node menu, shortcuts): arrange, collapse, bookmarks, find...
    const std::string vsProjectRoot = !ed.projectDir().empty() ? ed.projectDir()
                                    : g.path.empty() ? std::string(".") : std::filesystem::path(g.path).parent_path().string();
    VsIdeRunPending(st, vsProjectRoot);
    // ---- Debugger: breakpoints, and the bar while the game is frozen at one --------
    VsDebugView& dv = DebugView();
    auto& dbg = okay::vs::VsDebugger::Get();
    auto toggleBreakpoint = [&](int id) {
        const VsEditNode* n = g.find(id);
        if (!n) return;
        const VsDef* d = FindDef(n->type);
        if (!d || !(d->execIn || !VsEntryName(n->type, n->arg).empty() || n->type == "FunctionStart")) {
            Log("Breakpoints go on steps (nodes the flow runs through). Value nodes run when a step reads them.", 1);
            return;
        }
        if (g.path.empty()) { Log("Save the graph to a file first: breakpoints are kept per graph file.", 1); return; }
        dbg.ToggleBreakpoint(g.path, id);
    };
    auto debugGo = [&](okay::vs::VsDebugger::Step s) {
        if (!dv.stopped) return;
        dbg.RequestStep(s);
        dv.resume = true;
    };
    if (dv.stopped) {
        using S = okay::vs::VsDebugger::Step;
        if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) debugGo(S::None);
        if (ImGui::IsKeyPressed(ImGuiKey_F10, false)) debugGo(S::Over);
        if (ImGui::IsKeyPressed(ImGuiKey_F11, false)) debugGo(io.KeyShift ? S::Out : S::Into);
        if (dv.focusPending && dv.key == g.path) { st.focusNode = dv.node; st.sel = {dv.node}; dv.focusPending = false; }
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.30f, 0.24f, 0.08f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 6.0f));
        ImGui::BeginChild("vsdbgbar", ImVec2(0, ImGui::GetFrameHeightWithSpacing() + 18.0f), true, ImGuiWindowFlags_NoScrollbar);
        const VsDef* sd = FindDef(dv.nodeType);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.30f, 1.0f), "Paused");
        ImGui::SameLine();
        ImGui::Text("at %s (#%d) on '%s', %s", sd ? sd->title : dv.nodeType.c_str(), dv.node, dv.object.c_str(),
                    dv.event.empty() ? "?" : dv.event.c_str());
        if (dv.key != g.path) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Open its graph")) {
                if (g.dirty) Log("Save or undo your changes to this graph first: opening " + dv.key + " would replace them.", 1);
                else { OpenFile(dv.key); dv.focusPending = true; }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", dv.key.c_str());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Continue (F5)")) debugGo(S::None);
        ImGui::SameLine();
        if (ImGui::SmallButton("Step (F10)")) debugGo(S::Over);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Run this step and stop at the next one (a Call Function runs whole).");
        ImGui::SameLine();
        if (ImGui::SmallButton("Step Into (F11)")) debugGo(S::Into);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop at the very next step, even inside a function it calls.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Step Out (Shift+F11)")) debugGo(S::Out);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Run until this function returns, then stop.");
        ImGui::SameLine();
        // This window keeps Ctrl+P for itself and may cover the toolbar's Stop.
        if (ImGui::SmallButton("Stop Play")) { ed.Stop(); Log("Stop"); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop the game (the paused frame finishes first, without stopping again).");
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    // Lightweight validation + live status.
    {
        bool hasEvent = false;
        for (const auto& n : g.nodes)
            if (!VsEntryName(n.type, n.arg).empty() || VsStartsOnItsOwn(n.type)) { hasEvent = true; break; }
        st.needsEvent = !g.nodes.empty() && !hasEvent;   // shown on the canvas
    }
    // The running instance of this graph (Play mode): the selected object's if it
    // uses this file, else the first object that does.
    okay::vs::NodeGraph* live = nullptr;
    std::string liveName;
    if (ed.isPlaying() && !g.path.empty()) {
        auto usesThis = [&](GameObject* o) -> VisualScriptComponent* {
            if (!o) return nullptr;
            for (auto* vc : o->GetComponents<VisualScriptComponent>())
                if (vc->Path() == g.path && vc->Graph()) return vc;
            return nullptr;
        };
        VisualScriptComponent* vc = usesThis(ed.selected());
        if (!vc) for (const auto& up : ed.scene().Objects()) if ((vc = usesThis(up.get()))) break;
        if (vc) { live = vc->Graph(); liveName = vc->gameObject ? vc->gameObject->name : std::string(); }
        // Frozen at a breakpoint in this graph: show that object's instance.
        if (dv.stopped && dv.graph && dv.key == g.path) { live = dv.graph; liveName = dv.object; }
    }

    // ---- Keyboard shortcuts (canvas focused, not typing) -------------------------
    if (winFocused && !typing) {
        const bool ctrl = io.KeyCtrl;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) { if (io.KeyShift) doRedo(); else doUndo(); }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) doRedo();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) { st.sel.clear(); for (const auto& n : g.nodes) st.sel.insert(n.id); }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && !st.sel.empty())
            ImGui::SetClipboardText(VsSelectionText(g, st.sel).c_str());
        if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) addComment = true;
        if (ImGui::IsKeyPressed(ImGuiKey_F9, false)) for (int id : std::vector<int>(st.sel.begin(), st.sel.end())) toggleBreakpoint(id);
        VsIdeShortcuts(st);   // Shift+W/A/S/D align, Shift+H/V distribute, Q straighten, Shift+L arrange, Ctrl+B / F2 bookmarks, Shift+F12, Alt+Enter
    }

    // ---- Events list: every event chain in the graph; click one to jump to it ----
    if (st.showOutline) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(dm.windowPad.x, dm.windowPad.y));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(dm.itemSpacing.x, dm.itemSpacing.y * 0.8f));
        ImGui::BeginChild("vsoutline", ImVec2(dm.outlineW, 0), true);
        ImGui::TextUnformatted(kPanelEvents);
        ImGui::Separator();
        int shown = 0;
        for (const auto& n : g.nodes) {
            if (VsEntryName(n.type, n.arg).empty()) continue;
            const VsDef* d = FindDef(n.type);
            std::string lbl = d ? d->title : n.type;
            if (!n.arg.empty() && n.type != "OnStart" && n.type != "OnUpdate") lbl += ": " + (IsKeyNode(n.type) ? VsKeyLabel(n.arg) : n.arg);
            // How many steps hang off it (follow the first exec pin).
            int steps = 0;
            for (int cur = VsExecTarget(g, n.id, 0); cur >= 0 && steps < 99; cur = VsExecTarget(g, cur, 0)) ++steps;
            ImGui::PushID(n.id);
            const bool on = st.sel.count(n.id) > 0;
            if (ImGui::Selectable(lbl.c_str(), on, 0, ImVec2(0, 30))) { st.sel = {n.id}; st.selected = n.id; st.focusNode = n.id; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(kPanelEventTip, steps);
            ImGui::PopID();
            ++shown;
        }
        if (!shown) ImGui::TextWrapped("%s", kPanelNoEvents);
        // The graph's functions (Function nodes), each with how often it's called.
        bool fnHeader = false;
        for (const auto& n : g.nodes) {
            if (n.type != "FunctionStart") continue;
            if (!fnHeader) { ImGui::Spacing(); ImGui::TextUnformatted(kPanelNamedActions); ImGui::Separator(); fnHeader = true; }
            const std::string nm = okay::vs::FunctionNameOf(n.arg);
            int calls = 0;
            for (const auto& m : g.nodes) if (m.type == "CallFunction" && okay::vs::FunctionNameOf(m.arg) == nm) ++calls;
            ImGui::PushID(n.id);
            std::string lbl = "\xC6\x92 " + (nm.empty() ? std::string("(unnamed)") : nm);
            if (ImGui::Selectable(lbl.c_str(), st.sel.count(n.id) > 0, 0, ImVec2(0, 30))) { st.sel = {n.id}; st.selected = n.id; st.focusNode = n.id; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nCalled from %d place(s). Click to jump there.", n.arg.c_str(), calls);
            ImGui::PopID();
        }
        // Breakpoints in this graph: the game stops before these steps in Play.
        if (!g.path.empty()) {
            bool bpHeader = false;
            int removeBp = -1;
            for (const auto& bp : dbg.Breakpoints()) {
                if (bp.first != g.path) continue;
                const VsEditNode* bn = g.find(bp.second);
                if (!bpHeader) { ImGui::Spacing(); ImGui::TextUnformatted(kPanelStopPoints); ImGui::Separator(); bpHeader = true; }
                const VsDef* bd = bn ? FindDef(bn->type) : nullptr;
                std::string lbl = "\xE2\x97\x8F " + std::string(bd ? bd->title : bn ? bn->type.c_str() : kPanelStopGone) +
                                  " #" + std::to_string(bp.second);
                ImGui::PushID(bp.second + 100000);
                if (ImGui::Selectable(lbl.c_str(), st.sel.count(bp.second) > 0) && bn) { st.sel = {bp.second}; st.selected = bp.second; st.focusNode = bp.second; }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kPanelStopTip);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) removeBp = bp.second;
                ImGui::PopID();
            }
            if (removeBp >= 0) dbg.SetBreakpoint(g.path, removeBp, false);
            if (bpHeader && ImGui::SmallButton("Remove all")) {
                std::vector<int> ids;
                for (const auto& bp : dbg.Breakpoints()) if (bp.first == g.path) ids.push_back(bp.second);
                for (int id : ids) dbg.SetBreakpoint(g.path, id, false);
            }
        }
        // Variables and function parameters named in this graph. Click a row to
        // jump to the node that declares or first uses it.
        {
            std::vector<VsVarNode> refs;
            refs.reserve(g.nodes.size());
            for (const auto& n : g.nodes) {
                refs.push_back({n.type, n.arg, n.id});
                if (const okay::vs::QuickBlock* qb = okay::vs::FindQuickBlock(n.type)) {   // a Quick block's Name field is a saved value too
                    const auto fields = okay::vs::QbAllFields(*qb);
                    for (std::size_t fi = 0; fi < fields.size(); ++fi) {
                        if (fields[fi].kind != okay::vs::QbKind::Name) continue;
                        auto it = n.pinDefault.find((int)fi);
                        refs.push_back({"GetVar", okay::vs::QbNameBare(it != n.pinDefault.end() && !it->second.empty() ? it->second : std::string(fields[fi].def)), n.id});
                    }
                }
            }
            // In Play: the running copy of this graph, so each saved value can show what it holds now.
            VisualScriptComponent* liveComp = nullptr;
            if (ed.isPlaying() && !g.path.empty())
                for (const auto& up : ed.scene().Objects()) {
                    if (!up || liveComp) continue;
                    for (auto* vc : up->GetComponents<VisualScriptComponent>()) if (vc->Path() == g.path) { liveComp = vc; break; }
                }
            const std::vector<VsVarRef> vars = VsCollectVariables(refs);
            const std::vector<VsParamRef> params = VsCollectParameters(refs);
            // Nobody has to declare anything: a name typed on a block is made on the spot. The start-value form only
            // shows when the script already has declarations, or when asked for.
            static bool s_showDecls = false;
            if (!g.vars.empty() || !g.structs.empty() || s_showDecls) VsDrawVariableDecls(g);
            else if (ImGui::SmallButton(kPanelStartValues)) s_showDecls = true;
            ImGui::Spacing();
            ImGui::TextUnformatted(kPanelUsed);
            ImGui::Separator();
            if (vars.empty()) ImGui::TextWrapped("%s", kPanelNoneUsed);
            VsVarScope last = VsVarScope::Object;
            bool anyScope = false;
            for (const auto& v : vars) {
                if (!anyScope || v.scope != last) {
                    ImGui::TextDisabled("%s", VsVarScopeName(v.scope));
                    last = v.scope; anyScope = true;
                }
                ImGui::PushID(v.nodeId + 500000 + (int)v.scope);
                std::string rowText = v.name;
                if (liveComp) rowText += "   =   " + okay::vs::VsToLiteral(liveComp->GetVariable(v.name));
                if (ImGui::Selectable(rowText.c_str(), st.sel.count(v.nodeId) > 0, 0, ImVec2(0, 28))) {
                    st.sel = {v.nodeId}; st.selected = v.nodeId; st.focusNode = v.nodeId;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kPanelUsedTip);
                ImGui::PopID();
            }
            ImGui::Spacing();
            // Folded away unless a named action has inputs.
            if (ImGui::CollapsingHeader(kPanelInputs, params.empty() ? 0 : ImGuiTreeNodeFlags_DefaultOpen)) {
            if (params.empty()) ImGui::TextWrapped("%s", kPanelNoInputs);
            for (const auto& p : params) {
                std::string row = p.function;
                if (!p.name.empty()) row += "  " + p.name + (p.type.empty() ? "" : " : " + p.type);
                else row += "  (no inputs)";
                ImGui::PushID(p.nodeId + 800000);
                if (ImGui::Selectable(row.c_str(), st.sel.count(p.nodeId) > 0, 0, ImVec2(0, 28))) {
                    st.sel = {p.nodeId}; st.selected = p.nodeId; st.focusNode = p.nodeId;
                }
                ImGui::PopID();
            }
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::SameLine();
    }

    // Canvas (leaves room on the right for the OkayScript preview when open).
    const float scriptPanelW = 420.0f;
    const float detailsW = 340.0f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.102f, 0.110f, 0.135f, 1.0f));
    const float watchW = 340.0f;   // the debugger's watch panel replaces the preview while paused
    const float rightW = dv.stopped ? watchW : st.showScript ? scriptPanelW : st.showDetails ? detailsW : 0.0f;
    ImGui::BeginChild("vscanvas", ImVec2(rightW > 0.0f ? -rightW : 0.0f, 0), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 canvasMin = origin, canvasMax = ImVec2(origin.x + ImGui::GetContentRegionAvail().x,
                                                  origin.y + ImGui::GetContentRegionAvail().y);
    // The full-canvas catch-all button handles panning, box select and the
    // right-click palette, but it MUST allow overlap: without this it greedily
    // latches the mouse-press (it's submitted before the nodes), so the pin/header
    // buttons drawn on top of it never activate.
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("vscbg", ImVec2(canvasMax.x - canvasMin.x, canvasMax.y - canvasMin.y),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    bool canvasHovered = ImGui::IsItemHovered();
    const bool bgActive = ImGui::IsItemActive();   // the left press began on empty canvas
    if (!st.refHint.empty()) {
        ImGui::SetCursorScreenPos(ImVec2(canvasMin.x + 8, canvasMin.y + 8));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.45f, 1.0f));
        ImGui::TextUnformatted(st.refHint.c_str());
        ImGui::PopStyleColor();
        if (ImGui::IsItemClicked()) st.refHint.clear();
    }
    if (ImGui::BeginDragDropTarget()) {
        const float kL = 1.4f;
        const float wx = (io.MousePos.x - canvasMin.x - st.pan.x) / (st.zoom * kL);
        const float wy = (io.MousePos.y - canvasMin.y - st.pan.y) / (st.zoom * kL);
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("VS_INSPECTOR"))
            VsPlaceInspectorDrop(g, st, wx, wy, (const char*)p->Data);
        if (const ImGuiPayload* gp = ImGui::AcceptDragDropPayload("GO_PTR")) {
            okay::GameObject* go = gp->Data ? *(okay::GameObject**)gp->Data : nullptr;
            if (go) {
                const int nid = g.add("FindObject", wx, wy);
                if (VsEditNode* nn = g.find(nid))
                    nn->arg = go->Guid().empty() ? go->name : okay::vs::VsGuidRef(go->Guid());
                st.selected = nid;
                st.sel = {nid};
                g.dirty = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    const bool bgClicked = ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool canvasWindowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

    // ---- Zoom: wheel zooms about the mouse; Home / Frame All fits the graph ----
    float& z = st.zoom;
    // Node positions are stored in the original (compact) layout units; spread
    // them by the same factor the nodes grew, so saved graphs keep their gaps.
    const float kLayout = 1.4f;
    const ImVec2 mouse = io.MousePos;
    if (canvasWindowHovered && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f) && !ImGui::IsAnyItemActive() &&
        !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        const VsWheelResult wr = VsWheel(io.MouseWheelH, io.MouseWheel, io.KeyCtrl, io.KeyShift, st.scrollPans);
        st.pan.x += wr.panX; st.pan.y += wr.panY;
        if (wr.zoomSteps != 0.0f) {
            float wx = (mouse.x - canvasMin.x - st.pan.x) / z, wy = (mouse.y - canvasMin.y - st.pan.y) / z;
            z = std::clamp(z * std::pow(1.12f, wr.zoomSteps > 0 ? 1.0f : -1.0f), 0.28f, 2.2f);
            st.pan = ImVec2(mouse.x - canvasMin.x - wx * z, mouse.y - canvasMin.y - wy * z);
        }
    }
    if (winFocused && !typing && ImGui::IsKeyPressed(ImGuiKey_Home, false)) st.frameAll = true;
    if (winFocused && !typing && !io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) { if (!st.sel.empty()) st.frameSel = true; else st.frameAll = true; }   // F fits the selection, or everything
    if (st.focusNode >= 0) {   // centre on one node, keeping the zoom readable
        if (const VsEditNode* fn = g.find(st.focusNode)) {
            z = std::max(z, 0.8f);
            st.pan = ImVec2((canvasMax.x - canvasMin.x) * 0.35f - fn->x * kLayout * z,
                            (canvasMax.y - canvasMin.y) * 0.45f - fn->y * kLayout * z);
        }
        st.focusNode = -1;
    }
    if (st.frameAll || st.frameSel) {
        const bool onlySel = st.frameSel && !st.frameAll;
        st.frameAll = st.frameSel = false;
        bool any = false;
        for (const auto& n : g.nodes) if (!onlySel || st.sel.count(n.id)) { any = true; break; }
        if (any) {
            float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
            for (const auto& n : g.nodes) {   // rough node extents in world units
                if (onlySel && !st.sel.count(n.id)) continue;
                x0 = std::min(x0, n.x * kLayout); y0 = std::min(y0, n.y * kLayout);
                x1 = std::max(x1, n.x * kLayout + 340.0f); y1 = std::max(y1, n.y * kLayout + 240.0f);
            }
            float cw = canvasMax.x - canvasMin.x - 60.0f, chh = canvasMax.y - canvasMin.y - 60.0f;
            z = std::clamp(std::min(cw / std::max(1.0f, x1 - x0), chh / std::max(1.0f, y1 - y0)), 0.28f, 1.0f);
            st.pan = ImVec2((canvasMax.x - canvasMin.x) * 0.5f - (x0 + x1) * 0.5f * z,
                            (canvasMax.y - canvasMin.y) * 0.5f - (y0 + y1) * 0.5f * z);
        }
    }
    // Text and widgets follow the zoom.
    ImGui::SetWindowFontScale(z);
    ImFont* font = ImGui::GetFont();
    const float fs = ImGui::GetFontSize();

    // Grid: minor lines + a major line every 4 cells.
    const float grid = 32.0f * z;
    const ImU32 gridMinor = IM_COL32(255, 255, 255, 12), gridMajor = IM_COL32(180, 196, 220, 28);
    {
        int i = (int)std::floor(-st.pan.x / grid);
        for (float x = std::fmod(st.pan.x, grid); x < canvasMax.x - canvasMin.x; x += grid, ++i)
            dl->AddLine(ImVec2(canvasMin.x + x, canvasMin.y), ImVec2(canvasMin.x + x, canvasMax.y), (i % 4 == 0) ? gridMajor : gridMinor);
        int j = (int)std::floor(-st.pan.y / grid);
        for (float y = std::fmod(st.pan.y, grid); y < canvasMax.y - canvasMin.y; y += grid, ++j)
            dl->AddLine(ImVec2(canvasMin.x, canvasMin.y + y), ImVec2(canvasMax.x, canvasMin.y + y), (j % 4 == 0) ? gridMajor : gridMinor);
    }

    // Pan: middle drag, right drag on empty canvas, Space + left drag, the hand button + left drag, arrow keys.
    // A right CLICK / Space TAP without dragging still opens the add menu (VisualScriptPan.hpp decides).
    {
        const bool spaceHeld = winFocused && !typing && ImGui::IsKeyDown(ImGuiKey_Space);
        st.panLeftNow = false; st.spaceOpen = false;
        if (canvasWindowHovered) {
            VsPanFrame pf;
            pf.hovered = true;
            pf.leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left); pf.middleDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
            pf.rightDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
            pf.leftDragging = ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f);
            pf.middleDragging = ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 1.0f);
            pf.rightDragging = ImGui::IsMouseDragging(ImGuiMouseButton_Right, 1.0f);
            pf.rightMaxDrag = std::sqrt(io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Right]);
            pf.dx = io.MouseDelta.x; pf.dy = io.MouseDelta.y;
            pf.pressedOnBackground = bgActive; pf.nodeDragging = st.dragging || VsIdeKnotBusy(st); pf.boxing = st.boxing;
            pf.space = spaceHeld; pf.panTool = st.panTool; pf.shift = io.KeyShift; pf.ctrl = io.KeyCtrl;
            const VsPanOut po = VsPanStep(pf);
            if (po.pans) {
                st.pan.x += po.dx; st.pan.y += po.dy;
                st.panLeftNow = po.leftPan;
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);   // the grab hand while the view moves
            }
        }
        // Space tap versus Space pan
        if (spaceHeld) {
            if (!st.spaceDown) { st.spaceDown = true; st.spaceT = 0.0f; st.spaceMoved = 0.0f; }
            st.spaceT += io.DeltaTime;
            st.spaceMoved += std::sqrt(io.MouseDelta.x * io.MouseDelta.x + io.MouseDelta.y * io.MouseDelta.y);
        } else if (st.spaceDown) {
            st.spaceDown = false;
            st.spaceOpen = VsSpaceOpensPopup(st.spaceT, st.spaceMoved);
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) st.rightT = 0.0f;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) st.rightT += io.DeltaTime;
        // Arrow keys (canvas focused, no text field)
        if (winFocused && !typing && !io.KeyCtrl && !io.KeyAlt) {
            float dx, dy;
            VsKeyPan(ImGui::IsKeyDown(ImGuiKey_LeftArrow), ImGui::IsKeyDown(ImGuiKey_RightArrow), ImGui::IsKeyDown(ImGuiKey_UpArrow),
                     ImGui::IsKeyDown(ImGuiKey_DownArrow), io.KeyShift, io.DeltaTime, dx, dy);
            st.pan.x += dx; st.pan.y += dy;
        }
        // Keep some of the graph on screen (Fit works from anywhere)
        if (!g.nodes.empty()) {
            float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
            for (const auto& n : g.nodes) {
                x0 = std::min(x0, n.x * kLayout); y0 = std::min(y0, n.y * kLayout);
                x1 = std::max(x1, n.x * kLayout + 230.0f); y1 = std::max(y1, n.y * kLayout + 150.0f);
            }
            VsClampPan(st.pan.x, st.pan.y, canvasMax.x - canvasMin.x, canvasMax.y - canvasMin.y, x0, y0, x1, y1, z);
        }
    }

    auto W2S = [&](float wx, float wy) { return ImVec2(canvasMin.x + st.pan.x + wx * kLayout * z, canvasMin.y + st.pan.y + wy * kLayout * z); };
    auto S2W = [&](ImVec2 p) { return ImVec2((p.x - canvasMin.x - st.pan.x) / (z * kLayout), (p.y - canvasMin.y - st.pan.y) / (z * kLayout)); };

    // Compute pin screen positions this frame.
    std::map<long long, ImVec2> pinPos;
    std::map<int, std::pair<ImVec2, ImVec2>> nodeRect;   // screen rect per node (box select, minimap)
    auto pk = [](int node, int kind, int side, int idx) -> long long {
        return ((long long)node << 20) | ((long long)kind << 18) | ((long long)side << 16) | idx;
    };

    // Roomy layout (world units, scaled by the zoom).
    // Sizes come from the Density table (editor/ScriptingDensity.hpp), scaled by the canvas zoom.
    // (`dm` is the Density table read at the top of this function.)
    const float NODE_W_BASE = dm.nodeW * z, HEADER_H = dm.headerH * z, ROW_H = dm.rowH * z, PIN_R = dm.pinR * z;
    const float BOX_W = dm.boxW * z;   // inline value box on an unconnected data input
    const float PAD = dm.nodePad * z, ROUND = dm.round * z;
    auto inputLinked = [&](int node, int pin) {
        for (const auto& l : g.dataLinks) if (l.consumer == node && l.inPin == pin) return true;
        return false;
    };
    auto text = [&](ImVec2 p, ImU32 c, const char* t) { dl->AddText(font, fs, p, c, t); };

    // Layers: 0 = comment frames, 1 = wires, 2 = nodes.
    dl->ChannelsSplit(3);

    // ---- Comment frames -----------------------------------------------------------
    dl->ChannelsSetCurrent(0);
    static const ImU32 kCommentCols[6] = {IM_COL32(90, 110, 140, 255), IM_COL32(80, 130, 90, 255), IM_COL32(150, 110, 60, 255),
                                          IM_COL32(140, 70, 80, 255), IM_COL32(110, 80, 150, 255), IM_COL32(100, 100, 106, 255)};
    int pendingCommentDelete = -1;
    for (int ci = 0; ci < (int)g.comments.size(); ++ci) {
        VsComment& c = g.comments[ci];
        ImVec2 a = W2S(c.x, c.y), b = W2S(c.x + c.w, c.y + c.h);
        ImU32 col = kCommentCols[((c.color % 6) + 6) % 6];
        ImVec4 cv = ImGui::ColorConvertU32ToFloat4(col);
        dl->AddRectFilled(a, b, ImGui::ColorConvertFloat4ToU32(ImVec4(cv.x, cv.y, cv.z, 0.16f)), ROUND);
        dl->AddRect(a, b, ImGui::ColorConvertFloat4ToU32(ImVec4(cv.x, cv.y, cv.z, 0.75f)), ROUND, 0, 1.5f);
        const float th = HEADER_H * 1.05f;
        dl->AddRectFilled(a, ImVec2(b.x, a.y + th), ImGui::ColorConvertFloat4ToU32(ImVec4(cv.x, cv.y, cv.z, 0.55f)), ROUND, ImDrawFlags_RoundCornersTop);
        dl->AddText(font, fs * 1.08f, ImVec2(a.x + PAD, a.y + (th - fs * 1.08f) * 0.5f), IM_COL32(240, 242, 248, 255), c.text.c_str());
        ImGui::PushID(100000 + ci);
        ImGui::SetCursorScreenPos(a);
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##chdr", ImVec2(std::max(10.0f, b.x - a.x), th));
        if (ImGui::IsItemActivated()) {
            st.dragComment = ci;
            st.commentNodes.clear();   // carry the nodes that sit inside the frame
            for (const auto& n : g.nodes)
                if (n.x >= c.x && n.y >= c.y && n.x <= c.x + c.w && n.y <= c.y + c.h) st.commentNodes.push_back(n.id);
        }
        if (st.dragComment == ci && !st.panLeftNow && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            float dx = io.MouseDelta.x / (z * kLayout), dy = io.MouseDelta.y / (z * kLayout);
            c.x += dx; c.y += dy;
            for (int id : st.commentNodes) if (VsEditNode* n = g.find(id)) { n->x += dx; n->y += dy; }
            g.dirty = true;
        }
        if (ImGui::IsItemDeactivated() && st.dragComment == ci) st.dragComment = -1;
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            st.renameComment = ci; std::snprintf(st.renameBuf, sizeof st.renameBuf, "%s", c.text.c_str());
            ImGui::OpenPopup("##crename");
        }
        if (ImGui::BeginPopupContextItem("##cctx")) {
            if (ImGui::MenuItem("Rename")) { st.renameComment = ci; std::snprintf(st.renameBuf, sizeof st.renameBuf, "%s", c.text.c_str()); }
            if (ImGui::BeginMenu("Color")) {
                static const char* names[6] = {"Blue", "Green", "Orange", "Red", "Purple", "Grey"};
                for (int k = 0; k < 6; ++k) if (ImGui::MenuItem(names[k], nullptr, c.color == k)) { c.color = k; g.dirty = true; }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Delete Comment")) pendingCommentDelete = ci;
            ImGui::EndPopup();
        }
        // Resize grip (bottom-right corner).
        const float gs = 14.0f * z;
        dl->AddTriangleFilled(ImVec2(b.x - gs, b.y - 2), ImVec2(b.x - 2, b.y - gs), ImVec2(b.x - 2, b.y - 2),
                              ImGui::ColorConvertFloat4ToU32(ImVec4(cv.x, cv.y, cv.z, 0.9f)));
        ImGui::SetCursorScreenPos(ImVec2(b.x - gs, b.y - gs));
        ImGui::InvisibleButton("##cgrip", ImVec2(gs, gs));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            c.w = std::max(80.0f, c.w + io.MouseDelta.x / (z * kLayout));
            c.h = std::max(50.0f, c.h + io.MouseDelta.y / (z * kLayout));
            g.dirty = true;
        }
        ImGui::PopID();
    }
    if (st.renameComment >= 0 && st.renameComment < (int)g.comments.size()) {
        if (!ImGui::IsPopupOpen("##crename")) ImGui::OpenPopup("##crename");
        if (ImGui::BeginPopup("##crename")) {
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(260);
            if (ImGui::InputText("##ctext", st.renameBuf, sizeof st.renameBuf, ImGuiInputTextFlags_EnterReturnsTrue)) {
                g.comments[st.renameComment].text = st.renameBuf; g.dirty = true;
                st.renameComment = -1; ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("OK")) { g.comments[st.renameComment].text = st.renameBuf; g.dirty = true; st.renameComment = -1; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        } else st.renameComment = -1;
    }
    if (pendingCommentDelete >= 0) { g.comments.erase(g.comments.begin() + pendingCommentDelete); g.dirty = true; }
    if (addComment) {   // VsIdeModel.hpp: the same frame the Visual Script app makes
        const ImVec2 w = S2W(ImVec2((canvasMin.x + canvasMax.x) * 0.5f, (canvasMin.y + canvasMax.y) * 0.5f));
        VsCommentAround(st, w.x, w.y);
    }

    // ---- Live debugging: which nodes ran, and their latest values --------------
    const float dtSec = io.DeltaTime;
    for (auto& kv : st.flash) kv.second = std::max(0.0f, kv.second - dtSec * 2.5f);
    auto liveNode = [&](int fileId) -> okay::vs::VsNode* {
        if (!live) return nullptr;
        int rid = live->NodeByFileId(fileId);
        return rid >= 0 ? live->Node(rid) : nullptr;
    };

    // A function node's pins follow its signature. Wires to pins it doesn't
    // have right now (mid-edit of the signature) are kept, not deleted: they come
    // back when the pin does, and the checker reports them meanwhile.
    struct VsBadge { ImVec2 c; ImU32 col; std::string tips; };
    std::vector<VsBadge> badges;   // checker findings, drawn over the nodes
    // The object this graph is attached to, so a node can warn about a missing component.
    GameObject* vsOwner = nullptr;
    if (!g.path.empty()) {
        for (const auto& up : ed.scene().Objects()) {
            if (!up) continue;
            for (auto* vc : up->GetComponents<VisualScriptComponent>())
                if (vc->Path() == g.path) {
                    if (up.get() == ed.selected()) vsOwner = up.get();
                    else if (!vsOwner) vsOwner = up.get();
                }
        }
    }
    // The Compare window's changes (outlined on the blocks) and the path the run took this frame (lit wires).
    const VsGraphDiff ideDiff = VsIdeDiffNow(st);
    const std::set<int> ideDiffAdded = ideDiff.addedIds(), ideDiffChanged = ideDiff.changedIds();
    // ---- Nodes ----------------------------------------------------------------------
    dl->ChannelsSetCurrent(2);
    int pendingDelete = -1, pendingDup = -1;
    bool startNodeDrag = false;
    // "Connect to..." from a node's right-click menu: add this node (def) wired to one pin, or open the full list for it.
    struct PendingConnect { bool on = false, openAll = false, exec = false, fromOutput = false; int node = -1, pin = -1; const VsDef* def = nullptr; ImVec2 at{0, 0}; } pconn;
    for (auto& n : g.nodes) {
        const VsDef* d = VsDefFor(g, n);
        if (!d) continue;
        int leftRows  = (d->execIn ? 1 : 0) + (int)d->dataIn.size();
        int rightRows = (int)d->execOut.size() + (int)d->dataOut.size();
        int rows = std::max(leftRows, rightRows);
        std::string inlineMsg;
        bool inlineErr = false;
        for (const auto& is : st.issues) if (is.node == n.id) {
            if (is.severity == 0) { inlineMsg = VsPlainIssue(is.message); inlineErr = true; break; }
            if (inlineMsg.empty()) inlineMsg = VsPlainIssue(is.message);
        }
        const bool isSwitch = IsSwitchNode(n.type);
        bool targetWired = false;
        for (int ti = 0; ti < (int)d->dataIn.size(); ++ti)
            if (d->dataIn[ti] && std::strcmp(d->dataIn[ti], "Target") == 0) {
                for (const auto& l : g.dataLinks) if (l.consumer == n.id && l.inPin == ti) targetWired = true;
                auto pit = n.pinDefault.find(ti);
                if (pit != n.pinDefault.end() && !pit->second.empty()) targetWired = true;
            }
        const VsReqGap reqGap = VsRequirementGap(vsOwner, VsNodeDocFor(n.type), targetWired);
        const bool showAdd = reqGap.missing && inlineMsg.empty();
        if (showAdd) inlineMsg = "Needs " + reqGap.addId;
        const float msgH = inlineMsg.empty() ? 0.0f : (showAdd ? ROW_H * 2.4f : ROW_H * 1.35f);
        float bodyH = rows * ROW_H + (d->param ? ROW_H + 10.0f * z : 0) + (isSwitch ? ROW_H : 0.0f) + PAD;
        float h = HEADER_H + bodyH + msgH;
        // Wider when it has data inputs: their value boxes sit between the input
        // labels and the output labels.
        float inLabelW = 0.0f, outLabelW = 0.0f;
        for (const char* lbl : d->dataIn) inLabelW = std::max(inLabelW, ImGui::CalcTextSize(lbl).x);
        for (const char* lbl : d->dataOut) outLabelW = std::max(outLabelW, ImGui::CalcTextSize(lbl).x);
        for (const char* lbl : d->execOut) outLabelW = std::max(outLabelW, ImGui::CalcTextSize(lbl).x);
        const std::string title = VsNodeTitle(n, *d);
        const bool isStep = d->param >= 3 && d->param <= 5;   // Action / Action Steps / Check
        const std::string stepLine = isStep ? VsStepLine(n) : std::string();
        float titleTextW = ImGui::CalcTextSize(title.c_str()).x;
        if (isStep) titleTextW = std::max(titleTextW, std::min(ImGui::CalcTextSize(stepLine.c_str()).x, 320.0f * z));
        // The width rule (title + the status badge beside it, input labels + value box + output labels) is in the header.
        oked::scriptui::NodeWidthIn nwi;
        nwi.zoom = z; nwi.titleW = titleTextW; nwi.badgeW = ImGui::CalcTextSize("Needs input").x + 10.0f * z;
        nwi.hasDataIn = !d->dataIn.empty(); nwi.inLabelW = inLabelW; nwi.outLabelW = outLabelW; nwi.boxW = BOX_W;
        const float NODE_W = oked::scriptui::NodeWidth(dm, nwi);
        ImVec2 p0 = W2S(n.x, n.y), p1 = ImVec2(p0.x + NODE_W, p0.y + h);
        nodeRect[n.id] = {p0, p1};
        const bool hovered = ImGui::IsMouseHoveringRect(p0, p1) && canvasWindowHovered;
        const bool isSel = st.sel.count(n.id) > 0 || st.selected == n.id;

        // Live: flash when it ran since the last frame.
        okay::vs::VsNode* ln = liveNode(n.id);
        if (ln) {
            unsigned& seen = st.seenExec[n.id];
            if (ln->execCount != seen) { st.flash[n.id] = 1.0f; seen = ln->execCount; }
        } else if (live && okay::vs::FindQuickBlock(n.type)) {
            // A Quick block is one node here but several in the running graph: light it when any of them ran.
            const unsigned ran = live->BlockExecCount(n.id);
            unsigned& seen = st.seenExec[n.id];
            if (ran != seen) { st.flash[n.id] = 1.0f; seen = ran; }
        }
        const float fl = (live && st.flash.count(n.id)) ? st.flash[n.id] : 0.0f;

        // Shadow, body, header.
        dl->AddRectFilled(ImVec2(p0.x + 3 * z, p0.y + 5 * z), ImVec2(p1.x + 3 * z, p1.y + 5 * z), IM_COL32(0, 0, 0, 90), ROUND);
        if (fl > 0.0f)
            dl->AddRect(ImVec2(p0.x - 4 * z, p0.y - 4 * z), ImVec2(p1.x + 4 * z, p1.y + 4 * z),
                        IM_COL32(255, 200, 80, (int)(220 * fl)), ROUND + 4 * z, 0, 3.0f * z);
        // Checker findings for this node: red (error) / amber (warning) dot on the
        // header, the messages on hover.
        {
            int worst = 2; std::string tips;
            for (const auto& is : st.issues)
                if (is.node == n.id) { worst = std::min(worst, is.severity); tips += (tips.empty() ? "" : "\n") + std::string(is.severity == 0 ? "Error: " : "Warning: ") + VsPlainIssue(is.message); }
            if (worst < 2) {
                const ImVec2 bc(p1.x - 12.0f * z, p0.y + HEADER_H * 0.5f);
                const ImU32 col = worst == 0 ? IM_COL32(255, 85, 75, 255) : IM_COL32(240, 185, 70, 255);
                dl->AddRect(ImVec2(p0.x - 2 * z, p0.y - 2 * z), ImVec2(p1.x + 2 * z, p1.y + 2 * z), col, ROUND + 2 * z, 0, 2.0f * z);
                badges.push_back({bc, col, tips});
            }
        }
        VsIdeNodeDecor(dl, st, n, p0, p1, z, kLayout, ideDiffAdded, ideDiffChanged);   // its size, a bookmark tag, Compare outlines
        const bool pausedHere = dv.stopped && dv.key == g.path && dv.node == n.id;
        if (pausedHere)
            dl->AddRect(ImVec2(p0.x - 7 * z, p0.y - 7 * z), ImVec2(p1.x + 7 * z, p1.y + 7 * z),
                        IM_COL32(255, 205, 70, 255), ROUND + 7 * z, 0, 4.0f * z);
        const bool errNode = live && live->LastErrorNode() == n.id && !live->LastError().empty();
        if (errNode)
            dl->AddRect(ImVec2(p0.x - 5 * z, p0.y - 5 * z), ImVec2(p1.x + 5 * z, p1.y + 5 * z),
                        IM_COL32(255, 90, 80, 255), ROUND + 5 * z, 0, 3.0f * z);
        dl->AddRectFilled(p0, p1, IM_COL32(30, 33, 42, 248), ROUND);
        ImU32 hc = CatColor(d->cat);
        dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + HEADER_H), hc, ROUND, ImDrawFlags_RoundCornersTop);
        dl->AddLine(ImVec2(p0.x, p0.y + HEADER_H), ImVec2(p1.x, p0.y + HEADER_H), IM_COL32(0, 0, 0, 120), 1.0f);
        if (isSel) {
            dl->AddRect(ImVec2(p0.x - 2 * z, p0.y - 2 * z), ImVec2(p1.x + 2 * z, p1.y + 2 * z), IM_COL32(120, 170, 255, 255), ROUND + 2 * z, 0, 2.5f);
        } else {
            dl->AddRect(p0, p1, hovered ? IM_COL32(150, 154, 166, 200) : IM_COL32(0, 0, 0, 170), ROUND, 0, hovered ? 1.5f : 1.0f);
        }
        {   // a title longer than the widest node is cut with "..." (the full name is in the node's help)
            const float titleRoom = NODE_W - PAD * 2.0f - (ImGui::CalcTextSize("Needs input").x + 10.0f * z + PAD * 0.5f);
            const std::string shownTitle = oked::scriptui::ElideToWidth(title, titleRoom,
                [](const std::string& t) { return ImGui::CalcTextSize(t.c_str()).x; });
            text(ImVec2(p0.x + PAD, p0.y + 6.0f * z), IM_COL32(244, 246, 250, 255), shownTitle.c_str());
        }
        {
            const std::string full = VsPlainLineCached(*d);   // one plain sentence for every node (VisualScriptGuide.hpp)
            std::string hint = full;
            const float maxW = NODE_W - PAD * 2.0f - 78.0f * z;
            while (hint.size() > 8 && ImGui::CalcTextSize(hint.c_str()).x > maxW) hint.pop_back();
            if (hint.size() < full.size()) hint += "...";
            bool hasErr = false;
            std::string miss;
            for (const auto& is : st.issues) if (is.node == n.id && is.severity == 0) hasErr = true;
            auto pinFilled = [&](int pin) {
                if (inputLinked(n.id, pin)) return true;
                auto it = n.pinDefault.find(pin);
                return it != n.pinDefault.end() && !it->second.empty();
            };
            VsReady ready = VsNodeReadiness(n.type, !n.arg.empty(), pinFilled, hasErr, &miss);
            if (ready != VsReady::Error) {   // an empty input the node cannot work without: say so on the node, not just on hover
                for (int pi = 0; pi < (int)d->dataIn.size(); ++pi)
                    if (VsPinNeedsInput(*d, pi) && !pinFilled(pi)) {
                        ready = VsReady::Missing;
                        if (miss.empty() || miss.rfind("Required: ", 0) == 0) { miss = VsMissingHint(*d, pi) + "."; miss[0] = (char)std::toupper((unsigned char)miss[0]); }
                        break;
                    }
            }
            const char* badge = ready == VsReady::Missing ? "Needs input" : VsReadyName(ready);
            const ImVec2 bts = ImGui::CalcTextSize(badge);
            const ImVec2 br(p1.x - PAD - bts.x - 10.0f * z, p0.y + 5.0f * z);
            const ImU32 bg = ready == VsReady::Error ? IM_COL32(150, 48, 44, 255)
                           : ready == VsReady::Missing ? IM_COL32(140, 96, 32, 255)
                           : IM_COL32(36, 110, 72, 255);
            dl->AddRectFilled(br, ImVec2(br.x + bts.x + 10.0f * z, br.y + bts.y + 4.0f * z), bg, 4.0f * z);
            text(ImVec2(br.x + 5.0f * z, br.y + 2.0f * z), IM_COL32(255, 248, 240, 255), badge);
            // Under the title: the plain sentence, or (when an input is empty) what to plug in, in amber.
            if (ready == VsReady::Missing && !miss.empty())
                text(ImVec2(p0.x + PAD, p0.y + 8.0f * z + fs), IM_COL32(255, 196, 140, 255), miss.c_str());
            else if (!hint.empty())
                text(ImVec2(p0.x + PAD, p0.y + 8.0f * z + fs), IM_COL32(230, 236, 244, 210), hint.c_str());
        }
        if (!g.path.empty() && dbg.HasBreakpoint(g.path, n.id)) {   // breakpoint: a red dot left of the header
            const ImVec2 bc(p0.x - 11.0f * z, p0.y + HEADER_H * 0.5f);
            dl->AddCircleFilled(bc, 6.5f * z, IM_COL32(225, 60, 55, 255));
            dl->AddCircle(bc, 6.5f * z, IM_COL32(20, 20, 20, 220), 0, 1.5f * z);
        }
        if (pausedHere) {   // the game is frozen right before this step
            const float ay = p0.y + HEADER_H * 0.5f, ax = p0.x - 24.0f * z;
            dl->AddTriangleFilled(ImVec2(ax - 10 * z, ay - 8 * z), ImVec2(ax - 10 * z, ay + 8 * z), ImVec2(ax + 2 * z, ay),
                                  IM_COL32(255, 205, 70, 255));
        }

        // Node drag handle: an invisible button over the header.
        ImGui::SetCursorScreenPos(p0);
        ImGui::PushID(n.id);
        ImGui::InvisibleButton("##hdr", ImVec2(NODE_W, HEADER_H));
        if (ImGui::IsItemActivated()) {
            if (io.KeyCtrl || io.KeyShift) {   // toggle in the selection
                if (st.sel.count(n.id)) st.sel.erase(n.id); else st.sel.insert(n.id);
            } else if (!st.sel.count(n.id)) { st.sel.clear(); st.sel.insert(n.id); }
            st.selected = n.id;
        }
        if (!st.panLeftNow && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            startNodeDrag = true;   // move every selected node together (after the loop)
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !ImGui::IsItemActive())
            VsNodeTooltip(title.c_str(), VsNodeDocFor(n.type), d);
        if (ImGui::BeginPopupContextItem("##nctx")) {
            if (!st.sel.count(n.id)) { st.sel.clear(); st.sel.insert(n.id); st.selected = n.id; }
            // Applied after the loop: the node list mustn't change while it's iterated.
            if (ImGui::MenuItem("Duplicate", "Ctrl+D")) pendingDup = n.id;
            if (ImGui::MenuItem("Copy", "Ctrl+C")) ImGui::SetClipboardText(VsSelectionText(g, st.sel).c_str());
            if (ImGui::MenuItem("Frame in Comment", "C")) addComment = true;
            const bool hasBp = !g.path.empty() && dbg.HasBreakpoint(g.path, n.id);
            if (ImGui::MenuItem(hasBp ? "Remove Breakpoint" : "Add Breakpoint", "F9")) toggleBreakpoint(n.id);
            if (ImGui::MenuItem("Break All Wires")) {
                for (int k = 0; k < 2; ++k) for (int sd = 0; sd < 2; ++sd) for (int pi = 0; pi < 8; ++pi) g.unlinkPin(n.id, k, sd, pi);
            }
            // What can plug into this node: one entry per pin, listing the nodes that fit it (the same rule as a wire
            // dropped in empty space). Pick one to add it already wired, or open the full list for that pin.
            if (ImGui::BeginMenu("Connect to...")) {
                const ImVec2 outAt = S2W(ImVec2(p0.x + NODE_W + 50.0f * z, p0.y));
                ImVec2 inAt = S2W(ImVec2(p0.x - NODE_W_BASE - 50.0f * z, p0.y));
                inAt.x += 170.0f;   // a new node feeding an input is shifted left by the same amount when it is added
                auto pinMenu = [&](const std::string& label, bool exec, bool fromOutput, int pin, okay::vs::PinType pt) {
                    if (!ImGui::BeginMenu(label.c_str())) return;
                    const std::vector<const VsDef*>& fit = VsNodesFittingPin(exec, fromOutput, pt);
                    if (fit.empty()) ImGui::TextDisabled("Nothing fits this pin.");
                    const int shown = std::min((int)fit.size(), 16);
                    for (int i = 0; i < shown; ++i) {
                        if (ImGui::MenuItem(fit[(std::size_t)i]->title && fit[(std::size_t)i]->title[0] ? fit[(std::size_t)i]->title : fit[(std::size_t)i]->type)) {
                            pconn.on = true; pconn.def = fit[(std::size_t)i];
                            pconn.exec = exec; pconn.fromOutput = fromOutput; pconn.node = n.id; pconn.pin = pin;
                            pconn.at = fromOutput ? outAt : inAt;
                        }
                    }
                    if (!fit.empty()) {
                        ImGui::Separator();
                        const std::string all = "Search all " + std::to_string(fit.size()) + " that fit...";
                        if (ImGui::MenuItem(all.c_str())) {
                            pconn.on = true; pconn.openAll = true; pconn.def = nullptr;
                            pconn.exec = exec; pconn.fromOutput = fromOutput; pconn.node = n.id; pconn.pin = pin;
                            pconn.at = fromOutput ? outAt : inAt;
                        }
                    }
                    ImGui::EndMenu();
                };
                ImGui::TextDisabled("Pick a pin to see what fits it");
                bool any = false;
                if (d->execIn) { pinMenu("Flow in  <-  what runs this node", true, false, 0, okay::vs::PinType::Any); any = true; }
                for (int j = 0; j < (int)d->execOut.size(); ++j) {
                    const char* lbl = d->execOut[(std::size_t)j];
                    pinMenu(std::string("Flow out  ->  ") + (lbl && *lbl ? lbl : "Next"), true, true, j, okay::vs::PinType::Any); any = true;
                }
                for (int i = 0; i < (int)d->dataIn.size(); ++i) {
                    const okay::vs::PinType pt = VsInPinType(g, n.id, i);
                    pinMenu(std::string("Value in  <-  ") + d->dataIn[(std::size_t)i] + " (" + okay::vs::PinTypeName(pt) + ")", false, false, i, pt); any = true;
                }
                for (int o = 0; o < (int)d->dataOut.size(); ++o) {
                    const okay::vs::PinType pt = VsOutPinType(g, n.id, o);
                    pinMenu(std::string("Value out  ->  ") + d->dataOut[(std::size_t)o] + " (" + okay::vs::PinTypeName(pt) + ")", false, true, o, pt); any = true;
                }
                if (!any) ImGui::TextDisabled("This node has no pins.");
                ImGui::EndMenu();
            }
            VsIdeNodeMenu(st, n);   // quick fixes, find references, bookmark, watch values, arrange, collapse to function
            ImGui::Separator();
            if (ImGui::MenuItem("Delete", "Del")) pendingDelete = n.id;
            ImGui::EndPopup();
        }

        const ImU32 labelCol = IM_COL32(205, 209, 216, 255);
        const ImU32 execCol = IM_COL32(235, 236, 240, 255), dataCol = IM_COL32(120, 200, 140, 255);
        float ly = p0.y + HEADER_H + PAD * 0.5f;
        auto execTri = [&](ImVec2 pp) {
            float r = PIN_R * 1.15f;
            dl->AddTriangleFilled(ImVec2(pp.x - r * 0.8f, pp.y - r), ImVec2(pp.x - r * 0.8f, pp.y + r), ImVec2(pp.x + r, pp.y), execCol);
        };
        auto dataDot = [&](ImVec2 pp, bool linked, ImU32 col = 0) {
            if (!col) col = dataCol;
            dl->AddCircleFilled(pp, PIN_R, linked ? col : IM_COL32(38, 39, 44, 255), 16);
            dl->AddCircle(pp, PIN_R, col, 16, 2.0f * z);
        };
        // Left pins (exec-in then data-in).
        int li = 0;
        if (d->execIn) {
            ImVec2 pp(p0.x, ly + li * ROW_H + ROW_H * 0.5f);
            execTri(pp);
            pinPos[pk(n.id, 0, 0, 0)] = pp;
            ++li;
        }
        for (int i = 0; i < (int)d->dataIn.size(); ++i, ++li) {
            ImVec2 pp(p0.x, ly + li * ROW_H + ROW_H * 0.5f);
            const bool linked = inputLinked(n.id, i);
            const okay::vs::PinType pt = VsInPinType(g, n.id, i);
            bool badPin = false;
            for (const auto& l : g.dataLinks)
                if (l.consumer == n.id && l.inPin == i && !VsWireOk(g, l.consumer, l.inPin, l.producer, l.outPin)) badPin = true;
            for (const auto& is : st.issues)
                if (is.node == n.id && is.pin == i && is.pinDir == 0 && is.severity == 0) badPin = true;
            dataDot(pp, linked || badPin, badPin ? IM_COL32(255, 72, 64, 255) : VsPinColor(pt));
            const bool req = VsPinNeedsInput(*d, i) && !linked && n.pinDefault.count(i) == 0;
            if (req) dl->AddCircle(pp, PIN_R + 3.0f * z, IM_COL32(255, 80, 70, 255), 16, 2.0f * z);
            text(ImVec2(p0.x + PAD, pp.y - fs * 0.5f), labelCol, d->dataIn[i]);
            pinPos[pk(n.id, 1, 0, i)] = pp;
            // Nothing wired in: type the value right here (text, number, true/false).
            if (!linked) {
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5 * z, 3 * z));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4 * z);
                const float boxH = fs + 6 * z;
                ImGui::SetCursorScreenPos(ImVec2(p0.x + PAD + inLabelW + dm.pinLabelGap * z, pp.y - boxH * 0.5f));
                const okay::vs::VsRefKind rk = VsPinKind(n.type, i, pt, d->dataIn[i]);
                std::string& dv = n.pinDefault[i];
                const okay::vs::QuickBlock* qbk = okay::vs::FindQuickBlock(n.type);
                const auto qfields = qbk ? okay::vs::QbAllFields(*qbk) : std::vector<okay::vs::QbField>();
                if (qbk && i < (int)qfields.size() && qfields[(std::size_t)i].kind != okay::vs::QbKind::Text) {
                    if (VsDrawQuickField(ed, g, n, *qbk, i, dv, BOX_W)) g.dirty = true;
                    if (dv.empty()) n.pinDefault.erase(i);
                } else if (rk != okay::vs::VsRefKind::None) {
                    const bool selfDefault = okay::vs::TargetPinFor(n.type) == i;
                    char bid[16]; std::snprintf(bid, sizeof(bid), "pd%d", i);
                    if (VsDrawRefSlot(ed, st, dv, rk, selfDefault, BOX_W, bid)) g.dirty = true;
                    if (dv.empty()) n.pinDefault.erase(i);
                } else {
                    ImGui::SetNextItemWidth(BOX_W);
                    char vb[128]; std::snprintf(vb, sizeof(vb), "%s", dv.c_str());
                    char bid[16]; std::snprintf(bid, sizeof(bid), "##pd%d", i);
                    const bool needsHere = VsPinNeedsInput(*d, i);
                    if (ImGui::InputTextWithHint(bid, needsHere ? "needed" : "value", vb, sizeof(vb))) { dv = vb; g.dirty = true; }
                    if (ImGui::IsItemHovered()) {
                        if (needsHere) ImGui::SetTooltip("%s: %s. Or type it here.", d->dataIn[i], VsMissingHint(*d, i).c_str());
                        else ImGui::SetTooltip("%s: type a value (text, a number, true/false), or wire a node in", d->dataIn[i]);
                    }
                    if (dv.empty()) n.pinDefault.erase(i);
                }
                ImGui::PopStyleVar(2);
            }
        }
        // Right pins (exec-out then data-out).
        int ri = 0;
        for (int i = 0; i < (int)d->execOut.size(); ++i, ++ri) {
            ImVec2 pp(p1.x, ly + ri * ROW_H + ROW_H * 0.5f);
            execTri(pp);
            if (d->execOut[i][0]) {
                ImVec2 ts = ImGui::CalcTextSize(d->execOut[i]);
                text(ImVec2(p1.x - PAD - ts.x, pp.y - fs * 0.5f), labelCol, d->execOut[i]);
            }
            pinPos[pk(n.id, 0, 1, i)] = pp;
        }
        for (int i = 0; i < (int)d->dataOut.size(); ++i, ++ri) {
            ImVec2 pp(p1.x, ly + ri * ROW_H + ROW_H * 0.5f);
            bool used = false, badPin = false;
            for (const auto& l : g.dataLinks) if (l.producer == n.id && l.outPin == i) {
                used = true;
                if (!VsWireOk(g, l.consumer, l.inPin, l.producer, l.outPin)) badPin = true;
            }
            for (const auto& is : st.issues)
                if (is.node == n.id && is.pin == i && is.pinDir == 1 && is.severity == 0) badPin = true;
            const okay::vs::PinType pt = VsOutPinType(g, n.id, i);
            dataDot(pp, used || badPin, badPin ? IM_COL32(255, 72, 64, 255) : VsPinColor(pt));
            ImVec2 ts = ImGui::CalcTextSize(d->dataOut[i]);
            text(ImVec2(p1.x - PAD - ts.x, pp.y - fs * 0.5f), labelCol, d->dataOut[i]);
            pinPos[pk(n.id, 1, 1, i)] = pp;
            // Live value bubble beside the output pin.
            if (st.liveValues && ln && i < (int)ln->lastOut.size()) {
                const std::string v = VsLiveValueText(ln->lastOut[i]);   // the watch list and wire tips read the same (VsDebugWatch.hpp)
                ImVec2 vs2 = ImGui::CalcTextSize(v.c_str());
                ImVec2 bp(pp.x + PIN_R + 6 * z, pp.y - vs2.y * 0.5f - 2 * z);
                dl->AddRectFilled(bp, ImVec2(bp.x + vs2.x + 10 * z, bp.y + vs2.y + 4 * z), IM_COL32(20, 60, 34, 230), 4 * z);
                text(ImVec2(bp.x + 5 * z, bp.y + 2 * z), IM_COL32(150, 240, 170, 255), v.c_str());
            }
        }

        // Param editor (inline) in the body.
        if (d->param) {
            float py = p0.y + HEADER_H + PAD * 0.5f + std::max(leftRows, rightRows) * ROW_H + 4 * z;
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6 * z, 4 * z));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4 * z);
            ImGui::SetCursorScreenPos(ImVec2(p0.x + PAD, py));
            ImGui::SetNextItemWidth(NODE_W - PAD * 2);
            char buf[96]; std::snprintf(buf, sizeof(buf), "%s", n.arg.c_str());
            if (isStep) {
                // A step node: its arguments as a sentence; click to edit with the
                // same friendly fields the Actions component had (no typing code).
                const bool cond = d->param == 5;
                std::string shown = stepLine;
                const float maxW = NODE_W - PAD * 2 - 12 * z;
                while (shown.size() > 4 && ImGui::CalcTextSize(shown.c_str()).x > maxW) shown = shown.substr(0, shown.size() - 4) + "..";
                if (ImGui::Button((shown + "##stepbtn").c_str(), ImVec2(NODE_W - PAD * 2, 0))) ImGui::OpenPopup("##stepedit");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to edit");
                ImGui::SetNextWindowSizeConstraints(ImVec2(440, 0), ImVec2(720, 520));
                if (ImGui::BeginPopup("##stepedit")) {
                    ImGui::SetWindowFontScale(1.0f);
                    if (d->param == 4) {   // a list of steps
                        auto list = okay::ActionList::DecodeItems(n.arg);
                        ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "Steps (run top to bottom; Wait pauses)");
                        ImGui::Separator();
                        bool ch = false; int act = 0, actAt = -1;
                        for (std::size_t i = 0; i < list.size(); ++i) {
                            ImGui::TextDisabled("%d", (int)i + 1); ImGui::SameLine(26);
                            int a = StepEditor ? StepEditor(list[i], false, (int)i, ch) : 0;
                            if (a) { act = a; actAt = (int)i; }
                        }
                        if (act == 1 && actAt >= 0) { list.erase(list.begin() + actAt); ch = true; }
                        if (act == 2 && actAt > 0) { std::swap(list[actAt], list[actAt - 1]); ch = true; }
                        if (act == 3 && actAt >= 0 && actAt + 1 < (int)list.size()) { std::swap(list[actAt], list[actAt + 1]); ch = true; }
                        if (ImGui::Button("+ Add step")) { list.push_back({"log", {"Hello"}}); ch = true; }
                        if (ch) { n.arg = okay::ActionList::EncodeItems(list); g.dirty = true; }
                    } else {
                        okay::ActionList::Item it = okay::ActionList::DecodeItem(n.arg);
                        if (it.op.empty()) it.op = cond ? "always" : "log";
                        ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), cond ? "Check" : "Action");
                        ImGui::Separator();
                        bool ch = false;
                        if (StepEditor) StepEditor(it, cond, 0, ch);
                        if (ch) { n.arg = okay::ActionList::EncodeItem(it); g.dirty = true; }
                    }
                    ImGui::SetWindowFontScale(1.0f);
                    ImGui::EndPopup();
                }
            } else if (d->param == 6) {   // pick one of this graph's functions
                const std::string cur = okay::vs::FunctionNameOf(n.arg);
                if (ImGui::BeginCombo("##fn", cur.empty() ? "(pick a function)" : cur.c_str())) {
                    bool any = false;
                    for (const auto& m : g.nodes) {
                        if (m.type != "FunctionStart") continue;
                        const std::string nm = okay::vs::FunctionNameOf(m.arg);
                        if (nm.empty()) continue;
                        any = true;
                        if (ImGui::Selectable(nm.c_str(), nm == cur)) { n.arg = nm; g.dirty = true; }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", m.arg.c_str());
                    }
                    if (!any) ImGui::TextDisabled("No functions yet: add a Function node first");
                    ImGui::EndCombo();
                }
            } else if (n.type == "FunctionStart") {   // the signature (may be long)
                char sbuf[256]; std::snprintf(sbuf, sizeof(sbuf), "%s", n.arg.c_str());
                if (ImGui::InputTextWithHint("##sig", d->paramHint, sbuf, sizeof(sbuf))) {
                    // Renaming the function renames the Calls and Returns that use it.
                    const std::string was = okay::vs::FunctionNameOf(n.arg), now = okay::vs::FunctionNameOf(sbuf);
                    if (!was.empty() && !now.empty() && was != now)
                        for (auto& m : g.nodes)
                            if ((m.type == "CallFunction" || m.type == "Return") && okay::vs::FunctionNameOf(m.arg) == was) m.arg = now;
                    n.arg = sbuf; g.dirty = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Name(param: Type, ...) -> result: Type\nTypes: Number, Bool, Text, Vector3, Object, List, Any");
            } else if (d->param == 2) {   // compare op combo
                const char* ops[] = {">", "<", ">=", "<=", "==", "!="};
                if (ImGui::BeginCombo("##op", n.arg.c_str())) {
                    for (const char* o : ops) if (ImGui::Selectable(o, n.arg == o)) { n.arg = o; g.dirty = true; }
                    ImGui::EndCombo();
                }
            } else if (IsKeyNode(n.type)) {
                if (KeyPicker(n.arg, NODE_W - PAD * 2)) g.dirty = true;
            } else if (n.type == "PlayAction") {
                const auto& names = okay::HumanoidActions::ActionNames();
                const char* preview = n.arg.empty() ? "(action)" : n.arg.c_str();
                if (ImGui::BeginCombo("##act", preview)) {
                    for (const auto& nm : names)
                        if (ImGui::Selectable(nm.c_str(), n.arg == nm)) { n.arg = nm; g.dirty = true; }
                    if (!n.arg.empty()) {
                        bool known = false;
                        for (const auto& nm : names) if (nm == n.arg) known = true;
                        if (!known && ImGui::Selectable(n.arg.c_str(), true)) { /* keep a saved name */ }
                    }
                    ImGui::EndCombo();
                }
            } else if (n.type == "Die") {
                const char* dirs[] = {"forward", "back", "left", "right"};
                int cur = 0;
                for (int i = 0; i < 4; ++i) if (n.arg == dirs[i]) cur = i;
                if (ImGui::Combo("##diedir", &cur, dirs, 4)) { n.arg = dirs[cur]; g.dirty = true; }
            } else if (n.type == "MouseButton" || n.type == "MouseButtonDown") {
                const char* btns[] = {"Left button", "Right button", "Middle button"};
                int b = std::atoi(n.arg.c_str());
                if (n.arg == "right") b = 1; else if (n.arg == "middle") b = 2;
                if (b < 0 || b > 2) b = 0;
                if (ImGui::Combo("##mbtn", &b, btns, 3)) { n.arg = std::to_string(b); g.dirty = true; }
            } else if (isSwitch) {
                char sbuf[512]; std::snprintf(sbuf, sizeof(sbuf), "%s", n.arg.c_str());
                if (ImGui::InputTextWithHint("##sw", d->paramHint ? d->paramHint : "", sbuf, sizeof(sbuf))) {
                    const int oldN = (int)VsEditorSwitchCases(n.arg).size();
                    const int newN = (int)VsEditorSwitchCases(sbuf).size();
                    VsMoveSwitchDefault(g, n.id, oldN, newN);
                    n.arg = sbuf; g.dirty = true;
                }
                // InputText returns the cursor to the window flow. Put + case
                // back under the field, inside this node.
                const ImVec2 swField = ImGui::GetItemRectMax();
                ImGui::SetCursorScreenPos(ImVec2(p0.x + PAD, swField.y + 6.0f * z));
                if (ImGui::SmallButton("+ case")) {
                    auto cases = VsEditorSwitchCases(n.arg);
                    if ((int)cases.size() < 63) {
                        const int oldN = (int)cases.size();
                        if (n.type == "SwitchCases") {
                            int next = oldN;
                            if (!cases.empty()) {
                                char* end = nullptr;
                                const long v = std::strtol(cases.back().c_str(), &end, 10);
                                if (end && *end == '\0') next = (int)v + 1;
                            }
                            cases.push_back(std::to_string(next));
                        } else {
                            cases.push_back("case" + std::to_string(oldN + 1));
                        }
                        std::string joined;
                        for (const auto& c : cases) { if (!joined.empty()) joined += ", "; joined += c; }
                        VsMoveSwitchDefault(g, n.id, oldN, oldN + 1);
                        n.arg = joined; g.dirty = true;
                    }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add another case. Default stays the last pin.");
            } else if (VsChoiceKind ck = VsArgChoice(n.type); ck != VsChoiceKind::None) {
                if (VsDrawChoice(ed, g, n.arg, ck, NODE_W - PAD * 2)) g.dirty = true;
            } else if (okay::vs::VsRefKind ak = VsArgKind(n.type); ak != okay::vs::VsRefKind::None) {
                if (VsDrawRefSlot(ed, st, n.arg, ak, VsArgDefaultsSelf(n.type), NODE_W - PAD * 2, "argref"))
                    g.dirty = true;
            } else {
                if (ImGui::InputTextWithHint("##arg", d->paramHint ? d->paramHint : "", buf, sizeof(buf)))
                    { n.arg = buf; g.dirty = true; }
            }
            ImGui::PopStyleVar(2);
        }

        // Start a wire from a pin: an invisible button over each pin (generous hit
        // area). Alt+click a pin breaks its wires instead.
        auto pinButton = [&](long long key, int kind, int side, int idx) {
            auto it = pinPos.find(key); if (it == pinPos.end()) return;
            ImVec2 pp = it->second;
            const float hit = oked::scriptui::PinHitSize(dm, z);
            ImGui::SetCursorScreenPos(ImVec2(pp.x - hit * 0.5f, pp.y - hit * 0.5f));
            char id[24]; std::snprintf(id, sizeof(id), "p%d%d%d", kind, side, idx);
            ImGui::InvisibleButton(id, ImVec2(hit, hit));
            if (st.dragging && n.id != st.dragNode) {
                const bool kindOk = kind == (st.dragExec ? 0 : 1);
                const bool sideOk = side == (st.dragFromOutput ? 0 : 1);
                bool fit = false;
                std::string why;
                if (kindOk && sideOk) {
                    if (st.dragExec) {
                        fit = !(side == 0 && d && !d->execIn);
                        if (!fit) why = VsPlainRefuseEventIntoEvent();
                    } else {
                        const int cons = st.dragFromOutput ? n.id : st.dragNode;
                        const int inP = st.dragFromOutput ? idx : st.dragPin;
                        const int prod = st.dragFromOutput ? st.dragNode : n.id;
                        const int outP = st.dragFromOutput ? st.dragPin : idx;
                        fit = VsWireOk(g, cons, inP, prod, outP, &why);
                        if (fit) {
                            std::vector<std::pair<int, int>> edges;
                            for (const auto& l : g.dataLinks) edges.push_back({l.producer, l.consumer});
                            if (VsDataWouldCycle(edges, prod, cons)) { fit = false; why = VsPlainRefuseLoop(); }
                        }
                    }
                } else if (!kindOk) {
                    why = st.dragExec ? "This wire carries flow (the triangle), not a value. Drop it on a white triangle."
                                      : "This wire carries a value. Drop it on a coloured circle, not a flow triangle.";
                }
                if (fit) dl->AddCircle(pp, PIN_R + 5.0f * z, IM_COL32(140, 230, 160, 240), 18, 2.5f * z);
                else dl->AddCircleFilled(pp, PIN_R + 2.0f * z, IM_COL32(10, 10, 14, 160), 16);
                if (!fit && !why.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", why.c_str());
            }
            if (kind == 1 && side == 0 && ImGui::BeginDragDropTarget()) {
                const char* label = (idx >= 0 && idx < (int)d->dataIn.size()) ? d->dataIn[(std::size_t)idx] : "";
                const okay::vs::VsRefKind rk = VsPinKind(n.type, idx, VsInPinType(g, n.id, idx), label);
                if (rk != okay::vs::VsRefKind::None) {
                    std::string slot = n.pinDefault.count(idx) ? n.pinDefault[idx] : std::string();
                    if (VsApplyDrop(ed, st, rk, slot)) {
                        g.unlinkPin(n.id, 1, 0, idx);
                        n.pinDefault[idx] = slot;
                        g.dirty = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::IsItemActivated()) {
                if (io.KeyAlt) g.unlinkPin(n.id, kind, side, idx);
                else {
                    st.dragging = true; st.dragExec = (kind == 0);
                    st.dragFromOutput = (side == 1); st.dragNode = n.id; st.dragPin = idx;
                }
            }
            if (ImGui::IsItemHovered()) {
                dl->AddCircle(pp, PIN_R + 3 * z, IM_COL32(255, 255, 255, 210), 18, 2.0f);
                if (!st.dragging) {
                    const bool input = side == 0, exec = kind == 0;
                    std::string pname;
                    if (!exec) pname = input ? (idx < (int)d->dataIn.size() ? d->dataIn[idx] : "") : (idx < (int)d->dataOut.size() ? d->dataOut[idx] : "");
                    else if (!input && idx < (int)d->execOut.size()) pname = d->execOut[idx];
                    const VsNodeDoc* nd = VsNodeDocFor(n.type);
                    const VsPinDoc* pd = nd ? VsPinDocFor(*nd, input, exec, pname) : nullptr;
                    const char* tn = exec ? "Flow" : okay::vs::PinTypeName(input ? VsInPinType(g, n.id, idx) : VsOutPinType(g, n.id, idx));
                    const bool needsNow = input && !exec && !inputLinked(n.id, idx) && n.pinDefault.count(idx) == 0 && VsPinNeedsInput(*d, idx);
                    const std::string needText = needsNow ? VsMissingHint(*d, idx) : std::string();
                    const std::string plainTip = VsPinTip(*d, input, exec, idx);
                    VsPinTooltip(pd, pname.c_str(), tn,
                                 exec ? okay::vs::PinType::Any : (input ? VsInPinType(g, n.id, idx) : VsOutPinType(g, n.id, idx)), !input, exec,
                                 needsNow ? needText.c_str() : nullptr, plainTip.c_str());
                }
            }
        };
        if (d->execIn) pinButton(pk(n.id, 0, 0, 0), 0, 0, 0);
        for (int i = 0; i < (int)d->dataIn.size(); ++i)  pinButton(pk(n.id, 1, 0, i), 1, 0, i);
        for (int i = 0; i < (int)d->execOut.size(); ++i) pinButton(pk(n.id, 0, 1, i), 0, 1, i);
        for (int i = 0; i < (int)d->dataOut.size(); ++i) pinButton(pk(n.id, 1, 1, i), 1, 1, i);

        if (!inlineMsg.empty()) {
            const float my = p1.y - msgH;
            const ImU32 band = inlineErr ? IM_COL32(120, 48, 46, 230) : IM_COL32(120, 96, 40, 220);
            dl->AddRectFilled(ImVec2(p0.x + 2.0f * z, my), ImVec2(p1.x - 2.0f * z, p1.y - 2.0f * z), band, ROUND, ImDrawFlags_RoundCornersBottom);
            std::string shown = inlineMsg;
            const float maxW = NODE_W - PAD * 2.0f;
            while (shown.size() > 4 && ImGui::CalcTextSize(shown.c_str()).x > maxW) shown.pop_back();
            if (shown.size() < inlineMsg.size()) shown += "...";
            text(ImVec2(p0.x + PAD, my + 4.0f * z), IM_COL32(255, 236, 228, 255), shown.c_str());
            if (showAdd && VsAddComponentById && vsOwner) {
                ImGui::SetCursorScreenPos(ImVec2(p0.x + PAD, my + fs + 8.0f * z));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 3.0f));
                if (ImGui::SmallButton(reqGap.addLabel.c_str())) VsAddComponentById(vsOwner, reqGap.addId.c_str());
                ImGui::PopStyleVar();
            }
            if (ImGui::IsMouseHoveringRect(ImVec2(p0.x, my), p1) && !showAdd) ImGui::SetTooltip("%s", inlineMsg.c_str());
            else if (showAdd && ImGui::IsMouseHoveringRect(ImVec2(p0.x, my), ImVec2(p1.x, my + fs + 6.0f)))
                ImGui::SetTooltip("%s", reqGap.message.c_str());
        }

        // Play feedback: a short "what happened" line under a block that has run (the glow is the flash above).
        if (live && ln && ln->execCount > 0 && st.liveValues) {
            std::vector<std::string> shown;
            for (int pi = 0; pi < (int)d->dataIn.size(); ++pi) {
                std::string val;
                for (const auto& l : g.dataLinks)
                    if (l.consumer == n.id && l.inPin == pi)
                        if (okay::vs::VsNode* pn = liveNode(l.producer))
                            if (l.outPin < (int)pn->lastOut.size()) val = VsDebugText(pn->lastOut[(std::size_t)l.outPin]);
                if (val.empty()) { const auto it = n.pinDefault.find(pi); if (it != n.pinDefault.end()) val = it->second; }
                shown.push_back(val);
            }
            const std::string said = VsWhatHappened(n.type, n.arg, shown, ln->execCount);
            const ImVec2 sz = ImGui::CalcTextSize(said.c_str());
            const ImVec2 q0(p0.x, p1.y + 6.0f * z), q1(p0.x + std::min(NODE_W + 40.0f * z, sz.x + 16.0f * z), q0.y + sz.y + 6.0f * z);
            const float fade = 0.55f + 0.45f * (st.flash.count(n.id) ? st.flash[n.id] : 0.0f);
            dl->AddRectFilled(q0, q1, IM_COL32(26, 64, 40, (int)(235 * fade)), 6.0f * z);
            dl->PushClipRect(q0, q1, true);
            dl->AddText(ImVec2(q0.x + 8.0f * z, q0.y + 3.0f * z), IM_COL32(190, 245, 205, (int)(255 * fade)), said.c_str());
            dl->PopClipRect();
        }
        ImGui::PopID();
    }
    // Move the dragged node and every other selected node with it.
    if (startNodeDrag) {
        float dx = io.MouseDelta.x / (z * kLayout), dy = io.MouseDelta.y / (z * kLayout);
        if (dx != 0.0f || dy != 0.0f) {
            for (auto& n : g.nodes) if (st.sel.count(n.id)) { n.x += dx; n.y += dy; }
            g.dirty = true;
        }
    }
    // Every wire with its two plugs on screen (for dropping a block on a wire and double-clicking one).
    auto wireEnds = [&]() {
        std::vector<VsWireEnds> out;
        for (int i = 0; i < (int)g.execLinks.size(); ++i) {
            const auto& l = g.execLinks[(std::size_t)i];
            auto a = pinPos.find(pk(l.from, 0, 1, l.pin)), b = pinPos.find(pk(l.to, 0, 0, 0));
            if (a != pinPos.end() && b != pinPos.end()) out.push_back({true, i, a->second.x, a->second.y, b->second.x, b->second.y});
        }
        for (int i = 0; i < (int)g.dataLinks.size(); ++i) {
            const auto& l = g.dataLinks[(std::size_t)i];
            auto a = pinPos.find(pk(l.producer, 1, 1, l.outPin)), b = pinPos.find(pk(l.consumer, 1, 0, l.inPin));
            if (a != pinPos.end() && b != pinPos.end()) out.push_back({false, i, a->second.x, a->second.y, b->second.x, b->second.y});
        }
        return out;
    };
    // A block with no wires, dropped with its middle on a wire: the wire goes through the block.
    if (st.wasNodeDrag && !startNodeDrag && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && st.sel.size() == 1 &&
        VsIsLoose(g, *st.sel.begin()) && nodeRect.count(*st.sel.begin())) {
        const auto& rc = nodeRect[*st.sel.begin()];
        const std::vector<VsWireEnds> we = wireEnds();
        const int hit = VsNearestWire(we, (rc.first.x + rc.second.x) * 0.5f, (rc.first.y + rc.second.y) * 0.5f, z, 26.0f * z);
        if (hit >= 0 && VsSplitWire(g, VsRefOfWire(g, we[(std::size_t)hit].exec, we[(std::size_t)hit].index), *st.sel.begin()))
            st.lastUsed = *st.sel.begin();
    }
    if (st.wasNodeDrag && !startNodeDrag && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && st.snapGrid) {
        bool moved = false;
        for (auto& n : g.nodes) if (st.sel.count(n.id)) {
            const float x = VsSnap(n.x, 20.0f), y = VsSnap(n.y, 20.0f);
            if (x != n.x || y != n.y) { n.x = x; n.y = y; moved = true; }
        }
        if (moved) g.dirty = true;
    }
    if (st.wasNodeDrag && !startNodeDrag && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && st.sel.size() == 1) {
        // Dropped on a flow wire: the node goes into the wire and both sides stay connected.
        const int dropped = *st.sel.begin();
        const auto at = pinPos.find(pk(dropped, 0, 0, 0));
        if (at != pinPos.end()) {
            std::vector<VsWireLine> lines;
            for (const auto& l : g.execLinks) {
                const auto a = pinPos.find(pk(l.from, 0, 1, l.pin)), b = pinPos.find(pk(l.to, 0, 0, 0));
                lines.push_back(a != pinPos.end() && b != pinPos.end() ? VsWireLine{a->second.x, a->second.y, b->second.x, b->second.y}
                                                                       : VsWireLine{-1e6f, -1e6f, -1e6f, -1e6f});
            }
            if (VsInsertOnWire(g, dropped, VsWireUnder(lines, at->second.x, at->second.y, 18.0f * z))) g.dirty = true;
        }
    }
    st.wasNodeDrag = startNodeDrag;
    if (pendingDup >= 0 || (winFocused && !typing && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false) && !st.sel.empty())) {
        std::set<int> src = st.sel;
        if (src.empty() && pendingDup >= 0) src.insert(pendingDup);
        float mx = 1e9f, my = 1e9f;
        for (const auto& n : g.nodes) if (src.count(n.id)) { mx = std::min(mx, n.x); my = std::min(my, n.y); }
        st.sel = VsPasteText(g, VsSelectionText(g, src), mx + 30.0f, my + 30.0f);
        st.selected = st.sel.empty() ? -1 : *st.sel.begin();
    }
    if (winFocused && !typing && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
        const char* clip = ImGui::GetClipboardText();
        if (clip && std::strstr(clip, "node ")) {
            ImVec2 w = canvasWindowHovered ? S2W(mouse) : S2W(ImVec2((canvasMin.x + canvasMax.x) * 0.5f, (canvasMin.y + canvasMax.y) * 0.5f));
            st.sel = VsPasteText(g, clip, w.x, w.y);
            st.selected = st.sel.empty() ? -1 : *st.sel.begin();
        }
    }
    if (pendingDelete >= 0) {
        if (!st.sel.count(pendingDelete)) st.sel = {pendingDelete};
        VsDeleteBlocks(g, st.sel, io.KeyShift);   // the blocks around it are joined; Shift cuts instead
        st.sel.clear(); st.selected = -1;
    }
    if (winFocused && !typing && (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) &&
        (!st.sel.empty() || st.selected >= 0)) {
        if (st.selected >= 0) st.sel.insert(st.selected);
        VsDeleteBlocks(g, st.sel, io.KeyShift);   // Delete joins the blocks around it; Shift+Delete cuts instead
        st.sel.clear(); st.selected = -1;
    }

    // ---- Wires, behind the nodes. Ones touching the selection are highlighted. --
    dl->ChannelsSetCurrent(1);
    auto bezier = [&](ImVec2 a, ImVec2 b, ImU32 col, float w) {
        float dx = std::max(50.0f * z, std::fabs(b.x - a.x) * 0.5f);
        dl->AddBezierCubic(a, ImVec2(a.x + dx, a.y), ImVec2(b.x - dx, b.y), b, col, w);
    };
    auto hotNode = [&](int id) { return st.sel.count(id) > 0 || st.selected == id; };
    // A wire through its knots (reroute dots): one curve per stretch.
    auto knotW2S = [&](float x, float y) { return W2S(x, y); };
    auto wireThrough = [&](ImVec2 a, ImVec2 b, const std::vector<ImVec2>& knots, ImU32 col, float w) {
        ImVec2 from = a;
        for (const ImVec2& k : knots) { bezier(from, k, col, w); from = k; }
        bezier(from, b, col, w);
    };
    // The path the run took this frame: a wire is lit when both its ends ran (VsDebugWatch.hpp).
    std::set<int> ranNow;
    if (live) for (const auto& kv : st.flash) if (kv.second > 0.0f) ranNow.insert(kv.first);
    const VsLitWires litNow = VsLitPath(g, live, ranNow);
    int hoverValueWire = -1;   // a value wire under the mouse while the game runs: its value shows in a tip
    // Alt held over a wire: it lights up, and a click breaks that wire only (the
    // other wires on the same pin stay). Nearest wire within a few pixels wins.
    auto bezierDist = [&](ImVec2 a, ImVec2 b) {
        const float dx = std::max(50.0f * z, std::fabs(b.x - a.x) * 0.5f);
        const ImVec2 c1(a.x + dx, a.y), c2(b.x - dx, b.y);
        float best = 1.0e9f;
        for (int i = 0; i <= 24; ++i) {
            const float t = i / 24.0f, u = 1.0f - t;
            const float x = u*u*u*a.x + 3*u*u*t*c1.x + 3*u*t*t*c2.x + t*t*t*b.x;
            const float y = u*u*u*a.y + 3*u*u*t*c1.y + 3*u*t*t*c2.y + t*t*t*b.y;
            best = std::min(best, (x - io.MousePos.x) * (x - io.MousePos.x) + (y - io.MousePos.y) * (y - io.MousePos.y));
        }
        return std::sqrt(best);
    };
    int altExec = -1, altData = -1;   // index of the wire under the mouse (Alt held)
    if (io.KeyAlt && !st.dragging && ImGui::IsWindowHovered()) {
        float best = 9.0f;
        for (int i = 0; i < (int)g.execLinks.size(); ++i) {
            const auto& l = g.execLinks[(std::size_t)i];
            auto a = pinPos.find(pk(l.from, 0, 1, l.pin)), b = pinPos.find(pk(l.to, 0, 0, 0));
            if (a == pinPos.end() || b == pinPos.end()) continue;
            const float dd = bezierDist(a->second, b->second);
            if (dd < best) { best = dd; altExec = i; altData = -1; }
        }
        for (int i = 0; i < (int)g.dataLinks.size(); ++i) {
            const auto& l = g.dataLinks[(std::size_t)i];
            auto a = pinPos.find(pk(l.producer, 1, 1, l.outPin)), b = pinPos.find(pk(l.consumer, 1, 0, l.inPin));
            if (a == pinPos.end() || b == pinPos.end()) continue;
            const float dd = bezierDist(a->second, b->second);
            if (dd < best) { best = dd; altData = i; altExec = -1; }
        }
        if ((altExec >= 0 || altData >= 0) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (altExec >= 0) g.execLinks.erase(g.execLinks.begin() + altExec);
            else g.dataLinks.erase(g.dataLinks.begin() + altData);
            g.dirty = true; altExec = altData = -1;
        }
    }
    int execIdx = -1;
    for (const auto& l : g.execLinks) {
        ++execIdx;
        auto a = pinPos.find(pk(l.from, 0, 1, l.pin)), b = pinPos.find(pk(l.to, 0, 0, 0));
        if (a == pinPos.end() || b == pinPos.end()) continue;
        if (execIdx == altExec) { bezier(a->second, b->second, IM_COL32(255, 120, 100, 255), 4.2f * std::max(0.7f, z)); continue; }
        const bool hot = hotNode(l.from) || hotNode(l.to);
        const bool ran = live && execIdx < (int)litNow.exec.size() && litNow.exec[(std::size_t)execIdx];
        ImU32 col = ran ? IM_COL32(255, 205, 90, 255) : hot ? IM_COL32(255, 255, 255, 255) : IM_COL32(215, 216, 222, 200);
        wireThrough(a->second, b->second, VsKnotPoints(g, true, l.from, l.pin, l.to, knotW2S), col, (hot || ran ? 3.6f : 2.8f) * std::max(0.7f, z));
    }
    int dataIdx = -1;
    for (const auto& l : g.dataLinks) {
        ++dataIdx;
        auto a = pinPos.find(pk(l.producer, 1, 1, l.outPin)), b = pinPos.find(pk(l.consumer, 1, 0, l.inPin));
        if (a == pinPos.end() || b == pinPos.end()) continue;
        if (dataIdx == altData) { bezier(a->second, b->second, IM_COL32(255, 120, 100, 255), 4.2f * std::max(0.7f, z)); continue; }
        const bool hot = hotNode(l.producer) || hotNode(l.consumer);
        // A wire whose value can't mean what the input needs (from an older graph
        // or hand-written file) is drawn red; the checker lists it too.
        const std::vector<ImVec2> knots = VsKnotPoints(g, false, l.consumer, l.inPin, 0, knotW2S);
        if (!VsWireOk(g, l.consumer, l.inPin, l.producer, l.outPin)) {
            wireThrough(a->second, b->second, knots, IM_COL32(255, 80, 70, 240), (hot ? 3.6f : 3.0f) * std::max(0.7f, z));
            continue;
        }
        const bool litData = live && dataIdx < (int)litNow.data.size() && litNow.data[(std::size_t)dataIdx];
        ImU32 wc = VsPinColor(VsOutPinType(g, l.producer, l.outPin));
        if (!hot && !litData) wc = (wc & 0x00FFFFFF) | (200u << 24);
        wireThrough(a->second, b->second, knots, wc, (hot || litData ? 3.4f : 2.6f) * std::max(0.7f, z));
        if (live && hoverValueWire < 0 && !st.dragging && !io.KeyAlt && knots.empty() && ImGui::IsWindowHovered() &&
            bezierDist(a->second, b->second) < 7.0f)
            hoverValueWire = dataIdx;
    }
    if (hoverValueWire >= 0) {   // the value that went through the wire last (paused or playing)
        const std::string v = VsWireValueText(g, live, hoverValueWire);
        ImGui::SetTooltip("%s", v.empty() ? "No value has gone through this wire yet." : ("Value: " + v).c_str());
    }
    dl->ChannelsSetCurrent(2);
    VsIdeKnots(dl, st, z, knotW2S, S2W, canvasHovered && !st.dragging);   // knots: drag to move, right-click to remove
    for (const VsBadge& b : badges) {
        const float r = 7.0f * std::max(0.8f, z);
        dl->AddCircleFilled(b.c, r, b.col, 16);
        const ImVec2 ts = ImGui::CalcTextSize("!");
        dl->AddText(ImVec2(b.c.x - ts.x * 0.5f, b.c.y - ts.y * 0.5f), IM_COL32(20, 20, 24, 255), "!");
        if (ImGui::IsWindowHovered() && std::fabs(io.MousePos.x - b.c.x) < r * 1.6f && std::fabs(io.MousePos.y - b.c.y) < r * 1.6f)
            ImGui::SetTooltip("%s", b.tips.c_str());
    }

    // Live drag wire + release handling.
    bool openPaletteNow = false;
    const std::size_t wiresBeforeDrag = g.execLinks.size() + g.dataLinks.size();
    if (st.dragging) {
        // While a wire is dragged: a green ring on every plug it can go into, a red ring on the ones that would refuse it.
        {
            const int wantSide = st.dragFromOutput ? 0 : 1, wantKind = st.dragExec ? 0 : 1;
            const float rr = 11.0f * std::max(0.8f, z);
            for (const auto& kvp : pinPos) {
                const int node = (int)(kvp.first >> 20), kind = (int)((kvp.first >> 18) & 3), side = (int)((kvp.first >> 16) & 3), idx = (int)(kvp.first & 0xFFFF);
                if (kind != wantKind || side != wantSide || node == st.dragNode) continue;
                const bool fits = VsPlugAccepts(g, st.dragExec, st.dragFromOutput, st.dragNode, st.dragPin, node, idx);
                dl->AddCircle(kvp.second, rr, fits ? IM_COL32(90, 230, 130, 255) : IM_COL32(245, 90, 80, 235), 20, 3.0f);
            }
        }
        auto from = pinPos.find(pk(st.dragNode, st.dragExec ? 0 : 1, st.dragFromOutput ? 1 : 0, st.dragPin));
        if (from != pinPos.end()) {
            ImVec2 m = io.MousePos;
            bezier(st.dragFromOutput ? from->second : m, st.dragFromOutput ? m : from->second,
                   IM_COL32(255, 255, 255, 170), 2.6f * std::max(0.7f, z));
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            // Find a pin under the mouse of the opposite side + same kind.
            ImVec2 m = io.MousePos;
            float snap = std::max(26.0f, 30.0f * z);
            int bestNode = -1, bestIdx = -1; float bestD = snap * snap;   // forgiving snap radius
            int wantSide = st.dragFromOutput ? 0 : 1;   // connect to an input if we came from an output
            int wantKind = st.dragExec ? 0 : 1;
            for (auto& kvp : pinPos) {
                int node = (int)(kvp.first >> 20);
                int kind = (int)((kvp.first >> 18) & 3);
                int side = (int)((kvp.first >> 16) & 3);
                int idx  = (int)(kvp.first & 0xFFFF);
                if (kind != wantKind || side != wantSide || node == st.dragNode) continue;
                float dx = kvp.second.x - m.x, dy = kvp.second.y - m.y;
                float dd = dx * dx + dy * dy;
                if (dd < bestD) { bestD = dd; bestNode = node; bestIdx = idx; }
            }
            if (bestNode >= 0) {
                VsTryWire(st, bestNode, bestIdx);   // VsIdeModel.hpp: the wiring rules the Visual Script app uses too
            } else {
                // Dropped on the other kind of pin: say so instead of doing nothing.
                float hitDist2 = std::max(26.0f, 30.0f * z); hitDist2 *= hitDist2;
                int nk = -1, ns = -1;
                for (auto& kvp : pinPos) {
                    int node = (int)(kvp.first >> 20);
                    int kind = (int)((kvp.first >> 18) & 3);
                    int side = (int)((kvp.first >> 16) & 3);
                    if (node == st.dragNode) continue;
                    float dx = kvp.second.x - m.x, dy = kvp.second.y - m.y;
                    if (dx * dx + dy * dy < hitDist2) { nk = kind; ns = side; break; }
                }
                if (nk >= 0 && nk != (st.dragExec ? 0 : 1)) {
                    st.refuseMsg = st.dragExec ? VsPlainFlowToValue() : VsPlainValueToFlow();
                    st.refuseTime = 5.0f; st.offerOn = false; (void)ns;
                } else {
                // Dropped in empty space: offer the palette and wire up what's picked.
                bool overNode = false;
                for (const auto& kv : nodeRect)
                    if (m.x >= kv.second.first.x && m.x <= kv.second.second.x && m.y >= kv.second.first.y && m.y <= kv.second.second.y) { overNode = true; break; }
                if (!overNode && m.x >= canvasMin.x && m.x <= canvasMax.x && m.y >= canvasMin.y && m.y <= canvasMax.y) {
                    st.linkPending = true; st.linkExec = st.dragExec; st.linkFromOutput = st.dragFromOutput;
                    st.linkNode = st.dragNode; st.linkPin = st.dragPin;
                    openPaletteNow = true;
                }
                }
            }
            st.dragging = false;
        }
    }

    const bool userWiredNow = g.execLinks.size() + g.dataLinks.size() > wiresBeforeDrag;   // a wire dropped on a plug
    // ---- Box selection --------------------------------------------------------------
    // Plain left-drag on empty canvas pans (above); Shift or Ctrl + drag draws the selection box.
    if (bgClicked && !st.dragging && !io.KeyAlt && !io.KeyCtrl && !io.KeyShift && !ImGui::IsKeyDown(ImGuiKey_Space)) {
        st.sel.clear(); st.selected = -1;   // a plain click on empty canvas still deselects
    }
    if (bgClicked && !st.dragging && !io.KeyAlt &&
        VsLeftDragModeEmpty(ImGui::IsKeyDown(ImGuiKey_Space), st.panTool, io.KeyShift, io.KeyCtrl) == VsLeftDrag::Box) {
        st.boxing = true; st.boxStart = mouse;
    }
    if (!st.boxing && g.nodes.size() < 3 && !st.dragging)
        dl->AddText(ImVec2(canvasMin.x + 16, canvasMax.y - 28), IM_COL32(170, 180, 200, 170),
                    "Drag the background to move around. Scroll to zoom.");
    if (st.boxing) {
        ImVec2 a(std::min(st.boxStart.x, mouse.x), std::min(st.boxStart.y, mouse.y));
        ImVec2 b(std::max(st.boxStart.x, mouse.x), std::max(st.boxStart.y, mouse.y));
        dl->AddRectFilled(a, b, IM_COL32(120, 170, 255, 40));
        dl->AddRect(a, b, IM_COL32(120, 170, 255, 200), 0.0f, 0, 1.5f);
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (b.x - a.x > 3.0f || b.y - a.y > 3.0f)
                for (const auto& kv : nodeRect) {
                    const ImVec2& p0 = kv.second.first; const ImVec2& p1 = kv.second.second;
                    if (p1.x >= a.x && p0.x <= b.x && p1.y >= a.y && p0.y <= b.y) st.sel.insert(kv.first);
                }
            st.selected = st.sel.empty() ? -1 : *st.sel.begin();
            st.boxing = false;
        }
    }
    dl->ChannelsMerge();
    ImGui::SetWindowFontScale(1.0f);   // popups / overlay text at normal size

    // ---- Minimap (bottom-right): the whole graph + the visible area; click to jump --
    if (st.showMinimap && !g.nodes.empty()) {
        const ImVec2 mmSize(220.0f, 148.0f);
        const ImVec2 mmMax(canvasMax.x - 10.0f, canvasMax.y - 10.0f), mmMin(mmMax.x - mmSize.x, mmMax.y - mmSize.y);
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (const auto& n : g.nodes) {
            x0 = std::min(x0, n.x * kLayout); y0 = std::min(y0, n.y * kLayout);
            x1 = std::max(x1, n.x * kLayout + 230.0f); y1 = std::max(y1, n.y * kLayout + 150.0f);
        }
        // Include the visible area so its rectangle always fits.
        ImVec2 v0 = S2W(canvasMin), v1 = S2W(canvasMax);
        x0 = std::min(x0, v0.x * kLayout); y0 = std::min(y0, v0.y * kLayout);
        x1 = std::max(x1, v1.x * kLayout); y1 = std::max(y1, v1.y * kLayout);
        float sc = std::min((mmSize.x - 8.0f) / std::max(1.0f, x1 - x0), (mmSize.y - 8.0f) / std::max(1.0f, y1 - y0));
        auto M = [&](float wx, float wy) { return ImVec2(mmMin.x + 4.0f + (wx - x0) * sc, mmMin.y + 4.0f + (wy - y0) * sc); };
        dl->AddRectFilled(mmMin, mmMax, IM_COL32(22, 26, 34, 230), 10.0f);
        dl->AddRect(mmMin, mmMax, IM_COL32(120, 136, 160, 180), 10.0f, 0, 1.5f);
        for (const auto& n : g.nodes) {
            const VsDef* d = FindDef(n.type);
            ImVec2 a = M(n.x * kLayout, n.y * kLayout), b = M(n.x * kLayout + 220.0f, n.y * kLayout + 110.0f);
            dl->AddRectFilled(a, b, d ? CatColor(d->cat) : IM_COL32(120, 120, 120, 255), 2.0f);
            if (st.sel.count(n.id)) dl->AddRect(a, b, IM_COL32(140, 190, 255, 255), 2.0f);
        }
        dl->AddRect(M(v0.x * kLayout, v0.y * kLayout), M(v1.x * kLayout, v1.y * kLayout), IM_COL32(255, 255, 255, 200), 0.0f, 0, 1.5f);
        ImGui::SetCursorScreenPos(mmMin);
        ImGui::InvisibleButton("##minimap", mmSize);
        if (ImGui::IsItemActive()) {   // centre the view on the clicked point
            float wx = (mouse.x - mmMin.x - 4.0f) / sc + x0, wy = (mouse.y - mmMin.y - 4.0f) / sc + y0;
            st.pan = ImVec2((canvasMax.x - canvasMin.x) * 0.5f - wx * z, (canvasMax.y - canvasMin.y) * 0.5f - wy * z);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Minimap: click or drag to move the view");
    }

    // "Connect to... > Search all": the wire-drop list, started from that pin of the node.
    bool pconnPlace = false;
    if (pconn.on && pconn.openAll) {
        st.linkPending = true; st.linkExec = pconn.exec; st.linkFromOutput = pconn.fromOutput;
        st.linkNode = pconn.node; st.linkPin = pconn.pin;
        openPaletteNow = true; pconnPlace = true;
    }
    // Double-click a wire: the palette opens, and the block picked goes onto that wire.
    if (canvasHovered && !st.dragging && !io.KeyAlt && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        bool onNode = false;
        for (const auto& kv : nodeRect)
            if (mouse.x >= kv.second.first.x && mouse.x <= kv.second.second.x && mouse.y >= kv.second.first.y && mouse.y <= kv.second.second.y) onNode = true;
        const std::vector<VsWireEnds> we = wireEnds();
        const int hit = onNode ? -1 : VsNearestWire(we, mouse.x, mouse.y, z, 12.0f);
        if (hit >= 0 && io.KeyShift) {   // Shift+double-click: a knot (reroute dot) on the wire, where you clicked
            const ImVec2 wpt = S2W(mouse);
            VsAddKnot(g, VsRefOfWire(g, we[(std::size_t)hit].exec, we[(std::size_t)hit].index), wpt.x, wpt.y);
        } else if (hit >= 0) {
            const VsWireRef ref = VsRefOfWire(g, we[(std::size_t)hit].exec, we[(std::size_t)hit].index);
            st.wireInsOn = ref.on; st.wireInsExec = ref.exec; st.wireInsA = ref.a; st.wireInsB = ref.b; st.wireInsC = ref.c;
            st.linkPending = false;
            openPaletteNow = ref.on;
            st.sel.clear(); st.selected = -1;
        }
    }
    // Right-click empty canvas (or Space): node palette.
    if ((canvasHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && VsRightClickOpensMenu(st.rightT, std::sqrt(io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Right])) && !st.dragging) ||
        (winFocused && !typing && canvasWindowHovered && st.spaceOpen) || openPaletteNow || st.addAtCenter) {
        st.openPalette = true;
        st.spaceOpen = false;
        if (!openPaletteNow) st.linkPending = false;
        st.paletteWorld = st.addAtCenter ? S2W(ImVec2((canvasMin.x + canvasMax.x) * 0.5f - 100.0f, (canvasMin.y + canvasMax.y) * 0.5f - 40.0f))
                                         : S2W(io.MousePos);
        st.addAtCenter = false;
        if (pconnPlace) st.paletteWorld = pconn.at;
        ImGui::OpenPopup("##vspalette");
    }
    // A template picked in the toolbar: drop its nodes in the middle of the view.
    if (st.pasteTemplate) {
        st.pasteTemplate = false;
        ImVec2 c = S2W(ImVec2(canvasMin.x + 60.0f, (canvasMin.y + canvasMax.y) * 0.5f - 60.0f));
        std::set<int> made = VsPasteText(g, st.templateText, c.x, c.y);
        st.sel = made; st.selected = made.empty() ? -1 : *made.begin();
        st.frameSel = !made.empty();
        st.templateText.clear();
    }
    // Add a node from the palette; wire it to the dropped pin when there is one.
    auto addFromPalette = [&](const VsDef& d, const std::string& arg = std::string()) {
        {   // a new Quick block fills itself in from the scene: the selected object, its neighbours, the last key
            okay::vs::QbContext& qc = VsQuickContext();
            qc.objectNames.clear();
            for (const auto& up : ed.scene().Objects()) if (up) qc.objectNames.push_back(up->name);
            qc.selfName = ed.selected() ? ed.selected()->name : std::string();
        }
        VsAddFromPalette(st, d, arg);   // VisualScriptModel's VsIdeModel.hpp: the same placing and wiring the Visual Script app does
    };
    // "Connect to... > a node": add it already wired to the pin it was chosen for.
    if (pconn.on && !pconn.openAll && pconn.def) {
        st.linkPending = true; st.linkExec = pconn.exec; st.linkFromOutput = pconn.fromOutput;
        st.linkNode = pconn.node; st.linkPin = pconn.pin;
        st.paletteWorld = pconn.at;
        addFromPalette(*pconn.def);
        st.linkPending = false;
    }
    // The popup opens at the size you left it at (or the default for the Density), inside the screen.
    ImGui::SetNextWindowSize(VsPaletteWantedSize(ImGui::GetMainViewport()->Size), ImGuiCond_Always);   // Always: a popup otherwise falls back to its content size
    if (ImGui::BeginPopup("##vspalette", ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        DrawNodePalette(st, g, [&](const VsDef& d, const std::string& arg) { addFromPalette(d, arg); });
        ImGui::EndPopup();

} else if ((st.linkPending || st.wireInsOn) && !openPaletteNow) {
        st.linkPending = false; st.wireInsOn = false;   // palette dismissed without a pick
    }

    // Guided first steps: which one-line hint to show (VisualScriptHints.hpp), and what is already done.
    VsHintResult guide;
    {
        VsHintFacts hf;
        hf.nodeCount = (int)g.nodes.size();
        hf.wireCount = (int)(g.execLinks.size() + g.dataLinks.size());
        hf.playing = ed.isPlaying();
        hf.userWired = userWiredNow;
        std::size_t sig = 0;
        for (const auto& n : g.nodes) {
            sig = sig * 131u + std::hash<std::string>()(n.arg);
            for (const auto& pd : n.pinDefault) sig = sig * 131u + std::hash<std::string>()(pd.second) + (std::size_t)pd.first;
        }
        hf.editedNumber = st.sigNodes > 0 && st.sigNodes == hf.nodeCount && sig != st.valueSig;
        st.valueSig = sig; st.sigNodes = hf.nodeCount;
        guide = VsHintStep(VsPrefs().hintsDone, hf, st.showHint && !st.missing);
        if (guide.done != VsPrefs().hintsDone) { VsPrefs().hintsDone = guide.done; VsPrefsSave(); }
        if (VsStartVisible(hf.nodeCount, VsPrefs().hideStart, st.startedEmpty, st.missing)) guide.hint = VsHint::None;   // the cards already say it
    }
    if (st.missing) {
        const char* msg = "This graph file could not be opened.";
        const std::string path = g.path.empty() ? std::string("(no path)") : g.path;
        dl->AddText(ImVec2(canvasMin.x + 24.0f, canvasMin.y + 24.0f), IM_COL32(255, 170, 150, 255), msg);
        dl->AddText(ImVec2(canvasMin.x + 24.0f, canvasMin.y + 48.0f), IM_COL32(200, 206, 216, 255), path.c_str());
    } else if (VsStartVisible((int)g.nodes.size(), VsPrefs().hideStart, st.startedEmpty, st.missing)) {
        VsDrawStartScreen(ed, st, canvasMin, canvasMax);
    } else if (g.nodes.empty()) {
        const float cardW = 580.0f, cardH = 360.0f;
        const ImVec2 c0((canvasMin.x + canvasMax.x - cardW) * 0.5f, (canvasMin.y + canvasMax.y - cardH) * 0.5f);
        const ImVec2 c1(c0.x + cardW, c0.y + cardH);
        dl->AddRectFilled(c0, c1, IM_COL32(32, 36, 46, 245), 16.0f);
        dl->AddRect(c0, c1, IM_COL32(120, 140, 168, 160), 16.0f, 0, 1.5f);
        const char* title = "What is this?";
        const char* hint = "A script is a chain of blocks: when something happens, do something. Press Pick a recipe to start.";
        const char* hint2 = "Or add a block and open the Starter group at the top of the list.";
        const ImVec2 ts = ImGui::CalcTextSize(title);
        dl->AddText(ImVec2(c0.x + (cardW - ts.x) * 0.5f, c0.y + 18.0f), IM_COL32(236, 240, 246, 255), title);
        const ImVec2 hs = ImGui::CalcTextSize(hint);
        dl->AddText(ImVec2(c0.x + (cardW - hs.x) * 0.5f, c0.y + 46.0f), IM_COL32(170, 180, 196, 255), hint);
        // Big one-click cards: each is a small working graph (VisualScriptRecipes.hpp).
        int shownCards = 0;
        for (const VsRecipe& r : VsRecipes()) {
            if (shownCards >= 8) break;
            const int col = shownCards % 2, row = shownCards / 2;
            ImGui::SetCursorScreenPos(ImVec2(c0.x + 24.0f + col * 272.0f, c0.y + 80.0f + row * 54.0f));
            ImGui::PushID(shownCards);
            if (ImGui::Button(r.name, ImVec2(262.0f, 46.0f))) {
                st.templateText = r.graph; st.pasteTemplate = true;
                if (GameObject* go = ed.selected()) VsAddTemplateComponents(go, r.add);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", r.blurb);
            ImGui::PopID();
            ++shownCards;
        }
        ImGui::SetCursorScreenPos(ImVec2(c0.x + (cardW - 180.0f) * 0.5f, c1.y - 46.0f));
        if (ImGui::Button("Or add a block", ImVec2(180.0f, 0.0f))) st.addAtCenter = true;
    } else if (st.needsEvent) {
        const char* msg = "Nothing runs yet. Add a block that starts with On (On Start, On Key Pressed) or a Quick block, and plug the first step into it.";
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        const ImVec2 bp(canvasMin.x + 18.0f, canvasMin.y + 16.0f);
        dl->AddRectFilled(bp, ImVec2(bp.x + ts.x + 24.0f, bp.y + ts.y + 16.0f), IM_COL32(64, 48, 24, 230), 8.0f);
        dl->AddText(ImVec2(bp.x + 12.0f, bp.y + 8.0f), IM_COL32(245, 214, 150, 255), msg);
    } else if (st.showHint && guide.hint == VsHint::None) {
        const char* msg = "Right-click or tap Space to add a node    ·    Move the view: middle-drag, right-drag, or hold Space and drag    ·    Scroll to zoom    ·    Drag empty space to select";
        dl->AddText(ImVec2(canvasMin.x + 16.0f, canvasMax.y - 28.0f), IM_COL32(168, 178, 194, 180), msg);
    }
    // The next-step bar: one thin line at the bottom, gone once each step has been done.
    if (guide.hint != VsHint::None) {
        const char* msg = VsHintText(guide.hint);
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        const float maxW = std::max(120.0f, canvasMax.x - canvasMin.x - 260.0f);
        const ImVec2 b0(canvasMin.x + 12.0f, canvasMax.y - ts.y - 24.0f);
        const ImVec2 b1(b0.x + std::min(maxW, ts.x + 44.0f), canvasMax.y - 10.0f);
        dl->AddRectFilled(b0, b1, IM_COL32(28, 58, 96, 238), 12.0f);
        dl->AddRect(b0, b1, IM_COL32(110, 160, 235, 200), 12.0f);
        dl->AddCircleFilled(ImVec2(b0.x + 16.0f, (b0.y + b1.y) * 0.5f), 4.5f, IM_COL32(120, 190, 255, 255));
        dl->PushClipRect(b0, b1, true);
        dl->AddText(ImVec2(b0.x + 30.0f, b0.y + 7.0f), IM_COL32(232, 240, 252, 255), msg);
        dl->PopClipRect();
    }
    // Why the last wire was refused. Wrapped, so the suggestion is not cut off.
    if (st.refuseTime > 0.0f && !st.refuseMsg.empty()) {
        st.refuseTime -= io.DeltaTime;
        const float boxW = std::min(560.0f, canvasMax.x - canvasMin.x - 32.0f);
        ImGui::SetCursorScreenPos(ImVec2(canvasMin.x + 16.0f, canvasMax.y - 120.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(64, 22, 22, 245));
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(220, 90, 80, 255));
        ImGui::BeginChild("##vsrefuse", ImVec2(boxW, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
        ImGui::PushTextWrapPos(boxW - 16.0f);
        ImGui::TextUnformatted(st.refuseMsg.c_str());
        ImGui::PopTextWrapPos();
        if (st.offerOn && !st.offerType.empty() && ImGui::Button(VsPlainFixIt())) VsAcceptConvertOffer(st);   // VsIdeModel.hpp
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
    }
    // The "What is this?" card for the selected node (bottom right), with next-step chips and Try this node.
    if (!st.missing && !g.nodes.empty()) VsDrawHelpCard(st, canvasMin, canvasMax);
    // The checker's list (toolbar button): click a row to jump to its node.
    if (st.showIssues && !st.issues.empty()) {
        const float w = std::min(820.0f, canvasMax.x - canvasMin.x - 36.0f);
        ImGui::SetCursorScreenPos(ImVec2(canvasMax.x - w - 14, canvasMin.y + 40));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(28, 29, 34, 245));
        ImGui::BeginChild("##vsissuelist", ImVec2(w, std::min(260.0f, 34.0f + 26.0f * (float)st.issues.size())), ImGuiChildFlags_Borders);
        ImGui::TextDisabled("Problems the checker found");
        ImGui::Separator();
        for (std::size_t i = 0; i < st.issues.size(); ++i) {
            const auto& is = st.issues[i];
            ImGui::PushID((int)i);
            ImGui::PushStyleColor(ImGuiCol_Text, is.severity == 0 ? ImVec4(1.0f, 0.5f, 0.45f, 1) : ImVec4(0.95f, 0.78f, 0.4f, 1));
            std::string row = std::string(is.severity == 0 ? "Error: " : "Warning: ") + VsPlainIssue(is.message);
            const bool canFix = VsFixAvailable(g, is.message, is.node, is.pin, is.pinDir);
            if (ImGui::Selectable(row.c_str(), false, 0, ImVec2(canFix ? w - 190.0f : 0.0f, 0.0f)) && is.node >= 0) { st.sel = {is.node}; st.selected = is.node; st.focusNode = is.node; }
            if (ImGui::IsItemHovered()) {   // long messages: the whole text
                ImGui::BeginTooltip(); ImGui::PushTextWrapPos(ImGui::GetFontSize() * 36.0f);
                ImGui::TextUnformatted(row.c_str()); ImGui::PopTextWrapPos(); ImGui::EndTooltip();
            }
            ImGui::PopStyleColor();
            if (canFix) {   // an obvious fix exists: one button
                ImGui::SameLine();
                if (ImGui::SmallButton(VsPlainFixIt())) {
                    VsFixIssue(g, is.message, is.node, is.pin, is.pinDir);
                    ImGui::PopID();
                    break;   // the list is rebuilt next frame
                }
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    if (live) {
        std::string lv = "\xE2\x97\x8F LIVE: " + liveName;
        ImVec2 ts = ImGui::CalcTextSize(lv.c_str());
        dl->AddText(ImVec2(canvasMax.x - ts.x - 16, canvasMin.y + 12), IM_COL32(115, 230, 140, 255), lv.c_str());
        // A runtime error (runaway loop, data cycle...): say it on the canvas too,
        // not only in the Console. The node it happened at is outlined in red.
        if (!live->LastError().empty()) {
            std::string er = "\xE2\x9A\xA0 " + live->LastError();
            if (live->ErrorCount() > 1) er += "  (x" + std::to_string(live->ErrorCount()) + ")";
            const float wrap = std::max(200.0f, canvasMax.x - canvasMin.x - 36.0f);
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(canvasMin.x + 18, canvasMax.y - 12 - ImGui::GetTextLineHeight() * 2),
                        IM_COL32(255, 120, 110, 255), er.c_str(), nullptr, wrap);
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    // ---- OkayScript preview panel -----------------------------------------
    // Live compile of the graph to OkayScript, side by side with the canvas, so
    // the two front-ends stay legibly equivalent. Read-only here (the graph is
    // the source of truth); Copy drops it into the clipboard to paste into the
    // text script editor.
    if (dv.stopped) {
        // ---- Debugger watch: where the flow is and what it can see ------------
        ImGui::SameLine();
        ImGui::BeginChild("vsdbgwatch", ImVec2(0, 0), true);
        auto row = [](const std::string& name, const okay::vs::VsValue& v) {
            ImGui::TextDisabled("%s", name.c_str());
            ImGui::SameLine(110.0f);
            if (v.IsColor()) {   // a swatch beside the hex
                const okay::vs::VsColor c = v.AsColor();
                ImGui::ColorButton("##dbgcol", ImVec4(c.r, c.g, c.b, c.a), ImGuiColorEditFlags_NoTooltip, ImVec2(14, 14));
                ImGui::SameLine();
            }
            ImGui::TextWrapped("%s", VsDebugText(v).c_str());
        };
        if (ImGui::CollapsingHeader("Call stack", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (int i = (int)dv.frames.size() - 1; i >= 0; --i)
                ImGui::BulletText("%s  (function)", dv.frames[(std::size_t)i].name.c_str());
            ImGui::BulletText("%s  (event on '%s')", dv.event.c_str(), dv.object.c_str());
        }
        if (!dv.frames.empty()) {
            const auto& f = dv.frames.back();
            const std::string hdr = "Function " + f.name;
            if (ImGui::CollapsingHeader(hdr.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                okay::vs::FunctionSig sig;
                bool haveSig = false;
                for (const auto& m : g.nodes)
                    if (m.type == "FunctionStart" && okay::vs::FunctionNameOf(m.arg) == f.name) { haveSig = okay::vs::ParseFunctionSignature(m.arg, sig); break; }
                for (std::size_t i = 0; i < f.args.size(); ++i)
                    row(haveSig && i < sig.params.size() ? sig.params[i].name : "arg " + std::to_string(i), f.args[i]);
                std::vector<std::pair<std::string, okay::vs::VsValue>> locals(f.locals.begin(), f.locals.end());
                std::sort(locals.begin(), locals.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
                for (const auto& kv : locals) row("local " + kv.first, kv.second);
                if (f.args.empty() && locals.empty()) ImGui::TextDisabled("(no parameters or locals)");
            }
        }
        auto section = [&](const char* title, const std::vector<std::pair<std::string, okay::vs::VsValue>>& vars, bool open) {
            if (!ImGui::CollapsingHeader(title, open ? ImGuiTreeNodeFlags_DefaultOpen : 0)) return;
            if (vars.empty()) ImGui::TextDisabled("(none)");
            for (const auto& kv : vars) row(kv.first, kv.second);
        };
        const std::string objTitle = "Variables on '" + dv.object + "'";
        section(objTitle.c_str(), dv.vars, true);
        section("Scene variables", dv.sceneVars, false);
        section("Global variables", dv.globalVars, false);
        if (ImGui::CollapsingHeader("Watches", ImGuiTreeNodeFlags_DefaultOpen)) VsIdeDrawWatches(st, live);
        ImGui::Separator();
        ImGui::TextDisabled("Wire values show the last value each node gave. Point at a wire to see what went through it.");
        ImGui::EndChild();
    } else if (st.showScript) {
        ImGui::SameLine();
        ImGui::BeginChild("vsscript", ImVec2(0, 0), true);
        std::vector<const okay::vs::VsIssue*> blocked;
        for (const auto& is : st.exportIssues) blocked.push_back(&is);
        if (!blocked.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.40f, 1.0f), "This graph cannot be exported.");
            ImGui::TextWrapped("These nodes have no script. The export stops instead of skipping them.");
            ImGui::Separator();
            for (const okay::vs::VsIssue* is : blocked) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.48f, 1.0f));
                ImGui::TextWrapped("%s", is->message.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::EndChild();
        } else {
        std::string code = CompileToOkayScript(g);
        ImGui::TextDisabled("OkayScript (generated from the graph)");
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy")) ImGui::SetClipboardText(code.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Save .okay")) {
            std::string p = g.path;
            if (!p.empty()) { auto d = p.rfind(".okayvs"); if (d != std::string::npos) p = p.substr(0, d); }
            else { std::string dir = ed.projectDir(); p = (dir.empty() ? std::string("Script") : dir + "/Assets/Script"); }
            p += ".okay";
            std::ofstream f(p, std::ios::binary);
            if (f) { f << code; Log("Wrote " + p); } else Log("Save failed: " + p, 2);
        }
        ImGui::Separator();
        // Read-only multiline view (a copy buffer; edits don't flow back).
        static std::vector<char> buf;
        buf.assign(code.begin(), code.end()); buf.push_back('\0');
        ImGui::InputTextMultiline("##vsscriptview", buf.data(), buf.size(),
                                  ImVec2(-FLT_MIN, -FLT_MIN), ImGuiInputTextFlags_ReadOnly);
        ImGui::EndChild();
        }
    } else if (st.showDetails) {
        ImGui::SameLine();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
        ImGui::BeginChild("vsdetails", ImVec2(0, 0), true);
        const VsEditNode* sn = st.selected >= 0 ? g.find(st.selected) : nullptr;
        if (!sn && !st.sel.empty()) sn = g.find(*st.sel.begin());
        if (!sn) {
            ImGui::TextUnformatted("Node details");
            ImGui::Spacing();
            ImGui::TextWrapped("Select a node to see what it does, its pins, and any problems the checker found.");
        } else {
            const VsDef* d = VsDefFor(g, *sn);
            ImGui::TextUnformatted(d ? VsNodeTitle(*sn, *d).c_str() : sn->type.c_str());
            ImGui::Spacing();
            bool targetWired = false;
            if (d) for (int ti = 0; ti < (int)d->dataIn.size(); ++ti)
                if (d->dataIn[ti] && std::strcmp(d->dataIn[ti], "Target") == 0) {
                    for (const auto& l : g.dataLinks) if (l.consumer == sn->id && l.inPin == ti) targetWired = true;
                }
            if (const VsNodeDoc* doc = VsNodeDocFor(sn->type)) VsDrawDocBody(*doc, true, vsOwner, targetWired);
            else ImGui::TextDisabled("No description for %s.", sn->type.c_str());
            bool anyIssue = false;
            for (const auto& is : st.issues) if (is.node == sn->id) {
                if (!anyIssue) { ImGui::Spacing(); ImGui::Separator(); ImGui::TextUnformatted("Problems"); anyIssue = true; }
                ImGui::PushStyleColor(ImGuiCol_Text, is.severity == 0 ? ImVec4(1.0f, 0.55f, 0.48f, 1) : ImVec4(0.95f, 0.78f, 0.42f, 1));
                ImGui::TextWrapped("%s", is.message.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::Spacing();
            if (ImGui::Button("Frame this node", ImVec2(-1, 0))) { st.focusNode = sn->id; st.sel = {sn->id}; }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    // The IDE tools' own windows: Graph Navigator (references, bookmarks, watches) and Compare (an earlier save).
    VsIdeDrawNavigator(st, live, vsProjectRoot);
    VsIdeDrawCompare(st);
    // ---- Undo tracking: commit a change once the gesture / edit is finished ----
    VsTrackUndo(st, !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsAnyItemActive());

    // Save prompt. Save is the focused button. Don't Save is not.
    if (Session().ask != VsSession::Ask::None) ImGui::OpenPopup("Save this graph?");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Save this graph?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        VsSession& ses = Session();
        auto dirtyIdx = [&]() {
            std::vector<int> ids;
            for (int i = 0; i < (int)ses.tabs.size(); ++i) {
                if (!ses.tabs[(std::size_t)i]->g.dirty) continue;
                if (ses.ask == VsSession::Ask::CloseTab && i != ses.askTab) continue;
                if (ses.ask == VsSession::Ask::CloseOthers && i == ses.askTab) continue;
                if (ses.ask == VsSession::Ask::NewGraph && i != ses.askTab) continue;
                ids.push_back(i);
            }
            return ids;
        };
        const std::vector<int> ids = dirtyIdx();
        if (ses.ask == VsSession::Ask::NewGraph)
            ImGui::TextUnformatted("Save this graph before opening a new one?");
        else if (ses.ask == VsSession::Ask::CloseWindow)
            ImGui::TextUnformatted("Close the Visual Script window?");
        else if (ids.empty())
            ImGui::TextUnformatted("Close these graphs?");
        else
            ImGui::TextUnformatted("Save these graphs before closing?");
        for (int i : ids) ImGui::BulletText("%s", VsTabTitle(*ses.tabs[(std::size_t)i]).c_str());
        if (ses.ask == VsSession::Ask::CloseWindow && ids.empty())
            ImGui::TextDisabled("Nothing unsaved. The window will hide; the tabs stay.");
        ImGui::Spacing();
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            VsSaveAllTabs(ed.projectDir());
            s_closeRemember = false;
            if (ses.ask == VsSession::Ask::NewGraph) {
                const bool starter = ses.newStarter;
                ses.ask = VsSession::Ask::None;
                NewGraph(starter);
                ImGui::CloseCurrentPopup();
            } else if (ses.ask == VsSession::Ask::CloseTab) s_closeIds.push_back(ses.askTab);
            else if (ses.ask == VsSession::Ask::CloseOthers) {
                for (int i = 0; i < (int)ses.tabs.size(); ++i) if (i != ses.askTab) s_closeIds.push_back(i);
            } else if (ses.ask == VsSession::Ask::CloseAll) {
                for (int i = 0; i < (int)ses.tabs.size(); ++i) s_closeIds.push_back(i);
            } else if (ses.ask == VsSession::Ask::CloseWindow && open) *open = false;
            ses.ask = VsSession::Ask::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ses.ask = VsSession::Ask::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ses.ask == VsSession::Ask::CloseWindow) {
            if (ImGui::Button("Hide", ImVec2(120, 0))) {
                if (open) *open = false;
                ses.ask = VsSession::Ask::None;
                ImGui::CloseCurrentPopup();
            }
        } else if (ImGui::Button("Don't Save", ImVec2(120, 0))) {
            if (ses.ask == VsSession::Ask::NewGraph) {
                const bool starter = ses.newStarter;
                ses.ask = VsSession::Ask::None;
                NewGraph(starter);
                ImGui::CloseCurrentPopup();
            } else {
                s_closeRemember = true;
                if (ses.ask == VsSession::Ask::CloseTab) s_closeIds.push_back(ses.askTab);
                else if (ses.ask == VsSession::Ask::CloseOthers) {
                    for (int i = 0; i < (int)ses.tabs.size(); ++i) if (i != ses.askTab) s_closeIds.push_back(i);
                } else if (ses.ask == VsSession::Ask::CloseAll) {
                    for (int i = 0; i < (int)ses.tabs.size(); ++i) s_closeIds.push_back(i);
                }
                ses.ask = VsSession::Ask::None;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

} // namespace okay::editor::vsed
