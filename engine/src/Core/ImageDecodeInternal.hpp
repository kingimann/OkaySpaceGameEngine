#pragma once
// Private to the image decoder translation units.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace okay {
namespace imgdec {

/// JPEG decoder (ImageDecodeJpeg.cpp). With `rgba` null only the header is parsed
/// (width/height are filled). On failure `err` is set and false returned.
bool DecodeJpeg(const std::uint8_t* data, std::size_t size, int* width, int* height,
                std::vector<std::uint8_t>* rgba, std::string& err);

/// WebP decoder (ImageDecodeWebp.cpp): lossless (VP8L), lossy (VP8 key frame) with or without an ALPH chunk, and the
/// first frame of an animation. With `rgba` null only the headers are read.
bool DecodeWebp(const std::uint8_t* data, std::size_t size, int* width, int* height, std::vector<std::uint8_t>* rgba,
                std::string& err);

/// The Y, U and V planes of one VP8 key frame (the payload of a 'VP8 ' chunk), before colour conversion: Y is
/// width x height, U and V are ((width + 1) / 2) x ((height + 1) / 2). For tests that check the decoder bit for bit.
bool DecodeVp8Planes(const std::uint8_t* data, std::size_t size, int& width, int& height, std::vector<std::uint8_t>& y,
                     std::vector<std::uint8_t>& u, std::vector<std::uint8_t>& v, std::string& err);

/// SVG (ImageDecodeSvg.cpp). LooksLikeSvg: XML whose root is <svg> (checked on the first 4 KiB). DecodeSvg renders at
/// reqW x reqH pixels (0: from the picture's own size, keeping its shape); with `rgba` null only the size is worked out.
bool LooksLikeSvg(const std::uint8_t* data, std::size_t size);
bool DecodeSvg(const std::uint8_t* data, std::size_t size, int reqW, int reqH, int* width, int* height, std::vector<std::uint8_t>* rgba,
               std::string& err);

} // namespace imgdec
} // namespace okay
