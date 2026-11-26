#pragma once
// The Input Actions window (View > Editors & Tools > Input Actions): author an input asset (.okayinput) - control schemes, action maps,
// actions with their value type, bindings (single, 2D vector, 3D vector, 1D axis, chord) with scheme group and processors, and
// interactions - with a validation list, plus a live view of the player's actions (value, phase, which binding drives it).
// All logic is okay::ActionEditor (engine). OkayUI draws the window (no Dear ImGui version; docs/ui/own_ui_toolkit.md section 30):
// FillInputActions builds the model the window shows (tools/input_shots.cpp uses it too, for its pictures), ApplyInputActionsIntent
// does what the person asked. Written for this project from the ActionEditor / InputRuntime interfaces; no outside code.
#include "okay/Input/ActionEditor.hpp"
#include "okay/Input/InputRuntime.hpp"
#include "EditorPanelModels.hpp"
#include "OkayPanels.hpp"   // DrawInputActionsOkayUI (only DrawInputActionsWindow needs it)
#include <cstdio>
#include <string>

namespace okay::editor {

/// The window's model from the asset being edited and the engine's players now. `state.selected` picks the action of the Action tab.
inline void FillInputActions(okaypanels::InputActionsModel& m, const ActionEditor& ed, const okaypanels::InputActionsState& state) {
    m.dirty = ed.dirty;
    for (const ControlScheme& c : ed.set.Schemes())
        m.schemes.push_back(c.name + (c.keyboard ? " keyboard" : "") + (c.mouse ? " mouse" : "") + (c.gamepad ? " gamepad" : "") + (c.touch ? " touch" : ""));
    for (const ActionMap& map : ed.set.Maps()) {
        okaypanels::InputActionMapRow row;
        row.name = map.name; row.priority = map.priority; row.consume = map.consume; row.enabled = map.enabled;
        for (const ActionDef& a : ed.set.Actions())
            if (a.map == map.name) row.actions.push_back({a.name, ValueTypeName(a.type)});
        m.maps.push_back(std::move(row));
    }
    if (const ActionDef* a = ed.set.Find(state.selected)) {
        m.hasAction = true;
        m.describe = ed.Describe(state.selected);
        for (const BindingSpec& bs : a->bindings) m.bindings.push_back({bs.parts, ProcessorsToString(bs.processors)});
        for (const auto& it : a->interactions) m.interactions.push_back(it.ToString());
    }
    m.problems = ed.Validate();
    m.controllers = ControllerDebugLines();
    PlayerInputManager& pm = InputRuntime::Players();
    for (int p = 0; p < pm.Count(); ++p)
        if (const ActionSet* s = pm.Player(p)) m.players.push_back({p + 1, ActionSetDebugLines(*s)});
}

/// Do what the person asked for, on the asset being edited; the answer goes to `state.status`.
inline void ApplyInputActionsIntent(ActionEditor& ed, okaypanels::InputActionsState& st, const okaypanels::InputActionsIntent& in) {
    const std::string file = st.file;
    std::string& sel = st.selected;
    if (in.load) { std::string err; st.status = ed.Load(file, &err) ? "Loaded." : err; sel.clear(); }
    if (in.save) st.status = ed.SaveAs(file) ? "Saved." : "Could not write the file.";
    if (in.newDefault) { ed.NewFromDefault(); sel.clear(); st.status = "Started from the default asset."; }
    if (in.migrate) { ed.NewFromMigration(); sel.clear(); st.status = "Built from the actions the game defines now."; }
    if (in.useInGame && (!ed.path.empty() || ed.SaveAs(file)))   // the running game's players start from this asset
        st.status = InputRuntime::LoadAsset(ed.path) ? "The game now uses this asset." : "Could not load it into the game.";
    if (in.addMap) st.status = ed.AddMap(st.name) ? "Map added." : "Map name taken, empty or has a space.";
    if (in.addScheme)
        st.status = ed.AddScheme(st.name, true, true, false, false) ? "Scheme added (keyboard and mouse; edit the file for others)." : "Scheme name taken, empty or has a space.";
    if (in.addAction) {
        static const ValueType kTypes[] = {ValueType::Button, ValueType::Axis1D, ValueType::Vector2, ValueType::Vector3};
        const ValueType vt = kTypes[st.newType >= 0 && st.newType < 4 ? st.newType : 0];
        const std::string map = ed.set.Maps().empty() ? std::string() : ed.set.Maps().front().name;
        if (ed.AddAction(st.name, vt, map)) { sel = st.name; st.status = "Action added to " + (map.empty() ? std::string("the default map") : map) + "."; }
        else st.status = "Action name taken, empty or has a space.";
    }
    if (in.select) sel = in.selectName;
    if (in.partBinding >= 0)
        st.status = ed.SetPart(sel, in.partBinding, in.partIndex, in.partText) ? "Input changed." : "That is not an input (try key:w, pad:a, stick:left, mouse:left).";
    if (in.procBinding >= 0)
        st.status = ed.SetProcessors(sel, in.procBinding, in.procText) ? "Processors changed." : "Cannot read the processors (deadzone(0.2) invert(y) scale(2) ...).";
    if (in.removeBinding >= 0) ed.RemoveBinding(sel, in.removeBinding);
    if (in.addBinding >= 0 && in.addBinding < 5) {
        static const BindingSpec::Kind kKinds[] = {BindingSpec::Kind::Single, BindingSpec::Kind::Vector2, BindingSpec::Kind::Vector3,
                                                   BindingSpec::Kind::Axis1D, BindingSpec::Kind::Chord};
        ed.AddBinding(sel, kKinds[in.addBinding]);
    }
    if (in.addInteraction) {
        const bool ok = ed.AddInteraction(sel, in.interactionText);
        st.status = ok ? "Interaction added." : "Cannot read the interaction.";
        if (ok) st.interaction[0] = '\0';
    }
    if (in.removeInteraction >= 0) ed.RemoveInteraction(sel, in.removeInteraction);
    if (in.deleteAction) { ed.Remove(sel); sel.clear(); }
}

/// The editor's one Input Actions window: the asset it edits and the window's state live here.
struct InputActionsWindowData {
    ActionEditor ed;
    okaypanels::InputActionsState state;
    bool started = false;
};
inline InputActionsWindowData& InputActionsData() { static InputActionsWindowData d; return d; }

/// Start from the default asset the first time (the path is the project's Assets/input.okayinput).
inline void StartInputActions(InputActionsWindowData& d, const std::string& projectDir) {
    if (d.started) return;
    d.started = true;
    d.ed.NewFromDefault();
    d.ed.dirty = false;
    std::snprintf(d.state.file, sizeof d.state.file, "%s", projectDir.empty() ? "default.okayinput" : (projectDir + "/Assets/input.okayinput").c_str());
}

inline void DrawInputActionsWindow(bool* open, const std::string& projectDir) {
    if (!open || !*open) return;
    InputActionsWindowData& d = InputActionsData();
    StartInputActions(d, projectDir);
    okaypanels::InputActionsModel model;
    okaypanels::InputActionsIntent in;
    oked::DrawInputActionsOkayUI(open, model, [&](okaypanels::InputActionsModel& m) { FillInputActions(m, d.ed, d.state); }, d.state, in);
    ApplyInputActionsIntent(d.ed, d.state, in);
}

} // namespace okay::editor
