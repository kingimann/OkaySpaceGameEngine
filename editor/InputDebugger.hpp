#pragma once
// The Input panel (View > Panels > Input): what the engine sees right now (keys, mouse, controllers,
// actions), the last events, and each action's response curve with sliders for dead zone, curve,
// sensitivity and invert. Reads okay::InputDebug and okay::InputActions only; draws with ImGui.
// Written for this project from the InputDebug / InputActions interfaces; no outside code.
#include "okay/Input/Input.hpp"
#include "okay/Input/InputActions.hpp"
#include "okay/Input/InputDebug.hpp"
#include "okay/Input/ActionEditor.hpp"
#include "okay/Input/InputRuntime.hpp"
#include "okay/Input/InputRecorder.hpp"
#include <imgui.h>
#include <string>
#include <vector>

namespace okay::editor {

inline void DrawInputDebugger(bool* open, bool playing) {
    if (!open || !*open) { if (InputDebug::Enabled()) InputDebug::Enable(false); return; }
    InputDebug::Enable(true);
    ImGui::SetNextWindowSize(ImVec2(520, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Input", open)) { ImGui::End(); return; }
    if (!playing) ImGui::TextDisabled("Press Play, then click the Game view: the game only receives input while it plays.");

    if (ImGui::CollapsingHeader("Right now", ImGuiTreeNodeFlags_DefaultOpen))
        for (const std::string& l : InputDebug::Snapshot()) ImGui::TextUnformatted(l.c_str());

    if (ImGui::CollapsingHeader("Controllers and typed actions")) {
        // Which controller is plugged in (family, ids, mapping), then every local player's actions: value, phase and the binding that drives it.
        for (const std::string& l : ControllerDebugLines()) ImGui::TextUnformatted(l.c_str());
        PlayerInputManager& pm = InputRuntime::Players();
        for (int p = 0; p < pm.Count(); ++p) {
            const ActionSet* s = pm.Player(p);
            if (!s) continue;
            ImGui::Separator();
            ImGui::Text("Player %d", p + 1);
            for (const std::string& l : ActionSetDebugLines(*s)) ImGui::TextUnformatted(l.c_str());
        }
        if (pm.Count() == 0) ImGui::TextDisabled("No local player yet (ia_join or the first ia_ read creates one).");
    }

    if (ImGui::CollapsingHeader("Last events", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::SmallButton("Clear")) InputDebug::Clear();
        const auto& ev = InputDebug::Events();
        ImGui::BeginChild("##inputevents", ImVec2(0, 130), true);
        for (std::size_t i = ev.size(); i-- > 0;) ImGui::TextUnformatted(ev[i].c_str());
        ImGui::EndChild();
    }

    if (ImGui::CollapsingHeader("Record and replay")) {
        ImGui::TextWrapped("Record what you press while the game plays, then replay it to reproduce a bug. While it replays, the keyboard and mouse are ignored.");
        if (ImGui::Button("Record")) InputRecorder::StartRecording();
        ImGui::SameLine();
        if (ImGui::Button("Stop")) { InputRecorder::StopRecording(); InputRecorder::StopPlayback(); }
        ImGui::SameLine();
        if (ImGui::Button("Replay")) InputRecorder::StartPlayback(InputRecorder::Text());
        ImGui::SameLine();
        if (ImGui::Button("Copy text")) ImGui::SetClipboardText(InputRecorder::Text().c_str());
        ImGui::Text("%s %zu frames", InputRecorder::Recording() ? "Recording:" : InputRecorder::Playing() ? "Replaying:" : "Kept:", InputRecorder::FrameCount());
    }

    if (ImGui::CollapsingHeader("Response curves")) {
        static int sel = 0;
        const auto& all = InputActions::All();
        if (sel >= (int)all.size()) sel = 0;
        if (ImGui::BeginCombo("Action", all.empty() ? "" : all[(std::size_t)sel].name.c_str())) {
            for (int i = 0; i < (int)all.size(); ++i)
                if (ImGui::Selectable(all[(std::size_t)i].name.c_str(), i == sel)) sel = i;
            ImGui::EndCombo();
        }
        if (!all.empty()) {
            const InputActions::Action& a = all[(std::size_t)sel];
            InputActions::Response r = InputActions::GetResponse(a.name);
            bool changed = false;
            changed |= ImGui::SliderFloat("Dead zone", &r.deadzone, 0.0f, 0.9f);
            changed |= ImGui::SliderFloat("Curve", &r.curve, 0.3f, 4.0f, "%.2f (1 = straight, 2 = gentle near centre)");
            changed |= ImGui::SliderFloat("Sensitivity", &r.sensitivity, 0.1f, 5.0f);
            if (a.direction) { changed |= ImGui::Checkbox("Invert X", &r.invertX); ImGui::SameLine(); changed |= ImGui::Checkbox("Invert Y", &r.invertY); }
            if (changed) InputActions::SetResponse(a.name, r);
            if (ImGui::SmallButton("Reset curve")) InputActions::SetResponse(a.name, InputActions::Response{});
            float pts[64];
            for (int i = 0; i < 64; ++i) pts[i] = InputActions::ShapeValue((float)i / 63.0f, r);
            ImGui::PlotLines("##curve", pts, 64, 0, "how far you push (left to right) -> what the game gets", 0.0f, std::max(1.0f, r.sensitivity), ImVec2(0, 90));
            ImGui::Text("Reads now: %.2f", a.value);
        }
    }
    ImGui::End();
}

} // namespace okay::editor
