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
/// Add a RandR mode of w x h at `hz` (a whole number of hertz: the mode's timings are made so that pixel clock /
/// (horizontal total x vertical total) is exactly `hz`) to screen 0's first output, what a monitor that offers more modes
/// does. Xvfb offers only its screen size. False without RandR or when the server refused.
bool AddOutputMode(int w, int h, int hz);
/// What the X server shows now: the size of screen 0's first lit CRTC and the size of the screen. False without RandR.
bool CrtcAndScreen(int& crtcW, int& crtcH, int& screenW, int& screenH);
} // namespace fakedisplay
