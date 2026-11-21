#pragma once
// Wind in the editor (WindTools.cpp, docs/gameplay/wind.md): Wind Zones drawn in the Scene view with arrows that show the real wind
// (direction, strength, gusts), the Inspector rows for Wind Zone / Wind Drag / Wind Sway / Wind Ambience, the menu rows
// (GameObject > Effects > Environment, Add Component > Environment) and the Wind section of the Lighting window.
#include "EditorState.hpp"
#include "imgui.h"
#include <functional>

namespace okay { class Component; class GameObject; struct Mat4; struct Vec3; }

namespace oked {

extern bool g_showWind;   // draw every wind zone and its arrows in the Scene view (the selected one is always drawn)

// Scene-view overlay: a shape and a field of arrows per wind zone. Pure drawing (no handles).
void DrawWindOverlay(okay::editor::EditorState& ed, ImDrawList* dl, const okay::Mat4& vp, ImVec2 center,
                     ImVec2 canvasSize, ImVec2 canvasPos);

using WindCompHeaderFn = bool (*)(const char*, okay::Component*, okay::Component**, bool);
// Inspector rows of the wind components (a no-op for other components).
void DrawWindInspector(okay::editor::EditorState& ed, okay::GameObject* go, okay::Component* comp,
                       okay::Component*& toRemove, WindCompHeaderFn header);

// GameObject > Effects > Environment: Wind (global), Wind Zone, Fan, Flag, Swaying tree.
void DrawWindGameObjectMenu(okay::editor::EditorState& ed, bool& created);
// Scene Overlays > Wind.
void DrawWindOverlayMenu(std::function<void()> onChange);
// Add Component > Environment rows.
void WindAddComponentItems(okay::editor::EditorState& ed, okay::GameObject* go,
                           const std::function<bool(bool, const char*)>& item);
// The Wind section of the Lighting window (global wind with a live preview arrow).
void DrawWindSettingsSection(okay::editor::EditorState& ed);

// Make the objects the menus make (also used by the tests and the example scene builder).
okay::GameObject* MakeGlobalWind(okay::editor::EditorState& ed);

} // namespace oked
