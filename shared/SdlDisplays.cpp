#include "SdlDisplays.hpp"
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>
#include "PlatformDisplays.hpp"
#include <string>

namespace okay::sdldisplay {
namespace {

} // namespace

std::vector<DisplayMonitor> ListMonitors() {
    // Through the seam. On the sdl2 backend this is the same SDL calls as before; the
    // monitor list belongs to the SDL window here, so ask the sdl2 backend, not Default().
    platform::Backend* b = platform::Get(platform::BackendKind::Sdl2);
    return b ? platdisplay::ListMonitors(*b) : std::vector<DisplayMonitor>();
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
