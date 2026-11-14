// Hostile-input tests and a deterministic mutation fuzz for the own OFF reader. Needs only okay/Render/ModelData.hpp and
// engine/src/Render/OffReader.cpp, so it builds on its own:
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all
//       -Iengine/include -Itests -DOKAY_SOURCE_DIR='"."' tests/test_off_hostile.cpp engine/src/Render/OffReader.cpp -o off_hostile
// OKAY_READER_FUZZ_ITERS=200000 for a long run.
#include "test_framework.hpp"
#include "reader_hostile_common.hpp"
#include <chrono>

using namespace okay;
using namespace hostile;

static const std::string kDir = std::string(OKAY_SOURCE_DIR) + "/tests/data/models/";

static void TryOff(const std::string& text) {
    ImportedModel m; std::string err;
    const bool ok = ReadOffBuffer(text, m, &err);
    if (ok) { CHECK(Sane(m) && !m.parts.empty()); }
    else { CHECK(!err.empty() && m.parts.empty() && m.nodes.empty()); }
}

int main() {
    RUN_SUITE("off_hostile");
    const std::string fixtures[] = {Slurp(kDir + "cube.off"), Slurp(kDir + "square_stnoff.off"), Slurp(kDir + "mixed_coff.off"), Slurp(kDir + "cube_crlf.off")};
    for (const std::string& f : fixtures) CHECK(!f.empty());

    for (const std::string& f : fixtures)
        for (std::size_t cut = 0; cut < f.size(); ++cut) TryOff(f.substr(0, cut));

    const std::string tri = "0 0 0\n1 0 0\n0 1 0\n3 0 1 2\n";
    TryOff("OFF\n3 1 0\n" + tri.substr(0, tri.size() - 8));
    TryOff("OFF\n10000000 1 0\n0 0 0\n");                       // the largest legal count, no data
    TryOff("OFF\n10000001 1 0\n0 0 0\n");                       // one over the cap
    TryOff("OFF\n3 10000001 0\n" + tri);
    TryOff("OFF\n2147483647 2147483647 0\n");
    TryOff("OFF\n4294967296 1 0\n");
    TryOff("OFF\n3 1 0\n0 0 0\n1 0 0\n0 1 0\n3 0 1 2 3 4 5 6 7 8 9\n");
    TryOff("OFF\n3 1 0\n0 0 0\n1 0 0\n0 1 0\n65537 0 1 2\n");
    TryOff("OFF\n3 1 0\n0 0 0\n1 0 0\n0 1 0\n3 -1 1 2\n");
    TryOff("OFF\n3 1 0\n0 0 0\n1 0 0\n0 1 0\n3 0 1 2147483647\n");
    TryOff("OFF\n3 1 0\n0 0 0\n1 0 0\n0 1 0\n3 0 1 99999999999\n");
    TryOff("OFF\n3 1 0\n0 0 0\n1e15 0 0\n0 1 0\n3 0 1 2\n");   // at the coordinate limit
    TryOff("OFF\n3 1 0\n0 0 0\n1e15 1e15 1e15\n-1e15 0 0\n3 0 1 2\n");
    TryOff("OFF\n3 1 0\n0 0 0\n1e16 0 0\n0 1 0\n3 0 1 2\n");
    TryOff("OFF\n3 1 0\n0 0 0\n1e400 0 0\n0 1 0\n3 0 1 2\n");
    TryOff("OFF\n3 1 0\n0 0 0\n-0 -0 -0\n0 1 0\n3 0 1 2\n");
    { std::string t = "OFF\n3 1 0\n0 0 0\n1 0 0\n0 1"; t.push_back('\0'); t += " 0\n3 0 1 2\n"; TryOff(t); }   // NUL byte
    TryOff("OFF\n3 1 0\n" + std::string(100000, '9') + " 0 0\n1 0 0\n0 1 0\n3 0 1 2\n");
    TryOff("NOFF\n3 1 0\n0 0 0 0 0 0\n1 0 0 0 0 0\n0 1 0 0 0 0\n3 0 1 2\n");      // zero normals: generated instead
    TryOff("NOFF\n3 1 0\n0 0 0 1 0 0\n1 0 0 1 0 0\n0 1 0 1e30 0 0\n3 0 1 2\n");
    TryOff("COFF\n3 1 0\n0 0 0 1\n1 0 0 1 2 3 4 5 6 7\n0 1 0\n3 0 1 2\n");
    TryOff("STOFF\n3 1 0\n0 0 0 1\n1 0 0 1 2\n0 1 0 5 5\n3 0 1 2\n");
    TryOff("STCNOFF\n3 1 0\n0 0 0 1 2 3 4\n1 0 0 1 2 3 4 5 6 7\n0 1 0\n3 0 1 2\n");
    TryOff("OFF OFF OFF\n3 1 0\n");
    TryOff(std::string(100000, '#') + "\nOFF\n3 1 0\n" + tri);
    TryOff("OFF\n" + std::string(200000, '\n') + "3 1 0\n" + tri);
    // a huge convex polygon: 60000 corners on a circle, at the corner cap
    {
        std::string t = "OFF\n60000 1 0\n";
        for (int i = 0; i < 60000; ++i) { const float a = (float)i / 60000.0f * 6.2831853f; t += std::to_string(std::cos(a)) + " " + std::to_string(std::sin(a)) + " 0\n"; }
        t += "60000";
        for (int i = 0; i < 60000; ++i) t += " " + std::to_string(i);
        t += "\n";
        const auto t0 = std::chrono::steady_clock::now();
        ImportedModel m; std::string err;
        CHECK(ReadOffBuffer(t, m, &err) && Sane(m) && m.parts[0].mesh.triangles.size() == 3 * 59998);
        CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 20.0);
    }
    // a concave polygon with many corners (a zig-zag comb): ear clipping must stay bounded
    {
        std::string t = "OFF\n200 1 0\n";
        for (int i = 0; i < 100; ++i) t += std::to_string(i) + " " + std::to_string(i % 2 ? 1 : 0) + " 0\n";
        for (int i = 99; i >= 0; --i) t += std::to_string(i) + " 2 0\n";
        t += "200";
        for (int i = 0; i < 200; ++i) t += " " + std::to_string(i);
        t += "\n";
        const auto t0 = std::chrono::steady_clock::now();
        TryOff(t);
        CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 10.0);
    }
    // a very large vertex count claimed by a short file: refused before any allocation
    { ImportedModel m; std::string err; CHECK(!ReadOffBuffer("OFF\n9999999 9999999 0\n0 0 0\n", m, &err) && err.find("larger than the file") != std::string::npos); }
    // many faces on the same three vertices: linear time
    {
        std::string t = "OFF\n3 20000 0\n0 0 0\n1 0 0\n0 1 0\n";
        for (int i = 0; i < 20000; ++i) t += "3 0 1 2\n";
        const auto t0 = std::chrono::steady_clock::now();
        TryOff(t);
        CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 10.0);
    }

    // ---------------- mutation fuzz ----------------
    {
        const long iters = Iters(8000);
        const std::string alphabet = "0123456789.-+eE \n\r\t#OFSTCN4n";
        for (long it = 0; it < iters; ++it) {
            std::string s = fixtures[Rnd() % 4];
            const int ops = 1 + (int)(Rnd() % 4);
            for (int o = 0; o < ops; ++o) MutateText(s, alphabet);
            TryOff(s);
        }
        std::printf("  fuzz: %ld iterations\n", iters);
    }
    TEST_MAIN_RESULT();
}
