// Own BMP writer (replaces SDL_SaveBMP in the screenshot tools). Written from the BMP file format description:
// 14-byte file header, 40-byte BITMAPINFOHEADER, 32 bits per pixel, rows stored bottom-up, bytes B G R A.
#pragma once
#include <cstdint>
#include <cstdio>
#include <vector>

namespace okay::platform {

// RGBA bytes (top row first) -> a complete .bmp file in `out`. False for null pixels or a size that is not positive or too large.
inline bool EncodeBmp32(const std::uint8_t* rgba, int w, int h, std::vector<std::uint8_t>& out) {
    out.clear();
    if (!rgba || w <= 0 || h <= 0 || w > 32768 || h > 32768) return false;
    const std::uint32_t imageSize = (std::uint32_t)w * (std::uint32_t)h * 4u;
    out.assign(54u + imageSize, 0);
    auto put32 = [&](size_t at, std::uint32_t v) { for (int i = 0; i < 4; ++i) out[at + i] = (std::uint8_t)(v >> (8 * i)); };
    out[0] = 'B'; out[1] = 'M';
    put32(2, 54u + imageSize); put32(10, 54); put32(14, 40);
    put32(18, (std::uint32_t)w); put32(22, (std::uint32_t)h);
    out[26] = 1; out[28] = 32; put32(34, imageSize);
    std::uint8_t* dst = out.data() + 54;
    for (int y = h - 1; y >= 0; --y) {
        const std::uint8_t* s = rgba + (size_t)y * (size_t)w * 4;
        for (int x = 0; x < w; ++x, dst += 4) { dst[0] = s[x*4+2]; dst[1] = s[x*4+1]; dst[2] = s[x*4]; dst[3] = s[x*4+3]; }
    }
    return true;
}

inline bool WriteBmp32(const char* path, const std::uint8_t* rgba, int w, int h) {
    std::vector<std::uint8_t> bytes;
    if (!path || !EncodeBmp32(rgba, w, h, bytes)) return false;
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    return std::fclose(f) == 0 && ok;
}


// Surface variant: rows of 0xAARRGGBB words (B,G,R,A in memory) with a pitch, as an SDL ARGB8888 surface holds them.
/// Encodes `height` rows of `width` pixels. `pixels` points at 0xAARRGGBB words (little-endian memory order B,G,R,A);
/// `pitchBytes` is the distance between rows (>= width*4). Returns false on bad arguments.
inline bool EncodeBmp32Bgra(const void* pixels, int width, int height, int pitchBytes, std::vector<uint8_t>& out) {
    if (!pixels || width <= 0 || height <= 0 || pitchBytes < width * 4) return false;
    const uint32_t dataSize = (uint32_t)width * 4u * (uint32_t)height;
    out.assign(54 + (size_t)dataSize, 0);
    auto put32 = [&](size_t o, uint32_t v) { for (int i = 0; i < 4; ++i) out[o + i] = (uint8_t)(v >> (8 * i)); };
    out[0] = 'B'; out[1] = 'M';
    put32(2, 54 + dataSize);
    put32(10, 54);
    put32(14, 40);
    put32(18, (uint32_t)width);
    put32(22, (uint32_t)height);
    out[26] = 1; out[28] = 32;
    put32(34, dataSize);
    const uint8_t* src = static_cast<const uint8_t*>(pixels);
    for (int y = 0; y < height; ++y) {
        const uint8_t* row = src + (size_t)(height - 1 - y) * (size_t)pitchBytes;   // bottom-up
        uint8_t* dst = out.data() + 54 + (size_t)y * (size_t)width * 4;
        for (int x = 0; x < width * 4; ++x) dst[x] = row[x];
    }
    return true;
}

/// Encodes and writes to `path`. Returns false if the arguments are bad or the file cannot be written.
inline bool WriteBmp32Bgra(const char* path, const void* pixels, int width, int height, int pitchBytes) {
    std::vector<uint8_t> b;
    if (!path || !EncodeBmp32Bgra(pixels, width, height, pitchBytes, b)) return false;
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    const bool ok = std::fwrite(b.data(), 1, b.size(), f) == b.size();
    return (std::fclose(f) == 0) && ok;
}

} // namespace okay::platform
