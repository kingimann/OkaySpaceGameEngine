#pragma once
// ---------------------------------------------------------------------------
// Dithered LOD cross-fade (feature map 2.8). While a LOD Group changes level, the
// renderers draw both meshes for a short time: the new level keeps the pixels whose
// screen-door threshold is below `fade` and the old level keeps the others, so every
// pixel is covered by exactly one of the two and the change looks like a dissolve
// instead of a pop. `fade` runs from 0 (all old) to 1 (all new).
//
// The threshold is a 4x4 ordered-dither (Bayer) matrix built from the 2x2 one
// B2(x, y) = (2x + 3y) mod 4 = [[0, 2], [3, 1]]: B4 = 4 * B2(low bits) + B2(high bits),
// which gives each of the 16 values 0..15 exactly once per 4x4 tile. The GLSL and HLSL
// shaders compute the same numbers (GLRenderer.cpp, D3D11Hlsl.hpp); tests/test_lod_fade.cpp
// checks the table and that the two halves never overlap.
// Plain arithmetic, no state.
// ---------------------------------------------------------------------------

namespace okay {

/// 0..15, each value once per 4x4 block of pixels.
inline int LodDitherIndex(int x, int y) {
    const int lx = x & 1, ly = y & 1, hx = (x >> 1) & 1, hy = (y >> 1) & 1;
    return 4 * ((2 * lx + 3 * ly) & 3) + ((2 * hx + 3 * hy) & 3);
}

/// Threshold in (0, 1) for a pixel.
inline float LodDitherThreshold(int x, int y) { return ((float)LodDitherIndex(x, y) + 0.5f) / 16.0f; }

/// Whether a pixel is drawn. `invert` false: the incoming level (kept where the threshold is below `fade`);
/// `invert` true: the outgoing level (the other pixels). fade >= 1 draws everything / nothing respectively.
inline bool LodDitherKeep(int x, int y, float fade, bool invert) {
    const bool low = LodDitherThreshold(x, y) < fade;
    return invert ? !low : low;
}

/// What the software renderer is drawing right now (one per thread: its bands draw in parallel).
struct LodDitherState {
    float fade = 1.0f;     ///< 1 and not inverted = no dithering
    bool invert = false;
    bool Active() const { return invert || fade < 1.0f; }
    bool Keep(int x, int y) const { return LodDitherKeep(x, y, fade, invert); }
};

inline LodDitherState& SoftwareLodDither() { static thread_local LodDitherState s; return s; }

} // namespace okay
