#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Components/UIAnchor.hpp"
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
    /// Screen-space only: which screen point `screenPos` is an offset from, so a
    /// centered title or a bottom-right score adapts to the window size. The
    /// text's own width/height is used so it stays inside the anchored corner.
    UIAnchor anchor = UIAnchor::TopLeft;

    /// Width/height of the current text at native font size (8 px per glyph).
    int PixelWidth() const { return Font8x8::MeasureWidth(text.c_str()); }
    int PixelHeight() const { return Font8x8::Height; }

    /// Resolve the top-left draw pixel for screen-space text, given the rendered
    /// glyph size (`pixelSize` * native px) and the canvas dimensions.
    Vec2 ResolvedScreenPos(float canvasW, float canvasH) const {
        Vec2 size{PixelWidth() * pixelSize, PixelHeight() * pixelSize};
        return ResolveAnchor(anchor, screenPos, size, canvasW, canvasH);
    }
};

} // namespace okay
