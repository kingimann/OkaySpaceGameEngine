#pragma once
// One shared, seedable, allocation-free noise library: gradient noise (Perlin),
// simplex, value noise, Worley / cellular, plus fBm / ridged / turbulence layering.
//
//   NoiseField n(1234);                  // same seed => identical output on every platform
//   float h = n.Fbm2(NoiseType::Perlin, x * 0.01f, z * 0.01f);   // [-1, 1]
//
// Ranges: Perlin2/3, Simplex2/3, Value2/3 and the Fbm functions return [-1, 1];
// Ridged and Turbulence return [0, 1]; Worley returns distances in cell units.
// All results are pure functions of (seed, coordinates): no global state, any thread.
#include "okay/Math/Mathf.hpp"
#include <cstdint>

namespace okay {

enum class NoiseType { Perlin, Simplex, Value };

struct FractalParams {
    int   octaves    = 4;
    float lacunarity = 2.0f;   // frequency multiplier per octave
    float gain       = 0.5f;   // amplitude multiplier per octave
};

struct WorleyResult {
    float f1 = 0.0f;   // distance to the nearest feature point
    float f2 = 0.0f;   // distance to the second nearest
    float id = 0.0f;   // stable per-cell value in [0, 1) of the nearest point's cell
};

class NoiseField {
public:
    explicit NoiseField(std::uint32_t seed = 0) : m_seed(seed) {}
    std::uint32_t Seed() const { return m_seed; }
    void SetSeed(std::uint32_t s) { m_seed = s; }

    /// Lattice hash (the basis of every function here); exposed for custom noise.
    std::uint32_t Hash(int x, int y = 0, int z = 0) const {
        std::uint32_t h = m_seed * 0x9E3779B1u ^ ((std::uint32_t)x * 0x85EBCA6Bu) ^
                          ((std::uint32_t)y * 0xC2B2AE35u) ^ ((std::uint32_t)z * 0x27D4EB2Fu);
        h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
        return h;
    }
    /// Hash as a float in [0, 1).
    float Hash01(int x, int y = 0, int z = 0) const { return (Hash(x, y, z) >> 8) * (1.0f / 16777216.0f); }

    // ---- value noise ----
    float Value2(float x, float y) const {
        int xi = FloorI(x), yi = FloorI(y);
        float fx = Fade(x - xi), fy = Fade(y - yi);
        float a = V(xi, yi, 0), b = V(xi + 1, yi, 0), c = V(xi, yi + 1, 0), d = V(xi + 1, yi + 1, 0);
        return Lerp(Lerp(a, b, fx), Lerp(c, d, fx), fy);
    }
    float Value3(float x, float y, float z) const {
        int xi = FloorI(x), yi = FloorI(y), zi = FloorI(z);
        float fx = Fade(x - xi), fy = Fade(y - yi), fz = Fade(z - zi);
        float l[2];
        for (int k = 0; k < 2; ++k) {
            float a = V(xi, yi, zi + k), b = V(xi + 1, yi, zi + k), c = V(xi, yi + 1, zi + k), d = V(xi + 1, yi + 1, zi + k);
            l[k] = Lerp(Lerp(a, b, fx), Lerp(c, d, fx), fy);
        }
        return Lerp(l[0], l[1], fz);
    }

    // ---- gradient (Perlin) noise ----
    float Perlin2(float x, float y) const {
        int xi = FloorI(x), yi = FloorI(y);
        float xf = x - xi, yf = y - yi;
        float u = Fade(xf), v = Fade(yf);
        float n00 = G2(Hash(xi, yi), xf, yf),         n10 = G2(Hash(xi + 1, yi), xf - 1, yf);
        float n01 = G2(Hash(xi, yi + 1), xf, yf - 1), n11 = G2(Hash(xi + 1, yi + 1), xf - 1, yf - 1);
        return Clamp1(Lerp(Lerp(n00, n10, u), Lerp(n01, n11, u), v));
    }
    float Perlin3(float x, float y, float z) const {
        int xi = FloorI(x), yi = FloorI(y), zi = FloorI(z);
        float xf = x - xi, yf = y - yi, zf = z - zi;
        float u = Fade(xf), v = Fade(yf), w = Fade(zf);
        float n[2][2][2];
        for (int dz = 0; dz < 2; ++dz) for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx)
            n[dz][dy][dx] = G3(Hash(xi + dx, yi + dy, zi + dz), xf - dx, yf - dy, zf - dz);
        float x00 = Lerp(n[0][0][0], n[0][0][1], u), x10 = Lerp(n[0][1][0], n[0][1][1], u);
        float x01 = Lerp(n[1][0][0], n[1][0][1], u), x11 = Lerp(n[1][1][0], n[1][1][1], u);
        return Clamp1(Lerp(Lerp(x00, x10, v), Lerp(x01, x11, v), w));
    }

    // ---- simplex noise ----
    float Simplex2(float x, float y) const {
        const float F2 = 0.3660254037844386f, G2c = 0.21132486540518713f;
        float s = (x + y) * F2;
        int i = FloorI(x + s), j = FloorI(y + s);
        float t = (i + j) * G2c;
        float x0 = x - (i - t), y0 = y - (j - t);
        int i1 = x0 > y0 ? 1 : 0, j1 = 1 - i1;
        float x1 = x0 - i1 + G2c, y1 = y0 - j1 + G2c;
        float x2 = x0 - 1.0f + 2.0f * G2c, y2 = y0 - 1.0f + 2.0f * G2c;
        float n = 0.0f;
        float t0 = 0.5f - x0 * x0 - y0 * y0; if (t0 > 0) { t0 *= t0; n += t0 * t0 * G2(Hash(i, j), x0, y0); }
        float t1 = 0.5f - x1 * x1 - y1 * y1; if (t1 > 0) { t1 *= t1; n += t1 * t1 * G2(Hash(i + i1, j + j1), x1, y1); }
        float t2 = 0.5f - x2 * x2 - y2 * y2; if (t2 > 0) { t2 *= t2; n += t2 * t2 * G2(Hash(i + 1, j + 1), x2, y2); }
        return Clamp1(70.0f * n);
    }
    float Simplex3(float x, float y, float z) const {
        const float F3 = 1.0f / 3.0f, G3c = 1.0f / 6.0f;
        float s = (x + y + z) * F3;
        int i = FloorI(x + s), j = FloorI(y + s), k = FloorI(z + s);
        float t = (i + j + k) * G3c;
        float x0 = x - (i - t), y0 = y - (j - t), z0 = z - (k - t);
        int i1, j1, k1, i2, j2, k2;
        if (x0 >= y0) {
            if (y0 >= z0)      { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
            else if (x0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1; }
            else               { i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1; }
        } else {
            if (y0 < z0)       { i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1; }
            else if (x0 < z0)  { i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1; }
            else               { i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
        }
        float x1 = x0 - i1 + G3c, y1 = y0 - j1 + G3c, z1 = z0 - k1 + G3c;
        float x2 = x0 - i2 + 2 * G3c, y2 = y0 - j2 + 2 * G3c, z2 = z0 - k2 + 2 * G3c;
        float x3 = x0 - 1 + 3 * G3c, y3 = y0 - 1 + 3 * G3c, z3 = z0 - 1 + 3 * G3c;
        float n = 0.0f;
        float t0 = 0.6f - x0*x0 - y0*y0 - z0*z0; if (t0 > 0) { t0 *= t0; n += t0 * t0 * G3(Hash(i, j, k), x0, y0, z0); }
        float t1 = 0.6f - x1*x1 - y1*y1 - z1*z1; if (t1 > 0) { t1 *= t1; n += t1 * t1 * G3(Hash(i + i1, j + j1, k + k1), x1, y1, z1); }
        float t2 = 0.6f - x2*x2 - y2*y2 - z2*z2; if (t2 > 0) { t2 *= t2; n += t2 * t2 * G3(Hash(i + i2, j + j2, k + k2), x2, y2, z2); }
        float t3 = 0.6f - x3*x3 - y3*y3 - z3*z3; if (t3 > 0) { t3 *= t3; n += t3 * t3 * G3(Hash(i + 1, j + 1, k + 1), x3, y3, z3); }
        return Clamp1(32.0f * n);
    }

    // ---- Worley / cellular ----
    /// `jitter` 0 = points on a regular lattice, 1 = fully random inside each cell.
    WorleyResult Worley2(float x, float y, float jitter = 1.0f) const {
        int xi = FloorI(x), yi = FloorI(y);
        WorleyResult r; float f1 = 1e30f, f2 = 1e30f; std::uint32_t best = 0;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            int cx = xi + dx, cy = yi + dy;
            std::uint32_t h = Hash(cx, cy, 0x51ED);
            float px = cx + 0.5f + (U(h) - 0.5f) * jitter;
            float py = cy + 0.5f + (U(h * 0x9E3779B1u + 0x7F4A7C15u) - 0.5f) * jitter;
            float d = (x - px) * (x - px) + (y - py) * (y - py);
            if (d < f1) { f2 = f1; f1 = d; best = h; } else if (d < f2) f2 = d;
        }
        r.f1 = Mathf::Sqrt(f1); r.f2 = Mathf::Sqrt(f2); r.id = U(best ^ 0xA5A5A5A5u);
        return r;
    }
    WorleyResult Worley3(float x, float y, float z, float jitter = 1.0f) const {
        int xi = FloorI(x), yi = FloorI(y), zi = FloorI(z);
        WorleyResult r; float f1 = 1e30f, f2 = 1e30f; std::uint32_t best = 0;
        for (int dz = -1; dz <= 1; ++dz) for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            int cx = xi + dx, cy = yi + dy, cz = zi + dz;
            std::uint32_t h = Hash(cx, cy, cz);
            std::uint32_t h2 = h * 0x9E3779B1u + 0x7F4A7C15u, h3 = h2 * 0x9E3779B1u + 0x7F4A7C15u;
            float px = cx + 0.5f + (U(h) - 0.5f) * jitter;
            float py = cy + 0.5f + (U(h2) - 0.5f) * jitter;
            float pz = cz + 0.5f + (U(h3) - 0.5f) * jitter;
            float d = (x - px) * (x - px) + (y - py) * (y - py) + (z - pz) * (z - pz);
            if (d < f1) { f2 = f1; f1 = d; best = h; } else if (d < f2) f2 = d;
        }
        r.f1 = Mathf::Sqrt(f1); r.f2 = Mathf::Sqrt(f2); r.id = U(best ^ 0xA5A5A5A5u);
        return r;
    }

    // ---- selectable basis + layering ----
    float Sample2(NoiseType t, float x, float y) const {
        return t == NoiseType::Perlin ? Perlin2(x, y) : t == NoiseType::Simplex ? Simplex2(x, y) : Value2(x, y);
    }
    float Sample3(NoiseType t, float x, float y, float z) const {
        return t == NoiseType::Perlin ? Perlin3(x, y, z) : t == NoiseType::Simplex ? Simplex3(x, y, z) : Value3(x, y, z);
    }

    /// Fractal Brownian motion, normalised to [-1, 1].
    float Fbm2(NoiseType t, float x, float y, const FractalParams& p = {}) const {
        return Layer2(t, x, y, p, 0);
    }
    float Fbm3(NoiseType t, float x, float y, float z, const FractalParams& p = {}) const {
        return Layer3(t, x, y, z, p, 0);
    }
    /// Ridged multifractal: sharp crests where the base noise crosses zero. [0, 1].
    float Ridged2(NoiseType t, float x, float y, const FractalParams& p = {}) const {
        return Layer2(t, x, y, p, 1);
    }
    float Ridged3(NoiseType t, float x, float y, float z, const FractalParams& p = {}) const {
        return Layer3(t, x, y, z, p, 1);
    }
    /// Sum of |noise| octaves (billowy / cloud-like creases). [0, 1].
    float Turbulence2(NoiseType t, float x, float y, const FractalParams& p = {}) const {
        return Layer2(t, x, y, p, 2);
    }
    float Turbulence3(NoiseType t, float x, float y, float z, const FractalParams& p = {}) const {
        return Layer3(t, x, y, z, p, 2);
    }

private:
    static int FloorI(float v) { return (int)Mathf::Floor(v); }
    static float Fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
    static float Lerp(float a, float b, float t) { return a + (b - a) * t; }
    static float Clamp1(float v) { return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); }
    static float U(std::uint32_t h) { return (h >> 8) * (1.0f / 16777216.0f); }
    float V(int x, int y, int z) const { return Hash01(x, y, z) * 2.0f - 1.0f; }

    static float G2(std::uint32_t h, float x, float y) {
        static const float g[8][2] = {{1,1},{-1,1},{1,-1},{-1,-1},{1,0},{-1,0},{0,1},{0,-1}};
        const float* a = g[h & 7u];
        return a[0] * x + a[1] * y;
    }
    static float G3(std::uint32_t h, float x, float y, float z) {
        static const float g[12][3] = {{1,1,0},{-1,1,0},{1,-1,0},{-1,-1,0},{1,0,1},{-1,0,1},
                                       {1,0,-1},{-1,0,-1},{0,1,1},{0,-1,1},{0,1,-1},{0,-1,-1}};
        const float* a = g[(h >> 3) % 12u];
        return a[0] * x + a[1] * y + a[2] * z;
    }

    // mode 0 fbm, 1 ridged, 2 turbulence
    static float Combine(float n, int mode, float& weight) {
        if (mode == 0) return n;
        if (mode == 2) return n < 0 ? -n : n;
        float s = 1.0f - (n < 0 ? -n : n);   // ridged: crest at the zero crossing
        s *= s;
        float out = s * weight;
        weight = Mathf::Clamp01(out * 2.0f);
        return out;
    }
    float Layer2(NoiseType t, float x, float y, const FractalParams& p, int mode) const {
        float sum = 0, amp = 1, norm = 0, freq = 1, weight = 1;
        int oct = p.octaves < 1 ? 1 : (p.octaves > 16 ? 16 : p.octaves);
        for (int o = 0; o < oct; ++o) {
            // A per-octave offset keeps octaves from lining up on the lattice origin.
            float off = o * 17.37f;
            sum += Combine(Sample2(t, x * freq + off, y * freq - off), mode, weight) * amp;
            norm += amp; amp *= p.gain; freq *= p.lacunarity;
        }
        return norm > 0 ? sum / norm : 0.0f;
    }
    float Layer3(NoiseType t, float x, float y, float z, const FractalParams& p, int mode) const {
        float sum = 0, amp = 1, norm = 0, freq = 1, weight = 1;
        int oct = p.octaves < 1 ? 1 : (p.octaves > 16 ? 16 : p.octaves);
        for (int o = 0; o < oct; ++o) {
            float off = o * 17.37f;
            sum += Combine(Sample3(t, x * freq + off, y * freq - off, z * freq + off * 0.5f), mode, weight) * amp;
            norm += amp; amp *= p.gain; freq *= p.lacunarity;
        }
        return norm > 0 ? sum / norm : 0.0f;
    }

    std::uint32_t m_seed;
};

} // namespace okay
