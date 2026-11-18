#pragma once
// The one ImGui piece of the Density choice: a "Density" submenu with the three steps, used by the Visual Script window's
// Graph menu and the Script editor's View menu so both show the same list and change the same setting
// (editor/ScriptingDensity.hpp holds the numbers; the editor saves the choice with the other settings).
#include "ScriptingDensity.hpp"
#include "imgui.h"

namespace oked::scriptui {

/// Call inside an open menu. Draws "Density >" with Compact / Comfortable / Spacious and a tick on the current one.
inline void DrawDensityMenu() {
    if (!ImGui::BeginMenu("Density")) return;
    for (int d = 0; d < DensityCount(); ++d) {
        if (ImGui::MenuItem(DensityName(d), nullptr, g_scriptDensity == d)) SetDensity(d);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nUsed by the Visual Script and Script windows.", DensityHint(d));
    }
    ImGui::EndMenu();
}

} // namespace oked::scriptui
