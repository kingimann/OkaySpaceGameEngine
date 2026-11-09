#include "okay/Graphics/ColorVision.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace okay {

namespace {

float Clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Machado, Oliveira and Fernandes (2009), severity 1.0, on linear RGB.
const ColorMatrix3 kProtanopia = {0.152286f, 1.052583f, -0.204868f,
                                  0.114503f, 0.786281f, 0.099216f,
                                  -0.003882f, -0.048116f, 1.051998f};
const ColorMatrix3 kDeuteranopia = {0.367322f, 0.860646f, -0.227968f,
                                    0.280085f, 0.672501f, 0.047413f,
                                    -0.011820f, 0.042940f, 0.968881f};
const ColorMatrix3 kTritanopia = {1.255528f, -0.076749f, -0.178779f,
                                  -0.078411f, 0.930809f, 0.147602f,
                                  0.004733f, 0.691367f, 0.303900f};
// Luminance (Rec. 709 weights) in every channel.
const ColorMatrix3 kAchromatopsia = {0.2126f, 0.7152f, 0.0722f,
                                     0.2126f, 0.7152f, 0.0722f,
                                     0.2126f, 0.7152f, 0.0722f};
const ColorMatrix3 kIdentity = {1, 0, 0, 0, 1, 0, 0, 0, 1};

// Where the part of a colour a deficiency loses is moved to: row = channel that gains.
const ColorMatrix3 kShiftRedGreen = {0, 0, 0,
                                     0.7f, 1, 0,
                                     0.7f, 0, 1};
const ColorMatrix3 kShiftBlueYellow = {1, 0, 0.7f,
                                       0, 1, 0.7f,
                                       0, 0, 0};

struct Rgb { float r, g, b; };

Rgb Mul(const ColorMatrix3& m, const Rgb& v) {
    return {m[0] * v.r + m[1] * v.g + m[2] * v.b,
            m[3] * v.r + m[4] * v.g + m[5] * v.b,
            m[6] * v.r + m[7] * v.g + m[8] * v.b};
}

Rgb ToLinear(const Color& c) { return {SrgbToLinear(c.r), SrgbToLinear(c.g), SrgbToLinear(c.b)}; }

Color FromLinear(const Rgb& v, float alpha) {
    return Color(LinearToSrgb(Clamp01(v.r)), LinearToSrgb(Clamp01(v.g)), LinearToSrgb(Clamp01(v.b)), alpha);
}

const ColorMatrix3& ShiftFor(ColorblindMode mode) {
    return mode == ColorblindMode::Tritanopia ? kShiftBlueYellow : kShiftRedGreen;
}

Rgb Daltonize(const Rgb& lin, const ColorMatrix3& sim, const ColorMatrix3& shift, float strength) {
    Rgb seen = Mul(sim, lin);
    seen = {Clamp01(seen.r), Clamp01(seen.g), Clamp01(seen.b)};
    const Rgb err = {lin.r - seen.r, lin.g - seen.g, lin.b - seen.b};
    const Rgb moved = Mul(shift, err);
    return {lin.r + strength * moved.r, lin.g + strength * moved.g, lin.b + strength * moved.b};
}

// Byte <-> linear tables for image work.
const std::array<float, 256>& ByteToLinear() {
    static const std::array<float, 256> t = [] {
        std::array<float, 256> a{};
        for (int i = 0; i < 256; ++i) a[(std::size_t)i] = SrgbToLinear((float)i / 255.0f);
        return a;
    }();
    return t;
}

std::uint8_t LinearToByte(float v) {
    static const std::vector<std::uint8_t> t = [] {
        std::vector<std::uint8_t> a(4097);
        for (int i = 0; i <= 4096; ++i) a[(std::size_t)i] = (std::uint8_t)(LinearToSrgb((float)i / 4096.0f) * 255.0f + 0.5f);
        return a;
    }();
    return t[(std::size_t)(Clamp01(v) * 4096.0f + 0.5f)];
}

template <class Fn> void ForPixels(Image& img, Fn fn) {
    if (!img.Valid()) return;
    const auto& lut = ByteToLinear();
    std::uint8_t* p = img.Data();
    const std::size_t n = (std::size_t)img.Width() * (std::size_t)img.Height();
    for (std::size_t i = 0; i < n; ++i, p += 4) {
        const Rgb out = fn(Rgb{lut[p[0]], lut[p[1]], lut[p[2]]});
        p[0] = LinearToByte(out.r);
        p[1] = LinearToByte(out.g);
        p[2] = LinearToByte(out.b);
    }
}

// The palette (see SafePalette): chosen by farthest-point search over sRGB colours that are
// light enough for a dark background and saturated, scoring each pair by its closest look under
// normal vision, protanopia, deuteranopia, tritanopia and achromatopsia.
const std::vector<Color>& Palette() {
    static const std::vector<Color> p = {
        Color::FromBytes(230, 159, 0),    // orange
        Color::FromBytes(0, 221, 255),    // cyan
        Color::FromBytes(136, 51, 255),   // violet
        Color::FromBytes(238, 0, 102),    // raspberry
        Color::FromBytes(204, 238, 153),  // pale green
        Color::FromBytes(0, 119, 187),    // blue
        Color::FromBytes(136, 102, 0),    // olive brown
        Color::FromBytes(255, 119, 85),   // coral
        Color::FromBytes(204, 136, 187),  // mauve
        Color::FromBytes(255, 221, 102),  // yellow
        Color::FromBytes(238, 85, 255),   // magenta
        Color::FromBytes(102, 187, 153),  // sea green
    };
    return p;
}

} // namespace

// =====================================================================================
const char* ColorblindName(ColorblindMode mode) {
    switch (mode) {
        case ColorblindMode::Protanopia: return "protanopia";
        case ColorblindMode::Deuteranopia: return "deuteranopia";
        case ColorblindMode::Tritanopia: return "tritanopia";
        case ColorblindMode::Achromatopsia: return "achromatopsia";
        case ColorblindMode::None: break;
    }
    return "none";
}

bool ColorblindFromName(const std::string& nameIn, ColorblindMode& mode) {
    const std::string n = Lower(nameIn);
    if (n == "none" || n == "normal" || n == "off") mode = ColorblindMode::None;
    else if (n == "protanopia" || n == "protan" || n == "protanomaly") mode = ColorblindMode::Protanopia;
    else if (n == "deuteranopia" || n == "deutan" || n == "deuteranomaly") mode = ColorblindMode::Deuteranopia;
    else if (n == "tritanopia" || n == "tritan" || n == "tritanomaly") mode = ColorblindMode::Tritanopia;
    else if (n == "achromatopsia" || n == "mono" || n == "monochrome" || n == "grayscale" || n == "greyscale") mode = ColorblindMode::Achromatopsia;
    else return false;
    return true;
}

float SrgbToLinear(float c) {
    c = Clamp01(c);
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float LinearToSrgb(float c) {
    c = Clamp01(c);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

ColorMatrix3 ColorblindMatrix(ColorblindMode mode, float severity) {
    const ColorMatrix3* full = &kIdentity;
    switch (mode) {
        case ColorblindMode::Protanopia: full = &kProtanopia; break;
        case ColorblindMode::Deuteranopia: full = &kDeuteranopia; break;
        case ColorblindMode::Tritanopia: full = &kTritanopia; break;
        case ColorblindMode::Achromatopsia: full = &kAchromatopsia; break;
        case ColorblindMode::None: break;
    }
    const float s = Clamp01(severity);
    ColorMatrix3 out;
    for (std::size_t i = 0; i < 9; ++i) out[i] = kIdentity[i] + (*full)[i] * s - kIdentity[i] * s;
    return out;
}

Color SimulateColorblind(const Color& c, ColorblindMode mode, float severity) {
    if (mode == ColorblindMode::None || severity <= 0.0f) return c;
    return FromLinear(Mul(ColorblindMatrix(mode, severity), ToLinear(c)), c.a);
}

Color DaltonizeColor(const Color& c, ColorblindMode mode, float strength) {
    if (mode == ColorblindMode::None || mode == ColorblindMode::Achromatopsia || strength <= 0.0f) return c;
    return FromLinear(Daltonize(ToLinear(c), ColorblindMatrix(mode), ShiftFor(mode), Clamp01(strength)), c.a);
}

void SimulateColorblind(Image& img, ColorblindMode mode, float severity) {
    if (mode == ColorblindMode::None || severity <= 0.0f) return;
    const ColorMatrix3 m = ColorblindMatrix(mode, severity);
    ForPixels(img, [&](const Rgb& lin) { return Mul(m, lin); });
}

void DaltonizeImage(Image& img, ColorblindMode mode, float strength) {
    if (mode == ColorblindMode::None || mode == ColorblindMode::Achromatopsia || strength <= 0.0f) return;
    const ColorMatrix3 m = ColorblindMatrix(mode), shift = ShiftFor(mode);
    const float k = Clamp01(strength);
    ForPixels(img, [&](const Rgb& lin) { return Daltonize(lin, m, shift, k); });
}

Image SimulatedColorblind(const Image& img, ColorblindMode mode, float severity) {
    Image out = img;
    SimulateColorblind(out, mode, severity);
    return out;
}

Image DaltonizedImage(const Image& img, ColorblindMode mode, float strength) {
    Image out = img;
    DaltonizeImage(out, mode, strength);
    return out;
}

// ---- Telling colours apart --------------------------------------------------------------------------
LabColor ToLab(const Color& c) {
    const Rgb l = ToLinear(c);
    const float x = 0.4124564f * l.r + 0.3575761f * l.g + 0.1804375f * l.b;
    const float y = 0.2126729f * l.r + 0.7151522f * l.g + 0.0721750f * l.b;
    const float z = 0.0193339f * l.r + 0.1191920f * l.g + 0.9503041f * l.b;
    auto f = [](float t) { return t > 216.0f / 24389.0f ? std::cbrt(t) : (24389.0f / 27.0f * t + 16.0f) / 116.0f; };
    const float fx = f(x / 0.95047f), fy = f(y), fz = f(z / 1.08883f);
    return {116.0f * fy - 16.0f, 500.0f * (fx - fy), 200.0f * (fy - fz)};
}

float ColorDistance(const Color& a, const Color& b) {
    const LabColor p = ToLab(a), q = ToLab(b);
    const float dl = p.L - q.L, da = p.a - q.a, db = p.b - q.b;
    return std::sqrt(dl * dl + da * da + db * db);
}

float MinColorSeparation(const std::vector<Color>& colors, ColorblindMode mode) {
    if (colors.size() < 2) return 0.0f;
    std::vector<Color> seen;
    seen.reserve(colors.size());
    for (const Color& c : colors) seen.push_back(SimulateColorblind(c, mode));
    float best = 1e9f;
    for (std::size_t i = 0; i < seen.size(); ++i)
        for (std::size_t j = i + 1; j < seen.size(); ++j) best = std::min(best, ColorDistance(seen[i], seen[j]));
    return best;
}

const std::vector<Color>& SafePalette() { return Palette(); }

Color SafePaletteColor(int index) {
    const int n = (int)Palette().size();
    int i = index % n;
    if (i < 0) i += n;
    return Palette()[(std::size_t)i];
}

// ---- Contrast ------------------------------------------------------------------------------------------
float RelativeLuminance(const Color& c) {
    const Rgb l = ToLinear(c);
    return 0.2126f * l.r + 0.7152f * l.g + 0.0722f * l.b;
}

float ContrastRatio(const Color& a, const Color& b) {
    const float la = RelativeLuminance(a), lb = RelativeLuminance(b);
    const float hi = la > lb ? la : lb, lo = la > lb ? lb : la;
    return (hi + 0.05f) / (lo + 0.05f);
}

Color EnsureContrast(const Color& fg, const Color& bg, float minRatio) {
    if (ContrastRatio(fg, bg) >= minRatio) return fg;
    const Color white(1, 1, 1, fg.a), black(0, 0, 0, fg.a);
    // The smallest step towards `target` that reaches the ratio, or -1 when even the target falls short.
    auto steps = [&](const Color& target) {
        for (int i = 1; i <= 64; ++i) {
            const float t = (float)i / 64.0f;
            if (ContrastRatio(Color::Lerp(fg, target, t), bg) >= minRatio) return t;
        }
        return -1.0f;
    };
    const float tl = steps(white), td = steps(black);
    if (tl < 0.0f && td < 0.0f) return ContrastRatio(white, bg) >= ContrastRatio(black, bg) ? white : black;
    Color out;
    if (td < 0.0f || (tl >= 0.0f && tl <= td)) out = Color::Lerp(fg, white, tl);
    else out = Color::Lerp(fg, black, td);
    out.a = fg.a;
    return out;
}

} // namespace okay
