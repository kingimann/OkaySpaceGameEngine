#pragma once
#include "okay/Math/Vec2.hpp"

namespace okay {

/// Where a UI element's position is measured from. `TopLeft` is the classic
/// pixels-from-the-top-left; the others anchor to a screen edge, corner, or the
/// center so layouts adapt to the window size. `position` is then an *offset*
/// from that anchor — e.g. `TopRight` with offset `(-20, 20)` keeps an element
/// 20px in from the top-right corner whatever the resolution.
/// The nine fixed points are followed by three *stretch* anchors (Unity's
/// stretch presets). A stretch widget fills a canvas axis and reinterprets its
/// `position`/`size` as MARGINS (Unity's offsetMin/offsetMax) rather than an
/// offset + extent — see ResolveAnchorRect. New values are appended so the
/// serialized anchor index of existing scenes is unchanged.
enum class UIAnchor {
    TopLeft, TopCenter, TopRight,
    MiddleLeft, Center, MiddleRight,
    BottomLeft, BottomCenter, BottomRight,
    StretchHorizontal,   // fill width:  position=(left,top), size=(right margin, height)
    StretchVertical,     // fill height: position=(left,top), size=(width, bottom margin)
    StretchFull          // fill both:   position=(left,top) margins, size=(right,bottom) margins
};

/// Does this anchor stretch a widget across a canvas axis (rather than pin it at a
/// fixed point)? Stretch anchors size themselves from the canvas, so tools treat
/// their `position`/`size` fields as margins.
inline bool AnchorIsStretch(UIAnchor a) {
    return a == UIAnchor::StretchHorizontal ||
           a == UIAnchor::StretchVertical   ||
           a == UIAnchor::StretchFull;
}

/// Current UI canvas (render-target) size in pixels. The runtime sets this each
/// frame so anchored widgets and their hit-tests agree on where things sit.
struct UICanvas {
    static float& Width()  { static float w = 1280.0f; return w; }
    static float& Height() { static float h = 720.0f;  return h; }
    static void Set(float w, float h) { Width() = w; Height() = h; }
};

/// Extra scale applied to Constant-Pixel-Size UI so the editor's Game view can preview
/// the game at a chosen resolution (a fixed-px HUD looks smaller at 4K than at 720p,
/// just like Unity). 1.0 everywhere except the editor Game view when a fixed resolution
/// is selected, where it's (on-screen height / target height). ScaleWithScreenSize
/// canvases are unaffected — they already scale with the reference resolution.
inline float& UIResolutionScale() { static float s = 1.0f; return s; }

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

/// Resolve a widget to BOTH its absolute top-left pixel and its effective size on a
/// canvas of `cw` x `ch`. For the nine fixed anchors this just forwards to
/// ResolveAnchor and passes `size` through. For the three stretch anchors it
/// reinterprets `position`/`size` as margins (Unity's offsetMin/offsetMax):
///   - StretchHorizontal: width = cw - left - right (position.x=left, size.x=right);
///                        height stays `size.y`, top stays `position.y`.
///   - StretchVertical:   height = ch - top - bottom (position.y=top, size.y=bottom);
///                        width stays `size.x`, left stays `position.x`.
///   - StretchFull:       fills both axes, all four values are margins.
/// This is the single source of truth for stretch layout, shared by rendering,
/// hit-testing and the editor.
inline void ResolveAnchorRect(UIAnchor a, const Vec2& position, const Vec2& size,
                              float cw, float ch, Vec2& origin, Vec2& outSize) {
    switch (a) {
        case UIAnchor::StretchHorizontal:
            origin  = { position.x, position.y };
            outSize = { cw - position.x - size.x, size.y };
            break;
        case UIAnchor::StretchVertical:
            origin  = { position.x, position.y };
            outSize = { size.x, ch - position.y - size.y };
            break;
        case UIAnchor::StretchFull:
            origin  = { position.x, position.y };
            outSize = { cw - position.x - size.x, ch - position.y - size.y };
            break;
        default:
            outSize = size;
            origin  = ResolveAnchor(a, position, size, cw, ch);
            break;
    }
    if (outSize.x < 0.0f) outSize.x = 0.0f;   // don't invert when margins overlap
    if (outSize.y < 0.0f) outSize.y = 0.0f;
}

} // namespace okay
