#pragma once
// ---------------------------------------------------------------------------
// ProcTexture — built-in procedural textures, generated in code (no files).
// Use them anywhere a texture path goes by name:
//
//   proc:checker  proc:grid   proc:brick  proc:wood   proc:stone  proc:tiles
//   proc:marble   proc:metal  proc:noise  proc:grass  proc:sand   proc:lava
//
// GetCachedTexture() recognizes the "proc:" prefix and generates the image on
// first use, so they work in the editor, every GPU renderer and the shipped
// player, and serialize as plain texture strings. All textures are 256x256,
// seamlessly tileable (noise wraps), and deterministic (same every run).
// ---------------------------------------------------------------------------
#include "okay/Graphics/Image.hpp"
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace okay {

namespace proctex_detail {

// Deterministic integer hash -> [0,1). Wrapped by `period` for tileability.
inline float Hash01(int x, int y, int period, std::uint32_t seed = 0) {
    x &= period - 1; y &= period - 1;                       // period must be a power of two
    std::uint32_t h = (std::uint32_t)x * 374761393u + (std::uint32_t)y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xFFFFFF) / 16777216.0f;
}

// Tileable value noise at integer lattice `period`, sampled at (u,v) in [0,1).
inline float ValueNoise(float u, float v, int period, std::uint32_t seed = 0) {
    float fx = u * period, fy = v * period;
    int ix = (int)std::floor(fx), iy = (int)std::floor(fy);
    float tx = fx - ix, ty = fy - iy;
    tx = tx * tx * (3.0f - 2.0f * tx);                      // smoothstep
    ty = ty * ty * (3.0f - 2.0f * ty);
    float a = Hash01(ix, iy, period, seed),     b = Hash01(ix + 1, iy, period, seed);
    float c = Hash01(ix, iy + 1, period, seed), d = Hash01(ix + 1, iy + 1, period, seed);
    return a + (b - a) * tx + (c - a) * ty + (a - b - c + d) * tx * ty;
}

// Fractal (4-octave) tileable noise in [0,1].
inline float Fbm(float u, float v, int basePeriod, std::uint32_t seed = 0) {
    float sum = 0.0f, amp = 0.5f, tot = 0.0f;
    int p = basePeriod;
    for (int o = 0; o < 4; ++o) {
        sum += ValueNoise(u, v, p, seed + o * 101u) * amp;
        tot += amp; amp *= 0.5f; p *= 2;
    }
    return sum / tot;
}

// Tileable Worley/cellular noise: distance to the nearest feature point of a
// wrapped `period` x `period` cell grid, normalized so ~1 = cell centre spans.
// `outF2` (optional) receives the SECOND-nearest distance — (F2-F1) is ~0 on
// the border between two cells, which draws connected joints (cobblestones).
// `outCell` (optional) receives a per-cell random in [0,1) for tinting.
inline float Worley(float u, float v, int period, std::uint32_t seed = 0,
                    float* outF2 = nullptr, float* outCell = nullptr) {
    float fx = u * period, fy = v * period;
    int ix = (int)std::floor(fx), iy = (int)std::floor(fy);
    float best = 8.0f, second = 8.0f, cell = 0.0f;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            int cx = ix + dx, cy = iy + dy;
            float px = cx + Hash01(cx, cy, period, seed);
            float py = cy + Hash01(cx, cy, period, seed + 7u);
            float ddx = fx - px, ddy = fy - py;
            float d = std::sqrt(ddx * ddx + ddy * ddy);
            if (d < best) { second = best; best = d; cell = Hash01(cx, cy, period, seed + 13u); }
            else if (d < second) second = d;
        }
    if (outF2) *outF2 = second > 1.5f ? 1.5f : second;
    if (outCell) *outCell = cell;
    return best > 1.0f ? 1.0f : best;
}

inline Color Lerp(const Color& a, const Color& b, float t) {
    return Color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                 a.b + (b.b - a.b) * t, 1.0f);
}

} // namespace proctex_detail

/// Names of every built-in procedural texture (for editor pickers), "proc:"-less.
inline const std::vector<std::string>& ProcTextureNames() {
    static const std::vector<std::string> names = {
        "checker", "grid", "brick", "wood", "stone", "tiles",
        "marble", "metal", "noise", "grass", "sand", "lava",
    };
    return names;
}

/// Generate a built-in procedural texture by its "proc:<name>" id (or bare
/// name). Returns an empty Image for unknown names.
inline Image GenerateProcTexture(const std::string& id) {
    using namespace proctex_detail;
    std::string name = id.rfind("proc:", 0) == 0 ? id.substr(5) : id;
    const int S = 256;
    Image img(S, S);

    auto forEach = [&](auto&& pixel) {
        for (int y = 0; y < S; ++y)
            for (int x = 0; x < S; ++x)
                img.SetPixel(x, y, pixel((x + 0.5f) / S, (y + 0.5f) / S, x, y));
    };

    if (name == "checker") {
        forEach([&](float, float, int x, int y) {
            bool a = ((x / 32) + (y / 32)) & 1;
            return a ? Color(0.82f, 0.82f, 0.84f, 1) : Color(0.32f, 0.33f, 0.36f, 1);
        });
    } else if (name == "grid") {
        forEach([&](float, float, int x, int y) {
            bool line = (x % 32) < 2 || (y % 32) < 2;
            return line ? Color(0.25f, 0.28f, 0.33f, 1) : Color(0.87f, 0.88f, 0.90f, 1);
        });
    } else if (name == "brick") {
        forEach([&](float u, float v, int, int) {
            const float bw = 1.0f / 4.0f, bh = 1.0f / 8.0f;   // 4x8 bricks
            int row = (int)(v / bh);
            float uu = u + (row & 1 ? bw * 0.5f : 0.0f);      // stagger odd rows
            float fx = std::fmod(uu, bw) / bw, fy = std::fmod(v, bh) / bh;
            bool mortar = fx < 0.06f || fy < 0.12f;
            if (mortar) return Color(0.75f, 0.73f, 0.70f, 1);
            int bx = (int)(uu / bw), by = row;
            float tint = 0.85f + 0.3f * Hash01(bx, by, 16);   // per-brick variation
            float n = 0.92f + 0.16f * Fbm(u, v, 32, 5u);      // surface roughness
            return Color(0.62f * tint * n, 0.30f * tint * n, 0.24f * tint * n, 1);
        });
    } else if (name == "wood") {
        forEach([&](float u, float v, int, int) {
            const int planks = 4;
            int plank = (int)(u * planks);
            float pu = u * planks - plank;
            float tint = 0.85f + 0.3f * Hash01(plank, 3, 8);          // per-plank tone
            float grain = std::sin((v * 24.0f + Fbm(u, v, 8, 11u) * 6.0f) * 3.14159f);
            float g = 0.82f + 0.14f * grain;
            if (pu < 0.03f) g *= 0.55f;                                // plank seam
            return Color(0.55f * tint * g, 0.36f * tint * g, 0.20f * tint * g, 1);
        });
    } else if (name == "stone") {
        forEach([&](float u, float v, int, int) {
            float f2, cell;
            float f1 = Worley(u, v, 6, 3u, &f2, &cell);
            float edge = f2 - f1;                                      // ~0 on cell borders
            float joint = edge < 0.08f ? 0.45f + edge * 4.0f : 1.0f;   // connected mortar lines
            float dome = 1.0f - 0.25f * f1;                            // stones bulge in the middle
            float tint = 0.82f + 0.3f * cell;                          // per-stone tone
            float n = 0.88f + 0.2f * Fbm(u, v, 32, 9u);
            float b = 0.60f * joint * dome * tint * n;
            return Color(b, b * 1.0f, b * 1.04f, 1);
        });
    } else if (name == "tiles") {
        forEach([&](float u, float v, int, int) {
            const int n = 8;
            float fx = std::fmod(u * n, 1.0f), fy = std::fmod(v * n, 1.0f);
            if (fx < 0.05f || fy < 0.05f) return Color(0.30f, 0.31f, 0.33f, 1);   // grout
            int tx = (int)(u * n), ty = (int)(v * n);
            float t = 0.85f + 0.25f * Hash01(tx, ty, n);
            return Color(0.62f * t, 0.72f * t, 0.74f * t, 1);
        });
    } else if (name == "marble") {
        forEach([&](float u, float v, int, int) {
            float vein = std::sin((u * 6.0f + Fbm(u, v, 8, 21u) * 5.0f) * 3.14159f);
            float m = 0.72f + 0.26f * vein * vein;                     // bright field, dark veins
            return Color(m, m * 0.99f, m * 1.02f, 1);
        });
    } else if (name == "metal") {
        forEach([&](float u, float v, int, int y) {
            float streak = 0.85f + 0.15f * ValueNoise(u * 0.02f, v, 128, 31u);   // horizontal brushing
            float n = 0.96f + 0.08f * Hash01((int)(u * 256), y, 256, 12u);
            float b = 0.62f * streak * n;
            return Color(b, b * 1.02f, b * 1.08f, 1);
        });
    } else if (name == "noise") {
        forEach([&](float u, float v, int, int) {
            float n = Fbm(u, v, 8, 42u);
            return Color(n, n, n, 1);
        });
    } else if (name == "grass") {
        forEach([&](float u, float v, int, int) {
            float n = Fbm(u, v, 16, 51u);
            float blades = 0.85f + 0.3f * ValueNoise(u, v * 0.05f, 128, 52u);   // vertical strands
            return Color(0.20f * blades * (0.7f + 0.5f * n),
                         0.45f * blades * (0.75f + 0.5f * n),
                         0.16f * blades * (0.7f + 0.4f * n), 1);
        });
    } else if (name == "sand") {
        forEach([&](float u, float v, int, int) {
            float dune = 0.9f + 0.1f * std::sin((u * 5.0f + Fbm(u, v, 4, 61u) * 2.0f) * 6.2832f);
            float grainy = 0.92f + 0.16f * Fbm(u, v, 64, 62u);
            return Color(0.78f * dune * grainy, 0.68f * dune * grainy, 0.48f * dune * grainy, 1);
        });
    } else if (name == "lava") {
        forEach([&](float u, float v, int, int) {
            float w = Worley(u, v, 8, 71u);
            float glow = w > 0.75f ? 1.0f : Fbm(u, v, 16, 72u) * 0.25f;  // bright cracks
            return Color(0.25f + 0.75f * glow, 0.08f + 0.55f * glow * glow, 0.05f + 0.1f * glow, 1);
        });
    } else {
        return Image();   // unknown name
    }
    return img;
}

} // namespace okay
