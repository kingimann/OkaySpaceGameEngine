// Hostile-input tests and a deterministic mutation fuzz for the own SMD reader. Needs only okay/Render/ModelData.hpp and
// engine/src/Render/SmdReader.cpp, so it builds on its own:
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all
//       -Iengine/include -Itests -DOKAY_SOURCE_DIR='"."' tests/test_smd_hostile.cpp engine/src/Render/SmdReader.cpp -o smd_hostile
// OKAY_READER_FUZZ_ITERS=200000 for a long run.
#include "test_framework.hpp"
#include "reader_hostile_common.hpp"
#include <chrono>

using namespace okay;
using namespace hostile;

static const std::string kDir = std::string(OKAY_SOURCE_DIR) + "/tests/data/models/";

static void TrySmd(const std::string& text) {
    ImportedModel m; std::string err;
    const bool ok = ReadSmdBuffer(text, "", m, &err);
    if (ok) { CHECK(Sane(m)); }
    else { CHECK(!err.empty() && m.parts.empty() && m.nodes.empty() && m.clips.empty()); }
}
static double Secs(std::chrono::steady_clock::time_point t0) { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); }

int main() {
    RUN_SUITE("smd_hostile");
    const std::string fixtures[] = {Slurp(kDir + "arm.smd"), Slurp(kDir + "arm_anim.smd"), Slurp(kDir + "arm_crlf.smd")};
    for (const std::string& f : fixtures) CHECK(!f.empty());
    for (const std::string& f : fixtures)
        for (std::size_t cut = 0; cut < f.size(); ++cut) TrySmd(f.substr(0, cut));

    const std::string nodes1 = "version 1\nnodes\n 0 \"a\" -1\nend\n";
    const std::string tri = "triangles\nm.tga\n 0 0 0 0 0 0 1 0 0\n 0 1 0 0 0 0 1 1 0\n 0 0 1 0 0 0 1 0 1\nend\n";
    TrySmd(nodes1 + tri);
    TrySmd(tri);                                            // triangles with no nodes at all
    TrySmd(nodes1 + "triangles\nm.tga\n 5 0 0 0 0 0 1 0 0\n 5 1 0 0 0 0 1 1 0\n 5 0 1 0 0 0 1 0 1\nend\n");   // bone 5 does not exist
    TrySmd(nodes1 + "triangles\nm.tga\n -1 0 0 0 0 0 1 0 0\n 0 1 0 0 0 0 1 1 0\n 0 0 1 0 0 0 1 0 1\nend\n");
    TrySmd(nodes1 + "triangles\nm.tga\n 0 0 0 0 0 0 1 0 0 2 0 -1 5 nan\n 0 1 0 0 0 0 1 1 0\n 0 0 1 0 0 0 1 0 1\nend\n");
    TrySmd(nodes1 + "triangles\nm.tga\n 0 0 0 0 0 0 1 0 0 65 0 1\n 0 1 0 0 0 0 1 1 0\n 0 0 1 0 0 0 1 0 1\nend\n");
    TrySmd(nodes1 + "triangles\nm.tga\n 0 0 0 0 0 0 1 0 0 0\n 0 1 0 0 0 0 1 1 0\n 0 0 1 0 0 0 1 0 1\nend\n");
    TrySmd(nodes1 + "triangles\nm.tga\n 0 0 0 0 0 0 1 0 0 1 0 0\n 0 1 0 0 0 0 1 1 0\n 0 0 1 0 0 0 1 0 1\nend\n");     // a link of weight 0
    TrySmd(nodes1 + "triangles\nm.tga\n 0 1e15 1e15 1e15 0 0 1 0 0\n 0 -1e15 0 0 0 0 1 1 0\n 0 0 1e15 0 0 0 1 0 1\nend\n");
    TrySmd(nodes1 + "triangles\nm.tga\n 0 1e16 0 0 0 0 1 0 0\n 0 1 0 0 0 0 1 1 0\n 0 0 1 0 0 0 1 0 1\nend\n");
    TrySmd(nodes1 + "triangles\nm.tga\n 0 0 0 0 1e30 0 0 0 0\n 0 1 0 0 1e30 0 0 1 0\n 0 0 1 0 1e30 0 0 0 1\nend\n");    // normals at the limit
    TrySmd(nodes1 + "triangles\n" + std::string(100000, 'm') + "\n 0 0 0 0 0 0 1 0 0\n 0 1 0 0 0 0 1 1 0\n 0 0 1 0 0 0 1 0 1\nend\n");   // a long material name
    // 5000 materials -> over the cap of 4096
    {
        std::string t = nodes1 + "triangles\n";
        for (int i = 0; i < 5000; ++i) t += "m" + std::to_string(i) + "\n 0 0 0 0 0 0 1 0 0\n 0 1 0 0 0 0 1 1 0\n 0 0 1 0 0 0 1 0 1\n";
        TrySmd(t + "end\n");
    }
    // many bones: the cap of 4096 refuses more, 4000 deep chain is fine and does not recurse
    {
        std::string t = "nodes\n";
        for (int i = 0; i < 4000; ++i) t += " " + std::to_string(i) + " \"b" + std::to_string(i) + "\" " + std::to_string(i - 1) + "\n";
        t += "end\nskeleton\ntime 0\n";
        for (int i = 0; i < 4000; ++i) t += " " + std::to_string(i) + " 0 1 0 0 0 0.01\n";
        t += "end\n" + std::string();
        const auto t0 = std::chrono::steady_clock::now();
        ImportedModel m; std::string err;
        CHECK(ReadSmdBuffer(t, "", m, &err) && Sane(m) && m.nodes.size() == 4001);
        CHECK(Secs(t0) < 10.0);
        std::string t2 = "nodes\n";
        for (int i = 0; i < 4097; ++i) t2 += " " + std::to_string(i) + " \"b\" -1\n";
        CHECK(!ReadSmdBuffer(t2 + "end\n", "", m, &err) && err.find("too many bones") != std::string::npos);
    }
    // all parents pointing at each other in one big loop, and a self parent
    {
        std::string t = "nodes\n";
        for (int i = 0; i < 3000; ++i) t += " " + std::to_string(i) + " \"b" + std::to_string(i) + "\" " + std::to_string((i + 1) % 3000) + "\n";
        TrySmd(t + "end\nskeleton\ntime 0\n 0 0 0 0 0 0 0\nend\n");
        TrySmd("nodes\n 0 \"a\" 0\nend\nskeleton\ntime 0\n 0 0 0 0 0 0 0\nend\n");
        TrySmd("nodes\n 0 \"a\" -1\n 0 \"b\" -1\nend\n");   // the same id twice
    }
    // frame counts: 100000 frames at the cap, 100001 over it; negative and repeated times
    {
        const std::string head = "nodes\n 0 \"a\" -1\nend\nskeleton\n";
        std::string t = head;
        for (int i = 0; i <= 100000; ++i) t += "time " + std::to_string(i) + "\n 0 0 0 0 0 0 0\n";
        t += "end\n";
        const auto t0 = std::chrono::steady_clock::now();
        ImportedModel m; std::string err;
        CHECK(!ReadSmdBuffer(t, "", m, &err));
        CHECK(Secs(t0) < 20.0);
        TrySmd(head + "time -5\n 0 0 0 0 0 0 0\ntime -10\n 0 0 0 0 0 0 0\ntime -10\n 0 1 0 0 0 0 0\nend\n");
        TrySmd(head + "time 2147483647\n 0 0 0 0 0 0 0\ntime 0\n 0 1 0 0 0 0 0\nend\n");
        TrySmd(head + "time 2147483648\n 0 0 0 0 0 0 0\nend\n");
        TrySmd(head + "time 0\n 0 1 2 3 4 5 6\n 0 1 2 3 4 5 6\n 7 1 2 3 4 5 6\nend\n");
        TrySmd(head + "time 0\n 0 0 0 0 1e15 1e15 1e15\ntime 1\n 0 1e15 0 0 0 0 0\nend\n");
        TrySmd(head + "time 0\n 0 0 0 0 0 0\nend\n");
    }
    TrySmd("version 1\nnodes\nvertexanimation\ntime 0\n 1 0 0 0 0 0 1\nend\n");
    TrySmd("garbage\nmore garbage\nnodes\nend\n");
    { std::string t = "nodes\n 0 \"a\" -1\nend\nskeleton\ntime 0\n 0 0 0 0 0 0 0\nend\n"; t.append(3, '\0'); TrySmd(t); }
    TrySmd("nodes\n 0 \"unterminated -1\nend\n");
    TrySmd("nodes\n 0 \"\" -1\nend\n");
    TrySmd("//comment\nnodes // another\n 0 \"a\" -1 // x\nend\n");
    { std::string t = "\xFF\xFE"; for (char c : std::string("nodes\n")) { t += c; t += '\0'; } TrySmd(t); }   // UTF-16 with a mark
    TrySmd(std::string("\xEF\xBB\xBF") + nodes1);
    TrySmd(std::string("\xFF\xFE\x00\xD8", 4));                    // a lone surrogate
    // a flood of identical triangles on one material (welding must stay linear)
    {
        std::string t = nodes1 + "triangles\nm.tga\n";
        for (int i = 0; i < 60000; ++i) t += " 0 0 0 0 0 0 1 0 0\n 0 1 0 0 0 0 1 1 0\n 0 0 1 0 0 0 1 0 1\nm.tga\n";
        const auto t0 = std::chrono::steady_clock::now();
        t += "end\n";
        TrySmd(t);
        CHECK(Secs(t0) < 15.0);
    }

    // ---------------- mutation fuzz ----------------
    {
        const long iters = Iters(8000);
        const std::string alphabet = "0123456789.-+eE \n\r\t\"/#nodestriangleskeletonvrsi";
        for (long it = 0; it < iters; ++it) {
            std::string s = fixtures[Rnd() % 3];
            const int ops = 1 + (int)(Rnd() % 4);
            for (int o = 0; o < ops; ++o) MutateText(s, alphabet);
            TrySmd(s);
        }
        std::printf("  fuzz: %ld iterations\n", iters);
    }
    TEST_MAIN_RESULT();
}
