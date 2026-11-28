#pragma once
// The Navigation window (NavigationWindow.cpp): navmesh bake settings, Bake /
// Clear with progress and stats, the Scene-view overlays (polygons, agent paths,
// obstacles), and the inspectors / Add Component rows of the navigation components.
#include "EditorState.hpp"
#include "imgui.h"
#include <functional>

namespace okay { class Component; class GameObject; struct Mat4; }

namespace oked {

// Window visibility flag (defined in NavigationWindow.cpp).
extern bool g_showNavigation;

// Draw the Navigation window (no-op while hidden).
void DrawNavigationWindow(okay::editor::EditorState& ed);

// Scene-view overlay, drawn above the meshes like the onion skin: the navmesh
// polygons, the agents' paths and corners, obstacles. `clickable` says the view is
// hovered and nothing else took this frame's click (the "send agent" mode uses it).
void DrawNavigationOverlay(okay::editor::EditorState& ed, ImDrawList* dl, const okay::Mat4& vp,
                           ImVec2 center, ImVec2 canvasSize, ImVec2 canvasPos, bool clickable, ImGuiIO& io);

// The Inspector of one navigation component (a no-op for other components).
// `header` is the Inspector's collapsing component header (CompHeader).
using CompHeaderFn = bool (*)(const char*, okay::Component*, okay::Component**, bool);
void DrawNavigationInspector(okay::editor::EditorState& ed, okay::GameObject* go, okay::Component* comp,
                             okay::Component*& toRemove, CompHeaderFn header);

// The Add Component rows (Gameplay). `item(absent, label)` is the menu's row widget.
void NavigationAddComponentItems(okay::editor::EditorState& ed, okay::GameObject* go,
                                 const std::function<bool(bool, const char*)>& item);

// "Bake before Play": rebakes the surfaces that ask for it. Call right before ed.Play().
void NavigationBeforePlay(okay::editor::EditorState& ed);

// Dev screenshot hooks: OKAY_UI_NAV=1 opens the window, OKAY_UI_NAVBAKE=1 bakes at
// start, OKAY_UI_NAVAGENT=<object> gives that object a Nav Mesh Agent and selects
// it, OKAY_UI_NAVSEND=1 turns on send-to-click, OKAY_UI_NAVDEST=x,y,z sends the
// agent there once Play runs.
void NavigationDevHooks(okay::editor::EditorState& ed);

} // namespace oked
