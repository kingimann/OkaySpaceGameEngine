// Our own SIB (Silo) reader (engine/src/Render/SibReader.cpp). SYNTHETIC files built in memory (tests/sib_fixtures.hpp) checked against
// numbers worked out by hand, plus every truncation, a mutation sweep and the budgets. The one real file (heffalump.sib, public domain,
// not in this repository) is checked by own_sib when OKAY_MODEL_CORPUS is set. The structure the reader follows was worked out from
// that one file only: see docs/content/model_readers.md (section SIB) for what is an assumption.
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "reader_hostile_common.hpp"
#include "sib_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

static bool HasNote(const ImportedModel& m, const char* what) { for (const std::string& n : m.notes) if (n.find(what) != std::string::npos) return true; return false; }
static bool Near(float a, float b, float e = 1e-5f) { return std::fabs(a - b) <= e; }
static bool Read(const std::string& t, ImportedModel& m, std::string& err, const std::string& dir = std::string()) { return ReadSibBuffer(t, dir, m, &err); }
static std::string Basic() {
    return sibfx::File({sibfx::Matr(0.5f, 0.5f, 0.5f, "Grey", ""), sibfx::Matr(0.25f, 0.5f, 0.75f, "Red", "tex.png"),
                        sibfx::Shp(sibfx::Cube()), sibfx::Shp(sibfx::LShape()), sibfx::Chunk("LGHT", "xxxx"), sibfx::Chunk("INST", "yy"), sibfx::Chunk("GRPS", "")});
}

int main() {
    RUN_SUITE("sib_reader");
    const fs::path dir = fs::temp_directory_path() / "okay_sib_reader";
    fs::create_directories(dir);
    const std::string base = dir.string() + "/";
    {
        ImportedModel m; std::string err;
        const bool ok = Read(Basic(), m, err, base);
        if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
        CHECK(ok && m.backend == "own" && fx::Sane(m));
        CHECK(m.nodes.size() == 3 && m.nodes[0].name == "SIB" && m.nodes[1].name == "Cube" && m.nodes[2].name == "L" && m.nodes[1].parent == 0);
        CHECK(m.materials.size() == 2 && m.materials[0].name == "Grey" && m.materials[1].name == "Red");
        CHECK(m.materials[1].hasDiffuse && Near(m.materials[1].diffuse.r, 0.25f) && Near(m.materials[1].diffuse.g, 0.5f) && Near(m.materials[1].diffuse.b, 0.75f));
        CHECK(m.materials[0].texture.empty() && m.materials[1].texture == base + "tex.png");
        // the cube: faces 0..2 on material 1, faces 3..5 on material 0: two parts of 6 triangles; the L: one part, 4 triangles
        CHECK(m.parts.size() == 3);
        CHECK(m.parts[0].node == 1 && m.parts[0].material == 1 && m.parts[0].mesh.triangles.size() == 18 && m.parts[0].mesh.vertices.size() >= 8 && m.parts[0].mesh.vertices.size() <= 12);   // corners with the same position and texture coordinate are welded
        CHECK(m.parts[1].node == 1 && m.parts[1].material == 0 && m.parts[1].mesh.triangles.size() == 18);
        CHECK(m.parts[2].node == 2 && m.parts[2].material == -1 && m.parts[2].mesh.triangles.size() == 12);
        CHECK(fx::TriCount(m) == 16);
        CHECK(Near((float)fx::Area(m), 6.f + 3.f, 1e-4f));
        // texture coordinates: v is flipped: the first corner of face 0 is vertex 0 with uv (0, 0) in the file, (0, 1) here
        {
            const Mesh& me = m.parts[0].mesh;
            bool found = false;
            for (std::size_t i = 0; i < me.vertices.size(); ++i)
                if (Near(me.vertices[i].x, 0) && Near(me.vertices[i].y, 0) && Near(me.vertices[i].z, 0) && Near(me.uvs[i].x, 0) && Near(me.uvs[i].y, 1)) found = true;
            CHECK(found);
        }
        // winding kept: the cube's triangles face outward (normal . (centre - 0.5) > 0)
        for (std::size_t pi = 0; pi < 2; ++pi) {
            const Mesh& me = m.parts[pi].mesh;
            for (std::size_t i = 0; i + 2 < me.triangles.size(); i += 3) {
                const Vec3 &a = me.vertices[(std::size_t)me.triangles[i]], &b = me.vertices[(std::size_t)me.triangles[i + 1]], &c = me.vertices[(std::size_t)me.triangles[i + 2]];
                const double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
                const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
                const double cx = (a.x + b.x + c.x) / 3 - 0.5, cy = (a.y + b.y + c.y) / 3 - 0.5, cz = (a.z + b.z + c.z) / 3 - 0.5;
                CHECK(nx * cx + ny * cy + nz * cz > 0);
            }
        }
        // the concave L is not fanned wrong: no triangle leaves the L (the notch at (1.5, 1.5) is empty)
        for (std::size_t i = 0; i + 2 < m.parts[2].mesh.triangles.size(); i += 3) {
            const Vec3 &a = m.parts[2].mesh.vertices[(std::size_t)m.parts[2].mesh.triangles[i]], &b = m.parts[2].mesh.vertices[(std::size_t)m.parts[2].mesh.triangles[i + 1]], &c = m.parts[2].mesh.vertices[(std::size_t)m.parts[2].mesh.triangles[i + 2]];
            const float cx = (a.x + b.x + c.x) / 3, cy = (a.y + b.y + c.y) / 3;
            CHECK(!(cx > 1.f && cy > 1.f));
        }
        CHECK(HasNote(m, "1 instance") && HasNote(m, "1 light") && HasNote(m, "groups") && HasNote(m, "smooth normals"));
        CHECK(HasNote(m, "missing or bad shape number"));   // the INST chunk of Basic() is two bytes of nothing
    }
    {   // instances: a copy of the shape, taken into the shape's frame, scaled by SMTX, put into the instance's frame
        //   the cube's frame: origin (0.5, 0.5, 0.5), the plain axes -> the cube is [-0.5, 0.5] in its frame
        //   the instance: origin (10, 0, 0), axes x = (0, 0, -1), y = (0, 1, 0), z = (1, 0, 0) (a quarter turn about y), scale x by 2
        //   -> local x in [-1, 1] goes along -z, local z in [-0.5, 0.5] along +x: x 9.5..10.5, y -0.5..0.5, z -1..1
        sibfx::Shape c = sibfx::Cube(); c.axis = {0.5f, 0.5f, 0.5f, 1, 0, 0, 0, 1, 0, 0, 0, 1};
        const std::vector<float> turn = {10, 0, 0, 0, 0, -1, 0, 1, 0, 1, 0, 0};
        for (float sx : {2.0f, -2.0f}) {   // -2: a mirroring scale, the winding is turned back so the faces still face out
            ImportedModel m; std::string err;
            const bool ok = Read(sibfx::File({sibfx::Shp(c), sibfx::Inst("Copy", 0, turn, {sx, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1})}), m, err);
            if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
            CHECK(ok && fx::Sane(m) && m.nodes.size() == 3 && m.nodes[2].name == "Copy" && m.parts.size() == 2 && fx::TriCount(m) == 24 && HasNote(m, "1 instance(s) placed"));   // no materials: one part per shape
            if (!ok || m.parts.size() != 2) continue;
            float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
            for (std::size_t pi = 1; pi < 2; ++pi) {
                CHECK(m.parts[pi].node == 2 && m.parts[pi].material == m.parts[0].material && m.parts[0].node == 1);
                const Mesh& me = m.parts[pi].mesh;
                for (const Vec3& v : me.vertices) { const float q[3] = {v.x, v.y, v.z}; for (int k = 0; k < 3; ++k) { lo[k] = std::fmin(lo[k], q[k]); hi[k] = std::fmax(hi[k], q[k]); } }
                for (std::size_t i = 0; i + 2 < me.triangles.size(); i += 3) {   // outward: normal . (centroid - centre) > 0, and the stored normals agree
                    const Vec3 &a = me.vertices[(std::size_t)me.triangles[i]], &b = me.vertices[(std::size_t)me.triangles[i + 1]], &d = me.vertices[(std::size_t)me.triangles[i + 2]];
                    const double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, vx = d.x - a.x, vy = d.y - a.y, vz = d.z - a.z;
                    const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
                    const double cx = (a.x + b.x + d.x) / 3 - 10, cy = (a.y + b.y + d.y) / 3, cz = (a.z + b.z + d.z) / 3;
                    CHECK(nx * cx + ny * cy + nz * cz > 0);
                    const Vec3& sn = me.normals[(std::size_t)me.triangles[i]];
                    CHECK(nx * sn.x + ny * sn.y + nz * sn.z > 0);
                }
            }
            CHECK(Near(lo[0], 9.5f, 1e-4f) && Near(hi[0], 10.5f, 1e-4f) && Near(lo[1], -0.5f, 1e-4f) && Near(hi[1], 0.5f, 1e-4f) && Near(lo[2], -1, 1e-4f) && Near(hi[2], 1, 1e-4f));
        }
        {   // a shape number past the shapes, a frame of NaN, a flat scale: skipped with a note, the shape still reads
            ImportedModel m; std::string err;
            const float nan = std::nanf("");
            CHECK(Read(sibfx::File({sibfx::Shp(c), sibfx::Inst("A", 5, turn, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}), sibfx::Inst("B", 0, {nan, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1}, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}),
                                     sibfx::Inst("C", 0, turn, {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1})}), m, err) && m.nodes.size() == 2 && HasNote(m, "3 instance(s) with a missing or bad"));
            // a scale matrix with a move: noted
            CHECK(Read(sibfx::File({sibfx::Shp(c), sibfx::Inst("D", 0, turn, {1, 0, 0, 5, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1})}), m, err) && HasNote(m, "hold a move"));
        }
        {   // budget: 2,000 instances of a shape of 2,500 triangles is over the 4,000,000 triangle budget: refused, nothing left behind
            sibfx::Shape g; g.name = "grid"; g.fama = {0, -1};
            const int n = 51;
            for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) { g.pos.push_back((float)x); g.pos.push_back((float)y); g.pos.push_back(0); }
            for (int y = 0; y + 1 < n; ++y) for (int x = 0; x + 1 < n; ++x) { const int a = y * n + x; g.faces.push_back({{a, a + 1, a + n + 1, a + n}, {{0, 0}, {0, 0}, {0, 0}, {0, 0}}}); }
            std::vector<std::string> ch = {sibfx::Shp(g)};
            for (int i = 0; i < 2000; ++i) ch.push_back(sibfx::Inst("I", 0, {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1}, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}));
            ImportedModel m; std::string err;
            CHECK(!Read(sibfx::File(ch), m, err) && err.find("budget") != std::string::npos && m.parts.empty());
        }
    }
    {   // a texture path that leaves the folder is not passed on; a shape with no texture coordinates still reads
        sibfx::Shape c = sibfx::Cube(); c.uv = false;
        ImportedModel m; std::string err;
        CHECK(Read(sibfx::File({sibfx::Matr(1, 1, 1, "Esc", "../x.png"), sibfx::Shp(c)}), m, err, base) && m.materials[0].texture.empty() && HasNote(m, "not used") && HasNote(m, "no usable texture coordinates") && m.parts[0].mesh.uvs.empty());
        // absolute path
        CHECK(Read(sibfx::File({sibfx::Matr(1, 1, 1, "Abs", "/etc/passwd"), sibfx::Shp(sibfx::Cube())}), m, err, base) && m.materials[0].texture.empty());
    }
    {   // bad indices and short faces are skipped with a note; a mirror plane is mentioned
        sibfx::Shape c = sibfx::Cube();
        c.faces[0].v[2] = 99;
        c.faces.push_back({{0, 1}, {{0, 0}, {1, 0}}});
        c.mirror = true;
        ImportedModel m; std::string err;
        CHECK(Read(sibfx::File({sibfx::Shp(c)}), m, err) && fx::Sane(m) && HasNote(m, "out-of-range") && HasNote(m, "fewer than 3") && HasNote(m, "mirror plane") && fx::TriCount(m) == 10);
        // a material number past the table is "none"
        sibfx::Shape d = sibfx::Cube(); d.fama = {0, 7};
        CHECK(Read(sibfx::File({sibfx::Matr(1, 1, 1, "A", ""), sibfx::Shp(d)}), m, err) && m.parts.size() == 1 && m.parts[0].material == -1);
        // a corner count that does not match the texture coordinates: those corners get (0, 0)... the face reads without texture coordinates
        sibfx::Shape e = sibfx::Cube(); e.faces[0].uv.pop_back();
        CHECK(Read(sibfx::File({sibfx::Shp(e)}), m, err) && fx::TriCount(m) == 12);
    }
    {   // refusals
        ImportedModel m; std::string err;
        CHECK(!Read("", m, err) && err.find("HEAD") != std::string::npos && m.parts.empty() && m.nodes.empty());
        CHECK(!Read("HEAD", m, err) && err.find("HEAD") != std::string::npos);
        std::string v2 = Basic(); v2[8] = 2;
        CHECK(!Read(v2, m, err) && err.find("version 2") != std::string::npos);
        CHECK(!Read(sibfx::File({sibfx::Matr(1, 1, 1, "A", "")}), m, err) && err.find("no usable shapes") != std::string::npos);
        std::string cut = Basic().substr(0, Basic().size() - 1);   // the last chunk (empty) has a header one byte short
        CHECK(!Read(cut, m, err) && err.find("cut off") != std::string::npos);
        std::string tail = Basic() + "ab";                          // two stray bytes
        CHECK(!Read(tail, m, err) && err.find("cut off") != std::string::npos);
    }
    {   // every truncation is refused or sane; a mutation sweep
        const std::string full = Basic();
        for (std::size_t n = 0; n < full.size(); ++n) {
            ImportedModel m; std::string err;
            const bool ok = Read(full.substr(0, n), m, err);
            CHECK(ok ? fx::Sane(m) : (!err.empty() && m.parts.empty() && m.nodes.empty()));
        }
        long reads = 0, refusals = 0;
        for (long i = 0; i < hostile::Iters(30000); ++i) {
            std::string s = full;
            for (int k = 0; k < 1 + (int)(hostile::Rnd() % 4); ++k) hostile::MutateBinary(s);
            ImportedModel m; std::string err;
            const bool ok = Read(s, m, err, base);
            CHECK(ok ? hostile::Sane(m) : (m.parts.empty() && m.nodes.empty() && !err.empty()));
            (ok ? reads : refusals)++;
        }
        std::fprintf(stderr, "  mutation sweep: %ld read, %ld refused\n", reads, refusals);
        // every aligned word of the file at extreme values
        for (std::size_t at = 12; at + 4 <= full.size(); at += 4)
            for (std::uint32_t v : {0xFFFFFFFFu, 0x7FFFFFFFu, 0x80000000u, 0x7F800000u, 0x7FC00000u}) {
                std::string s = full;
                for (int i = 0; i < 4; ++i) s[at + (std::size_t)i] = (char)((v >> (8 * i)) & 255);
                ImportedModel m; std::string err;
                const bool ok = Read(s, m, err, base);
                CHECK(ok ? hostile::Sane(m) : (m.parts.empty() && m.nodes.empty() && !err.empty()));
            }
    }
    {   // budgets: a face that names 40,000 corners is fanned (past the ear clipping size), 100,000 faces of a million corners over the corner budget is refused
        sibfx::Shape big; big.name = "big";
        const int n = 40000;
        for (int i = 0; i < n; ++i) { const double a = 6.283185307179586 * i / n; big.pos.push_back((float)std::cos(a)); big.pos.push_back((float)std::sin(a)); big.pos.push_back(0.f); }
        sibfx::FaceDef f; for (int i = 0; i < n; ++i) { f.v.push_back(i); f.uv.push_back({0, 0}); }
        big.faces.push_back(f);
        ImportedModel m; std::string err;
        CHECK(Read(sibfx::File({sibfx::Shp(big)}), m, err) && fx::TriCount(m) == (std::size_t)n - 2 && HasNote(m, "fanned"));
        // one triangle named 5,000,000 times: over the 4,000,000 triangle budget
        sibfx::Shape rep; rep.name = "rep"; rep.pos = {0,0,0, 1,0,0, 0,1,0};
        std::string facs, ftvs;
        for (int i = 0; i < 5000000; ++i) { fx::P32(facs, 3); fx::P32(facs, 0); fx::P32(facs, 1); fx::P32(facs, 2); }
        std::string body = sibfx::Chunk("VRTS", [&] { std::string v; for (float x : rep.pos) fx::PF(v, x); return v; }()) + sibfx::Chunk("FACS", facs);
        ImportedModel r;
        CHECK(!Read(sibfx::File({sibfx::Chunk("SHAP", body)}), r, err) && err.find("triangles (the budget)") != std::string::npos && r.parts.empty());
    }
    std::error_code ec; fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
