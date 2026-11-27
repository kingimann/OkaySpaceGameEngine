#include "okay/Graphics/TtfFont.hpp"
#include "EmbeddedFonts.hpp"    // the built-in fonts, decoded once from our own OKF1 container (shared/)

namespace okay {

// The built-in UI font (Roboto Medium) as TrueType bytes. False only if the embedded data were damaged
// (tests/test_embedded_font.cpp pins the decoded bytes, so that cannot ship unnoticed).
bool EmbeddedUIFontData(std::vector<unsigned char>& out) {
    out = okf::RobotoMediumTtf();
    return !out.empty();
}

} // namespace okay
