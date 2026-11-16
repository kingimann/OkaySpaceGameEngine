#pragma once
// The monitor list and the window-mode switch of the game's display settings (okay::DisplaySettings) through okay::platform, with
// no SDL: what shared/SdlDisplays.* does with SDL, for the editor's no-SDL mode (docs/sdl_removal_plan.md, stage E3).
//
// What is the same and what is not, compared with the SDL version:
//  * the monitors, their names, places and sizes come from Backend::Displays() (our X11 RandR / Win32 code);
//  * each monitor's modes come from Backend::DisplayModes() (X11 RandR, Win32 EnumDisplaySettingsW, SDL on the sdl2 backend)
//    plus its current mode; a backend that lists none gives one mode, the desktop size (refresh rate 0, unknown);
//  * Windowed and Borderless (desktop fullscreen) work as with SDL; exclusive Fullscreen switches the display's mode with
//    Window::SetFullscreenMode (X11 XRRSetCrtcConfig, Win32 ChangeDisplaySettingsExW) and falls back to desktop fullscreen
//    when the switch is refused.
// Written from the two interfaces (okay::DisplayMonitor / DisplayApply in engine/include/okay/Core/DisplayModes.hpp, okay::platform's
// Backend::Displays and Window in okay/Platform/Platform.hpp). Header-only so the tests can use it with a fake window.
#include "okay/Core/DisplayModes.hpp"
#include "okay/Platform/Platform.hpp"

#include <string>
#include <vector>

namespace okay::platdisplay {

/// The monitors as DisplaySettings wants them, from a list of okay::platform displays (the primary one first) and the modes each
/// one lists (`modes[i]` for display i; a display with no list gets one mode, its desktop size).
inline std::vector<DisplayMonitor> MonitorsFrom(const std::vector<okay::platform::DisplayInfo>& displays,
                                                const std::vector<std::vector<okay::platform::VideoMode>>& modes) {
    std::vector<DisplayMonitor> out;
    out.reserve(displays.size());
    for (std::size_t i = 0; i < displays.size(); ++i) {
        const okay::platform::DisplayInfo& d = displays[i];
        DisplayMonitor m;
        m.index = (int)i;
        m.name = d.name.empty() ? "Display " + std::to_string(i + 1) : d.name;
        m.x = d.bounds.x; m.y = d.bounds.y;
        m.width = d.bounds.w; m.height = d.bounds.h;
        if (i < modes.size())
            for (const okay::platform::VideoMode& v : modes[i]) m.modes.push_back({v.width, v.height, v.refreshHz});
        if (m.modes.empty() && m.width > 0 && m.height > 0) m.modes.push_back({m.width, m.height, 0});
        m.modes = FilterDisplayModes(std::move(m.modes));
        out.push_back(std::move(m));
    }
    return out;
}

/// The same with no mode lists: one mode per display, its desktop size.
inline std::vector<DisplayMonitor> MonitorsFrom(const std::vector<okay::platform::DisplayInfo>& displays) {
    return MonitorsFrom(displays, {});
}

/// The backend's monitors and the modes each one lists (with its current mode, as SDL's list has it).
inline std::vector<DisplayMonitor> ListMonitors(okay::platform::Backend& b) {
    const std::vector<okay::platform::DisplayInfo> ds = b.Displays();
    std::vector<std::vector<okay::platform::VideoMode>> modes(ds.size());
    for (std::size_t i = 0; i < ds.size(); ++i) {
        modes[i] = b.DisplayModes((int)i);
        okay::platform::VideoMode cur;
        if (b.CurrentDisplayMode((int)i, cur)) modes[i].push_back(cur);
    }
    return MonitorsFrom(ds, modes);
}

/// Put `window` into the chosen mode (the same steps as the SDL version).
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
    if (want.mode == WindowMode::Fullscreen && want.hasSize &&
        window.SetFullscreenMode(okay::platform::VideoMode{want.width, want.height, want.refreshHz > 0 ? want.refreshHz : 0}))
        return true;              // exclusive fullscreen at the chosen mode
    window.SetFullscreen(true);   // Borderless, and Fullscreen when the mode switch was refused (desktop fullscreen)
    return true;
}

} // namespace okay::platdisplay
