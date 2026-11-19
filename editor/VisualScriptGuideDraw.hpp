#pragma once
// Paints the "What is this?" card for the selected Visual Script node, with its "Next steps" chips and the
// "Try this node" button. The words and the rules (what goes in the card, which nodes follow, the try graph) are
// in VisualScriptGuide.hpp and are tested; this file only draws them and handles the clicks.
// The card stays in the bottom-right corner of the canvas, always visible while a node is selected. The little
// bar at its top collapses it, and the choice is remembered (okay_vs_usage.txt, "helpcard").
// Included from VisualScriptEditor.hpp after the editor state exists.
#include "VisualScriptGuide.hpp"

namespace okay::editor::vsed {

/// Add `step.type` next to `from` and wire it the way the suggestion says. Returns the new node id (or -1).
inline int VsAddNextStep(VsEditGraph& g, int fromId, const VsNextStep& step) {
    const VsEditNode* from = g.find(fromId);
    if (!from) return -1;
    int below = 0;
    for (const VsExecLink& l : g.execLinks) if (l.from == fromId) ++below;
    for (const VsDataLink& l : g.dataLinks) if (l.producer == fromId) ++below;
    const int id = g.add(step.type, from->x + kVsEstWidth + kVsGapX, from->y + (float)below * 120.0f);
    if (step.exec) g.linkExec(fromId, step.outPin, id);
    else g.linkData(id, step.inPin, fromId, step.outPin);
    return id;
}

/// Draws the card. `canvasMin` / `canvasMax` are the canvas corners (screen). Returns true when the card took the
/// mouse this frame (so the canvas should not also start a box select under it).
inline bool VsDrawHelpCard(VsEditState& st, ImVec2 canvasMin, ImVec2 canvasMax) {
    VsEditGraph& g = st.g;
    const VsEditNode* sn = st.selected >= 0 ? g.find(st.selected) : nullptr;
    if (!sn) return false;
    const VsDef* d = VsDefFor(g, *sn);
    if (!d) return false;
    const std::vector<VsDef>& all = Registry();
    const float W = std::min(360.0f, std::max(220.0f, canvasMax.x - canvasMin.x - 32.0f));
    const float maxH = std::max(120.0f, (canvasMax.y - canvasMin.y) * 0.62f);
    const bool open = VsPrefs().helpCardOpen;
    ImGui::SetCursorScreenPos(ImVec2(canvasMax.x - W - 14.0f, canvasMax.y - (open ? maxH : 34.0f) - 14.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(26, 30, 40, 244));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(110, 160, 235, 190));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
    bool took = false;
    ImGui::BeginChild("##vshelpcard", ImVec2(W, open ? maxH : 34.0f), ImGuiChildFlags_Borders, open ? 0 : ImGuiWindowFlags_NoScrollbar);
    took = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    // Header bar: the title, and a button that folds the card up or down.
    const std::string title = std::string("What is this?  ") + (d->title ? d->title : d->type);
    ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "%s", title.c_str());
    ImGui::SameLine(W - 70.0f);
    if (ImGui::SmallButton(open ? "Hide" : "Show")) { VsPrefs().helpCardOpen = !open; VsPrefsSave(); }
    if (open) {
        const VsHelpCard c = VsHelpCardFor(*d, &all);
        ImGui::Separator();
        ImGui::PushTextWrapPos(W - 24.0f);
        ImGui::TextWrapped("%s", c.does.c_str());
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.45f, 1.0f), "What to plug in");
        for (const auto& pl : c.plugs) ImGui::BulletText("%s: %s", pl.first.c_str(), pl.second.c_str());
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.62f, 0.85f, 0.65f, 1.0f), "A tiny example");
        std::string chain;
        for (std::size_t i = 0; i < c.chain.size(); ++i) chain += (i ? "  ->  " : "") + c.chain[i];
        ImGui::TextWrapped("%s", chain.c_str());
        ImGui::TextDisabled("%s", c.example.c_str());
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.48f, 1.0f), "Common mistakes");
        for (const std::string& m : c.mistakes) ImGui::BulletText("%s", m.c_str());
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "%s", c.next.c_str());
        ImGui::Spacing();
        // Next steps: one click adds the node and wires it in.
        const std::vector<VsNextStep> next = VsNextSteps(*d, all, 4);
        if (!next.empty()) {
            ImGui::TextUnformatted("Next steps (click to add and connect)");
            for (std::size_t i = 0; i < next.size(); ++i) {
                ImGui::PushID((int)i);
                if (i) ImGui::SameLine();
                if (ImGui::SmallButton(next[i].title.c_str())) {
                    const int id = VsAddNextStep(g, st.selected, next[i]);
                    if (id >= 0) { st.sel = {id}; st.selected = id; st.focusNode = id; }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", next[i].line.c_str());
                ImGui::PopID();
            }
            ImGui::Spacing();
        }
        if (ImGui::Button("Try this node", ImVec2(-FLT_MIN, 0.0f))) {
            VsEditGraph tmp;
            st.templateText = VsTryGraph(*d, tmp);
            st.pasteTemplate = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Adds a small ready-made graph that uses this node. Press Play to see it work.");
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    return took;
}

}  // namespace okay::editor::vsed
