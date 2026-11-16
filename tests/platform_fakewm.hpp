#pragma once
// See platform_fakewm.cpp. Plain types only: no X11 or SDL header here.
namespace fakewm {
/// True when libX11 loaded and the X server ($DISPLAY) can be reached.
bool Available();
/// Set _NET_WM_STATE of X window `win` to the named states (what a window manager does to a minimised window).
bool SetNetWmState(unsigned long win, const char* const* states, int n);
/// Set the ICCCM WM_STATE of `win` (0 withdrawn, 1 normal, 3 iconic).
bool SetWmState(unsigned long win, long state);
/// Whether WM_HINTS of `win` has the urgency hint (ICCCM 4.1.2.4, XUrgencyHint): what a task bar shows as "wants
/// attention". -1 when it cannot be read.
int UrgencyHint(unsigned long win);
/// The X window id behind an SDL_Window*; 0 when it is not an X11 window or the SDL headers were not found.
unsigned long X11WindowOfSdl(void* sdlWindow);
} // namespace fakewm
