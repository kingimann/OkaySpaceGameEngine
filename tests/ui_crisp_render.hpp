#pragma once
// Headless render of the crisp-UI scene through the UI pass onto the software painter (ui_soft_render.hpp).
#include "ui_crisp_scene.hpp"
#include "ui_soft_render.hpp"
#include <string>

namespace ui_crisp {
using namespace okay;
/// Draw the scene on a W x H target (RGBA out). `pixel` = the scene's pixel-art look.
inline bool Render(int W, int H, const std::string& tex, bool pixel, bool batched, Image& out) {
    Scene scene("crisp");
    ui_crisp::Build(scene, tex, pixel);
    return okaytest::SoftRenderSceneUI(scene, W, H, out, batched, 14, 16, 22);
}

} // namespace ui_crisp
