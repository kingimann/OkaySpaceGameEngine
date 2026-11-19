#pragma once
// ---------------------------------------------------------------------------
// Light probes: a small grid of baked ambient light samples, interpolated at runtime.
//
// Each probe stores the light arriving from all directions as first-order spherical harmonics (4 coefficients per colour:
// the average and one per axis). Looking up a surface point and its normal blends the 8 nearest probes and evaluates the
// 4 coefficients, so ambient light changes from place to place (dark under a roof, blue-ish near the sky, warm near a lit
// wall) instead of being one colour for the whole scene.
//
// Written from the public definition of spherical harmonics (the constants 0.282095 and 0.488603 are the real basis
// functions; the (2/3) factor is the standard cosine-lobe convolution of the first band) in our own words and structure.
// Pure data and maths (no Scene), so tests and every renderer can use it. Baking is in LightProbeBake.hpp.
// ---------------------------------------------------------------------------
#include "okay/Math/Vec3.hpp"
#include <cmath>
#include <vector>

namespace okay {

struct LightProbeGrid {
    bool  enabled = true;                  // use the baked probes (when there are any)
    Vec3  origin{0, 0, 0};                 // world position of probe (0, 0, 0)
    float spacing = 2.0f;                  // metres between probes
    int   nx = 0, ny = 0, nz = 0;          // probe counts
    std::vector<float> sh;                 // per probe 12 floats: L00 rgb, L1(y) rgb, L1(z) rgb, L1(x) rgb

    bool Baked() const { return nx > 0 && ny > 0 && nz > 0 && sh.size() == (std::size_t)nx * ny * nz * 12; }
    void Clear() { nx = ny = nz = 0; sh.clear(); }
    int  Count() const { return Baked() ? nx * ny * nz : 0; }

    static constexpr float kY00 = 0.282095f, kY1 = 0.488603f;

    /// Add one radiance sample (colour `rgb` arriving from unit direction `d`) with weight `w` into 12 coefficients.
    static void Accumulate(float* c, const Vec3& d, const float* rgb, float w) {
        const float b[4] = {kY00, kY1 * d.y, kY1 * d.z, kY1 * d.x};
        for (int k = 0; k < 4; ++k) for (int ch = 0; ch < 3; ++ch) c[k * 3 + ch] += rgb[ch] * b[k] * w;
    }
    /// The ambient colour a surface with unit normal `n` receives from coefficients `c` (cosine-weighted, divided by pi so that
    /// a uniform sky of colour C gives exactly C).
    static Vec3 Evaluate(const float* c, const Vec3& n) {
        const float n1[3] = {n.y, n.z, n.x};
        float out[3];
        for (int ch = 0; ch < 3; ++ch) {
            float v = kY00 * c[ch];
            for (int k = 0; k < 3; ++k) v += (2.0f / 3.0f) * kY1 * n1[k] * c[(k + 1) * 3 + ch];
            out[ch] = v < 0.0f ? 0.0f : v;
        }
        return Vec3{out[0], out[1], out[2]};
    }
    /// Blend the 8 probes around world point `p` and evaluate for normal `n`. Outside the grid the nearest edge probes are used.
    Vec3 Ambient(const Vec3& p, const Vec3& n) const {
        const float inv = spacing > 1e-4f ? 1.0f / spacing : 1.0f;
        float gx = (p.x - origin.x) * inv, gy = (p.y - origin.y) * inv, gz = (p.z - origin.z) * inv;
        auto clampf = [](float v, float hi) { return v < 0.0f ? 0.0f : (v > hi ? hi : v); };
        gx = clampf(gx, (float)(nx - 1)); gy = clampf(gy, (float)(ny - 1)); gz = clampf(gz, (float)(nz - 1));
        const int x0 = (int)gx, y0 = (int)gy, z0 = (int)gz;
        const int x1 = x0 + 1 < nx ? x0 + 1 : x0, y1 = y0 + 1 < ny ? y0 + 1 : y0, z1 = z0 + 1 < nz ? z0 + 1 : z0;
        const float fx = gx - (float)x0, fy = gy - (float)y0, fz = gz - (float)z0;
        float c[12] = {0};
        for (int k = 0; k < 8; ++k) {
            const int ix = (k & 1) ? x1 : x0, iy = (k & 2) ? y1 : y0, iz = (k & 4) ? z1 : z0;
            const float w = ((k & 1) ? fx : 1.0f - fx) * ((k & 2) ? fy : 1.0f - fy) * ((k & 4) ? fz : 1.0f - fz);
            const float* s = &sh[((std::size_t)(iz * ny + iy) * nx + ix) * 12];
            for (int j = 0; j < 12; ++j) c[j] += s[j] * w;
        }
        return Evaluate(c, n);
    }
};

/// The grid the renderers use this frame (set by ApplySceneLight; null = no probes, the flat/hemisphere ambient is used).
inline const LightProbeGrid*& ActiveLightProbes() { static const LightProbeGrid* g = nullptr; return g; }

} // namespace okay
