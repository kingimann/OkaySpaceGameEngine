#pragma once
// The monitor list, built from okay::platform::Backend (DisplayCount / GetDisplayInfo /
// DisplayModes) instead of SDL calls. On the sdl2 backend it gives the same list the old
// SDL-only code gave (tests/test_platform_seam.cpp compares the two under Xvfb). Header-only
// so the editor, player and the test share it. Window changes (Apply) stay in SdlDisplays.
#include "okay/Core/DisplayModes.hpp"
#include "okay/Platform/Platform.hpp"
#include <string>
#include <vector>

namespace okay::platdisplay {

inline std::vector<DisplayMonitor> ListMonitors(platform::Backend& b) {
    std::vector<DisplayMonitor> out;
    const int n = b.DisplayCount();
    if (n <= 0) return out;
    out.reserve((std::size_t)n);
    for (int i = 0; i < n; ++i) {
        DisplayMonitor m;
        m.index = i;
        platform::DisplayInfo info;
        const bool haveInfo = b.GetDisplayInfo(i, info);
        m.name = haveInfo && !info.name.empty() ? info.name : "Display " + std::to_string(i + 1);
        if (haveInfo) {
            m.x = info.x; m.y = info.y; m.width = info.width; m.height = info.height;
        }
        std::vector<DisplayMode> modes;
        for (const platform::DisplayModeInfo& dm : b.DisplayModes(i)) {
            if (dm.width <= 0 || dm.height <= 0) continue;
            if (m.width <= 0) m.width = dm.width;    // bounds missing: the current mode stands in (first entry)
            if (m.height <= 0) m.height = dm.height;
            modes.push_back({dm.width, dm.height, dm.refreshHz > 0 ? dm.refreshHz : 0});
        }
        m.modes = FilterDisplayModes(std::move(modes));
        out.push_back(std::move(m));
    }
    return out;
}

} // namespace okay::platdisplay
