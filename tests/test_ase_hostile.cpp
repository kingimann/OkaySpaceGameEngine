// Hostile-input tests and a deterministic mutation fuzz for the own ASE reader. Needs only okay/Render/ModelData.hpp and
// engine/src/Render/AseReader.cpp, so it builds on its own:
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all
//       -Iengine/include -Itests -DOKAY_SOURCE_DIR='"."' tests/test_ase_hostile.cpp engine/src/Render/AseReader.cpp -o ase_hostile
// OKAY_READER_FUZZ_ITERS=200000 for a long run.
#include "test_framework.hpp"
#include "reader_hostile_common.hpp"
#include <chrono>

using namespace okay;
using namespace hostile;

static const std::string kDir = std::string(OKAY_SOURCE_DIR) + "/tests/data/models/";

static void TryAse(const std::string& text) {
    ImportedModel m; std::string err;
    const bool ok = ReadAseBuffer(text, "", m, &err);
    if (ok) { CHECK(Sane(m)); }
    else { CHECK(!err.empty() && m.parts.empty() && m.nodes.empty() && m.clips.empty()); }
}
static double Secs(std::chrono::steady_clock::time_point t0) { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); }

static const char* kHead = "*3DSMAX_ASCIIEXPORT 200\n";
// one mesh object with `body` pasted inside its *MESH block
static std::string Obj(const std::string& name, const std::string& tm, const std::string& body, const std::string& parent = "") {
    std::string s = "*GEOMOBJECT {\n*NODE_NAME \"" + name + "\"\n";
    if (!parent.empty()) s += "*NODE_PARENT \"" + parent + "\"\n";
    s += "*NODE_TM {\n*NODE_NAME \"" + name + "\"\n" + tm + "}\n*MESH {\n" + body + "}\n}\n";
    return s;
}
static const char* kIdTm = "*TM_ROW0 1 0 0\n*TM_ROW1 0 1 0\n*TM_ROW2 0 0 1\n*TM_ROW3 0 0 0\n";
static std::string Tri(const std::string& v2 = "0 1 0", const std::string& face = "A: 0 B: 1 C: 2") {
    return "*MESH_NUMVERTEX 3\n*MESH_NUMFACES 1\n*MESH_VERTEX_LIST {\n*MESH_VERTEX 0 0 0 0\n*MESH_VERTEX 1 1 0 0\n*MESH_VERTEX 2 " + v2 + "\n}\n*MESH_FACE_LIST {\n*MESH_FACE 0: " + face + " *MESH_SMOOTHING 1 *MESH_MTLID 0\n}\n";
}

int main() {
    RUN_SUITE("ase_hostile");
    const std::string fixtures[] = {Slurp(kDir + "scene.ase"), Slurp(kDir + "scene_crlf.ase"), Slurp(kDir + "scene_utf16le.ase")};
    for (const std::string& f : fixtures) CHECK(!f.empty());
    for (const std::string& f : fixtures) for (std::size_t cut = 0; cut < f.size(); cut += (f.size() > 4000 ? 3 : 1)) TryAse(f.substr(0, cut));

    const std::string head = kHead;
    TryAse(head + Obj("a", kIdTm, Tri()));
    TryAse(head + Obj("a", kIdTm, Tri("0 1 nan")));
    TryAse(head + Obj("a", kIdTm, Tri("0 1 inf")));
    TryAse(head + Obj("a", kIdTm, Tri("0 1 1e15")));
    TryAse(head + Obj("a", kIdTm, Tri("0 1 1e16")));
    TryAse(head + Obj("a", kIdTm, Tri("1e15 1e15 1e15")));
    TryAse(head + Obj("a", kIdTm, Tri("0 1 0", "A: 0 B: 1 C: 9")));
    TryAse(head + Obj("a", kIdTm, Tri("0 1 0", "A: 0 B: 1 C: -1")));
    TryAse(head + Obj("a", kIdTm, Tri("0 1 0", "A: 0 B: 1 C: 99999999999")));
    TryAse(head + Obj("a", kIdTm, Tri("0 1 0", "A: 0 B: 0 C: 0")));
    TryAse(head + Obj("a", kIdTm, Tri("0 1 0", "A: 0 B: 1")));
    // transforms: singular, huge, NaN, mirrored, sheared
    TryAse(head + Obj("a", "*TM_ROW0 0 0 0\n*TM_ROW1 0 0 0\n*TM_ROW2 0 0 0\n*TM_ROW3 0 0 0\n", Tri()));
    TryAse(head + Obj("a", "*TM_ROW0 1e15 0 0\n*TM_ROW1 0 1e15 0\n*TM_ROW2 0 0 1e15\n*TM_ROW3 1e15 1e15 1e15\n", Tri()));
    TryAse(head + Obj("a", "*TM_ROW0 1e-15 0 0\n*TM_ROW1 0 1e-15 0\n*TM_ROW2 0 0 1e-15\n*TM_ROW3 0 0 0\n", Tri("1e15 1e15 1e15")));
    TryAse(head + Obj("a", "*TM_ROW0 nan 0 0\n*TM_ROW1 0 1 0\n*TM_ROW2 0 0 1\n*TM_ROW3 0 0 0\n", Tri()));
    TryAse(head + Obj("a", "*TM_ROW0 -1 0 0\n*TM_ROW1 0 1 0\n*TM_ROW2 0 0 1\n*TM_ROW3 0 0 0\n", Tri()));
    TryAse(head + Obj("a", "*TM_ROW0 1 1 0\n*TM_ROW1 0 1 0\n*TM_ROW2 0 0 1\n*TM_ROW3 0 0 0\n", Tri()));
    TryAse(head + Obj("a", "*TM_ROW0 1 0 0\n*TM_ROW1 1 0 0\n*TM_ROW2 0 0 1\n*TM_ROW3 0 0 0\n", Tri()));
    // counts that claim more than the file holds
    TryAse(head + Obj("a", kIdTm, "*MESH_NUMVERTEX 10000000\n*MESH_NUMFACES 10000000\n"));
    TryAse(head + Obj("a", kIdTm, "*MESH_NUMVERTEX 10000001\n"));
    TryAse(head + Obj("a", kIdTm, "*MESH_NUMVERTEX 2147483647\n*MESH_NUMFACES 2147483647\n*MESH_NUMTVERTEX 2147483647\n"));
    TryAse(head + Obj("a", kIdTm, "*MESH_NUMVERTEX -5\n"));
    TryAse(head + "*MATERIAL_LIST {\n*MATERIAL_COUNT 2147483647\n}\n");
    // nesting: 100000 blocks inside each other hits the depth cap, not the stack
    {
        std::string t = head;
        for (int i = 0; i < 100000; ++i) t += "*X {\n";
        ImportedModel m; std::string err;
        CHECK(!ReadAseBuffer(t, "", m, &err) && err.find("nested") != std::string::npos);
        std::string c = head + "}\n}\n}\n";
        CHECK(!ReadAseBuffer(c, "", m, &err));
    }
    // many objects (the cap is 100000 nodes) and a parent chain 20000 deep: iterative, no recursion
    {
        std::string t = head;
        for (int i = 0; i < 20000; ++i) t += Obj("n" + std::to_string(i), kIdTm, i % 1000 == 0 ? Tri() : "", i ? "n" + std::to_string(i - 1) : "");
        const auto t0 = std::chrono::steady_clock::now();
        ImportedModel m; std::string err;
        CHECK(ReadAseBuffer(t, "", m, &err) && Sane(m) && m.nodes.size() == 20001);
        CHECK(Secs(t0) < 20.0);
        std::string loop = head;
        for (int i = 0; i < 5000; ++i) loop += Obj("l" + std::to_string(i), kIdTm, Tri(), "l" + std::to_string((i + 1) % 5000));
        TryAse(loop);
    }
    // a vertex shared by many faces with distinct smoothing groups
    {
        std::string v = "*MESH_NUMVERTEX 3002\n*MESH_NUMFACES 3000\n*MESH_VERTEX_LIST {\n*MESH_VERTEX 0 0 0 0\n";
        for (int i = 1; i <= 3001; ++i) v += "*MESH_VERTEX " + std::to_string(i) + " " + std::to_string(std::cos(i * 0.002)) + " " + std::to_string(std::sin(i * 0.002)) + " 0\n";
        v += "}\n*MESH_FACE_LIST {\n";
        for (int i = 0; i < 3000; ++i) v += "*MESH_FACE " + std::to_string(i) + ": A: 0 B: " + std::to_string(i + 1) + " C: " + std::to_string(i + 2) + " *MESH_SMOOTHING " + std::to_string(i % 40) + "," + std::to_string(i % 7 + 1) + " *MESH_MTLID 0\n";
        v += "}\n";
        const auto t0 = std::chrono::steady_clock::now();
        TryAse(head + Obj("fan", kIdTm, v));
        CHECK(Secs(t0) < 10.0);
    }
    // materials: sub-material ids far outside the list, recursion-looking nesting, huge names
    TryAse(head + "*MATERIAL_LIST {\n*MATERIAL_COUNT 1\n*MATERIAL 0 {\n*NUMSUBMTLS 3\n*SUBMATERIAL 0 {\n*MATERIAL_NAME \"x\"\n}\n}\n}\n" + Obj("a", std::string(kIdTm) + "}\n*MATERIAL_REF 0\n*X {\n", Tri()));
    TryAse(head + "*MATERIAL_LIST {\n*MATERIAL_COUNT 1\n*MATERIAL 0 {\n*MATERIAL_NAME \"" + std::string(100000, 'n') + "\"\n*MATERIAL_DIFFUSE nan 2 -3\n*MATERIAL_SHINE 1e30\n*MATERIAL_TRANSPARENCY -1e30\n*MATERIAL_SELFILLUM 1e30\n}\n}\n" + "*GEOMOBJECT {\n*NODE_NAME \"a\"\n*MATERIAL_REF 0\n*MESH {\n" + Tri() + "}\n}\n");
    // animation keys: out of order, negative, NaN, huge counts of the same tick, short lines, a million samples
    {
        auto anim = [&](const std::string& tracks) { return head + "*GEOMOBJECT {\n*NODE_NAME \"a\"\n*NODE_TM {\n" + kIdTm + "}\n*MESH {\n" + Tri() + "}\n*TM_ANIMATION {\n*NODE_NAME \"a\"\n" + tracks + "}\n}\n"; };
        TryAse(anim("*CONTROL_POS_TRACK {\n*CONTROL_POS_SAMPLE 100 1 2 3\n*CONTROL_POS_SAMPLE 50 1 2 3\n}\n"));
        TryAse(anim("*CONTROL_POS_TRACK {\n*CONTROL_POS_SAMPLE -100 1 2 3\n*CONTROL_POS_SAMPLE 50 1 2 3\n}\n"));
        TryAse(anim("*CONTROL_POS_TRACK {\n*CONTROL_POS_SAMPLE 0 nan 2 3\n*CONTROL_POS_SAMPLE 50 1 2 3\n}\n"));
        TryAse(anim("*CONTROL_POS_TRACK {\n*CONTROL_POS_SAMPLE 0 1e15 1e15 1e15\n*CONTROL_POS_SAMPLE 50 1 2 3\n}\n"));
        TryAse(anim("*CONTROL_POS_TRACK {\n*CONTROL_POS_SAMPLE 0 1 2\n*CONTROL_POS_SAMPLE 50 1 2 3\n}\n"));
        TryAse(anim("*CONTROL_POS_TRACK {\n*CONTROL_POS_SAMPLE 1e300 1 2 3\n*CONTROL_POS_SAMPLE 1e301 1 2 3\n}\n"));
        TryAse(anim("*CONTROL_ROT_TRACK {\n*CONTROL_ROT_SAMPLE 0 0 0 0 1\n*CONTROL_ROT_SAMPLE 10 0 0 0 1\n*CONTROL_ROT_SAMPLE 20 1e30 1e30 1e30 1e30\n}\n"));
        TryAse(anim("*CONTROL_ROT_TRACK {\n*CONTROL_ROT_SAMPLE 0 1 0 0 nan\n*CONTROL_ROT_SAMPLE 10 0 1 0 1e300\n}\n"));
        TryAse(anim("*CONTROL_SCALE_TRACK {\n*CONTROL_SCALE_SAMPLE 0 0 0 0\n*CONTROL_SCALE_SAMPLE 10 -1 -1 -1\n}\n"));
        TryAse(anim("*CONTROL_POS_BEZIER {\n*CONTROL_BEZIER_POS_KEY 0 1 2 3 4 5 6 7 8 9 1 1\n*CONTROL_BEZIER_POS_KEY 10 1 2 3\n}\n"));
        std::string many = "*CONTROL_POS_TRACK {\n";
        for (int i = 0; i < 200000; ++i) many += "*CONTROL_POS_SAMPLE " + std::to_string(i * 160) + " 1 2 3\n";
        many += "}\n";
        const auto t0 = std::chrono::steady_clock::now();
        TryAse(anim(many));
        CHECK(Secs(t0) < 20.0);
        // a TM_ANIMATION that does not close, key lines outside any track
        TryAse(head + "*GEOMOBJECT {\n*NODE_NAME \"a\"\n*MESH {\n" + Tri() + "}\n*TM_ANIMATION {\n*CONTROL_POS_SAMPLE 0 1 2 3\n");
        TryAse(head + "*CONTROL_POS_SAMPLE 0 1 2 3\n*MESH_VERTEX 0 1 2 3\n*MESH_FACE 0: A: 0 B: 1 C: 2\n");
    }
    // encodings: UTF-8 byte order mark, UTF-16 BE, a broken UTF-16, binary junk
    TryAse(std::string("\xEF\xBB\xBF") + head + Obj("a", kIdTm, Tri()));
    { std::string t = "\xFE\xFF"; for (char c : head + Obj("a", kIdTm, Tri())) { t += '\0'; t += c; } TryAse(t); }
    TryAse(std::string("\xFF\xFE\x00\xDC", 4));
    TryAse(std::string("\xFF\xFE\x2A", 3));
    { std::string t; for (int i = 0; i < 5000; ++i) t.push_back((char)(Rnd() & 255)); TryAse(t); TryAse(head + t); }
    { std::string t = head + Obj("a", kIdTm, Tri()); t.push_back('\0'); t += "}}}{{{"; TryAse(t); }
    TryAse(head + "*GEOMOBJECT\n{\n*NODE_NAME \"a\"\n}\n");                    // the brace on its own line
    TryAse(head + "*GEOMOBJECT {\n*NODE_NAME \"" + std::string(100000, 'q') + "\"\n}\n");
    TryAse(head + std::string(200000, '*'));
    TryAse(head + std::string(100000, '{'));

    // ---------------- mutation fuzz ----------------
    {
        const long iters = Iters(6000);
        const std::string alphabet = "0123456789.-+eE \n\r\t\"*{}:,ABCMESHTVNORWP_";
        for (long it = 0; it < iters; ++it) {
            std::string s = fixtures[Rnd() % 2];
            const int ops = 1 + (int)(Rnd() % 4);
            for (int o = 0; o < ops; ++o) MutateText(s, alphabet);
            TryAse(s);
        }
        std::printf("  fuzz: %ld iterations\n", iters);
    }
    TEST_MAIN_RESULT();
}
