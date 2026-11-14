// Hostile-input tests and a deterministic mutation fuzz for the own Terragen (.ter) reader. Needs only
// okay/Render/ModelData.hpp and engine/src/Render/TerragenReader.cpp, so it builds on its own:
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all
//       -Iengine/include -Iengine/src/Render -Itests -DOKAY_SOURCE_DIR='"."' tests/test_terragen_hostile.cpp
//       engine/src/Render/TerragenReader.cpp -o terragen_hostile
// OKAY_READER_FUZZ_ITERS=200000 for a long run. With OKAY_MODEL_CORPUS set (docs/model_corpus.md), the real files under
// TER/ are fuzzed too.
#include "test_framework.hpp"
#include "reader_hostile_common.hpp"
#include "terragen_test_build.hpp"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>

using namespace okay;
using namespace hostile;
using namespace terbuild;

static long g_ok = 0, g_refused = 0;

static void TryTer(const std::string& bytes) {
    ImportedModel m; std::string err;
    const bool ok = ReadTerragenBuffer(bytes, m, &err);
    if (ok) {
        ++g_ok;
        CHECK(Sane(m) && m.parts.size() == 1 && m.nodes.size() == 1);
        if (m.parts.size() == 1) {
            const Mesh& me = m.parts[0].mesh;
            CHECK(me.vertices.size() >= 4 && me.vertices.size() <= 2049u * 2049u);
            CHECK(me.normals.size() == me.vertices.size() && me.uvs.size() == me.vertices.size());
            CHECK(me.triangles.size() % 3 == 0 && !me.triangles.empty());
            for (const Vec3& n : me.normals) if (!(n.y > 0)) { CHECK(n.y > 0); break; }   // a height field always faces up
        }
    } else {
        ++g_refused;
        CHECK(!err.empty() && m.parts.empty() && m.nodes.empty());
    }
}

int main() {
    RUN_SUITE("terragen_hostile");
    std::vector<std::string> seeds;
    {
        std::vector<std::int16_t> e;
        for (int k = 0; k < 9 * 7; ++k) e.push_back((std::int16_t)((k * 7919) % 2000 - 1000));
        seeds.push_back(Grid(9, 7, 1200, -30, e));
        seeds.push_back(Header() + Dim("SIZE", 2) + Scal(5, 5, 5) + Crad(1) + Crvm(1) + Altw(32767, 0, std::vector<std::int16_t>(9, 32767)) + Eof());
        seeds.push_back(Header() + Crvm(0) + Dim("YPTS", 2) + Dim("SIZE", 1) + Dim("XPTS", 5) + Altw(-32768, -32768, std::vector<std::int16_t>(10, -32768), false) + Eof());
    }
    if (const char* c = std::getenv("OKAY_MODEL_CORPUS")) {   // the real files, when available
        namespace fs = std::filesystem;
        std::error_code ec;
        for (const char* name : {"TER/RealisticTerrain.ter", "TER/RealisticTerrain_Large.ter"}) {
            const std::string s = Slurp((fs::path(c) / name).string());
            if (!s.empty()) { seeds.push_back(s); std::printf("  real file %s added (%zu bytes)\n", name, s.size()); }
        }
    }
    for (const std::string& s : seeds) { ImportedModel m; std::string err; CHECK(ReadTerragenBuffer(s, m, &err)); }

    // every truncation of every seed (for the real files: every length up to 4 KB, then a stride)
    for (const std::string& s : seeds) {
        const std::size_t step = s.size() > 4096 ? 1 + s.size() / 4096 : 1;
        for (std::size_t cut = 0; cut < s.size(); cut += (cut < 4096 ? 1 : step)) TryTer(s.substr(0, cut));
    }

    // sizes at and over the caps, contradictions, extreme values
    const std::vector<std::int16_t> four = {0, 0, 0, 0};
    TryTer(Header() + Dim("SIZE", 65535) + Altw(0, 0, {}));
    TryTer(Header() + Dim("SIZE", 1) + Dim("XPTS", 65535) + Dim("YPTS", 65535) + Altw(0, 0, {}));
    TryTer(Header() + Dim("SIZE", 1) + Dim("XPTS", 4097) + Dim("YPTS", 2) + Altw(0, 0, std::vector<std::int16_t>(8194, 1)) + Eof());
    TryTer(Header() + Dim("SIZE", 1) + Dim("XPTS", 2) + Dim("YPTS", 4097) + Altw(0, 0, std::vector<std::int16_t>(8194, 1)) + Eof());
    TryTer(Header() + Dim("SIZE", 1) + Dim("XPTS", 0) + Dim("YPTS", 0) + Altw(0, 0, four));
    TryTer(Header() + Dim("SIZE", 1) + Scal(1e6f, 1e6f, 1e6f) + Altw(32767, 32767, {32767, -32768, 32767, -32768}) + Eof());
    TryTer(Header() + Dim("SIZE", 1) + Scal(1e-30f, 1e-30f, 1e-30f) + Altw(32767, 32767, four) + Eof());
    TryTer(Header() + Dim("SIZE", 1) + Scal(INFINITY, 30, 30) + Altw(0, 0, four) + Eof());
    TryTer(Header() + Dim("SIZE", 1) + Scal(1e-6f, 1e-6f, 1e6f) + Altw(32767, 32767, {32767, -32768, -32768, 32767}) + Eof());   // the steepest slope allowed
    TryTer(Header() + Dim("SIZE", 1) + Crad(1e-30f) + Crvm(1) + Altw(0, 0, four) + Eof());
    TryTer(Header() + Dim("SIZE", 1) + Crad(INFINITY) + Crvm(1) + Altw(0, 0, four) + Eof());
    TryTer(Header() + Dim("SIZE", 1) + Crad(1e30f) + Crvm(1) + Altw(0, 0, four) + Eof());
    TryTer(Header() + Dim("SIZE", 4) + Scal(1e-6f, 1e-6f, 1e-6f) + Crad(1e-9f) + Crvm(1) + Altw(0, 0, std::vector<std::int16_t>(25, 0)) + Eof());
    TryTer(Header() + Dim("SIZE", 1) + Crvm(0xffffffffu) + Altw(0, 0, four) + Eof());
    TryTer(Header() + Dim("SIZE", 1) + Altw(0, 0, four) + std::string(100000, 'E'));
    TryTer(Header() + std::string(1000000, '\0'));
    {   std::string many = Header();   // thousands of chunks before the data: linear
        for (int k = 0; k < 200000; ++k) many += Crvm(0);
        const auto t0 = std::chrono::steady_clock::now();
        TryTer(many + Dim("SIZE", 1) + Altw(0, 0, four) + Eof());
        CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 10.0); }
    {   // the largest grid, refused one point over it, both quickly
        const auto t0 = std::chrono::steady_clock::now();
        TryTer(Header() + Dim("SIZE", 2048) + Altw(1, 0, std::vector<std::int16_t>(2049u * 2049u, 3)) + Eof());
        TryTer(Header() + Dim("SIZE", 1) + Dim("XPTS", 2050) + Dim("YPTS", 2049) + Altw(1, 0, std::vector<std::int16_t>(2050u * 2049u, 3)) + Eof());
        CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 30.0); }

    // ---------------- mutation fuzz ----------------
    {
        const long iters = Iters(8000);
        const long before = g_ok;
        for (long it = 0; it < iters; ++it) {
            std::string s = seeds[Rnd() % seeds.size()];
            if (s.size() > 4096 && Rnd() % 2) s.resize(16 + Rnd() % 4096);   // keep most real-file runs short
            const int ops = 1 + (int)(Rnd() % 4);
            for (int o = 0; o < ops; ++o) MutateBinary(s);
            TryTer(s);
        }
        std::printf("  fuzz: %ld iterations, %ld read, %ld refused (all runs)\n", iters, g_ok, g_refused);
        CHECK(g_ok > before);       // some damaged files still read: the fuzz reaches the mesh-building code
        CHECK(g_refused > 0);
    }
    TEST_MAIN_RESULT();
}
