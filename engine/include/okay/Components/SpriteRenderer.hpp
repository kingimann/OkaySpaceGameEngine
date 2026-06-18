#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include <string>

namespace okay {

/// Draws a colored quad at its GameObject's position, sized by the Transform's
/// scale. Conceptually the same role as Unity's SpriteRenderer. With a `texture`
/// path set, the windowed runtimes draw that image (tinted by `color`); the
/// console backend always falls back to the `glyph`.
class SpriteRenderer : public Component {
public:
    /// Base size of the sprite in world units, before Transform scale.
    Vec2  size = Vec2::One;
    Color color = Color::White;
    /// The character drawn by the console renderer.
    char  glyph = '#';
    /// Optional image file drawn by the editor/player (PNG/JPG/BMP/...).
    /// Empty means a flat colored quad.
    std::string texture;
    /// Sub-region of the texture to draw, in normalized 0..1 coordinates
    /// (origin top-left). The default covers the whole image; a SpriteAnimator
    /// in atlas mode rewrites these to walk a sprite sheet.
    Vec2 uvMin{0.0f, 0.0f};
    Vec2 uvMax{1.0f, 1.0f};
    /// Draw order: lower values render first (further back), higher on top.
    /// Use it for parallax backgrounds, characters, foreground props, etc.
    int sortOrder = 0;

    void OnRender(IRenderer& renderer) override;
};

} // namespace okay
