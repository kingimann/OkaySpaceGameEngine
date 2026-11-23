#pragma once
// The Inspector of a character controller (one view, no Simple / Advanced split) (claude/controller-1). All the logic is in
// okay/Components/ControllerTuning.hpp (tested headless); this file only draws it with Dear ImGui:
//   * the five feel presets as buttons,
//   * plain-name settings with units, a tooltip and a per-setting reset button,
//   * plain-language warnings with a suggested fix,
//   * Save / Load of a .okaycontroller text file (shareable between projects).
// Several controllers on one object (a Player Controller with both views) are kept in step: the settings are
// written to the other one as well. Compiled in the editor only; not run in a window here ("unverified").
#include "EditorState.hpp"
#include "ConsolePanel.hpp"
#include "okay/Components/ControllerPresets.hpp"
#include "okay/Components/ControllerTuning.hpp"
#include "imgui.h"
#include <fstream>
#include <sstream>
#include <string>

namespace oked {
namespace ctrlui {

inline std::string& PresetPath() { static std::string p = "MyController.okaycontroller"; return p; }
inline std::string& Message() { static std::string m; return m; }

/// `c` is the controller that is read; `other` (may be null) is kept in step. Returns true if anything changed.
template <class C, class D>
bool DrawSimple(okay::editor::EditorState& ed, C& c, D* other, const char* id) {
    using T = okay::ControllerTuning;
    bool changed = false;
    ImGui::PushID(id);
    auto sync = [&]() { if (other) T::Load(*other, T::Save(c)); };

    ImGui::TextDisabled("Feel (one click):");
    for (int i = 0; i < okay::ControllerPresets::Count; ++i) {
        ImGui::SameLine(0, 4);
        if (ImGui::SmallButton(okay::ControllerPresets::Name(i))) {
            ed.PushUndo();
            okay::ControllerPresets::Apply(c, i);
            if (other) okay::ControllerPresets::Apply(*other, i);
            changed = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", okay::ControllerPresets::About(i));
    }

    const char* section = "";
    for (const T::Setting* s : T::Visible(c)) {
        if (std::string(s->section) != section) { section = s->section; ImGui::SeparatorText(section); }
        float v = T::Get(c, s->id);
        bool edited = false;
        if (s->toggle) {
            bool on = v > 0.5f;
            const std::string label = std::string(s->label) + "##" + s->id;
            if (ImGui::Checkbox(label.c_str(), &on)) { v = on ? 1.0f : 0.0f; edited = true; }
        } else {
            const std::string label = std::string(s->label) + " (" + s->unit + ")##" + s->id;
            edited = ImGui::DragFloat(label.c_str(), &v, (s->hi - s->lo) / 200.0f, s->lo, s->hi, "%.2f");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", s->tooltip);
        if (edited) {
            if (!ed.isPlaying()) ed.PushUndo();   // in Play mode the change is live and is not an undo step
            T::Set(c, s->id, v); sync(); changed = true;
        }
        if (!T::IsDefault(c, s->id)) {
            ImGui::SameLine();
            if (ImGui::SmallButton((std::string("Reset##r") + s->id).c_str())) {
                ed.PushUndo(); T::Reset(c, s->id); sync(); changed = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Put %s back to the default.", s->label);
        }
    }

    for (const T::Issue& i : T::Validate(c)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.75f, 0.35f, 1.0f));
        ImGui::TextWrapped("%s %s", i.message.c_str(), i.fix.c_str());
        ImGui::PopStyleColor();
    }

    char buf[256];
    std::snprintf(buf, sizeof buf, "%s", PresetPath().c_str());
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::InputText("##path", buf, sizeof buf)) PresetPath() = buf;
    ImGui::SameLine();
    if (ImGui::SmallButton("Save settings")) {
        std::ofstream f(PresetPath());
        if (f) { f << T::Save(c); Message() = "Saved " + PresetPath(); } else Message() = "Could not write " + PresetPath();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Load settings")) {
        std::ifstream f(PresetPath());
        std::stringstream ss; ss << f.rdbuf();
        ed.PushUndo();
        const int n = f ? T::Load(c, ss.str()) : -1;
        if (n >= 0) { sync(); changed = true; Message() = "Loaded " + std::to_string(n) + " settings from " + PresetPath(); }
        else Message() = "Not a controller settings file: " + PresetPath();
    }
    if (!Message().empty()) ImGui::TextDisabled("%s", Message().c_str());
    ImGui::PopID();
    if (changed) ed.dirty = true;
    return changed;
}

} // namespace ctrlui
} // namespace oked
