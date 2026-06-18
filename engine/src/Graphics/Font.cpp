#include "okay/Graphics/Font.hpp"

#include <cstring>

// The vendored public-domain 8x8 font, included in exactly one translation unit.
#include "font8x8_basic.h"

namespace okay {

const unsigned char* Font8x8::Glyph(char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (uc >= 128) uc = 0; // out of basic-latin range -> blank
    return reinterpret_cast<const unsigned char*>(font8x8_basic[uc]);
}

int Font8x8::MeasureWidth(const char* text) {
    return text ? static_cast<int>(std::strlen(text)) * Width : 0;
}

} // namespace okay
