// OkayUI software backend. See OkayUI_Soft.hpp.
#include "OkayUI_Soft.hpp"
#include "OkayUI_SoftRaster.hpp"
#include "okay/Graphics/TtfFont.hpp"

#include <algorithm>
#include <cmath>

namespace OkayUI {
namespace soft {

void Bitmap::Clear(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) { rgba[i] = r; rgba[i + 1] = g; rgba[i + 2] = b; rgba[i + 3] = a; }
}

void Render(const DrawData& data, Bitmap& bm) {
    if (bm.w <= 0 || bm.h <= 0 || bm.rgba.size() != (std::size_t)bm.w * (std::size_t)bm.h * 4) return;
    if (!data.vertices || !data.indices || data.vertexCount <= 0 || data.indexCount <= 0) return;
    if (data.vertexStride != (int)sizeof(DrawVertex)) return;   // not our vertex layout
    const DrawVertex* vtx = (const DrawVertex*)data.vertices;
    raster::Surface surf; surf.px = bm.rgba.data(); surf.w = bm.w; surf.h = bm.h;
    for (int ci = 0; ci < data.cmdCount && data.cmds; ++ci) {
        const DrawCmd& cmd = data.cmds[ci];
        raster::Clip clip{ 0, 0, bm.w, bm.h };
        if (cmd.clipOn) {   // a garbage clip rectangle is clamped in 64-bit maths, never trusted
            clip.x0 = cmd.clipX; clip.y0 = cmd.clipY;
            clip.x1 = (std::int64_t)cmd.clipX + std::max(0, cmd.clipW); clip.y1 = (std::int64_t)cmd.clipY + std::max(0, cmd.clipH);
        }
        raster::Tex tex;
        bool textured = false;
        if (cmd.texture) {
            if (cmd.texture == data.atlasTexture && data.atlasRgba && data.atlasW > 0 && data.atlasH > 0) { tex.w = data.atlasW; tex.h = data.atlasH; tex.rgba = data.atlasRgba; textured = true; }
            else if (okay::TtfFont* font = FallbackFontOf(cmd.texture)) {   // a font's own glyph atlas (a fallback font, or one added with AddFontFrom*)
                const okay::Image& a = font->Atlas();
                if (!font->Valid() || a.Width() <= 0 || a.Height() <= 0 || a.Width() > 8192 || a.Height() > 8192) continue;
                tex.w = a.Width(); tex.h = a.Height(); tex.rgba = a.Data(); textured = true;
            }
            else continue;      // a GPU texture we cannot see
        }
        if (cmd.idxOffset < 0 || cmd.idxCount <= 0) continue;
        const std::int64_t end = std::min<std::int64_t>(data.indexCount, (std::int64_t)cmd.idxOffset + cmd.idxCount);
        for (std::int64_t i = cmd.idxOffset; i + 3 <= end; i += 3) {
            const int a = data.indices[i], b = data.indices[i + 1], c = data.indices[i + 2];
            if (a < 0 || b < 0 || c < 0 || a >= data.vertexCount || b >= data.vertexCount || c >= data.vertexCount) continue;
            raster::Triangle(surf, clip, textured ? &tex : nullptr, true, vtx[a], vtx[b], vtx[c]);
        }
    }
}

} // namespace soft
} // namespace OkayUI
