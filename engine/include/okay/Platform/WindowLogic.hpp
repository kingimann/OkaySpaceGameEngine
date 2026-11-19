// Pure decisions the editor and launcher make about their windows, kept free of any window system so they can be
// tested on their own. Written from the behaviour the apps already had (no outside code).
#pragma once
#include <cstdint>

namespace okay::platform {

/// First-run UI scale for a display of `dpi` (96 = 100 percent): snapped to a quarter step (1.0, 1.25, 1.5 ...) and
/// kept between 1.0 and 2.0. An unknown or non-positive DPI gives 1.0.
inline float UiScaleFromDpi(int dpi) {
    if (dpi <= 0) return 1.0f;
    float s = (float)dpi / 96.0f;
    s = (float)(int)(s * 4.0f + 0.5f) / 4.0f;   // round half up to a quarter
    return s < 1.0f ? 1.0f : (s > 2.0f ? 2.0f : s);
}

/// The editor hands the keyboard back to its main window after a pop-out viewport closed, but only if the window system
/// did not (no window has the keyboard) and the pointer is over the main window, so another program's focus is never taken.
inline bool ShouldReclaimInputFocus(int prevViewports, int viewports, bool mainHasInputFocus,
                                    uint32_t keyboardFocusId, uint32_t mouseFocusId, uint32_t mainId) {
    return prevViewports > 1 && viewports <= 1 && !mainHasInputFocus && keyboardFocusId == 0 &&
           mainId != 0 && mouseFocusId == mainId;
}

/// A window that should draw at the full frame rate: it has the keyboard or the mouse (or its chat window does).
inline bool IsActiveForFrameRate(bool inputFocus, bool mouseFocus, bool otherWindowFocused) {
    return inputFocus || mouseFocus || otherWindowFocused;
}

/// The launcher flashes the task bar when it is minimized or has no keyboard focus.
inline bool NeedsAttentionFlash(bool minimized, bool inputFocus) { return minimized || !inputFocus; }

/// The launcher skips drawing its main window while it is hidden or minimized.
inline bool IsBackgroundWindow(bool visible, bool minimized) { return !visible || minimized; }

} // namespace okay::platform
