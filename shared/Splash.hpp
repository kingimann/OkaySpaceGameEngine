#pragma once
// A tiny, dependency-free startup splash drawn with raw SDL (no ImGui/font/GL),
// so the launcher and editor show a branded loading frame immediately after the
// window is created instead of a black rectangle while the heavier init (font
// atlas, GPU renderers, account/session checks) runs.
#include <SDL.h>

namespace okay {

// Draw one splash frame (dark background + the isometric-cube logo mark + a
// loading bar) and present it. `ar/ag/ab` is the accent colour (0..1). Safe to
// call repeatedly to animate the bar; a single call leaves a static frame up.
inline void DrawSplash(SDL_Renderer* r, int w, int h, float ar, float ag, float ab) {
    if (!r || w <= 0 || h <= 0) return;
    SDL_SetRenderDrawColor(r, 30, 30, 32, 255);
    SDL_RenderClear(r);

    const float cx = w * 0.5f, cy = h * 0.5f - 18.0f, S = 96.0f;
    const float hw = S * 0.42f, hh = S * 0.24f, vh = S * 0.34f;
    SDL_FPoint top{cx, cy - hh - vh * 0.5f}, left{cx - hw, cy - vh * 0.5f},
               right{cx + hw, cy - vh * 0.5f}, mid{cx, cy + hh - vh * 0.5f};
    SDL_FPoint lb{left.x, left.y + vh}, rb{right.x, right.y + vh}, mb{mid.x, mid.y + vh};

    auto shade = [&](float m) -> SDL_Color {
        auto cl = [](float v) { return (Uint8)(v < 0 ? 0 : (v > 1 ? 1 : v) * 255.0f); };
        return SDL_Color{cl(ar * m), cl(ag * m), cl(ab * m), 255};
    };
    auto quad = [&](SDL_FPoint a, SDL_FPoint b, SDL_FPoint c2, SDL_FPoint d, SDL_Color col) {
        SDL_Vertex v[4] = { {a, col, {0, 0}}, {b, col, {0, 0}}, {c2, col, {0, 0}}, {d, col, {0, 0}} };
        int idx[6] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(r, nullptr, v, 4, idx, 6);
    };
    quad(top, right, mid, left, shade(1.00f));   // top face (brightest)
    quad(left, mid, mb, lb,     shade(0.55f));   // left face
    quad(mid, right, rb, mb,    shade(0.78f));   // right face

    // Indeterminate loading bar beneath the mark.
    const float bw = 190.0f, bx = cx - bw * 0.5f, by = cy + S * 0.62f, bh = 4.0f;
    SDL_SetRenderDrawColor(r, 58, 58, 62, 255);
    SDL_FRect track{bx, by, bw, bh};
    SDL_RenderFillRectF(r, &track);
    float t = (float)(SDL_GetTicks() % 1300) / 1300.0f;   // 0..1 sweep
    float seg = bw * 0.32f;
    SDL_FRect fill{bx + t * bw, by, seg, bh};
    if (fill.x + fill.w > bx + bw) fill.w = (bx + bw) - fill.x;   // clamp at the right edge
    SDL_SetRenderDrawColor(r, (Uint8)(ar * 255), (Uint8)(ag * 255), (Uint8)(ab * 255), 255);
    SDL_RenderFillRectF(r, &fill);

    SDL_RenderPresent(r);
}

} // namespace okay
