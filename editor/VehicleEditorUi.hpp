#pragma once
// Editor pieces for vehicles (no code in main.cpp beyond three one-line calls):
//   * GameObject > Vehicles > <type>: a ready-to-drive vehicle built from cubes and cylinders (VehicleBuilder.hpp),
//   * the Vehicle Controller Inspector: a one short page of plain settings in a few sections (about 15 rows, units, tooltips, reset buttons) and a Controls section
//     (control scheme, press-a-key remapping, clash warnings, fine tuning, save and load); everything else is automatic,
//   * Save as preset / Load preset (.okayvehicle text files).
// Written from scratch for this engine on top of Dear ImGui calls. Compiled on Linux (okay-editor target) but never
// run in a window: see docs/gameplay/vehicles.md "Unverified". The pure decisions live in VehicleEditorModel.hpp (tested).
#include "EditorState.hpp"
#include "ConsolePanel.hpp"
#include "VehicleEditorModel.hpp"
#include "okay/Components/VehicleController.hpp"
#include "okay/Scene/VehicleBuilder.hpp"
#include "imgui.h"
#include <fstream>
#include <sstream>
#include <string>

namespace oked {
namespace vehicleui {

inline std::string& PresetPath() { static std::string p = "MyVehicle.okayvehicle"; return p; }
inline std::string& Message() { static std::string m; return m; }

/// One entry per vehicle type in the Create menu. Returns true if one was made.
inline bool DrawCreateItems(okay::editor::EditorState& ed) {
    bool made = false;
    for (const okay::VehicleBuilder::Shape& sh : okay::VehicleBuilder::Shapes()) {
        if (!ImGui::MenuItem(sh.name)) continue;
        ed.PushUndo();
        okay::Vec3 at = ed.camTarget;
        static const okay::Color paints[] = {okay::Color::FromBytes(205, 60, 55), okay::Color::FromBytes(240, 200, 40),
                                              okay::Color::FromBytes(40, 90, 200), okay::Color::FromBytes(90, 140, 90)};
        static int n = 0;
        okay::GameObject* car = okay::VehicleBuilder::Make(ed.scene(), sh.name, {at.x, 0.0f, at.z}, 0.0f, paints[n++ % 4], true, false);
        if (car) { ed.Select(car); ed.view3D = true; ed.dirty = true; made = true;
                   ConsoleLog(std::string("Created ") + sh.name + " (press Play, walk to it with WASD and press E to get in)"); }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Each vehicle has a driver seat: add a player (GameObject > Player), press Play, walk up and press E.");
    return made;
}

/// What the Controls tab is waiting for: -1 = nothing, else (action, slot) the next key or mouse button press is bound to.
struct Listen { int action = -1; int slot = 0; };
inline Listen& Listening() { static Listen l; return l; }
inline std::string& ControlsPath() { static std::string p = "MyDriving.okaydrive"; return p; }

/// The key or mouse button pressed this frame as a Binding (letters are lower case; Esc is not bound, it cancels).
inline bool PressedBinding(okay::drivectl::Binding& out) {
    using B = okay::drivectl::Binding;
    for (int i = 0; i < 26; ++i) if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_A + i), false)) { out = B::K((char)('a' + i)); return true; }
    for (int i = 0; i < 10; ++i) if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_0 + i), false)) { out = B::K((char)('0' + i)); return true; }
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) { out = B::K(' '); return true; }
    if (ImGui::IsKeyPressed(ImGuiKey_LeftShift, false) || ImGui::IsKeyPressed(ImGuiKey_RightShift, false)) { out = B::K((char)16); return true; }
    if (ImGui::IsKeyPressed(ImGuiKey_LeftCtrl, false) || ImGui::IsKeyPressed(ImGuiKey_RightCtrl, false)) { out = B::K((char)17); return true; }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) { out = B::K(okay::Input::KeyUp); return true; }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) { out = B::K(okay::Input::KeyDown); return true; }
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) { out = B::K(okay::Input::KeyLeft); return true; }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) { out = B::K(okay::Input::KeyRight); return true; }
    for (int b = 1; b < 3; ++b) if (ImGui::IsMouseClicked(b)) { out = B::M(b); return true; }   // the left button is the UI's own click
    return false;
}

/// The Controls tab: pick a scheme, press the key you want for each action, see clashes in plain words, fine-tune, save and load.
inline bool DrawControls(okay::VehicleController* v) {
    using namespace okay::drivectl;
    namespace vm = oked::vehiclemodel;
    bool changed = false;
    if (ImGui::Checkbox("Use a control scheme##vc", &v->useScheme)) changed = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("On: the keys and buttons below. Off: the vehicle's own older key settings (old scenes).");
    if (!v->useScheme) { ImGui::TextDisabled("Off: this vehicle reads its older key settings."); return changed; }
    ControlScheme& s = v->scheme;
    if (ImGui::BeginCombo("Scheme##vc", s.name.c_str())) {
        for (const std::string& n : PresetNames())
            if (ImGui::Selectable(n.c_str(), s.name == n)) { Preset(n, s); Listening() = Listen{}; changed = true; }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Street: gas and brake on separate analog axes, mouse look, recentring camera.\nClassic: the older scheme.\nArcade: more steering help.\nRealistic: less help, manual gears.");

    // Press-a-key capture: while a button is waiting, the next key or mouse button is the new binding. Esc cancels, Backspace clears.
    Listen& L = Listening();
    if (L.action >= 0) {
        Binding b;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) L = Listen{};
        else if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) { Rebind(s, L.action, L.slot, Binding()); L = Listen{}; changed = true; Message() = ""; }
        else if (PressedBinding(b)) { Message() = CaptureRebind(s, L.action, L.slot, b); L = Listen{}; changed = true; }
    }
    if (ImGui::BeginTable("##drivemap", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Action"); ImGui::TableSetupColumn("Keyboard / mouse"); ImGui::TableSetupColumn("Gamepad");
        ImGui::TableHeadersRow();
        for (int a = 0; a < ActionCount; ++a) {
            ImGui::TableNextRow(); ImGui::TableNextColumn();
            ImGui::TextUnformatted(vm::ActionLabel(a));
            ImGui::TableNextColumn();
            const bool waiting = L.action == a && L.slot == 0;
            const std::string label = (waiting ? std::string("Press a key...") : vm::BindingLabel(s.act[a].primary)) + "##k" + std::to_string(a);
            if (ImGui::Button(label.c_str(), ImVec2(-1, 0))) { L.action = a; L.slot = 0; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click, then press the key or mouse button you want.\nEsc cancels, Backspace clears.");
            ImGui::TableNextColumn();
            const std::string pl = "##p" + std::to_string(a);
            if (ImGui::BeginCombo(pl.c_str(), vm::BindingLabel(s.act[a].pad).c_str())) {
                for (const Binding& c : vm::PadChoices())
                    if (ImGui::Selectable(vm::BindingLabel(c).c_str(), s.act[a].pad == c)) { Message() = CaptureRebind(s, a, 1, c); changed = true; }
                ImGui::EndCombo();
            }
        }
        ImGui::EndTable();
    }
    for (const std::string& line : vm::ConflictLines(s)) ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "Clash: %s", line.c_str());
    if (!Message().empty()) ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "%s", Message().c_str());

    if (ImGui::TreeNode("Fine tuning##vc")) {
        for (int a = 0; a < ActionCount; ++a) {
            if (!IsAnalog(a)) continue;
            ActionConfig& c = s.act[a];
            ImGui::PushID(a);
            if (ImGui::TreeNode(vm::ActionLabel(a))) {
                if (ImGui::SliderFloat("Sensitivity (times)", &c.sensitivity, 0.1f, 3.0f)) changed = true;
                if (ImGui::SliderFloat("Dead zone (share of travel)", &c.deadZone, 0.0f, 0.9f)) changed = true;
                if (ImGui::SliderFloat("Response curve (1 = straight)", &c.curve, 0.5f, 3.0f)) changed = true;
                if (ImGui::Checkbox("Invert", &c.invert)) changed = true;
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (ImGui::SliderFloat("Steering help at speed (0 to 1)##vc", &s.steerAssist, 0.0f, 1.0f)) changed = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("How much the steering is limited at high speed. 0 = full lock always.");
        if (ImGui::Checkbox("Automatic gears##vc", &s.autoGear)) changed = true;
        if (ImGui::Checkbox("Mouse looks around while driving##vc", &s.mouseLook)) changed = true;
        if (ImGui::SliderFloat("Mouse look (degrees per count)##vc", &s.mouseLookSensitivity, 0.0f, 1.0f)) changed = true;
        if (ImGui::SliderFloat("Camera recentres after (seconds)##vc", &s.recentreDelay, 0.0f, 10.0f)) changed = true;
        if (ImGui::SliderFloat("Camera recentre speed (1/s)##vc", &s.recentreRate, 0.0f, 10.0f)) changed = true;
        if (ImGui::SliderFloat("Hold to get in as passenger (seconds)##vc", &s.enterHoldTime, 0.1f, 2.0f)) changed = true;
        for (int i = 0; i < kCameraViews; ++i) {
            ImGui::PushID(100 + i);
            if (ImGui::TreeNode(CameraViewName(i))) {
                if (ImGui::SliderFloat("Distance behind (m)", &s.camDistance[i], 0.0f, 40.0f)) changed = true;
                if (ImGui::SliderFloat("Height (m)", &s.camHeight[i], -2.0f, 20.0f)) changed = true;
                if (ImGui::SliderFloat("Field of view (degrees)", &s.camFov[i], 20.0f, 120.0f)) changed = true;
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    char buf[260]; std::snprintf(buf, sizeof(buf), "%s", ControlsPath().c_str());
    if (ImGui::InputText("File##vcf", buf, sizeof(buf))) ControlsPath() = buf;
    if (ImGui::Button("Save controls##vc")) {
        std::ofstream f(ControlsPath(), std::ios::binary);
        if (f) { f << ToText(s); Message() = "Saved " + ControlsPath(); } else Message() = "Could not write " + ControlsPath();
    }
    ImGui::SameLine();
    if (ImGui::Button("Load controls##vc")) {
        std::ifstream f(ControlsPath(), std::ios::binary); std::stringstream ss; if (f) ss << f.rdbuf();
        ControlScheme t; std::string why;
        if (f && FromText(ss.str(), t, &why)) { s = t; Message() = "Loaded " + ControlsPath(); changed = true; }
        else Message() = f ? why : "Could not open " + ControlsPath();
    }
    return changed;
}

/// One row: the setting with its plain name and unit, its tooltip, and a reset button when it differs from the preset.
inline bool DrawRow(okay::VehicleController* v, const okay::VehicleTuning::Desc& d) {
    using okay::VehicleTuning;
    namespace vm = oked::vehiclemodel;
    const vm::Info* info = vm::InfoFor(d.name);
    const std::string label = std::string(info ? info->label : d.name) + " (" + (info ? info->unit : "") + ")##" + d.name;
    bool changed = false;
    switch (d.kind) {
        case VehicleTuning::KFloat: if (ImGui::DragFloat(label.c_str(), &(v->*(d.f)), (float)d.step, (float)d.lo, (float)d.hi)) changed = true; break;
        case VehicleTuning::KInt: case VehicleTuning::KEnum:
            if (d.kind == VehicleTuning::KEnum) { const std::string items = vm::ComboItems(d.options); if (ImGui::Combo(label.c_str(), &(v->*(d.i)), items.data())) changed = true; }
            else if (ImGui::DragInt(label.c_str(), &(v->*(d.i)), 1.0f, (int)d.lo, (int)d.hi)) changed = true;
            break;
        case VehicleTuning::KBool: if (ImGui::Checkbox(label.c_str(), &(v->*(d.b)))) changed = true; break;
        case VehicleTuning::KKey: break;   // keys live in the Controls tab
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", d.tip);
    if (vm::IsChanged(*v, d.name, v->PresetName())) {
        ImGui::SameLine();
        if (ImGui::SmallButton((std::string("Reset##r") + d.name).c_str())) { vm::ResetSetting(*v, d.name, v->PresetName()); changed = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Put this back to the preset's value");
    }
    return changed;
}

/// The whole Inspector body of a Vehicle Controller: one short page, no Simple / Advanced split. Sections Driving, Handling,
/// Camera, Boost and Controls; every row has a plain name, unit, tooltip and a Reset button; the rest is automatic.
inline bool DrawInspector(okay::VehicleController* v) {
    using okay::VehicleTuning;
    namespace vm = oked::vehiclemodel;
    bool changed = false;
    const char* cur = v->PresetName().empty() ? "Custom" : v->PresetName().c_str();
    if (ImGui::BeginCombo("Vehicle type##vt", cur)) {
        for (const auto& p : VehicleTuning::Presets())
            if (ImGui::Selectable(p.name, v->PresetName() == p.name)) { v->ApplyPreset(p.name); changed = true; }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("One click sets the whole feel of the vehicle. Change any number afterwards.");
    for (const char* sec : vm::Sections()) {
        if (std::string(sec) == "Boost") {
            if (ImGui::Checkbox("Boost button##vt", &v->boostEnabled)) changed = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lets the driver press Shift for a burst of speed.");
            if (v->boostEnabled) for (const VehicleTuning::Desc* d : vm::RowsOf(sec, vm::Level::Revealed)) if (DrawRow(v, *d)) changed = true;
            continue;
        }
        if (!ImGui::CollapsingHeader(sec, ImGuiTreeNodeFlags_DefaultOpen)) continue;
        for (const VehicleTuning::Desc* d : vm::RowsOf(sec, vm::Level::Shown)) if (DrawRow(v, *d)) changed = true;
        if (std::string(sec) == "Camera" && ImGui::Checkbox("Camera follows when driven##vt", &v->followCamera)) changed = true;
    }
    if (ImGui::CollapsingHeader("Controls")) { if (DrawControls(v)) changed = true; }
    if (ImGui::CollapsingHeader("Save and share")) {
        char buf[260]; std::snprintf(buf, sizeof(buf), "%s", PresetPath().c_str());
        if (ImGui::InputText("File##vp", buf, sizeof(buf))) PresetPath() = buf;
        if (ImGui::Button("Save vehicle##vp")) {
            std::ofstream f(PresetPath(), std::ios::binary);
            if (f) { f << v->SavePresetText(v->PresetName().empty() ? "My vehicle" : v->PresetName()); Message() = "Saved " + PresetPath(); }
            else Message() = "Could not write " + PresetPath();
        }
        ImGui::SameLine();
        if (ImGui::Button("Load vehicle##vp")) {
            std::ifstream f(PresetPath(), std::ios::binary);
            std::stringstream ss; if (f) ss << f.rdbuf();
            if (f && v->LoadPresetText(ss.str())) { Message() = "Loaded " + PresetPath(); changed = true; }
            else Message() = "That file is not a vehicle preset: " + PresetPath();
        }
        if (!Message().empty()) ImGui::TextDisabled("%s", Message().c_str());
    }
    return changed;
}

} // namespace vehicleui
} // namespace oked
