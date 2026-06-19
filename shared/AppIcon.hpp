#pragma once
#include <SDL.h>
#include <cmath>
#include <cstdint>
#include <vector>

namespace okay {

// A self-contained placeholder app logo, drawn procedurally so it needs no
// image file: a rounded dark-navy tile with a cyan ringed planet and a few
// stars — the "OkaySpace" space theme. Set as the window icon for the editor
// and the launcher. Replace with real art later by loading a PNG instead.
inline void SetAppIcon(SDL_Window* window) {
    const int N = 64;
    std::vector<std::uint32_t> px(N * N);

    auto put = [&](int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
        // ABGR8888 (matches SDL_PIXELFORMAT_RGBA32 little-endian byte order below).
        px[y * N + x] = (std::uint32_t(a) << 24) | (std::uint32_t(b) << 16) |
                        (std::uint32_t(g) << 8) | std::uint32_t(r);
    };
    auto blend = [&](int x, int y, float r, float g, float b, float t) {
        if (x < 0 || y < 0 || x >= N || y >= N || t <= 0.0f) return;
        if (t > 1.0f) t = 1.0f;
        std::uint32_t cur = px[y * N + x];
        float cr = (cur & 0xFF), cg = ((cur >> 8) & 0xFF), cb = ((cur >> 16) & 0xFF);
        float ca = ((cur >> 24) & 0xFF) / 255.0f;
        float nr = cr * (1 - t) + r * 255 * t;
        float ng = cg * (1 - t) + g * 255 * t;
        float nb = cb * (1 - t) + b * 255 * t;
        float na = ca * (1 - t) + t;
        put(x, y, (std::uint8_t)nr, (std::uint8_t)ng, (std::uint8_t)nb, (std::uint8_t)(na * 255));
    };

    // Rounded-rectangle navy background.
    const float radius = 12.0f;
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            float dx = 0, dy = 0;
            if (x < radius)        dx = radius - x;
            else if (x > N - 1 - radius) dx = x - (N - 1 - radius);
            if (y < radius)        dy = radius - y;
            else if (y > N - 1 - radius) dy = y - (N - 1 - radius);
            float corner = std::sqrt(dx * dx + dy * dy);
            float inside = (dx > 0 && dy > 0) ? (radius - corner) : 1.0f;
            if (inside <= 0.0f) { put(x, y, 0, 0, 0, 0); continue; }
            // Vertical gradient from deep space to a lighter navy.
            float t = (float)y / N;
            std::uint8_t r = (std::uint8_t)(14 + 10 * t);
            std::uint8_t g = (std::uint8_t)(18 + 16 * t);
            std::uint8_t b = (std::uint8_t)(34 + 30 * t);
            put(x, y, r, g, b, 255);
        }

    // A scatter of stars.
    const int stars[][2] = {{12, 14}, {50, 12}, {44, 22}, {16, 46}, {54, 48}, {24, 10}, {38, 52}};
    for (auto& s : stars) blend(s[0], s[1], 1.0f, 1.0f, 1.0f, 0.9f);

    // The planet: a cyan disc with simple top-left lighting.
    const float cx = 30.0f, cy = 34.0f, pr = 16.0f;
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            float d = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
            float edge = pr - d;
            if (edge <= 0.0f) continue;
            float shade = 0.55f + 0.45f * ((cx - x) + (cy - y)) / (2 * pr); // lit toward top-left
            if (shade < 0.25f) shade = 0.25f;
            if (shade > 1.0f) shade = 1.0f;
            float aa = edge < 1.0f ? edge : 1.0f;
            blend(x, y, 0.25f * shade, 0.80f * shade, 1.0f * shade, aa);
        }

    // A thin ring around the planet (an ellipse band).
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            float rx = (x - cx) / 26.0f, ry = (y - cy) / 9.0f;
            float e = std::sqrt(rx * rx + ry * ry);
            float band = 1.0f - std::fabs(e - 1.0f) / 0.10f;
            if (band <= 0.0f) continue;
            // Hide the back of the ring behind the planet (upper half).
            float d = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
            if (d < pr && y < cy) continue;
            blend(x, y, 0.95f, 0.85f, 0.55f, band * 0.9f);
        }

    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
        px.data(), N, N, 32, N * 4, SDL_PIXELFORMAT_RGBA32);
    if (surf) {
        SDL_SetWindowIcon(window, surf);
        SDL_FreeSurface(surf);
    }
}

} // namespace okay
