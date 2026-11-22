// okay::tex pixel format conversion, written from the binary16 layout and plain packing rules (see Convert.hpp).
#include "okay/Texture/Convert.hpp"
#include <cctype>
#include <cmath>
#include <cstring>

namespace okay::tex {

int BytesPerPixel(PixelFormat f) {
    switch (f) {
        case PixelFormat::RGBA8: case PixelFormat::BGRA8: return 4;
        case PixelFormat::RGB565: return 2;
        case PixelFormat::RGBA16F: return 8;
        case PixelFormat::R8: return 1;
    }
    return 0;
}

const char* FormatName(PixelFormat f) {
    switch (f) {
        case PixelFormat::RGBA8: return "rgba8";
        case PixelFormat::BGRA8: return "bgra8";
        case PixelFormat::RGB565: return "rgb565";
        case PixelFormat::RGBA16F: return "rgba16f";
        case PixelFormat::R8: return "r8";
    }
    return "?";
}

bool ParseFormat(const std::string& name, PixelFormat* out) {
    std::string n;
    for (char c : name) n += char(std::tolower(static_cast<unsigned char>(c)));
    static const PixelFormat all[] = {PixelFormat::RGBA8, PixelFormat::BGRA8, PixelFormat::RGB565, PixelFormat::RGBA16F, PixelFormat::R8};
    for (PixelFormat f : all) if (n == FormatName(f)) { if (out) *out = f; return true; }
    return false;
}

std::uint16_t FloatToHalf(float v) {
    std::uint32_t b;
    std::memcpy(&b, &v, 4);
    const std::uint32_t sign = (b >> 16) & 0x8000u;
    const std::uint32_t expo = (b >> 23) & 0xFFu;
    std::uint32_t frac = b & 0x7FFFFFu;
    if (expo == 0xFF) return std::uint16_t(sign | 0x7C00u | (frac ? 0x200u : 0u));   // inf / NaN
    int e = int(expo) - 127 + 15;
    if (e >= 31) return std::uint16_t(sign | 0x7C00u);                                // overflow -> inf
    if (e <= 0) {
        if (e < -10) return std::uint16_t(sign);                                      // too small even for a subnormal
        frac |= 0x800000u;                                                            // implicit leading one
        int shift = 14 - e;                                                           // 14..24
        std::uint32_t half = frac >> shift;
        std::uint32_t rem = frac & ((1u << shift) - 1u), mid = 1u << (shift - 1);
        if (rem > mid || (rem == mid && (half & 1u))) ++half;                         // round to nearest even
        return std::uint16_t(sign | half);
    }
    std::uint32_t half = (std::uint32_t(e) << 10) | (frac >> 13);
    std::uint32_t rem = frac & 0x1FFFu;
    if (rem > 0x1000u || (rem == 0x1000u && (half & 1u))) ++half;                     // may carry into the exponent: correct
    return std::uint16_t(sign | half);
}

float HalfToFloat(std::uint16_t h) {
    const std::uint32_t sign = (std::uint32_t(h) & 0x8000u) << 16;
    std::uint32_t expo = (h >> 10) & 0x1Fu, frac = h & 0x3FFu, bits;
    if (expo == 0) {
        if (frac == 0) bits = sign;
        else {   // subnormal: value = frac * 2^-24
            float f = std::ldexp(float(frac), -24);
            std::uint32_t fb; std::memcpy(&fb, &f, 4);
            bits = sign | fb;
        }
    } else if (expo == 31) bits = sign | 0x7F800000u | (frac << 13);
    else bits = sign | ((expo - 15 + 127) << 23) | (frac << 13);
    float out; std::memcpy(&out, &bits, 4);
    return out;
}

namespace {

struct F4 { float r, g, b, a; };

inline float Sat(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline std::uint8_t Q8(float v) { return std::uint8_t(std::floor(Sat(v) * 255.0f + 0.5f)); }

F4 Decode(const std::uint8_t* p, PixelFormat f) {
    switch (f) {
        case PixelFormat::RGBA8: return {p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, p[3] / 255.0f};
        case PixelFormat::BGRA8: return {p[2] / 255.0f, p[1] / 255.0f, p[0] / 255.0f, p[3] / 255.0f};
        case PixelFormat::RGB565: {
            unsigned v = p[0] | (unsigned(p[1]) << 8);
            return {((v >> 11) & 31) / 31.0f, ((v >> 5) & 63) / 63.0f, (v & 31) / 31.0f, 1.0f};
        }
        case PixelFormat::RGBA16F: {
            auto h = [&](int i) { return HalfToFloat(std::uint16_t(p[i * 2] | (unsigned(p[i * 2 + 1]) << 8))); };
            return {h(0), h(1), h(2), h(3)};
        }
        case PixelFormat::R8: return {p[0] / 255.0f, 0.0f, 0.0f, 1.0f};
    }
    return {0, 0, 0, 0};
}

void Encode(std::uint8_t* p, PixelFormat f, F4 c, bool luma) {
    switch (f) {
        case PixelFormat::RGBA8: p[0] = Q8(c.r); p[1] = Q8(c.g); p[2] = Q8(c.b); p[3] = Q8(c.a); break;
        case PixelFormat::BGRA8: p[0] = Q8(c.b); p[1] = Q8(c.g); p[2] = Q8(c.r); p[3] = Q8(c.a); break;
        case PixelFormat::RGB565: {
            unsigned r = unsigned(std::floor(Sat(c.r) * 31.0f + 0.5f)), g = unsigned(std::floor(Sat(c.g) * 63.0f + 0.5f)),
                     b = unsigned(std::floor(Sat(c.b) * 31.0f + 0.5f));
            unsigned v = (r << 11) | (g << 5) | b;
            p[0] = std::uint8_t(v & 255); p[1] = std::uint8_t(v >> 8);
            break;
        }
        case PixelFormat::RGBA16F: {
            float v[4] = {c.r, c.g, c.b, c.a};
            for (int i = 0; i < 4; ++i) { std::uint16_t h = FloatToHalf(v[i]); p[i * 2] = std::uint8_t(h & 255); p[i * 2 + 1] = std::uint8_t(h >> 8); }
            break;
        }
        case PixelFormat::R8: p[0] = Q8(luma ? 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b : c.r); break;
    }
}

} // namespace

std::vector<std::uint8_t> ConvertPixels(const std::uint8_t* src, PixelFormat from, int w, int h, PixelFormat to,
                                        const ConvertOptions& opt) {
    std::vector<std::uint8_t> out;
    if (!src || w <= 0 || h <= 0) return out;
    const std::size_t n = std::size_t(w) * std::size_t(h);
    if (n / std::size_t(w) != std::size_t(h) || n > (std::size_t(1) << 30)) return out;
    const int sb = BytesPerPixel(from), db = BytesPerPixel(to);
    out.resize(n * db);
    for (std::size_t i = 0; i < n; ++i) {
        F4 c = Decode(src + i * sb, from);
        if (opt.premultiply) { c.r *= c.a; c.g *= c.a; c.b *= c.a; }
        else if (opt.unpremultiply) {
            if (c.a > 0.0f) { c.r /= c.a; c.g /= c.a; c.b /= c.a; } else { c.r = c.g = c.b = 0.0f; }
        }
        Encode(&out[i * db], to, c, opt.lumaToR8);
    }
    return out;
}

void PremultiplyRGBA8(std::uint8_t* p, std::size_t n) {
    if (!p) return;
    for (std::size_t i = 0; i < n; ++i, p += 4)
        for (int c = 0; c < 3; ++c) p[c] = std::uint8_t((unsigned(p[c]) * p[3] + 127u) / 255u);
}

void UnpremultiplyRGBA8(std::uint8_t* p, std::size_t n) {
    if (!p) return;
    for (std::size_t i = 0; i < n; ++i, p += 4)
        for (int c = 0; c < 3; ++c) {
            if (p[3] == 0) { p[c] = 0; continue; }
            unsigned v = (unsigned(p[c]) * 255u + p[3] / 2u) / p[3];
            p[c] = std::uint8_t(v > 255u ? 255u : v);
        }
}

std::vector<std::uint8_t> SwizzleRGBA8(const std::uint8_t* rgba, std::size_t n, const std::string& recipe) {
    std::vector<std::uint8_t> out;
    if (!rgba || recipe.size() != 4) return out;
    int src[4];
    for (int i = 0; i < 4; ++i) {
        switch (std::tolower(static_cast<unsigned char>(recipe[i]))) {
            case 'r': src[i] = 0; break; case 'g': src[i] = 1; break; case 'b': src[i] = 2; break; case 'a': src[i] = 3; break;
            case '0': src[i] = -1; break; case '1': src[i] = -2; break;
            default: return out;
        }
    }
    out.resize(n * 4);
    for (std::size_t i = 0; i < n; ++i)
        for (int c = 0; c < 4; ++c)
            out[i * 4 + c] = src[c] >= 0 ? rgba[i * 4 + src[c]] : (src[c] == -1 ? 0 : 255);
    return out;
}

std::vector<std::uint8_t> PackChannelsRGBA8(const std::uint8_t* r, const std::uint8_t* g, const std::uint8_t* b,
                                            const std::uint8_t* a, std::size_t n, const std::uint8_t fill[4]) {
    std::vector<std::uint8_t> out(n * 4);
    const std::uint8_t* pl[4] = {r, g, b, a};
    for (std::size_t i = 0; i < n; ++i)
        for (int c = 0; c < 4; ++c) out[i * 4 + c] = pl[c] ? pl[c][i] : (fill ? fill[c] : 0);
    return out;
}

std::vector<std::uint8_t> ExtractChannelRGBA8(const std::uint8_t* rgba, std::size_t n, int ch) {
    std::vector<std::uint8_t> out;
    if (!rgba || ch < 0 || ch > 3) return out;
    out.resize(n);
    for (std::size_t i = 0; i < n; ++i) out[i] = rgba[i * 4 + ch];
    return out;
}

} // namespace okay::tex
