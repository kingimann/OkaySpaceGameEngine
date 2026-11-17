#pragma once
// Drawing of the module checklist and the preset buttons (Dear ImGui), over ModulesModel.hpp. Used by the New
// Project form (second part: "Modules") and by Edit > Project Settings > Modules, and by tools/modules_shots.cpp,
// which draws the same code into a software surface for pictures. Written for this repository.
#include "ModulesModel.hpp"
#include <imgui.h>
#include <cstdio>
#include <map>
#include <string>

namespace okaymod {

/// Preset buttons in a row; the one matching the selection is lit. True when a preset was applied.
inline bool DrawPresetButtons(Selection& sel) {
    bool changed = false;
    const std::string current = sel.MatchingPreset();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Start from");
    for (auto& p : Presets()) {
        ImGui::SameLine();
        const bool lit = p.id == current;
        if (lit) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button((p.title + "##preset_" + p.id).c_str())) { sel.ApplyPreset(p.id); changed = true; }
        if (lit) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.blurb.c_str());
    }
    return changed;
}

/// Text in a colour, wrapped to the window (long messages are never clipped).
inline void WrappedColored(const ImVec4& col, const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

/// "27 of 40 modules are on" (the always-on core counts) and which preset that is.
inline std::string SummaryText(const Selection& sel, size_t total) {
    size_t on = 0;
    for (auto& r : sel.Rows()) if (r.on) ++on;
    const std::string pre = sel.MatchingPreset();
    std::string name = "custom";
    for (auto& p : Presets()) if (p.id == pre) name = p.title;
    char buf[200];
    std::snprintf(buf, sizeof(buf), "%zu of %zu modules are on (%s).", on, total, name.c_str());
    return buf;
}

/// The checklist, grouped by area. `usage` (optional) adds "used by N objects in the open scene". `note` keeps the
/// "also turned on" / "can't turn off" message between frames. True when a tick changed.
inline bool DrawChecklist(Selection& sel, const std::map<std::string, int>* usage, std::string& note, float height) {
    bool changed = false;
    ImGui::BeginChild("##modlist", ImVec2(0, height), ImGuiChildFlags_Borders);
    std::string area;
    for (auto& r : sel.Rows()) {
        if (r.area != area) {
            area = r.area;
            ImGui::Dummy(ImVec2(0, 2));
            ImGui::TextDisabled("%s", area.c_str());
        }
        ImGui::PushID(r.name.c_str());
        bool on = r.on;
        ImGui::BeginDisabled(r.locked);
        if (ImGui::Checkbox("##on", &on)) {
            const Selection::Change c = sel.Set(r.name, on);
            changed = true;
            if (!c.ok && !c.blockedBy.empty()) {
                note = "Can't turn off " + r.title + ": ";
                for (size_t i = 0; i < c.blockedBy.size(); ++i) note += (i ? ", " : "") + c.blockedBy[i];
                note += " need it.";
            } else if (!c.alsoOn.empty()) {
                note = r.title + " also needs: ";
                for (size_t i = 0; i < c.alsoOn.size(); ++i) {
                    const auto* mi = ModuleRegistry::Get().Find(c.alsoOn[i]);
                    note += (i ? ", " : "") + (mi ? mi->manifest.title : c.alsoOn[i]);
                }
                note += " (turned on too).";
            } else note.clear();
        }
        ImGui::EndDisabled();
        if (r.locked && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", r.lockReason.c_str());
        ImGui::SameLine();
        ImGui::TextUnformatted(r.title.c_str());
        if (ImGui::IsItemHovered()) {
            std::string tip = r.description;
            const std::string needs = sel.NeedsText(r);
            if (!needs.empty()) tip += "\nAlso needs: " + needs;
            ImGui::SetTooltip("%s", tip.c_str());
        }
        if (usage) {
            auto it = usage->find(r.name);
            if (it != usage->end()) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.55f, 0.75f, 0.95f, 1.0f), "  used by %d object%s", it->second, it->second == 1 ? "" : "s");
            }
        }
        // The description on its own line under the title, wrapped to the list width (never cut).
        ImGui::Indent(26.0f);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", r.description.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Unindent(26.0f);
        ImGui::PopID();
    }
    ImGui::EndChild();
    return changed;
}

} // namespace okaymod
