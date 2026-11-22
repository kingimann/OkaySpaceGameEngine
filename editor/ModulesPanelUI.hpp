#pragma once
// Drawing of the module checklist and the preset buttons (Dear ImGui), over ModulesModel.hpp: Edit > Project Settings >
// Modules (DrawModulesSettings) and the New Project form (DrawNewProjectModules). Written for this repository.
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

/// Edit > Project Settings > Modules. `modules` is the project's saved list (project.okayproj modules=, "" = every
/// module); `scene` is the open scene (what it uses is shown, and a module it uses that is off gets a warning).
/// Turning a module off never edits a scene. True when the list changed (the caller saves the project).
inline bool DrawModulesSettings(std::string& modules, const okay::Scene& scene, std::string& note) {
    Selection sel(ModuleRegistry::Get(), modules);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("The parts of the engine this game uses. Turning one on also turns on what it needs; a module "
                        "that another one needs stays on. In this version every module is still inside the built game: "
                        "the choice is saved with the project and the Build report lists it.");
    ImGui::PopTextWrapPos();
    const auto usage = UsageInScene(ModuleRegistry::Get(), scene);
    bool changed = DrawPresetButtons(sel);
    ImGui::TextDisabled("%s", SummaryText(sel, sel.Rows().size()).c_str());
    changed |= DrawChecklist(sel, &usage, note, 260.0f);
    if (!note.empty()) WrappedColored(ImVec4(0.95f, 0.80f, 0.35f, 1.0f), note);
    for (auto& u : UsedButOff(sel, usage)) {
        const auto* mi = ModuleRegistry::Get().Find(u.first);
        WrappedColored(ImVec4(0.96f, 0.55f, 0.50f, 1.0f),
            "The open scene uses " + std::to_string(u.second) + " object(s) of a module that is off: " + (mi ? mi->manifest.title : u.first) + ".");
    }
    if (modules.empty() && !changed) ImGui::TextDisabled("Every module is on.");
    if (changed) modules = sel.ToText();
    return changed;
}

/// The "Modules" part of the New Project form. `modules` is what the new project will save ("" = every module);
/// a new template picks its preset (PresetForTemplate) until the person changes something (`touched`).
/// `matchedTpl` remembers which template the modules were last matched to. Folded by default: one line says
/// which preset is chosen.
inline void DrawNewProjectModules(std::string& modules, bool& touched, int& matchedTpl, int tpl, const std::string& tplTitle,
                                  std::string& note) {
    if (matchedTpl != tpl) {
        matchedTpl = tpl;
        if (!touched) {
            Selection pick(ModuleRegistry::Get());
            pick.ApplyPreset(PresetForTemplate(tplTitle));
            modules = pick.ToText();
        }
    }
    Selection sel(ModuleRegistry::Get(), modules);
    const std::string summary = SummaryText(sel, sel.Rows().size());
    if (ImGui::TreeNode("##npmods", "Modules: %s", summary.c_str())) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Which parts of the engine the game uses (Project Settings > Modules changes it later). Every module "
                            "is still inside the built game in this version.");
        ImGui::PopTextWrapPos();
        bool changed = DrawPresetButtons(sel);
        changed |= DrawChecklist(sel, nullptr, note, 120.0f);
        if (!note.empty()) WrappedColored(ImVec4(0.95f, 0.80f, 0.35f, 1.0f), note);
        if (changed) { modules = sel.ToText(); touched = true; }
        ImGui::TreePop();
    }
}

} // namespace okaymod
