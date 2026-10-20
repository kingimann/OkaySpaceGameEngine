#pragma once
// A tiny, dependency-free startup splash drawn with raw SDL (no ImGui/font/GL),
// so the launcher and editor show a branded loading frame immediately after the
// window is created instead of a black rectangle while the heavier init (font
// atlas, GPU renderers, account/session checks) runs.
//
// Both hosts manage their own entry point (they call SDL_SetMainReady), so make
// sure pulling in SDL here does NOT activate SDL's `#define main SDL_main`
// redirection — otherwise, if this header is the first SDL include in a
// translation unit, that macro mangles later identifiers named `main`
// (e.g. a `Camera::main` member).
#ifndef SDL_MAIN_HANDLED
#  define SDL_MAIN_HANDLED
#endif
#include <SDL.h>
#include <cmath>

namespace okay {

// Draw one animated splash frame and present it:
//   - a soft vertical-gradient backdrop,
//   - an accent-coloured glow with the isometric-cube logo mark floating over
//     a contact shadow (a gentle bob + rim light), and
//   - a rounded indeterminate progress bar sweeping in the accent colour.
// `ar/ag/ab` is the accent colour (0..1). Call every frame to animate, or once
// for a static frame.
inline void DrawSplash(SDL_Renderer* r, int w, int h, float ar, float ag, float ab) {
    if (!r || w <= 0 || h <= 0) return;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    auto U8 = [](float v) { return (Uint8)((v < 0 ? 0 : (v > 1 ? 1 : v)) * 255.0f + 0.5f); };
    const float tms = (float)SDL_GetTicks();

    // ---- Backdrop: gentle vertical gradient (deep charcoal) --------------
    {
        SDL_Color tcol{24, 24, 27, 255}, bcol{13, 13, 15, 255};
        SDL_Vertex bg[4] = {
            {{0.0f, 0.0f}, tcol, {0, 0}}, {{(float)w, 0.0f}, tcol, {0, 0}},
            {{(float)w, (float)h}, bcol, {0, 0}}, {{0.0f, (float)h}, bcol, {0, 0}} };
        int bi[6] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(r, nullptr, bg, 4, bi, 6);
    }

    const float cx = w * 0.5f;
    const float cy = h * 0.5f - 26.0f;

    // Soft elliptical glow (centre colour fading to a transparent edge).
    auto glow = [&](float gx, float gy, float rx, float ry,
                    float cr, float cg, float cb, float a) {
        constexpr int N = 56;
        SDL_Vertex fan[N + 2];
        fan[0].position = {gx, gy};
        fan[0].color = {U8(cr), U8(cg), U8(cb), U8(a)};
        fan[0].tex_coord = {0, 0};
        for (int i = 0; i <= N; ++i) {
            float ang = (float)i / (float)N * 6.2831853f;
            fan[i + 1].position = {gx + std::cos(ang) * rx, gy + std::sin(ang) * ry};
            fan[i + 1].color = {U8(cr), U8(cg), U8(cb), 0};
            fan[i + 1].tex_coord = {0, 0};
        }
        int idx[N * 3];
        for (int i = 0; i < N; ++i) { idx[i*3] = 0; idx[i*3+1] = i+1; idx[i*3+2] = i+2; }
        SDL_RenderGeometry(r, nullptr, fan, N + 2, idx, N * 3);
    };
    // Flat (solid) disc — used for the rounded bar caps.
    auto disc = [&](float gx, float gy, float rad, float cr, float cg, float cb, float a) {
        constexpr int N = 24;
        SDL_Vertex fan[N + 2];
        SDL_Color col{U8(cr), U8(cg), U8(cb), U8(a)};
        fan[0].position = {gx, gy}; fan[0].color = col; fan[0].tex_coord = {0, 0};
        for (int i = 0; i <= N; ++i) {
            float ang = (float)i / (float)N * 6.2831853f;
            fan[i + 1].position = {gx + std::cos(ang) * rad, gy + std::sin(ang) * rad};
            fan[i + 1].color = col; fan[i + 1].tex_coord = {0, 0};
        }
        int idx[N * 3];
        for (int i = 0; i < N; ++i) { idx[i*3] = 0; idx[i*3+1] = i+1; idx[i*3+2] = i+2; }
        SDL_RenderGeometry(r, nullptr, fan, N + 2, idx, N * 3);
    };

    const float pulse = 0.5f + 0.5f * std::sin(tms * 0.0016f);
    const float bob   = std::sin(tms * 0.0022f) * 5.0f;   // gentle vertical float
    const float ccy   = cy + bob;

    // Contact shadow (fixed, so the cube reads as floating above it) + accent glow.
    glow(cx, cy + 86.0f, 86.0f, 18.0f, 0.0f, 0.0f, 0.0f, 0.50f);
    glow(cx, ccy, 158.0f + pulse * 20.0f, 150.0f + pulse * 20.0f, ar, ag, ab, 0.13f + pulse * 0.05f);

    // ---- Isometric cube logo mark ----------------------------------------
    const float S = 100.0f;
    const float hw = S * 0.42f, hh = S * 0.24f, vh = S * 0.34f;
    SDL_FPoint top{cx, ccy - hh - vh * 0.5f}, left{cx - hw, ccy - vh * 0.5f},
               right{cx + hw, ccy - vh * 0.5f}, mid{cx, ccy + hh - vh * 0.5f};
    SDL_FPoint lb{left.x, left.y + vh}, rb{right.x, right.y + vh}, mb{mid.x, mid.y + vh};
    auto shade = [&](float m) -> SDL_Color { return {U8(ar*m), U8(ag*m), U8(ab*m), 255}; };
    auto quad = [&](SDL_FPoint a, SDL_FPoint b, SDL_FPoint c2, SDL_FPoint d, SDL_Color col) {
        SDL_Vertex v[4] = { {a, col, {0, 0}}, {b, col, {0, 0}}, {c2, col, {0, 0}}, {d, col, {0, 0}} };
        int idx[6] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(r, nullptr, v, 4, idx, 6);
    };
    quad(top, right, mid, left, shade(1.00f));   // top face (brightest)
    quad(left, mid, mb, lb,     shade(0.52f));   // left face
    quad(mid, right, rb, mb,    shade(0.76f));   // right face
    // Bright rim light along the two top edges.
    SDL_SetRenderDrawColor(r, U8(ar + (1 - ar) * 0.6f), U8(ag + (1 - ag) * 0.6f),
                           U8(ab + (1 - ab) * 0.6f), 230);
    SDL_RenderDrawLineF(r, top.x, top.y, left.x, left.y);
    SDL_RenderDrawLineF(r, top.x, top.y, right.x, right.y);

    // ---- Rounded indeterminate progress bar ------------------------------
    const float bw = 210.0f, bx = cx - bw * 0.5f, by = cy + 118.0f, bh = 5.0f, brad = bh * 0.5f;
    auto bar = [&](float x0, float x1, float cr, float cg, float cb, float a) {
        if (x1 <= x0) return;
        SDL_SetRenderDrawColor(r, U8(cr), U8(cg), U8(cb), U8(a));
        SDL_FRect rc{x0, by, x1 - x0, bh};
        SDL_RenderFillRectF(r, &rc);
        disc(x0, by + brad, brad, cr, cg, cb, a);   // rounded caps
        disc(x1, by + brad, brad, cr, cg, cb, a);
    };
    bar(bx, bx + bw, 0.21f, 0.21f, 0.235f, 1.0f);    // track
    const float segw = bw * 0.32f;
    float head = (float)std::fmod(tms * 0.00065f, 1.0f);   // 0..1
    float c0 = bx - segw + head * (bw + segw);             // segment slides across
    float x0 = c0 < bx ? bx : c0;
    float x1 = (c0 + segw) > (bx + bw) ? (bx + bw) : (c0 + segw);
    bar(x0, x1, ar, ag, ab, 1.0f);                   // accent sweep

    SDL_RenderPresent(r);
}

} // namespace okay
