#pragma once
// Our own image decoder (no third-party code). Every decode produces tightly packed
// RGBA8, rows top to bottom, so callers never deal with channel counts:
//   PNG   all colour types and bit depths, Adam7 interlace, tRNS, gamma/ICC ignored,
//         16-bit reduced to 8 by keeping the high byte
//   JPEG  baseline, extended-sequential and progressive Huffman, any chroma
//         subsampling, restart markers, greyscale and YCbCr/RGB (Adobe marker aware).
//         CMYK/YCCK and arithmetic-coded or 12-bit files are rejected.
//   BMP   1/4/8-bit palette, 16/24/32-bit incl. bitfield masks, top-down or bottom-up
//         (RLE and embedded JPEG/PNG are rejected)
//   TGA   palette, true colour, greyscale, RLE, either origin
//   GIF   first frame via DecodeImage, all frames (disposal, transparency) via DecodeGifFrames
//   HDR   Radiance RGBE, tone-mapped to 8-bit with gamma 1/2.2 (scale 1)
//   PSD   flattened composite of 8/16-bit RGB files, raw or RLE
// Decoding never reads or writes out of bounds on malformed input: it fails with an error
// string instead. Dimensions are capped at 16384 on either side.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace okay {

enum class ImageFormat { Unknown, PNG, JPEG, BMP, TGA, GIF, HDR, PSD };

struct DecodedImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;   ///< width * height * 4 bytes
};

/// All frames of a GIF, each a full-canvas RGBA8 composite (as a viewer would show it).
struct DecodedAnimation {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> frames;   ///< FrameCount() * width * height * 4 bytes
    std::vector<int> delaysMs;          ///< per frame, milliseconds (0 when the file gives none)
    int FrameCount() const { return static_cast<int>(delaysMs.size()); }
};

/// Largest width or height any decoder accepts.
constexpr int kMaxImageDimension = 16384;

/// Identify a format from its leading bytes (TGA, which has no signature, is recognised by
/// a plausibility check of its header).
ImageFormat DetectImageFormat(const std::uint8_t* data, std::size_t size);
/// Read just the dimensions (and format) from the header. False when unrecognised/invalid.
bool GetImageInfo(const std::uint8_t* data, std::size_t size, int* width, int* height,
                  ImageFormat* format = nullptr);
/// Decode any supported format to RGBA8. On failure returns false, leaves `out` empty and
/// stores a short reason in `*error` when given.
bool DecodeImage(const std::uint8_t* data, std::size_t size, DecodedImage& out, std::string* error = nullptr);
/// Read a whole file and decode it.
bool DecodeImageFile(const std::string& path, DecodedImage& out, std::string* error = nullptr);
/// Header-only dimensions of a file.
bool GetImageFileInfo(const std::string& path, int* width, int* height, ImageFormat* format = nullptr);
/// Decode every frame of a GIF.
bool DecodeGifFrames(const std::uint8_t* data, std::size_t size, DecodedAnimation& out, std::string* error = nullptr);

/// DEFLATE decompression (RFC 1951) with an optional zlib wrapper (RFC 1950; the header is
/// validated, the Adler-32 is not). `maxOut` bounds the output (decompression-bomb guard);
/// exceeding it fails. Returns false on malformed or truncated input.
bool Inflate(const std::uint8_t* data, std::size_t size, bool zlibWrapper, std::vector<std::uint8_t>& out,
             std::size_t maxOut = std::size_t(1) << 31);

} // namespace okay
