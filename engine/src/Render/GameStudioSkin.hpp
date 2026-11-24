#pragma once
// The size of an embedded 3D GameStudio skin picture, shared by our own MDL (MDL3 / MDL4 / MDL5 / MDL7) and HMP readers.
// Written from Conitec's published descriptions: the "3D GameStudio Programmer's Manual" (Conitec, May 2001: MDL3 / MDL4 skins
// of type 0, 2 and 3 with the size in the file header) and the "Gamestudio file formats" page of the Gamestudio manual (MDL5
// skins with their own width and height; type 2 = 565 RGB, 3 = 4444 ARGB, 4 = 888 RGB, 5 = 8888 ARGB, 0 = 8-bit palette; 8 is
// added when three mipmaps of half, a quarter and an eighth of the size follow the picture). No other project's code was read.
// Internal: not installed.
#include <cstdint>

namespace okay {
namespace detail {
namespace gsskin {

// Bytes per pixel of a skin type without the mipmap flag; 0 when the type is not one of the documented pixel types.
inline int BytesPerPixel(std::int32_t type) {
    switch (type & ~8) {
        case 0: return (type & 8) ? 0 : 1;   // the 8-bit palette skin cannot have mipmaps (the manual says so)
        case 2: case 3: return 2;
        case 4: return 3;
        case 5: return 4;
        default: return 0;
    }
}

// Pixel bytes of a skin of `w` x `h` (both > 0, the caller caps them) of `type`, mipmaps included. 0 = unknown type.
inline std::uint64_t PixelBytes(std::int32_t type, std::int64_t w, std::int64_t h) {
    if (type < 0 || type > 15) return 0;
    const int bpp = BytesPerPixel(type);
    if (bpp == 0 || w <= 0 || h <= 0) return 0;
    std::uint64_t n = (std::uint64_t)w * (std::uint64_t)h;
    if (type & 8) n += (std::uint64_t)(w / 2) * (std::uint64_t)(h / 2) + (std::uint64_t)(w / 4) * (std::uint64_t)(h / 4) + (std::uint64_t)(w / 8) * (std::uint64_t)(h / 8);
    return n * (std::uint64_t)bpp;
}

} // namespace gsskin
} // namespace detail
} // namespace okay
