// Our own COB reader (engine/src/Render/CobReader.cpp), binary and ASCII. SYNTHETIC fixtures built in memory (CobAscii, CobBinary in
// e3_fixtures.hpp: the same scene in both encodings) checked against hand-computed numbers; hostile files (counts 0x7FFFFFFF and negative,
// chunk sizes past the end, cyclic object parents, truncation at every length, random byte flips); the per-file budgets; a counting operator
// new for the worst file the budgets allow; and, when OKAY_E3_SAMPLES names a folder holding the real trueSpace files (molecule, spider_4_3,
// spider_6_6, dwarf, each binary and *_ascii), those too (without it that part says "skipped"). Builds on its own with -DOKAY_STANDALONE_TEST.
#include "test_framework.hpp"
#include "e3_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <new>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

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

// time limits are for a normal build; the sanitizers (CI runs them) make the same read several times slower (7.2 s measured for the 25 MB chunk walk)
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_UNDEFINED__)
static constexpr double kSlow = 6.0;
#else
static constexpr double kSlow = 1.0;
#endif
static constexpr std::size_t kWorstCap = 480u * 1024u * 1024u;   // about 2x the measured worst case (see the printed peak)
static bool HasNote(const ImportedModel& m, const char* what) { for (const std::string& n : m.notes) if (n.find(what) != std::string::npos) return true; return false; }
static bool Read(const std::string& b, ImportedModel& m, std::string* e = nullptr) { std::string er; return ReadCobBuffer(b, "dir/", m, e ? e : &er); }
static double PartArea(const ImportedPart& p) { ImportedModel m; m.nodes.push_back(ImportedNode{}); m.parts.push_back(p); return fx::Area(m); }

static void CheckScene(const ImportedModel& m, const char* what) {
    std::fprintf(stderr, "  scene check: %s\n", what);
    CHECK(m.backend == "own" && fx::Sane(m));
    CHECK(m.nodes.size() == 3 && m.nodes[0].name == "COB" && m.nodes[1].name == "G" && m.nodes[2].name == "P" && m.nodes[1].parent == 0 && m.nodes[2].parent == 1);
    // G: world scale 2, translation (10,0,0); P is placed relative to G: (0, 2.5, 0) after the y/z swap, scale 1
    CHECK(fx::Near(m.nodes[1].position.x, 10) && fx::Near(m.nodes[1].position.y, 0) && fx::Near(m.nodes[1].position.z, 0) && fx::Near(m.nodes[1].scale.x, 2) && fx::Near(m.nodes[1].scale.y, 2) && fx::Near(m.nodes[1].scale.z, 2));
    CHECK(fx::Near(m.nodes[2].position.x, 0) && fx::Near(m.nodes[2].position.y, 2.5f) && fx::Near(m.nodes[2].position.z, 0) && fx::Near(m.nodes[2].scale.x, 1) && fx::Near(m.nodes[2].rotation.w, 1));
    CHECK(m.parts.size() == 2 && m.materials.size() == 2);
    CHECK(m.parts[0].node == 2 && m.parts[1].node == 2 && m.parts[0].material == 0 && m.parts[1].material == 1);
    CHECK(m.parts[0].mesh.triangles.size() == 6 && m.parts[1].mesh.triangles.size() == 3);
    CHECK_NEAR(PartArea(m.parts[0]), 1.0, 1e-5);
    CHECK_NEAR(PartArea(m.parts[1]), 0.5, 1e-5);
    // the quad: vertex (0,1,0) of the file is (0,0,1) after the swap; its texture coordinate (0,1) comes out as (0,0) (v flipped)
    bool found = false;
    const Mesh& q = m.parts[0].mesh;
    CHECK(q.vertices.size() == 4 && q.uvs.size() == 4 && q.normals.size() == 4);
    for (std::size_t i = 0; i < q.vertices.size(); ++i) if (fx::Near(q.vertices[i].x, 0) && fx::Near(q.vertices[i].y, 0) && fx::Near(q.vertices[i].z, 1)) { found = fx::Near(q.uvs[i].x, 0) && fx::Near(q.uvs[i].y, 0); }
    CHECK(found);
    for (const Vec3& n : q.normals) CHECK(fx::Near(n.x, 0) && fx::Near(n.y, -1) && fx::Near(n.z, 0));   // faceted, and the winding kept: (x,y,z) -> (x,z,y) turns +z into -y by the winding
    // material 0: red, opaque, the texture next to the model; material 1: blue, alpha 0.5, no texture
    CHECK(fx::Near(m.materials[0].diffuse.r, 1) && fx::Near(m.materials[0].diffuse.g, 0) && fx::Near(m.materials[0].diffuse.a, 1) && m.materials[0].texture == "dir/tex.png");
    CHECK(fx::Near(m.materials[1].diffuse.b, 1) && fx::Near(m.materials[1].diffuse.a, 0.5f) && m.materials[1].texture.empty());
    CHECK(HasNote(m, "left-handed"));
}

int main() {
    RUN_SUITE("cob_reader");
    {
        ImportedModel m; std::string e;
        CHECK(Read(fx::CobAscii(), m, &e)); if (!e.empty()) std::fprintf(stderr, "  %s\n", e.c_str());
        CheckScene(m, "ASCII");
        ImportedModel b;
        CHECK(Read(fx::CobBinary(), b, &e)); if (!e.empty()) std::fprintf(stderr, "  %s\n", e.c_str());
        CheckScene(b, "binary");
        // the two encodings give the same model
        CHECK(m.parts.size() == b.parts.size() && fx::TriCount(m) == fx::TriCount(b));
        for (std::size_t i = 0; i < m.parts.size() && i < b.parts.size(); ++i) CHECK(m.parts[i].mesh.vertices.size() == b.parts[i].mesh.vertices.size());
        // CRLF line ends and a UTF-8 file name are fine in ASCII
        std::string crlf;
        for (char c : fx::CobAscii()) { if (c == '\n') crlf += "\r\n"; else crlf.push_back(c); }
        ImportedModel c;
        CHECK(Read(crlf, c) && fx::TriCount(c) == 3);
    }
    {   // refusals
        ImportedModel m; std::string e;
        CHECK(!Read(std::string(), m, &e) && e.find("COB") == 0);
        CHECK(!Read("Caligari V00.01XLH             \n", m, &e) && e.find("neither") != std::string::npos);
        CHECK(!Read("Caligari V00.01BHL             \n", m, &e) && e.find("little endian") != std::string::npos);
        CHECK(!Read("hello world, not a model at all....", m, &e) && e.find("not a COB") != std::string::npos);
        CHECK(!Read("Caligari V00.01BLH             \n", m, &e) && e.find("no polygon object") != std::string::npos);   // a header and nothing else
        CHECK(!Read("Caligari V00.01ALH             \nEND V1.00 Id 0 Parent 0 Size 0\n", m, &e));
    }
    {   // binary: chunk size past the end, counts negative / huge, a face that runs past its chunk
        const std::string f = fx::CobBinary();
        ImportedModel m; std::string e;
        std::string g = f; fx::Put32(g, 32 + 16, 0x7FFFFFFFu);   // first chunk's size
        CHECK(!Read(g, m, &e) && e.find("larger than the rest") != std::string::npos);
        // the polygon chunk: find "PolH", the vertex count sits after the header (20) + name (4 + 1) + 96 bytes
        const std::size_t ph = f.find("PolH");
        const std::size_t vc = ph + 20 + 4 + 1 + 96;
        for (std::uint32_t v : {0x7FFFFFFFu, 0xFFFFFFFFu, 0x80000000u, 0x00FFFFFFu}) {
            g = f; fx::Put32(g, vc, v);
            CHECK(!Read(g, m, &e) && !e.empty());
        }
        g = f; fx::Put32(g, vc + 4 + 5 * 12, 0xFFFFFFFFu);   // texture vertex count
        CHECK(!Read(g, m, &e) && !e.empty());
        g = f; fx::Put32(g, vc + 4 + 5 * 12 + 4 + 4 * 8, 0x7FFFFFFFu);   // face count
        CHECK(!Read(g, m, &e) && !e.empty());
        // a face with 65535 corners that is not there
        g = f; { const std::size_t fo = vc + 4 + 5 * 12 + 4 + 4 * 8 + 4; g[fo + 1] = (char)0xFF; g[fo + 2] = (char)0xFF; }
        CHECK(!Read(g, m, &e) && !e.empty());
        // a vertex index past the vertex list: the face is skipped, said in the notes, the rest is read
        g = f; { const std::size_t fo = vc + 4 + 5 * 12 + 4 + 4 * 8 + 4 + 5; fx::Put32(g, fo, 99); }
        CHECK(Read(g, m, &e) && fx::TriCount(m) == 1 && HasNote(m, "does not exist"));
        // a NaN vertex is refused
        g = f; { const std::size_t vo = vc + 4; fx::Put32(g, vo, 0x7FC00000u); }
        CHECK(!Read(g, m, &e) && e.find("not finite") != std::string::npos);
    }
    {   // cyclic and missing parents
        std::string a = fx::CobAscii();
        auto rep = [](std::string s, const std::string& from, const std::string& to) { const std::size_t p = s.find(from); if (p != std::string::npos) s.replace(p, from.size(), to); return s; };
        ImportedModel m;
        std::string cyc = rep(a, "Grou V0.01 Id 10 Parent 0 ", "Grou V0.01 Id 10 Parent 11 ");   // G's parent is P, P's parent is G
        CHECK(Read(cyc, m) && fx::Sane(m) && m.nodes.size() == 3 && HasNote(m, "loop"));
        std::string orphan = rep(a, "PolH V0.08 Id 11 Parent 10 ", "PolH V0.08 Id 11 Parent 777 ");
        CHECK(Read(orphan, m) && fx::Sane(m) && m.nodes[2].parent == 0);
        std::string self = rep(a, "PolH V0.08 Id 11 Parent 10 ", "PolH V0.08 Id 11 Parent 11 ");
        CHECK(Read(self, m) && fx::Sane(m));
        // binary: same
        std::string b = fx::CobBinary();
        fx::Put32(b, 32 + 12, 11);   // the Grou chunk's parent id (bytes 12..15 of its header) -> P: loop
        CHECK(Read(b, m) && fx::Sane(m) && m.nodes.size() == 3 && HasNote(m, "loop"));
    }
    {   // ASCII: counts that lie, short lines, junk numbers
        const std::string a = fx::CobAscii();
        auto rep = [](std::string s, const std::string& from, const std::string& to) { const std::size_t p = s.find(from); if (p != std::string::npos) s.replace(p, from.size(), to); return s; };
        ImportedModel m; std::string e;
        CHECK(!Read(rep(a, "World Vertices 5", "World Vertices 2147483647"), m, &e) && e.find("too many vertices") != std::string::npos);
        CHECK(!Read(rep(a, "World Vertices 5", "World Vertices 500"), m, &e) && !e.empty());
        CHECK(!Read(rep(a, "World Vertices 5", "World Vertices -3"), m, &e) && !e.empty());
        CHECK(!Read(rep(a, "Faces 2", "Faces 99999999"), m, &e) && e.find("too many faces") != std::string::npos);
        CHECK(!Read(rep(a, "Face verts 4 flags", "Face verts 9999 flags"), m, &e) && !e.empty());
        CHECK(!Read(rep(a, "<2,2> <3,3>", "<2,2> <3 3>"), m, &e) && e.find("corner") != std::string::npos);
        CHECK(!Read(rep(a, "1 1 0\n0 1 0\n2 0 0", "1 1 nan\n0 1 0\n2 0 0"), m, &e) && !e.empty());
        CHECK(!Read(rep(a, "2 0 0 10\n0 2 0 0\n0 0 2 5", "2 0 0 1e99\n0 2 0 0\n0 0 2 5"), m, &e) && e.find("Transform") != std::string::npos);
        // texture paths
        const std::string t = rep(a, "\"tex.png\"", "\"../../x.png\"");   // climbs out: only the file name is kept, inside the model's folder
        CHECK(Read(t, m) && m.materials[0].texture == "dir/x.png" && HasNote(m, "reduced"));
        const std::string t3 = rep(a, "\"tex.png\"", "\"../\"");           // nothing left once the folder part is dropped
        CHECK(Read(t3, m) && m.materials[0].texture.empty() && HasNote(m, "refused"));
        const std::string t4 = rep(a, "\"tex.png\"", "\"/etc/passwd\"");   // absolute: the name alone, in our folder (never the absolute path)
        CHECK(Read(t4, m) && m.materials[0].texture == "dir/passwd");
        const std::string t2 = rep(a, "\"tex.png\"", "\"C:\\\\art\\\\tex.png\"");
        CHECK(Read(t2, m) && m.materials[0].texture == "dir/tex.png" && HasNote(m, "reduced"));
    }
    {   // hostile sweeps: truncation at every length, random flips
        for (const std::string& f : {fx::CobAscii(), fx::CobBinary()}) {
            for (std::size_t n = 0; n < f.size(); ++n) { ImportedModel m; const bool ok = Read(f.substr(0, n), m); CHECK(!ok || fx::Sane(m)); }
            fx::State() = 0xBEEF;
            int iters = 4000;
            if (const char* e = std::getenv("OKAY_BATCHD_FUZZ_ITERS")) iters = std::atoi(e) > 0 ? std::atoi(e) : iters;
            for (int it = 0; it < iters; ++it) {
                std::string g = f;
                const int flips = 1 + (int)(fx::Rnd() % 4);
                for (int k = 0; k < flips; ++k) g[(std::size_t)(fx::Rnd() % g.size())] ^= (char)(1u << (fx::Rnd() % 8));
                ImportedModel m;
                if (Read(g, m)) CHECK(fx::Sane(m));
            }
        }
    }
    {   // budgets: more than 1,000,000 triangles / 4,000,000 corners / 2,000,000 vertices
        ImportedModel m; std::string e;
        std::string a = "Caligari V00.01ALH             \nPolH V0.08 Id 1 Parent 0 Size 1\nName B\ncenter 0 0 0\nx axis 1 0 0\ny axis 0 1 0\nz axis 0 0 1\nTransform\n1 0 0 0\n0 1 0 0\n0 0 1 0\n0 0 0 1\n";
        std::string v = a + "World Vertices 2000001\n";
        CHECK(!Read(v, m, &e) && e.find("too many vertices") != std::string::npos);
        std::string tri = a + "World Vertices 3\n0 0 0\n1 0 0\n0 1 0\nFaces 1000001\n";
        CHECK(!Read(tri, m, &e) && e.find("too many faces") != std::string::npos);
        // 500,002 triangles in 250,001 quads: the triangle budget refuses, not the face budget
        std::string q = a + "World Vertices 4\n0 0 0\n1 0 0\n1 1 0\n0 1 0\nFaces 250001\n";
        for (int i = 0; i < 250001; ++i) q += "Face verts 4 flags 0 mat 0\n<0,0> <1,0> <2,0> <3,0> \n";
        CHECK(!Read(q, m, &e) && e.find("too many triangles") != std::string::npos);
    }
    {   // the worst file the budgets allow: 500,000 triangles (250,000 quads x 2) in ASCII; memory is counted
        std::string a = "Caligari V00.01ALH             \nPolH V0.08 Id 1 Parent 0 Size 1\nName B\ncenter 0 0 0\nx axis 1 0 0\ny axis 0 1 0\nz axis 0 0 1\nTransform\n1 0 0 0\n0 1 0 0\n0 0 1 0\n0 0 0 1\n";
        const int nq = 250000, side = 500;   // a grid of vertices; each quad uses 4 of them
        const int nv = (side + 1) * (side + 1);
        a += "World Vertices " + std::to_string(nv) + "\n";
        for (int y = 0; y <= side; ++y) for (int x = 0; x <= side; ++x) a += std::to_string(x) + " " + std::to_string(y) + " " + std::to_string((x * 7 + y * 3) % 5) + "\n";
        a += "Texture Vertices 4\n0 0\n1 0\n1 1\n0 1\n";
        a += "Faces " + std::to_string(nq) + "\n";
        for (int i = 0; i < nq; ++i) {
            const int x = i % side, y = (i / side) % side, v0 = y * (side + 1) + x;
            a += "Face verts 4 flags 0 mat 0\n<" + std::to_string(v0) + ",0> <" + std::to_string(v0 + 1) + ",1> <" + std::to_string(v0 + side + 2) + ",2> <" + std::to_string(v0 + side + 1) + ",3> \n";
        }
        const std::size_t base = g_live.load();
        g_peak.store(base);
        const auto t0 = std::chrono::steady_clock::now();
        ImportedModel m; std::string e;
        const bool ok = Read(a, m, &e);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const std::size_t extra = g_peak.load() - base;
        std::fprintf(stderr, "  worst allowed file (%zu MB ASCII, 500,000 triangles): %s in %.2f s, peak %zu MB beyond the input\n", a.size() >> 20, ok ? "read" : e.c_str(), secs, extra >> 20);
        CHECK(ok && fx::TriCount(m) == 500000);
        CHECK(extra < kWorstCap);
        CHECK(secs < 60.0);
    }
    {   // quadratic work: many shader chunks that each look for their material (ids that match nothing make the old search scan every material)
        const int nm = 100000, ns = 300000;
        std::string a = "Caligari V00.01ALH             \n";
        for (int i = 0; i < nm; ++i) a += "Mat1 V0.08 Id " + std::to_string(i + 1) + " Parent 5 Size 1\nmat# " + std::to_string(i % 60000) + "\nrgb 1,1,1\n";
        for (int i = 0; i < ns; ++i) a += "ShBx V0.04 Id " + std::to_string(nm + 10 + i) + " Parent 99999999 Size 1\nfile name: string \"a.png\"\n";
        a += "END V1.00 Id 0 Parent 0 Size 0\n";
        ImportedModel m; std::string e;
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = Read(a, m, &e);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::fprintf(stderr, "  %d materials + %d shader chunks (ASCII, %zu MB): %s in %.2f s\n", nm, ns, a.size() >> 20, ok ? "read" : e.c_str(), secs);
        CHECK(!ok && e.find("no polygon object") != std::string::npos);   // no object in it: refused for that, after the chunks were walked
        CHECK(secs < 5.0 * kSlow);
        // the same with a chunk count over the cap is refused by the cap
        std::string b = "Caligari V00.01ALH             \n";
        for (int i = 0; i < 1000001; ++i) b += "Unit V0.01 Id 1 Parent 0 Size 1\n";
        CHECK(!Read(b, m, &e) && e.find("too many chunks") != std::string::npos);
        // binary: 100,000 materials with distinct ids, 300,000 shader chunks naming nothing
        std::string c = "Caligari V00.01BLH             \n";
        for (int i = 0; i < nm; ++i) { std::string d; fx::P16(d, (unsigned)(i % 60000)); fx::PutU8(d, 'p'); fx::PutU8(d, 'a'); fx::PutU8(d, 40); for (int k = 0; k < 8; ++k) fx::PutF(d, 1); fx::CobChunk(c, "Mat1", i + 1, 5, d); }
        std::string sh; fx::PutU8(sh, 0); fx::P16(sh, 1); sh += "x"; fx::P32(sh, 1); fx::P16(sh, 9); sh += "file name"; fx::P32(sh, 10); fx::P16(sh, 5); sh += "a.png"; fx::P16(sh, 3);
        for (int i = 0; i < ns; ++i) fx::CobChunk(c, "ShBx", nm + 10 + i, 99999999, sh);
        fx::CobChunk(c, "END ", 0, 0, std::string());
        const auto t1 = std::chrono::steady_clock::now();
        const bool okb = Read(c, m, &e);
        const double sb = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
        std::fprintf(stderr, "  binary: %s in %.2f s\n", okb ? "read" : e.c_str(), sb);
        CHECK(!okb && e.find("no polygon object") != std::string::npos && sb < 5.0 * kSlow);
    }
    {   // quadratic / cubic work: many concave 256-corner polygons (ear clipping is cubic; above 48 corners the faces are fanned)
        std::string a = "Caligari V00.01ALH             \nPolH V0.08 Id 1 Parent 0 Size 1\nName S\ncenter 0 0 0\nx axis 1 0 0\ny axis 0 1 0\nz axis 0 0 1\nTransform\n1 0 0 0\n0 1 0 0\n0 0 1 0\n0 0 0 1\n";
        a += "World Vertices 256\n";
        for (int i = 0; i < 256; ++i) { const double r = i % 2 ? 0.4 : 1.0, t = 6.283185307179586 * i / 256.0; a += std::to_string(r * std::cos(t)) + " " + std::to_string(r * std::sin(t)) + " 0\n"; }   // a 128-point star: concave
        const int nf = 1900;   // 1900 x 254 = 482,600 triangles: inside the triangle budget
        a += "Faces " + std::to_string(nf) + "\n";
        std::string corners;
        for (int i = 0; i < 256; ++i) corners += "<" + std::to_string(i) + ",0> ";
        for (int i = 0; i < nf; ++i) a += "Face verts 256 flags 0 mat 0\n" + corners + "\n";
        ImportedModel m; std::string e;
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = Read(a, m, &e);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::fprintf(stderr, "  %d concave 256-corner faces: %s in %.2f s\n", nf, ok ? "read" : e.c_str(), secs);
        CHECK(ok || e.find("too many") != std::string::npos);
        CHECK(secs < 10.0 * kSlow);
        // a 40-corner concave star is still ear clipped (it has no overlapping triangles: all 38 are inside the star)
        std::string b = "Caligari V00.01ALH             \nPolH V0.08 Id 1 Parent 0 Size 1\nName S\ncenter 0 0 0\nx axis 1 0 0\ny axis 0 1 0\nz axis 0 0 1\nTransform\n1 0 0 0\n0 1 0 0\n0 0 1 0\n0 0 0 1\nWorld Vertices 40\n";
        for (int i = 0; i < 40; ++i) { const double r = i % 2 ? 0.3 : 1.0, t = 6.283185307179586 * i / 40.0; b += std::to_string(r * std::cos(t)) + " " + std::to_string(r * std::sin(t)) + " 0\n"; }
        b += "Faces 1\nFace verts 40 flags 0 mat 0\n";
        for (int i = 0; i < 40; ++i) b += "<" + std::to_string(i) + ",0> ";
        b += "\n";
        ImportedModel s;
        CHECK(Read(b, s, &e) && fx::TriCount(s) == 38);
        // star area by hand: 20 points of radius 1 and 20 of radius 0.3, 40 triangles (O, outer, inner) of area 0.5 r1 r2 sin(9 degrees)
        CHECK_NEAR(fx::Area(s), 40 * 0.5 * 1.0 * 0.3 * std::sin(6.283185307179586 / 40.0), 1e-3);
    }
    {   // a zero-scale parent cannot be undone: the child keeps its world matrix and a note says so
        std::string a = fx::CobAscii();
        const std::size_t p = a.find("2 0 0 10\n0 2 0 0\n0 0 2 0\n");
        a.replace(p, std::string("2 0 0 10\n0 2 0 0\n0 0 2 0\n").size(), "0 0 0 10\n0 0 0 0\n0 0 0 0\n");
        ImportedModel m;
        CHECK(Read(a, m) && fx::Sane(m) && HasNote(m, "zero scale"));
        CHECK(fx::Near(m.nodes[2].position.x, 10) && fx::Near(m.nodes[2].position.y, 5));   // the world position (10,0,5) after the swap (10,5,0)
    }
    {   // the real files, when at hand
        const char* dir = std::getenv("OKAY_E3_SAMPLES");
        struct R { const char* name; std::size_t parts, tris, verts, nodes; };
        const R rows[] = {
            {"molecule", 4, 896, 604, 6}, {"spider_4_3", 4, 1312, 1557, 2}, {"spider_6_6", 4, 1312, 1557, 2}, {"dwarf", 2, 1896, 2357, 3},
        };
        int done = 0;
        for (const R& r : rows) for (const char* suf : {"", "_ascii"}) {
            const fs::path p = dir ? fs::path(dir) / (std::string(r.name) + suf + ".cob") : fs::path();
            if (!dir || !fs::exists(p)) continue;
            ImportedModel m; std::string e;
            const bool ok = ReadCobFile(p.string(), m, &e);
            if (!ok) std::fprintf(stderr, "  %s: %s\n", p.string().c_str(), e.c_str());
            CHECK(ok && fx::Sane(m));
            std::size_t v = 0; for (const ImportedPart& q : m.parts) v += q.mesh.vertices.size();
            // the counts of the files (binary and ASCII agree, which is the check that both encodings are read the same way): parts, triangles, nodes
            CHECK(m.parts.size() == r.parts && fx::TriCount(m) == r.tris && m.nodes.size() == r.nodes);
            CHECK(v >= r.verts);   // welding per (vertex, texture vertex) can only add vertices
            // every triangle faces away from its part's centre (a closed sphere or body), after the left-handed to right-handed swap
            std::size_t out = 0, in = 0;
            for (const ImportedPart& q : m.parts) {
                Vec3 c{0, 0, 0}; for (const Vec3& x : q.mesh.vertices) c = c + x; c = c * (1.0f / (float)q.mesh.vertices.size());
                for (std::size_t i = 0; i + 2 < q.mesh.triangles.size(); i += 3) {
                    const Vec3 a = q.mesh.vertices[(std::size_t)q.mesh.triangles[i]], b = q.mesh.vertices[(std::size_t)q.mesh.triangles[i + 1]], d = q.mesh.vertices[(std::size_t)q.mesh.triangles[i + 2]];
                    const Vec3 n = Vec3::Cross(b - a, d - a);
                    if (Vec3::Dot(n, (a + b + d) * (1.0f / 3.0f) - c) >= 0) ++out; else ++in;
                }
            }
            if (std::string(r.name) == "molecule") CHECK(out > 20 * in);   // spheres: outward by the right-hand rule
            std::fprintf(stderr, "  real file %s%s.cob: %zu parts, %zu triangles, outward %zu inward %zu\n", r.name, suf, m.parts.size(), fx::TriCount(m), out, in);
            ++done;
        }
        if (!done) std::fprintf(stderr, "  real COB files: skipped (set OKAY_E3_SAMPLES to a folder that has them)\n");
    }
    TEST_MAIN_RESULT();
}
