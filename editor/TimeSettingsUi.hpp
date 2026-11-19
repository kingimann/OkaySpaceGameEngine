#pragma once
// Edit > Project Settings > Time: the controls for okay::TimeSettings (Core/TimeSettings.hpp holds the data,
// the saving and the checks; this file only draws them). Every label has a plain tooltip. Returns true when
// something changed, so the caller saves the project and hands the settings to the Play loop.
#include "imgui.h"
#include "okay/Core/TimeSettings.hpp"
#include <cstdio>
#include <string>

namespace oked {

inline bool DrawTimeSettings(okay::TimeSettings& t) {
    bool changed = false;
    ImGui::TextWrapped("How fast time runs in your game, what happens when the computer stalls, and the clocks. "
                       "Everything here is saved with the project and used by Play and by the built game.");
    ImGui::SetNextItemWidth(180);
    if (ImGui::SliderFloat("Game speed at start##tm", &t.timeScale, 0.0f, 4.0f, "%.2f")) changed = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("1 is normal. 0.5 starts the game in slow motion, 0 starts it frozen.\nScripts can change it any time (Time.timeScale).");
    ImGui::SetNextItemWidth(180);
    if (ImGui::DragFloat("Longest frame (seconds)##tm", &t.maxFrameSeconds, 0.005f, 0.01f, 5.0f, "%.3f")) changed = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("If the computer stalls (a big load, dragging the window), the game acts as if that frame only lasted this long,\nso nothing jumps ahead. 0.1 is the usual value.");
    ImGui::SetNextItemWidth(180);
    if (ImGui::DragFloat("Catch up after a stall (seconds)##tm", &t.maxCatchUpSeconds, 0.01f, 0.02f, 5.0f, "%.3f")) changed = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("After a stall, physics may run this much missed time in one frame to catch up.\nLower is safer on slow computers; higher keeps fast action closer to real time.");
    {
        int policy = t.spreadCatchUp ? 1 : 0;
        const char* names[] = {"Forget it (the game falls behind)", "Catch up over the next frames"};
        ImGui::SetNextItemWidth(280);
        if (ImGui::Combo("Time lost in a stall##tm", &policy, names, 2)) { t.spreadCatchUp = policy == 1; changed = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Forget it: time beyond the catch-up is thrown away. The game never runs fast, it just ends up a little behind.\nCatch up: up to one second of it is paid back, a little at a time, over the next frames.");
    }
    {
        int fps = (int)(t.unfocusedFps + 0.5f);
        ImGui::SetNextItemWidth(180);
        if (ImGui::InputInt("Frame rate when the window is not active##tm", &fps, 5, 30)) {
            t.unfocusedFps = fps <= 0 ? 0.0f : (float)(fps < 5 ? 5 : fps);
            changed = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Saves battery and heat while you are in another window.\n0 means no extra limit. 30 is a good choice.");
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Clocks");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A clock is a timer that can run faster, slower or stop on its own.\nGame, UI and Cinematic always exist. A clock \"under\" another one follows it:\nslow the Game clock and everything under it slows too, while the UI clock keeps going.");
    ImGui::TextDisabled("Built in: game (stops when paused), ui (never stops), cinematic (stops when paused, not slowed)");
    int remove = -1;
    for (std::size_t i = 0; i < t.clocks.size(); ++i) {
        okay::ClockSpec& c = t.clocks[i];
        ImGui::PushID((int)i);
        char name[64]; std::snprintf(name, sizeof(name), "%s", c.name.c_str());
        ImGui::SetNextItemWidth(130);
        if (ImGui::InputText("##name", name, sizeof(name))) { c.name = name; changed = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The clock's name, as scripts and timers use it.");
        ImGui::SameLine();
        // The "under" choice: real time, the built-in clocks, or another listed clock.
        const char* label = c.parent.empty() ? "real time" : c.parent.c_str();
        ImGui::SetNextItemWidth(130);
        if (ImGui::BeginCombo("##under", label)) {
            if (ImGui::Selectable("real time", c.parent.empty())) { c.parent.clear(); changed = true; }
            for (const char* b : {"game", "ui", "cinematic"})
                if (c.name != b && ImGui::Selectable(b, c.parent == b)) { c.parent = b; changed = true; }
            for (std::size_t j = 0; j < t.clocks.size(); ++j)
                if (j != i && !t.clocks[j].name.empty() && ImGui::Selectable(t.clocks[j].name.c_str(), c.parent == t.clocks[j].name)) { c.parent = t.clocks[j].name; changed = true; }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("What this clock follows. \"real time\" never slows or stops by itself.");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        if (ImGui::DragFloat("##speed", &c.scale, 0.01f, 0.0f, 100.0f, "%.2fx")) changed = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("How fast this clock runs, compared with what it follows. 1 = the same, 0.5 = half.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) remove = (int)i;
        ImGui::PopID();
    }
    if (remove >= 0) { t.clocks.erase(t.clocks.begin() + remove); changed = true; }
    if (ImGui::SmallButton("Add clock##tm") && (int)t.clocks.size() < okay::TimeSettings::kMaxClocks) {
        okay::ClockSpec c; c.name = "clock" + std::to_string(t.clocks.size() + 1); t.clocks.push_back(c); changed = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Add audio, physics and network clocks##tm")) {
        for (const okay::ClockSpec& s : okay::TimeSettings::StandardClocks()) {
            bool have = false;
            for (const okay::ClockSpec& k : t.clocks) if (k.name == s.name) have = true;
            if (!have) t.clocks.push_back(s);
        }
        changed = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Adds three ready-made clocks: audio and physics follow the game, network runs on real time.\nScripts and timers can use them by name.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset time settings##tm")) { t = okay::TimeSettings{}; changed = true; }
    return changed;   // names are checked and numbers clamped when the settings are used (TimeSettings::Clamp)
}

} // namespace oked
