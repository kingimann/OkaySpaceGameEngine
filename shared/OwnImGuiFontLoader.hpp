#pragma once
// OKAY_OWN_FONT_RASTER=1: Dear ImGui's font atlas takes its glyph bitmaps from our own
// TrueType reader and rasteriser (okay::ttf::Face, engine/src/Graphics/TtfRaster.cpp)
// instead of the rasteriser built into Dear ImGui. Off by default.
//
// How: Dear ImGui 1.92 loads glyphs through a font-loader interface (ImFontLoader,
// declared in imgui_internal.h; its own header says "prefer using a custom ImFontLoader"
// for custom glyphs). This file is our own loader for that interface: it parses each
// font source with ttf::Face, rasterises a glyph when Dear ImGui first asks for it,
// packs a rectangle in the atlas with ImFontAtlasPackAddRect and copies the coverage in
// with ImFontAtlasBakedSetFontGlyphBitmap. Dear ImGui's source is not changed, and no
// code of its own loader was read or copied: this was written from the declarations
// and comments in imgui.h / imgui_internal.h and checked against the stock loader's
// output (metrics and pixels) in tests/test_font_raster_compare.cpp.
//
// Use: call okay::imguifont::InstallIfRequested(io.Fonts) right after
// ImGui::CreateContext(), before the AddFont calls. Every font of that atlas (merged
// sources too) then goes through our rasteriser. Glyphs are rasterised once at the
// exact pixel size with exact-area coverage; OversampleH / OversampleV are ignored
// (they only matter to a rasteriser that cannot place glyphs exactly).
//
// Kerning: Dear ImGui's text layout has no kerning step, so pair kerning is not applied
// on this path (OkayUI and the engine's text do apply it).
#include "imgui.h"
#include "imgui_internal.h"
#include "okay/Graphics/TtfRaster.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace okay {
namespace imguifont {

/// True when OKAY_OWN_FONT_RASTER=1 (or "on" / "true") is set.
inline bool OwnFontRasterRequested() {
    const char* e = std::getenv("OKAY_OWN_FONT_RASTER");
    if (!e) return false;
    const char c = e[0];
    return c == '1' || c == 'o' || c == 'O' || c == 't' || c == 'T' || c == 'y' || c == 'Y';
}

namespace detail {

struct Source {
    ttf::Face face;
};

inline Source* Src(ImFontConfig* src) { return src ? static_cast<Source*>(src->FontLoaderData) : nullptr; }

// The pixel size one source is drawn at inside a baked font of size baked->Size.
inline float SourceSize(const ImFontConfig* src, const ImFontBaked* baked) {
    float size = baked->Size * (src->ExtraSizeScale > 0.0f ? src->ExtraSizeScale : 1.0f);
    return size > 0.0f ? size : 1.0f;
}
inline float Density(const ImFontBaked* baked) {
    return baked->RasterizerDensity > 0.0f ? baked->RasterizerDensity : 1.0f;
}
// Glyph offsets are given for the first source's size and scale with the baked size.
inline float OffsetScale(const ImFontConfig* src, const ImFontBaked* baked) {
    const ImFont* font = baked->OwnerFont;
    const float ref = (font && font->Sources.Size > 0) ? font->Sources[0]->SizePixels : src->SizePixels;
    return ref > 0.0f ? baked->Size / ref : 1.0f;
}
// The baseline, from the top of the line, in pixels: the first source's ascent rounded up
// to a whole pixel (and the descent rounded down), so text sits where it does with Dear
// ImGui's own loader (measured: tests/test_font_raster_compare.cpp checks ascent and descent).
inline float Baseline(const ImFontConfig* src, const ImFontBaked* baked) {
    const ImFont* font = baked->OwnerFont;
    ImFontConfig* first = (font && font->Sources.Size > 0) ? font->Sources[0] : const_cast<ImFontConfig*>(src);
    const Source* d = Src(first);
    if (!d) d = Src(const_cast<ImFontConfig*>(src));
    if (!d) return 0.0f;
    int asc = 0, desc = 0, gap = 0;
    d->face.VMetrics(asc, desc, gap);
    const float scale = d->face.ScaleForPixelHeight(SourceSize(first, baked) * Density(baked));
    return std::ceil((float)asc * scale) / Density(baked);
}

inline bool LoaderInit(ImFontAtlas*) { return true; }
inline void LoaderShutdown(ImFontAtlas*) {}

inline bool FontSrcInit(ImFontAtlas*, ImFontConfig* src) {
    if (!src || !src->FontData || src->FontDataSize <= 0) return false;
    Source* d = new Source();
    if (!d->face.Load(static_cast<const std::uint8_t*>(src->FontData), (std::size_t)src->FontDataSize, (int)src->FontNo)) {
        delete d;
        return false;
    }
    src->FontLoaderData = d;
    return true;
}

inline void FontSrcDestroy(ImFontAtlas*, ImFontConfig* src) {
    delete Src(src);
    if (src) src->FontLoaderData = nullptr;
}

inline bool FontSrcContainsGlyph(ImFontAtlas*, ImFontConfig* src, ImWchar cp) {
    const Source* d = Src(src);
    return d && d->face.FindGlyph((std::uint32_t)cp) != 0;
}

inline bool FontBakedInit(ImFontAtlas*, ImFontConfig* src, ImFontBaked* baked, void*) {
    const Source* d = Src(src);
    if (!d) return false;
    if (src->MergeMode) return true;   // the first source sets the line metrics
    int asc = 0, desc = 0, gap = 0;
    d->face.VMetrics(asc, desc, gap);
    const float density = Density(baked);
    const float scale = d->face.ScaleForPixelHeight(SourceSize(src, baked) * density);
    baked->Ascent = std::ceil((float)asc * scale) / density;
    baked->Descent = std::floor((float)desc * scale) / density;
    return true;
}

inline void FontBakedDestroy(ImFontAtlas*, ImFontConfig*, ImFontBaked*, void*) {}

inline bool FontBakedLoadGlyph(ImFontAtlas* atlas, ImFontConfig* src, ImFontBaked* baked, void*, ImWchar codepoint,
                               ImFontGlyph* out, float* outAdvance) {
    const Source* d = Src(src);
    if (!d) return false;
    const int gi = d->face.FindGlyph((std::uint32_t)codepoint);
    if (gi == 0) return false;   // not in this source: Dear ImGui tries the next merged one
    const float density = Density(baked);
    const float scale = d->face.ScaleForPixelHeight(SourceSize(src, baked) * density);
    int adv = 0, lsb = 0;
    d->face.HMetrics(gi, adv, lsb);
    const float advance = (float)adv * scale / density;
    if (!out) {   // metrics only (Dear ImGui's "advance first, pixels later" mode)
        if (outAdvance) *outAdvance = advance;
        return true;
    }
    out->AdvanceX = advance;
    static thread_local std::vector<unsigned char> buf;
    int x0, y0, w, h;
    if (!d->face.RenderGlyph(gi, scale, buf, x0, y0, w, h)) {   // nothing to draw (space)
        out->Visible = false;
        return true;
    }
    const ImFontAtlasRectId id = ImFontAtlasPackAddRect(atlas, w, h);
    if (id == ImFontAtlasRectId_Invalid) return false;   // the atlas is full
    ImTextureRect* r = ImFontAtlasPackGetRect(atlas, id);
    const float off = OffsetScale(src, baked);
    const float base = Baseline(src, baked);
    out->X0 = (float)x0 / density + src->GlyphOffset.x * off;
    out->Y0 = (float)y0 / density + base + src->GlyphOffset.y * off;
    out->X1 = out->X0 + (float)w / density;
    out->Y1 = out->Y0 + (float)h / density;
    out->Visible = true;
    out->PackId = id;
    ImFontAtlasBakedSetFontGlyphBitmap(atlas, baked, src, out, r, buf.data(), ImTextureFormat_Alpha8, w);
    return true;
}

} // namespace detail

/// Our loader for ImFontAtlas::SetFontLoader or ImFontConfig::FontLoader.
inline const ImFontLoader* Loader() {
    static ImFontLoader loader = [] {
        ImFontLoader l;
        l.Name = "OkaySpace TrueType (own)";
        l.LoaderInit = detail::LoaderInit;
        l.LoaderShutdown = detail::LoaderShutdown;
        l.FontSrcInit = detail::FontSrcInit;
        l.FontSrcDestroy = detail::FontSrcDestroy;
        l.FontSrcContainsGlyph = detail::FontSrcContainsGlyph;
        l.FontBakedInit = detail::FontBakedInit;
        l.FontBakedDestroy = detail::FontBakedDestroy;
        l.FontBakedLoadGlyph = detail::FontBakedLoadGlyph;
        l.FontBakedSrcLoaderDataSize = 0;
        return l;
    }();
    return &loader;
}

/// Switch `atlas` to our rasteriser when OKAY_OWN_FONT_RASTER=1. Returns true when it did.
inline bool InstallIfRequested(ImFontAtlas* atlas) {
    if (!atlas || !OwnFontRasterRequested()) return false;
    atlas->SetFontLoader(Loader());
    return true;
}

} // namespace imguifont
} // namespace okay
