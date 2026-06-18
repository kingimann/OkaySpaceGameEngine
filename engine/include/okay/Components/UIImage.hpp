#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include <string>

namespace okay {

/// A screen-space image (logos, icons, HUD frames, title art). Draws `texture`
/// stretched into the pixel rect at `position`/`size` (origin top-left), tinted
/// by `color`. With no texture it falls back to a flat colored rectangle, so it
/// doubles as a solid/translucent panel. Build Game bundles the image file.
class UIImage : public Behaviour {
public:
    Vec2 position{20.0f, 20.0f};
    Vec2 size{128.0f, 128.0f};
    std::string texture;                 // image path (PNG/JPG/BMP); empty = colored rect
    Color color = Color::White;          // tint (or fill color when no texture)
    UIAnchor anchor = UIAnchor::TopLeft;
};

} // namespace okay
