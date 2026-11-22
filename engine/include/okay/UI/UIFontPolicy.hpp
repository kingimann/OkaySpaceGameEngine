#pragma once
// How in-game UI text is rasterized.
//
// The shipped default is a TrueType face baked at the size it will occupy on
// screen (font pixels × the canvas scale), sampled with a linear filter.
// Scene::uiFont == "pixel" is the retro option: the built-in 8x8 bitmap and
// nearest filtering, with hard shape edges. It is never the default.
#include <cmath>
#include <cstdlib>
#include <cstring>
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
    // Up to 32 px every whole size gets its own bake (exact, the sharpest). Above that the sizes
    // step by about 6% (so the glyph is drawn at most 3% off its baked size and stays sharp),
    // which keeps the number of baked faces bounded while the editor zoom is dragged smoothly.
    if (h > 32) {
        const double steps = std::round(std::log((double)h / 32.0) / std::log(1.06));
        h = (int)std::lround(32.0 * std::pow(1.06, steps));
        if (h > 256) h = 256;
    }
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

/// UI filtering: true = Smooth (anti-aliased shapes, linear-filtered images, TrueType text; the
/// default), false = Pixel-art (hard shape edges, nearest images, bitmap text). The scene's UI font
/// "pixel" picks pixel-art; the environment variable OKAY_UI_FILTER=pixel or =smooth overrides it
/// (the escape hatch if something looks wrong: OKAY_UI_FILTER=pixel restores hard edges and nearest
/// images; the text font stays the scene's).
inline bool UISmoothFilteringFor(const std::string& uiFont) {
    static const int env = [] {
        const char* v = std::getenv("OKAY_UI_FILTER");
        if (!v) return -1;
        if (!std::strcmp(v, "pixel")) return 0;
        if (!std::strcmp(v, "smooth")) return 1;
        return -1;
    }();
    if (env >= 0) return env == 1;
    return uiFont != "pixel";
}

/// How carefully the in-game UI is drawn. Fast: text snapped to whole device pixels. Good (default): plus a
/// small-text weight correction, smooth soft shadows and mip-filtered image downscaling. Best: plus quarter-pixel
/// text positions and dithered gradients. Old: the drawing from before the UI quality work, for comparison.
enum class UIQuality { Fast = 0, Good = 1, Best = 2, Old = 3 };

/// The quality a scene draws with: OKAY_CLASSIC_UI=1 (or OKAY_UI_QUALITY=old|fast|good|best) wins over the
/// scene's own setting (`Scene::uiQuality`, 0..3).
inline UIQuality UIQualityFor(int sceneQuality) {
    static const int env = [] {
        const char* c = std::getenv("OKAY_CLASSIC_UI");
        if (c && c[0] && std::strcmp(c, "0") != 0) return 3;
        const char* v = std::getenv("OKAY_UI_QUALITY");
        if (!v) return -1;
        if (!std::strcmp(v, "fast")) return 0;
        if (!std::strcmp(v, "good")) return 1;
        if (!std::strcmp(v, "best")) return 2;
        if (!std::strcmp(v, "old") || !std::strcmp(v, "classic")) return 3;
        return -1;
    }();
    int q = env >= 0 ? env : sceneQuality;
    if (q < 0) q = 0;
    if (q > 3) q = 3;
    return (UIQuality)q;
}

/// Glyph atlases (TrueType) are uploaded with linear sampling. The bitmap font
/// does not use an atlas; pixel-art mode selects nearest at the policy above.
inline bool UIGlyphAtlasLinearSampling() { return true; }

} // namespace okay
