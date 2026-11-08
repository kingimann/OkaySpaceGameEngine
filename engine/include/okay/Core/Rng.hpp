#pragma once
// Deterministic RNG with independent, named streams and serialisable state.
//
//   Rng world(seed);                    // PCG32: identical sequence on every platform
//   Rng loot  = world.Fork("loot");     // independent stream; does NOT consume `world`,
//   Rng spawn = world.Fork("spawn");    // so adding draws to one system never shifts another
//   std::string saved = loot.GetState();   // put it in a save file / replay
//   loot.SetState(saved);                  // resume the exact sequence
//
// Rng is separate from okay::Random (xorshift128+, kept as is for existing seeds).
// No locking: give each thread its own Rng (Fork makes that cheap).
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace okay {

class Rng {
public:
    Rng() : Rng(0x853C49E6748FEA9Bull, 0) {}
    explicit Rng(std::uint64_t seed, std::uint64_t stream = 0) { Seed(seed, stream); }

    void Seed(std::uint64_t seed, std::uint64_t stream = 0) {
        m_seed = seed; m_stream = stream;
        m_state = 0; m_inc = (stream << 1u) | 1u;
        NextUInt32();
        m_state += seed;
        NextUInt32();
    }
    std::uint64_t GetSeed() const { return m_seed; }
    std::uint64_t GetStream() const { return m_stream; }

    // ---- raw output ----
    std::uint32_t NextUInt32() {
        std::uint64_t old = m_state;
        m_state = old * 6364136223846793005ull + m_inc;
        std::uint32_t xorshifted = (std::uint32_t)(((old >> 18u) ^ old) >> 27u);
        std::uint32_t rot = (std::uint32_t)(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
    }
    std::uint64_t NextUInt64() { std::uint64_t hi = NextUInt32(); return (hi << 32) | NextUInt32(); }

    // ---- values ----
    /// Float in [0, 1).
    float Value() { return (NextUInt32() >> 8) * (1.0f / 16777216.0f); }
    /// Double in [0, 1) with 53 bits.
    double ValueDouble() { return (double)(NextUInt64() >> 11) * (1.0 / 9007199254740992.0); }
    /// Float in [min, max).
    float Range(float min, float max) { return min + Value() * (max - min); }
    /// Integer in [min, max): max is EXCLUSIVE (same contract as Random::Range(int,int)),
    /// without modulo bias. max <= min returns min.
    int Range(int min, int max) {
        if (max <= min) return min;
        std::uint64_t span = (std::uint64_t)((std::int64_t)max - (std::int64_t)min);   // 1 .. 2^32 - 1
        return (int)((std::int64_t)min + (std::int64_t)Below((std::uint32_t)span));
    }
    /// Uniform integer in [0, n) (n == 0 returns 0).
    std::uint32_t Below(std::uint32_t n) {
        if (n <= 1) return 0;
        std::uint32_t threshold = (0u - n) % n;       // reject the biased low region
        for (;;) { std::uint32_t r = NextUInt32(); if (r >= threshold) return r % n; }
    }
    bool Bool() { return (NextUInt32() & 1u) != 0; }
    /// True with probability p (clamped to [0, 1]).
    bool Chance(float p) { return Value() < p; }
    /// Normal distribution (Box-Muller). Consumes exactly two draws.
    float Gaussian(float mean = 0.0f, float stddev = 1.0f);

    // ---- collections ----
    /// In-place Fisher-Yates shuffle.
    template <class It> void Shuffle(It first, It last) {
        auto n = last - first;
        for (auto i = n - 1; i > 0; --i) {
            auto j = (decltype(i))Below((std::uint32_t)(i + 1));
            std::swap(first[i], first[j]);
        }
    }
    template <class C> void Shuffle(C& c) { Shuffle(std::begin(c), std::end(c)); }
    /// Random element of [first, last); `last` when the range is empty.
    template <class It> It Choose(It first, It last) {
        auto n = last - first;
        if (n <= 0) return last;
        return first + (decltype(n))Below((std::uint32_t)n);
    }
    /// Pointer to a random element, nullptr when empty.
    template <class C> auto* Choose(C& c) {
        auto it = Choose(std::begin(c), std::end(c));
        return it == std::end(c) ? nullptr : &*it;
    }
    /// Index chosen with probability proportional to weights[i] (negative / NaN weights
    /// count as 0). Returns -1 when there is no positive weight.
    int WeightedIndex(const std::vector<float>& weights) {
        double total = 0.0;
        for (float w : weights) if (w > 0.0f) total += w;
        if (!(total > 0.0)) return -1;
        double r = ValueDouble() * total, acc = 0.0; int last = -1;
        for (std::size_t i = 0; i < weights.size(); ++i) {
            if (!(weights[i] > 0.0f)) continue;
            acc += weights[i]; last = (int)i;
            if (r < acc) return (int)i;
        }
        return last;                                  // rounding: fall back to the last positive
    }

    // ---- streams ----
    /// A new generator on an independent stream derived from this one's SEED and
    /// STREAM (not its current position), so forking is order-independent and repeatable.
    Rng Fork(std::uint64_t id) const {
        return Rng(Mix(m_seed ^ Mix(id + 0x9E3779B97F4A7C15ull)), Mix(m_stream * 0xD1342543DE82EF95ull + id + 1u));
    }
    Rng Fork(const std::string& name) const { return Fork(Fnv1a(name)); }
    Rng Fork(const char* name) const { return Fork(Fnv1a(name)); }

    // ---- state (save games, replays) ----
    /// Opaque text like "pcg32:<state>:<inc>:<seed>:<stream>" (hex).
    std::string GetState() const {
        return "pcg32:" + Hex(m_state) + ":" + Hex(m_inc) + ":" + Hex(m_seed) + ":" + Hex(m_stream);
    }
    /// Restore from GetState() text. Returns false (and changes nothing) if malformed.
    bool SetState(const std::string& s) {
        static const char prefix[] = "pcg32:";
        if (s.compare(0, sizeof(prefix) - 1, prefix) != 0) return false;
        std::uint64_t v[4]; std::size_t pos = sizeof(prefix) - 1;
        for (int k = 0; k < 4; ++k) {
            std::size_t end = k < 3 ? s.find(':', pos) : s.size();
            if (end == std::string::npos || end - pos != 16) return false;
            if (!ParseHex(s, pos, end, v[k])) return false;
            pos = end + 1;
        }
        if ((v[1] & 1u) == 0) return false;           // the increment must be odd
        m_state = v[0]; m_inc = v[1]; m_seed = v[2]; m_stream = v[3];
        return true;
    }
    bool operator==(const Rng& o) const { return m_state == o.m_state && m_inc == o.m_inc; }
    bool operator!=(const Rng& o) const { return !(*this == o); }

private:
    static std::uint64_t Mix(std::uint64_t z) {
        z += 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    static std::uint64_t Fnv1a(const std::string& s) { return Fnv1a(s.c_str()); }
    static std::uint64_t Fnv1a(const char* s) {
        std::uint64_t h = 0xCBF29CE484222325ull;
        for (; *s; ++s) { h ^= (unsigned char)*s; h *= 0x100000001B3ull; }
        return h;
    }
    static std::string Hex(std::uint64_t v) {
        static const char* d = "0123456789abcdef";
        std::string s(16, '0');
        for (int i = 15; i >= 0; --i) { s[(std::size_t)i] = d[v & 15u]; v >>= 4; }
        return s;
    }
    static bool ParseHex(const std::string& s, std::size_t b, std::size_t e, std::uint64_t& out) {
        std::uint64_t v = 0;
        for (std::size_t i = b; i < e; ++i) {
            char c = s[i]; int d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else return false;
            v = (v << 4) | (std::uint64_t)d;
        }
        out = v; return true;
    }

    std::uint64_t m_state = 0, m_inc = 1, m_seed = 0, m_stream = 0;
};

inline float Rng::Gaussian(float mean, float stddev) {
    // u1 in (0, 1] so log() is finite.
    double u1 = 1.0 - ValueDouble(), u2 = ValueDouble();
    double r = std::sqrt(-2.0 * std::log(u1));
    return mean + stddev * (float)(r * std::cos(6.283185307179586 * u2));
}

} // namespace okay
