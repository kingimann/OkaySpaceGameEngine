// Monitor list and display choice over the okay::platform seam (Backend::DisplayAt / DisplayModes, Window::SetBordered /
// SetFullscreenMode / SetFullscreen). Written from those interfaces and okay/Core/DisplayModes.hpp; the steps are the
// ones the SDL version took (leave fullscreen, place on the monitor, then size or fullscreen).
#include "PlatformDisplays.hpp"
#include <string>

namespace okay::platformdisplay {

std::vector<DisplayMonitor> ListMonitors(okay::platform::Backend* b) {
    std::vector<DisplayMonitor> out;
    if (!b) b = okay::platform::Default();
    if (!b) return out;
    const int n = b->DisplayCount();
    out.reserve(n > 0 ? (std::size_t)n : 0u);
    for (int i = 0; i < n; ++i) {
        okay::platform::DisplayInfo di;
        if (!b->DisplayAt(i, di)) continue;
        DisplayMonitor m;
        m.index = i;
        m.name = di.name.empty() ? "Display " + std::to_string(i + 1) : di.name;
        m.x = di.x; m.y = di.y; m.width = di.w; m.height = di.h;
        for (const okay::platform::DisplayMode& dm : b->DisplayModes(i))
            if (dm.w > 0 && dm.h > 0) m.modes.push_back({dm.w, dm.h, dm.refreshHz > 0 ? dm.refreshHz : 0});
        m.modes = FilterDisplayModes(std::move(m.modes));
        out.push_back(std::move(m));
    }
    return out;
}

bool Apply(okay::platform::Window* window, const DisplayApply& want) {
    if (!window) return false;
    window->SetFullscreen(false);
    if (want.hasMonitor && want.desktopWidth > 0 && want.desktopHeight > 0) {
        int x = want.originX;
        int y = want.originY;
        if (want.mode == WindowMode::Windowed && want.hasSize) {
            const int spareW = want.desktopWidth - want.width;
            const int spareH = want.desktopHeight - want.height;
            if (spareW > 0) x += spareW / 2;
            if (spareH > 0) y += spareH / 2;
        }
        window->SetPosition(x, y);
    }
    if (want.mode == WindowMode::Windowed) {
        window->SetBordered(true);
        if (want.hasSize) window->SetSize(want.width, want.height);
        return true;
    }
    if (want.mode == WindowMode::Borderless) {
        window->SetFullscreen(true);
        return true;
    }
    okay::platform::DisplayMode mode;
    if (want.hasSize) { mode.w = want.width; mode.h = want.height; mode.refreshHz = want.refreshHz > 0 ? want.refreshHz : 0; }
    const int display = want.hasMonitor && want.monitorIndex >= 0 ? want.monitorIndex : 0;
    if (!window->SetFullscreenMode(mode, display)) window->SetFullscreen(true);   // no video mode change on this backend
    return true;
}

} // namespace okay::platformdisplay
