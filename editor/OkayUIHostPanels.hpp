#pragma once
// The editor's real panel code running on OkayUI in window mode (docs/own_ui_toolkit.md 6.24 step 5). Console and Scene Manager are the same source files the
// Dear ImGui editor builds (editor/ConsolePanel.cpp, EditorWidgets.cpp, SceneManagerPanel.cpp), compiled a second time against OkayUI's Dear ImGui
// compatibility layer (okayui/probe/imgui.h) into editor/OkayUIHostPanels.cpp, under renamed entry points so the two copies live in one program. The Project
// window is written here (the editor's Project panel is still inside main.cpp), over the real okaypb::Browser and the real dialogs of ProjectPanelUI.cpp.
#include "OkayUIHostMode.hpp"
#include "okay/UI/OkayUI.hpp"

#include <functional>
#include <string>

namespace okay { namespace editor { class EditorState; } }
namespace okaypb { class Browser; }

namespace oked {
namespace wm {

using PanelHooks = HostModeState::Panels;   // console: DrawConsole(); sceneManager: DrawSceneManager(ed); project: the Project window; before / after: BeginPanels / EndPanels

/// Panel titles: kPanelConsole, kPanelSceneManager and kPanelProject in OkayUIHostMode.hpp.

/// Hooks over a real editor state and project folder: `ed` for the Scene Manager, `projectDir` (a folder with an Assets directory, or any folder) for the Project
/// window. Both may be null / empty: the panel is then not offered. `st` is the host state the before / after hooks sync with (null: no sync).
PanelHooks MakeHooks(okay::editor::EditorState* ed, const std::string& projectDir, HostModeState* st);

/// What `before` / `after` do: switch dock-aware Begin on, and sync the panels' own open flags with the host state before the panels draw and back after.
void BeginPanels(HostModeState& st);
void EndPanels(HostModeState& st);

/// A line for the window-mode Console (this copy of the Console panel has its own log; the editor's other code logs into the Dear ImGui copy).
void ConsoleLog(const std::string& msg, int level = 0);

/// Where the Project window put its controls last frame and what it shows, for tests and tools.
struct ProjectProbe { OkayUI::Rect up, newFolder, firstRow; std::string current; bool namePromptOpen = false; };
const ProjectProbe& GetProjectProbe();

} // namespace wm
} // namespace oked
