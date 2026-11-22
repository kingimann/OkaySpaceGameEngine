#pragma once
// ---------------------------------------------------------------------------
// DDS container writer and reader (okay::tex), written from the public DDS file layout: the "DDS " magic, a 124 byte
// header (flags, size, mip count, pixel format, caps) and, for BC4 / BC5, the 20 byte DX10 extension header with the
// DXGI format number (BC4_UNORM = 80, BC5_UNORM = 83). BC1, BC2 and BC3 use the older FourCC codes "DXT1", "DXT3" and
// "DXT5" which every viewer understands. Mip levels follow each other, largest first. 2D textures only (no cube maps,
// volumes or arrays).
// The reader also takes: FourCC ATI1 / BC4U / ATI2 / BC5U; DX10 headers with DXGI 71/72 (BC1), 74/75 (BC2), 77/78 (BC3),
// 80 (BC4), 83 (BC5), 28/29 (RGBA8), 87/91 (BGRA8), 88/93 (BGRX8); and uncompressed pixels described by bit masks
// (8, 16, 24 or 32 bits per pixel: RGB with or without alpha, luminance, alpha only), which it expands to RGBA8 (each
// channel scaled to 0..255 with rounding: v * 255 / max). Rows are packed with no padding (pitch = width * bytes per
// pixel), as the format description recommends. Premultiplied DXT2 / DXT4 and signed BC4 / BC5 are refused.
// ---------------------------------------------------------------------------
#include "okay/Texture/Bc.hpp"
#include <string>
#include <vector>

namespace okay::tex {

enum class DdsFormat { RGBA8, BC1, BC3, BC4, BC5, BC2 };   // BC2 last: added later

struct DdsImage {
    DdsFormat format = DdsFormat::RGBA8;
    int width = 0, height = 0;
    std::vector<std::vector<std::uint8_t>> mips;    ///< level 0 first; level i is the size MipSize(width, height, i)
    bool srgb = false;                              ///< read only: the DX10 header named an _SRGB format (the data is the same)
};

/// What a reader may accept from an untrusted file. A file over a limit is refused before anything big is allocated.
struct DdsLimits {
    int maxDimension = 16384;                       ///< widest or tallest level 0
    std::size_t maxBytes = std::size_t(256) << 20;  ///< all levels together, as stored in DdsImage (RGBA8 for expanded pixels)
};

/// The block format of a BC DdsFormat (RGBA8 gives false).
bool DdsBcFormat(DdsFormat f, BcFormat* out);
/// The DdsFormat that stores a block format.
DdsFormat DdsFormatFromBc(BcFormat f);
/// "RGBA8", "BC1" ... for messages and tools.
const char* DdsFormatName(DdsFormat f);

/// Bytes of one level of this format.
std::size_t DdsLevelBytes(DdsFormat f, int w, int h);

/// The file bytes. Empty when the image is inconsistent (bad size, wrong level sizes, more than 16 levels).
std::vector<std::uint8_t> WriteDds(const DdsImage& img);
/// Parse a file (hostile input safe). On failure returns false and fills `error`. A file cut off after the first level
/// keeps the levels that are whole.
bool ReadDds(const std::uint8_t* data, std::size_t size, DdsImage* out, std::string* error = nullptr);
bool ReadDds(const std::uint8_t* data, std::size_t size, DdsImage* out, std::string* error, const DdsLimits& limits);
/// Read a whole file from disk (refused when it is larger than the limits allow) and parse it.
bool ReadDdsFile(const std::string& path, DdsImage* out, std::string* error = nullptr, const DdsLimits& limits = DdsLimits());
/// One level as RGBA8 (block formats go through DecodeBc). False for a missing level.
bool DdsLevelToRGBA8(const DdsImage& img, int level, std::vector<std::uint8_t>* rgba);
/// True when the path ends in ".dds" (any case).
bool IsDdsPath(const std::string& path);

} // namespace okay::tex
