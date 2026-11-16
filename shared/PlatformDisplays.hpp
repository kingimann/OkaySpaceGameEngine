#pragma once
// The monitor list and the window-mode switch of the game's display settings (okay::DisplaySettings) through okay::platform, with
// no SDL: what shared/SdlDisplays.* does with SDL, for the editor's no-SDL mode (docs/sdl_removal_plan.md, stage E3).
//
// What is the same and what is not, compared with the SDL version:
//  * the monitors, their names, places and sizes come from Backend::Displays() (our X11 RandR / Win32 code);
//  * okay::platform has no list of display modes yet: each monitor lists one mode, its desktop size (refresh rate 0, unknown);
//  * Windowed and Borderless (desktop fullscreen) work as with SDL; exclusive Fullscreen has no mode switch here, so it is
//    desktop fullscreen at the monitor's own size (the window is not resized to the chosen mode).
// Written from the two interfaces (okay::DisplayMonitor / DisplayApply in engine/include/okay/Core/DisplayModes.hpp, okay::platform's
// Backend::Displays and Window in okay/Platform/Platform.hpp). Header-only so the tests can use it with a fake window.
#include "okay/Core/DisplayModes.hpp"
#include "okay/Platform/Platform.hpp"

#include <string>
#include <vector>

namespace okay::platdisplay {

/// The monitors as DisplaySettings wants them, from a list of okay::platform displays (the primary one first).
inline std::vector<DisplayMonitor> MonitorsFrom(const std::vector<okay::platform::DisplayInfo>& displays) {
    std::vector<DisplayMonitor> out;
    out.reserve(displays.size());
    for (std::size_t i = 0; i < displays.size(); ++i) {
        const okay::platform::DisplayInfo& d = displays[i];
        DisplayMonitor m;
        m.index = (int)i;
        m.name = d.name.empty() ? "Display " + std::to_string(i + 1) : d.name;
        m.x = d.bounds.x; m.y = d.bounds.y;
        m.width = d.bounds.w; m.height = d.bounds.h;
        if (m.width > 0 && m.height > 0) m.modes.push_back({m.width, m.height, 0});
        m.modes = FilterDisplayModes(std::move(m.modes));
        out.push_back(std::move(m));
    }
    return out;
}

inline std::vector<DisplayMonitor> ListMonitors(okay::platform::Backend& b) { return MonitorsFrom(b.Displays()); }

/// Put `window` into the chosen mode (the same steps as the SDL version, minus the display-mode switch).
inline bool Apply(okay::platform::Window& window, const DisplayApply& want) {
    window.SetFullscreen(false);
    if (want.hasMonitor && want.desktopWidth > 0 && want.desktopHeight > 0) {
        int x = want.originX;
        int y = want.originY;
        if (want.mode == WindowMode::Windowed && want.hasSize) {
            const int spareW = want.desktopWidth - want.width;
            const int spareH = want.desktopHeight - want.height;
            if (spareW > 0) x += spareW / 2;
            if (spareH > 0) y += spareH / 2;
        }
        window.SetPosition(x, y);
    }
    if (want.mode == WindowMode::Windowed) {
        if (want.hasSize) window.SetSize(want.width, want.height);
        return true;
    }
    window.SetFullscreen(true);   // Borderless, and Fullscreen without a mode switch
    return true;
}

} // namespace okay::platdisplay
