#pragma once
// ---------------------------------------------------------------------------
// Pixel format conversion (okay::tex). Written from the IEEE 754 binary16 layout (1 sign, 5 exponent, 10 fraction
// bits, round to nearest even) and the usual 5-6-5 packing. Every conversion goes through float RGBA, so any pair
// of formats works and a round trip through a wider format is lossless.
//
// Layouts (little endian, rows tightly packed, top row first):
//   RGBA8   bytes R,G,B,A            BGRA8   bytes B,G,R,A
//   RGB565  16 bit: R in bits 15-11, G in 10-5, B in 4-0 (alpha reads as 1)
//   RGBA16F four binary16 values R,G,B,A (8 bytes)
//   R8      one byte (reads as R = v, G = B = 0, A = 1)
// ---------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace okay::tex {

enum class PixelFormat { RGBA8, BGRA8, RGB565, RGBA16F, R8 };

int         BytesPerPixel(PixelFormat f);
const char* FormatName(PixelFormat f);
bool        ParseFormat(const std::string& name, PixelFormat* out);   ///< rgba8 bgra8 rgb565 rgba16f r8 (any case)

std::uint16_t FloatToHalf(float v);       ///< round to nearest even; overflow gives infinity; NaN stays NaN
float         HalfToFloat(std::uint16_t h);

struct ConvertOptions {
    bool premultiply = false;     ///< multiply colour by alpha on the way
    bool unpremultiply = false;   ///< divide colour by alpha on the way (alpha 0 gives black)
    bool lumaToR8 = false;        ///< when writing R8 use Rec.709 luma of RGB instead of the red channel
};

/// Convert a w x h image. Empty result for null input, non-positive size or a size that overflows.
std::vector<std::uint8_t> ConvertPixels(const std::uint8_t* src, PixelFormat from, int w, int h, PixelFormat to,
                                        const ConvertOptions& opt = ConvertOptions());

/// In place on RGBA8: colour = round(colour * alpha / 255), and the reverse (clamped to 255, alpha 0 gives 0).
void PremultiplyRGBA8(std::uint8_t* rgba, std::size_t pixelCount);
void UnpremultiplyRGBA8(std::uint8_t* rgba, std::size_t pixelCount);

/// Rearrange the channels of RGBA8 by a four character recipe: each of 'r','g','b','a' picks that source channel,
/// '0' and '1' give constants. "bgra" swaps red and blue, "rrr1" is grey, "rgb1" drops alpha. Empty if bad recipe.
std::vector<std::uint8_t> SwizzleRGBA8(const std::uint8_t* rgba, std::size_t pixelCount, const std::string& recipe);

/// Pack separate one-byte planes into RGBA8 (a null plane is filled with `fill[i]`), e.g. roughness / metal / AO.
std::vector<std::uint8_t> PackChannelsRGBA8(const std::uint8_t* r, const std::uint8_t* g, const std::uint8_t* b,
                                            const std::uint8_t* a, std::size_t pixelCount,
                                            const std::uint8_t fill[4]);
/// Take one channel (0..3) of RGBA8 out as a plane.
std::vector<std::uint8_t> ExtractChannelRGBA8(const std::uint8_t* rgba, std::size_t pixelCount, int channel);

} // namespace okay::tex
