#pragma once
#include "okay/Math/Vec2.hpp"

namespace okay {

/// Where a UI element's position is measured from. `TopLeft` is the classic
/// pixels-from-the-top-left; the others anchor to a screen edge, corner, or the
/// center so layouts adapt to the window size. `position` is then an *offset*
/// from that anchor — e.g. `TopRight` with offset `(-20, 20)` keeps an element
/// 20px in from the top-right corner whatever the resolution.
enum class UIAnchor {
    TopLeft, TopCenter, TopRight,
    MiddleLeft, Center, MiddleRight,
    BottomLeft, BottomCenter, BottomRight
};

/// Current UI canvas (render-target) size in pixels. The runtime sets this each
/// frame so anchored widgets and their hit-tests agree on where things sit.
struct UICanvas {
    static float& Width()  { static float w = 1280.0f; return w; }
    static float& Height() { static float h = 720.0f;  return h; }
    static void Set(float w, float h) { Width() = w; Height() = h; }
};

/// Resolve an anchored element to its absolute top-left pixel, given its offset
/// `position`, its `size`, and the canvas dimensions.
inline Vec2 ResolveAnchor(UIAnchor a, const Vec2& position, const Vec2& size,
                          float canvasW, float canvasH) {
    float x = position.x, y = position.y;
    switch (a) {
        case UIAnchor::TopLeft:                                                                  break;
        case UIAnchor::TopCenter:    x += (canvasW - size.x) * 0.5f;                             break;
        case UIAnchor::TopRight:     x += canvasW - size.x;                                      break;
        case UIAnchor::MiddleLeft:                                  y += (canvasH - size.y) * 0.5f; break;
        case UIAnchor::Center:       x += (canvasW - size.x) * 0.5f; y += (canvasH - size.y) * 0.5f; break;
        case UIAnchor::MiddleRight:  x += canvasW - size.x;          y += (canvasH - size.y) * 0.5f; break;
        case UIAnchor::BottomLeft:                                  y += canvasH - size.y;       break;
        case UIAnchor::BottomCenter: x += (canvasW - size.x) * 0.5f; y += canvasH - size.y;      break;
        case UIAnchor::BottomRight:  x += canvasW - size.x;          y += canvasH - size.y;      break;
    }
    return {x, y};
}

/// Convenience overload using the current UICanvas size (for hit-testing).
inline Vec2 ResolveAnchor(UIAnchor a, const Vec2& position, const Vec2& size) {
    return ResolveAnchor(a, position, size, UICanvas::Width(), UICanvas::Height());
}

} // namespace okay
