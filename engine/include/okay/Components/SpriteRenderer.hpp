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
    /// Sorting layer: a coarse band that ALWAYS wins over `sortOrder` (Unity-style).
    /// Everything in a higher sorting layer draws on top of everything in a lower
    /// one, regardless of order-in-layer. Use named bands like Background(-1),
    /// Default(0), Foreground(1), UI(2); `sortOrder` then orders within a band.
    int sortingLayer = 0;
    /// Draw order within the sorting layer: lower renders first (further back),
    /// higher on top. Use it for parallax backgrounds, characters, props, etc.
    int sortOrder = 0;

    /// Combined back-to-front sort key: sorting layer first, then order-in-layer.
    /// Compare these to draw correctly; smaller = further back.
    long long SortKey() const {
        return ((long long)sortingLayer << 32) | (unsigned int)(sortOrder + 0x40000000);
    }
    /// Mirror the sprite horizontally / vertically (e.g. flip a character to
    /// face the way it's moving) without touching the Transform.
    bool flipX = false;
    bool flipY = false;

    void OnRender(IRenderer& renderer) override;
};

} // namespace okay
