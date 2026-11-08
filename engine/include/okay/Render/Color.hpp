#pragma once
#include "okay/Math/Mathf.hpp"
#include <cstdlib>
#include <string>

namespace okay {

/// RGBA color with components in the [0, 1] range, like `Color`.
struct Color {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

    constexpr Color() = default;
    constexpr Color(float r, float g, float b, float a = 1.0f) : r(r), g(g), b(b), a(a) {}

    static Color FromBytes(int r, int g, int b, int a = 255) {
        return {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
    }

    /// A color from text: "#RGB", "#RRGGBB", "#RRGGBBAA" (the '#' is optional, any
    /// case) or "r,g,b[,a]" as 0-255 bytes (out of range values clamp, they do not
    /// wrap). Spaces around the text or the numbers are ignored. When the text is
    /// not a color, `ok` (if given) is set false and `fallback` returned.
    static Color Parse(const std::string& text, bool* ok = nullptr, const Color& fallback = Color(1, 1, 1, 1)) {
        std::size_t a = 0, b = text.size();
        while (a < b && (text[a] == ' ' || text[a] == '\t' || text[a] == '\r' || text[a] == '\n')) ++a;
        while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\t' || text[b - 1] == '\r' || text[b - 1] == '\n')) --b;
        const std::string s = text.substr(a, b - a);
        auto fail = [&]() { if (ok) *ok = false; return fallback; };
        if (s.empty()) return fail();
        if (s.find(',') == std::string::npos) {
            const std::size_t from = s[0] == '#' ? 1 : 0;
            const std::size_t n = s.size() - from;
            if (n != 3 && n != 4 && n != 6 && n != 8) return fail();
            int v[8];
            for (std::size_t i = 0; i < n; ++i) {
                const char ch = s[from + i];
                if (ch >= '0' && ch <= '9') v[i] = ch - '0';
                else if (ch >= 'a' && ch <= 'f') v[i] = ch - 'a' + 10;
                else if (ch >= 'A' && ch <= 'F') v[i] = ch - 'A' + 10;
                else return fail();
            }
            int c[4] = {255, 255, 255, 255};
            if (n <= 4) { for (std::size_t i = 0; i < n; ++i) c[i] = v[i] * 17; }
            else { for (std::size_t i = 0; i < n / 2; ++i) c[i] = v[2 * i] * 16 + v[2 * i + 1]; }
            if (ok) *ok = true;
            return FromBytes(c[0], c[1], c[2], c[3]);
        }
        int c[4] = {0, 0, 0, 255};
        int count = 0;
        std::size_t pos = 0;
        while (pos <= s.size() && count < 5) {
            std::size_t comma = s.find(',', pos);
            const std::string tok = s.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            char* end = nullptr;
            const long val = std::strtol(tok.c_str(), &end, 10);
            while (end && (*end == ' ' || *end == '\t')) ++end;
            const std::size_t lead = tok.find_first_not_of(" \t");
            if (tok.empty() || lead == std::string::npos || end == tok.c_str() || (end && *end != '\0')) return fail();
            if (count < 4) c[count] = (int)(val < 0 ? 0 : (val > 255 ? 255 : val));
            ++count;
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
        if (count < 3 || count > 4) return fail();
        if (ok) *ok = true;
        return FromBytes(c[0], c[1], c[2], c[3]);
    }

    static Color Lerp(const Color& x, const Color& y, float t) {
        t = Mathf::Clamp01(t);
        return {x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t,
                x.b + (y.b - x.b) * t, x.a + (y.a - x.a) * t};
    }

    /// Perceived luminance (Rec. 601), handy for ASCII rendering.
    float Luminance() const { return 0.299f * r + 0.587f * g + 0.114f * b; }

    static const Color Black;
    static const Color White;
    static const Color Red;
    static const Color Green;
    static const Color Blue;
    static const Color Yellow;
    static const Color Cyan;
    static const Color Magenta;
    static const Color Clear;
};

inline const Color Color::Black   {0, 0, 0, 1};
inline const Color Color::White   {1, 1, 1, 1};
inline const Color Color::Red     {1, 0, 0, 1};
inline const Color Color::Green   {0, 1, 0, 1};
inline const Color Color::Blue    {0, 0, 1, 1};
inline const Color Color::Yellow  {1, 1, 0, 1};
inline const Color Color::Cyan    {0, 1, 1, 1};
inline const Color Color::Magenta {1, 0, 1, 1};
inline const Color Color::Clear   {0, 0, 0, 0};

} // namespace okay
