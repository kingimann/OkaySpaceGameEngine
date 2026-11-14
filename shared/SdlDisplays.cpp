#include "SdlDisplays.hpp"
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>
#include <string>

namespace okay::sdldisplay {
namespace {

std::string DisplayName(int index) {
    if (const char* n = SDL_GetDisplayName(index)) {
        if (n[0]) return n;
    }
    return "Display " + std::to_string(index + 1);
}

} // namespace

std::vector<DisplayMonitor> ListMonitors() {
    std::vector<DisplayMonitor> out;
    const int n = SDL_GetNumVideoDisplays();
    if (n <= 0) return out;
    out.reserve((std::size_t)n);
    for (int i = 0; i < n; ++i) {
        DisplayMonitor m;
        m.index = i;
        m.name = DisplayName(i);
        SDL_Rect bounds;
        if (SDL_GetDisplayBounds(i, &bounds) == 0) {
            m.x = bounds.x;
            m.y = bounds.y;
            m.width = bounds.w;
            m.height = bounds.h;
        }
        SDL_DisplayMode current;
        if (SDL_GetCurrentDisplayMode(i, &current) == 0) {
            if (m.width <= 0) m.width = current.w;
            if (m.height <= 0) m.height = current.h;
            if (current.w > 0 && current.h > 0)
                m.modes.push_back({current.w, current.h, current.refresh_rate > 0 ? current.refresh_rate : 0});
        }
        const int modes = SDL_GetNumDisplayModes(i);
        for (int k = 0; k < modes; ++k) {
            SDL_DisplayMode dm;
            if (SDL_GetDisplayMode(i, k, &dm) != 0 || dm.w <= 0 || dm.h <= 0) continue;
            m.modes.push_back({dm.w, dm.h, dm.refresh_rate > 0 ? dm.refresh_rate : 0});
        }
        m.modes = FilterDisplayModes(std::move(m.modes));
        out.push_back(std::move(m));
    }
    return out;
}

bool Apply(SDL_Window* window, const DisplayApply& want) {
    if (!window) return false;
    SDL_SetWindowFullscreen(window, 0);
    if (want.hasMonitor && want.desktopWidth > 0 && want.desktopHeight > 0) {
        int x = want.originX;
        int y = want.originY;
        if (want.mode == WindowMode::Windowed && want.hasSize) {
            const int spareW = want.desktopWidth - want.width;
            const int spareH = want.desktopHeight - want.height;
            if (spareW > 0) x += spareW / 2;
            if (spareH > 0) y += spareH / 2;
        }
        SDL_SetWindowPosition(window, x, y);
    }
    if (want.mode == WindowMode::Windowed) {
        SDL_SetWindowBordered(window, SDL_TRUE);
        if (want.hasSize) SDL_SetWindowSize(window, want.width, want.height);
        return true;
    }
    if (want.mode == WindowMode::Borderless) {
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
        return true;
    }
    if (want.hasSize) {
        SDL_DisplayMode request;
        SDL_zero(request);
        request.w = want.width;
        request.h = want.height;
        request.refresh_rate = want.refreshHz > 0 ? want.refreshHz : 0;
        const int display = want.hasMonitor && want.monitorIndex >= 0 ? want.monitorIndex : 0;
        SDL_DisplayMode closest;
        if (SDL_GetClosestDisplayMode(display, &request, &closest)) SDL_SetWindowDisplayMode(window, &closest);
        else SDL_SetWindowDisplayMode(window, &request);
    }
    SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
    return true;
}

} // namespace okay::sdldisplay
