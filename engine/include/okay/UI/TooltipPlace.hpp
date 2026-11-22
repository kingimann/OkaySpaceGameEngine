#pragma once
#include <algorithm>

namespace okay {

struct TooltipBox { float x, y, w, h; };

/// The few numbers that make one UI's tooltips look like its own. Game UI uses the defaults; OkayUI (the editor and
/// tools toolkit) passes {14, 10, 2, false}.
struct TooltipPlaceStyle {
    float offset = 14.0f;    ///< the box's corner sits this far right of and below the pointer
    float flipGap = 8.0f;    ///< after a flip, the gap between the pointer and the box's far edge
    float pad = 4.0f;        ///< the box stays this far inside the screen
    bool shrink = true;      ///< a box larger than the screen is cut down to fit (off: it keeps its size)
};

/// Keep a tooltip box on screen. `ax, ay` is the pointer. The box prefers the
/// lower right of the pointer, flips to the other side when that would clip,
/// then clamps so the whole box stays inside [left, right) x [top, bottom).
/// Game UI and OkayUI both place their tooltips with this.
inline TooltipBox PlaceTooltip(float ax, float ay, float bw, float bh,
                              float left, float top, float right, float bottom,
                              const TooltipPlaceStyle& style = {}) {
    const float pad = style.pad;
    if (style.shrink) {
        const float limitW = std::max(8.0f, (right - left) - pad * 2.0f);
        const float limitH = std::max(8.0f, (bottom - top) - pad * 2.0f);
        if (bw > limitW) bw = limitW;
        if (bh > limitH) bh = limitH;
    }
    float x = ax + style.offset;
    float y = ay + style.offset;
    if (x + bw > right - pad) x = ax - bw - style.flipGap;
    if (y + bh > bottom - pad) y = ay - bh - style.flipGap;
    if (x < left + pad) x = left + pad;
    if (y < top + pad) y = top + pad;
    if (x + bw > right - pad) x = std::max(left + pad, right - pad - bw);
    if (y + bh > bottom - pad) y = std::max(top + pad, bottom - pad - bh);
    return {x, y, bw, bh};
}

} // namespace okay
