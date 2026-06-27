#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Graphics/Font.hpp"
#include "okay/Graphics/TtfFont.hpp"
#include <string>
#include <cmath>
#include <cctype>
#include <algorithm>

namespace okay {

/// Draws a string with the built-in 8x8 bitmap font — score counters, labels,
/// and HUD text without a font file. In world space it sits at the GameObject's
/// position (sized in world units); in screen space it's pinned to window pixels.
class TextRenderer : public Component {
public:
    std::string text = "Text";
    /// Optional path to an imported TrueType/OpenType font (e.g. "Assets/MyFont.ttf").
    /// Empty = the built-in 8x8 bitmap font. When set and loadable, hosts draw the
    /// text with the TTF glyph atlas; metrics fall back to the bitmap if it can't load.
    std::string fontPath;
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
    /// Bottom-align the text in its box (overrides vcenter). Together with vcenter
    /// this gives Top / Center / Bottom vertical alignment.
    bool alignBottom = false;
    /// Faux-italic: shear the glyphs to the right for emphasis.
    bool  italic = false;
    /// Vertical color gradient: the top of each glyph uses `color`, the bottom uses
    /// `colorBottom` (for titles / fancy labels). Off by default.
    bool  gradient = false;
    Color colorBottom = Color::FromBytes(120, 160, 255);
    /// Typewriter reveal: only the first `visibleChars` characters are drawn
    /// (-1 = all). If `typeSpeed` > 0, the count auto-advances at that many
    /// characters per second (dialogue that types itself out).
    int   visibleChars = -1;
    float typeSpeed = 0.0f;
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
    /// Extra spacing (in font pixels) between glyphs and between lines.
    float letterSpacing = 0.0f;
    float lineSpacing = 0.0f;
    /// Render the text UPPER-CASED without changing the stored string.
    bool  uppercase = false;
    /// Word-wrap the text to the box width (screen space). Long words overflow.
    bool  wrap = false;

    /// Horizontal advance per glyph / vertical advance per line, in font pixels.
    float Advance() const { return (float)Font8x8::Width + 1.0f + letterSpacing; }
    float LineAdvance() const { return (float)Font8x8::Height + 1.0f + lineSpacing; }

    /// The string actually drawn: optionally upper-cased and word-wrapped to the
    /// box. Existing '\n' line breaks are preserved.
    /// Auto-advance the typewriter reveal when typeSpeed > 0.
    void Update(float dt) override {
        if (typeSpeed > 0.0f) m_reveal += typeSpeed * dt;
    }
    /// Restart the typewriter reveal from the first character.
    void ResetReveal() { m_reveal = 0.0f; }
    /// How many characters are currently visible (-1 = all): the auto-advancing
    /// reveal when typeSpeed > 0, else the manual `visibleChars`.
    int EffectiveVisible() const {
        if (typeSpeed > 0.0f) return (int)m_reveal;
        return visibleChars;
    }

    std::string DisplayText() const {
        std::string s = text;
        int vis = EffectiveVisible();
        if (vis >= 0 && vis < (int)s.size()) s = s.substr(0, (std::size_t)vis);
        if (uppercase)
            for (char& c : s) c = (char)std::toupper((unsigned char)c);
        if (wrap && size.x > 0.0f && pixelSize > 0.0f) {
            float glyphW = Advance() * pixelSize;
            int maxChars = glyphW > 0.0f ? (int)(size.x / glyphW) : 0;
            if (maxChars >= 1) s = WrapTo(s, maxChars);
        }
        return s;
    }

    /// The resolved TTF font for this label, or nullptr to use the bitmap font.
    /// All measurement/draw use the SAME "font pixel" space (Font8x8::Height tall),
    /// so a host can scale either font by `pixelSize` identically.
    TtfFont* Font() const { return fontPath.empty() ? nullptr : GetFont(fontPath); }

    /// Width of the widest line in font pixels (honors letter spacing + wrap).
    int PixelWidth() const {
        std::string s = DisplayText();
        if (TtfFont* tf = Font()) {
            float best = 0.0f, cur = 0.0f;
            float adv = letterSpacing; // extra spacing per glyph, in font pixels
            for (char ch : s) {
                if (ch == '\n') { best = std::max(best, cur); cur = 0.0f; continue; }
                const TtfFont::Glyph* g = tf->Get(ch);
                cur += (g ? g->xadvance * tf->ScaleFor((float)Font8x8::Height) : 0.0f) + adv;
            }
            return (int)std::ceil(std::max(best, cur));
        }
        int best = 0, cur = 0;
        auto lineW = [this](int glyphs) {
            return glyphs > 0 ? (int)((glyphs - 1) * Advance() + Font8x8::Width) : 0;
        };
        for (char ch : s) {
            if (ch == '\n') { best = std::max(best, lineW(cur)); cur = 0; }
            else ++cur;
        }
        return std::max(best, lineW(cur));
    }
    /// Total height of all lines in font pixels (honors line spacing + wrap).
    int PixelHeight() const {
        std::string s = DisplayText();
        int lines = 1;
        for (char c : s) if (c == '\n') ++lines;
        if (TtfFont* tf = Font()) {
            float lh = tf->LineHeight((float)Font8x8::Height) + lineSpacing;
            return (int)std::ceil((lines - 1) * lh + (float)Font8x8::Height);
        }
        return (int)((lines - 1) * LineAdvance() + Font8x8::Height);
    }

    /// Word-wrap `s` so no line exceeds `maxChars` glyphs (existing '\n' kept;
    /// a single word longer than the limit overflows rather than splitting).
    static std::string WrapTo(const std::string& s, int maxChars) {
        std::string out, word; int lineLen = 0;
        auto flush = [&]() {
            if (word.empty()) return;
            if (lineLen > 0 && lineLen + 1 + (int)word.size() > maxChars) { out += '\n'; lineLen = 0; }
            else if (lineLen > 0) { out += ' '; ++lineLen; }
            out += word; lineLen += (int)word.size(); word.clear();
        };
        for (char c : s) {
            if (c == '\n') { flush(); out += '\n'; lineLen = 0; }
            else if (c == ' ' || c == '\t') flush();
            else word += c;
        }
        flush();
        return out;
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
        float y = box.y + (alignBottom ? (size.y * scale - th)
                                       : vcenter ? (size.y * scale - th) * 0.5f : 0.0f);
        return {x, y};
    }

private:
    float m_reveal = 0.0f;   // runtime typewriter progress (chars), when typeSpeed > 0
};

} // namespace okay
