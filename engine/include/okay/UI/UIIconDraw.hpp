#pragma once
// Draws a vector icon (okay/UI/UIIcons.hpp) through a Painter: one filled rectangle per run of equal coverage, cached per name and size.
#include "okay/UI/UIIcons.hpp"
#include "okay/UI/UIPainter.hpp"
#include <map>
#include <string>
#include <vector>

namespace okay { namespace ui2d {

/// Draw the icon `name` into the box (x, y, size, size), tinted `col` (its alpha scales the picture). false when there is no such icon.
inline bool DrawVectorIcon(Painter& p, const std::string& name, int x, int y, int size, RGBA col) {
    struct Run { int x, y, w; std::uint8_t a; };
    static std::map<std::pair<std::string, int>, std::vector<Run>> cache;   // UI code runs on one thread
    const uilib::IconDef* icon = uilib::FindIcon(name);
    if (!icon || size < 2) return false;
    auto key = std::make_pair(name, size);
    auto it = cache.find(key);
    if (it == cache.end()) {
        std::vector<Run> runs;
        const std::vector<std::uint8_t> px = uilib::RenderIcon(*icon, size);
        for (int yy = 0; yy < size; ++yy) {
            int xx = 0;
            while (xx < size) {
                const std::uint8_t a = (std::uint8_t)((px[(std::size_t)yy * (std::size_t)size + (std::size_t)xx] + 25) / 51 * 51);   // five levels
                int e = xx + 1;
                while (e < size && (std::uint8_t)((px[(std::size_t)yy * (std::size_t)size + (std::size_t)e] + 25) / 51 * 51) == a) ++e;
                if (a) runs.push_back({xx, yy, e - xx, a});
                xx = e;
            }
        }
        it = cache.emplace(key, std::move(runs)).first;
        if (cache.size() > 4096) cache.clear();   // keep the memory small if a game animates the size
    }
    for (const Run& r : it->second) {
        const int a = (int)col.a * (int)r.a / 255;
        if (a <= 0) continue;
        p.SetDrawColor(col.r, col.g, col.b, (std::uint8_t)a);
        Rect rc{x + r.x, y + r.y, r.w, 1};
        p.FillRect(&rc);
    }
    return true;
}

}} // namespace okay::ui2d
