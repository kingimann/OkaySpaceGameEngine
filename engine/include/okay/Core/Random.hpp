#pragma once
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace okay {

/// Deterministic, seedable pseudo-random generator (xorshift128+ style), modeled
/// after `Random`. There is a shared global instance plus instantiable
/// generators for independent, reproducible streams.
class Random {
public:
    Random() : Random(0x9E3779B97F4A7C15ull) {}
    explicit Random(std::uint64_t seed) { Seed(seed); }

    void Seed(std::uint64_t seed) {
        // SplitMix64 to initialize the state from a single seed.
        m_s[0] = SplitMix(seed);
        m_s[1] = SplitMix(seed);
        if (m_s[0] == 0 && m_s[1] == 0) m_s[0] = 1;
    }

    /// The whole generator state, to save and restore a stream exactly.
    struct State { std::uint64_t s0 = 1, s1 = 2; };
    State GetState() const { return {m_s[0], m_s[1]}; }
    void SetState(const State& st) {
        m_s[0] = st.s0; m_s[1] = st.s1;
        if (m_s[0] == 0 && m_s[1] == 0) m_s[0] = 1;   // the all-zero state never leaves zero
    }

    /// Reseed from the clock and an address, so each run differs (the default seed
    /// is fixed, which makes every run identical).
    void Randomize() {
        int local = 0;
        const auto t = (std::uint64_t)std::chrono::steady_clock::now().time_since_epoch().count();
        const auto w = (std::uint64_t)std::chrono::system_clock::now().time_since_epoch().count();
        Seed(t ^ (w * 0x9E3779B97F4A7C15ull) ^ (std::uint64_t)(std::uintptr_t)&local ^ (std::uint64_t)(std::uintptr_t)this);
    }

    /// Raw 64-bit value.
    std::uint64_t NextUInt64() {
        std::uint64_t s1 = m_s[0];
        const std::uint64_t s0 = m_s[1];
        m_s[0] = s0;
        s1 ^= s1 << 23;
        m_s[1] = s1 ^ s0 ^ (s1 >> 18) ^ (s0 >> 5);
        return m_s[1] + s0;
    }

    /// Float in [0, 1).
    float Value() { return ToUnitFloat(NextUInt64()); }

    /// A raw 64-bit value as a float in [0, 1): the top 24 bits, which a float holds
    /// exactly, so the largest input gives 1 - 2^-24 and never rounds up to 1.
    static float ToUnitFloat(std::uint64_t raw) {
        return (float)(raw >> 40) * (1.0f / 16777216.0f);
    }

    /// Float in [min, max).
    /// Rounding in `min + v * (max - min)` can land on max; that is clamped back below it.
    float Range(float min, float max) {
        const float r = min + Value() * (max - min);
        return (r >= max && max > min) ? std::nextafter(max, min) : r;
    }
    /// Integer in [min, max) (max exclusive). Unbiased, and safe for the full int range.
    int Range(int min, int max) {
        if (max <= min) return min;
        const std::uint64_t span = (std::uint64_t)((std::int64_t)max - (std::int64_t)min);
        return (int)((std::int64_t)min + (std::int64_t)Below(span));
    }

    /// Uniform integer in [0, n) by rejection sampling (n > 0).
    std::uint64_t Below(std::uint64_t n) {
        if (n <= 1) return 0;
        const std::uint64_t rem = (0ull - n) % n;   // 2^64 mod n
        std::uint64_t x;
        do { x = NextUInt64(); } while (x < rem);   // x >= rem leaves a whole number of n-blocks
        return x % n;
    }

    /// Normally distributed float (Box-Muller).
    float Gaussian(float mean = 0.0f, float stddev = 1.0f) {
        float u1 = Value();
        if (u1 < 1e-7f) u1 = 1e-7f;
        const float u2 = Value();
        return mean + stddev * std::sqrt(-2.0f * std::log(u1)) * std::cos(6.2831853f * u2);
    }

    /// Fisher-Yates shuffle in place.
    template <typename T>
    void Shuffle(std::vector<T>& v) {
        for (std::size_t i = v.size(); i > 1; --i)
            std::swap(v[i - 1], v[(std::size_t)Below(i)]);
    }

    /// An index chosen with probability proportional to its weight. Negative and
    /// non-finite weights count as 0; with no positive weight, returns 0 (-1 when empty).
    int Weighted(const std::vector<float>& weights) {
        if (weights.empty()) return -1;
        double total = 0.0;
        for (float w : weights) if (w > 0.0f && std::isfinite(w)) total += w;
        if (!(total > 0.0)) return 0;
        const double pick = (double)Value() * total;
        double acc = 0.0;
        int last = 0;
        for (std::size_t i = 0; i < weights.size(); ++i) {
            const float w = weights[i];
            if (!(w > 0.0f) || !std::isfinite(w)) continue;
            acc += w; last = (int)i;
            if (pick < acc) return last;
        }
        return last;   // rounding left pick == total: the last positive weight
    }

    bool Bool() { return (NextUInt64() & 1ull) != 0; }

    Vec2 InsideUnitCircle() {
        // Rejection sampling for a uniform disk.
        for (;;) {
            float x = Range(-1.0f, 1.0f), y = Range(-1.0f, 1.0f);
            if (x * x + y * y <= 1.0f) return {x, y};
        }
    }
    Vec3 OnUnitSphere() {
        float z = Range(-1.0f, 1.0f);
        float t = Range(0.0f, 6.2831853f);
        float r = Mathf::Sqrt(Mathf::Max(0.0f, 1.0f - z * z));
        return {r * Mathf::Cos(t), r * Mathf::Sin(t), z};
    }

    /// Process-wide shared generator.
    static Random& Shared() { static Random g(0xC0FFEEull); return g; }

private:
    static std::uint64_t SplitMix(std::uint64_t& x) {
        std::uint64_t z = (x += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    std::uint64_t m_s[2] = {1, 2};
};

} // namespace okay
