#pragma once
#include "okay/Components/LoadingScreen.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace okay {

/// One thing to draw for a loading screen, in screen pixels. The player (SDL) and the
/// editor's Play view (ImGui) both draw the same list, so every layout looks the same
/// in both and the arrangement itself is unit-tested.
struct LoadingDrawCmd {
    enum class Kind { Rect, Gradient, Outline, Image, Text, Circle, Line, Arc };
    Kind  kind = Kind::Rect;
    /// Rect / Gradient / Outline / Image: the box. Text: (x, y) is the top of the line
    /// at the alignment point and h the text height. Circle: centre (x, y), radius w.
    /// Line: from (x, y) to (w, h). Arc: centre (x, y), radius w, from angle u0 to
    /// u1 (radians, 0 = right, increasing clockwise on screen).
    float x = 0, y = 0, w = 0, h = 0;
    float radius = 0;                       ///< corner radius (rects) or line / arc width
    Color color = Color::White;             ///< fill (Gradient: top)
    Color color2 = Color::White;            ///< Gradient: bottom
    float alpha = 1.0f;                     ///< opacity, the screen's fade included
    int   align = 0;                        ///< Text: 0 left, 1 centre, 2 right of x
    std::string text;                       ///< Text: the string. Image: the file
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;   ///< Image: source region (0..1)
};

namespace loading_detail {
inline Color Lerp(const Color& a, const Color& b, float t) {
    return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}
}

/// Lay out `ls` on a W x H screen. Pure (no drawing), so it can be tested.
/// `previewTime` >= 0 lays it out as if it had been showing that long (looping the
/// bar, cycling the tips, fully opaque): the Inspector's preview, outside Play.
inline std::vector<LoadingDrawCmd> BuildLoadingScreen(const LoadingScreen& ls, float W, float H,
                                                      float previewTime = -1.0f) {
    using K = LoadingDrawCmd::Kind;
    std::vector<LoadingDrawCmd> out;
    if (W <= 0 || H <= 0) return out;
    const bool preview = previewTime >= 0.0f;
    const float A = preview ? 1.0f : Mathf::Clamp01(ls.Alpha());
    const float t = preview ? previewTime : ls.Elapsed();
    const float p = !preview ? ls.Progress()
                  : ls.duration > 1e-4f ? Mathf::Clamp01(std::fmod(previewTime, ls.duration + 0.6f) / ls.duration) : 1.0f;
    const int layout = std::clamp(ls.layout, 0, (int)LoadingScreen::Layout::Count - 1);
    using L = LoadingScreen::Layout;

    auto rect = [&](float x, float y, float w, float h, const Color& c, float rad = 0, float a = 1) {
        LoadingDrawCmd d; d.kind = K::Rect; d.x = x; d.y = y; d.w = w; d.h = h; d.color = c; d.radius = rad; d.alpha = A * a;
        out.push_back(d);
    };
    auto outline = [&](float x, float y, float w, float h, const Color& c, float rad = 0) {
        LoadingDrawCmd d; d.kind = K::Outline; d.x = x; d.y = y; d.w = w; d.h = h; d.color = c; d.radius = rad; d.alpha = A;
        out.push_back(d);
    };
    auto text = [&](const std::string& s, float x, float y, float h, const Color& c, int align, float a = 1) {
        if (s.empty()) return;
        LoadingDrawCmd d; d.kind = K::Text; d.text = s; d.x = x; d.y = y; d.h = h; d.color = c; d.align = align; d.alpha = A * a;
        out.push_back(d);
    };
    auto circle = [&](float cx, float cy, float r, const Color& c, float a = 1) {
        LoadingDrawCmd d; d.kind = K::Circle; d.x = cx; d.y = cy; d.w = r; d.color = c; d.alpha = A * a;
        out.push_back(d);
    };
    auto line = [&](float x0, float y0, float x1, float y1, float width, const Color& c, float a = 1) {
        LoadingDrawCmd d; d.kind = K::Line; d.x = x0; d.y = y0; d.w = x1; d.h = y1; d.radius = width; d.color = c; d.alpha = A * a;
        out.push_back(d);
    };
    // An arc from angle a0 to a1 (radians, 0 = right, clockwise on screen).
    auto arc = [&](float cx, float cy, float r, float a0, float a1, float width, const Color& c, float a = 1) {
        LoadingDrawCmd d; d.kind = K::Arc; d.x = cx; d.y = cy; d.w = r; d.radius = width;
        d.u0 = a0; d.u1 = a1; d.color = c; d.alpha = A * a;
        out.push_back(d);
    };
    (void)line;

    // ---- Background ----
    if (!ls.backgroundImage.empty()) {
        LoadingDrawCmd d; d.kind = K::Image; d.text = ls.backgroundImage; d.w = W; d.h = H; d.alpha = A;
        if (ls.kenBurns) {   // a slow zoom towards the centre over the screen's life
            const float z = 1.0f + 0.08f * Mathf::Clamp01(t / std::max(ls.duration, 0.1f));
            const float m = (1.0f - 1.0f / z) * 0.5f;
            d.u0 = d.v0 = m; d.u1 = d.v1 = 1.0f - m;
        }
        out.push_back(d);
    } else if (ls.gradientBackground) {
        LoadingDrawCmd d; d.kind = K::Gradient; d.w = W; d.h = H; d.color = ls.backgroundColor; d.color2 = ls.backgroundColor2; d.alpha = A;
        out.push_back(d);
    } else {
        rect(0, 0, W, H, ls.backgroundColor);
    }

    // Text heights in pixels (the classic layout keeps its original sizes).
    const float titleH = std::max(16.0f, H * 0.048f * ls.titleScale);
    const float tipH   = std::max(8.0f, H * 0.0256f * ls.tipScale);
    const float subH   = std::max(8.0f, H * 0.024f * ls.tipScale);
    const float pctH   = std::max(8.0f, H * 0.024f);
    const std::string pct = std::to_string((int)(Mathf::Clamp01(p) * 100.0f + 0.5f)) + "%";
    const std::string& tip = !preview || ls.tips.empty() ? ls.CurrentTip()
        : ls.tips[(std::size_t)(previewTime / std::max(ls.tipInterval > 0 ? ls.tipInterval : 3.0f, 0.5f)) % ls.tips.size()];

    auto bar = [&](float x, float y, float w, float h, float rad) {
        if (!ls.showBar || w <= 0 || h <= 0) return;
        using B = LoadingScreen::BarStyle;
        switch ((B)std::clamp(ls.barStyle, 0, (int)B::Count - 1)) {
        case B::Segmented: {
            const int n = 20; const float gap = std::max(1.0f, w * 0.006f);
            const float sw = (w - gap * (n - 1)) / n;
            const int lit = (int)(p * n + 0.5f);
            for (int i = 0; i < n; ++i)
                rect(x + i * (sw + gap), y, sw, h, i < lit ? ls.barFill : ls.barBackground, std::min(rad, sw * 0.5f));
            break;
        }
        case B::Indeterminate: {   // a chunk sliding back and forth: for loads with no known length
            rect(x, y, w, h, ls.barBackground, rad);
            const float cw = w * 0.3f;
            const float ph = std::fmod(t * 0.8f, 2.0f), u = ph < 1.0f ? ph : 2.0f - ph;
            const float s = u * u * (3.0f - 2.0f * u);
            rect(x + (w - cw) * s, y, cw, h, ls.barFill, rad);
            break;
        }
        default: {   // Solid / Shimmer
            rect(x, y, w, h, ls.barBackground, rad);
            const float fw = w * p;
            if (fw > 0.5f) rect(x, y, fw, h, ls.barFill, rad);
            if (ls.barStyle == (int)B::Shimmer && fw > 4.0f) {
                const float bw = std::min(fw, w * 0.12f);
                const float sx = x + std::fmod(t * 0.7f, 1.0f) * (fw - bw);
                rect(sx, y, bw, h, loading_detail::Lerp(ls.barFill, Color::White, 0.55f), rad, 0.55f);
            }
            break;
        }
        }
        if (ls.barBorder) outline(x, y, w, h, ls.barBorderColor, rad);
    };
    auto spinner = [&](float cx, float cy, float r) {
        if (!ls.showSpinner || r <= 0) return;
        using S = LoadingScreen::SpinnerStyle;
        switch ((S)std::clamp(ls.spinnerStyle, 0, (int)S::Count - 1)) {
        case S::Arc: {
            const float wdt = std::max(2.0f, r * 0.2f);
            arc(cx, cy, r, 0.0f, 6.2831853f, wdt, ls.spinnerColor, 0.18f);
            const float a0 = t * ls.spinnerSpeed * 0.9f;
            arc(cx, cy, r, a0, a0 + 4.2f, wdt, ls.spinnerColor);
            break;
        }
        case S::Bars: {   // an equaliser: five bars bobbing
            const float bw = r * 0.28f, gap = r * 0.14f, total = bw * 5 + gap * 4;
            for (int i = 0; i < 5; ++i) {
                const float k = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(t * ls.spinnerSpeed * 0.9f - i * 0.8f));
                const float bh = r * 2.0f * k;
                rect(cx - total * 0.5f + i * (bw + gap), cy + r - bh, bw, bh, ls.spinnerColor, bw * 0.4f);
            }
            break;
        }
        case S::Pulse: {   // a dot with a ring expanding out of it
            const float u = std::fmod(t * ls.spinnerSpeed * 0.15f, 1.0f);
            arc(cx, cy, r * (0.3f + 0.7f * u), 0.0f, 6.2831853f, std::max(1.5f, r * 0.08f), ls.spinnerColor, 1.0f - u);
            circle(cx, cy, r * 0.28f, ls.spinnerColor);
            break;
        }
        default: {   // Dots: a chasing ring of eight
            const float ds = std::max(2.0f, ls.spinnerDotSize * (H / 720.0f));
            const float phase = t * ls.spinnerSpeed;
            for (int i = 0; i < 8; ++i) {
                const float ang = (float)i / 8 * 6.2831853f;
                const float bright = 0.25f + 0.75f * (0.5f + 0.5f * std::cos(ang - phase));
                circle(cx + std::cos(ang) * r, cy + std::sin(ang) * r, ds * 0.5f, ls.spinnerColor, bright);
            }
            break;
        }
        }
    };
    auto logo = [&](float cx, float cy, float size) {
        if (ls.logoImage.empty() || size <= 0) return;
        if (ls.logoPulse) size *= 1.0f + 0.035f * std::sin(t * 2.4f);
        LoadingDrawCmd d; d.kind = K::Image; d.text = ls.logoImage;
        d.x = cx - size * 0.5f; d.y = cy - size * 0.5f; d.w = size; d.h = size; d.alpha = A;
        out.push_back(d);
    };
    auto title = [&](float x, float y, float h, int align) {
        if (ls.showTitle) text(ls.title, x, y, h, ls.titleColor, align);
    };

    switch ((L)layout) {
    case L::Minimal: {
        const float cy = H * 0.46f, r = H * 0.032f;
        logo(W * 0.5f, H * 0.30f, H * ls.logoSize * 0.6f);
        spinner(W * 0.5f, cy, r);
        const float th = std::max(12.0f, H * 0.028f * ls.titleScale);
        title(W * 0.5f, cy + r + H * 0.035f, th, 1);
        text(ls.subtitle, W * 0.5f, cy + r + H * 0.035f + th * 1.5f, subH, ls.tipColor, 1);
        const float bw = W * 0.2f, bh = std::max(3.0f, H * 0.005f), by = H * 0.64f;
        bar((W - bw) * 0.5f, by, bw, bh, bh * 0.5f);
        if (ls.showPercent) text(pct, W * 0.5f, by + bh + H * 0.012f, pctH * 0.85f, ls.textColor, 1);
        text(tip, W * 0.5f, H * 0.9f, tipH, ls.tipColor, 1);
        break;
    }
    case L::Cinematic: {
        const float lb = H * 0.12f;
        rect(0, 0, W, lb, Color::Black);
        rect(0, H - lb, W, lb, Color::Black);
        logo(W * 0.5f, lb * 0.5f, lb * 0.8f);
        const float mx = W * 0.06f;
        const float th = titleH * 1.1f, ty = H - lb - th - H * 0.035f;
        title(mx, ty, th, 0);
        text(ls.subtitle, mx, ty - subH - H * 0.014f, subH, ls.tipColor, 0);
        text(tip, mx, H - lb + (lb - tipH) * 0.5f, tipH, ls.tipColor, 0);
        const float bh = std::max(2.0f, H * 0.004f);
        bar(0, H - lb - bh, W, bh, 0);
        const float r = H * 0.022f, scx = W - mx - r, scy = H - lb * 0.5f;
        spinner(scx, scy, r);
        if (ls.showPercent) text(pct, scx - r - H * 0.02f, scy - pctH * 0.5f, pctH, ls.textColor, 2);
        break;
    }
    case L::Splash: {
        const float cy = H * 0.42f, size = H * ls.logoSize;
        float below = cy;
        if (!ls.logoImage.empty()) { logo(W * 0.5f, cy, size); below = cy + size * 0.5f; }
        const float th = ls.logoImage.empty() ? titleH * 1.25f : std::max(14.0f, H * 0.03f * ls.titleScale);
        const float ty = ls.logoImage.empty() ? cy - th * 0.5f : below + H * 0.03f;
        title(W * 0.5f, ty, th, 1);
        text(ls.subtitle, W * 0.5f, ty + th * 1.4f, subH, ls.tipColor, 1);
        const float bw = W * 0.18f, bh = std::max(3.0f, H * 0.005f), by = ty + th * 1.4f + subH * 2.2f;
        bar((W - bw) * 0.5f, by, bw, bh, bh * 0.5f);
        if (ls.showPercent) text(pct, W * 0.5f, by + bh + H * 0.012f, pctH * 0.85f, ls.textColor, 1);
        text(tip, W * 0.5f, H * 0.9f, tipH, ls.tipColor, 1);
        const float r = H * 0.022f;
        spinner(W - W * 0.04f - r, H - H * 0.06f - r, r);
        break;
    }
    case L::Card: {
        const float cw = std::clamp(W * 0.42f, std::min(W - 32.0f, H * 0.6f), W - 32.0f), ch = H * 0.30f;
        const float x0 = (W - cw) * 0.5f, y0 = (H - ch) * 0.5f, rad = H * 0.02f, pad = H * 0.04f;
        logo(W * 0.5f, y0 - H * 0.1f, H * ls.logoSize * 0.5f);
        rect(x0 + 2, y0 + H * 0.012f, cw, ch, Color::Black, rad * 1.4f, 0.35f);   // soft shadow
        rect(x0, y0, cw, ch, ls.cardColor, rad);
        const float th = std::max(14.0f, H * 0.04f * ls.titleScale);
        title(x0 + pad, y0 + pad, th, 0);
        text(ls.subtitle, x0 + pad, y0 + pad + th * 1.35f, subH, ls.tipColor, 0);
        const float r = H * 0.02f;
        spinner(x0 + cw - pad - r, y0 + pad + r, r);
        // The percent sits at the end of the bar's row, so a long tip can't run into it.
        const float bh = std::max(4.0f, H * 0.01f), by = y0 + ch - pad - bh;
        const float pctW = ls.showPercent ? pctH * 3.2f : 0.0f;
        bar(x0 + pad, by, cw - pad * 2 - pctW, bh, bh * 0.5f);
        if (ls.showPercent) text(pct, x0 + cw - pad, by + (bh - pctH) * 0.5f, pctH, ls.textColor, 2);
        text(tip, x0 + pad, by - H * 0.03f - tipH, tipH, ls.tipColor, 0);
        break;
    }
    case L::Corner: {
        const float mx = W * 0.05f, th = std::max(14.0f, H * 0.04f * ls.titleScale), ty = H * 0.82f;
        logo(mx + H * ls.logoSize * 0.25f, H * 0.1f, H * ls.logoSize * 0.5f);
        title(mx, ty, th, 0);
        text(ls.subtitle, mx, ty - subH - H * 0.012f, subH, ls.tipColor, 0);
        text(tip, mx, ty + th + H * 0.018f, tipH, ls.tipColor, 0);
        const float r = H * 0.028f, scx = W - mx - r, scy = ty + th * 0.6f;
        spinner(scx, scy, r);
        if (ls.showPercent) text(pct, scx - r - H * 0.025f, scy - pctH * 0.5f, pctH, ls.textColor, 2);
        const float bh = std::max(3.0f, H * 0.006f);
        bar(0, H - bh, W, bh, 0);
        break;
    }
    case L::Ring: {
        const float cx = W * 0.5f, cy = H * 0.48f, r = H * 0.1f, wdt = std::max(3.0f, H * 0.012f);
        logo(cx, H * 0.14f, H * ls.logoSize * 0.5f);
        if (ls.showBar) {
            arc(cx, cy, r, 0.0f, 6.2831853f, wdt, ls.barBackground);
            if (ls.barStyle == (int)LoadingScreen::BarStyle::Indeterminate) {
                const float a0 = t * 3.0f;
                arc(cx, cy, r, a0, a0 + 1.6f, wdt, ls.barFill);
            } else if (p > 0.001f) {
                arc(cx, cy, r, -1.5707963f, -1.5707963f + 6.2831853f * p, wdt, ls.barFill);
            }
        }
        if (ls.showPercent) text(pct, cx, cy - H * 0.022f, H * 0.044f, ls.textColor, 1);
        else spinner(cx, cy, r * 0.4f);
        title(cx, cy - r - H * 0.06f - titleH * 0.8f, titleH * 0.8f, 1);
        text(ls.subtitle, cx, cy + r + H * 0.045f, subH, ls.tipColor, 1);
        text(tip, cx, H * 0.88f, tipH, ls.tipColor, 1);
        break;
    }
    default: {   // Classic: everything centred at the positions set in the Inspector
        if (!ls.logoImage.empty()) {
            const float size = H * ls.logoSize;
            logo(W * 0.5f, std::max(size * 0.5f, H * ls.titleY - H * 0.03f - size * 0.5f), size);
        }
        title(W * 0.5f, H * ls.titleY, titleH, 1);
        text(ls.subtitle, W * 0.5f, H * ls.titleY + titleH * 1.4f, subH, ls.tipColor, 1);
        text(tip, W * 0.5f, H * ls.tipY, tipH, ls.tipColor, 1);
        const float bw = W * ls.barWidth, bh = std::max(4.0f, H * ls.barHeight);
        const float bx = (W - bw) * 0.5f, by = H * ls.barY;
        bar(bx, by, bw, bh, ls.barRadius);
        if (ls.showPercent && ls.showBar) text(pct, bx + bw + 8.0f, by + (bh - pctH) * 0.5f, pctH, ls.textColor, 0);
        spinner(W * 0.5f, H * ls.spinnerY, H * ls.spinnerRadius);
        break;
    }
    }
    return out;
}

/// One-click starting points for each layout (the Inspector's layout buttons): sets
/// the layout and the bar / spinner / colours that suit it. Text and images stay.
inline void ApplyLoadingPreset(LoadingScreen& ls, LoadingScreen::Layout layout) {
    using L = LoadingScreen::Layout; using B = LoadingScreen::BarStyle; using S = LoadingScreen::SpinnerStyle;
    const Color accent = Color::FromBytes(88, 140, 255), track = Color::FromBytes(255, 255, 255, 30);
    ls.layout = (int)layout;
    ls.gradientBackground = true;
    ls.backgroundColor = Color::FromBytes(14, 17, 25); ls.backgroundColor2 = Color::FromBytes(24, 31, 48);
    ls.titleColor = Color::White; ls.tipColor = Color::FromBytes(170, 178, 196); ls.textColor = Color::White;
    ls.barFill = accent; ls.barBackground = track; ls.barBorder = false;
    ls.spinnerColor = accent;
    ls.showBar = true; ls.showSpinner = true; ls.showPercent = false; ls.showTitle = true;
    ls.barStyle = (int)B::Solid; ls.spinnerStyle = (int)S::Arc;
    switch (layout) {
    case L::Classic:
        ls.barRadius = 4.0f; ls.showSpinner = false; ls.showPercent = true; ls.barStyle = (int)B::Shimmer; break;
    case L::Minimal:
        ls.gradientBackground = false; ls.backgroundColor = Color::FromBytes(10, 11, 15); break;
    case L::Cinematic:
        ls.backgroundColor = Color::FromBytes(20, 24, 32); ls.backgroundColor2 = Color::FromBytes(8, 9, 12);
        ls.kenBurns = true; ls.showPercent = true; ls.spinnerStyle = (int)S::Dots; ls.spinnerColor = Color::White; break;
    case L::Splash:
        ls.gradientBackground = false; ls.backgroundColor = Color::FromBytes(8, 9, 12);
        ls.spinnerStyle = (int)S::Pulse; ls.showSpinner = false; break;
    case L::Card:
        ls.cardColor = Color::FromBytes(28, 32, 44); ls.showPercent = true; ls.barStyle = (int)B::Shimmer; break;
    case L::Corner:
        ls.spinnerStyle = (int)S::Arc; ls.showPercent = true; ls.kenBurns = true; break;
    case L::Ring:
        ls.showPercent = true; ls.showSpinner = false; break;
    default: break;
    }
}

} // namespace okay
