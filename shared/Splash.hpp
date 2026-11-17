#pragma once
// A tiny, dependency-free startup splash drawn through okay::platform::Painter (no ImGui/font/GL, no SDL),
// so the launcher, the editor and the player show a branded loading frame immediately after the
// window is created instead of a black rectangle while the heavier init (font
// atlas, GPU renderers, account/session checks) runs. The painter is SDL_Renderer on an sdl2 window
// and our own painters otherwise; the 2D types are okay::ui2d's (laid out as SDL's).
#include "okay/Platform/Platform.hpp"   // okay::platform: timers
#include "okay/Platform/Painter.hpp"    // the splash is drawn through the platform painter
#include <cmath>
#include <memory>

namespace okay {

// How the splash looks — lets a host customize the loading screen: the accent
// colour, the background gradient, an optional custom logo image (drawn instead
// of the built-in isometric cube), and whether the cube / progress bar show.
struct SplashStyle {
    float ar = 0.235f, ag = 0.475f, ab = 0.800f;   // accent colour (0..1)
    ui2d::RGBA bgTop{24, 24, 27, 255};              // backdrop gradient (top)
    ui2d::RGBA bgBottom{13, 13, 15, 255};           //                  (bottom)
    okay::ui2d::Tex logo = nullptr;                // custom logo (a texture of the painter the splash is drawn on); replaces the cube when set
    float logoScale = 1.0f;                        // relative size of the custom logo
    bool  showCube = true;                         // draw the built-in cube (ignored if logo set)
    bool  showProgress = true;                     // draw the sweeping progress bar
};

// Draw one animated splash frame (into `style`) and present it:
//   - a soft vertical-gradient backdrop,
//   - an accent-coloured glow with the logo mark (a custom image, or the
//     built-in isometric cube) floating over a contact shadow, and
//   - a rounded indeterminate progress bar sweeping in the accent colour.
// Call every frame to animate, or once for a static frame.
inline void DrawSplashStyled(okay::platform::Painter* r, int w, int h, const SplashStyle& style) {
    if (!r || w <= 0 || h <= 0) return;
    r->SetBlendMode(okay::platform::BlendMode::Blend);
    auto U8 = [](float v) { return (std::uint8_t)((v < 0 ? 0 : (v > 1 ? 1 : v)) * 255.0f + 0.5f); };
    const float tms = (float)okay::platform::TicksMs32();
    const float ar = style.ar, ag = style.ag, ab = style.ab;

    // ---- Backdrop: gentle vertical gradient (deep charcoal) --------------
    {
        ui2d::RGBA tcol = style.bgTop, bcol = style.bgBottom;
        ui2d::Vertex bg[4] = {
            {{0.0f, 0.0f}, tcol, {0, 0}}, {{(float)w, 0.0f}, tcol, {0, 0}},
            {{(float)w, (float)h}, bcol, {0, 0}}, {{0.0f, (float)h}, bcol, {0, 0}} };
        int bi[6] = {0, 1, 2, 0, 2, 3};
        r->Geometry(nullptr, bg, 4, bi, 6);
    }

    const float cx = w * 0.5f;
    const float cy = h * 0.5f - 26.0f;

    // Soft elliptical glow (centre colour fading to a transparent edge).
    auto glow = [&](float gx, float gy, float rx, float ry,
                    float cr, float cg, float cb, float a) {
        constexpr int N = 56;
        ui2d::Vertex fan[N + 2];
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
        r->Geometry(nullptr, fan, N + 2, idx, N * 3);
    };
    // Flat (solid) disc — used for the rounded bar caps.
    auto disc = [&](float gx, float gy, float rad, float cr, float cg, float cb, float a) {
        constexpr int N = 24;
        ui2d::Vertex fan[N + 2];
        ui2d::RGBA col{U8(cr), U8(cg), U8(cb), U8(a)};
        fan[0].position = {gx, gy}; fan[0].color = col; fan[0].tex_coord = {0, 0};
        for (int i = 0; i <= N; ++i) {
            float ang = (float)i / (float)N * 6.2831853f;
            fan[i + 1].position = {gx + std::cos(ang) * rad, gy + std::sin(ang) * rad};
            fan[i + 1].color = col; fan[i + 1].tex_coord = {0, 0};
        }
        int idx[N * 3];
        for (int i = 0; i < N; ++i) { idx[i*3] = 0; idx[i*3+1] = i+1; idx[i*3+2] = i+2; }
        r->Geometry(nullptr, fan, N + 2, idx, N * 3);
    };

    const float pulse = 0.5f + 0.5f * std::sin(tms * 0.0016f);
    const float bob   = std::sin(tms * 0.0022f) * 5.0f;   // gentle vertical float
    const float ccy   = cy + bob;

    // Contact shadow (fixed, so the cube reads as floating above it) + accent glow.
    glow(cx, cy + 86.0f, 86.0f, 18.0f, 0.0f, 0.0f, 0.0f, 0.50f);
    glow(cx, ccy, 158.0f + pulse * 20.0f, 150.0f + pulse * 20.0f, ar, ag, ab, 0.13f + pulse * 0.05f);

    if (style.logo) {
        // ---- Custom logo image (replaces the cube) -----------------------
        int tw = 0, th = 0;
        r->QueryTexture(style.logo, &tw, &th);
        if (tw > 0 && th > 0) {
            float maxDim = 120.0f * (style.logoScale > 0.05f ? style.logoScale : 1.0f);
            float scale = maxDim / (float)(tw > th ? tw : th);
            float dw = tw * scale, dh = th * scale;
            ui2d::FRect dst{cx - dw * 0.5f, ccy - dh * 0.5f, dw, dh};
            r->CopyF(style.logo, nullptr, &dst);
        }
    } else if (style.showCube) {
        // ---- Isometric cube logo mark ------------------------------------
        const float S = 100.0f;
        const float hw = S * 0.42f, hh = S * 0.24f, vh = S * 0.34f;
        ui2d::FPoint top{cx, ccy - hh - vh * 0.5f}, left{cx - hw, ccy - vh * 0.5f},
                   right{cx + hw, ccy - vh * 0.5f}, mid{cx, ccy + hh - vh * 0.5f};
        ui2d::FPoint lb{left.x, left.y + vh}, rb{right.x, right.y + vh}, mb{mid.x, mid.y + vh};
        auto shade = [&](float m) -> ui2d::RGBA { return {U8(ar*m), U8(ag*m), U8(ab*m), 255}; };
        auto quad = [&](ui2d::FPoint a, ui2d::FPoint b, ui2d::FPoint c2, ui2d::FPoint d, ui2d::RGBA col) {
            ui2d::Vertex v[4] = { {a, col, {0, 0}}, {b, col, {0, 0}}, {c2, col, {0, 0}}, {d, col, {0, 0}} };
            int idx[6] = {0, 1, 2, 0, 2, 3};
            r->Geometry(nullptr, v, 4, idx, 6);
        };
        quad(top, right, mid, left, shade(1.00f));   // top face (brightest)
        quad(left, mid, mb, lb,     shade(0.52f));   // left face
        quad(mid, right, rb, mb,    shade(0.76f));   // right face
        // Bright rim light along the two top edges.
        r->SetDrawColor(U8(ar + (1 - ar) * 0.6f), U8(ag + (1 - ag) * 0.6f), U8(ab + (1 - ab) * 0.6f), 230);
        r->DrawLineF(top.x, top.y, left.x, left.y);
        r->DrawLineF(top.x, top.y, right.x, right.y);
    }

    // ---- Rounded indeterminate progress bar ------------------------------
    if (style.showProgress) {
    const float bw = 210.0f, bx = cx - bw * 0.5f, by = cy + 118.0f, bh = 5.0f, brad = bh * 0.5f;
    auto bar = [&](float x0, float x1, float cr, float cg, float cb, float a) {
        if (x1 <= x0) return;
        r->SetDrawColor(U8(cr), U8(cg), U8(cb), U8(a));
        ui2d::FRect rc{x0, by, x1 - x0, bh};
        r->FillRectF(&rc);
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
    }

    r->Present();
}

} // namespace okay
