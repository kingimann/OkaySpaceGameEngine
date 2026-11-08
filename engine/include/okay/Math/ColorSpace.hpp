#pragma once
// Colour-space helpers on `Color` (components 0..1, assumed sRGB-encoded unless a
// function says "Linear"): sRGB <-> linear, HSV, HSL, hex parse / format, and
// perceptually-correct blending in linear light. Alpha is never gamma-converted.
#include "okay/Render/Color.hpp"
#include <cstdint>
#include <string>

namespace okay {

struct Hsv { float h = 0, s = 0, v = 0; };   // h in degrees [0, 360), s and v in [0, 1]
struct Hsl { float h = 0, s = 0, l = 0; };   // h in degrees [0, 360), s and l in [0, 1]

struct ColorSpace {
    // ---- sRGB transfer function (IEC 61966-2-1) ----
    static float SrgbToLinear(float c) {
        c = Mathf::Clamp01(c);
        return c <= 0.04045f ? c / 12.92f : Mathf::Pow((c + 0.055f) / 1.055f, 2.4f);
    }
    static float LinearToSrgb(float c) {
        c = Mathf::Clamp01(c);
        return c <= 0.0031308f ? c * 12.92f : 1.055f * Mathf::Pow(c, 1.0f / 2.4f) - 0.055f;
    }
    /// sRGB-encoded colour -> linear light (alpha unchanged).
    static Color ToLinear(const Color& c) { return {SrgbToLinear(c.r), SrgbToLinear(c.g), SrgbToLinear(c.b), c.a}; }
    static Color ToSrgb(const Color& c)   { return {LinearToSrgb(c.r), LinearToSrgb(c.g), LinearToSrgb(c.b), c.a}; }
    /// Blend two sRGB-encoded colours in linear light (no dark midpoint banding).
    /// Alpha is interpolated linearly. t is clamped to [0, 1].
    static Color LerpLinear(const Color& a, const Color& b, float t) {
        return ToSrgb(Color::Lerp(ToLinear(a), ToLinear(b), t));
    }

    // ---- HSV / HSL ----
    static Hsv ToHsv(const Color& c) {
        float mx = Mathf::Max(c.r, Mathf::Max(c.g, c.b)), mn = Mathf::Min(c.r, Mathf::Min(c.g, c.b));
        float d = mx - mn;
        Hsv o; o.v = mx; o.s = mx > 0.0f ? d / mx : 0.0f; o.h = HueOf(c, mx, d);
        return o;
    }
    static Color FromHsv(float h, float s, float v, float a = 1.0f) {
        s = Mathf::Clamp01(s); v = Mathf::Clamp01(v);
        float cc = v * s;
        return FromHueChroma(h, cc, v - cc, a);
    }
    static Color FromHsv(const Hsv& c, float a = 1.0f) { return FromHsv(c.h, c.s, c.v, a); }

    static Hsl ToHsl(const Color& c) {
        float mx = Mathf::Max(c.r, Mathf::Max(c.g, c.b)), mn = Mathf::Min(c.r, Mathf::Min(c.g, c.b));
        float d = mx - mn;
        Hsl o; o.l = (mx + mn) * 0.5f;
        o.s = d <= 0.0f ? 0.0f : d / (1.0f - Mathf::Abs(2.0f * o.l - 1.0f));
        o.h = HueOf(c, mx, d);
        return o;
    }
    static Color FromHsl(float h, float s, float l, float a = 1.0f) {
        s = Mathf::Clamp01(s); l = Mathf::Clamp01(l);
        float cc = (1.0f - Mathf::Abs(2.0f * l - 1.0f)) * s;
        return FromHueChroma(h, cc, l - cc * 0.5f, a);
    }
    static Color FromHsl(const Hsl& c, float a = 1.0f) { return FromHsl(c.h, c.s, c.l, a); }

    // ---- hex ----
    /// Parse "#RGB", "#RGBA", "#RRGGBB" or "#RRGGBBAA" (the leading '#' or "0x" is
    /// optional; case-insensitive). The bytes are taken as sRGB values. On failure
    /// returns false and leaves `out` untouched.
    static bool ParseHex(const std::string& text, Color& out) {
        std::size_t i = 0, n = text.size();
        while (i < n && (text[i] == ' ' || text[i] == '\t')) ++i;
        while (n > i && (text[n - 1] == ' ' || text[n - 1] == '\t' || text[n - 1] == '\r' || text[n - 1] == '\n')) --n;
        if (i < n && text[i] == '#') ++i;
        else if (n - i >= 2 && text[i] == '0' && (text[i + 1] == 'x' || text[i + 1] == 'X')) i += 2;
        std::size_t len = n - i;
        if (len != 3 && len != 4 && len != 6 && len != 8) return false;
        int v[8];
        for (std::size_t k = 0; k < len; ++k) {
            char ch = text[i + k]; int d;
            if (ch >= '0' && ch <= '9') d = ch - '0';
            else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
            else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
            else return false;
            v[k] = d;
        }
        int ch[4] = {255, 255, 255, 255};
        if (len <= 4) { for (std::size_t k = 0; k < len; ++k) ch[k] = v[k] * 17; }
        else          { for (std::size_t k = 0; k < len / 2; ++k) ch[k] = v[2 * k] * 16 + v[2 * k + 1]; }
        out = Color::FromBytes(ch[0], ch[1], ch[2], ch[3]);
        return true;
    }
    /// "#RRGGBB" or, with `withAlpha`, "#RRGGBBAA" (components clamped, rounded to bytes).
    static std::string ToHex(const Color& c, bool withAlpha = false) {
        static const char* hx = "0123456789ABCDEF";
        std::string s = "#";
        float comp[4] = {c.r, c.g, c.b, c.a};
        for (int k = 0; k < (withAlpha ? 4 : 3); ++k) {
            int b = (int)(Mathf::Clamp01(comp[k]) * 255.0f + 0.5f);
            s += hx[b >> 4]; s += hx[b & 15];
        }
        return s;
    }

private:
    static float HueOf(const Color& c, float mx, float d) {
        if (d <= 0.0f) return 0.0f;
        float h;
        if (mx == c.r)      h = (c.g - c.b) / d;
        else if (mx == c.g) h = (c.b - c.r) / d + 2.0f;
        else                h = (c.r - c.g) / d + 4.0f;
        h *= 60.0f;
        return h < 0.0f ? h + 360.0f : (h >= 360.0f ? h - 360.0f : h);
    }
    static Color FromHueChroma(float h, float chroma, float m, float a) {
        h = Mathf::Repeat(h, 360.0f) / 60.0f;
        float x = chroma * (1.0f - Mathf::Abs(Mathf::Repeat(h, 2.0f) - 1.0f));
        float r = 0, g = 0, b = 0;
        int sector = (int)h;
        switch (sector) {
            case 0: r = chroma; g = x; break;
            case 1: r = x; g = chroma; break;
            case 2: g = chroma; b = x; break;
            case 3: g = x; b = chroma; break;
            case 4: r = x; b = chroma; break;
            default: r = chroma; b = x; break;
        }
        return {r + m, g + m, b + m, a};
    }
};

} // namespace okay
