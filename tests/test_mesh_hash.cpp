#include "test_framework.hpp"
#include "okay/Render/MeshHash.hpp"
#include <algorithm>
#include <chrono>
#include <functional>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <unordered_set>
#include <vector>

using namespace okay;

// The renderers' mesh-change hash (okay::meshhash::Bytes): any change to the bytes changes the hash, the length and the
// order of chained calls count, and it is much faster than the byte-at-a-time loop it replaced.
namespace {

struct Lcg {
    std::uint32_t s;
    explicit Lcg(std::uint32_t seed) : s(seed) {}
    std::uint32_t Next() { s = s * 1664525u + 1013904223u; return s >> 8; }
};

std::vector<unsigned char> RandomBytes(std::size_t n, std::uint32_t seed) {
    Lcg rng(seed);
    std::vector<unsigned char> v(n);
    for (auto& b : v) b = (unsigned char)rng.Next();
    return v;
}

// The loop the renderers used before (kept here only as the thing to beat).
std::uint64_t OldFnv(const void* data, std::size_t nbytes, std::uint64_t seed = 1469598103934665603ull) {
    const unsigned char* p = (const unsigned char*)data;
    std::uint64_t h = seed;
    for (std::size_t i = 0; i < nbytes; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

double Seconds(const std::function<void()>& f, int reps) {
    double best = 1e30;
    for (int r = 0; r < 5; ++r) {
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < reps; ++i) f();
        best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / reps);
    }
    return best;
}

} // namespace

int main() {
    RUN_SUITE("mesh_hash");

    // --- every single bit counts, at every length around the block edges --------------------------------------------
    {
        const std::size_t sizes[] = {1, 2, 3, 4, 5, 7, 8, 9, 12, 15, 16, 17, 31, 32, 33, 40, 63, 64, 65, 96, 100, 127, 128, 129, 255, 300};
        int misses = 0, total = 0;
        for (std::size_t n : sizes) {
            std::vector<unsigned char> b = RandomBytes(n, 17u + (std::uint32_t)n);
            const std::uint64_t base = meshhash::Bytes(b.data(), b.size());
            for (std::size_t bit = 0; bit < n * 8; ++bit) {
                b[bit / 8] ^= (unsigned char)(1u << (bit % 8));
                if (meshhash::Bytes(b.data(), b.size()) == base) ++misses;
                b[bit / 8] ^= (unsigned char)(1u << (bit % 8));
                ++total;
            }
        }
        CHECK(total > 10000);
        CHECK(misses == 0);
    }

    // --- length, order and seed count ------------------------------------------------------------------------------
    {
        std::vector<unsigned char> a = RandomBytes(100, 1u), b = RandomBytes(60, 2u);
        std::vector<unsigned char> az = a; az.push_back(0);
        CHECK(meshhash::Bytes(a.data(), a.size()) != meshhash::Bytes(az.data(), az.size()));      // a trailing zero is a change
        CHECK(meshhash::Bytes(a.data(), 0) != meshhash::Bytes(a.data(), 1));
        const std::uint64_t ab = meshhash::Bytes(b.data(), b.size(), meshhash::Bytes(a.data(), a.size()));
        const std::uint64_t ba = meshhash::Bytes(a.data(), a.size(), meshhash::Bytes(b.data(), b.size()));
        CHECK(ab != ba);                                                                         // chaining is ordered
        CHECK(meshhash::Bytes(a.data(), a.size(), 1u) != meshhash::Bytes(a.data(), a.size(), 2u));
        CHECK(meshhash::Bytes(a.data(), a.size()) == meshhash::Bytes(a.data(), a.size()));       // and stable
        std::vector<unsigned char> zeros(256, 0);
        CHECK(meshhash::Bytes(zeros.data(), 255) != meshhash::Bytes(zeros.data(), 256));
        CHECK(meshhash::Bytes(zeros.data(), 63) != meshhash::Bytes(zeros.data(), 64));
    }

    // --- a mesh-sized array: nudging one float by one step is noticed anywhere in it -------------------------------------
    {
        std::vector<float> v(300000);
        Lcg rng(5u);
        for (float& f : v) f = (float)(rng.Next() % 100000) / 977.0f;
        const std::uint64_t base = meshhash::Bytes(v.data(), v.size() * sizeof(float));
        int misses = 0;
        for (std::size_t at : {std::size_t(0), std::size_t(1), std::size_t(7), std::size_t(8), std::size_t(150001), std::size_t(299999)}) {
            std::uint32_t u; std::memcpy(&u, &v[at], 4);
            const float old = v[at];
            u ^= 1u; std::memcpy(&v[at], &u, 4);
            if (meshhash::Bytes(v.data(), v.size() * sizeof(float)) == base) ++misses;
            v[at] = old;
        }
        CHECK(misses == 0);
    }

    // --- no collisions among many distinct small keys (the renderers hash 12-byte vertices one by one) ---------------------
    {
        std::unordered_set<std::uint64_t> seen;
        bool clash = false;
        for (std::uint32_t x = 0; x < 200; ++x)
            for (std::uint32_t y = 0; y < 200; ++y) {
                float k[3] = {(float)x * 0.25f, (float)y * 0.25f, 1.5f};
                if (!seen.insert(meshhash::Bytes(k, sizeof(k))).second) clash = true;
            }
        CHECK(!clash);
        CHECK(seen.size() == 40000);
    }

    // --- much faster than the loop it replaced ----------------------------------------------------------------------------
    {
        std::vector<unsigned char> big = RandomBytes(4u << 20, 99u);
        volatile std::uint64_t sink = 0;
        const double oldT = Seconds([&] { sink = OldFnv(big.data(), big.size()); }, 2);
        const double newT = Seconds([&] { sink = meshhash::Bytes(big.data(), big.size()); }, 2);
        (void)sink;
        std::cout << "  4 MB: byte-at-a-time " << oldT * 1000.0 << " ms, now " << newT * 1000.0 << " ms (x" << oldT / newT << ")\n";
        CHECK(newT * 2.0 < oldT);   // the real factor is about 20 in a release build; the bar is low so a slow debug build passes
    }

    TEST_MAIN_RESULT();
}
