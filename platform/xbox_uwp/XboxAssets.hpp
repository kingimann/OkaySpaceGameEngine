#pragma once
// ---------------------------------------------------------------------------
// EXPERIMENTAL Xbox One Developer Mode export: the package's tile and logo images.
//
// Our own image code: an area-average (box) resize with alpha weighting, "contain" fitting on a
// solid background, and a generated PLACEHOLDER picture when the project has no icon. Written
// from the general algorithm (area averaging); no other project's code. Pure pixel work, tested
// on Linux; writing PNG files uses okay::Image::SavePNG.
//
// A placeholder is NOT a final asset: ExportAssets reports it, and the export writes
// PLACEHOLDER_ASSETS.txt beside the package so nobody ships it by accident.
// ---------------------------------------------------------------------------
#include "XboxManifest.hpp"
#include "okay/Graphics/Image.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace okay::xbox {

struct Rgba8 { std::uint8_t r = 0, g = 0, b = 0, a = 255; };

inline Rgba8 ParseBackground(const std::string& hex) {   // "#RRGGBB" (validated elsewhere); anything else: dark
    Rgba8 c; c.r = 16; c.g = 16; c.b = 24;
    if (hex.size() == 7 && hex[0] == '#') {
        auto h = [](char ch) { return ch >= 'a' ? ch - 'a' + 10 : ch >= 'A' ? ch - 'A' + 10 : ch - '0'; };
        c.r = static_cast<std::uint8_t>(h(hex[1]) * 16 + h(hex[2]));
        c.g = static_cast<std::uint8_t>(h(hex[3]) * 16 + h(hex[4]));
        c.b = static_cast<std::uint8_t>(h(hex[5]) * 16 + h(hex[6]));
    }
    return c;
}

// Shrink or enlarge `src` to dw x dh by averaging every source pixel a destination pixel covers (weighted by alpha).
inline Image ResizeBox(const Image& src, int dw, int dh) {
    Image out(dw, dh);
    if (!src.Valid() || dw <= 0 || dh <= 0) return out;
    const double sx = static_cast<double>(src.Width()) / dw, sy = static_cast<double>(src.Height()) / dh;
    for (int y = 0; y < dh; ++y) {
        const double y0 = y * sy, y1 = (y + 1) * sy;
        for (int x = 0; x < dw; ++x) {
            const double x0 = x * sx, x1 = (x + 1) * sx;
            double ar = 0, ag = 0, ab = 0, aa = 0, wsum = 0;
            const int iy0 = static_cast<int>(std::floor(y0)), iy1 = std::min(src.Height() - 1, static_cast<int>(std::ceil(y1)) - 1);
            const int ix0 = static_cast<int>(std::floor(x0)), ix1 = std::min(src.Width() - 1, static_cast<int>(std::ceil(x1)) - 1);
            for (int yy = iy0; yy <= std::max(iy0, iy1); ++yy) {
                const double wy = std::min<double>(yy + 1, y1) - std::max<double>(yy, y0);
                if (wy <= 0) continue;
                for (int xx = ix0; xx <= std::max(ix0, ix1); ++xx) {
                    const double wx = std::min<double>(xx + 1, x1) - std::max<double>(xx, x0);
                    if (wx <= 0) continue;
                    const std::uint8_t* p = src.Data() + (static_cast<std::size_t>(yy) * src.Width() + xx) * 4;
                    const double w = wx * wy, pa = p[3] / 255.0;
                    ar += p[0] * pa * w; ag += p[1] * pa * w; ab += p[2] * pa * w; aa += pa * w; wsum += w;
                }
            }
            std::uint8_t* o = out.Data() + (static_cast<std::size_t>(y) * dw + x) * 4;
            if (aa > 0 && wsum > 0) {
                o[0] = static_cast<std::uint8_t>(std::min(255.0, ar / aa + 0.5));
                o[1] = static_cast<std::uint8_t>(std::min(255.0, ag / aa + 0.5));
                o[2] = static_cast<std::uint8_t>(std::min(255.0, ab / aa + 0.5));
                o[3] = static_cast<std::uint8_t>(std::min(255.0, aa / wsum * 255.0 + 0.5));
            }
        }
    }
    return out;
}

// Fit `icon` inside w x h (keeping its shape, about `fill` of the shorter side) on a solid background.
inline Image FitOnBackground(const Image& icon, int w, int h, Rgba8 bg, double fill = 0.8) {
    Image out(w, h);
    for (int i = 0; i < w * h; ++i) {
        std::uint8_t* p = out.Data() + static_cast<std::size_t>(i) * 4;
        p[0] = bg.r; p[1] = bg.g; p[2] = bg.b; p[3] = 255;
    }
    if (!icon.Valid()) return out;
    const double box = std::min(w, h) * fill;
    const double scale = box / std::max(icon.Width(), icon.Height());
    const int iw = std::max(1, static_cast<int>(std::lround(icon.Width() * scale)));
    const int ih = std::max(1, static_cast<int>(std::lround(icon.Height() * scale)));
    const Image small = ResizeBox(icon, iw, ih);
    const int ox = (w - iw) / 2, oy = (h - ih) / 2;
    for (int y = 0; y < ih; ++y)
        for (int x = 0; x < iw; ++x) {
            const std::uint8_t* s = small.Data() + (static_cast<std::size_t>(y) * iw + x) * 4;
            std::uint8_t* d = out.Data() + (static_cast<std::size_t>(y + oy) * w + (x + ox)) * 4;
            const int a = s[3];
            for (int k = 0; k < 3; ++k) d[k] = static_cast<std::uint8_t>((s[k] * a + d[k] * (255 - a) + 127) / 255);
        }
    return out;
}

// A clearly generated stand-in: a ring with a diamond in the middle. Not artwork anyone should ship.
inline Image MakePlaceholderIcon(int size = 256) {
    Image im(size, size);
    const double c = (size - 1) / 2.0;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const double dx = x - c, dy = y - c, r = std::sqrt(dx * dx + dy * dy) / (size / 2.0);
            std::uint8_t* p = im.Data() + (static_cast<std::size_t>(y) * size + x) * 4;
            const bool ring = r > 0.62 && r < 0.9;
            const bool dia = (std::fabs(dx) + std::fabs(dy)) / (size / 2.0) < 0.38;
            p[0] = ring ? 235 : 90; p[1] = ring ? 235 : 190; p[2] = ring ? 245 : 255;
            p[3] = (ring || dia) ? 255 : 0;
        }
    return im;
}

struct AssetResult {
    bool ok = false;
    bool placeholder = false;       // true: the project has no usable icon, so a generated stand-in was used
    std::vector<std::string> files; // written, relative to the Assets folder
    std::string error;
};

// Writes every RequiredAssets() file at 100% and a ".scale-200" copy at double size into `assetsDir`.
// `iconPath` empty or unreadable -> the generated placeholder, and `placeholder` is set (the export still succeeds).
inline AssetResult WriteAssets(const std::string& assetsDir, const std::string& iconPath, const std::string& backgroundHex) {
    AssetResult res;
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(fs::u8path(assetsDir), ec);
    if (ec) { res.error = "Could not create " + assetsDir + ": " + ec.message(); return res; }
    Image icon;
    std::string loadErr;
    if (iconPath.empty() || !icon.Load(iconPath, &loadErr) || !icon.Valid()) {
        icon = MakePlaceholderIcon();
        res.placeholder = true;
    }
    const Rgba8 bg = ParseBackground(backgroundHex);
    for (const AssetSpec& a : RequiredAssets()) {
        for (int scale : {1, 2}) {
            const Image im = FitOnBackground(icon, a.w * scale, a.h * scale, bg);
            std::string name = a.file;
            if (scale == 2) name = name.substr(0, name.size() - 4) + ".scale-200.png";
            const fs::path p = fs::u8path(assetsDir) / fs::u8path(name);
            if (!im.SavePNG(p.u8string())) { res.error = "Could not write " + p.u8string(); return res; }
            res.files.push_back(name);
        }
    }
    res.ok = true;
    return res;
}

} // namespace okay::xbox
