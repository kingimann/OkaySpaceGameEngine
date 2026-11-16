#pragma once
// See platform_fakedisplay.cpp. Plain types only: no X11 header here.
namespace fakedisplay {
/// True when libX11 loaded and the X server ($DISPLAY) can be reached.
bool Available();
/// Set (w > 0) or remove (w <= 0) the root window's _NET_WORKAREA (one desktop), what a desktop sets for its panels.
bool SetWorkArea(int x, int y, int w, int h);
/// Set (non-null) or remove (null) the root window's RESOURCE_MANAGER string (where Xft.dpi lives).
bool SetResources(const char* text);
/// Split screen 0 into two RandR 1.5 monitors at x = `splitX`: "OKAY-LEFT" takes the screen's output (so the server drops
/// its automatic monitor), "OKAY-RIGHT" has none; `rightPrimary` marks the right one primary. False without RandR 1.5.
bool SplitIntoTwoMonitors(int splitX, bool rightPrimary);
/// Remove the two monitors again.
bool RemoveMonitors();
} // namespace fakedisplay
