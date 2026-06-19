#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Components/UIAnchor.hpp"
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
    UIAnchor anchor = UIAnchor::TopLeft;
    // Customization: rounded corners and an optional border.
    float cornerRadius = 4.0f;
    float borderWidth = 0.0f;                      // 0 = no border
    Color borderColor = Color::FromBytes(255, 255, 255, 60);
    // Optional top-to-bottom gradient: when on, `color` is the top color and
    // `colorBottom` the bottom (a vertical fade for headers/backdrops).
    bool  useGradient = false;
    Color colorBottom = Color::FromBytes(10, 12, 18, 200);
    // Optional drop shadow: a translucent copy drawn behind, offset by
    // `shadowOffset` pixels — lifts dialogs/cards off the scene.
    bool  shadow = false;
    Color shadowColor = Color::FromBytes(0, 0, 0, 120);
    Vec2  shadowOffset{6.0f, 6.0f};
};

} // namespace okay
