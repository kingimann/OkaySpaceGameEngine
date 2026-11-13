#pragma once
// Lists monitors and applies a display choice through SDL2. This is the small helper
// the player and the editor share. It is not the okay::platform seam: SDL stays the
// windowing library, and the own-code backend is not involved.
#include "okay/Core/DisplayModes.hpp"
#include <vector>

struct SDL_Window;

namespace okay::sdldisplay {

/// Every connected monitor and the modes SDL lists for it. Empty when video is not up.
std::vector<DisplayMonitor> ListMonitors();

/// Put `want` on an SDL window: windowed size, borderless desktop, or exclusive fullscreen
/// at a listed mode, on the monitor `want` names. False when there is no window.
bool Apply(SDL_Window* window, const DisplayApply& want);

}
