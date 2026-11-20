#pragma once
// Editor pieces for vehicles (no code in main.cpp beyond three one-line calls):
//   * GameObject > Vehicles > <type>: a ready-to-drive vehicle built from cubes and cylinders (VehicleBuilder.hpp),
//   * the Vehicle Controller Inspector: a Simple tab (preset, Handling slider, top speed, acceleration, grip, drift) and an
//     Advanced tab with every setting grouped and tooltipped (the table in VehicleTuning.hpp),
//   * Save as preset / Load preset (.okayvehicle text files).
// Written from scratch for this engine on top of Dear ImGui calls. Compiled on Linux (okay-editor target) but never
// run in a window: see docs/vehicle_controls.md "Unverified". The pure decisions live in VehicleEditorModel.hpp (tested).
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

/// The whole Inspector body of a Vehicle Controller. Returns true when something changed.
inline bool DrawInspector(okay::VehicleController* v) {
    using okay::VehicleTuning;
    bool changed = false;
    if (ImGui::Checkbox("Open-world driving##vt", &v->openWorld)) changed = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("On: brake to reverse, drifts, gears, boost, chase camera.\nOff: the older simple model (old scenes keep it).");
    if (ImGui::BeginTabBar("##vehtabs")) {
        if (ImGui::BeginTabItem("Simple")) {
            // Vehicle preset
            const char* cur = v->PresetName().empty() ? "Custom" : v->PresetName().c_str();
            if (ImGui::BeginCombo("Vehicle preset##vt", cur)) {
                for (const auto& p : VehicleTuning::Presets())
                    if (ImGui::Selectable(p.name, v->PresetName() == p.name)) { v->ApplyPreset(p.name); changed = true; }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pick a kind of vehicle. This sets every number below; you can change them afterwards.");
            if (ImGui::SliderFloat("Handling (Realistic - Arcade)##vt", &v->handling, 0.0f, 1.0f)) changed = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Left: realistic, slidey, you must be careful.\nRight: arcade, forgiving, grips well and is easy to drift.");
            for (const char* n : oked::vehiclemodel::SimpleRows()) {
                const VehicleTuning::Desc* d = VehicleTuning::Find(n);
                if (d && ImGui::DragFloat((std::string(d->name) + "##vts").c_str(), &(v->*(d->f)), (float)d->step, (float)d->lo, (float)d->hi)) changed = true;
                if (d && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", d->tip);
            }
            if (ImGui::Checkbox("Camera follows when driven##vt", &v->followCamera)) changed = true;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Advanced")) {
            const char* lastGroup = "";
            bool open = false;
            for (const VehicleTuning::Desc& d : VehicleTuning::Table()) {
                if (std::string(d.group) == "Simple") continue;
                if (std::string(d.group) != lastGroup) {
                    if (open) ImGui::TreePop();
                    lastGroup = d.group;
                    open = ImGui::TreeNode(d.group);
                }
                if (!open) continue;
                const std::string id = std::string(d.name) + "##va";
                switch (d.kind) {
                    case VehicleTuning::KFloat: if (ImGui::DragFloat(id.c_str(), &(v->*(d.f)), (float)d.step, (float)d.lo, (float)d.hi)) changed = true; break;
                    case VehicleTuning::KInt: case VehicleTuning::KEnum: {
                        if (d.kind == VehicleTuning::KEnum) {
                            const std::string items = oked::vehiclemodel::ComboItems(d.options);
                            if (ImGui::Combo(id.c_str(), &(v->*(d.i)), items.data())) changed = true;
                        } else if (ImGui::DragInt(id.c_str(), &(v->*(d.i)), 1.0f, (int)d.lo, (int)d.hi)) changed = true;
                        break; }
                    case VehicleTuning::KBool: if (ImGui::Checkbox(id.c_str(), &(v->*(d.b)))) changed = true; break;
                    case VehicleTuning::KKey: { int k = (unsigned char)(v->*(d.k));
                        if (ImGui::DragInt(id.c_str(), &k, 1.0f, 0, 255)) { v->*(d.k) = (char)k; changed = true; } break; }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", d.tip);
            }
            if (open) ImGui::TreePop();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Presets")) {
            char buf[260]; std::snprintf(buf, sizeof(buf), "%s", PresetPath().c_str());
            if (ImGui::InputText("File##vp", buf, sizeof(buf))) PresetPath() = buf;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("A .okayvehicle text file. Relative names are next to the editor's working folder.");
            if (ImGui::Button("Save as preset##vp")) {
                std::ofstream f(PresetPath(), std::ios::binary);
                if (f) { f << v->SavePresetText(v->PresetName().empty() ? "My vehicle" : v->PresetName()); Message() = "Saved " + PresetPath(); }
                else Message() = "Could not write " + PresetPath();
            }
            ImGui::SameLine();
            if (ImGui::Button("Load preset##vp")) {
                std::ifstream f(PresetPath(), std::ios::binary);
                std::stringstream ss; if (f) ss << f.rdbuf();
                if (f && v->LoadPresetText(ss.str())) { Message() = "Loaded " + PresetPath(); changed = true; }
                else Message() = "Could not read a vehicle preset from " + PresetPath();
            }
            if (!Message().empty()) ImGui::TextDisabled("%s", Message().c_str());
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    return changed;
}

} // namespace vehicleui
} // namespace oked
