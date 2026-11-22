#pragma once
// ---------------------------------------------------------------------------
// Block compression BC1 / BC2 / BC3 / BC4 / BC5 (okay::tex): our own encoder and decoder.
// Written from the public block layouts (4x4 texels per block; BC1 = two RGB565 endpoints + 2 bit indices, BC4 = two
// 8 bit endpoints + 3 bit indices, BC2 = 16 explicit 4 bit alphas + BC1 colour, BC3 = BC4 alpha + BC1 colour, BC5 = two
// BC4 blocks). In BC2 and BC3 the colour half always uses the four colour palette, whatever the endpoint order (only a
// lone BC1 block switches to three colours + transparent when c0 <= c1). Our own search:
//   - BC1: principal axis of the block's colours gives the first endpoints, then least-squares refinement of the
//     endpoints for the chosen indices, then (high quality) a small +-1 search around each endpoint.
//   - BC4: the 8-value mode on min/max and the 6-value + 0/255 mode on the inner range, both refined by least
//     squares; the lower error wins.
// The interpolated palette colours use integer division that truncates ((2a+b)/3 and so on); real GPUs may differ by
// one level, which does not matter for quality. The decoder is the reference the encoder measures itself against.
// ---------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>
#include <vector>

namespace okay::tex {

enum class BcFormat { BC1, BC3, BC4, BC5, BC2 };   // BC2 (DXT3) last: added later, the older values keep their numbers

int         BcBlockBytes(BcFormat f);                              ///< 8 for BC1 / BC4, 16 for BC2 / BC3 / BC5
const char* BcName(BcFormat f);
bool        ParseBcFormat(const char* name, BcFormat* out);        ///< bc1 bc2 bc3 bc4 bc5 (any case)
std::size_t BcImageBytes(BcFormat f, int w, int h);                ///< one level, edge blocks included; 0 for bad size
float       BcBytesPerPixel(BcFormat f);                           ///< 0.5 for BC1 / BC4, 1 for BC2 / BC3 / BC5 (for MipBudget)

struct BcEncodeOptions {
    int quality = 1;            ///< 0 fast (axis fit only), 1 normal (+ least squares), 2 high (+ endpoint search)
    int bc1AlphaCutoff = 0;     ///< BC1 only: 0 = opaque; else texels with alpha below this become transparent (1 bit alpha)
};

/// Compress an RGBA8 image. BC1, BC2 and BC3 use RGB (BC2 and BC3 also alpha), BC4 uses red, BC5 uses red and green.
/// BC2 alpha is the nearest of 16 levels (a * 15 / 255 rounded).
/// Empty result for null input or a bad size.
std::vector<std::uint8_t> EncodeBc(const std::uint8_t* rgba, int w, int h, BcFormat f,
                                   const BcEncodeOptions& opt = BcEncodeOptions());
/// Decompress to RGBA8. BC4 gives (v,0,0,255), BC5 (r,g,0,255), BC1 transparent texels (0,0,0,0).
/// Empty result when `size` is smaller than BcImageBytes.
std::vector<std::uint8_t> DecodeBc(const std::uint8_t* data, std::size_t size, int w, int h, BcFormat f);

// One block at a time (exposed for tests and tools). Texels are row-major, 16 per block.
void EncodeBc1Block(const std::uint8_t rgba[64], std::uint8_t out[8], const BcEncodeOptions& opt = BcEncodeOptions());
void DecodeBc1Block(const std::uint8_t in[8], std::uint8_t rgba[64]);
/// The colour half of a BC2 / BC3 block: always the four colour palette, alpha 255.
void DecodeBcColorBlock(const std::uint8_t in[8], std::uint8_t rgba[64]);
/// BC2 alpha half: 16 four bit values, low nibble first, scaled by 17.
void DecodeBc2AlphaBlock(const std::uint8_t in[8], std::uint8_t a[16]);
void EncodeBc4Block(const std::uint8_t v[16], std::uint8_t out[8], const BcEncodeOptions& opt = BcEncodeOptions());
void DecodeBc4Block(const std::uint8_t in[8], std::uint8_t v[16]);

} // namespace okay::tex
