#include "ModelingEasyUi.hpp"
#include "ModelingEasyOps.hpp"
#include "ModelingStarters.hpp"
#include "EditorWidgets.hpp"   // StateButton, the clear tab colours
#include "imgui.h"
#include <algorithm>
#include <cstdio>

namespace oked::easymodel_ui {
using namespace oked::easymodel;

namespace {
struct UiState {
    int tab = 0;
    float strength = 0.5f;
    float col[3] = {0.80f, 0.25f, 0.25f};
    std::vector<std::string> recent;
    std::string strengthTool;   // the last tool that uses the "How much" slider (it is previewed while the slider moves)
    okay::Mesh* previewTarget = nullptr;
    StrengthPreview preview;
    bool recentLoaded = false;
    std::string note;           // what the last tool said, in plain words
    float noteAge = 0;
    const Tool* hover = nullptr;      // the tool button under the mouse this frame (shown in the "About" card)
    const Starter* hoverStarter = nullptr;
    float tileW = 150;                // this frame's tile width (TilesFor), shared by every grid on the page
};
UiState g_ui;

const char* ShapeMeshName(const std::string& id) {
    if (id == "add.ball") return "Sphere";
    if (id == "add.box") return "Cube";
    if (id == "add.tube") return "Tube";
    if (id == "add.cone") return "Cone";
    if (id == "add.plane") return "Plane";
    if (id == "add.ring") return "Torus";
    return nullptr;
}

void Say(Host& h, const std::string& msg, int level = 0) {
    g_ui.note = msg; g_ui.noteAge = 0;
    if (h.log && !msg.empty()) h.log(msg, level);
}

void RunTool(Host& h, const Tool& t) {
    const std::string why = WhyNot(t, h.state);
    if (!why.empty()) { Say(h, why, 1); return; }
    const std::string id = t.id;
    if (const char* sn = ShapeMeshName(id)) { if (h.addShape) h.addShape(sn); }
    else if (id == "move")  { if (h.setGizmo) h.setGizmo(0); Say(h, "Drag the arrows on the model to move it."); }
    else if (id == "turn")  { if (h.setGizmo) h.setGizmo(1); Say(h, "Drag the rings on the model to turn it."); }
    else if (id == "bigger"){ if (h.setGizmo) h.setGizmo(2); Say(h, "Drag the boxes on the model to make it bigger."); }
    else if (id == "solid") { if (h.addSolid) h.addSolid(); Say(h, "The model is solid now."); }
    else if (id == "save.asset") { if (h.saveAsset) h.saveAsset(); }
    else if (id == "select.part" || id == "select.similar") {
        if (h.mesh && h.args && h.setPicks) {
            const PickResult pr = RunPickTool(*h.mesh, id, h.args());
            if (pr.ok) { h.setPicks(pr.faces, pr.corners); Say(h, std::string("Picked ") + std::to_string(pr.faces.empty() ? pr.corners.size() : pr.faces.size()) + (pr.faces.empty() ? " corners." : " faces.")); }
            else Say(h, pr.message, 1);
        }
    }
    else if (h.meshOp && h.args) {
        ToolArgs a = h.args();
        a.strength = g_ui.strength; a.r = g_ui.col[0]; a.g = g_ui.col[1]; a.b = g_ui.col[2];
        ToolResult res;
        h.meshOp(t.name, [&](okay::Mesh& m) { res = RunMeshTool(m, id, a); });
        if (!res.message.empty()) Say(h, res.message, res.ok ? 0 : 1);
        else if (res.ok) Say(h, std::string("Done: ") + t.name + ". Undo is at the top if you do not like it.");
    }
    if (UsesStrength(id)) g_ui.strengthTool = id;
    if (t.tab != Tab::Pick) { NoteRecent(g_ui.recent, id); if (h.saveRecent) h.saveRecent(g_ui.recent); }
}

// One big button with the plain name and a tooltip: tip, example, shortcut and, when greyed out, why.
bool ToolBtn(Host& h, const Tool& t, ImVec2 size, const char* idSuffix = "") {
    const std::string why = WhyNot(t, h.state);
    ImGui::BeginDisabled(!why.empty());
    char label[96]; std::snprintf(label, sizeof(label), "%s##easy_%s%s", t.name, t.id, idSuffix);
    const bool hit = ImGui::Button(label, size);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        g_ui.hover = &t;
        if (!why.empty()) ImGui::SetTooltip("%s", why.c_str());
        else ImGui::SetTooltip("%s", TooltipFor(t).c_str());
    }
    return hit;
}

void ToolGrid(Host& h, const std::vector<const Tool*>& tools, float height) {
    int col = 0;
    const TileLayout tl = TilesFor(ImGui::GetContentRegionAvail().x, ImGui::GetStyle().ItemSpacing.x, g_ui.tileW, g_ui.tileW);
    for (const Tool* t : tools) {
        if (col > 0 && col < tl.cols) ImGui::SameLine(); else col = 0;
        if (ToolBtn(h, *t, ImVec2(g_ui.tileW, height))) RunTool(h, *t);
        ++col;
    }
}

// The card under the buttons: what the button under the mouse does, or, with nothing under the mouse, how this step works.
void AboutCard(Host& h, int tab) {
    const float lineH = ImGui::GetTextLineHeightWithSpacing();
    ImGui::Spacing();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 4));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    ImGui::BeginChild("##easyabout", ImVec2(0, lineH * 5.0f + ImGui::GetStyle().WindowPadding.y * 2), ImGuiChildFlags_Borders);
    if (const Tool* t = g_ui.hover) {
        ImGui::TextUnformatted(t->name);
        ImGui::TextWrapped("%s", t->tip);
        ImGui::TextDisabled("For example: %s", t->example);
        const std::string why = WhyNot(*t, h.state);
        if (!why.empty()) ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "First: %s", why.c_str());
        else if (t->key[0]) ImGui::TextDisabled("Shortcut: %s", t->key);
    } else if (const Starter* st = g_ui.hoverStarter) {
        ImGui::TextUnformatted(st->name);
        ImGui::TextWrapped("%s", st->tip);
        ImGui::TextDisabled("Click to add it to the scene, then change it with the other steps.");
    } else {
        ImGui::Text("Step %d of %d: %s", tab + 1, kBeginnerTabCount, TabName((Tab)tab));
        if (tab + 1 < kBeginnerTabCount) ImGui::TextWrapped("Next step, %s: %s", TabName((Tab)(tab + 1)), TabHelp((Tab)(tab + 1)));
        else ImGui::TextWrapped("This is the last step. Your model is ready to use in the scene.");
        ImGui::TextDisabled("Point at a button to see what it does here.");
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}
} // namespace

void DrawEasyModeling(Host& h) {
    const float bigH = std::max(54.0f, ImGui::GetFrameHeight() * 1.6f);
    g_ui.hover = nullptr; g_ui.hoverStarter = nullptr;
    if (!g_ui.recentLoaded) { g_ui.recentLoaded = true; if (h.loadRecent) g_ui.recent = h.loadRecent(); }
    g_ui.noteAge += ImGui::GetIO().DeltaTime;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 10));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 8));

    // Row 1: Undo / Redo / Repeat always on screen, the size of the model.
    {
        const bool cu = h.canUndo && h.canUndo(), cr = h.canRedo && h.canRedo();
        const float rowH = std::max(40.0f, ImGui::GetFrameHeight() * 1.3f);
        // StateButton: a usable button is filled with an accent outline, an unusable one is an empty dim outline.
        if (StateButton("Undo##easy", cu, ImVec2(96, rowH), "Take back the last thing you did.  Ctrl+Z", "Nothing to take back yet.") && h.undo) h.undo();
        ImGui::SameLine();
        if (StateButton("Redo##easy", cr, ImVec2(96, rowH), "Put back what you took back.  Ctrl+Y", "Nothing to put back.") && h.redo) h.redo();
        ImGui::SameLine();
        const std::string rep = RepeatId(g_ui.recent);
        const Tool* rt = rep.empty() ? nullptr : FindTool(rep);
        const bool canRep = rt && WhyNot(*rt, h.state).empty();
        std::string rl = rt ? std::string("Do again: ") + rt->name : std::string("Do again");
        rl += "##easy_repeat";
        const std::string repWhy = !rt ? std::string("Use a tool first, then you can repeat it here.") : WhyNot(*rt, h.state);
        if (StateButton(rl.c_str(), canRep, ImVec2(0, rowH), "Do the last tool again.", repWhy.c_str()) && rt) RunTool(h, *rt);
        if (h.mesh && !h.mesh->vertices.empty()) {
            const okay::Vec3 sz = h.mesh->Size();
            ImGui::SameLine(); ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Size: %s", SizeLine(sz.x, sz.y, sz.z).c_str());
        }
    }

    // Row 2: what to click next.
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.85f, 1.0f, 1.0f));
    ImGui::AlignTextToFramePadding();
    ImGui::Text("What do I click next?  %s", NextHint(h.state).c_str());
    ImGui::PopStyleColor();
    if (!g_ui.note.empty() && g_ui.noteAge < 12.0f) ImGui::TextDisabled("%s", g_ui.note.c_str());

    // Row 3: pick parts (one click turns edit mode on in that mode) and the quick actions for the selection.
    if (h.state.haveObject) {
        ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Pick parts:"); ImGui::SameLine();
        static const char* names[] = {"Corners", "Edges", "Faces"};
        static const char* tips[] = {"Click corners of the model. Key 1.", "Click edges of the model. Key 2.", "Click faces of the model. Key 3."};
        for (int i = 0; i < 3; ++i) {
            const bool on = h.state.editMode && h.state.selMode == i;
            if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            char l[32]; std::snprintf(l, sizeof(l), "%s##easy_pick%d", names[i], i);
            if (ImGui::Button(l, ImVec2(96, 36)) && h.pickMode) h.pickMode(true, i);
            if (on) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tips[i]);
            ImGui::SameLine();
        }
        ImGui::BeginDisabled(!h.state.editMode);
        if (ImGui::Button("Done picking##easy", ImVec2(130, 36)) && h.pickMode) h.pickMode(false, h.state.selMode);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Stop picking parts and go back to the whole model.  Tab");

        ImGui::SameLine();
        for (const Tool* t : ToolsOnTab(Tab::Pick)) { if (ToolBtn(h, *t, ImVec2(0, 36), "_p")) RunTool(h, *t); ImGui::SameLine(); }
        ImGui::NewLine();
        { bool mir = h.mirror;
          if (ImGui::Checkbox("Mirror my changes left to right##easy_mirror", &mir) && h.setMirror) h.setMirror(mir);
          if (ImGui::IsItemHovered()) ImGui::SetTooltip("When you move or change a part on one side, the matching part on the other side changes too.\nTurn it off for a model that is not the same on both sides."); }
        const std::vector<std::string> qa = QuickActionsFor(h.state, g_ui.tab);   // O(1): reads a few numbers, six names at most
        if (!qa.empty()) {
            ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Quick:"); ImGui::SameLine();
            for (const std::string& id : qa) {
                if (const Tool* t = FindTool(id)) { if (ToolBtn(h, *t, ImVec2(0, 36), "_q")) RunTool(h, *t); ImGui::SameLine(); }
            }
            ImGui::NewLine();
        }
        const std::vector<std::string> rc = RecentShown(g_ui.recent, qa, g_ui.tab);   // a copy: RunTool changes the list
        if (!rc.empty()) {
            ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Used lately:"); ImGui::SameLine();
            for (const std::string& id : rc) {
                if (const Tool* t = FindTool(id)) { if (ToolBtn(h, *t, ImVec2(0, 30), "_r")) RunTool(h, *t); ImGui::SameLine(); }
            }
            ImGui::NewLine();
        }
    }
    ImGui::Separator();

    // The five steps, as tabs: the open one is filled with the accent colour (PushClearTabColors) and every tab carries its
    // step number. The tile width is worked out once for every grid of the page (TilesFor in ModelingEasy.hpp).
    {
        float minW = 0;
        for (const Starter& st : Starters()) minW = std::max(minW, ImGui::CalcTextSize(st.name).x);
        for (int i = 0; i < kBeginnerTabCount; ++i) for (const Tool* t : ToolsOnTab((Tab)i)) minW = std::max(minW, ImGui::CalcTextSize(t->name).x);
        minW = std::max(120.0f, minW + ImGui::GetStyle().FramePadding.x * 2 + 8);
        g_ui.tileW = TilesFor(ImGui::GetContentRegionAvail().x, ImGui::GetStyle().ItemSpacing.x, minW, minW * 1.3f).w;
    }
    PushClearTabColors();
    const bool tabsOpen = ImGui::BeginTabBar("##easytabs");
    PopClearTabColors();
    if (tabsOpen) {
        for (int i = 0; i < kBeginnerTabCount; ++i) {
            const Tab tab = (Tab)i;
            char tl[48]; std::snprintf(tl, sizeof(tl), "%d  %s###easytab%d", i + 1, TabName(tab), i);
            PushClearTabColors();
            const bool open = ImGui::BeginTabItem(tl, nullptr, h.openTab == i ? ImGuiTabItemFlags_SetSelected : 0);
            PopClearTabColors();
            if (!open) continue;
            g_ui.tab = i;
            ImGui::TextDisabled("%s", TabHelp(tab));
            if (tab == Tab::Shape) {
                ImGui::TextUnformatted("Start from something ready-made");
                const TileLayout tl = TilesFor(ImGui::GetContentRegionAvail().x, ImGui::GetStyle().ItemSpacing.x, g_ui.tileW, g_ui.tileW);
                const float tileH = std::max(100.0f, 68.0f + ImGui::GetTextLineHeight() + 14.0f);
                int col = 0;
                for (const Starter& s : Starters()) {
                    if (col > 0 && col < tl.cols) ImGui::SameLine(); else col = 0;
                    ++col;
                    char l[64]; std::snprintf(l, sizeof(l), "##easy_st_%s", s.id);
                    const bool hit = ImGui::Button(l, ImVec2(g_ui.tileW, tileH));
                    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                    if (h.thumb) {
                        std::string id = s.id;
                        if (void* th = h.thumb(std::string("easy_") + id, [id] { return BuildStarter(id); }))
                            ImGui::GetWindowDrawList()->AddImage((ImTextureID)th, ImVec2((a.x + b.x) * 0.5f - 32, a.y + 6), ImVec2((a.x + b.x) * 0.5f + 32, a.y + 70));
                    }
                    const ImVec2 ts = ImGui::CalcTextSize(s.name);
                    ImGui::GetWindowDrawList()->AddText(ImVec2((a.x + b.x) * 0.5f - ts.x * 0.5f, b.y - ts.y - 8), ImGui::GetColorU32(ImGuiCol_Text), s.name);
                    if (ImGui::IsItemHovered()) { g_ui.hoverStarter = &s; ImGui::SetTooltip("%s\nClick to add it, then change it with the other tabs.", s.tip); }
                    if (hit && h.addModel) { h.addModel(BuildStarter(s.id), s.name); Say(h, std::string("Added a ") + s.name + ". Change it with the other tabs."); }
                }
                ImGui::Spacing();
                ImGui::TextUnformatted("Or add a simple shape");
            }
            if (tab == Tab::Reshape || tab == Tab::Details) {
                ImGui::SetNextItemWidth(220);
                ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 26.0f);   // a handle you can grab
                const bool moved = ImGui::SliderFloat("How much##easy", &g_ui.strength, 0.1f, 1.0f, "%.2f");
                ImGui::PopStyleVar();
                {   // Live preview while the slider moves; the tool is applied once, when the slider is let go.
                    const Tool* st = g_ui.strengthTool.empty() ? nullptr : FindTool(g_ui.strengthTool);
                    if (moved && st && h.mesh && h.args && WhyNot(*st, h.state).empty()) {
                        ToolArgs a = h.args(); a.strength = g_ui.strength; a.r = g_ui.col[0]; a.g = g_ui.col[1]; a.b = g_ui.col[2];
                        if (g_ui.preview.active && g_ui.previewTarget != h.mesh) g_ui.preview.active = false;   // the model changed under us: drop the old copy
                        g_ui.previewTarget = h.mesh;
                        g_ui.preview.Update(*h.mesh, st->id, a);
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit() && g_ui.preview.active) {
                        if (h.mesh && g_ui.previewTarget == h.mesh) g_ui.preview.End(*h.mesh); else g_ui.preview.active = false;
                        if (st) RunTool(h, *st);
                    }
                    if (st) ImGui::SameLine(), ImGui::TextDisabled("Drag to try another amount of: %s", st->name);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Small = a little, big = a lot. Used by Squash, Smooth it, Round all edges and Round picked corners.");
            }
            if (tab == Tab::Colour) {
                static const float sw[][3] = {{0.85f,0.2f,0.2f},{0.95f,0.55f,0.15f},{0.95f,0.85f,0.2f},{0.3f,0.7f,0.3f},{0.2f,0.6f,0.85f},{0.25f,0.3f,0.8f},
                                              {0.6f,0.3f,0.75f},{0.95f,0.5f,0.7f},{0.45f,0.3f,0.15f},{0.95f,0.95f,0.95f},{0.5f,0.5f,0.5f},{0.1f,0.1f,0.12f}};
                ImGui::TextUnformatted("Pick a colour");
                for (int i = 0; i < 12; ++i) {
                    if (i) ImGui::SameLine();
                    char l[24]; std::snprintf(l, sizeof(l), "##easy_sw%d", i);
                    if (ImGui::ColorButton(l, ImVec4(sw[i][0], sw[i][1], sw[i][2], 1), 0, ImVec2(36, 36))) { g_ui.col[0] = sw[i][0]; g_ui.col[1] = sw[i][1]; g_ui.col[2] = sw[i][2]; }
                }
                ImGui::SetNextItemWidth(260);
                ImGui::ColorEdit3("Any colour##easy", g_ui.col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                ImGui::SameLine(); ImGui::TextDisabled("Your colour");
            }
            ToolGrid(h, ToolsOnTab(tab), bigH);
            AboutCard(h, i);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::PopStyleVar(2);
}

} // namespace oked::easymodel_ui
