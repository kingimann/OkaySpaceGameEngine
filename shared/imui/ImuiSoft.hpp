#pragma once
// Software backend for okay::imui: rasterises a DrawData into an RGBA8 bitmap on the CPU. It exists so
// the UI core can be tested headlessly (golden images, behaviour tests) and so a demo can write a PNG
// without a window. It is not meant to be fast.
#include "imui/Imui.hpp"

namespace okay {
namespace imui {

struct Bitmap {
    int w = 0, h = 0;
    std::vector<std::uint8_t> rgba;    // w * h * 4, top row first
    Bitmap() = default;
    Bitmap(int W, int H) : w(W > 0 ? W : 0), h(H > 0 ? H : 0), rgba((std::size_t)(w) * h * 4, 0) {}
    void Clear(Col c);
};

/// A user texture for AddImage (texture ids start at 1; id N is textures[N - 1]).
struct SoftTexture { int w = 0, h = 0; const std::uint8_t* rgba = nullptr; };

/// Draw every list of `data` into `target` (not cleared first). Texture 0 is `atlas`. Non-finite or
/// huge coordinates are skipped, never crash.
void SoftRender(const DrawData& data, const FontAtlas& atlas, Bitmap& target, const std::vector<SoftTexture>* textures = nullptr);

} // namespace imui
} // namespace okay
