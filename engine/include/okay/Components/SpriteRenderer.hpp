#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"

namespace okay {

/// Draws a colored quad at its GameObject's position, sized by the Transform's
/// scale. Conceptually the same role as Unity's SpriteRenderer, simplified for
/// the console backend (the glyph stands in for a texture).
class SpriteRenderer : public Component {
public:
    /// Base size of the sprite in world units, before Transform scale.
    Vec2  size = Vec2::One;
    Color color = Color::White;
    /// The character drawn by the console renderer.
    char  glyph = '#';

    void OnRender(IRenderer& renderer) override;
};

} // namespace okay
