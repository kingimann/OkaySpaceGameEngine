#pragma once
// The pure (no Dear ImGui, no window) decisions behind the Vehicle Controller Inspector in VehicleEditorUi.hpp, so they can be
// tested headless (tests/test_vehicle_editor_model.cpp). Written from scratch for this engine on top of the VehicleTuning table.
#include "okay/Components/VehicleTuning.hpp"
#include <cstdio>
#include <string>
#include <vector>

namespace oked {
namespace vehiclemodel {

/// "A|B|C" -> "A\0B\0C\0\0", the packed list Dear ImGui's Combo wants.
inline std::string ComboItems(const char* options) {
    std::string items;
    for (const char* c = options ? options : ""; *c; ++c) items += (*c == '|') ? '\0' : *c;
    items += '\0';
    return items;
}

/// The rows of the Simple tab (after the preset and Handling controls).
inline const std::vector<const char*>& SimpleRows() {
    static const std::vector<const char*> r = {"maxSpeed", "acceleration", "grip", "driftAmount"};
    return r;
}

/// One group of the Advanced tab: its name and the settings in it, in table order. "Simple" settings are not repeated here.
struct Group { std::string name; std::vector<const okay::VehicleTuning::Desc*> rows; };
inline std::vector<Group> AdvancedGroups() {
    std::vector<Group> out;
    for (const okay::VehicleTuning::Desc& d : okay::VehicleTuning::Table()) {
        if (std::string(d.group) == "Simple") continue;
        if (out.empty() || out.back().name != d.group) out.push_back({d.group, {}});
        out.back().rows.push_back(&d);
    }
    return out;
}

/// Short text for a key setting: a letter or digit shows as itself, anything else as its number.
inline std::string KeyLabel(char k) {
    const unsigned char u = (unsigned char)k;
    if ((u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9')) return std::string(1, (char)u);
    char b[16]; std::snprintf(b, sizeof(b), "#%d", (int)u); return b;
}

} // namespace vehiclemodel
} // namespace oked
