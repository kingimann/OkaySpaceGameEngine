#pragma once
// How in-game UI text is rasterized.
//
// The shipped default is a TrueType face baked at the size it will occupy on
// screen (font pixels × the canvas scale), sampled with a linear filter.
// Scene::uiFont == "pixel" is the retro option: the built-in 8x8 bitmap and
// nearest filtering, with hard shape edges. It is never the default.
#include <cmath>
#include <string>

namespace okay {

enum class UIAtlasFilter { Linear = 0, Nearest = 1 };

/// What DrawUIText will do for this scene font at `fontPx` (the widget's font-pixel
/// scale, where 1 is the 8px bitmap cell) on a canvas whose device scale is
/// `deviceScale` (1 = one logical pixel per screen pixel).
struct UITextRaster {
    bool trueType = false;
    float pixelHeight = 0.0f;   ///< device pixels the glyphs are rasterized at (TrueType)
    UIAtlasFilter filter = UIAtlasFilter::Linear;
    bool pixelArt = false;
};

/// Clamp a requested device-pixel glyph size to the range the atlas will bake.
inline int UIGlyphBakePixels(float devicePixels) {
    int h = (int)std::lround(devicePixels);
    if (h < 4) h = 4;
    if (h > 256) h = 256;
    return h;
}

inline UITextRaster UITextRasterFor(const std::string& uiFont, float fontPx, float deviceScale) {
    UITextRaster r;
    const float scale = deviceScale > 0.05f ? deviceScale : 1.0f;
    if (uiFont == "pixel") {
        r.pixelArt = true;
        r.trueType = false;
        r.filter = UIAtlasFilter::Nearest;
        r.pixelHeight = fontPx < 1.0f ? 1.0f : fontPx;
        return r;
    }
    r.pixelArt = false;
    r.trueType = true;
    r.filter = UIAtlasFilter::Linear;
    const float px = fontPx > 0.0f ? fontPx : 1.0f;
    r.pixelHeight = (float)UIGlyphBakePixels(px * 8.0f * scale);
    return r;
}

/// Glyph atlases (TrueType) are uploaded with linear sampling. The bitmap font
/// does not use an atlas; pixel-art mode selects nearest at the policy above.
inline bool UIGlyphAtlasLinearSampling() { return true; }

} // namespace okay
