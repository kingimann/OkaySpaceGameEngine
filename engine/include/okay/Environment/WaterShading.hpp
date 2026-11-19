#pragma once
// WaterShading: the maths behind how the water LOOKS (not how it moves: that is WaterWaves.hpp).
//
// Everything here is a plain function of numbers, seeded and deterministic, so it can be tested without a window.
// The Water component calls these once per mesh vertex per frame and stores the answer as the vertex colour and
// the vertex normal, which every renderer (software, GL, D3D11) already draws. Nothing here touches a renderer.
//
// Written by us from general, published ideas (no code from any other project):
//   * value noise and fractal sums of it (lattice hash + smooth interpolation),
//   * Schlick's Fresnel approximation  F = F0 + (1 - F0) (1 - cos)^5,
//   * Beer-Lambert absorption          T = exp(-sigma * depth)   (red light is absorbed first, so deep water is blue),
//   * a "backlit thin crest" scattering term (light passing through the top of a wave towards the eye),
//   * a ring wave that spreads from a splash:  cos(k (d - c t)) under a moving Gaussian that fades with age and distance,
//   * caustics as bright ridges where two moving noise fields cross (a look-alike, not a light simulation),
//   * a clipmap-style grid: square rings of cells that double in size outward, centred on the camera and snapped so
//     the sample points sit on a fixed lattice (the surface does not swim), with the odd vertices on each ring's outer
//     edge pulled onto the neighbouring coarse edge so there are no cracks.
// See docs/water_visuals.md.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace okay {
namespace watershade {

inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float Smooth(float a, float b, float x) {
    if (b == a) return x < a ? 0.0f : 1.0f;
    const float t = Clamp01((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

struct RGB { float r = 0, g = 0, b = 0; };
inline RGB Mix(const RGB& a, const RGB& b, float t) { return {Lerp(a.r, b.r, t), Lerp(a.g, b.g, t), Lerp(a.b, b.b, t)}; }

// ---- noise --------------------------------------------------------------------------------------------------------
inline float Hash(int x, int z, std::uint32_t seed) {
    std::uint32_t h = (std::uint32_t)x * 374761393u + (std::uint32_t)z * 668265263u + seed * 2246822519u + 0x9E3779B9u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float)(h & 0xFFFFFFu) / 16777216.0f;
}
/// Smooth value noise in [0,1].
inline float Noise(float x, float z, std::uint32_t seed = 0) {
    const float fx = std::floor(x), fz = std::floor(z);
    const int ix = (int)fx, iz = (int)fz;
    float tx = x - fx, tz = z - fz;
    tx = tx * tx * (3.0f - 2.0f * tx);
    tz = tz * tz * (3.0f - 2.0f * tz);
    const float a = Hash(ix, iz, seed), b = Hash(ix + 1, iz, seed), c = Hash(ix, iz + 1, seed), d = Hash(ix + 1, iz + 1, seed);
    return a + (b - a) * tx + (c - a) * tz + (a - b - c + d) * tx * tz;
}
/// Fractal sum of `octaves` noises (each twice as fine and half as strong), in [0,1].
inline float Fbm(float x, float z, int octaves, std::uint32_t seed = 0) {
    float sum = 0.0f, amp = 0.5f, tot = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        sum += Noise(x, z, seed + 101u * (std::uint32_t)o) * amp;
        tot += amp; amp *= 0.5f; x *= 2.03f; z *= 2.03f;
        // a small turn per octave so the lattice directions do not line up
        const float nx = x * 0.8f - z * 0.6f, nz = x * 0.6f + z * 0.8f; x = nx; z = nz;
    }
    return tot > 0.0f ? sum / tot : 0.0f;
}

// ---- small ripples (always there, even on a calm lake) -------------------------------------------------------------
/// Height of the fine ripples at a world x/z, in metres (about +-0.015 m times `strength`). Two noise layers scroll in
/// two different directions; `flowX/flowZ` (the current, m/s) drags both along, so a river's ripples travel downstream.
inline float RippleHeight(float x, float z, float t, float strength, float scale, float flowX, float flowZ) {
    if (strength <= 0.0f) return 0.0f;
    const float s = scale > 0.05f ? scale : 0.05f;
    const float fa = 1.9f / s, fb = 3.1f / s;
    const float ax = x - (0.30f + flowX) * t, az = z - (0.12f + flowZ) * t;     // layer A drifts one way
    const float bx = x + (0.11f - flowX * 0.7f) * t, bz = z - (0.27f + flowZ * 0.7f) * t;   // layer B drifts another way
    const float ha = Fbm(ax * fa, az * fa, 3, 11u) - 0.5f;
    const float hb = Fbm(bx * fb + 17.0f, bz * fb - 5.0f, 2, 23u) - 0.5f;
    return (ha * 0.030f + hb * 0.016f) * s * strength;
}
/// Slope (dh/dx, dh/dz) of the ripples (central differences of RippleHeight).
inline void RippleSlope(float x, float z, float t, float strength, float scale, float flowX, float flowZ, float& sx, float& sz) {
    const float e = 0.04f * (scale > 0.05f ? scale : 0.05f);
    sx = (RippleHeight(x + e, z, t, strength, scale, flowX, flowZ) - RippleHeight(x - e, z, t, strength, scale, flowX, flowZ)) / (2.0f * e);
    sz = (RippleHeight(x, z + e, t, strength, scale, flowX, flowZ) - RippleHeight(x, z - e, t, strength, scale, flowX, flowZ)) / (2.0f * e);
}

// ---- light ------------------------------------------------------------------------------------------------------
/// Schlick's Fresnel: how much of the sky is mirrored. cosTheta = dot(normal, direction to the eye).
inline float Fresnel(float cosTheta, float f0 = 0.02f) {
    const float c = 1.0f - Clamp01(cosTheta);
    const float c2 = c * c;
    return f0 + (1.0f - f0) * c2 * c2 * c;
}
/// Fraction of the light that survives going `depth` metres down (Beer-Lambert). `clarity` is the depth in metres where
/// the green light has dropped to 37 percent; red is absorbed about 4 times faster and blue about 2 times slower.
inline RGB Transmit(float depth, float clarity) {
    const float c = clarity > 0.05f ? clarity : 0.05f;
    const float d = depth > 0.0f ? depth : 0.0f;
    return {std::exp(-d / c * 4.0f), std::exp(-d / c), std::exp(-d / c * 0.5f)};
}
/// Body colour of the water by depth: shallow colour at the shore, deep colour once the light is gone.
inline RGB DepthColor(float depth, float clarity, const RGB& shallow, const RGB& deep) {
    const float c = clarity > 0.05f ? clarity : 0.05f;
    const float d = depth > 0.0f ? depth : 0.0f;
    return Mix(shallow, deep, 1.0f - std::exp(-d / c));
}
/// Light that comes through a thin crest from behind. `backlight` = dot(view direction, sun direction) (1 = looking
/// into the sun), `crest` 0..1 = how far up the wave this point is.
inline float Scatter(float backlight, float crest, float strength) {
    const float b = Clamp01(backlight);
    return strength * b * b * b * Smooth(0.35f, 1.0f, crest);
}

// ---- foam -------------------------------------------------------------------------------------------------------
/// A ragged foam pattern in [0,1] at a world x/z that slowly changes with time.
inline float FoamPattern(float x, float z, float t) {
    const float a = Fbm(x * 1.7f + t * 0.13f, z * 1.7f - t * 0.09f, 3, 41u);
    const float b = Fbm(x * 4.3f - t * 0.21f, z * 4.3f + t * 0.17f, 2, 57u);
    return Smooth(0.38f, 0.78f, a * 0.65f + b * 0.35f);
}
/// Foam from the wave itself: white caps on the high crests and where the surface is steep. `crest` is height divided
/// by the largest possible height (-1..1), `slope` = 1 - normal.y (0 flat).
inline float CrestFoam(float crest, float slope, float amount, float pattern) {
    if (amount <= 0.0f) return 0.0f;
    const float cap = Smooth(0.45f, 0.95f, crest) * (0.45f + 0.55f * Smooth(0.02f, 0.30f, slope));
    const float steep = Smooth(0.18f, 0.50f, slope) * 0.6f;
    return Clamp01((cap + steep) * amount * (0.35f + 0.65f * pattern));
}
/// Foam where the water meets the ground or an object: a band `width` metres wide that surges in and out with time.
inline float ShoreFoam(float depth, float width, float t, float pattern, float amount) {
    if (amount <= 0.0f || width <= 0.0f) return 0.0f;
    const float surge = 0.65f + 0.35f * std::sin(t * 1.4f + depth * 3.0f);
    const float band = 1.0f - Smooth(0.0f, width * surge, depth);
    return Clamp01(band * (0.55f + 0.45f * pattern) * (0.5f + 0.5f * amount) * (depth > -0.5f ? 1.0f : 0.0f));
}

// ---- rings from splashes --------------------------------------------------------------------------------------------
struct Ring {
    float x = 0, z = 0;     ///< where (water-local metres)
    float t0 = 0;           ///< water time it started
    float strength = 1;     ///< 0..1+, how big the first ring is
};
constexpr float kRingSpeed = 1.5f;     ///< metres per second the ring spreads
constexpr float kRingLife = 2.6f;      ///< seconds until it has faded
/// How far up (metres) a ring lifts the surface at x/z at water time t. Zero before it starts and after it fades.
inline float RingHeight(const Ring& r, float x, float z, float t) {
    const float age = t - r.t0;
    if (age < 0.0f || age > kRingLife) return 0.0f;
    const float dx = x - r.x, dz = z - r.z;
    const float d = std::sqrt(dx * dx + dz * dz);
    const float front = kRingSpeed * age;
    const float w = 0.28f + 0.22f * age;                         // the ring gets wider as it spreads
    const float g = (d - front) / w;
    if (g > 3.5f || g < -3.5f) return 0.0f;
    const float env = std::exp(-age * 1.9f) / (1.0f + 0.9f * front) * r.strength * (1.0f - age / kRingLife);
    return 0.09f * env * std::cos(g * 3.0f) * std::exp(-g * g);
}
inline bool RingAlive(const Ring& r, float t) { return t - r.t0 <= kRingLife; }

// ---- under the surface ------------------------------------------------------------------------------------------
/// Blue-green fog for a camera under the water: how thick the fog is (exponential density per metre) and its colour.
struct UnderwaterFog {
    RGB color;
    float density = 0.08f;
};
inline UnderwaterFog UnderwaterFogFor(const RGB& shallow, const RGB& deep, float clarity, float cameraDepth) {
    UnderwaterFog f;
    const float c = clarity > 0.05f ? clarity : 0.05f;
    f.color = DepthColor(cameraDepth, c * 2.5f, shallow, deep);
    f.density = Clamp01(0.55f / c) * 0.5f + 0.015f;           // murkier water (small clarity) gives thicker fog
    return f;
}
/// Light patterns on the bottom: bright thin ridges where two drifting noise fields cross. 0..1 (about 0.2 average).
inline float Caustics(float x, float z, float t) {
    const float a = Fbm(x * 0.9f + t * 0.20f, z * 0.9f + t * 0.14f, 2, 71u);
    const float b = Fbm(x * 1.1f - t * 0.17f + 9.0f, z * 1.1f + t * 0.19f - 3.0f, 2, 83u);
    const float ridge = 1.0f - std::fabs(a - b) * 5.0f;
    const float r = ridge > 0.0f ? ridge : 0.0f;
    return r * r * r;
}

// ---- sea states ---------------------------------------------------------------------------------------------------
enum class SeaState { Calm = 0, Light = 1, Rough = 2, Storm = 3 };
struct SeaParams {
    float height;       ///< crest to trough, metres
    float length;       ///< main wavelength, metres
    float steepness;    ///< 0..1
    int   count;        ///< waves added together
    float speed;        ///< time scale
    float foam;         ///< foam amount
};
inline SeaParams SeaParamsFor(SeaState s) {
    switch (s) {
        case SeaState::Calm:  return {0.12f, 14.0f, 0.15f, 3, 0.9f, 0.05f};
        case SeaState::Light: return {0.45f, 11.0f, 0.35f, 4, 1.0f, 0.30f};
        case SeaState::Rough: return {1.20f, 16.0f, 0.60f, 6, 1.1f, 0.65f};
        case SeaState::Storm: return {2.60f, 22.0f, 0.85f, 8, 1.3f, 1.00f};
    }
    return {0.45f, 11.0f, 0.35f, 4, 1.0f, 0.30f};
}

// ---- clipmap grid -------------------------------------------------------------------------------------------------
/// A square grid made of nested rings. Level 0 is a full square of n x n cells of size c0; level l >= 1 is a ring of cells
/// of size c0 * 2^l around the previous level. All levels share one centre, which the caller snaps to a multiple of the
/// coarsest cell, so every vertex sits on a fixed lattice and does not slide as the camera moves.
struct RingGrid {
    std::vector<float> x, z;          ///< base (rest) positions, water-local
    std::vector<int> tri;             ///< 3 indices per triangle
    std::vector<int> stitchA, stitchB;///< per vertex: -1, or the two coarse-edge neighbours to average (crack fix)
    std::vector<unsigned char> level; ///< per vertex: finest level that uses it
    int vertexCount() const { return (int)x.size(); }
};
/// `n` cells across each level (rounded up to a multiple of 4, at least 8), `levels` rings (1..8), finest cell `c0`.
/// Vertices beyond `limit` (water-local half size) are pulled onto it when `clampToLimit`, so a finite lake keeps a
/// straight edge.
inline RingGrid BuildRingGrid(int n, int levels, float c0, float cx, float cz, float limit, bool clampToLimit) {
    RingGrid g;
    if (n < 8) n = 8;
    n = (n + 3) / 4 * 4;
    if (levels < 1) levels = 1;
    if (levels > 8) levels = 8;
    const int stride = n + 1;
    std::vector<int> ids((std::size_t)stride * stride);
    for (int l = 0; l < levels; ++l) {
        const float c = c0 * (float)(1 << l);
        const float H = n * c * 0.5f;
        std::fill(ids.begin(), ids.end(), -1);
        auto vid = [&](int i, int j) {
            int& id = ids[(std::size_t)j * stride + i];
            if (id < 0) {
                id = (int)g.x.size();
                float px = cx - H + i * c, pz = cz - H + j * c;
                if (clampToLimit) {
                    px = std::min(limit, std::max(-limit, px));
                    pz = std::min(limit, std::max(-limit, pz));
                }
                g.x.push_back(px); g.z.push_back(pz);
                g.stitchA.push_back(-1); g.stitchB.push_back(-1);
                g.level.push_back((unsigned char)l);
            }
            return id;
        };
        const int q = n / 4;
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i) {
                if (l > 0 && i >= q && i < n - q && j >= q && j < n - q) continue;   // the finer level covers this
                const int a = vid(i, j), b = vid(i + 1, j), cc = vid(i, j + 1), d = vid(i + 1, j + 1);
                g.tri.insert(g.tri.end(), {a, cc, b, b, cc, d});
            }
        // Stitch this level's outer edge to the next (coarser) level: odd vertices along the edge are pulled onto the line
        // between their even neighbours, because the coarse level has no vertex there.
        if (l + 1 < levels) {
            for (int k = 1; k < n; k += 2) {
                const int edge[4][3] = {{k, 0, 0}, {k, n, 0}, {0, k, 1}, {n, k, 1}};
                for (const auto& e : edge) {
                    const int i = e[0], j = e[1];
                    const int ia = e[2] == 0 ? i - 1 : i, ja = e[2] == 0 ? j : j - 1;
                    const int ib = e[2] == 0 ? i + 1 : i, jb = e[2] == 0 ? j : j + 1;
                    const int v = ids[(std::size_t)j * stride + i];
                    const int na = ids[(std::size_t)ja * stride + ia], nb = ids[(std::size_t)jb * stride + ib];
                    if (v >= 0 && na >= 0 && nb >= 0) { g.stitchA[v] = na; g.stitchB[v] = nb; }
                }
            }
        }
    }
    return g;
}

} // namespace watershade
} // namespace okay
