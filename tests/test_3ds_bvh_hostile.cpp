// Hostile-input tests and a deterministic mutation fuzz for the own 3DS and BVH readers. Needs only
// okay/Render/ModelData.hpp, engine/src/Render/Max3dsReader.cpp and BvhReader.cpp, so it builds on its own:
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all
//       -Iengine/include -Itests -DOKAY_SOURCE_DIR='"."' tests/test_3ds_bvh_hostile.cpp
//       engine/src/Render/Max3dsReader.cpp engine/src/Render/BvhReader.cpp -o hostile
// OKAY_3DS_FUZZ_ITERS=200000 for a long run.
#include "test_framework.hpp"
#include "okay/Render/ModelData.hpp"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace okay;

static const std::string kDir = std::string(OKAY_SOURCE_DIR) + "/tests/data/models/";
static std::string Slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static bool Fin(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
// What the renderer and scene builder assume about any model a reader returns.
static bool Bad(int line) { std::fprintf(stderr, "  Sane failed at condition %d\n", line); return false; }
static bool Sane(const ImportedModel& m) {
    for (std::size_t i = 0; i < m.nodes.size(); ++i) {
        if (m.nodes[i].parent >= (int)i) return Bad(1);
        if (!Fin(m.nodes[i].position) || !Fin(m.nodes[i].scale)) return Bad(2);
        const Quat& q = m.nodes[i].rotation;
        if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w)) return Bad(3);
    }
    for (const ImportedPart& p : m.parts) {
        const Mesh& me = p.mesh;
        if (me.triangles.size() % 3) return Bad(4);
        for (int t : me.triangles) if (t < 0 || t >= (int)me.vertices.size()) return Bad(5);
        for (const Vec3& v : me.vertices) if (!Fin(v)) return Bad(6);
        for (const Vec3& v : me.normals) if (!Fin(v)) return Bad(7);
        if (!me.normals.empty() && me.normals.size() != me.vertices.size()) return Bad(8);
        if (!me.uvs.empty() && me.uvs.size() != me.vertices.size()) return Bad(9);
        if (p.material >= (int)m.materials.size()) return Bad(10);
        if (!m.nodes.empty() && (p.node < 0 || p.node >= (int)m.nodes.size())) return Bad(11);
    }
    for (const ImportedClip& c : m.clips)
        for (const ImportedChannel& ch : c.channels) {
            bool known = false;
            for (const ImportedNode& n : m.nodes) if (n.name == ch.node) { known = true; break; }
            if (!known) return Bad(12);
            for (const auto& k : ch.position) if (!std::isfinite(k.time) || k.time < 0 || !Fin(k.value)) return Bad(13);
            for (const auto& k : ch.scale) if (!std::isfinite(k.time) || k.time < 0 || !Fin(k.value)) return Bad(14);
            for (const auto& k : ch.rotation) if (!std::isfinite(k.time) || k.time < 0 || !std::isfinite(k.value.w)) return Bad(15);
        }
    return true;
}

static std::uint64_t g_state = 0x9E3779B97F4A7C15ull;
static std::uint64_t Rnd() { g_state ^= g_state << 13; g_state ^= g_state >> 7; g_state ^= g_state << 17; return g_state; }

// a successful read must be sane; a failed one must say why and leave nothing behind
static void Try3ds(const std::string& bytes, bool flatten = false) {
    ImportedModel m; std::string err;
    const bool ok = Read3dsBuffer(bytes, "", m, &err, flatten);
    if (ok) { CHECK(Sane(m)); }
    else { CHECK(!err.empty() && m.parts.empty() && m.nodes.empty()); }
}
static void TryBvh(const std::string& text) {
    ImportedModel m; std::string err;
    const bool ok = ReadBvhBuffer(text, m, &err);
    if (ok) { CHECK(Sane(m)); }
    else { CHECK(!err.empty() && m.nodes.empty()); }
}

// ---- little builders for hostile 3DS files ----
static void P16(std::string& s, unsigned v) { s.push_back((char)(v & 255)); s.push_back((char)(v >> 8)); }
static void P32(std::string& s, std::uint32_t v) { for (int i = 0; i < 4; ++i) s.push_back((char)((v >> (8 * i)) & 255)); }
static void PF(std::string& s, float f) { std::uint32_t u; std::memcpy(&u, &f, 4); P32(s, u); }
static std::string Chunk(unsigned id, const std::string& body) { std::string s; P16(s, id); P32(s, (std::uint32_t)(6 + body.size())); return s + body; }
static std::string Obj(const std::string& name, const std::string& tri) { return Chunk(0x4000, name + std::string(1, '\0') + Chunk(0x4100, tri)); }
static std::string Points(const std::vector<float>& xyz) { std::string b; P16(b, (unsigned)(xyz.size() / 3)); for (float f : xyz) PF(b, f); return Chunk(0x4110, b); }
static std::string Faces(const std::vector<unsigned>& idx, const std::string& sub = "") { std::string b; P16(b, (unsigned)(idx.size() / 3)); for (std::size_t i = 0; i < idx.size(); i += 3) { P16(b, idx[i]); P16(b, idx[i + 1]); P16(b, idx[i + 2]); P16(b, 7); } return Chunk(0x4120, b + sub); }
static std::string Main(const std::string& edit, const std::string& kf = "") {
    std::string body = Chunk(0x3D3D, edit);
    if (!kf.empty()) body += Chunk(0xB000, kf);
    return Chunk(0x4D4D, body);
}
static std::string Tri() { return Main(Obj("T", Points({0, 0, 0, 1, 0, 0, 0, 1, 0}) + Faces({0, 1, 2}))); }
static std::string KfNode(const std::string& name, unsigned id, unsigned parent, const std::string& tracks = "") {
    std::string h = name + std::string(1, '\0'); P16(h, 0); P16(h, 0); P16(h, parent);
    std::string idb; P16(idb, id);
    return Chunk(0xB002, Chunk(0xB010, h) + tracks + Chunk(0xB030, idb));
}
static std::string PosTrack(std::uint32_t claimedKeys, const std::string& keys) {
    std::string b; P16(b, 0); P32(b, 0); P32(b, 0); P32(b, claimedKeys); return Chunk(0xB020, b + keys);
}

int main() {
    RUN_SUITE("3ds_bvh_hostile");
    const std::string fixtures3ds[] = {Slurp(kDir + "cube.3ds"), Slurp(kDir + "box_flat.3ds"), Slurp(kDir + "hierarchy.3ds")};
    const std::string fixturesBvh[] = {Slurp(kDir + "walk.bvh"), Slurp(kDir + "walk_crlf.bvh"), Slurp(kDir + "two_roots.bvh")};
    for (const std::string& f : fixtures3ds) CHECK(!f.empty());
    for (const std::string& f : fixturesBvh) CHECK(!f.empty());

    // ---------------- every truncation of every fixture ----------------
    for (const std::string& f : fixtures3ds)
        for (std::size_t cut = 0; cut < f.size(); ++cut) { Try3ds(f.substr(0, cut)); if (cut % 7 == 0) Try3ds(f.substr(0, cut), true); }
    for (const std::string& f : fixturesBvh)
        for (std::size_t cut = 0; cut < f.size(); ++cut) TryBvh(f.substr(0, cut));

    // ---------------- 3DS: hostile structure ----------------
    {
        Try3ds(std::string());
        Try3ds(std::string("MM"));
        { std::string s; P16(s, 0x4D4D); P32(s, 0xFFFFFFFFu); Try3ds(s); }                         // main chunk longer than the file
        { std::string s; P16(s, 0x4D4D); P32(s, 5); Try3ds(s); }                                   // length below the header size
        { std::string s; P16(s, 0x4D4D); P32(s, 0); Try3ds(s); }
        // 5000 chunks nested inside each other: the depth cap, not the stack, ends it
        {
            std::string inner = Chunk(0x1234, "");
            for (int i = 0; i < 5000; ++i) inner = Chunk(0x3D3D, inner);
            ImportedModel m; std::string err;
            CHECK(!Read3dsBuffer(Chunk(0x4D4D, inner), "", m, &err));
            CHECK(err.find("nested") != std::string::npos);
        }
        // point array claims more than it holds
        { std::string b; P16(b, 65535); Try3ds(Main(Obj("A", Chunk(0x4110, b)))); }
        // face array claims more than it holds
        { std::string b; P16(b, 65535); Try3ds(Main(Obj("A", Points({0, 0, 0}) + Chunk(0x4120, b)))); }
        // face indices outside the point array
        { Try3ds(Main(Obj("A", Points({0, 0, 0, 1, 0, 0, 0, 1, 0}) + Faces({0, 1, 9, 65535, 0, 1})))); }
        // degenerate faces, a face using one point three times
        { Try3ds(Main(Obj("A", Points({0, 0, 0, 1, 0, 0, 0, 1, 0}) + Faces({0, 0, 0, 0, 1, 1, 0, 1, 2})))); }
        // non-finite points are refused
        { ImportedModel m; std::string err; const float nanf = std::nanf(""), inf = INFINITY;
          CHECK(!Read3dsBuffer(Main(Obj("A", Points({nanf, 0, 0, 1, 0, 0, 0, 1, 0}) + Faces({0, 1, 2}))), "", m, &err));
          CHECK(!Read3dsBuffer(Main(Obj("A", Points({inf, 0, 0, 1, 0, 0, 0, 1, 0}) + Faces({0, 1, 2}))), "", m, &err)); }
        // huge coordinates stay finite through the pipeline or are rejected cleanly
        { Try3ds(Main(Obj("A", Points({1e38f, 1e38f, 1e38f, -1e38f, 0, 0, 0, 1e38f, 0}) + Faces({0, 1, 2})))); Try3ds(Main(Obj("A", Points({1e38f, 1e38f, 1e38f, -1e38f, 0, 0, 0, 1e38f, 0}) + Faces({0, 1, 2}))), true); }
        // master scale: zero, negative, huge, NaN are ignored
        for (float s : {0.0f, -1.0f, 1e30f, std::nanf("")}) { std::string b; PF(b, s); Try3ds(Main(Chunk(0x0100, b) + Obj("A", Points({0, 0, 0, 1, 0, 0, 0, 1, 0}) + Faces({0, 1, 2})))); }
        // material group lists faces out of range and a material that does not exist
        { std::string g = std::string("NoSuch") + std::string(1, '\0'); P16(g, 3); P16(g, 0); P16(g, 7); P16(g, 65535);
          Try3ds(Main(Obj("A", Points({0, 0, 0, 1, 0, 0, 0, 1, 0}) + Faces({0, 1, 2}, Chunk(0x4130, g))))); }
        // a material group with an unterminated name and a count that does not fit
        { std::string g(40, 'x'); Try3ds(Main(Obj("A", Points({0, 0, 0, 1, 0, 0, 0, 1, 0}) + Faces({0, 1, 2}, Chunk(0x4130, g))))); }
        { std::string g = "M"; g.push_back('\0'); P16(g, 60000); Try3ds(Main(Obj("A", Points({0, 0, 0, 1, 0, 0, 0, 1, 0}) + Faces({0, 1, 2}, Chunk(0x4130, g))))); }
        // smoothing groups: shorter than the faces, and one vertex shared by many faces with distinct groups
        { std::string s; P32(s, 1); Try3ds(Main(Obj("A", Points({0, 0, 0, 1, 0, 0, 0, 1, 0}) + Faces({0, 1, 2, 0, 1, 2}, Chunk(0x4150, s))))); }
        {
            // a fan of 3000 triangles around point 0, every face in its own smoothing group
            std::vector<float> pts{0, 0, 0}; std::vector<unsigned> idx; std::string sg;
            const int N = 3000;
            for (int i = 0; i <= N; ++i) { const float a = (float)i / N * 6.2831853f; pts.push_back(std::cos(a)); pts.push_back(std::sin(a)); pts.push_back(0); }
            for (int i = 0; i < N; ++i) { idx.push_back(0); idx.push_back((unsigned)i + 1); idx.push_back((unsigned)i + 2); P32(sg, 1u << (i % 32) | (std::uint32_t)i << 5); }
            ImportedModel m; std::string err;
            const auto t0 = std::chrono::steady_clock::now();
            CHECK(Read3dsBuffer(Main(Obj("Fan", Points(pts) + Faces(idx, Chunk(0x4150, sg)))), "", m, &err));
            const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            CHECK(dt < 10.0);
            CHECK(Sane(m));
        }
        // 65535 faces on one point: linear time, bounded memory
        {
            std::vector<float> pts{0, 0, 0, 1, 0, 0, 0, 1, 0}; std::vector<unsigned> idx;
            for (int i = 0; i < 65535; ++i) { idx.push_back(0); idx.push_back(1); idx.push_back(2); }
            ImportedModel m; std::string err;
            const auto t0 = std::chrono::steady_clock::now();
            Read3dsBuffer(Main(Obj("Same", Points(pts) + Faces(idx))), "", m, &err);
            CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 10.0);
        }
        // many objects
        { std::string e; for (int i = 0; i < 3000; ++i) e += Obj("O" + std::to_string(i), Points({0, 0, 0, 1, 0, 0, 0, 1, 0}) + Faces({0, 1, 2})); ImportedModel m; std::string err; CHECK(Read3dsBuffer(Main(e), "", m, &err)); CHECK(m.parts.size() == 3000); CHECK(Sane(m)); }
        // too many materials
        { std::string e; for (int i = 0; i < 100001; ++i) e += Chunk(0xAFFF, ""); ImportedModel m; std::string err; CHECK(!Read3dsBuffer(Main(e), "", m, &err)); }
        // keyframes: cyclic parents, a self parent, a parent id that does not exist, many nodes, huge key counts, nodes with no mesh
        {
            const std::string tri = Obj("A", Points({0, 0, 0, 1, 0, 0, 0, 1, 0}) + Faces({0, 1, 2}));
            Try3ds(Main(tri, KfNode("A", 0, 1) + KfNode("B", 1, 0)));                         // a <-> b
            Try3ds(Main(tri, KfNode("A", 0, 0)));                                              // itself
            Try3ds(Main(tri, KfNode("A", 0, 77)));                                             // missing parent
            Try3ds(Main(tri, KfNode("A", 5, 5) + KfNode("A", 5, 5)));                          // same id twice
            std::string many; for (int i = 0; i < 2000; ++i) many += KfNode("A", (unsigned)i, i ? (unsigned)(i - 1) : 0xFFFF);   // a 2000 deep chain
            Try3ds(Main(tri, many));
            Try3ds(Main(tri, KfNode("A", 0, 0xFFFF, PosTrack(0xFFFFFFFFu, ""))));              // a billion keys, no data
            std::string key; P32(key, 5); P16(key, 0x1F); for (int i = 0; i < 5; ++i) PF(key, 1.0f); PF(key, 1); PF(key, 2); PF(key, 3);
            Try3ds(Main(tri, KfNode("A", 0, 0xFFFF, PosTrack(1, key))));
            Try3ds(Main(tri, KfNode("A", 0, 0xFFFF, PosTrack(9, key))));                       // claims more than it holds
            std::string nanKey; P32(nanKey, 0); P16(nanKey, 0); PF(nanKey, std::nanf("")); PF(nanKey, INFINITY); PF(nanKey, 1);
            Try3ds(Main(tri, KfNode("A", 0, 0xFFFF, PosTrack(1, nanKey))));
            // frames far apart / out of order
            std::string two; P32(two, 4000000000u); P16(two, 0); PF(two, 0); PF(two, 0); PF(two, 0); P32(two, 1); P16(two, 0); PF(two, 0); PF(two, 0); PF(two, 0);
            Try3ds(Main(tri, KfNode("A", 0, 0xFFFF, PosTrack(2, two))));
            // a zero scale track makes the rest transform singular
            std::string sc; P32(sc, 0); P16(sc, 0); PF(sc, 0); PF(sc, 0); PF(sc, 0);
            std::string st; P16(st, 0); P32(st, 0); P32(st, 0); P32(st, 1);
            Try3ds(Main(tri, KfNode("A", 0, 0xFFFF, Chunk(0xB022, st + sc))));
        }
        // texture names: no NUL, very long, path tricks (kept as plain strings)
        { std::string m = Chunk(0xA000, "M"); m[m.size() - 1] = 'M'; std::string tex = Chunk(0xA200, Chunk(0xA300, std::string(5000, 'a')));
          Try3ds(Main(Chunk(0xAFFF, Chunk(0xA000, "M") + tex) + Obj("A", Points({0, 0, 0, 1, 0, 0, 0, 1, 0}) + Faces({0, 1, 2})))); }
        { std::string tex = Chunk(0xA200, Chunk(0xA300, std::string("..\\..\\etc\\passwd\0", 17)));
          Try3ds(Main(Chunk(0xAFFF, Chunk(0xA000, std::string("M\0", 2)) + tex) + Obj("A", Points({0, 0, 0, 1, 0, 0, 0, 1, 0}) + Faces({0, 1, 2})))); }
    }

    // ---------------- BVH: hostile structure ----------------
    {
        const std::string head = "HIERARCHY\nROOT A\n{\nOFFSET 0 0 0\nCHANNELS 3 Xrotation Yrotation Zrotation\n}\nMOTION\n";
        TryBvh(head + "Frames: 1\nFrame Time: 1\n1 2 3\n");
        TryBvh(head + "Frames: 1000000\nFrame Time: 1\n1 2 3\n");                               // claims a million frames
        TryBvh(head + "Frames: 1000001\nFrame Time: 1\n1 2 3\n");
        TryBvh(head + "Frames: -1\nFrame Time: 1\n");
        TryBvh(head + "Frames: 1.5\nFrame Time: 1\n1 2 3\n");
        TryBvh(head + "Frames: 1\nFrame Time: -1\n1 2 3\n");
        TryBvh(head + "Frames: 1\nFrame Time: 1e400\n1 2 3\n");
        TryBvh(head + "Frames: 1\nFrame Time: 1\n1e999 2 3\n");
        TryBvh(head + "Frames: 1\nFrame Time: 1\n1 2 3 4 5 6 trailing\n");
        TryBvh(head + "Frames: 1\nFrame Time: 1\n0x10 2 3\n");
        TryBvh(head + "Frames: 1\nFrame Time: 1\n1 2 3x\n");
        TryBvh(head + std::string("Frames: 1\nFrame Time: 1\n1 2\0 3\n", 31));                    // NUL in the data
        TryBvh(head);
        TryBvh("HIERARCHY\nROOT A\n{\nOFFSET 0 0 0\nCHANNELS 25 " + std::string(25 * 10, ' ') + "\n}\nMOTION\n");
        { std::string deep = "HIERARCHY\nROOT A\n{\nOFFSET 0 0 0\nCHANNELS 0\n"; for (int i = 0; i < 100000; ++i) deep += "JOINT J\n{\nOFFSET 0 0 0\nCHANNELS 0\n"; TryBvh(deep + "}\nMOTION\nFrames: 0\nFrame Time: 1\n");
          ImportedModel m; std::string err; CHECK(!ReadBvhBuffer(deep + "MOTION\n", m, &err)); }
        { std::string wide = "HIERARCHY\nROOT A\n{\nOFFSET 0 0 0\nCHANNELS 0\n"; for (int i = 0; i < 20000; ++i) wide += "JOINT J\n{\nOFFSET 1 1 1\nCHANNELS 0\n}\n"; wide += "}\nMOTION\nFrames: 0\nFrame Time: 1\n";
          ImportedModel m; std::string err; CHECK(!ReadBvhBuffer(wide, m, &err) && err.find("too many") != std::string::npos); }
        { std::string wide = "HIERARCHY\nROOT A\n{\nOFFSET 0 0 0\nCHANNELS 0\n"; for (int i = 0; i < 9000; ++i) wide += "JOINT J\n{\nOFFSET 1 1 1\nCHANNELS 6 Xposition Yposition Zposition Xrotation Yrotation Zrotation\n}\n"; wide += "}\nMOTION\nFrames: 1\nFrame Time: 1\n";
          for (int i = 0; i < 9000 * 6; ++i) { wide += "1 "; }
          ImportedModel m; std::string err; CHECK(ReadBvhBuffer(wide, m, &err)); CHECK(Sane(m)); CHECK(m.nodes.size() == 9001); }
        // frames x channels over the sample cap, with a long enough text that the size check alone would pass
        { std::string t = "HIERARCHY\nROOT A\n{\nOFFSET 0 0 0\nCHANNELS 6 Xposition Yposition Zposition Xrotation Yrotation Zrotation\n}\nMOTION\nFrames: 1000000\nFrame Time: 1\n";
          ImportedModel m; std::string err; CHECK(!ReadBvhBuffer(t, m, &err)); }
        // names: empty, only a brace, huge
        TryBvh("HIERARCHY\nROOT\n{\n}\nMOTION\nFrames: 0\nFrame Time: 1\n");
        TryBvh("HIERARCHY\nROOT {\n}\nMOTION\nFrames: 0\nFrame Time: 1\n");
        TryBvh("HIERARCHY\nROOT " + std::string(100000, 'n') + "\n{\nOFFSET 0 0 0\n}\nMOTION\nFrames: 0\nFrame Time: 1\n");
        TryBvh("HIERARCHY\nROOT A\n{\nOFFSET nan 0 0\n}\nMOTION\nFrames: 0\nFrame Time: 1\n");
        TryBvh("HIERARCHY\nROOT A\n{\nOFFSET 1e39 0 0\n}\nMOTION\nFrames: 0\nFrame Time: 1\n");
        TryBvh("HIERARCHY\nROOT A\n{\nOFFSET 0 0 0\nEnd Site\n{\nOFFSET 0 0 0\n}\nEnd Site\n{\n}\n}\nMOTION\nFrames: 0\nFrame Time: 1\n");
    }

    // ---------------- mutation fuzz ----------------
    {
        long iters = 6000;
        if (const char* e = std::getenv("OKAY_3DS_FUZZ_ITERS")) iters = std::atol(e);
        const std::string alphabet = "0123456789.- \n\r\t{}ABCDEFXYZ:jJ";
        for (long it = 0; it < iters; ++it) {
            {   // 3DS: byte flips, chunk-length stomps, inserts, deletes, splices
                std::string s = fixtures3ds[Rnd() % 3];
                const int ops = 1 + (int)(Rnd() % 4);
                for (int o = 0; o < ops && !s.empty(); ++o) {
                    const std::size_t at = Rnd() % s.size();
                    switch (Rnd() % 6) {
                    case 0: s[at] = (char)(Rnd() & 255); break;
                    case 1: s[at] = (char)(s[at] ^ (1 << (Rnd() % 8))); break;
                    case 2: if (at + 4 <= s.size()) { const std::uint32_t v = Rnd() % 3 == 0 ? 0xFFFFFFFFu : (std::uint32_t)(Rnd() % 300); std::memcpy(&s[at], &v, 4); } break;
                    case 3: s.insert(at, std::string(1 + Rnd() % 8, (char)(Rnd() & 255))); break;
                    case 4: s.erase(at, 1 + Rnd() % 16); break;
                    default: if (at + 2 <= s.size()) { const std::uint16_t v = (std::uint16_t)(Rnd() % 4 == 0 ? 0xFFFF : Rnd() % 100); std::memcpy(&s[at], &v, 2); } break;
                    }
                }
                Try3ds(s, it % 5 == 0);
            }
            {   // BVH
                std::string s = fixturesBvh[Rnd() % 3];
                const int ops = 1 + (int)(Rnd() % 4);
                for (int o = 0; o < ops && !s.empty(); ++o) {
                    const std::size_t at = Rnd() % s.size();
                    switch (Rnd() % 4) {
                    case 0: s[at] = alphabet[Rnd() % alphabet.size()]; break;
                    case 1: s.insert(at, std::string(1 + Rnd() % 6, alphabet[Rnd() % alphabet.size()])); break;
                    case 2: s.erase(at, 1 + Rnd() % 12); break;
                    default: s[at] = (char)(Rnd() & 255); break;
                    }
                }
                TryBvh(s);
            }
        }
        std::printf("  fuzz: %ld iterations each\n", iters);
    }

    TEST_MAIN_RESULT();
}
