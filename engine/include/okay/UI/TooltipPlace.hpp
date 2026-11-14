#pragma once
#include <algorithm>

namespace okay {

struct TooltipBox { float x, y, w, h; };

/// Keep a tooltip box on screen. `ax, ay` is the pointer. The box prefers the
/// lower right of the pointer, flips to the other side when that would clip,
/// then clamps so the whole box stays inside [left, right) x [top, bottom).
inline TooltipBox PlaceTooltip(float ax, float ay, float bw, float bh,
                              float left, float top, float right, float bottom) {
    const float pad = 4.0f;
    const float limitW = std::max(8.0f, (right - left) - pad * 2.0f);
    const float limitH = std::max(8.0f, (bottom - top) - pad * 2.0f);
    if (bw > limitW) bw = limitW;
    if (bh > limitH) bh = limitH;
    float x = ax + 14.0f;
    float y = ay + 14.0f;
    if (x + bw > right - pad) x = ax - bw - 8.0f;
    if (y + bh > bottom - pad) y = ay - bh - 8.0f;
    if (x < left + pad) x = left + pad;
    if (y < top + pad) y = top + pad;
    if (x + bw > right - pad) x = std::max(left + pad, right - pad - bw);
    if (y + bh > bottom - pad) y = std::max(top + pad, bottom - pad - bh);
    return {x, y, bw, bh};
}

} // namespace okay
