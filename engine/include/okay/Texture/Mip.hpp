#pragma once
// ---------------------------------------------------------------------------
// Mip chain generation (okay::tex). Written from the definitions of the box, Lanczos and Kaiser-windowed sinc
// filters and from the idea of keeping the share of "solid" pixels of a cut-out texture constant down the chain.
//
//  - Filtering is separable and handles any size (non power of two, 1xN, odd sizes).
//  - Box is exact area coverage; Lanczos3 and Kaiser are sampled at texel centres and normalised.
//  - Colour is filtered in linear light when `srgb` is set, and always weighted by alpha (premultiplied) so
//    transparent texels do not bleed their colour into the result.
//  - `alphaCoverage` scales alpha on each level so the fraction of pixels at or above `alphaCutoff` stays equal
//    to level 0 (stops cut-out leaves and fences from fading away in the distance).
//  - `normalMap` decodes RGB as a vector (n * 0.5 + 0.5), filters it and renormalises it on every level.
// ---------------------------------------------------------------------------
#include "okay/Texture/Texture.hpp"

namespace okay::tex {

enum class MipFilter { Box, Lanczos3, Kaiser };
enum class MipWrap { Clamp, Repeat };

struct MipOptions {
    MipFilter filter = MipFilter::Box;
    MipWrap   wrap = MipWrap::Clamp;
    bool  srgb = false;              ///< RGBA8 entry points: colour bytes are sRGB, filter in linear light
    bool  premultipliedInput = false;///< the input colour is already multiplied by alpha (the output stays so)
    bool  alphaCoverage = false;     ///< keep the coverage of alpha >= alphaCutoff the same on every level
    float alphaCutoff = 0.5f;
    bool  normalMap = false;         ///< RGB is a tangent-space normal; renormalise (never sRGB, never premultiplied)
    int   maxLevels = 0;             ///< 0 = down to 1x1
    float kaiserBeta = 4.0f;         ///< Kaiser window shape (larger = smoother, less ringing)
    bool  hdr = false;               ///< do not clamp colour above 1 (float / 16F data); below 0 is always clamped
};

/// One weighted source span for a destination texel of a 1-D resample (exposed for tests).
struct Tap { int index; float weight; };
/// Weights of destination texel `dst` when `srcSize` texels shrink to `dstSize` texels.
std::vector<Tap> ResampleTaps(int srcSize, int dstSize, int dst, MipFilter f, MipWrap wrap, float kaiserBeta = 4.0f);

/// Shrink a linear straight-alpha surface to dstW x dstH.
Surface Resample(const Surface& src, int dstW, int dstH, const MipOptions& opt);

/// Full chain, level 0 first (level 0 is `base` unchanged). `base` is linear, straight alpha (or a normal map).
std::vector<Surface> GenerateMips(const Surface& base, const MipOptions& opt = MipOptions());

/// Whole-chain entry point on bytes: decodes with opt.srgb, builds the chain, encodes again. Level 0 is returned
/// byte-for-byte as given. Empty result for invalid input.
std::vector<std::vector<std::uint8_t>> GenerateMipsRGBA8(const std::uint8_t* rgba, int w, int h,
                                                        const MipOptions& opt = MipOptions());

/// Share of texels with alpha >= cutoff (0..1).
float AlphaCoverage(const Surface& s, float cutoff);

} // namespace okay::tex
