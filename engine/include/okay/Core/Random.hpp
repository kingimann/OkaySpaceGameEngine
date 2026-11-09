#pragma once
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
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
    /// A span too wide for a float (Range(-FLT_MAX, FLT_MAX)) is worked out in double
    /// (it used to overflow to infinity and always return max). A NaN or infinite
    /// limit returns `min` (0 when that is not finite either) instead of poisoning the caller.
    float Range(float min, float max) {
        if (!std::isfinite(min) || !std::isfinite(max)) return std::isfinite(min) ? min : 0.0f;
        const float span = max - min;
        const float r = std::isfinite(span)
            ? min + Value() * span
            : (float)((double)min + (double)Value() * ((double)max - (double)min));
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
    /// True `percent` percent of the time: 0 never, 100 (or more) always, NaN never.
    bool Chance(float percent) { return Value() * 100.0f < percent; }

    /// A new generator for a separate stream (loot, spawns, effects), seeded from this
    /// one's next output, so the child's numbers do not repeat the parent's. It advances
    /// this generator by one draw: fork in a fixed order (or reseed from a name with
    /// SeedFromString) to keep runs reproducible. Streams are independent in practice;
    /// xorshift128+ cannot promise they never overlap.
    Random Fork() { return Random(NextUInt64() ^ 0xD1B54A32D192ED03ull); }

    /// A seed from text, the same on every platform and build (FNV-1a over the bytes,
    /// then mixed): `Random rng(Random::HashSeed("world-42"))` gives one world per name.
    static std::uint64_t HashSeed(const std::string& text) {
        std::uint64_t h = 0xCBF29CE484222325ull;
        for (unsigned char c : text) { h ^= c; h *= 0x100000001B3ull; }
        return SplitMix(h);
    }
    void SeedFromString(const std::string& text) { Seed(HashSeed(text)); }
    static Random FromString(const std::string& text) { return Random(HashSeed(text)); }

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

    /// Process-wide shared generator. It has no lock: use it from one thread (the main
    /// loop). Another thread should use ThreadLocal() or a generator of its own, or take
    /// the lock for the length of its draws with LockShared().
    static Random& Shared() { static Random g(0xC0FFEEull); return g; }

    /// Exclusive use of Shared() for as long as the handle lives, so a worker thread can draw
    /// from the same stream the main thread seeded (a replay, a seeded test) without a data race:
    ///
    ///     { auto rng = Random::LockShared(); a = rng->Range(0, 6); b = rng->Value(); }
    ///
    /// It is a short spin lock (no <mutex>, which a header here must not need): keep the scope to
    /// a few draws, never take it twice on one thread (it would wait for itself), and know that
    /// code that calls Shared() directly does not take it. The stream stays one stream: the numbers
    /// threads draw are the sequential ones in some interleaving, so the sum of N draws is the
    /// same whichever thread got which.
    class SharedLock {
    public:
        SharedLock() { while (Flag().test_and_set(std::memory_order_acquire)) { /* spin */ } }
        ~SharedLock() { Flag().clear(std::memory_order_release); }
        SharedLock(const SharedLock&) = delete;
        SharedLock& operator=(const SharedLock&) = delete;
        Random* operator->() const { return &Shared(); }
        Random& operator*() const { return Shared(); }
    private:
        static std::atomic_flag& Flag() { static std::atomic_flag f = ATOMIC_FLAG_INIT; return f; }
    };
    static SharedLock LockShared() { return SharedLock(); }
    /// A generator private to the calling thread (so no two threads share state),
    /// seeded when the thread first asks, in the order threads ask.
    static Random& ThreadLocal() {
        static std::atomic<std::uint64_t> next{0};
        thread_local Random g(HashSeed("okay.thread." + std::to_string(next.fetch_add(1))));
        return g;
    }

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
