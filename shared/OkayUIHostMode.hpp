#pragma once
// Window mode: the editor with OkayUI owning the window and the frame (no Dear ImGui). Off by default, a dev switch, step 1 and 2 of the plan in
// docs/own_ui_toolkit.md 6.23. Switch: OKAY_DEV=1 and OKAY_UI_BACKEND=okayui and OKAY_UI_HOST=okayui. Without all three nothing changes.
#include "OkayUIBackend.hpp"

#include <cstring>

namespace oked {

/// The three switches, as main() reads them (the three getenv values, any may be null). The saved settings file does not take part: window mode is
/// chosen per run, so a stale setting cannot move an editor into it.
inline bool HostModeRequested(const char* okayDev, const char* okayUiBackend, const char* okayUiHost) {
    return okayuibackend::DevMode(okayDev) && okayuibackend::Resolve(okayUiBackend, std::string(), true) == okayuibackend::Backend::OkayUI
           && okayUiBackend && okayUiHost && std::strcmp(okayUiHost, "okayui") == 0;
}

/// What the window-mode screen keeps between frames.
struct HostModeState {
    int frame = 0;            // frames drawn
    bool docked = false;      // the first-run layout was built
    bool showStatus = true, showMixer = true;
};

/// One frame of the window-mode UI: a dock space over the whole display with the "Window mode" status panel and the Audio Mixer (the engine's real mixer,
/// drawn by the same okayuiscreens code the hosted version uses). Between BeginFrame and EndFrame; the first frame builds the layout.
void DrawHostModeUi(HostModeState& state);

/// Create the backend, window and painter, and run frames until the window is closed. Returns the process exit code (0 on a normal close, 1 when no
/// window or painter could be made, with the reason on stderr). Defined in editor/OkayUIHostMode.cpp; with OKAY_HAVE_OKAYUI off it only reports that.
int RunOkayUIHostMode(int argc, char** argv);

} // namespace oked
