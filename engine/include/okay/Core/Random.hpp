#pragma once
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
#include <cstdint>

namespace okay {

/// Deterministic, seedable pseudo-random generator (xorshift128+ style), modeled
/// after Unity's `Random`. There is a shared global instance plus instantiable
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
    float Value() { return (NextUInt64() >> 11) * (1.0f / 9007199254740992.0f); }

    /// Float in [min, max).
    float Range(float min, float max) { return min + Value() * (max - min); }
    /// Integer in [min, max) (max exclusive).
    int Range(int min, int max) {
        if (max <= min) return min;
        return min + static_cast<int>(NextUInt64() % static_cast<std::uint64_t>(max - min));
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

    /// Process-wide shared generator (matching Unity's static API style).
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
