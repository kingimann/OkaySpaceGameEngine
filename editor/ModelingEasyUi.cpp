#include "ModelingEasyUi.hpp"
#include "ModelingEasyOps.hpp"
#include "ModelingStarters.hpp"
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
        if (!why.empty()) ImGui::SetTooltip("%s", why.c_str());
        else ImGui::SetTooltip("%s", TooltipFor(t).c_str());
    }
    return hit;
}

void ToolGrid(Host& h, const std::vector<const Tool*>& tools, ImVec2 size) {
    const float w = ImGui::GetContentRegionAvail().x, gap = ImGui::GetStyle().ItemSpacing.x;
    float x = 0;
    for (const Tool* t : tools) {
        if (x > 0 && x + size.x > w) x = 0; else if (x > 0) ImGui::SameLine();
        if (ToolBtn(h, *t, size)) RunTool(h, *t);
        x += size.x + gap;
    }
}
} // namespace

void DrawEasyModeling(Host& h, bool& showAdvanced) {
    const ImVec2 big(150, 54);
    if (!g_ui.recentLoaded) { g_ui.recentLoaded = true; if (h.loadRecent) g_ui.recent = h.loadRecent(); }
    g_ui.noteAge += ImGui::GetIO().DeltaTime;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 10));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 8));

    // Row 1: Undo / Redo / Repeat always on screen, the size of the model, the switch to the full window.
    {
        const bool cu = h.canUndo && h.canUndo(), cr = h.canRedo && h.canRedo();
        ImGui::BeginDisabled(!cu);
        if (ImGui::Button("Undo##easy", ImVec2(96, 40)) && h.undo) h.undo();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(cu ? "Take back the last thing you did.  Ctrl+Z" : "Nothing to take back yet.");
        ImGui::SameLine();
        ImGui::BeginDisabled(!cr);
        if (ImGui::Button("Redo##easy", ImVec2(96, 40)) && h.redo) h.redo();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(cr ? "Put back what you took back.  Ctrl+Y" : "Nothing to put back.");
        ImGui::SameLine();
        const std::string rep = RepeatId(g_ui.recent);
        const Tool* rt = rep.empty() ? nullptr : FindTool(rep);
        const bool canRep = rt && WhyNot(*rt, h.state).empty();
        ImGui::BeginDisabled(!canRep);
        std::string rl = rt ? std::string("Do again: ") + rt->name : std::string("Do again");
        rl += "##easy_repeat";
        if (ImGui::Button(rl.c_str(), ImVec2(0, 40)) && rt) RunTool(h, *rt);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(rt ? "Do the last tool again." : "Use a tool first, then you can repeat it here.");
        if (h.mesh && !h.mesh->vertices.empty()) {
            const okay::Vec3 sz = h.mesh->Size();
            ImGui::SameLine(); ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Size: %s", SizeLine(sz.x, sz.y, sz.z).c_str());
        }
        const char* sw = "Show advanced tools";
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(sw).x - 40));
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 12));   // a bigger tick box: easy to find and to hit
        ImGui::Checkbox("Show advanced tools##easy", &showAdvanced);
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Switch to the full toolset for people who know 3D modeling.\nIt is remembered next time.");
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

    // The five tabs.
    if (ImGui::BeginTabBar("##easytabs")) {
        for (int i = 0; i < kBeginnerTabCount; ++i) {
            const Tab tab = (Tab)i;
            if (!ImGui::BeginTabItem(TabName(tab), nullptr, h.openTab == i ? ImGuiTabItemFlags_SetSelected : 0)) continue;
            g_ui.tab = i;
            ImGui::TextDisabled("%s", TabHelp(tab));
            if (tab == Tab::Shape) {
                ImGui::TextUnformatted("Start from something ready-made");
                const float w = ImGui::GetContentRegionAvail().x;
                float x = 0;
                for (const Starter& s : Starters()) {
                    if (x > 0 && x + 110 > w) x = 0; else if (x > 0) ImGui::SameLine();
                    char l[64]; std::snprintf(l, sizeof(l), "##easy_st_%s", s.id);
                    const bool hit = ImGui::Button(l, ImVec2(110, 96));
                    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                    if (h.thumb) {
                        std::string id = s.id;
                        if (void* th = h.thumb(std::string("easy_") + id, [id] { return BuildStarter(id); }))
                            ImGui::GetWindowDrawList()->AddImage((ImTextureID)th, ImVec2((a.x + b.x) * 0.5f - 32, a.y + 4), ImVec2((a.x + b.x) * 0.5f + 32, a.y + 68));
                    }
                    const ImVec2 ts = ImGui::CalcTextSize(s.name);
                    ImGui::GetWindowDrawList()->AddText(ImVec2((a.x + b.x) * 0.5f - ts.x * 0.5f, b.y - ts.y - 6), ImGui::GetColorU32(ImGuiCol_Text), s.name);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nClick to add it, then change it with the other tabs.", s.tip);
                    if (hit && h.addModel) { h.addModel(BuildStarter(s.id), s.name); Say(h, std::string("Added a ") + s.name + ". Change it with the other tabs."); }
                    x += 110 + ImGui::GetStyle().ItemSpacing.x;
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
            ToolGrid(h, ToolsOnTab(tab), big);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::PopStyleVar(2);
}

} // namespace oked::easymodel_ui
