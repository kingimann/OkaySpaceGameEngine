#pragma once
// Headless pictures of the game's UI pass for the UI picture tests: a W x H canvas on our own software painter
// (OkayUI::soft::CreateBitmapPainter, okayui/soft), cleared to a colour, drawn into by the caller, read back as an RGBA
// okay::Image. One home for what each picture test used to do by hand on an SDL software renderer (SDL2 left the
// repository's desktop builds on 2026-11-27). Link okay_ui_soft.
#include "OkayUI_SoftPainter.hpp"
#include "okay/Graphics/Image.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/UI/UIBatcher.hpp"
#include "okay/UI/UIRenderer.hpp"

#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

namespace okaytest {

/// The canvas: `draw` paints on the painter (source-over blending on), `out` gets the finished picture. False when the
/// painter cannot be made (a size outside 1..16384) or the pixels cannot be read.
inline bool SoftPaint(int w, int h, int bgR, int bgG, int bgB, const std::function<void(okay::platform::Painter&)>& draw,
                      okay::Image& out) {
    std::unique_ptr<okay::platform::Painter> p = OkayUI::soft::CreateBitmapPainter(w, h);
    if (!p) return false;
    p->SetBlendMode(okay::platform::BlendMode::None);
    p->SetDrawColor((std::uint8_t)bgR, (std::uint8_t)bgG, (std::uint8_t)bgB, 255);
    p->Clear();
    p->SetBlendMode(okay::platform::BlendMode::Blend);
    if (draw) draw(*p);
    p->Present();
    std::vector<std::uint8_t> px;
    int rw = 0, rh = 0;
    if (!p->ReadPixels(px, rw, rh) || rw != w || rh != h || px.size() < (std::size_t)w * (std::size_t)h * 4) return false;
    out = okay::Image(w, h);
    std::memcpy(out.Data(), px.data(), (std::size_t)w * (std::size_t)h * 4);
    return true;
}

/// The game's UI pass for `scene` on such a canvas. `batched` = the player's batched path (ui2d::BatchPainter).
inline bool SoftRenderSceneUI(okay::Scene& scene, int w, int h, okay::Image& out, bool batched = false,
                              int bgR = 14, int bgG = 16, int bgB = 22) {
    return SoftPaint(w, h, bgR, bgG, bgB, [&](okay::platform::Painter& painter) {
        if (batched) {
            okay::ui2d::BatchPainter batch(painter);
            okay::ui2d::RenderSceneUI(scene, batch, w, h);
            batch.Flush();
        } else {
            okay::ui2d::RenderSceneUI(scene, painter, w, h);
        }
    }, out);
}

} // namespace okaytest
