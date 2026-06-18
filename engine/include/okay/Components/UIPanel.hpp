#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"

namespace okay {

/// A non-interactive screen-space rectangle (menu backgrounds, HUD frames,
/// dimming overlays). Position/size are window pixels (origin top-left); alpha
/// is honored so you can lay translucent panels over the scene.
class UIPanel : public Behaviour {
public:
    Vec2 position{0.0f, 0.0f};
    Vec2 size{200.0f, 120.0f};
    Color color = Color::FromBytes(20, 24, 34, 200);
};

} // namespace okay
