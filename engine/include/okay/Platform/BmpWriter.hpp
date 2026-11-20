// Writes a 32-bit BMP picture file. Written from the BMP file format description (file header, BITMAPINFOHEADER,
// bottom-up BGRA rows); no outside code. Replaces SDL_SaveBMP in the dev picture tools.
#pragma once
#include <cstdint>
#include <cstdio>
#include <vector>

namespace okay::platform {

/// Encodes `height` rows of `width` pixels. `pixels` points at 0xAARRGGBB words (little-endian memory order B,G,R,A);
/// `pitchBytes` is the distance between rows (>= width*4). Returns false on bad arguments.
inline bool EncodeBmp32(const void* pixels, int width, int height, int pitchBytes, std::vector<uint8_t>& out) {
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
inline bool WriteBmp32(const char* path, const void* pixels, int width, int height, int pitchBytes) {
    std::vector<uint8_t> b;
    if (!path || !EncodeBmp32(pixels, width, height, pitchBytes, b)) return false;
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    const bool ok = std::fwrite(b.data(), 1, b.size(), f) == b.size();
    return (std::fclose(f) == 0) && ok;
}

}  // namespace okay::platform
