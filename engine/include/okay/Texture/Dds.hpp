#pragma once
// ---------------------------------------------------------------------------
// DDS container writer and reader (okay::tex), written from the public DDS file layout: the "DDS " magic, a 124 byte
// header (flags, size, mip count, pixel format, caps) and, for BC4 / BC5, the 20 byte DX10 extension header with the
// DXGI format number (BC4_UNORM = 80, BC5_UNORM = 83). BC1 and BC3 use the older FourCC codes "DXT1" and "DXT5" which
// every viewer understands. Mip levels follow each other, largest first. 2D textures only (no cube maps or arrays).
// ---------------------------------------------------------------------------
#include "okay/Texture/Bc.hpp"
#include <string>
#include <vector>

namespace okay::tex {

enum class DdsFormat { RGBA8, BC1, BC3, BC4, BC5 };

struct DdsImage {
    DdsFormat format = DdsFormat::RGBA8;
    int width = 0, height = 0;
    std::vector<std::vector<std::uint8_t>> mips;    ///< level 0 first; level i is the size MipSize(width, height, i)
};

/// Bytes of one level of this format.
std::size_t DdsLevelBytes(DdsFormat f, int w, int h);

/// The file bytes. Empty when the image is inconsistent (bad size, wrong level sizes, more than 16 levels).
std::vector<std::uint8_t> WriteDds(const DdsImage& img);
/// Parse a file (hostile input safe). On failure returns false and fills `error`.
bool ReadDds(const std::uint8_t* data, std::size_t size, DdsImage* out, std::string* error = nullptr);

} // namespace okay::tex
