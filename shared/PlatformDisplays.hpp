#pragma once
// Lists monitors and applies a display choice through okay::platform (either backend). The small helper the player and
// the editor share; it used to call SDL directly (shared/SdlDisplays.cpp, replaced by this file).
#include "okay/Core/DisplayModes.hpp"
#include "okay/Platform/Platform.hpp"
#include <vector>

namespace okay::platformdisplay {

/// Every connected monitor of `b` (the default backend when null) and the video modes it lists. Empty when the
/// backend knows no display.
std::vector<DisplayMonitor> ListMonitors(okay::platform::Backend* b = nullptr);

/// Put `want` on a window: windowed size, borderless desktop fullscreen, or exclusive fullscreen at a listed mode on the
/// monitor `want` names (desktop fullscreen where the backend cannot change the video mode). False when there is no window.
bool Apply(okay::platform::Window* window, const DisplayApply& want);

}
