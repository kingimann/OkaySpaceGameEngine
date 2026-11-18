#pragma once
// Draws a vector icon (okay/UI/UIIcons.hpp) through a Painter: one filled rectangle per run of equal coverage, cached per name and DEVICE size.
#include "okay/UI/UIIcons.hpp"
#include "okay/UI/UIPainter.hpp"
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace okay { namespace ui2d {

/// Draw the icon `name` into the box (x, y, size, size) in logical pixels, tinted `col` (its alpha scales the picture). `deviceScale` is the number
/// of device pixels per logical pixel (a canvas scaled to 2x or 3x, a HiDPI screen, a zoomed view): the picture is made at size x deviceScale pixels
/// and laid on the device pixel grid, so it is rasterised sharp at the size it is shown and never a small picture stretched up. false when there is
/// no such icon.
inline bool DrawVectorIcon(Painter& p, const std::string& name, int x, int y, int size, RGBA col, float deviceScale = 1.0f) {
    struct Run { int x, y, w; std::uint8_t a; };
    static std::map<std::pair<std::string, int>, std::vector<Run>> cache;   // UI code runs on one thread
    const uilib::IconDef* icon = uilib::FindIcon(name);
    const float S = deviceScale > 0.05f ? deviceScale : 1.0f;
    const int px = (int)std::lround((double)size * (double)S);
    if (!icon || px < 2) return false;
    auto key = std::make_pair(name, px);
    auto it = cache.find(key);
    if (it == cache.end()) {
        std::vector<Run> runs;
        const std::vector<std::uint8_t> cov = uilib::RenderIcon(*icon, px);
        for (int yy = 0; yy < px; ++yy) {
            int xx = 0;
            while (xx < px) {
                const std::uint8_t a = cov[(std::size_t)yy * (std::size_t)px + (std::size_t)xx];   // 16 levels of coverage: a smooth edge
                int e = xx + 1;
                while (e < px && cov[(std::size_t)yy * (std::size_t)px + (std::size_t)e] == a) ++e;
                if (a) runs.push_back({xx, yy, e - xx, a});
                xx = e;
            }
        }
        it = cache.emplace(key, std::move(runs)).first;
        if (cache.size() > 4096) cache.clear();   // keep the memory small if a game animates the size
    }
    const bool unit = std::fabs(S - 1.0f) < 1e-4f;
    // the icon's top-left corner on the device pixel grid
    const long dx0 = std::lround((double)x * (double)S), dy0 = std::lround((double)y * (double)S);
    for (const Run& r : it->second) {
        const int a = (int)col.a * (int)r.a / 255;
        if (a <= 0) continue;
        p.SetDrawColor(col.r, col.g, col.b, (std::uint8_t)a);
        if (unit) { Rect rc{(int)dx0 + r.x, (int)dy0 + r.y, r.w, 1}; p.FillRect(&rc); }
        else {
            // a hundredth of a device pixel of bias keeps a software renderer that truncates float rects from losing a pixel to rounding
            FRect rc{((float)(dx0 + r.x) + 0.01f) / S, ((float)(dy0 + r.y) + 0.01f) / S, ((float)r.w + 0.01f) / S, ((float)1 + 0.01f) / S};
            p.FillRectF(&rc);
        }
    }
    return true;
}

}} // namespace okay::ui2d
