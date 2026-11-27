#pragma once
// The Input panel (View > Panels > Input): what the engine sees right now (keys, mouse, controllers,
// actions), the last events, and each action's response curve with sliders for dead zone, curve,
// sensitivity and invert. Reads okay::InputDebug and okay::InputActions only. OkayUI draws it (no Dear
// ImGui version; docs/ui/own_ui_toolkit.md section 29): FillInputPanel builds the model the window shows
//, ApplyInputPanelIntent does what the person asked.
// Written for this project from the InputDebug / InputActions interfaces; no outside code.
#include "okay/Input/Input.hpp"
#include "okay/Input/InputActions.hpp"
#include "okay/Input/InputDebug.hpp"
#include "okay/Input/ActionEditor.hpp"
#include "okay/Input/InputRuntime.hpp"
#include "okay/Input/InputRecorder.hpp"
#include "EditorPanelModels.hpp"
#include "OkayPanels.hpp"   // DrawInputPanelOkayUI (only DrawInputDebugger needs it)
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace okay::editor {

inline InputActions::Response ToEngineResponse(const okaypanels::InputResponse& r) {
    InputActions::Response e;
    e.deadzone = r.deadzone; e.curve = r.curve; e.sensitivity = r.sensitivity; e.invertX = r.invertX; e.invertY = r.invertY;
    return e;
}

/// The panel's model from the engine's input state now. `state.action` picks the action whose response curve is shown (kept in range).
inline void FillInputPanel(okaypanels::InputPanelModel& m, okaypanels::InputPanelState& state, bool playing) {
    m.playing = playing;
    m.snapshot = InputDebug::Snapshot();
    // Which controller is plugged in (family, ids, mapping), then every local player's actions: value, phase and the binding that drives it.
    m.controllers = ControllerDebugLines();
    PlayerInputManager& pm = InputRuntime::Players();
    for (int p = 0; p < pm.Count(); ++p)
        if (const ActionSet* s = pm.Player(p)) m.players.push_back({p + 1, ActionSetDebugLines(*s)});
    const auto& ev = InputDebug::Events();
    for (std::size_t i = ev.size(); i-- > 0;) m.events.push_back(ev[i]);   // newest first
    char rec[64];
    std::snprintf(rec, sizeof rec, "%s %zu frames", InputRecorder::Recording() ? "Recording:" : InputRecorder::Playing() ? "Replaying:" : "Kept:",
                  InputRecorder::FrameCount());
    m.recordStatus = rec;
    const auto& all = InputActions::All();
    for (const InputActions::Action& a : all) m.actions.push_back(a.name);
    if (state.action < 0 || state.action >= (int)all.size()) state.action = 0;
    if (!all.empty()) {
        const InputActions::Action& a = all[(std::size_t)state.action];
        const InputActions::Response r = InputActions::GetResponse(a.name);
        m.response = {r.deadzone, r.curve, r.sensitivity, r.invertX, r.invertY};
        m.direction = a.direction;
        m.value = a.value;
    }
    m.shape = [](float in, const okaypanels::InputResponse& r) { return InputActions::ShapeValue(in, ToEngineResponse(r)); };
}

/// Do what the person asked for. `action` is the action the model was filled for (its response is in `m`); `copy` puts text on the clipboard.
inline void ApplyInputPanelIntent(const okaypanels::InputPanelModel& m, int action, const okaypanels::InputPanelIntent& in,
                                  const std::function<void(const std::string&)>& copy) {
    if (in.clearEvents) InputDebug::Clear();
    if (in.record) InputRecorder::StartRecording();
    if (in.stop) { InputRecorder::StopRecording(); InputRecorder::StopPlayback(); }
    if (in.replay) InputRecorder::StartPlayback(InputRecorder::Text());
    if (in.copyText && copy) copy(InputRecorder::Text());
    if (action < 0 || (std::size_t)action >= m.actions.size()) return;
    const std::string& name = m.actions[(std::size_t)action];
    if (in.responseChanged) InputActions::SetResponse(name, ToEngineResponse(m.response));
    if (in.resetCurve) InputActions::SetResponse(name, InputActions::Response{});
}

/// `copy` puts text on the clipboard (Copy text).
inline void DrawInputDebugger(bool* open, bool playing, const std::function<void(const std::string&)>& copy) {
    if (!open || !*open) { if (InputDebug::Enabled()) InputDebug::Enable(false); return; }
    InputDebug::Enable(true);
    static okaypanels::InputPanelState state;
    okaypanels::InputPanelModel model;
    okaypanels::InputPanelIntent in;
    int filledFor = -1;
    oked::DrawInputPanelOkayUI(open, model, [&](okaypanels::InputPanelModel& m) { FillInputPanel(m, state, playing); filledFor = state.action; }, state, in);
    ApplyInputPanelIntent(model, filledFor, in, copy);
}

} // namespace okay::editor
