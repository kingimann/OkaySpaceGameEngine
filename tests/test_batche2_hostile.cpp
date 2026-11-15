// Hostile input for the batch-E own readers (Q3D, RAW): every truncation of the fixtures, every 32-bit window of the Q3D fixture
// overwritten with 0, 1, 0x7FFFFFFF, 0x80000000 and 0xFFFFFFFF (counts and indices), single-byte flips from a fixed generator
// (OKAY_BATCHE_FUZZ_ITERS raises the sweep), garbage. A reader may refuse or read, never crash, hang or return a model the
// renderer cannot take (fx::Sane). Builds on its own with the reader sources, so it can also be built under
// -fsanitize=address,undefined.
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "batche2_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include <cstdint>
#include <cstdlib>
#include <string>

using namespace okay;

template <class F> static void Probe(F&& read, const std::string& bytes, int& reads, int& refusals) {
    ImportedModel m; std::string err;
    const bool ok = read(bytes, m, &err);
    if (ok) { ++reads; CHECK(fx::Sane(m)); CHECK(!m.parts.empty()); }
    else { ++refusals; CHECK(!err.empty()); CHECK(m.parts.empty()); }
}
static bool RQ(const std::string& b, ImportedModel& m, std::string* e) { return ReadQ3dBuffer(b, m, e); }
static bool RR(const std::string& b, ImportedModel& m, std::string* e) { return ReadRawBuffer(b, m, e); }

int main() {
    RUN_SUITE("batche2_hostile");
    const int iters = std::getenv("OKAY_BATCHE_FUZZ_ITERS") ? std::atoi(std::getenv("OKAY_BATCHE_FUZZ_ITERS")) : 20000;
    int reads = 0, refusals = 0;
    for (bool be : {false, true}) for (bool scene : {false, true}) {
        const std::string good = fx2::BuildQ3d(be, scene, true);
        for (std::size_t n = 0; n < good.size(); ++n) Probe(RQ, good.substr(0, n), reads, refusals);   // every truncation
        for (std::size_t off = 0; off + 4 <= good.size(); ++off)
            for (std::uint32_t v : {0u, 1u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu, 0x00FFFFFFu}) {
                std::string s = good;
                for (int k = 0; k < 4; ++k) s[off + (std::size_t)k] = (char)((v >> (8 * k)) & 255);
                Probe(RQ, s, reads, refusals);
            }
        std::uint32_t x = 12345u;
        for (int i = 0; i < iters / 4; ++i) {
            x = x * 1664525u + 1013904223u;
            std::string s = good;
            s[(x >> 8) % s.size()] ^= (char)(1u << (x & 7));
            if (x & 0x100000) s[(x >> 4) % s.size()] = (char)(x >> 24);
            Probe(RQ, s, reads, refusals);
        }
    }
    {   // a refusal is really reached by a count of 0xFFFFFFFF (negative as a signed int): this must NOT be read
        std::string s = fx2::BuildQ3d();
        for (int k = 0; k < 4; ++k) s[22 + 1 + (std::size_t)k] = (char)0xFF;   // vertex count of mesh 0
        ImportedModel m; std::string err;
        CHECK(!ReadQ3dBuffer(s, m, &err) && !err.empty());
        s = fx2::BuildQ3d();
        for (int k = 0; k < 4; ++k) s[22 + 1 + (std::size_t)k] = (char)(k == 3 ? 0x7F : 0xFF);   // 2^31 - 1 vertices
        CHECK(!ReadQ3dBuffer(s, m, &err) && !err.empty());
    }
    for (const char* eol : {"\n", "\r\n"}) {
        const std::string good = fx2::BuildRaw(eol);
        for (std::size_t n = 0; n < good.size(); ++n) Probe(RR, good.substr(0, n), reads, refusals);
        std::uint32_t x = 777u;
        for (int i = 0; i < iters / 4; ++i) {
            x = x * 1664525u + 1013904223u;
            std::string s = good;
            s[(x >> 8) % s.size()] ^= (char)(1u << (x & 7));
            Probe(RR, s, reads, refusals);
        }
    }
    {   // garbage
        std::uint32_t x = 99u;
        for (int i = 0; i < 300; ++i) {
            std::string s((std::size_t)(i % 200 + 1), '\0');
            for (char& c : s) { x = x * 1664525u + 1013904223u; c = (char)(x >> 24); }
            Probe(RQ, s, reads, refusals);
            Probe(RR, s, reads, refusals);
            if (i % 3 == 0) { std::string q = "quick3Do30" + s; Probe(RQ, q, reads, refusals); }
        }
    }
    {   // huge single values and long lines in RAW
        int r = 0, f = 0;
        Probe(RR, "1e308 0 0 0 0 0 0 0 0\n", r, f); CHECK(f == 1);
        Probe(RR, std::string(2000000, '9') + " 0 0 0 0 0 0 0 0\n", r, f); CHECK(f == 2);
        std::string many = "0 0 0 1 0 0 0 1 0"; for (int i = 0; i < 100; ++i) many += " 0";
        Probe(RR, many, r, f); CHECK(f == 3);
        std::string names; for (int i = 0; i < 5000; ++i) names += "g" + std::to_string(i) + "\n0 0 0 1 0 0 0 1 " + std::to_string(i + 1) + "\n";
        Probe(RR, names, r, f); CHECK(r == 1);   // 5000 groups of one triangle each is fine
    }
    std::fprintf(stderr, "  %d inputs read, %d refused\n", reads, refusals);
    CHECK(reads > 0 && refusals > 1000);
    TEST_MAIN_RESULT();
}
