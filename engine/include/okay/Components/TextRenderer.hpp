#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Graphics/Font.hpp"
#include <string>

namespace okay {

/// Draws a string with the built-in 8x8 bitmap font — score counters, labels,
/// and HUD text without a font file. In world space it sits at the GameObject's
/// position (sized in world units); in screen space it's pinned to window pixels.
class TextRenderer : public Component {
public:
    std::string text = "Text";
    Color color = Color::White;
    /// World units per font pixel (world space) or window pixels per font pixel
    /// (screen space).
    float pixelSize = 0.1f;
    /// When true, `screenPos` (pixels, top-left origin) is used instead of the
    /// Transform — handy for a fixed HUD.
    bool  screenSpace = false;
    Vec2  screenPos{12.0f, 12.0f};

    /// Width/height of the current text at native font size (8 px per glyph).
    int PixelWidth() const { return Font8x8::MeasureWidth(text.c_str()); }
    int PixelHeight() const { return Font8x8::Height; }
};

} // namespace okay
