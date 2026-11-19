// The same functions as SdlDisplays.cpp (monitor list, applying a display choice) over the okay::platform seam, for a build with no SDL (OKAY_NO_SDL).
// The seam lists each monitor's current size and refresh rate but not every video mode, so a monitor has one mode and "exclusive fullscreen" is
// the desktop-sized borderless fullscreen (no resolution change). Written from the seam's DisplayInfo and Window interfaces.
#include "SdlDisplays.hpp"
#include "WindowHandles.hpp"
#include "okay/Platform/Platform.hpp"

namespace okay::sdldisplay {

std::vector<DisplayMonitor> ListMonitors() {
    std::vector<DisplayMonitor> out;
    okay::platform::Backend* b = okay::platform::Default();
    if (!b) return out;
    const int n = b->DisplayCount();
    for (int i = 0; i < n; ++i) {
        okay::platform::DisplayInfo di;
        if (!b->DisplayAt(i, di) || di.w <= 0 || di.h <= 0) continue;
        DisplayMonitor m;
        m.index = i;
        m.name = di.name.empty() ? "Display " + std::to_string(i + 1) : di.name;
        m.x = di.x; m.y = di.y; m.width = di.w; m.height = di.h;
        m.modes.push_back({di.w, di.h, di.refreshHz > 0 ? di.refreshHz : 0});
        m.modes = FilterDisplayModes(std::move(m.modes));
        out.push_back(std::move(m));
    }
    return out;
}

bool Apply(SDL_Window* window, const DisplayApply& want) {
    auto w = okay::pb::Win(window);
    if (!window || !w) return false;
    w->SetFullscreen(false);
    if (want.hasMonitor && want.desktopWidth > 0 && want.desktopHeight > 0) {
        int x = want.originX, y = want.originY;
        if (want.mode == WindowMode::Windowed && want.hasSize) {
            const int spareW = want.desktopWidth - want.width, spareH = want.desktopHeight - want.height;
            if (spareW > 0) x += spareW / 2;
            if (spareH > 0) y += spareH / 2;
        }
        w->SetPosition(x, y);
    }
    if (want.mode == WindowMode::Windowed) {
        if (want.hasSize) w->SetSize(want.width, want.height);
        return true;
    }
    w->SetFullscreen(true);   // borderless desktop fullscreen for both Borderless and Fullscreen (no video mode change on this backend)
    return true;
}

} // namespace okay::sdldisplay
