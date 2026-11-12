#pragma once
// Volumes in the editor (VolumeTools.cpp): every volume and logic zone drawn as a translucent shape in the Scene
// view (a colour per kind, each kind switchable), six face handles on the selected one to resize it, a tool that
// draws a new volume by dragging on the ground, the Volume inspector, and the menu rows (GameObject > Volume,
// Scene Overlays > Volumes, Add Component).
#include "EditorState.hpp"
#include "imgui.h"
#include <functional>

namespace okay { class Component; class GameObject; struct Mat4; struct Vec3; }

namespace oked {

// What the Scene view shows. Bits 0..4 are the Volume kinds (Checkpoint, Kill, Force, Atmosphere, Ambience),
// bit 5 the zones of Logic Sources (plates and trigger zones). Saved in the editor settings.
extern bool     g_showVolumes;
extern bool     g_showVolumeLabels;
extern unsigned g_volumeKinds;
constexpr unsigned kVolumeKindsAll = 0x3Fu;

// The ground-drag tool is armed: a left drag in the 3D Scene view draws a volume instead of orbiting.
bool VolumeDrawActive();

// Scene-view overlay: all volumes, then the selected one's handles. Returns true when the selection's handles took
// the click this frame (the transform gizmo and click-select must then leave it alone). `clickable` says the view is
// hovered and nothing else has the mouse.
bool DrawVolumeOverlay(okay::editor::EditorState& ed, ImDrawList* dl, const okay::Mat4& vp, const okay::Vec3& eye,
                       ImVec2 center, ImVec2 canvasSize, ImVec2 canvasPos, bool clickable, ImGuiIO& io);

// Inspector rows of a Volume (a no-op for other components). `header` is the Inspector's CompHeader.
using CompHeaderFn = bool (*)(const char*, okay::Component*, okay::Component**, bool);
void DrawVolumeInspector(okay::editor::EditorState& ed, okay::GameObject* go, okay::Component* comp,
                         okay::Component*& toRemove, CompHeaderFn header);

// GameObject > Volume: draw by dragging, or create one in front of the Scene camera.
void DrawVolumeGameObjectMenu(okay::editor::EditorState& ed, bool& created);
// Scene Overlays > Volumes: show / hide by kind.
void DrawVolumeOverlayMenu(std::function<void()> onChange);
// Add Component rows (Gameplay).
void VolumeAddComponentItems(okay::editor::EditorState& ed, okay::GameObject* go,
                             const std::function<bool(bool, const char*)>& item);

} // namespace oked
