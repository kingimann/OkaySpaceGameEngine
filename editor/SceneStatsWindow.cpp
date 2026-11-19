// The Scene Stats window and the Run order settings. Written from our own design (docs/ecs_guide.md).
#include "SceneStatsWindow.hpp"
#include "okay/Scene/ExecutionOrder.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include "okay/Scene/Scene.hpp"
#include "imgui.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_map>

namespace oked {

bool g_showSceneStats = false;

void DrawSceneStatsWindow(okay::editor::EditorState& ed) {
    if (!g_showSceneStats) return;
    if (!ImGui::Begin("Scene Stats", &g_showSceneStats)) { ImGui::End(); return; }
    okay::Scene& scene = ed.scene();
    const okay::SceneGraphStats st = scene.Stats();

    ImGui::Text("Objects: %d", st.objects);
    ImGui::SameLine(); ImGui::TextDisabled("(%d top level, %d switched off, %d levels deep)", st.roots, st.inactive, st.maxDepth + 1);
    ImGui::Text("Components: %d", st.components);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Every piece attached to an object, the Transform of each object included.");
    ImGui::Text("Running every frame: %d", st.runningComponents);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Components that get Update each frame. A switched-off one stays on the list but is skipped.");
    if (st.awaitingStart > 0) ImGui::Text("Waiting to start: %d (they join at the next frame)", st.awaitingStart);
    if (st.pendingDestroy > 0) ImGui::Text("About to be destroyed: %d (freed at the end of the frame)", st.pendingDestroy);

    ImGui::Spacing();
    bool timing = scene.ComponentTimingOn();
    if (ImGui::Checkbox("Measure time per type", &timing)) scene.SetComponentTiming(timing);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Times every Update call while the game runs (press Play) and shows the total per component type.\nOff by default because reading the clock for every call costs a little.");
    if (timing && scene.LastFrameTimings().empty()) ImGui::TextDisabled("Press Play: times appear once the game runs.");

    std::unordered_map<std::string, const okay::Scene::ComponentTiming*> byName;
    double total = 0.0;
    for (const auto& t : scene.LastFrameTimings()) { byName[t.type] = &t; total += t.milliseconds; }
    ImGui::Spacing();
    ImGui::TextDisabled("Components by type");
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("##scenestatstypes", timing ? 4 : 2, flags, ImVec2(0, ImGui::GetContentRegionAvail().y))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("How many", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        if (timing) {
            ImGui::TableSetupColumn("Update ms", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Share", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        }
        ImGui::TableHeadersRow();
        for (const auto& tc : st.byType) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(tc.type.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::Text("%d", tc.count);
            if (timing) {
                auto it = byName.find(tc.type);
                ImGui::TableSetColumnIndex(2);
                if (it != byName.end()) ImGui::Text("%.3f", it->second->milliseconds); else ImGui::TextDisabled("-");
                ImGui::TableSetColumnIndex(3);
                if (it != byName.end() && total > 0.0) ImGui::Text("%.0f%%", 100.0 * it->second->milliseconds / total); else ImGui::TextDisabled("-");
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void DrawRunOrderSettings(okay::editor::EditorState& ed, void (*save)()) {
    (void)ed;
    using okay::ExecutionOrderSettings;
    using okay::RunWhen;
    ImGui::TextWrapped("Each frame, components run in a fixed order. Most do not care. If one must come first (it reads input) or last (it reads what the others did), pick it here. Inside one group, components run in the order they were added.");
    const auto rules = ExecutionOrderSettings::All();   // a copy: a row below edits the list
    if (rules.empty()) ImGui::TextDisabled("Nothing set: every type runs Normal.");
    for (std::size_t i = 0; i < rules.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(rules[i].first.c_str());
        ImGui::SameLine(220.0f);
        int cur = rules[i].second == RunWhen::Early ? 0 : rules[i].second == RunWhen::Normal ? 1 : 2;
        ImGui::SetNextItemWidth(120);
        const char* names[] = {"Run early", "Normal", "Run late"};
        if (ImGui::Combo("##when", &cur, names, 3)) {
            ExecutionOrderSettings::Set(rules[i].first, cur == 0 ? RunWhen::Early : cur == 1 ? RunWhen::Normal : RunWhen::Late);
            if (save) save();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Run early: before every other component each frame.\nNormal: the component's own order.\nRun late: after every other component.");
        ImGui::PopID();
    }
    // Add a type from the list of every component type the editor knows.
    static std::string picked;
    ImGui::SetNextItemWidth(220);
    if (ImGui::BeginCombo("##addruntype", picked.empty() ? "Add a component type..." : picked.c_str())) {
        std::vector<std::string> all;
        for (const auto& info : okay::PropertyRegistry::All())
            if (info && !info->objectLevel && ExecutionOrderSettings::Get(info->name) == RunWhen::Normal) all.push_back(info->name);
        std::sort(all.begin(), all.end());
        for (const std::string& n : all)
            if (ImGui::Selectable(n.c_str(), n == picked)) picked = n;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Run early##addrun") && !picked.empty()) { ExecutionOrderSettings::Set(picked, RunWhen::Early); picked.clear(); if (save) save(); }
    ImGui::SameLine();
    if (ImGui::Button("Run late##addrun") && !picked.empty()) { ExecutionOrderSettings::Set(picked, RunWhen::Late); picked.clear(); if (save) save(); }
}

} // namespace oked
