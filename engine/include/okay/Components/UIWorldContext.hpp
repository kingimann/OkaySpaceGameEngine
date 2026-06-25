#pragma once
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
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
};
inline UIWorldCtx& UIWorld() { static thread_local UIWorldCtx c; return c; }

} // namespace okay
