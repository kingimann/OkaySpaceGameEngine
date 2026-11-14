// Hostile-input tests and a deterministic mutation fuzz for the own MS3D reader. Needs only okay/Render/ModelData.hpp and
// engine/src/Render/Ms3dReader.cpp, so it builds on its own:
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all
//       -Iengine/include -Itests -DOKAY_SOURCE_DIR='"."' tests/test_ms3d_hostile.cpp engine/src/Render/Ms3dReader.cpp -o ms3d_hostile
// OKAY_READER_FUZZ_ITERS=200000 for a long run.
#include "test_framework.hpp"
#include "reader_hostile_common.hpp"
#include <chrono>
#include <cstring>

using namespace okay;
using namespace hostile;

static const std::string kDir = std::string(OKAY_SOURCE_DIR) + "/tests/data/models/";

static void TryMs3d(const std::string& bytes) {
    ImportedModel m; std::string err;
    const bool ok = ReadMs3dBuffer(bytes, "", m, &err);
    if (ok) { CHECK(Sane(m) && !m.parts.empty()); }
    else { CHECK(!err.empty() && m.parts.empty() && m.nodes.empty() && m.clips.empty()); }
}
static double Secs(std::chrono::steady_clock::time_point t0) { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); }

static void P16(std::string& s, unsigned v) { s.push_back((char)(v & 255)); s.push_back((char)(v >> 8)); }
static void P32(std::string& s, std::uint32_t v) { for (int i = 0; i < 4; ++i) s.push_back((char)((v >> (8 * i)) & 255)); }
static void PF(std::string& s, float f) { std::uint32_t u; std::memcpy(&u, &f, 4); P32(s, u); }
static void PS(std::string& s, const std::string& t, std::size_t n) { std::string x = t; x.resize(n, '\0'); s += x; }
static std::string Head(int version = 4) { std::string s = "MS3D000000"; P32(s, (std::uint32_t)version); return s; }
// a minimal model builder: `nv` vertices, `nt` triangles, `groups` (each a list of triangle indices), `nj` joints, no materials
struct B {
    int nv = 3, nt = 1;
    std::vector<std::vector<int>> groups{{0}};
    int nj = 0;
    float vx = 0;
    int tri0 = 0, tri1 = 1, tri2 = 2;
    int keys = 0; float keyTime = 0;
    std::string Build() const {
        std::string s = Head();
        P16(s, (unsigned)nv);
        for (int i = 0; i < nv; ++i) { s.push_back(0); PF(s, i == 0 ? vx : (float)i); PF(s, 0); PF(s, 0); s.push_back(nj ? 0 : (char)-1); s.push_back(0); }
        P16(s, (unsigned)nt);
        for (int i = 0; i < nt; ++i) {
            P16(s, 0); P16(s, (unsigned)tri0); P16(s, (unsigned)tri1); P16(s, (unsigned)tri2);
            for (int k = 0; k < 3; ++k) { PF(s, 0); PF(s, 0); PF(s, 1); }
            for (int k = 0; k < 6; ++k) PF(s, 0);
            s.push_back(1); s.push_back(0);
        }
        P16(s, (unsigned)groups.size());
        for (const auto& g : groups) { s.push_back(0); PS(s, "g", 32); P16(s, (unsigned)g.size()); for (int t : g) P16(s, (unsigned)t); s.push_back((char)-1); }
        P16(s, 0);                                   // materials
        PF(s, 24); PF(s, 0); P32(s, 1);
        P16(s, (unsigned)nj);
        for (int j = 0; j < nj; ++j) {
            s.push_back(0); PS(s, "j" + std::to_string(j), 32); PS(s, j ? "j" + std::to_string(j - 1) : "", 32);
            PF(s, 0); PF(s, 0); PF(s, 0); PF(s, 0); PF(s, 0); PF(s, 0);
            P16(s, (unsigned)keys); P16(s, (unsigned)keys);
            for (int k = 0; k < keys * 2; ++k) { PF(s, keyTime + (float)k); PF(s, 1); PF(s, 2); PF(s, 3); }
        }
        return s;
    }
};

int main() {
    RUN_SUITE("ms3d_hostile");
    const std::string fixtures[] = {Slurp(kDir + "rig.ms3d"), Slurp(kDir + "rig_v3.ms3d")};
    for (const std::string& f : fixtures) CHECK(!f.empty());
    for (const std::string& f : fixtures) for (std::size_t cut = 0; cut < f.size(); ++cut) TryMs3d(f.substr(0, cut));

    { B b; TryMs3d(b.Build()); }
    { B b; b.nj = 1; TryMs3d(b.Build()); }
    { B b; b.tri2 = 7; TryMs3d(b.Build()); }                           // an index outside the vertex list
    { B b; b.tri2 = 65535; TryMs3d(b.Build()); }
    { B b; b.tri1 = 0; TryMs3d(b.Build()); }                           // degenerate
    { B b; b.groups = {{0, 0, 0, 5, 65535}}; TryMs3d(b.Build()); }    // repeated and missing triangle indices
    { B b; b.vx = std::nanf(""); TryMs3d(b.Build()); }
    { B b; b.vx = INFINITY; TryMs3d(b.Build()); }
    { B b; b.vx = 1e15f; TryMs3d(b.Build()); }
    { B b; b.vx = 1e16f; TryMs3d(b.Build()); }
    { B b; b.vx = 3e38f; TryMs3d(b.Build()); }
    { B b; b.nj = 1; b.keys = 3; b.keyTime = -5; TryMs3d(b.Build()); }
    { B b; b.nj = 1; b.keys = 3; b.keyTime = std::nanf(""); TryMs3d(b.Build()); }
    { B b; b.nj = 1; b.keys = 3; b.keyTime = 1e30f; TryMs3d(b.Build()); }
    { B b; b.nj = 1; b.keys = 3000; b.keyTime = 0; TryMs3d(b.Build()); }
    // a long joint chain: no recursion
    {
        B b; b.nj = 5000;
        const auto t0 = std::chrono::steady_clock::now();
        ImportedModel m; std::string err;
        CHECK(ReadMs3dBuffer(b.Build(), "", m, &err) && Sane(m) && m.nodes.size() == 5001);
        CHECK(Secs(t0) < 10.0);
    }
    // 60000 groups of one triangle each (the file is small, the work is bounded by the output cap)
    {
        B b; b.groups.clear(); for (int i = 0; i < 60000; ++i) b.groups.push_back({0});
        const auto t0 = std::chrono::steady_clock::now();
        TryMs3d(b.Build());
        CHECK(Secs(t0) < 20.0);
    }
    // 65535 vertices and triangles claimed, with a tiny file: refused before any allocation
    {
        std::string s = Head(); P16(s, 65535);
        ImportedModel m; std::string err;
        CHECK(!ReadMs3dBuffer(s, "", m, &err) && err.find("ends inside the vertex list") != std::string::npos);
        std::string t = Head(); P16(t, 0); P16(t, 65535); t.append(100, '\0');
        CHECK(!ReadMs3dBuffer(t, "", m, &err));
    }
    // extras: comment counts that claim billions, truncated weight blocks, unknown sub versions
    {
        const std::string& f = fixtures[0];
        for (std::size_t at = f.size() - 120; at + 4 <= f.size(); at += 3) {
            for (std::uint32_t v : {0xFFFFFFFFu, 0x7FFFFFFFu, 0x80000000u, 1u, 2u, 3u, 4u, 0u, 100u}) {
                std::string s = f;
                for (int i = 0; i < 4; ++i) s[at + (std::size_t)i] = (char)((v >> (8 * i)) & 255);
                TryMs3d(s);
            }
        }
    }
    // header counts stomped to extremes in the fixture
    for (std::size_t at = 14; at < 14 + 40; ++at) for (unsigned char v : {0x00, 0x01, 0x7F, 0x80, 0xFF}) { std::string s = fixtures[0]; s[at] = (char)v; TryMs3d(s); }

    // ---------------- mutation fuzz ----------------
    {
        const long iters = Iters(8000);
        for (long it = 0; it < iters; ++it) {
            std::string s = fixtures[Rnd() % 2];
            const int ops = 1 + (int)(Rnd() % 4);
            for (int o = 0; o < ops; ++o) MutateBinary(s);
            TryMs3d(s);
        }
        std::printf("  fuzz: %ld iterations\n", iters);
    }
    TEST_MAIN_RESULT();
}
