#pragma once
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Components/UIAnchor.hpp"
#include <functional>

namespace okay {

class GameObject;

/// World-space UI context, set by the renderer (player / editor) for the current
/// frame so UI code can project in-world widgets (a World-Space Canvas, or a
/// standalone WorldSpaceUI object) onto the screen. The renderer supplies a
/// projector (world point -> screen pixels + depth) and the camera's right axis
/// (for billboarding). It is left inactive for normal screen-space UI, which is
/// therefore completely unchanged. Kept in its own tiny header so low-level UI
/// components (e.g. UIButton's hit-test) can consult it without pulling in the
/// whole UIElement layer.
struct UIWorldCtx {
    bool active = false;
    std::function<bool(const Vec3&, Vec2&, float&)> project;  // false if behind the camera
    Vec3  right{1.0f, 0.0f, 0.0f};   // world axis design +X runs along (camera right when billboarding)
    float screenW = 0.0f, screenH = 0.0f;
    /// Optional: maps a UI GameObject to its on-screen rect (origin + size, px),
    /// honoring world-space projection. Set by the renderer (which has the full
    /// UIElement layer) so low-level hit-tests — e.g. UIButton::Contains — can
    /// click a world-space widget where it actually appears, without this header
    /// depending on GetUIScreenRect. Null when unavailable.
    std::function<bool(GameObject*, Vec2&, Vec2&)> rectOf;
    /// Optional: false when a screen point falls outside the scroll views / masks
    /// clipping a widget, so the hidden part of a widget can't be clicked. Set next
    /// to `rectOf`. Null = no clipping.
    std::function<bool(GameObject*, const Vec2&)> clipContains;
};
inline UIWorldCtx& UIWorld() { static thread_local UIWorldCtx c; return c; }

/// Map a screen point into a widget's authored frame: the frame where the widget
/// sits at `designOrigin` (ResolveAnchor of its anchor/position/size against the
/// canvas) at 1:1 size. Widgets keep their hit-test and drag math in that frame.
/// When the host installed `rectOf` it knows where the widget is really drawn
/// (inside its UI parent, at the Canvas scale, or projected in world space), so a
/// button inside a scaled panel is clickable exactly where it appears. Without
/// `rectOf` the point is unchanged. A widget that isn't on screen maps far away.
inline Vec2 UIToLocal(GameObject* go, const Vec2& designOrigin, const Vec2& size, const Vec2& p) {
    if (!go || !UIWorld().rectOf) return p;
    Vec2 o, sz;
    if (!UIWorld().rectOf(go, o, sz)) return Vec2{-1.0e9f, -1.0e9f};   // behind the camera
    if (UIWorld().clipContains && !UIWorld().clipContains(go, p)) return Vec2{-1.0e9f, -1.0e9f};   // clipped away
    const float kx = sz.x > 1e-4f ? size.x / sz.x : 1.0f;
    const float ky = sz.y > 1e-4f ? size.y / sz.y : 1.0f;
    return Vec2{designOrigin.x + (p.x - o.x) * kx, designOrigin.y + (p.y - o.y) * ky};
}

/// The widget's real rectangle. Stretch anchors store margins in `position` /
/// `authored`, so a fill-all button with zero margins has an authored size of
/// 0×0 and would otherwise have no click area. This resolves that once; hit
/// tests and drag math use `origin` + `extent`, never the raw margin size.
/// Fixed anchors pass `authored` through unchanged.
inline void UIControlRect(UIAnchor anchor, const Vec2& position, const Vec2& authored,
                          Vec2& origin, Vec2& extent) {
    ResolveAnchorRect(anchor, position, authored, UICanvas::Width(), UICanvas::Height(), origin, extent);
}

/// Map `p` into the resolved control rect and report that rect. Widgets hit-test
/// and drag in this frame (the same one UIToLocal uses when a host installed
/// `rectOf`).
inline Vec2 UIControlPoint(GameObject* go, UIAnchor anchor, const Vec2& position, const Vec2& authored,
                           const Vec2& p, Vec2& origin, Vec2& extent) {
    UIControlRect(anchor, position, authored, origin, extent);
    return UIToLocal(go, origin, extent, p);
}

} // namespace okay
