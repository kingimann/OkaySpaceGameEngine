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

} // namespace imgdec
} // namespace okay
