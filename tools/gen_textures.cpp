// Procedural texture generator for OkaySpace's starter asset pack.
// Produces seamless, tileable 512x512 RGBA PNGs: grass, dirt, stone and a grid.
// Build/run is wired by tools/CMakeLists.txt; the output lands in assets/textures/.
//
//   ./gen_textures <output-dir>
//
// Everything here is deterministic (fixed seeds) so re-running yields identical
// files — no churn in git.
#include "okay/Graphics/Image.hpp"
#include <cmath>
#include <cstdint>
#include <string>

using okay::Image;
using okay::Color;

namespace {

constexpr int N = 512;   // texture size (power of two, tiles cleanly)

// Integer hash -> [0,1). Cheap, deterministic, no <random> needed.
float hash2(int x, int y, std::uint32_t seed) {
    std::uint32_t h = (std::uint32_t)x * 374761393u + (std::uint32_t)y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xFFFFFFu) / float(0x1000000u);
}

// Tileable value noise: integer lattice wraps at `period`, so the texture is
// seamless when tiled. Smoothstep interpolation between lattice points.
float vnoise(float x, float y, int period, std::uint32_t seed) {
    auto wrap = [period](int v) { int m = v % period; return m < 0 ? m + period : m; };
    int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    float fx = x - x0, fy = y - y0;
    float sx = fx * fx * (3.0f - 2.0f * fx), sy = fy * fy * (3.0f - 2.0f * fy);
    float a = hash2(wrap(x0),     wrap(y0),     seed);
    float b = hash2(wrap(x0 + 1), wrap(y0),     seed);
    float c = hash2(wrap(x0),     wrap(y0 + 1), seed);
    float d = hash2(wrap(x0 + 1), wrap(y0 + 1), seed);
    float top = a + (b - a) * sx, bot = c + (d - c) * sx;
    return top + (bot - top) * sy;
}

// Multi-octave (fractal) tileable noise, returns ~[0,1].
float fbm(float u, float v, int baseFreq, int octaves, std::uint32_t seed) {
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    int freq = baseFreq;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * vnoise(u * freq, v * freq, freq, seed + (std::uint32_t)o * 101u);
        norm += amp;
        amp *= 0.5f;
        freq *= 2;
    }
    return sum / norm;
}

float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
Color mix(const Color& a, const Color& b, float t) {
    t = clamp01(t);
    return Color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.0f);
}

void save(const Image& img, const std::string& dir, const char* name) {
    std::string path = dir + "/" + name;
    if (img.SavePNG(path)) std::printf("  wrote %s\n", path.c_str());
    else                   std::printf("  FAILED %s\n", path.c_str());
}

// ---- grass: layered greens with a few warm/dry flecks ------------------------
Image grass() {
    Image img(N, N);
    Color dark(0.12f, 0.30f, 0.10f), mid(0.22f, 0.46f, 0.16f), light(0.40f, 0.62f, 0.26f);
    Color dry(0.56f, 0.55f, 0.24f);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            float u = x / float(N), v = y / float(N);
            float n  = fbm(u, v, 16, 5, 7u);
            float fine = fbm(u, v, 64, 3, 23u);            // blade-scale detail
            Color c = mix(dark, mid, n);
            c = mix(c, light, clamp01(fine * fine * 1.3f));
            float fleck = hash2(x, y, 91u);
            if (fleck > 0.985f) c = mix(c, dry, 0.6f);     // sparse dry blades
            img.SetPixel(x, y, c);
        }
    return img;
}

// ---- dirt: brown fbm with darker clumps and small pebbles --------------------
Image dirt() {
    Image img(N, N);
    Color deep(0.22f, 0.14f, 0.08f), base(0.40f, 0.26f, 0.15f), pale(0.55f, 0.40f, 0.26f);
    Color pebble(0.62f, 0.58f, 0.52f);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            float u = x / float(N), v = y / float(N);
            float n  = fbm(u, v, 12, 5, 41u);
            float gr = fbm(u, v, 48, 3, 67u);
            Color c = mix(deep, base, n);
            c = mix(c, pale, clamp01((gr - 0.5f) * 1.6f));
            float p = hash2(x, y, 13u);
            if (p > 0.992f) c = mix(c, pebble, 0.7f);      // scattered grit
            img.SetPixel(x, y, c);
        }
    return img;
}

// ---- stone: grey fbm with subtle tileable cracks -----------------------------
Image stone() {
    Image img(N, N);
    Color d(0.30f, 0.30f, 0.33f), m(0.50f, 0.50f, 0.54f), l(0.68f, 0.68f, 0.72f);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            float u = x / float(N), v = y / float(N);
            float n = fbm(u, v, 10, 6, 5u);
            Color c = mix(d, m, n);
            c = mix(c, l, clamp01((fbm(u, v, 40, 3, 19u) - 0.55f) * 2.0f));
            // Crack mask: ridged noise (1 - |2n-1|) thresholded near its peak.
            float r = fbm(u, v, 8, 4, 77u);
            float ridge = 1.0f - std::fabs(2.0f * r - 1.0f);
            if (ridge > 0.93f) c = mix(c, Color(0.16f, 0.16f, 0.18f, 1.0f), (ridge - 0.93f) / 0.07f);
            img.SetPixel(x, y, c);
        }
    return img;
}

// ---- grid: blockout texture — major + minor lines on a dark cell -------------
Image grid() {
    Image img(N, N);
    Color cell(0.13f, 0.14f, 0.17f), minor(0.24f, 0.26f, 0.30f), major(0.45f, 0.80f, 0.95f);
    const int step = 64;        // one major cell
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            // distance (in px) to nearest major line, wrapping at the edges so it tiles
            int mx = x % step, my = y % step;
            int dx = mx < step - mx ? mx : step - mx;
            int dy = my < step - my ? my : step - my;
            int d = dx < dy ? dx : dy;
            Color c = cell;
            if ((x % (step / 4) == 0) || (y % (step / 4) == 0)) c = minor; // minor every 16px
            if (d == 0) c = major;                                         // bright major lines
            else if (d == 1) c = mix(cell, major, 0.4f);                   // soft edge
            img.SetPixel(x, y, c);
        }
    return img;
}

} // namespace

int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : ".";
    std::printf("Generating OkaySpace starter textures into %s\n", dir.c_str());
    save(grass(), dir, "grass.png");
    save(dirt(),  dir, "dirt.png");
    save(stone(), dir, "stone.png");
    save(grid(),  dir, "grid.png");
    return 0;
}
