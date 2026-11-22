// Budgets of the batch-E own readers (Q3D, RAW). A counting operator new measures the live peak. Cases:
//  * the worst file each reader still ACCEPTS (Q3D: 4,000,000 triangles, RAW: 500,000 triangles) reads within the memory cap and time;
//  * one triangle / vertex more is REFUSED with a reason that names the budget, within the same cap;
//  * a Q3D file whose counts claim far more than its bytes hold is refused without allocating for the claim.
// All fixtures are SYNTHETIC, generated here. Builds on its own with the reader sources.
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "batche2_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <new>
#include <string>

using namespace okay;

static std::atomic<std::size_t> g_live{0}, g_peak{0};
static void* CountedAlloc(std::size_t n) {
    void* raw = std::malloc(n + 16);
    if (!raw) throw std::bad_alloc();
    *static_cast<std::size_t*>(raw) = n;
    const std::size_t now = g_live.fetch_add(n) + n;
    std::size_t p = g_peak.load();
    while (now > p && !g_peak.compare_exchange_weak(p, now)) {}
    return static_cast<char*>(raw) + 16;
}
static void CountedFree(void* q) noexcept { if (!q) return; void* raw = static_cast<char*>(q) - 16; g_live.fetch_sub(*static_cast<std::size_t*>(raw)); std::free(raw); }
void* operator new(std::size_t n) { return CountedAlloc(n); }
void* operator new[](std::size_t n) { return CountedAlloc(n); }
void operator delete(void* p) noexcept { CountedFree(p); }
void operator delete[](void* p) noexcept { CountedFree(p); }
void operator delete(void* p, std::size_t) noexcept { CountedFree(p); }
void operator delete[](void* p, std::size_t) noexcept { CountedFree(p); }

static constexpr std::size_t kCapBytes = 640u * 1024u * 1024u;   // what a reader may hold at once on top of its input
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_UNDEFINED__)
static constexpr double kMaxSeconds = 60.0;
#else
static constexpr double kMaxSeconds = 15.0;
#endif
static double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

template <class F> static bool Measure(const char* what, F&& f) {
    const std::size_t base = g_live.load();
    g_peak.store(base);
    const double t = Now();
    const bool ok = f();
    const double s = Now() - t;
    const std::size_t extra = g_peak.load() - base;
    std::fprintf(stderr, "  %s: %s in %.2f s, peak %zu MB\n", what, ok ? "read" : "refused", s, extra >> 20);
    CHECK(s < kMaxSeconds);
    CHECK(extra < kCapBytes);
    return ok;
}

// A Q3D file with `meshes` meshes of nv vertices each and nf faces each; face i of a mesh is a quad on vertices 0..3 (shared) when
// `quads`, else the triangle (3i, 3i+1, 3i+2) (unique vertices).
static std::string Q3dBig(int meshes, std::uint32_t nv, std::uint32_t nf, bool quads) {
    fx2::QW w(false);
    w.s.reserve(64 + (std::size_t)meshes * ((std::size_t)nv * 12 + (std::size_t)nf * 28));
    w.s += "quick3Do30"; w.I32((std::uint32_t)meshes); w.I32(0); w.I32(0);
    w.U8('m');
    for (int m = 0; m < meshes; ++m) {
        w.I32(nv);
        for (std::uint32_t i = 0; i < nv; ++i) { if (nv == 4) { w.F((float)(i & 1)); w.F((float)(i >> 1)); w.F(0.0f); } else { w.F((float)(i % 1000)); w.F((float)(i / 1000) + ((i % 3) == 1 ? 0.5f : 0.0f)); w.F(0.0f); } }
        w.I32(nf);
        for (std::uint32_t i = 0; i < nf; ++i) w.U16(quads ? 4 : 3);
        for (std::uint32_t i = 0; i < nf; ++i) { if (quads) { for (int k = 0; k < 4; ++k) w.I32((std::uint32_t)k); } else { for (int k = 0; k < 3; ++k) w.I32(3 * i + (std::uint32_t)k); } }
        for (std::uint32_t i = 0; i < nf; ++i) w.I32(0xFFFFFFFFu);   // no material
        w.I32(0); w.I32(0);   // normals, texture coordinates
        for (int i = 0; i < 9; ++i) w.F(0.0f);
    }
    return w.s;
}

// nf faces, each the same concave star polygon of `n` corners (alternating radius 1 and 0.5 around a circle).
static std::string Q3dStars(std::uint32_t n, std::uint32_t nf) {
    fx2::QW w(false);
    w.s.reserve(64 + (std::size_t)n * 12 + (std::size_t)nf * ((std::size_t)n * 4 + 6));
    w.s += "quick3Do30"; w.I32(1); w.I32(0); w.I32(0); w.U8('m');
    w.I32(n);
    for (std::uint32_t i = 0; i < n; ++i) { const double a = 6.283185307179586 * i / n, r = (i & 1) ? 0.5 : 1.0; w.F((float)(r * std::cos(a))); w.F((float)(r * std::sin(a))); w.F(0.0f); }
    w.I32(nf);
    for (std::uint32_t i = 0; i < nf; ++i) w.U16(n);
    for (std::uint32_t i = 0; i < nf; ++i) for (std::uint32_t k = 0; k < n; ++k) w.I32(k);
    for (std::uint32_t i = 0; i < nf; ++i) w.I32(0xFFFFFFFFu);
    w.I32(0); w.I32(0);   // normals, texture coordinates
    for (int i = 0; i < 9; ++i) w.F(0.0f);
    return w.s;
}

static std::string RawBig(std::size_t tris) {
    std::string s;
    s.reserve(tris * 40);
    char buf[128];
    for (std::size_t i = 0; i < tris; ++i) {
        const int n = std::snprintf(buf, sizeof(buf), "%zu 0 0 %zu 1 0 %zu 0 1\n", 3 * i, 3 * i + 1, 3 * i + 2);
        s.append(buf, (std::size_t)n);
    }
    return s;
}

int main() {
    RUN_SUITE("batche2_budget");
    {   // Q3D: 2,000,000 quads on 4 shared vertices = exactly the 4,000,000 triangles of the budget
        const std::string f = Q3dBig(1, 4, 2000000, true);
        ImportedModel m; std::string err;
        const bool ok = Measure("q3d worst accepted (4,000,000 triangles)", [&] { return ReadQ3dBuffer(f, m, &err); });
        CHECK(ok && fx::TriCount(m) <= 4000000);
        if (!ok) std::fprintf(stderr, "    %s\n", err.c_str());
    }
    {   // one more quad: refused, naming the triangle budget
        const std::string f = Q3dBig(1, 4, 2000001, true);
        ImportedModel m; std::string err;
        CHECK(!Measure("q3d 4,000,002 triangles", [&] { return ReadQ3dBuffer(f, m, &err); }));
        CHECK(err.find("budget") != std::string::npos && err.find("triangles") != std::string::npos);
    }
    {   // output vertices: two meshes of 2,100,000 unique triangle vertices (700,000 triangles each) = 4,200,000 > 4,000,000
        const std::string f = Q3dBig(2, 2100000, 700000, false);
        ImportedModel m; std::string err;
        CHECK(!Measure("q3d 4,200,000 output vertices", [&] { return ReadQ3dBuffer(f, m, &err); }));
        CHECK(err.find("budget") != std::string::npos && err.find("vertices") != std::string::npos);
    }
    {   // counts that claim far more than the bytes hold: refused without allocating for the claim
        std::string f = fx2::BuildQ3d();
        for (int k = 0; k < 4; ++k) f[22 + 1 + (std::size_t)k] = (char)(k == 3 ? 0x00 : 0xFF);   // 0x00FFFFFF vertices: over the per-mesh limit
        ImportedModel m; std::string err;
        CHECK(!Measure("q3d vertex count 16M in a 300 byte file", [&] { return ReadQ3dBuffer(f, m, &err); }));
        f = fx2::BuildQ3d();
        for (int k = 0; k < 4; ++k) f[22 + 1 + (std::size_t)k] = (char)(k == 0 ? 0x80 : (k == 1 ? 0x84 : (k == 2 ? 0x1E : 0)));   // 2,000,000 vertices in a 300 byte file
        CHECK(!Measure("q3d vertex count 2M in a 300 byte file", [&] { return ReadQ3dBuffer(f, m, &err); }));
        CHECK(err.find("ends inside") != std::string::npos);
    }
    {   // face shapes that claim 16,000,000 corners (the corner budget) in an 8 KB file: no allocation for the claim
        fx2::QW w(false);
        w.s += "quick3Do30"; w.I32(1); w.I32(0); w.I32(0); w.U8('m');
        w.I32(1); w.F(0); w.F(0); w.F(0);
        w.I32(4000);
        for (int i = 0; i < 4000; ++i) w.U16(4000);
        ImportedModel m; std::string err;
        const std::size_t base = g_live.load(); g_peak.store(base);
        const bool ok = ReadQ3dBuffer(w.s, m, &err);
        const std::size_t extra = g_peak.load() - base;
        std::fprintf(stderr, "  q3d 16M claimed corners in 8 KB: %s, peak %zu KB\n", ok ? "read" : "refused", extra >> 10);
        CHECK(!ok && err.find("ends inside the faces") != std::string::npos);
        CHECK(extra < (1u << 20));
    }
    {   // ear clipping is n^3 per concave polygon: 15,000 concave 256-corner faces (3.84M corners, 3.8M triangles) and
        // 100,000 concave 32-corner faces (3.2M corners) must finish in the time cap, because they are fanned or share a work budget
        for (std::uint32_t n : {256u, 32u}) {
            const std::uint32_t nf = n == 256 ? 15000 : 100000;
            const std::string f = Q3dStars(n, nf);
            ImportedModel m; std::string err;
            char what[96]; std::snprintf(what, sizeof(what), "q3d %u concave %u-corner faces", nf, n);
            const bool ok = Measure(what, [&] { return ReadQ3dBuffer(f, m, &err); });
            CHECK(ok);
            if (!ok) std::fprintf(stderr, "    %s\n", err.c_str());
            bool noted = false; for (const std::string& t : m.notes) if (t.find("fanned") != std::string::npos) noted = true;
            CHECK(noted);
        }
    }
    {   // RAW: one huge line must not become millions of token strings: 64 MB of "1 1 1 ..." and of "0 a a a ..." in one line
        for (int variant = 0; variant < 2; ++variant) {
            std::string f = variant == 0 ? std::string() : std::string("0 ");
            f.reserve(64u << 20);
            while (f.size() < (64u << 20)) f += variant == 0 ? "1 " : "a ";
            f += "\n";
            ImportedModel m; std::string err;
            const std::size_t base = g_live.load(); g_peak.store(base);
            const bool ok = ReadRawBuffer(f, m, &err);
            const std::size_t extra = g_peak.load() - base;
            std::fprintf(stderr, "  raw 64 MB line (%d): %s, peak %zu KB\n", variant, ok ? "read" : "refused", extra >> 10);
            CHECK(!ok);
            CHECK(err.find("more than") != std::string::npos);
            CHECK(extra < (1u << 20));
        }
        // and a 64 MB first word on a line of its own is only a (capped) group name
        std::string f(64u << 20, 'a'); f += "\n0 0 0 1 0 0 0 1 0\n";
        ImportedModel m; std::string err;
        const std::size_t base = g_live.load(); g_peak.store(base);
        const bool ok = ReadRawBuffer(f, m, &err);
        CHECK(ok && m.parts.size() == 1 && m.parts[0].mesh.name.size() == 256);
        CHECK(g_peak.load() - base < (1u << 20));
    }
    {   // RAW: 500,000 triangles with all-different corners = the budget
        const std::string f = RawBig(500000);
        ImportedModel m; std::string err;
        const bool ok = Measure("raw worst accepted (500,000 triangles)", [&] { return ReadRawBuffer(f, m, &err); });
        CHECK(ok && fx::TriCount(m) == 500000);
    }
    {
        const std::string f = RawBig(500001);
        ImportedModel m; std::string err;
        CHECK(!Measure("raw 500,001 triangles", [&] { return ReadRawBuffer(f, m, &err); }));
        CHECK(err.find("budget") != std::string::npos);
    }
    TEST_MAIN_RESULT();
}
