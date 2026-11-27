#pragma once
// CSS colour values: "#rgb", "#rgba", "#rrggbb", "#rrggbbaa", rgb() / rgba() and hsl() / hsla() (comma or space
// separated, an optional "/ alpha"), "transparent" and the named colours of CSS Color Module Level 4. The one colour
// parser of the engine: the browser's CSS (okay::browser::ParseColor in Browser/Css.cpp) and the SVG renderer
// (Core/ImageDecodeSvg.cpp) both call it. Header-only and standard-library only (plus Core/StringUtil.hpp), so the
// std-only image decoder can use it. "currentColor" is the caller's business. Written from the CSS Color
// specification (the named colours are its table of keywords).
#include "okay/Core/StringUtil.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace okay {
namespace csscolor {

struct Rgba { std::uint8_t r = 0, g = 0, b = 0, a = 255; };

struct Named { const char* name; std::uint32_t rgb; };
inline constexpr Named kNamed[] = {

    {"black", 0x000000}, {"silver", 0xC0C0C0}, {"gray", 0x808080}, {"grey", 0x808080}, {"white", 0xFFFFFF}, {"maroon", 0x800000},
    {"red", 0xFF0000}, {"purple", 0x800080}, {"fuchsia", 0xFF00FF}, {"magenta", 0xFF00FF}, {"green", 0x008000}, {"lime", 0x00FF00},
    {"olive", 0x808000}, {"yellow", 0xFFFF00}, {"navy", 0x000080}, {"blue", 0x0000FF}, {"teal", 0x008080}, {"aqua", 0x00FFFF},
    {"cyan", 0x00FFFF}, {"orange", 0xFFA500}, {"pink", 0xFFC0CB}, {"brown", 0xA52A2A}, {"gold", 0xFFD700}, {"indigo", 0x4B0082},
    {"violet", 0xEE82EE}, {"crimson", 0xDC143C}, {"coral", 0xFF7F50}, {"salmon", 0xFA8072}, {"tomato", 0xFF6347},
    {"orchid", 0xDA70D6}, {"khaki", 0xF0E68C}, {"beige", 0xF5F5DC}, {"ivory", 0xFFFFF0}, {"lavender", 0xE6E6FA},
    {"tan", 0xD2B48C}, {"chocolate", 0xD2691E}, {"turquoise", 0x40E0D0}, {"skyblue", 0x87CEEB}, {"steelblue", 0x4682B4},
    {"royalblue", 0x4169E1}, {"dodgerblue", 0x1E90FF}, {"slategray", 0x708090}, {"slategrey", 0x708090},
    {"darkgray", 0xA9A9A9}, {"darkgrey", 0xA9A9A9}, {"lightgray", 0xD3D3D3}, {"lightgrey", 0xD3D3D3}, {"gainsboro", 0xDCDCDC},
    {"whitesmoke", 0xF5F5F5}, {"dimgray", 0x696969}, {"dimgrey", 0x696969}, {"darkred", 0x8B0000}, {"darkgreen", 0x006400},
    {"darkblue", 0x00008B}, {"darkorange", 0xFF8C00}, {"lightblue", 0xADD8E6}, {"lightgreen", 0x90EE90},
    {"lightyellow", 0xFFFFE0}, {"limegreen", 0x32CD32}, {"forestgreen", 0x228B22}, {"seagreen", 0x2E8B57}, {"midnightblue", 0x191970},
    {"firebrick", 0xB22222}, {"goldenrod", 0xDAA520}, {"plum", 0xDDA0DD}, {"rebeccapurple", 0x663399}, {"aliceblue", 0xF0F8FF},
    {"ghostwhite", 0xF8F8FF}, {"snow", 0xFFFAFA}, {"linen", 0xFAF0E6}, {"mintcream", 0xF5FFFA}, {"honeydew", 0xF0FFF0},
    {"azure", 0xF0FFFF}, {"cornflowerblue", 0x6495ED}, {"darkslategray", 0x2F4F4F}, {"darkslategrey", 0x2F4F4F},
    {"lightslategray", 0x778899}, {"hotpink", 0xFF69B4}, {"deeppink", 0xFF1493}, {"chartreuse", 0x7FFF00}, {"olivedrab", 0x6B8E23},
    {"sienna", 0xA0522D}, {"peru", 0xCD853F}, {"wheat", 0xF5DEB3}, {"moccasin", 0xFFE4B5}, {"navajowhite", 0xFFDEAD},
    {"darkcyan", 0x008B8B}, {"darkmagenta", 0x8B008B}, {"darkviolet", 0x9400D3}, {"darkkhaki", 0xBDB76B}, {"lightcoral", 0xF08080},
    {"lightpink", 0xFFB6C1}, {"lightsalmon", 0xFFA07A}, {"lightseagreen", 0x20B2AA}, {"lightskyblue", 0x87CEFA},
    {"lightsteelblue", 0xB0C4DE}, {"mediumblue", 0x0000CD}, {"mediumseagreen", 0x3CB371}, {"mediumpurple", 0x9370DB},
    {"palegreen", 0x98FB98}, {"paleturquoise", 0xAFEEEE}, {"papayawhip", 0xFFEFD5}, {"peachpuff", 0xFFDAB9},
    {"powderblue", 0xB0E0E6}, {"seashell", 0xFFF5EE}, {"springgreen", 0x00FF7F}, {"thistle", 0xD8BFD8}, {"oldlace", 0xFDF5E6},
    {"cornsilk", 0xFFF8DC}, {"lemonchiffon", 0xFFFACD}, {"mistyrose", 0xFFE4E1}, {"blanchedalmond", 0xFFEBCD},
    {"bisque", 0xFFE4C4}, {"floralwhite", 0xFFFAF0}, {"antiquewhite", 0xFAEBD7}, {"darkslateblue", 0x483D8B},
    {"slateblue", 0x6A5ACD}, {"mediumslateblue", 0x7B68EE}, {"cadetblue", 0x5F9EA0}, {"darkturquoise", 0x00CED1},
    {"deepskyblue", 0x00BFFF}, {"mediumturquoise", 0x48D1CC}, {"aquamarine", 0x7FFFD4}, {"mediumaquamarine", 0x66CDAA},
    {"darkseagreen", 0x8FBC8F}, {"greenyellow", 0xADFF2F}, {"lawngreen", 0x7CFC00}, {"yellowgreen", 0x9ACD32},
    {"darkolivegreen", 0x556B2F}, {"saddlebrown", 0x8B4513}, {"sandybrown", 0xF4A460}, {"rosybrown", 0xBC8F8F},
    {"indianred", 0xCD5C5C}, {"orangered", 0xFF4500}, {"palevioletred", 0xDB7093}, {"mediumvioletred", 0xC71585},
    {"blueviolet", 0x8A2BE2}, {"darkorchid", 0x9932CC}, {"mediumorchid", 0xBA55D3}, {"lightcyan", 0xE0FFFF},
    {"lightgoldenrodyellow", 0xFAFAD2}, {"palegoldenrod", 0xEEE8AA}, {"darkgoldenrod", 0xB8860B}, {"burlywood", 0xDEB887},
    {"lavenderblush", 0xFFF0F5}, {"mediumspringgreen", 0x00FA9A}, {"lightslategrey", 0x778899},
};

namespace detail {

inline std::uint8_t ToByte(float v) { return (std::uint8_t)std::lround(std::max(0.0f, std::min(255.0f, v))); }

inline float HueChannel(float lo, float hi, float t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0f / 6) return lo + (hi - lo) * 6 * t;
    if (t < 0.5f) return hi;
    if (t < 2.0f / 3) return lo + (hi - lo) * (2.0f / 3 - t) * 6;
    return lo;
}

// A CSS number with an optional unit: digits only (no "inf", "nan" or hex, which strtod would take), kept within +-1e7.
inline bool NumberWithUnit(const std::string& word, float& value, std::string& unit) {
    if (word.empty()) return false;
    const char* start = word.c_str();
    const char lead = (start[0] == '+' || start[0] == '-') ? start[1] : start[0];
    if (!((lead >= '0' && lead <= '9') || lead == '.')) return false;
    char* stop = nullptr;
    const double d = std::strtod(start, &stop);
    if (stop == start || !std::isfinite(d)) return false;
    for (const char* c = start; c < stop; ++c) if (*c == 'x' || *c == 'X') return false;
    value = (float)std::max(-1e7, std::min(1e7, d));
    unit = str::LowerAscii(std::string(stop));
    return true;
}

// The arguments between the brackets of a colour function, split at white space, ',' and '/' (which are dropped).
// A nested bracket or a quote can never be part of a valid number, so it needs no special case: the colour fails.
inline std::vector<std::string> Arguments(const std::string& inner) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < inner.size()) {
        const std::size_t start = inner.find_first_not_of(" \t\n\r\f,/", i);
        if (start == std::string::npos) break;
        std::size_t stop = inner.find_first_of(" \t\n\r\f,/", start);
        if (stop == std::string::npos) stop = inner.size();
        out.push_back(inner.substr(start, stop - start));
        i = stop;
    }
    return out;
}

} // namespace detail

/// A colour value; false (and `out` unchanged) when `text` is not one.
inline bool Parse(const std::string& textIn, Rgba& out) {
    const std::string text = str::LowerAscii(str::Trim(textIn));
    if (text.empty()) return false;
    if (text[0] == '#') {
        const std::size_t n = text.size() - 1;
        int h[8];
        if (n != 3 && n != 4 && n != 6 && n != 8) return false;
        for (std::size_t i = 0; i < n; ++i) if ((h[i] = str::HexDigitValue(text[i + 1])) < 0) return false;
        Rgba c;
        if (n <= 4) {
            c.r = (std::uint8_t)(h[0] * 17); c.g = (std::uint8_t)(h[1] * 17); c.b = (std::uint8_t)(h[2] * 17);
            c.a = n == 4 ? (std::uint8_t)(h[3] * 17) : 255;
        } else {
            c.r = (std::uint8_t)(h[0] * 16 + h[1]); c.g = (std::uint8_t)(h[2] * 16 + h[3]); c.b = (std::uint8_t)(h[4] * 16 + h[5]);
            c.a = n == 8 ? (std::uint8_t)(h[6] * 16 + h[7]) : 255;
        }
        out = c;
        return true;
    }
    const bool isRgb = text.compare(0, 4, "rgb(") == 0 || text.compare(0, 5, "rgba(") == 0;
    const bool isHsl = text.compare(0, 4, "hsl(") == 0 || text.compare(0, 5, "hsla(") == 0;
    if (isRgb || isHsl) {
        const std::size_t open = text.find('('), close = text.rfind(')');
        if (close == std::string::npos || close < open) return false;
        const std::vector<std::string> args = detail::Arguments(text.substr(open + 1, close - open - 1));
        if (args.size() != 3 && args.size() != 4) return false;
        float ch[4] = {0, 0, 0, 255};
        for (std::size_t k = 0; k < args.size(); ++k) {
            float num = 0;
            std::string unit;
            if (!detail::NumberWithUnit(args[k], num, unit)) return false;
            if (k == 3) {   // alpha: a number 0..1 or a percentage
                if (unit == "%") ch[3] = num * 255.0f / 100.0f;
                else if (unit.empty()) ch[3] = num * 255.0f;
                else return false;
                if (ch[3] < -0.5f) return false;   // a clearly negative alpha is not a colour
                continue;
            }
            if (isRgb) {
                if (unit == "%") ch[k] = num * 255.0f / 100.0f;
                else if (unit.empty()) ch[k] = num;
                else return false;
            } else if (k == 0) {
                if (!(unit.empty() || unit == "deg")) return false;
                ch[0] = num;
            } else {
                if (!(unit == "%" || unit.empty())) return false;
                ch[k] = num / 100.0f;
            }
        }
        if (isHsl) {
            const float hue = std::fmod(std::fmod(ch[0], 360.0f) + 360.0f, 360.0f) / 360.0f;
            const float s = std::max(0.0f, std::min(1.0f, ch[1])), l = std::max(0.0f, std::min(1.0f, ch[2]));
            const float hi = l < 0.5f ? l * (1 + s) : l + s - l * s, lo = 2 * l - hi;
            ch[0] = detail::HueChannel(lo, hi, hue + 1.0f / 3) * 255.0f;
            ch[1] = detail::HueChannel(lo, hi, hue) * 255.0f;
            ch[2] = detail::HueChannel(lo, hi, hue - 1.0f / 3) * 255.0f;
        }
        out = Rgba{detail::ToByte(ch[0]), detail::ToByte(ch[1]), detail::ToByte(ch[2]), detail::ToByte(ch[3])};
        return true;
    }
    if (text == "transparent") { out = Rgba{0, 0, 0, 0}; return true; }
    for (const Named& n : kNamed) {
        if (text == n.name) {
            out = Rgba{(std::uint8_t)(n.rgb >> 16), (std::uint8_t)((n.rgb >> 8) & 0xFF), (std::uint8_t)(n.rgb & 0xFF), 255};
            return true;
        }
    }
    return false;
}

} // namespace csscolor
} // namespace okay
