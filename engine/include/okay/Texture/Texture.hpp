#pragma once
// ---------------------------------------------------------------------------
// okay::tex - our own texture-processing library (docs/own_texture_library.md).
//
// Common pieces: a float RGBA surface, the sRGB transfer functions, the pixel formats and byte sizes.
// Written from the sRGB standard's piecewise curve (IEC 61966-2-1) and plain arithmetic. No third-party code.
// ---------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>
#include <vector>

namespace okay::tex {

/// sRGB transfer functions on 0..1 values (the piecewise curve with the linear toe).
float SrgbToLinear(float s);
float LinearToSrgb(float l);

/// A picture as float RGBA (4 floats per pixel, row-major, top row first). Straight (not premultiplied) alpha
/// unless a function says otherwise. Colour is linear light unless a function says otherwise.
struct Surface {
    int w = 0, h = 0;
    std::vector<float> px;
    Surface() = default;
    Surface(int width, int height) : w(width), h(height),
        px(static_cast<std::size_t>(width > 0 ? width : 0) * static_cast<std::size_t>(height > 0 ? height : 0) * 4, 0.0f) {}
    bool Valid() const { return w > 0 && h > 0 && px.size() == static_cast<std::size_t>(w) * h * 4; }
    float*       At(int x, int y)       { return &px[(static_cast<std::size_t>(y) * w + x) * 4]; }
    const float* At(int x, int y) const { return &px[(static_cast<std::size_t>(y) * w + x) * 4]; }
};

/// RGBA8 bytes to a float surface. `srgb` decodes the colour channels (alpha is always linear). Null / non-positive
/// sizes give an invalid (empty) surface.
Surface FromRGBA8(const std::uint8_t* rgba, int w, int h, bool srgb);
/// Float surface to RGBA8 bytes (clamped to 0..1, rounded to nearest). `srgb` encodes the colour channels.
std::vector<std::uint8_t> ToRGBA8(const Surface& s, bool srgb);

/// Number of levels in a full chain down to 1x1 (1x1 -> 1, 256x64 -> 9, 5x3 -> 3).
int MipCount(int w, int h);
/// Size of level `level` (each side halves, rounding down, never below 1).
void MipSize(int w, int h, int level, int* outW, int* outH);

/// Peak signal to noise ratio between two byte arrays of `count` bytes, in dB. Identical data gives 99.
double Psnr8(const std::uint8_t* a, const std::uint8_t* b, std::size_t count);

} // namespace okay::tex
