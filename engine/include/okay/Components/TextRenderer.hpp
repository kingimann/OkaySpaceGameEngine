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
    /// Screen-space bounding box (unscaled pixels). The text is laid out *inside*
    /// this box (by `align` horizontally and vertically centered), so a label has
    /// a stable, selectable, resizable rect that doesn't jump around as the text
    /// changes — like a Unity Text's RectTransform.
    Vec2  size{220.0f, 48.0f};
    /// Screen-space only: which screen point the box anchors to, so a centered
    /// title or a bottom-right score adapts to the window size.
    UIAnchor anchor = UIAnchor::TopLeft;
    /// Screen-space horizontal alignment inside the box: 0 = left, 1 = center,
    /// 2 = right.
    int align = 0;
    /// Vertically center the text in its box (else top-aligned).
    bool vcenter = true;
    /// Optional filled box drawn behind the text (a label background / chip).
    bool  background = false;
    Color backgroundColor = Color::FromBytes(0, 0, 0, 140);
    /// Drop shadow: a second copy drawn behind the text, offset by `shadowOffset`
    /// font-pixels, in `shadowColor` — keeps HUD text legible over any backdrop.
    bool  shadow = false;
    Color shadowColor = Color::FromBytes(0, 0, 0, 200);
    Vec2  shadowOffset{1.0f, 1.0f};
    /// Outline: the text is also drawn offset by 1 font-pixel in all 4 (or 8)
    /// directions in `outlineColor`, so it reads on any background.
    bool  outline = false;
    Color outlineColor = Color::FromBytes(0, 0, 0, 230);
    /// Faux-bold: the glyphs are drawn a second time shifted 1 px right, so the
    /// strokes thicken — for headings/emphasis without a separate font.
    bool  bold = false;

    /// Width of the widest line at native font size (8 px per glyph), multi-line
    /// aware (handles embedded '\n').
    int PixelWidth() const {
        int best = 0, cur = 0;
        for (char ch : text) {
            if (ch == '\n') { if (cur > best) best = cur; cur = 0; }
            else cur += Font8x8::Width;
        }
        return cur > best ? cur : best;
    }
    /// Total height of all lines at native font size.
    int PixelHeight() const {
        int lines = 1;
        for (char c : text) if (c == '\n') ++lines;
        return lines * Font8x8::Height + (lines - 1);   // (Height+1) per line gap
    }

    /// Top-left pixel of the text BOX (screen space), anchored and sized by `size`.
    Vec2 BoxTopLeft(float canvasW, float canvasH, float scale = 1.0f) const {
        return ResolveAnchor(anchor, screenPos * scale, size * scale, canvasW, canvasH);
    }

    /// Resolve the top-left draw pixel for screen-space text inside its box,
    /// honoring horizontal `align` and vertical centering. `scale` is the canvas
    /// scale; the returned point is in canvas pixels (relative to the canvas).
    Vec2 ResolvedScreenPos(float canvasW, float canvasH, float scale = 1.0f) const {
        Vec2 box = BoxTopLeft(canvasW, canvasH, scale);
        float tw = PixelWidth() * pixelSize * scale, th = PixelHeight() * pixelSize * scale;
        float x = box.x;
        if (align == 1)      x += (size.x * scale - tw) * 0.5f;
        else if (align == 2) x +=  size.x * scale - tw;
        float y = box.y + (vcenter ? (size.y * scale - th) * 0.5f : 0.0f);
        return {x, y};
    }
};

} // namespace okay
