#pragma once
// Window mode: the editor with OkayUI owning the window and the frame (no Dear ImGui). Off by default, a dev switch, step 1 and 2 of the plan in
// docs/own_ui_toolkit.md 6.23. Switch: OKAY_DEV=1 and OKAY_UI_BACKEND=okayui and OKAY_UI_HOST=okayui. Without all three nothing changes.
#include "OkayUIBackend.hpp"
#include "OkayUIHostState.hpp"

#include <cstring>
#include <functional>
#include <map>
#include <string>

namespace oked {

/// The three switches, as main() reads them (the three getenv values, any may be null). The saved settings file does not take part: window mode is
/// chosen per run, so a stale setting cannot move an editor into it.
inline bool HostModeRequested(const char* okayDev, const char* okayUiBackend, const char* okayUiHost) {
    return okayuibackend::DevMode(okayDev) && okayuibackend::Resolve(okayUiBackend, std::string(), true) == okayuibackend::Backend::OkayUI
           && okayUiBackend && okayUiHost && std::strcmp(okayUiHost, "okayui") == 0;
}

/// What the window-mode screen keeps between frames (and, through ToPersisted / ApplyPersisted, between runs: shared/OkayUIHostState.hpp).
/// Window titles of the panels window mode can show (the dock layout, the saved state and the panels' own Begin calls use these exact strings).
inline const char* kPanelConsole = "Console###Console";
inline const char* kPanelSceneManager = "Scene Manager###Scenes";
inline const char* kPanelProject = "Project";
inline const char* kPanelScene = "Scene";
inline const char* kPanelGame = "Game";
inline const char* kPanelHierarchy = "Hierarchy";
inline const char* kPanelInspector = "Inspector";
inline const char* kPanelMixer = "Audio Mixer";
inline const char* kPanelStatus = "Window mode";

struct HostModeState {
    /// The real panels, wired in by the host (editor/OkayUIHostPanels.hpp): each draws its own window with Begin / End (dock-aware, see
    /// OkayUI::SetDockAwareBegin). A null function means the panel is not offered (not in the default layout, not drawn). `before` / `after` run around the
    /// panels each frame (they set the dock-aware switch and sync the panels' own open flags with `open`).
    struct Panels { std::function<void()> console, sceneManager, project, scene, game, hierarchy, inspector, dialogs, before, after; } panels;
    int frame = 0;                           // frames drawn
    bool docked = false;                     // the layout was built or restored
    std::map<std::string, bool> open;        // panel name -> open; a panel not listed is open
    std::string restoredLayout;              // a saved layout to load on the first frame (empty = build the default)
    bool IsOpen(const std::string& name) const { auto it = open.find(name); return it == open.end() || it->second; }
    bool& OpenRef(const std::string& name) { auto it = open.find(name); if (it == open.end()) it = open.emplace(name, true).first; return it->second; }
};

/// The state to save: the panel flags, the live dock layout text and the window's place. `window` comes from the platform window.
okayuihost::PersistedState ToPersisted(const HostModeState& st, const okayuihost::WindowState& window);
/// Take a loaded state in: the panel flags and the layout (loaded by the first frame). The window part is the caller's (it makes the window).
void ApplyPersisted(HostModeState& st, const okayuihost::PersistedState& saved);
/// Where window mode keeps its state file: the environment variable OKAY_UI_HOST_STATE if set, else okay_uihost.txt in `prefDir` (a per-user folder with
/// a trailing separator, from the platform backend; empty = no state kept, the returned path is empty).
std::string HostStatePath(const char* envValue, const std::string& prefDir);

/// One frame of the window-mode UI: a dock space over the whole display with the "Window mode" status panel and the Audio Mixer (the engine's real mixer,
/// drawn by the same okayuiscreens code the hosted version uses). Between BeginFrame and EndFrame; the first frame builds the layout.
void DrawHostModeUi(HostModeState& state);

/// Create the backend, window and painter, and run frames until the window is closed. Returns the process exit code (0 on a normal close, 1 when no
/// window or painter could be made, with the reason on stderr). Defined in editor/OkayUIHostMode.cpp; with OKAY_HAVE_OKAYUI off it only reports that.
int RunOkayUIHostMode(int argc, char** argv);

} // namespace oked
