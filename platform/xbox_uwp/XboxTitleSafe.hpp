#pragma once
// The title-safe area for the Xbox UWP app (pure, no Windows headers): where on a TV screen menus and HUD text must stay so that
// overscan (a TV cutting off the picture edges) never hides them. Used by XboxGameRuntime to fill okay::UISafeInsets(), which the
// UI Safe Area container (UISafeArea.hpp) already obeys; tested on Linux by tests/test_xbox_uwp_runtime.cpp (TitleSafeTests).
//
// How it fits. By default a UWP app on an Xbox is laid out inside the TV-safe area by the system (BoundsMode::SystemSafe): the
// picture does not reach the screen edges and nothing has to be added. A game wants the whole screen, so the app shell asks for
// ApplicationViewBoundsMode.UseCoreWindow (BoundsMode::FullScreen); then the 3D view fills the TV and the UI must keep itself inside
// the safe area: Microsoft's "Designing for Xbox and TV" page gives 48 effective pixels left and right and 27 top and bottom of the
// 960 x 540 effective screen, which is 5% of each side. Those numbers are FROM MEMORY of that page: check them. On a PC (device
// family other than Windows.Xbox) there is no overscan and the insets are zero.
// Written from Microsoft's documentation of ApplicationView.SetDesiredBoundsMode, ApplicationViewBoundsMode and
// AnalyticsInfo.VersionInfo.DeviceFamily, and the design page above. Our own code. UNVERIFIED on a console.
#include <string>

namespace okay::xbox {

enum class BoundsMode { SystemSafe, FullScreen };

/// Fractions of the screen width (left, right) and height (top, bottom), the same shape as okay::UISafeInsetFractions.
struct SafeInsets {
    float left = 0, top = 0, right = 0, bottom = 0;
    bool Zero() const { return left == 0 && top == 0 && right == 0 && bottom == 0; }
};

constexpr float kTvSafeFraction = 0.05f;   // 48 / 960 and 27 / 540

inline SafeInsets TitleSafeInsets(BoundsMode mode, bool onXbox) {
    SafeInsets i;
    if (mode == BoundsMode::FullScreen && onXbox) i.left = i.top = i.right = i.bottom = kTvSafeFraction;
    return i;
}

/// Never negative, never more than a quarter of a side; NaN becomes 0.
inline SafeInsets Clamped(SafeInsets i) {
    auto c = [](float v) { return !(v > 0.0f) ? 0.0f : (v > 0.25f ? 0.25f : v); };
    i.left = c(i.left); i.top = c(i.top); i.right = c(i.right); i.bottom = c(i.bottom);
    return i;
}

struct SafeRect { int x = 0, y = 0, w = 0, h = 0; };

/// The safe rectangle in pixels of a w x h picture (edges rounded to the nearest pixel).
inline SafeRect TitleSafeRect(int w, int h, SafeInsets in) {
    SafeRect r;
    if (w <= 0 || h <= 0) return r;
    const SafeInsets i = Clamped(in);
    auto px = [](float f, int n) { return (int)(f * (float)n + 0.5f); };
    r.x = px(i.left, w); r.y = px(i.top, h);
    r.w = w - r.x - px(i.right, w); r.h = h - r.y - px(i.bottom, h);
    if (r.w < 0) r.w = 0;
    if (r.h < 0) r.h = 0;
    return r;
}

/// AnalyticsInfo.VersionInfo.DeviceFamily is "Windows.Xbox" on a console (compared without regard to case).
inline bool IsXboxDeviceFamily(const std::string& family) {
    static const char kXbox[] = "windows.xbox";
    if (family.size() != sizeof(kXbox) - 1) return false;
    for (size_t i = 0; i < family.size(); ++i) {
        char c = family[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != kXbox[i]) return false;
    }
    return true;
}

} // namespace okay::xbox
