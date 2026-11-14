// Hostile-input tests and a deterministic mutation fuzz for the own MD5 reader. Needs only okay/Render/ModelData.hpp and
// engine/src/Render/Md5Reader.cpp, so it builds on its own:
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all
//       -Iengine/include -Itests -DOKAY_SOURCE_DIR='"."' tests/test_md5_hostile.cpp engine/src/Render/Md5Reader.cpp -o md5_hostile
// OKAY_READER_FUZZ_ITERS=200000 for a long run.
#include "test_framework.hpp"
#include "reader_hostile_common.hpp"
#include <chrono>

using namespace okay;
using namespace hostile;

static const std::string kDir = std::string(OKAY_SOURCE_DIR) + "/tests/data/models/";

static void TryMd5(const std::string& mesh, const std::string& anim) {
    ImportedModel m; std::string err;
    const bool ok = ReadMd5Buffer(mesh, anim, "", m, &err);
    if (ok) { CHECK(Sane(m)); }
    else { CHECK(!err.empty() && m.parts.empty() && m.nodes.empty() && m.clips.empty()); }
}
static double Secs(std::chrono::steady_clock::time_point t0) { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); }

static const char* kJ1 = "MD5Version 10\nnumJoints 1\nnumMeshes 1\njoints {\n\"a\" -1 ( 0 0 0 ) ( 0 0 0 )\n}\n";
static const char* kA1 = "MD5Version 10\nnumFrames 1\nnumJoints 1\nframeRate 24\nnumAnimatedComponents 6\nhierarchy {\n\"a\" -1 63 0\n}\nbaseframe {\n( 0 0 0 ) ( 0 0 0 )\n}\n";

int main() {
    RUN_SUITE("md5_hostile");
    const std::string fm = Slurp(kDir + "skel.md5mesh"), fa = Slurp(kDir + "skel.md5anim"), fmc = Slurp(kDir + "skel_crlf.md5mesh"), fac = Slurp(kDir + "skel_crlf.md5anim");
    CHECK(!fm.empty() && !fa.empty() && !fmc.empty() && !fac.empty());
    for (std::size_t cut = 0; cut < fm.size(); ++cut) { TryMd5(fm.substr(0, cut), ""); if (cut % 5 == 0) TryMd5(fm.substr(0, cut), fa); }
    for (std::size_t cut = 0; cut < fa.size(); ++cut) { TryMd5("", fa.substr(0, cut)); if (cut % 5 == 0) TryMd5(fm, fa.substr(0, cut)); }
    for (std::size_t cut = 0; cut < fmc.size(); cut += 3) TryMd5(fmc.substr(0, cut), fac);

    // meshes: counts that claim far more than the file holds
    const std::string j1 = kJ1;
    TryMd5(j1 + "mesh {\nnumverts 10000000\n}\n", "");
    TryMd5(j1 + "mesh {\nnumverts 10000001\n}\n", "");
    TryMd5(j1 + "mesh {\nnumtris 20000001\n}\n", "");
    TryMd5(j1 + "mesh {\nnumweights 40000001\n}\n", "");
    TryMd5(j1 + "mesh {\nnumverts 2147483647\nnumtris 2147483647\nnumweights 2147483647\n}\n", "");
    TryMd5(j1 + "mesh {\nnumverts 3\nvert 0 ( 0 0 ) 0 1\nvert 1 ( 0 0 ) 0 1\nvert 2 ( 0 0 ) 0 1\nnumtris 1\ntri 0 0 1 2\nnumweights 1\nweight 0 0 1 ( 0 0 0 )\n}\n", "");
    TryMd5(j1 + "mesh {\nnumverts 3\nvert 0 ( 0 0 ) 0 1\nvert 1 ( 0 0 ) 0 1\nvert 2 ( 0 0 ) 0 1\nnumtris 2\ntri 0 0 1 2\ntri 1 0 1 9\nnumweights 1\nweight 0 0 0 ( 0 0 0 )\n}\n", "");   // zero bias
    TryMd5(j1 + "mesh {\nnumverts 3\nvert 0 ( 0 0 ) 0 1\nvert 1 ( 0 0 ) 0 1\nvert 2 ( 0 0 ) 0 1\nnumtris 1\ntri 0 0 0 0\nnumweights 1\nweight 0 0 -1 ( 0 0 0 )\n}\n", "");
    TryMd5(j1 + "mesh {\nnumverts 3\nvert 0 ( 0 0 ) 0 1\nvert 1 ( 0 0 ) 0 1\nvert 2 ( 0 0 ) 0 1\nnumtris 1\ntri 0 0 1 2\nnumweights 1\nweight 0 0 1 ( 1e15 1e15 1e15 )\n}\n", "");
    TryMd5(j1 + "mesh {\nnumverts 3\nvert 0 ( 0 0 ) 0 1\nvert 1 ( 0 0 ) 0 1\nvert 2 ( 0 0 ) 0 1\nnumtris 1\ntri 0 0 1 2\nnumweights 1\nweight 0 0 1e30 ( 1e15 0 0 )\n}\n", "");
    TryMd5("MD5Version 10\nnumJoints 1\nnumMeshes 0\njoints {\n\"a\" -1 ( 1e15 1e15 1e15 ) ( 1e15 1e15 1e15 )\n}\n", "");   // an unnormalizable orientation
    TryMd5("MD5Version 10\nnumJoints 1\nnumMeshes 0\njoints {\n\"a\" -1 ( 0 0 0 ) ( 0.9 0.9 0.9 )\n}\n", "");                // x^2+y^2+z^2 > 1: w = 0
    TryMd5("MD5Version 10\nnumJoints 1\nnumMeshes 0\njoints {\n" + std::string("\"") + std::string(100000, 'n') + "\" -1 ( 0 0 0 ) ( 0 0 0 )\n}\n", "");
    // joints: a long chain (no recursion), a loop, a self parent, the 4096 cap and one over it
    {
        std::string t = "MD5Version 10\nnumJoints 4000\nnumMeshes 0\njoints {\n";
        for (int i = 0; i < 4000; ++i) t += "\"j" + std::to_string(i) + "\" " + std::to_string(i - 1) + " ( 0 1 0 ) ( 0 0 0.1 )\n";
        t += "}\n";
        const auto t0 = std::chrono::steady_clock::now();
        ImportedModel m; std::string err;
        CHECK(ReadMd5Buffer(t, "", "", m, &err) && Sane(m) && m.nodes.size() == 4001);
        CHECK(Secs(t0) < 10.0);
        std::string l = "MD5Version 10\nnumJoints 3000\nnumMeshes 0\njoints {\n";
        for (int i = 0; i < 3000; ++i) l += "\"j" + std::to_string(i) + "\" " + std::to_string((i + 1) % 3000) + " ( 0 1 0 ) ( 0 0 0 )\n";
        TryMd5(l + "}\n", "");
        TryMd5("MD5Version 10\nnumJoints 2\nnumMeshes 0\njoints {\n\"a\" 0 ( 0 0 0 ) ( 0 0 0 )\n\"b\" 1 ( 0 0 0 ) ( 0 0 0 )\n}\n", "");
        std::string over = "MD5Version 10\nnumJoints 4097\nnumMeshes 0\njoints {\n}\n";
        CHECK(!ReadMd5Buffer(over, "", "", m, &err) && err.find("too many joints") != std::string::npos);
    }
    // animations: counts, flags, indices
    const std::string a1 = kA1;
    TryMd5("", a1 + "frame 0 {\n1 2 3 4 5 6\n}\n");
    TryMd5("", a1 + "frame 0 {\n1 2 3 4 5\n}\n");
    TryMd5("", a1 + "frame 0 {\n1 2 3 4 5 6 7\n}\n");
    TryMd5("", a1 + "frame 1 {\n1 2 3 4 5 6\n}\n");
    TryMd5("", a1 + "frame -1 {\n1 2 3 4 5 6\n}\n");
    TryMd5("", a1 + "frame 0 {\n1e15 1e15 1e15 1e15 1e15 1e15\n}\n");
    TryMd5("", a1 + "frame 0 {\n1e16 0 0 0 0 0\n}\n");
    TryMd5("", a1 + "frame 0 {\n0 0 0 0.9 0.9 0.9\n}\n");
    TryMd5("", a1);
    TryMd5(j1 + "mesh {\n}\n", a1 + "frame 0 {\n1 2 3 4 5 6\n}\n");
    TryMd5("", "MD5Version 10\nnumFrames 100000\nnumJoints 1\nnumAnimatedComponents 6\nhierarchy {\n\"a\" -1 63 0\n}\nbaseframe {\n( 0 0 0 ) ( 0 0 0 )\n}\nframe 0 {\n1 2 3 4 5 6\n}\n");
    TryMd5("", "MD5Version 10\nnumFrames 100000\nnumJoints 4096\nnumAnimatedComponents 24576\nframe 0 {\n}\n");   // frames x joints over the sample cap, text too small
    TryMd5("", "MD5Version 10\nnumFrames 100001\nnumJoints 1\n");
    TryMd5("", "MD5Version 10\nnumFrames 1\nnumJoints 1\nframeRate 1e30\n");
    TryMd5("", "MD5Version 10\nnumFrames 1\nnumJoints 1\nframeRate 1e-30\nnumAnimatedComponents 0\nhierarchy {\n\"a\" -1 0 0\n}\nbaseframe {\n( 0 0 0 ) ( 0 0 0 )\n}\nframe 0 {\n}\n");
    TryMd5("", "MD5Version 10\nnumFrames 1\nnumJoints 1\nnumAnimatedComponents 0\nhierarchy {\n\"a\" -1 0 2147483647\n}\nbaseframe {\n( 0 0 0 ) ( 0 0 0 )\n}\nframe 0 {\n}\n");
    TryMd5("", "MD5Version 10\nnumFrames 1\nnumJoints 1\nnumAnimatedComponents 0\nhierarchy {\n\"a\" -1 64 0\n}\n");
    {   // the largest animation the caps allow with a small text: refused before allocating
        const auto t0 = std::chrono::steady_clock::now();
        ImportedModel m; std::string err;
        CHECK(!ReadMd5Buffer("", "MD5Version 10\nnumFrames 100000\nnumJoints 4000\nnumAnimatedComponents 24000\nhierarchy {\n}\n", "", m, &err));
        CHECK(Secs(t0) < 5.0);
    }
    // a real-sized animation: 2000 frames x 30 joints x 6 components
    {
        std::string t = "MD5Version 10\nnumFrames 2000\nnumJoints 30\nframeRate 24\nnumAnimatedComponents 180\nhierarchy {\n";
        for (int i = 0; i < 30; ++i) t += "\"j" + std::to_string(i) + "\" " + std::to_string(i - 1) + " 63 " + std::to_string(i * 6) + "\n";
        t += "}\nbaseframe {\n";
        for (int i = 0; i < 30; ++i) t += "( 0 1 0 ) ( 0 0 0 )\n";
        t += "}\n";
        for (int f = 0; f < 2000; ++f) { t += "frame " + std::to_string(f) + " {\n"; for (int k = 0; k < 180; ++k) t += (k % 6 < 3 ? "1.5 " : "0.01 "); t += "\n}\n"; }
        const auto t0 = std::chrono::steady_clock::now();
        ImportedModel m; std::string err;
        CHECK(ReadMd5Buffer("", t, "", m, &err) && Sane(m) && m.clips.size() == 1 && m.clips[0].channels.size() == 30 && m.clips[0].channels[0].position.size() == 2000);
        CHECK(Secs(t0) < 10.0);
    }
    TryMd5(std::string("\xEF\xBB\xBF") + fm, "");
    { std::string t = "\xFF\xFE"; for (char c : std::string("MD5Version 10\n")) { t += c; t += '\0'; } TryMd5(t, ""); }
    { std::string t = fm; t.push_back('\0'); t += "}}}{{{"; TryMd5(t, ""); }
    TryMd5("MD5Version 10 // comment\nnumJoints 0\nnumMeshes 0\njoints {\n}\n", "");
    TryMd5("MD5Version 10\n" + std::string(100000, '{'), "");
    TryMd5("MD5Version 10\n\"" + std::string(100000, 'x'), "");

    // ---------------- mutation fuzz ----------------
    {
        const long iters = Iters(8000);
        const std::string alphabet = "0123456789.-+eE \n\r\t\"(){}/#abcdefjmnstuvwx_";
        for (long it = 0; it < iters; ++it) {
            std::string a = (it & 1) ? fm : fmc, b = (it & 1) ? fa : fac;
            const int ops = 1 + (int)(Rnd() % 4);
            const int which = (int)(Rnd() % 3);
            for (int o = 0; o < ops; ++o) { if (which != 1) MutateText(a, alphabet); if (which != 0) MutateText(b, alphabet); }
            TryMd5(which == 1 ? fm : a, which == 0 ? "" : b);
            if (which == 2) TryMd5("", b);
        }
        std::printf("  fuzz: %ld iterations\n", iters);
    }
    TEST_MAIN_RESULT();
}
