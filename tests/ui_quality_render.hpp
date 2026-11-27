#pragma once
// Headless render of any Scene through the UI pass onto the software painter (ui_soft_render.hpp), for the UI quality test.
#include "ui_quality_scene.hpp"
#include "ui_soft_render.hpp"
#include <string>

namespace ui_quality {
using namespace okay;

/// Draw `scene` on a W x H target cleared to (bgR, bgG, bgB); RGBA out. `batched` = the player's batched path.
inline bool RenderScene(Scene& scene, int W, int H, Image& out, bool batched = false, int bgR = 14, int bgG = 16, int bgB = 22) {
    return okaytest::SoftRenderSceneUI(scene, W, H, out, batched, bgR, bgG, bgB);
}

inline int Byte(const Image& im, int x, int y, int c) {
    const Color p = im.GetPixel(x, y);
    const float v = c == 0 ? p.r : (c == 1 ? p.g : p.b);
    return (int)(v * 255.0f + 0.5f);
}

} // namespace ui_quality
