// 1.1 fifth pass: Random::Shared() is safe to draw from on any thread. It used to be a plain unlocked
// generator ("main thread only"), so two threads that called Random::Shared().Value() raced on its two state
// words: torn states, duplicated numbers, and a data race a thread sanitizer reports on the first draw.
// Now every step of the shared generator (and Seed, SetState, GetState) is taken under a short recursive
// spin lock. The tests below are deterministic in what they check (sums and sets of numbers, not timing):
// the threads' draws are the sequential numbers in some interleaving, so their sum and the generator's end
// state do not depend on who drew what.
//
// The file includes only Random.hpp (a header-only class) so it also builds on its own under
// -fsanitize=thread: `g++ -std=c++17 -fsanitize=thread -Iengine/include tests/test_random_threads.cpp -pthread`.
#include "test_framework.hpp"
#include "okay/Core/Random.hpp"
#include <atomic>
#include <cstdint>
#include <thread>
#include <unordered_set>
#include <vector>

using namespace okay;

namespace {

constexpr std::uint64_t kSeed = 0xFEEDF00Dull;

// The states a sequence of `n` draws from `seed` passes through (the state after draw 0, 1, ... n).
std::unordered_set<std::uint64_t> StateKeys(std::uint64_t seed, int n) {
    std::unordered_set<std::uint64_t> keys;
    Random r(seed);
    auto key = [](const Random::State& s) { return s.s0 * 0x9E3779B97F4A7C15ull ^ (s.s1 + 0x7F4A7C15ull) * 0xBF58476D1CE4E5B9ull; };
    keys.insert(key(r.GetState()));
    for (int i = 0; i < n; ++i) { r.NextUInt64(); keys.insert(key(r.GetState())); }
    return keys;
}

}  // namespace

int main() {
    RUN_SUITE("random_threads");

    // ---- single thread: the sequence and the seed API are exactly what they were ----------------------------
    {
        Random::Shared().Seed(kSeed);
        Random plain(kSeed);
        bool same = true;
        for (int i = 0; i < 1000; ++i) if (Random::Shared().NextUInt64() != plain.NextUInt64()) same = false;
        CHECK(same);
        // every helper draws the same numbers too (they are built on NextUInt64)
        Random::Shared().Seed(kSeed); plain.Seed(kSeed);
        CHECK(Random::Shared().Range(0, 1000) == plain.Range(0, 1000));
        CHECK(Random::Shared().Gaussian() == plain.Gaussian());
        CHECK(Random::Shared().Value() == plain.Value());
        CHECK(Random::Shared().InsideUnitCircle().x == plain.InsideUnitCircle().x);
        // the state round trip
        const Random::State st = Random::Shared().GetState();
        const std::uint64_t a = Random::Shared().NextUInt64();
        Random::Shared().SetState(st);
        CHECK(Random::Shared().NextUInt64() == a);
        // the default seed of the shared stream is unchanged
        Random fresh(0xC0FFEEull);
        Random::Shared().Seed(0xC0FFEEull);
        CHECK(Random::Shared().NextUInt64() == fresh.NextUInt64());
    }

    // ---- a copy of Shared() is an ordinary generator; assigning to Shared() keeps it Shared() -------------------
    {
        Random::Shared().Seed(kSeed);
        Random copy = Random::Shared();                  // same state, no lock of its own
        CHECK(copy.GetState().s0 == Random::Shared().GetState().s0 && copy.GetState().s1 == Random::Shared().GetState().s1);
        const std::uint64_t c = copy.NextUInt64();
        CHECK(Random::Shared().NextUInt64() == c);       // they walk the same sequence, separately
        copy.NextUInt64();
        CHECK(Random::Shared().GetState().s0 != copy.GetState().s0);
        Random other(7);
        Random::Shared() = other;                        // takes the state, stays the shared (locked) generator
        CHECK(Random::Shared().GetState().s0 == other.GetState().s0);
        Random::Shared().Seed(kSeed);
    }

    // ---- N threads draw straight from Shared(): no lost or duplicated step -----------------------------------
    {
        constexpr int kThreads = 6, kDraws = 20000;
        std::uint64_t seqSum = 0;
        Random::State seqEnd;
        {
            Random seq(kSeed);
            for (int i = 0; i < kThreads * kDraws; ++i) seqSum += seq.NextUInt64() >> 20;
            seqEnd = seq.GetState();
        }
        Random::Shared().Seed(kSeed);
        std::vector<std::uint64_t> sums(kThreads, 0);
        std::vector<std::thread> ts;
        std::atomic<bool> go{false};
        for (int t = 0; t < kThreads; ++t)
            ts.emplace_back([&, t] {
                while (!go.load()) {}
                std::uint64_t s = 0;
                for (int i = 0; i < kDraws; ++i) s += Random::Shared().NextUInt64() >> 20;   // no LockShared(): the draw itself is safe
                sums[(std::size_t)t] = s;
            });
        go = true;
        for (auto& t : ts) t.join();
        std::uint64_t total = 0;
        for (std::uint64_t s : sums) total += s;
        CHECK(total == seqSum);                                     // the same multiset of numbers as the sequential run
        CHECK(Random::Shared().GetState().s0 == seqEnd.s0 && Random::Shared().GetState().s1 == seqEnd.s1);   // and the same end state
    }

    // ---- no thread ever sees a torn state, and nobody draws a number twice ---------------------------------------
    {
        constexpr int kThreads = 4, kDraws = 5000;
        const auto valid = StateKeys(kSeed, kThreads * kDraws);
        auto key = [](const Random::State& s) { return s.s0 * 0x9E3779B97F4A7C15ull ^ (s.s1 + 0x7F4A7C15ull) * 0xBF58476D1CE4E5B9ull; };
        Random::Shared().Seed(kSeed);
        std::atomic<bool> stop{false};
        std::atomic<int> torn{0}, snapshots{0};
        std::thread reader([&] {
            while (!stop.load()) {
                const Random::State s = Random::Shared().GetState();
                if (valid.find(key(s)) == valid.end()) ++torn;
                ++snapshots;
            }
        });
        std::vector<std::vector<std::uint64_t>> drawn((std::size_t)kThreads);
        std::vector<std::thread> ts;
        for (int t = 0; t < kThreads; ++t)
            ts.emplace_back([&, t] { for (int i = 0; i < kDraws; ++i) drawn[(std::size_t)t].push_back(Random::Shared().NextUInt64()); });
        for (auto& t : ts) t.join();
        stop = true;
        reader.join();
        CHECK(torn.load() == 0);
        CHECK(snapshots.load() > 0);
        std::unordered_set<std::uint64_t> uniq;
        std::size_t n = 0;
        for (auto& v : drawn) for (std::uint64_t x : v) { uniq.insert(x); ++n; }
        CHECK(n == (std::size_t)kThreads * kDraws && uniq.size() == n);   // 20000 distinct 64-bit numbers: none handed out twice
    }

    // ---- Seed and SetState while others draw: the generator never ends in a state that is not a real one ------------
    {
        Random::Shared().Seed(kSeed);
        std::atomic<bool> stop{false};
        std::thread drawers[2];
        for (auto& d : drawers) d = std::thread([&] { while (!stop.load()) Random::Shared().NextUInt64(); });
        const Random::State fixed{0x1111111122222222ull, 0x3333333344444444ull};
        for (int i = 0; i < 200; ++i) { Random::Shared().SetState(fixed); Random::Shared().Seed((std::uint64_t)i); }
        stop = true;
        for (auto& d : drawers) d.join();
        const Random::State end = Random::Shared().GetState();
        CHECK(end.s0 != 0 || end.s1 != 0);
        Random::Shared().Seed(kSeed);
    }

    // ---- LockShared keeps several draws together; it nests; plain draws from other threads still work -----------------
    {
        Random::Shared().Seed(kSeed);
        {
            auto outer = Random::LockShared();
            auto inner = Random::LockShared();                 // the same thread again: no deadlock
            const std::uint64_t a = outer->NextUInt64();
            const std::uint64_t b = Random::Shared().NextUInt64();   // a plain draw while holding it, on the holding thread
            Random seq(kSeed);
            CHECK(a == seq.NextUInt64() && b == seq.NextUInt64());
        }
        // pairs of draws taken under the lock are consecutive numbers of the sequence, whichever thread took them
        constexpr int kThreads = 4, kPairs = 3000;
        Random::Shared().Seed(kSeed);
        std::vector<std::vector<std::pair<std::uint64_t, std::uint64_t>>> pairs((std::size_t)kThreads);
        std::vector<std::thread> ts;
        for (int t = 0; t < kThreads; ++t)
            ts.emplace_back([&, t] {
                for (int i = 0; i < kPairs; ++i) {
                    auto rng = Random::LockShared();
                    const std::uint64_t a = rng->NextUInt64();
                    const std::uint64_t b = rng->NextUInt64();
                    pairs[(std::size_t)t].push_back({a, b});
                }
            });
        for (auto& t : ts) t.join();
        std::vector<std::uint64_t> seq;
        {
            Random r(kSeed);
            for (int i = 0; i < 2 * kThreads * kPairs; ++i) seq.push_back(r.NextUInt64());
        }
        bool together = true;
        std::unordered_set<std::uint64_t> after;   // fingerprints of every consecutive pair of the sequential run
        for (std::size_t i = 0; i + 1 < seq.size(); ++i) after.insert(seq[i] ^ (seq[i + 1] * 0x9E3779B97F4A7C15ull));
        for (auto& v : pairs) for (auto& p : v) if (after.find(p.first ^ (p.second * 0x9E3779B97F4A7C15ull)) == after.end()) together = false;
        CHECK(together);                                        // no other thread's draw landed between the two
        Random::Shared().Seed(kSeed);
    }

    // ---- ThreadLocal() and private generators are unaffected -------------------------------------------------------------
    {
        std::uint64_t a = 0, b = 0;
        std::thread t1([&] { a = Random::ThreadLocal().NextUInt64(); });
        std::thread t2([&] { b = Random::ThreadLocal().NextUInt64(); });
        t1.join(); t2.join();
        CHECK(a != b);                                          // each thread has a stream of its own
    }

    TEST_MAIN_RESULT();
}
