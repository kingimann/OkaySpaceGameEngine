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

ImVec4 Colour(as::Status s) {
    switch (s) {
    case as::Status::Ok: return ImVec4(0.45f, 0.85f, 0.50f, 1);
    case as::Status::Warn: return ImVec4(0.95f, 0.80f, 0.35f, 1);
    case as::Status::Missing: return ImVec4(0.95f, 0.45f, 0.40f, 1);
    default: return ImVec4(0.65f, 0.70f, 0.78f, 1);
    }
}
const char* Mark(as::Status s) {
    switch (s) {
    case as::Status::Ok: return "[on]";
    case as::Status::Warn: return "[check]";
    case as::Status::Missing: return "[missing]";
    default: return "[optional]";
    }
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

void DrawAntiCheatChecklist(EditorState& ed, int& manifestMode, const char* idSuffix) {
    okay::Scene& scene = ed.scene();
    const std::vector<as::Item> items = as::Evaluate(as::GatherFacts(scene, manifestMode, ed.projectDir()));
    const as::Summary sum = as::Summarize(items);
    char head[96];
    std::snprintf(head, sizeof(head), "Anti-cheat checklist: %d on, %d to check%s##accl%s", sum.ok, sum.warn + sum.missing,
                  sum.missing ? " (something is missing)" : "", idSuffix);
    if (!ImGui::CollapsingHeader(head, sum.missing || sum.warn ? ImGuiTreeNodeFlags_DefaultOpen : 0)) return;
    ImGui::PushID(idSuffix);
    Wrapped("What anti-cheat does in this scene and Build Settings. A fix button only makes things safer: none of them kicks or bans anyone.", true);
    as::Fix doFix = as::Fix::None;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const as::Item& it = items[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::TextColored(Colour(it.status), "%s", Mark(it.status));
        ImGui::SameLine();
        ImGui::TextUnformatted(it.title.c_str());
        if (it.fix != as::Fix::None) {
            ImGui::SameLine();
            if (ImGui::SmallButton(it.fixLabel)) doFix = it.fix;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Safe automatic fix. You can undo it by changing the field back.");
        }
        ImGui::Indent();
        Wrapped(it.detail.c_str(), true);
        if (std::string(it.id) == "unclaimed") {   // one Select button per object, so the maker can look and decide
            int shown = 0;
            for (const auto& go : scene.Objects())
                for (auto* ns : go->GetComponents<okay::NetworkSync>())
                    if (as::IsUnclaimed(*go, *ns) && shown++ < 12) {
                        ImGui::PushID(go.get());
                        if (ImGui::SmallButton("Select")) ed.Select(go.get());
                        ImGui::SameLine();
                        ImGui::TextUnformatted(go->name.c_str());
                        ImGui::PopID();
                    }
            if (shown > 12) ImGui::TextDisabled("and %d more", shown - 12);
        }
        ImGui::Unindent();
        ImGui::PopID();
    }
    ImGui::PopID();
    if (doFix != as::Fix::None) {
        okay::GameObject* sel = ed.selected();
        if (as::ApplyFix(scene, doFix, manifestMode, ed.projectDir(), doFix == as::Fix::AddManager ? nullptr : sel) && doFix != as::Fix::ManifestReport)
            ed.dirty = true;   // (the Build Settings combo is not part of the scene)
    }
}

} // namespace oked
