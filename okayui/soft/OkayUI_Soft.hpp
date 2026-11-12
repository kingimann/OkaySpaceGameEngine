#pragma once
// OkayUI software backend: rasterises OkayUI::DrawData (the backend-neutral draw list) into an
// RGBA8 bitmap on the CPU. It exists so the toolkit can be tested headlessly (golden images,
// behaviour tests, fuzzing) and so a demo can write a PNG without a window or a GPU. It is not
// meant to be fast, and it is not used by the player or the editor.
//
// What it draws: every DrawCmd with texture == nullptr (flat colour) or texture ==
// DrawData::atlasTexture (the launcher look's glyph atlas, nearest-neighbour sampled; the real
// backend filters it linearly). Commands with any other texture (Image, fallback fonts) belong to
// a GPU and are skipped. Triangles use the top-left fill rule, so shared edges are never drawn
// twice; the scissor rectangle of each command is honoured; blending is source-over.
//
// Hostile data (NaN or huge coordinates, out-of-range indices, bad clip rectangles) is skipped or
// clamped, never trusted.
#include "okay/UI/OkayUI.hpp"

#include <cstdint>
#include <vector>

namespace OkayUI {
namespace soft {

struct Bitmap {
    int w = 0, h = 0;
    std::vector<std::uint8_t> rgba;    // w * h * 4, top row first
    Bitmap() = default;
    Bitmap(int W, int H) : w(W > 0 ? W : 0), h(H > 0 ? H : 0), rgba((std::size_t)(w) * (std::size_t)h * 4, 0) {}
    void Clear(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255);
};

/// Draw `data` into `target` (not cleared first).
void Render(const DrawData& data, Bitmap& target);

} // namespace soft
} // namespace OkayUI
