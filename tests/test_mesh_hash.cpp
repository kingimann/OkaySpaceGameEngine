#include "test_framework.hpp"
#include "okay/Render/MeshHash.hpp"
#include "okay/Render/Mesh.hpp"
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


// The two per-mesh helpers exactly as GLRenderer.cpp and D3D11Renderer.cpp each carried them before they moved into
// MeshHash.hpp (meshhash::MeshSample / meshhash::MeshFull). Kept here so the shared versions are proved to give the
// very same values: the renderers' caches must not see a change that is not there, or miss one that is.
std::uint64_t RefSample(const Mesh& m) {
    std::size_t sizes[7] = { m.vertices.size(), m.triangles.size(), m.normals.size(),
                             m.uvs.size(), m.triColors.size(), m.vertexColors.size(), m.triSlot.size() };
    std::uint64_t h = meshhash::Bytes(sizes, sizeof(sizes));
    { const std::size_t cn = m.vertexColors.size(), cstep = cn / 2048 + 1;
      for (std::size_t i = 0; i < cn; i += cstep) h = meshhash::Bytes(&m.vertexColors[i], sizeof(Color), h); }
    const std::size_t vn = m.vertices.size(), step = vn / 2048 + 1;
    if (step == 1) { if (vn) h = meshhash::Bytes(m.vertices.data(), vn * sizeof(Vec3), h); }
    else for (std::size_t i = 0; i < vn; i += step) h = meshhash::Bytes(&m.vertices[i], sizeof(Vec3), h);
    const std::size_t tn = m.triangles.size(), tstep = tn / 2048 + 1;
    if (tstep == 1) { if (tn) h = meshhash::Bytes(m.triangles.data(), tn * sizeof(int), h); }
    else for (std::size_t i = 0; i < tn; i += tstep) h = meshhash::Bytes(&m.triangles[i], sizeof(int), h);
    return h;
}
std::uint64_t RefFull(const Mesh& m) {
    std::uint64_t h = 1469598103934665603ull;
    if (!m.vertices.empty())  h = meshhash::Bytes(m.vertices.data(),  m.vertices.size()  * sizeof(Vec3),  h);
    if (!m.normals.empty())   h = meshhash::Bytes(m.normals.data(),   m.normals.size()   * sizeof(Vec3),  h);
    if (!m.uvs.empty())       h = meshhash::Bytes(m.uvs.data(),       m.uvs.size()       * sizeof(Vec2),  h);
    if (!m.triangles.empty()) h = meshhash::Bytes(m.triangles.data(), m.triangles.size() * sizeof(int),   h);
    if (!m.triColors.empty()) h = meshhash::Bytes(m.triColors.data(), m.triColors.size() * sizeof(Color), h);
    if (!m.vertexColors.empty()) h = meshhash::Bytes(m.vertexColors.data(), m.vertexColors.size() * sizeof(Color), h);
    if (!m.triSlot.empty()) h = meshhash::Bytes(m.triSlot.data(), m.triSlot.size() * sizeof(int), h);
    return h;
}

Mesh MakeMesh(std::size_t nv, std::uint32_t seed, bool extras) {
    Lcg rng(seed);
    Mesh m;
    for (std::size_t i = 0; i < nv; ++i) m.vertices.push_back(Vec3((float)(rng.Next() % 1000) * 0.01f, (float)(rng.Next() % 1000) * 0.01f, (float)i));
    for (std::size_t i = 0; i + 2 < nv; i += 3) { m.triangles.push_back((int)i); m.triangles.push_back((int)i + 1); m.triangles.push_back((int)i + 2); }
    if (extras) {
        for (std::size_t i = 0; i < nv; ++i) { m.normals.push_back(Vec3(0, 1, 0)); m.uvs.push_back(Vec2((float)i, 0.5f)); }
        for (std::size_t i = 0; i < nv; ++i) m.vertexColors.push_back(Color((float)(i % 7) / 7.0f, 0.2f, 0.3f, 1.0f));
        for (std::size_t i = 0; i < m.triangles.size() / 3; ++i) { m.triColors.push_back(Color(1, 0, 0, 1)); m.triSlot.push_back((int)(i % 3)); }
    }
    return m;
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

    // --- the per-mesh helpers both renderers share (meshhash::MeshSample / MeshFull) give the values they always gave --
    {
        int same = 0, total = 0;
        const std::size_t counts[] = {0, 3, 30, 2047, 2048, 2049, 9000, 70000};
        for (std::size_t n : counts)
            for (int extras = 0; extras < 2; ++extras) {
                const Mesh m = MakeMesh(n, 7u + (std::uint32_t)n, extras != 0);
                total += 2;
                same += meshhash::MeshSample(m) == RefSample(m);
                same += meshhash::MeshFull(m) == RefFull(m);
            }
        std::cout << "  per-mesh helpers equal to the renderers' old copies: " << same << " / " << total << "\n";
        CHECK(same == total);
        // and they still notice what the renderers need them to notice
        Mesh m = MakeMesh(9000, 3u, true);
        const std::uint64_t s0 = meshhash::MeshSample(m), f0 = meshhash::MeshFull(m);
        m.vertexColors[4].r += 0.5f;                       // one stroke of vertex paint: the cheap sample sees it
        CHECK(meshhash::MeshSample(m) != s0);
        m.vertexColors[4].r -= 0.5f;
        m.vertices[1].x += 1.0f;                           // a lone vertex the sample skips: only the full hash sees it
        CHECK(meshhash::MeshFull(m) != f0);
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
