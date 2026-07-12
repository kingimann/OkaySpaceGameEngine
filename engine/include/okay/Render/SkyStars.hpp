#pragma once
#include <vector>
#include <cstdint>

namespace okay {

/// A single painted star: screen position, radius (px), and 0..255 alpha.
struct SkyStarPt { float x, y, r; unsigned char a; };

/// Upper bound on stars at density 1.0 (keeps the paint cheap on every backend).
inline constexpr int kSkyStarMax = 340;

/// Deterministically place a star field in the upper sky for a `vw`x`vh` viewport,
/// with the horizon band at `horizonY` pixels (stars only appear above it). The
/// layout is a pure function of the index + `seed`, so it never flickers between
/// frames and is identical in the editor preview and the shipped player.
/// `density` (0..1) scales the count; `bright` (0..1) scales each star's alpha.
inline std::vector<SkyStarPt> SkyStars(float vw, float vh, float horizonY,
                                       float density, float bright, uint32_t seed = 1u) {
    (void)vh;
    std::vector<SkyStarPt> out;
    float d = density < 0.0f ? 0.0f : (density > 1.0f ? 1.0f : density);
    float b = bright < 0.0f ? 0.0f : (bright > 1.0f ? 1.0f : bright);
    int count = (int)(kSkyStarMax * d);
    if (count <= 0 || horizonY <= 1.0f || vw <= 0.0f) return out;
    auto hash = [](uint32_t x) {                 // a fast integer hash (stable placement)
        x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x;
    };
    out.reserve(count);
    for (int i = 0; i < count; ++i) {
        uint32_t ha = hash((uint32_t)i * 3u + seed);
        uint32_t hb = hash((uint32_t)i * 3u + 1u + seed);
        uint32_t hc = hash((uint32_t)i * 3u + 2u + seed);
        float fx = (ha & 0xffffu) / 65535.0f;
        float fy = (hb & 0xffffu) / 65535.0f;
        // Bias stars toward the top (denser zenith) with a square curve.
        float y = fy * fy * horizonY;
        float tw = 0.35f + 0.65f * ((hc & 0xffu) / 255.0f);   // per-star twinkle/brightness
        float al = b * tw; if (al > 1.0f) al = 1.0f;
        float r = ((hc >> 9) & 3u) == 0u ? 1.5f : 0.9f;       // a few larger stars
        out.push_back({ fx * vw, y, r, (unsigned char)(al * 255.0f) });
    }
    return out;
}

}  // namespace okay
