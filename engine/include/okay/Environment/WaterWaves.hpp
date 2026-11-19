#pragma once
// WaterWaves: a sum of a few travelling waves, the same function for the picture and for the physics.
//
// Written from the published Gerstner ("trochoidal") wave formula, in our own words and structure (no code from
// any other project). A point of water that rests at (x0, z0) is carried round a small circle by each wave i:
//
//     theta_i = k_i * (D_i . (x0, z0)) - omega_i * t + phase_i
//     x = x0 + sum Q_i * A_i * D_i.x * cos(theta_i)
//     z = z0 + sum Q_i * A_i * D_i.z * cos(theta_i)
//     y =      sum A_i * sin(theta_i)
//
// A_i amplitude, k_i = 2 pi / wavelength, D_i the direction the wave runs in, omega_i = sqrt(g * k_i) (deep-water
// dispersion, so long waves run faster), Q_i how pointed the crest is (0 = plain sine, sum of Q*k*A <= 1 never
// loops). Because the points move sideways as well as up, the crests are sharper than the troughs, which looks
// like the sea.
//
// Height(x, z, t) is "how high is the surface above this world x/z": the rest position behind (x, z) is found by
// a short fixed-point loop (the sideways shift is a contraction while the steepness is below 1). Normal and
// Velocity are the exact derivatives of the same formula, so a boat, a crate and the drawn mesh all agree.
// Everything is seeded and deterministic: the same Params and time give the same numbers on any machine.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace okay {
namespace waves {

constexpr int kMaxWaves = 8;

/// The few numbers a person sets. Everything else follows from them and the seed.
struct Params {
    int   count        = 4;       ///< how many waves are added (1..8)
    float height       = 0.35f;   ///< crest to trough of the whole swell, metres
    float length       = 9.0f;    ///< main wavelength, metres (the other waves are shorter)
    float directionDeg = 0.0f;    ///< direction the main wave runs in, degrees from +X towards +Z
    float spreadDeg    = 35.0f;   ///< how far the other waves may differ in direction
    float steepness    = 0.5f;    ///< 0 = smooth sine .. 1 = sharpest crest that still does not fold over
    std::uint32_t seed = 7u;      ///< same seed = same sea

    bool operator==(const Params& o) const {
        return count == o.count && height == o.height && length == o.length && directionDeg == o.directionDeg &&
               spreadDeg == o.spreadDeg && steepness == o.steepness && seed == o.seed;
    }
    bool operator!=(const Params& o) const { return !(*this == o); }
};

/// What the surface does at one place and time.
struct Sample {
    float height = 0.0f;                  ///< surface height (y)
    float nx = 0.0f, ny = 1.0f, nz = 0.0f;    ///< unit normal, pointing up
    float vx = 0.0f, vy = 0.0f, vz = 0.0f;    ///< velocity of the water there, metres per second
};

class Field {
public:
    Field() { Build(Params{}); }
    explicit Field(const Params& p) { Build(p); }

    const Params& params() const { return m_params; }
    int count() const { return m_n; }

    void Build(const Params& in) {
        m_params = in;
        Params p = in;
        if (!(p.count >= 1)) p.count = 1;
        if (p.count > kMaxWaves) p.count = kMaxWaves;
        if (!std::isfinite(p.height) || p.height < 0.0f) p.height = 0.0f;
        if (!std::isfinite(p.length) || p.length < 0.1f) p.length = 0.1f;
        if (!std::isfinite(p.steepness)) p.steepness = 0.0f;
        p.steepness = std::min(1.0f, std::max(0.0f, p.steepness));
        if (!std::isfinite(p.directionDeg)) p.directionDeg = 0.0f;
        if (!std::isfinite(p.spreadDeg)) p.spreadDeg = 0.0f;
        m_n = p.count;

        // Each wave gets 0.65 times the amplitude of the one before; together they add up to half the height.
        float weights[kMaxWaves], total = 0.0f;
        for (int i = 0; i < m_n; ++i) { weights[i] = std::pow(0.65f, (float)i); total += weights[i]; }
        const float halfHeight = p.height * 0.5f;
        std::uint64_t rng = 0x9E3779B97F4A7C15ull ^ ((std::uint64_t)p.seed * 0xD1B54A32D192ED03ull);
        const float kTwoPi = 6.28318530718f, kDeg = 0.01745329252f, kG = 9.81f;
        for (int i = 0; i < m_n; ++i) {
            Wave& w = m_w[i];
            const float r1 = Next(rng), r2 = Next(rng), r3 = Next(rng);
            w.A = halfHeight * weights[i] / total;
            const float len = p.length / (1.0f + 0.55f * (float)i) * (0.9f + 0.2f * r1);
            w.k = kTwoPi / std::max(0.05f, len);
            w.omega = std::sqrt(kG * w.k);
            const float ang = (p.directionDeg + (i == 0 ? (r2 * 2.0f - 1.0f) * 0.25f : (r2 * 2.0f - 1.0f)) * p.spreadDeg) * kDeg;
            w.dx = std::cos(ang); w.dz = std::sin(ang);
            w.phase = r3 * kTwoPi;
            w.Q = (w.A > 1e-6f) ? p.steepness / (w.k * w.A * (float)m_n) : 0.0f;
        }
    }

    /// Where the bit of water that rests at (x0, z0) is at time t (the drawn mesh uses this, one call per vertex).
    void Displace(float x0, float z0, float t, float& x, float& y, float& z) const {
        x = x0; y = 0.0f; z = z0;
        for (int i = 0; i < m_n; ++i) {
            const Wave& w = m_w[i];
            const float th = w.k * (w.dx * x0 + w.dz * z0) - w.omega * t + w.phase;
            const float c = std::cos(th), s = std::sin(th);
            x += w.Q * w.A * w.dx * c;
            z += w.Q * w.A * w.dz * c;
            y += w.A * s;
        }
    }

    /// Height, normal and water velocity at a world x/z.
    Sample At(float x, float z, float t) const {
        float x0, z0;
        RestPosition(x, z, t, x0, z0);
        Sample out;
        float nx = 0.0f, nz = 0.0f, ny = 1.0f, vx = 0.0f, vy = 0.0f, vz = 0.0f, h = 0.0f;
        for (int i = 0; i < m_n; ++i) {
            const Wave& w = m_w[i];
            const float th = w.k * (w.dx * x0 + w.dz * z0) - w.omega * t + w.phase;
            const float c = std::cos(th), s = std::sin(th);
            h += w.A * s;
            nx -= w.dx * w.k * w.A * c;
            nz -= w.dz * w.k * w.A * c;
            ny -= w.Q * w.k * w.A * s;
            vx += w.Q * w.A * w.omega * w.dx * s;
            vz += w.Q * w.A * w.omega * w.dz * s;
            vy -= w.A * w.omega * c;
        }
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        out.height = h;
        if (len > 1e-6f) { out.nx = nx / len; out.ny = ny / len; out.nz = nz / len; }
        out.vx = vx; out.vy = vy; out.vz = vz;
        return out;
    }

    float Height(float x, float z, float t) const { return At(x, z, t).height; }

    /// Largest height the surface can reach above its rest level (sum of the amplitudes).
    float MaxAmplitude() const { float a = 0.0f; for (int i = 0; i < m_n; ++i) a += m_w[i].A; return a; }

private:
    struct Wave { float A = 0, k = 1, omega = 1, dx = 1, dz = 0, phase = 0, Q = 0; };
    Params m_params;
    Wave m_w[kMaxWaves];
    int m_n = 1;

    // SplitMix64, mapped to [0, 1).
    static float Next(std::uint64_t& s) {
        s += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = s;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return (float)((z >> 40) & 0xFFFFFFu) / 16777216.0f;
    }

    // The rest position (x0, z0) of the water that is at world (x, z) at time t: undo the sideways shift by iterating.
    void RestPosition(float x, float z, float t, float& x0, float& z0) const {
        x0 = x; z0 = z;
        for (int it = 0; it < 12; ++it) {
            float sx = 0.0f, sz = 0.0f;
            for (int i = 0; i < m_n; ++i) {
                const Wave& w = m_w[i];
                const float th = w.k * (w.dx * x0 + w.dz * z0) - w.omega * t + w.phase;
                const float c = std::cos(th);
                sx += w.Q * w.A * w.dx * c;
                sz += w.Q * w.A * w.dz * c;
            }
            const float nx0 = x - sx, nz0 = z - sz;
            const float moved = std::fabs(nx0 - x0) + std::fabs(nz0 - z0);
            x0 = nx0; z0 = nz0;
            if (moved < 1e-5f) break;
        }
    }
};

} // namespace waves
} // namespace okay
