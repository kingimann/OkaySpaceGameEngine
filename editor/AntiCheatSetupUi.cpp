#include "AntiCheatSetupUi.hpp"
#include "AntiCheatSetup.hpp"
#include "imgui.h"

using okay::editor::EditorState;

namespace oked {
namespace as = okay::acsetup;

namespace {
as::Preset g_preset = as::Preset::CasualCoop;   // the picker's choice (not saved: a convenience for this session)

void PresetCombo(const char* id) {
    const as::PresetInfo& cur = as::Info(g_preset);
    ImGui::SetNextItemWidth(180);
    if (ImGui::BeginCombo(id, cur.name)) {
        for (int i = 0; i < as::kPresetCount; ++i) {
            const as::Preset p = static_cast<as::Preset>(i);
            if (ImGui::Selectable(as::Info(p).name, p == g_preset)) g_preset = p;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", as::Info(p).tip);
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", as::Info(g_preset).tip);
}

void Wrapped(const char* text, bool disabled) {
    ImGui::PushTextWrapPos(0.0f);
    if (disabled) ImGui::TextDisabled("%s", text); else ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
}
} // namespace

bool AddAntiCheatMenuItem(EditorState& ed, okay::GameObject* go) {
    const bool have = as::FindConfig(ed.scene()) != nullptr;
    if (!ImGui::MenuItem("Anti-Cheat (one click, safe preset)", nullptr, false, !have)) {
        if (have && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("This scene already has an Anti-Cheat Config.");
        else if (ImGui::IsItemHovered()) ImGui::SetTooltip("Adds an Anti-Cheat Config next to the Network Manager with the Casual co-op preset: server authority, encryption, "
                                                           "spawn list from your networked prefabs, and the detection engine watching without kicking anyone. Change the preset in the Inspector.");
        return false;
    }
    const as::AddResult r = as::AddAntiCheat(ed.scene(), go, as::Preset::CasualCoop, ed.projectDir());
    if (r.added) ed.dirty = true;
    return r.added;
}

void DrawNetworkManagerAntiCheat(EditorState& ed, okay::NetworkManager* nm) {
    (void)nm;
    if (as::FindConfig(ed.scene())) return;
    ImGui::Spacing();
    PresetCombo("##nm_acpreset");
    ImGui::SameLine();
    if (ImGui::Button("Add Anti-Cheat##nm")) {
        const as::AddResult r = as::AddAntiCheat(ed.scene(), nm ? nm->gameObject : nullptr, g_preset, ed.projectDir());
        if (r.added) ed.dirty = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Turns on server-side anti-cheat for this game in one click: the server refuses moving other players' objects, spawning prefabs that are not on the list, "
                          "writing the server's variables, and moving too fast. Players on an older build of your game are not affected until you ship the update.");
    Wrapped(as::Info(g_preset).blurb, true);
}

void DrawAntiCheatPresetPicker(EditorState& ed, okay::AntiCheatConfig* ac) {
    if (!ac) return;
    PresetCombo("Preset##acpreset");
    ImGui::SameLine();
    if (ImGui::Button("Apply preset##acpreset")) {
        as::ApplyPreset(ac->settings, g_preset, as::NetworkedPrefabs(ed.projectDir(), as::FindManager(ed.scene())));
        ed.dirty = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Replaces the numbers below with the preset's, fills \"Prefabs clients may spawn\" from your project's networked prefabs, and turns the detection engine on in "
                          "observe mode (it never kicks). Keeps your ticket key, weapons and client-writable variables. The Rules file field is left alone.");
    Wrapped(as::Info(g_preset).blurb, true);
    ImGui::Spacing();
}

} // namespace oked
