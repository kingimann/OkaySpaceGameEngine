#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Math/Vec2.hpp"
#include <algorithm>

namespace okay {

/// The parts of the screen a phone covers or reserves (notch / Dynamic Island,
/// rounded corners, the home bar), as fractions of the screen width / height.
/// The host sets it: the editor from the emulated device in the Game view, a
/// mobile build from the OS. Zero on a desktop.
struct UISafeInsetFractions { float left = 0, top = 0, right = 0, bottom = 0; };
inline UISafeInsetFractions& UISafeInsets() { static UISafeInsetFractions f; return f; }

/// A full-screen UI container that stays inside the safe area (common
/// "SafeArea" panel). Put HUD and menu widgets under it and they keep clear of
/// the notch and the home bar on phones; on a desktop it simply fills the screen.
/// Each side can be turned off (e.g. a background that should run under the notch).
class UISafeArea : public Behaviour {
public:
    bool left = true, top = true, right = true, bottom = true;
    // Unused margins: the container always fills its parent minus the insets, but
    // tools that move / resize widgets get the usual stretch-anchor rect fields.
    Vec2 position{0.0f, 0.0f};
    Vec2 size{0.0f, 0.0f};
    UIAnchor anchor = UIAnchor::StretchFull;

    /// The safe rect for a screen of `sw` x `sh`, clipped to the container rect
    /// (`co`, `cs`) it lives in.
    void Resolve(float sw, float sh, Vec2 co, Vec2 cs, Vec2& origin, Vec2& outSize) const {
        const UISafeInsetFractions& f = UISafeInsets();
        const float x0 = left ? f.left * sw : 0.0f, y0 = top ? f.top * sh : 0.0f;
        const float x1 = sw - (right ? f.right * sw : 0.0f), y1 = sh - (bottom ? f.bottom * sh : 0.0f);
        const float ax = std::max(co.x, x0), ay = std::max(co.y, y0);
        const float bx = std::min(co.x + cs.x, x1), by = std::min(co.y + cs.y, y1);
        origin = {ax, ay};
        outSize = {std::max(0.0f, bx - ax), std::max(0.0f, by - ay)};
    }
};

} // namespace okay
