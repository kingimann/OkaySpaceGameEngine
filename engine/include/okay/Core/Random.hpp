#pragma once
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
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
    /// A copy is an ordinary generator with the same state: it never takes Shared()'s lock, so copying
    /// Shared() gives an independent stream (a worker's own copy) and not a second handle on the shared one.
    Random(const Random& o) { const State st = o.GetState(); m_s[0] = st.s0; m_s[1] = st.s1; }
    /// Copies the state only: assigning to Shared() keeps it Shared() (and locked); assigning Shared() to a
    /// local copies a consistent snapshot of it.
    Random& operator=(const Random& o) { if (this != &o) SetState(o.GetState()); return *this; }

    void Seed(std::uint64_t seed) {
        // SplitMix64 to initialize the state from a single seed.
        const std::uint64_t a = SplitMix(seed);
        const std::uint64_t b = SplitMix(seed);
        SharedGuard guard(m_shared);
        m_s[0] = a;
        m_s[1] = b;
        if (m_s[0] == 0 && m_s[1] == 0) m_s[0] = 1;
    }

    /// The whole generator state, to save and restore a stream exactly. On Shared() both words are read
    /// under its lock, so a snapshot taken while another thread draws is a real state of the sequence.
    struct State { std::uint64_t s0 = 1, s1 = 2; };
    State GetState() const {
        SharedGuard guard(m_shared);
        return {m_s[0], m_s[1]};
    }
    void SetState(const State& st) {
        SharedGuard guard(m_shared);
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

    /// Raw 64-bit value. Every draw, and so every other method, is one step of the generator; on
    /// Random::Shared() that step is taken under the shared lock (see Shared()), on any other
    /// generator it is a plain, unlocked step.
    std::uint64_t NextUInt64() {
        SharedGuard guard(m_shared);
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

    /// Normally distributed float (Box-Muller). Always consumes two draws. A NaN or infinite `mean` counts
    /// as 0 and a NaN or infinite `stddev` as 0 (the result is then the mean), and a result beyond the float
    /// range is held at its edge, so a bad argument never hands back NaN or infinity.
    float Gaussian(float mean = 0.0f, float stddev = 1.0f) {
        float u1 = Value();
        if (u1 < 1e-7f) u1 = 1e-7f;
        const float u2 = Value();
        if (!std::isfinite(mean)) mean = 0.0f;
        if (!std::isfinite(stddev)) stddev = 0.0f;
        const float r = mean + stddev * std::sqrt(-2.0f * std::log(u1)) * std::cos(6.2831853f * u2);
        if (std::isfinite(r)) return r;
        if (std::isnan(r)) return mean;   // inf * 0 at an exact zero of the cosine
        return r > 0.0f ? std::numeric_limits<float>::max() : -std::numeric_limits<float>::max();   // overflow
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

    /// Process-wide shared generator, seedable (`Random::Shared().Seed(42)`) and the same sequence on one
    /// thread as ever. Safe to draw from on any thread: every step of THIS generator (and Seed, SetState,
    /// GetState) is taken under a short spin lock, so the threads' draws are the sequential numbers in some
    /// interleaving and the state is never torn (it was a data race before). A generator of your own, a
    /// copy of Shared() and ThreadLocal() have no lock and cost nothing extra. Two things the lock does
    /// not do: a helper that makes several draws (Gaussian, a point in a circle) can be interleaved with
    /// another thread's draws between them (hold LockShared() across it when the pair must stay together),
    /// and which thread gets which number is up to the scheduler, so a replay that needs the same numbers on
    /// the same events draws from one thread or gives each thread a ThreadLocal() stream.
    static Random& Shared() { static Random g = MakeShared(); return g; }

    /// Exclusive use of Shared() for as long as the handle lives, so a worker can take several draws that
    /// stay together (the numbers of one Gaussian, a roll and the re-roll after it):
    ///
    ///     { auto rng = Random::LockShared(); a = rng->Range(0, 6); b = rng->Value(); }
    ///
    /// The lock is recursive on one thread (a nested LockShared, or a draw made while holding it, does not
    /// wait for itself) and spins, so keep the scope to a few draws (no <mutex>: a header here must not need it).
    class SharedLock {
    public:
        SharedLock() { Random::GateLock(); }
        ~SharedLock() { Random::GateUnlock(); }
        SharedLock(const SharedLock&) = delete;
        SharedLock& operator=(const SharedLock&) = delete;
        Random* operator->() const { return &Shared(); }
        Random& operator*() const { return Shared(); }
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
    // The lock behind Shared(): recursive on one thread (owner is a per-thread token), spinning, atomics
    // only. `depth` is only touched by the owner, so the acquire/release pair on `owner` is all it needs.
    struct Gate {
        std::atomic<const void*> owner{nullptr};
        int depth = 0;
    };
    static Gate& GateState() { static Gate g; return g; }
    static const void* ThreadToken() { static thread_local char token; return &token; }
    static void GateLock() {
        Gate& g = GateState();
        const void* me = ThreadToken();
        if (g.owner.load(std::memory_order_relaxed) == me) { ++g.depth; return; }
        const void* expected = nullptr;
        while (!g.owner.compare_exchange_weak(expected, me, std::memory_order_acquire, std::memory_order_relaxed)) expected = nullptr;
        g.depth = 1;
    }
    static void GateUnlock() {
        Gate& g = GateState();
        if (--g.depth == 0) g.owner.store(nullptr, std::memory_order_release);
    }
    /// Takes the gate for one step of the generator when `on` (only Shared() is on).
    struct SharedGuard {
        const bool on;
        explicit SharedGuard(bool o) : on(o) { if (on) Random::GateLock(); }
        ~SharedGuard() { if (on) Random::GateUnlock(); }
        SharedGuard(const SharedGuard&) = delete;
        SharedGuard& operator=(const SharedGuard&) = delete;
    };
    static Random MakeShared() { Random r(0xC0FFEEull); r.m_shared = true; return r; }

    static std::uint64_t SplitMix(std::uint64_t& x) {
        std::uint64_t z = (x += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    std::uint64_t m_s[2] = {1, 2};
    bool m_shared = false;   // true only for Random::Shared(): its steps take the gate
};

} // namespace okay
