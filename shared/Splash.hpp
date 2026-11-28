#pragma once
// The start-up splash (loading screen) of the editor, the launcher and the player, drawn through the platform
// painter right after the window is made, so start-up never shows a black rectangle while the heavier set-up
// (fonts, GPU renderers, the project) runs.
//
// Two layers:
//   SplashStyle  how it looks (the project's or the user's choices: accent, backdrop, logo, cube, bar).
//   SplashProgress  what is happening (optional): the project name, the step running now, a determinate progress
//                value, the version, a failed step, and the opacity for the fade into the host's first frame.
// Without a state the splash is the plain animated frame with a sweeping bar (the launcher and the player).
// SplashSteps turns a list of named start-up steps into the progress value and status line; SplashFinish is
// the fade at the end (hold while the host still works, show a failure for a moment, then fade out).
//
// Text uses the engine's built-in UI font (okay::BuiltinUIFont, our own TTF rasterizer) through
// ui2d::DrawTtfText, the in-game UI pass's text drawing.
//
// No SDL header or type: everything goes through the platform painter.
#include "okay/Platform/Platform.hpp"   // okay::platform: timers
#include "okay/Platform/Painter.hpp"    // the splash is drawn through the platform painter
#include "okay/Graphics/TtfFont.hpp"    // okay::BuiltinUIFont: the status text's font
#include "okay/UI/UIRenderer.hpp"       // ui2d::DrawTtfText / UIMeasureText
#include "HierarchyPanelModel.hpp"      // okaypanels::HierEllipsize: long names cut with an ellipsis
#include "PainterBridge.hpp"
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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
    bool  showProgress = true;                     // draw the progress bar
    okay::TtfFont* font = nullptr;                 // the text's font (null = the engine's built-in UI font)
};

// What the splash says and how far start-up is. Every field is optional.
struct SplashProgress {
    std::string title;          // the big line under the logo: the project's name (empty = no title)
    std::string status;         // what is loading now ("Opening project Space Game"); in the failed state, the step's message
    std::string detail;         // right end of the status line (empty = "42%" while the progress is known)
    std::string footer;         // bottom line: product and version ("OkaySpace Editor  v1.2.3")
    float progress = -1.0f;     // 0..1 = a determinate bar; below 0 = the indeterminate sweep (a step that cannot measure itself)
    bool  failed = false;       // a step failed: the bar and status turn red and `failNote` is shown under the status
    std::string failNote = "The editor will open anyway.";
    float opacity = 1.0f;       // the whole splash (the fade over the host's first frame)
    bool  present = true;       // false: draw only; the host presents (drawn over its own frame)
    double timeMs = -1.0;       // the animation clock (tests and pictures); below 0 = the real clock
};

// Where the parts of the splash go on a w x h output (the drawing and the tests use the same numbers).
struct SplashLayout {
    float cx = 0, cy = 0;                     // logo centre (before its gentle bob)
    float titleY = 0, titlePx = 0;            // title: top of the line, text height in pixels
    float barX = 0, barY = 0, barW = 0, barH = 0;
    float fillW = 0;                          // the determinate fill (0 for the sweep)
    float statusY = 0, statusPx = 0;          // status line: top, text height
    float noteY = 0;                          // the failure note under the status line
    float footerY = 0, footerPx = 0;
    bool  text = false;                       // any text to draw
};

inline float SplashClamp01(float v) { return !(v > 0.0f) ? 0.0f : (v > 1.0f ? 1.0f : v); }   // nan counts as 0

inline SplashLayout ComputeSplashLayout(int w, int h, const SplashProgress* st) {
    SplashLayout L;
    L.text = st && (!st->title.empty() || !st->status.empty() || !st->footer.empty() || st->failed);
    L.cx = w * 0.5f;
    if (!L.text) {   // the plain frame: the positions the launcher and the player always had
        L.cy = h * 0.5f - 26.0f;
        L.barW = 210.0f; L.barH = 5.0f;
        L.barX = L.cx - L.barW * 0.5f; L.barY = L.cy + 118.0f;
    } else {
        L.cy = h * 0.5f - 62.0f;
        L.titlePx = 24.0f;
        L.titleY = L.cy + 104.0f;
        float bw = w * 0.34f;
        bw = bw < 240.0f ? 240.0f : (bw > 380.0f ? 380.0f : bw);
        if (bw > w - 40.0f) bw = w - 40.0f > 40.0f ? w - 40.0f : 40.0f;
        L.barW = bw; L.barH = 4.0f;
        L.barX = L.cx - bw * 0.5f;
        L.barY = L.titleY + (st->title.empty() ? 0.0f : 46.0f);
        L.statusPx = 15.0f;
        L.statusY = L.barY + L.barH + 12.0f;
        L.noteY = L.statusY + 22.0f;
        L.footerPx = 13.0f;
        L.footerY = h - 34.0f;
    }
    if (st && st->progress >= 0.0f) L.fillW = std::floor(L.barW * SplashClamp01(st->progress) + 0.5f);
    return L;
}

// Draw one animated splash frame (into `style`) and present it:
//   - a soft vertical-gradient backdrop,
//   - an accent-coloured glow with the logo mark (a custom image, or the
//     built-in isometric cube) floating over a contact shadow,
//   - a rounded progress bar in the accent colour: filled to `state->progress` with a slow shine
//     moving over the fill, or the indeterminate sweep, and
//   - with a state: the title, the status line with the percentage, the failure note and the footer.
// Call every frame to animate, or once for a static frame.
inline void DrawSplashStyled(okay::platform::Painter* r, int w, int h, const SplashStyle& style, const SplashProgress* state = nullptr) {
    if (!r || w <= 0 || h <= 0) return;
    const float op = state ? SplashClamp01(state->opacity) : 1.0f;
    if (op <= 0.0f) { if (!state || state->present) r->Present(); return; }
    r->SetBlendMode(okay::platform::BlendMode::Blend);
    auto U8 = [](float v) { return (std::uint8_t)(SplashClamp01(v) * 255.0f + 0.5f); };
    const float tms = state && state->timeMs >= 0.0 ? (float)state->timeMs : (float)okay::platform::TicksMs32();
    const float ar = style.ar, ag = style.ag, ab = style.ab;
    const SplashLayout L = ComputeSplashLayout(w, h, state);
    const bool failed = state && state->failed;
    // The failure colour: a soft red that reads on the dark backdrop.
    const float er = 0.93f, eg = 0.40f, eb = 0.37f;

    // ---- Backdrop: gentle vertical gradient (deep charcoal) --------------
    {
        ui2d::RGBA tcol = style.bgTop, bcol = style.bgBottom;
        tcol.a = U8(op); bcol.a = U8(op);
        ui2d::Vertex bg[4] = {
            {{0.0f, 0.0f}, tcol, {0, 0}}, {{(float)w, 0.0f}, tcol, {0, 0}},
            {{(float)w, (float)h}, bcol, {0, 0}}, {{0.0f, (float)h}, bcol, {0, 0}} };
        int bi[6] = {0, 1, 2, 0, 2, 3};
        r->Geometry(nullptr, (const ui2d::Vertex*)bg, 4, bi, 6);
    }

    const float cx = L.cx;
    const float cy = L.cy;

    // Soft elliptical glow (centre colour fading to a transparent edge).
    auto glow = [&](float gx, float gy, float rx, float ry,
                    float cr, float cg, float cb, float a) {
        constexpr int N = 56;
        ui2d::Vertex fan[N + 2];
        fan[0].position = {gx, gy};
        fan[0].color = {U8(cr), U8(cg), U8(cb), U8(a * op)};
        fan[0].tex_coord = {0, 0};
        for (int i = 0; i <= N; ++i) {
            float ang = (float)i / (float)N * 6.2831853f;
            fan[i + 1].position = {gx + std::cos(ang) * rx, gy + std::sin(ang) * ry};
            fan[i + 1].color = {U8(cr), U8(cg), U8(cb), 0};
            fan[i + 1].tex_coord = {0, 0};
        }
        int idx[N * 3];
        for (int i = 0; i < N; ++i) { idx[i*3] = 0; idx[i*3+1] = i+1; idx[i*3+2] = i+2; }
        r->Geometry(nullptr, (const ui2d::Vertex*)fan, N + 2, idx, N * 3);
    };
    // Flat (solid) disc — used for the rounded bar caps.
    auto disc = [&](float gx, float gy, float rad, float cr, float cg, float cb, float a) {
        constexpr int N = 24;
        ui2d::Vertex fan[N + 2];
        ui2d::RGBA col{U8(cr), U8(cg), U8(cb), U8(a * op)};
        fan[0].position = {gx, gy}; fan[0].color = col; fan[0].tex_coord = {0, 0};
        for (int i = 0; i <= N; ++i) {
            float ang = (float)i / (float)N * 6.2831853f;
            fan[i + 1].position = {gx + std::cos(ang) * rad, gy + std::sin(ang) * rad};
            fan[i + 1].color = col; fan[i + 1].tex_coord = {0, 0};
        }
        int idx[N * 3];
        for (int i = 0; i < N; ++i) { idx[i*3] = 0; idx[i*3+1] = i+1; idx[i*3+2] = i+2; }
        r->Geometry(nullptr, (const ui2d::Vertex*)fan, N + 2, idx, N * 3);
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
            if (op < 1.0f) r->SetTextureAlphaMod(style.logo, U8(op));
            r->CopyF(style.logo, nullptr, (const ui2d::FRect*)&dst);
            if (op < 1.0f) r->SetTextureAlphaMod(style.logo, 255);
        }
    } else if (style.showCube) {
        // ---- Isometric cube logo mark ------------------------------------
        const float S = 100.0f;
        const float hw = S * 0.42f, hh = S * 0.24f, vh = S * 0.34f;
        ui2d::FPoint top{cx, ccy - hh - vh * 0.5f}, left{cx - hw, ccy - vh * 0.5f},
                   right{cx + hw, ccy - vh * 0.5f}, mid{cx, ccy + hh - vh * 0.5f};
        ui2d::FPoint lb{left.x, left.y + vh}, rb{right.x, right.y + vh}, mb{mid.x, mid.y + vh};
        auto shade = [&](float m) -> ui2d::RGBA { return {U8(ar*m), U8(ag*m), U8(ab*m), U8(op)}; };
        auto quad = [&](ui2d::FPoint a, ui2d::FPoint b, ui2d::FPoint c2, ui2d::FPoint d, ui2d::RGBA col) {
            ui2d::Vertex v[4] = { {a, col, {0, 0}}, {b, col, {0, 0}}, {c2, col, {0, 0}}, {d, col, {0, 0}} };
            int idx[6] = {0, 1, 2, 0, 2, 3};
            r->Geometry(nullptr, (const ui2d::Vertex*)v, 4, idx, 6);
        };
        quad(top, right, mid, left, shade(1.00f));   // top face (brightest)
        quad(left, mid, mb, lb,     shade(0.52f));   // left face
        quad(mid, right, rb, mb,    shade(0.76f));   // right face
        // Bright rim light along the two top edges.
        r->SetDrawColor(U8(ar + (1 - ar) * 0.6f), U8(ag + (1 - ag) * 0.6f), U8(ab + (1 - ab) * 0.6f), U8(0.9f * op));
        r->DrawLineF(top.x, top.y, left.x, left.y);
        r->DrawLineF(top.x, top.y, right.x, right.y);
    }

    // ---- Rounded progress bar ---------------------------------------------
    // The caps sit inside [barX, barX + barW], so the filled width is exactly the progress.
    if (style.showProgress || failed) {
        const float bx = L.barX, bw = L.barW, by = L.barY, bh = L.barH, brad = bh * 0.5f;
        auto bar = [&](float x0, float x1, float cr, float cg, float cb, float a) {
            if (x1 - x0 < bh) { if (x1 <= x0) return; const float m = (x0 + x1) * 0.5f; x0 = m - brad; x1 = m + brad; }
            r->SetDrawColor(U8(cr), U8(cg), U8(cb), U8(a * op));
            ui2d::FRect rc{x0 + brad, by, x1 - x0 - bh, bh};
            if (rc.w > 0.0f) r->FillRectF((const ui2d::FRect*)&rc);
            disc(x0 + brad, by + brad, brad, cr, cg, cb, a);   // rounded caps
            disc(x1 - brad, by + brad, brad, cr, cg, cb, a);
        };
        bar(bx, bx + bw, 0.21f, 0.21f, 0.235f, 1.0f);    // track
        const float fr = failed ? er : ar, fg = failed ? eg : ag, fb = failed ? eb : ab;
        if (state && state->progress >= 0.0f) {
            // Determinate: the fill, and a soft shine sliding over it so a long step still looks alive.
            const float fx1 = bx + L.fillW;
            if (L.fillW > 0.0f) {
                bar(bx, fx1, fr, fg, fb, 1.0f);
                if (!failed && L.fillW > 2.0f * bh) {
                    const float band = 70.0f;
                    const float t = (float)std::fmod(tms * 0.00045f, 1.0f);
                    const float hc = bx - band + t * (L.fillW + 2.0f * band);   // the band's centre
                    auto shine = [&](float x0, float x1, float a0, float a1) {   // a horizontal ramp, cut to the fill
                        const float c0 = x0 < bx + brad ? bx + brad : x0, c1 = x1 > fx1 - brad ? fx1 - brad : x1;
                        if (c1 <= c0) return;
                        auto at = [&](float x) { return a0 + (a1 - a0) * ((x - x0) / (x1 - x0)); };
                        const ui2d::RGBA ca{255, 255, 255, U8(at(c0) * op)}, cb{255, 255, 255, U8(at(c1) * op)};
                        ui2d::Vertex v[4] = { {{c0, by}, ca, {0, 0}}, {{c1, by}, cb, {0, 0}},
                                            {{c1, by + bh}, cb, {0, 0}}, {{c0, by + bh}, ca, {0, 0}} };
                        int idx[6] = {0, 1, 2, 0, 2, 3};
                        r->Geometry(nullptr, (const ui2d::Vertex*)v, 4, idx, 6);
                    };
                    shine(hc - band * 0.5f, hc, 0.0f, 0.38f);
                    shine(hc, hc + band * 0.5f, 0.38f, 0.0f);
                }
            }
        } else {
            // Indeterminate: a segment sweeping across (a step that cannot say how far it is).
            const float segw = bw * 0.32f;
            float head = (float)std::fmod(tms * 0.00065f, 1.0f);   // 0..1
            float c0 = bx - segw + head * (bw + segw);             // segment slides across
            float x0 = c0 < bx ? bx : c0;
            float x1 = (c0 + segw) > (bx + bw) ? (bx + bw) : (c0 + segw);
            bar(x0, x1, fr, fg, fb, 1.0f);                         // accent sweep
        }
    }

    // ---- Text: title, status line (+ percentage), failure note, footer ----
    if (L.text) {
        okay::TtfFont* font = style.font ? style.font : okay::BuiltinUIFont();
        if (font && font->Valid()) {
            auto px = [](float pixels) { return pixels / 8.0f; };   // DrawTtfText sizes text in 8x8-font pixels
            auto width = [&](const std::string& s, float pixels) { return ui2d::UIMeasureText(font, s, px(pixels)); };
            auto fit = [&](const std::string& s, float pixels, float maxW) {
                return okaypanels::HierEllipsize(s, maxW, [&](const std::string& t) { return width(t, pixels); });
            };
            auto text = [&](const std::string& s, float x, float y, float pixels, float cr, float cg, float cb, float a) {
                if (s.empty()) return;
                ui2d::DrawTtfText(*r, font, s, x, y, px(pixels), ui2d::RGBA{U8(cr), U8(cg), U8(cb), U8(a * op)}, 0.0f, 0.0f);
            };
            const float maxW = (float)w - 48.0f;
            if (!state->title.empty()) {
                const std::string t = fit(state->title, L.titlePx, maxW);
                text(t, std::floor(cx - width(t, L.titlePx) * 0.5f), L.titleY, L.titlePx, 0.93f, 0.93f, 0.95f, 1.0f);
            }
            std::string detail = state->detail;
            if (detail.empty() && !failed && state->progress >= 0.0f)
                detail = std::to_string((int)std::floor(SplashClamp01(state->progress) * 100.0f + 0.5f)) + "%";
            const float detailW = detail.empty() ? 0.0f : width(detail, L.statusPx);
            if (!state->status.empty()) {
                const std::string s = fit(state->status, L.statusPx, L.barW - detailW - (detail.empty() ? 0.0f : 12.0f));
                if (failed) text(s, L.barX, L.statusY, L.statusPx, er, eg + 0.12f, eb + 0.12f, 1.0f);
                else        text(s, L.barX, L.statusY, L.statusPx, 0.68f, 0.69f, 0.73f, 1.0f);
            }
            if (!detail.empty())
                text(detail, std::floor(L.barX + L.barW - detailW), L.statusY, L.statusPx,
                     ar + (1 - ar) * 0.45f, ag + (1 - ag) * 0.45f, ab + (1 - ab) * 0.45f, 1.0f);
            if (failed && !state->failNote.empty())
                text(fit(state->failNote, L.statusPx, L.barW), L.barX, L.noteY, L.statusPx, 0.55f, 0.56f, 0.60f, 1.0f);
            if (!state->footer.empty()) {
                const std::string f = fit(state->footer, L.footerPx, maxW);
                text(f, std::floor(cx - width(f, L.footerPx) * 0.5f), L.footerY, L.footerPx, 0.42f, 0.43f, 0.47f, 1.0f);
            }
        }
    }

    if (!state || state->present) r->Present();
}

// Backward-compatible helper: draw the splash with just an accent colour.
inline void DrawSplash(okay::platform::Painter* r, int w, int h, float ar, float ag, float ab) {
    SplashStyle s; s.ar = ar; s.ag = ag; s.ab = ab;
    DrawSplashStyled(r, w, h, s);
}

// A host's start-up as named steps with weights (how long each roughly takes), turned into the progress value
// and the status line. Begin(name) starts a step: every step before it counts as done, so the bar shows the
// work finished so far while the step runs. The bar never goes back: beginning an earlier step again keeps the
// progress where it is. An unknown name is added at the end.
class SplashSteps {
public:
    void Add(const std::string& name, float weight = 1.0f) {
        m_steps.push_back({name, weight > 0.0f ? weight : 0.0f});
    }
    void Begin(const std::string& name, const std::string& status = std::string()) {
        int i = Find(name);
        if (i < 0) { Add(name); i = (int)m_steps.size() - 1; }
        if (i > m_cur) m_cur = i;
        m_status = status.empty() ? name : status;
    }
    /// Everything is done (the bar is full).
    void Finish(const std::string& status = "Ready") { m_cur = (int)m_steps.size(); m_status = status; }
    /// The step running now failed: `message` is what the splash says (plain words).
    void Fail(const std::string& message) { m_failed = true; m_status = message; }
    float Fraction() const {
        float total = 0.0f, done = 0.0f;
        for (int i = 0; i < (int)m_steps.size(); ++i) { total += m_steps[(std::size_t)i].weight; if (i < m_cur) done += m_steps[(std::size_t)i].weight; }
        if (m_cur >= (int)m_steps.size()) return 1.0f;
        return total > 0.0f ? done / total : 0.0f;
    }
    int  Index() const { return m_cur; }
    int  Count() const { return (int)m_steps.size(); }
    bool Failed() const { return m_failed; }
    const std::string& Status() const { return m_status; }
    /// Put the progress, the status and the failure into `s`.
    void Apply(SplashProgress& s) const { s.progress = Fraction(); s.status = m_status; s.failed = m_failed; }

private:
    struct Step { std::string name; float weight = 1.0f; };
    int Find(const std::string& name) const {
        for (int i = 0; i < (int)m_steps.size(); ++i) if (m_steps[(std::size_t)i].name == name) return i;
        return -1;
    }
    std::vector<Step> m_steps;
    int m_cur = -1;
    bool m_failed = false;
    std::string m_status;
};

// The end of the splash, drawn over the host's first frames: fully up while the host still holds it (a start-up
// step still running, at most `maxHoldMs`), up for `errorHoldMs` after a step failed so the message can be read,
// then a `fadeMs` fade into the host's own frame (no black frame in between). Update once per frame.
class SplashFinish {
public:
    double fadeMs = 280.0, errorHoldMs = 1800.0, maxHoldMs = 15000.0;
    /// The splash's opacity for this frame (1 = fully up, 0 = gone; Done() from then on).
    float Update(double nowMs, bool holding, bool failed) {
        if (m_done) return 0.0f;
        if (m_t0 < 0.0) m_t0 = nowMs;
        if (failed && m_failAt < 0.0) m_failAt = nowMs;
        if (m_fadeAt < 0.0) {
            const bool hold = m_failAt >= 0.0 ? (nowMs - m_failAt < errorHoldMs) : (holding && nowMs - m_t0 < maxHoldMs);
            if (hold) return 1.0f;
            m_fadeAt = nowMs;
        }
        const float t = fadeMs > 0.0 ? (float)((nowMs - m_fadeAt) / fadeMs) : 1.0f;
        if (t >= 1.0f) { m_done = true; return 0.0f; }
        const float k = 1.0f - (t < 0.0f ? 0.0f : t);
        return k * k * (3.0f - 2.0f * k);   // smoothstep: eases out of full and into the frame
    }
    bool Done() const { return m_done; }
    bool Fading() const { return m_fadeAt >= 0.0 && !m_done; }

private:
    double m_t0 = -1.0, m_failAt = -1.0, m_fadeAt = -1.0;
    bool m_done = false;
};

} // namespace okay
