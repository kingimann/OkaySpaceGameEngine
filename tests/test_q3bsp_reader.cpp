// Our own Quake III map reader (engine/src/Render/Q3BspReader.cpp). SYNTHETIC maps built in memory (tests/q3bsp_fixtures.hpp) and
// checked against numbers worked out by hand. No real .bsp or .pk3 file is in this repository: the reader is unverified against
// real maps (the winding rule "Quake III stores triangles clockwise" and the patch front side are assumptions, see the doc).
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "q3bsp_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

static bool HasNote(const ImportedModel& m, const char* what) { for (const std::string& n : m.notes) if (n.find(what) != std::string::npos) return true; return false; }
static bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) <= e; }
static bool Read(const std::string& t, ImportedModel& m, std::string& err) { return ReadQ3bspBuffer(t, m, &err); }
static double Z(const Mesh& me, std::size_t i) {
    const Vec3 &a = me.vertices[(std::size_t)me.triangles[i]], &b = me.vertices[(std::size_t)me.triangles[i + 1]], &c = me.vertices[(std::size_t)me.triangles[i + 2]];
    return ((double)b.x - a.x) * ((double)c.y - a.y) - ((double)b.y - a.y) * ((double)c.x - a.x);
}
// Every triangle of the part has a geometric normal whose z is positive (sign > 0) or negative (sign < 0).
static bool AllFacing(const Mesh& me, int sign) {
    for (std::size_t i = 0; i + 2 < me.triangles.size(); i += 3) if (sign * Z(me, i) <= 0) return false;
    return true;
}
static double PartArea(const ImportedPart& p) { ImportedModel t; t.parts.push_back(p); return fx::Area(t); }
static int Find(const Mesh& me, float x, float y, float z) {
    for (std::size_t i = 0; i < me.vertices.size(); ++i) if (Near(me.vertices[i].x, x) && Near(me.vertices[i].y, y) && Near(me.vertices[i].z, z)) return (int)i;
    return -1;
}
static void WriteFile(const fs::path& p, const std::string& s) { std::ofstream f(p, std::ios::binary); f.write(s.data(), (std::streamsize)s.size()); }

int main() {
    RUN_SUITE("q3bsp_reader");
    {   // the basic map
        ImportedModel m; std::string err;
        const bool ok = Read(q3::Basic().Build(), m, err);
        if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
        CHECK(ok && m.backend == "own" && fx::Sane(m));
        CHECK(m.nodes.size() == 2 && m.nodes[0].name == "Q3BSP" && m.nodes[1].name == "model_1" && m.nodes[1].parent == 0);
        CHECK(Near(m.nodes[0].rotation.x, -0.70710678f) && Near(m.nodes[0].rotation.w, 0.70710678f));   // Z up to Y up
        CHECK(m.parts.size() == 3 && m.materials.size() == 2 && m.clips.empty());
        CHECK(m.materials[0].name == "textures/base/wall" && m.materials[1].name == "textures/base/floor");
        // part 0: texture 0 of the world: the quad (4 vertices, 2 triangles) and the patch (49 vertices, 72 triangles)
        const ImportedPart& p0 = m.parts[0];
        CHECK(p0.node == 0 && p0.material == 0 && p0.mesh.vertices.size() == 53 && p0.mesh.triangles.size() == 74 * 3);
        CHECK(p0.mesh.normals.size() == 53 && p0.mesh.uvs.size() == 53);
        CHECK(AllFacing(p0.mesh, +1));                        // clockwise as stored, corners swapped: counter clockwise from +z
        CHECK(Near((float)PartArea(p0), 4096.f + 4096.f, 0.5f));   // quad 64 x 64 and the flat patch 64 x 64
        CHECK(Find(p0.mesh, 64, 64, 0) >= 0 && Find(p0.mesh, 0, 0, 0) >= 0);
        for (const Vec3& n : p0.mesh.normals) CHECK(Near(n.z, 1.f, 1e-3f));
        // a patch point worked out by hand: the control points 0, 32, 64 make x = 64 u; u = 2/6 and v = 3/6 give (21.333, 32, 20) and uv (u / 2 * 2 ...)
        {
            const int i = Find(p0.mesh, 64.f * 2 / 6, 32, 20);
            CHECK(i >= 0 && Near(p0.mesh.uvs[(std::size_t)i].x, 2.f / 6, 1e-4f) && Near(p0.mesh.uvs[(std::size_t)i].y, 0.5f, 1e-4f));
        }
        // part 1: the mesh face, texture 2
        const ImportedPart& p1 = m.parts[1];
        CHECK(p1.node == 0 && p1.material == 1 && p1.mesh.vertices.size() == 3 && p1.mesh.triangles.size() == 3 && AllFacing(p1.mesh, +1));
        CHECK(Find(p1.mesh, 64, 0, 10) >= 0 && Near(p1.mesh.uvs[(std::size_t)Find(p1.mesh, 0, 64, 10)].y, 1.f));   // texture coordinates as written
        CHECK(Near((float)PartArea(p1), 2048.f, 0.5f));
        // part 2: model 1, on its own node
        const ImportedPart& p2 = m.parts[2];
        CHECK(p2.node == 1 && p2.material == 1 && p2.mesh.vertices.size() == 4 && p2.mesh.triangles.size() == 6);
        CHECK(fx::TriCount(m) == 77);
        CHECK(HasNote(m, "nodraw") && HasNote(m, "billboard") && HasNote(m, "lightmaps"));
        // bounding box over all parts: x from 0 to 164
        float lo = 1e9f, hi = -1e9f;
        for (const ImportedPart& p : m.parts) for (const Vec3& v : p.mesh.vertices) { lo = std::min(lo, v.x); hi = std::max(hi, v.x); }
        CHECK(Near(lo, 0) && Near(hi, 164));
    }
    {   // patch front side: stored normals -z flip the winding and the normals; no stored normals keep the (column x row) side and say so
        q3::Map a = q3::Basic();
        for (int k = 11; k < 20; ++k) a.v[(std::size_t)k].n[2] = -1;
        ImportedModel m; std::string err;
        CHECK(Read(a.Build(), m, err) && fx::Sane(m));
        int up = 0, down = 0;
        const Mesh& me = m.parts[0].mesh;
        for (std::size_t i = 0; i + 2 < me.triangles.size(); i += 3) (Z(me, i) > 0 ? up : down)++;
        CHECK(up == 2 && down == 72);
        int negN = 0; for (const Vec3& n : me.normals) if (n.z < -0.99f) ++negN;
        CHECK(negN == 49);
        q3::Map b = q3::Basic();
        for (int k = 11; k < 20; ++k) { b.v[(std::size_t)k].n[0] = b.v[(std::size_t)k].n[1] = b.v[(std::size_t)k].n[2] = 0; }
        ImportedModel m2;
        CHECK(Read(b.Build(), m2, err) && AllFacing(m2.parts[0].mesh, +1) && HasNote(m2, "no stored normals"));
        for (const Vec3& n : m2.parts[0].mesh.normals) CHECK(Near(n.z, 1.f, 1e-3f));
    }
    {   // a bent patch: the middle row of control points (v = 0.5 row) lifted by 16; the surface at v = 0.5 is lifted by 16 / 2 = 8
        q3::Map a = q3::Basic();
        for (int c = 0; c < 3; ++c) a.v[(std::size_t)(11 + 3 + c)].p[2] += 16;
        ImportedModel m; std::string err;
        CHECK(Read(a.Build(), m, err) && fx::Sane(m));
        const Mesh& me = m.parts[0].mesh;
        const int i = Find(me, 32, 32, 20 + 8);   // (u, v) = (0.5, 0.5): x = y = 32, z = 20 + 16 * B1(0.5) = 20 + 16 * 0.5
        CHECK(i >= 0);
        if (i >= 0) CHECK(Near(me.normals[(std::size_t)i].z, 1.f, 1e-3f));   // the top of the bump: the normal still points up
        // an edge point at v = 1/3 is lifted by 16 * 2 * (1/3) * (2/3) = 7.111
        CHECK(Find(me, 32, 64.f / 3, 20 + 16.f * 2 * (1.f / 3) * (2.f / 3)) >= 0);
    }
    {   // bad mesh vertex numbers are skipped with a note; a zero area triangle is dropped
        q3::Map a = q3::Basic();
        a.mv[1] = 9;   // beyond the 4 vertices of face 0
        ImportedModel m; std::string err;
        CHECK(Read(a.Build(), m, err) && fx::Sane(m) && HasNote(m, "bad indices") && fx::TriCount(m) == 76);
        q3::Map b = q3::Basic();
        b.v[1] = b.v[0];   // vertex 1 on vertex 0: triangle (0, 2, 1) has a zero side, (0, 3, 2) does not
        ImportedModel m2;
        CHECK(Read(b.Build(), m2, err) && fx::Sane(m2) && HasNote(m2, "zero area") && fx::TriCount(m2) == 76);
    }
    {   // no models lump: all faces form one model; a model that names faces that do not exist is skipped, with a note
        q3::Map a = q3::Basic(); a.noModels = true;
        ImportedModel m; std::string err;
        CHECK(Read(a.Build(), m, err) && fx::Sane(m) && m.nodes.size() == 1 && HasNote(m, "no usable models") && fx::TriCount(m) == 77);
        q3::Map b = q3::Basic(); b.m[1] = {5, 40};
        ImportedModel m2;
        CHECK(Read(b.Build(), m2, err) && fx::Sane(m2) && m2.nodes.size() == 1 && HasNote(m2, "outside the faces lump") && fx::TriCount(m2) == 75);
    }
    {   // refusals
        ImportedModel m; std::string err;
        CHECK(!Read("", m, err) && err.find("IBSP") != std::string::npos && m.parts.empty());
        q3::Map a = q3::Basic(); a.version = 47;
        CHECK(!Read(a.Build(), m, err) && err.find("version 47") != std::string::npos);
        q3::Map b = q3::Basic(); b.magic = "VBSP";
        CHECK(!Read(b.Build(), m, err) && err.find("IBSP") != std::string::npos);
        q3::Map c = q3::Basic(); c.f.clear(); c.m.clear();
        CHECK(!Read(c.Build(), m, err) && err.find("no faces") != std::string::npos);
        std::string d = q3::Basic().Build();
        d.resize(100);
        CHECK(!Read(d, m, err) && err.find("lump directory") != std::string::npos);
        std::string e = q3::Basic().Build();
        e[8 + 8 * 10 + 4 + 3] = (char)0x7f;   // the vertexes lump claims a size far beyond the file
        CHECK(!Read(e, m, err) && err.find("outside the file") != std::string::npos);
        std::string g = q3::Basic().Build();
        g[8 + 8 * 13 + 4] = (char)(g[8 + 8 * 13 + 4] + 1);   // the faces lump is no longer a whole number of faces
        CHECK(!Read(g, m, err) && !err.empty());
        q3::Map h = q3::Basic();   // only a skipped face
        h.f = {h.f[2]}; h.m = {{0, 1}};
        CHECK(!Read(h.Build(), m, err) && err.find("no drawable") != std::string::npos);
        q3::Map k = q3::Basic();   // a patch whose grid does not match its vertex count
        k.f[3].size1 = 5;
        ImportedModel m3;
        CHECK(Read(k.Build(), m3, err) && HasNote(m3, "bad control grid") && fx::TriCount(m3) == 5);
    }
    {   // every truncation of the basic map is refused with a reason (the lumps are checked against the file size)
        const std::string full = q3::Basic().Build();
        int refused = 0;
        for (std::size_t n = 0; n < full.size(); ++n) {
            ImportedModel m; std::string err;
            const bool ok = Read(full.substr(0, n), m, err);
            CHECK(ok ? fx::Sane(m) : (!err.empty() && m.parts.empty() && m.nodes.empty()));
            if (!ok) ++refused;
        }
        CHECK(refused == (int)full.size());
    }
    {   // .pk3: the map is found under maps/, texture names resolve to the entries of the archive, stored and deflated
        const fs::path dir = fs::temp_directory_path() / "okay_q3bsp_reader";
        fs::create_directories(dir);
        for (bool deflate : {false, true}) {
            const fs::path p = dir / "t.pk3";
            WriteFile(p, q3::Pk3(q3::Basic().Build(), true, deflate));
            ImportedModel m; std::string err;
            const bool ok = ReadQ3bspFile(p.string(), m, &err);
            if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
            CHECK(ok && fx::Sane(m) && fx::TriCount(m) == 77 && m.nodes[0].name == "Q3BSP maps/test.bsp");
            CHECK(m.materials.size() == 2 && m.materials[0].texture == "textures/base/wall.tga" && m.materials[1].texture == "textures/base/floor");
            CHECK(HasNote(m, "pk3"));
        }
        WriteFile(dir / "t.bsp", q3::Basic().Build());
        ImportedModel m; std::string err;
        CHECK(ReadQ3bspFile((dir / "t.bsp").string(), m, &err) && fx::TriCount(m) == 77 && m.nodes[0].name == "Q3BSP");
        // a pk3 with no map, a corrupt pk3, a missing file
        fx::ZipEntry t; t.name = "textures/a.tga"; t.data = "x";
        WriteFile(dir / "nomap.pk3", fx::MakeZip({t}));
        CHECK(!ReadQ3bspFile((dir / "nomap.pk3").string(), m, &err) && err.find("maps/") != std::string::npos && m.parts.empty());
        WriteFile(dir / "bad.pk3", std::string("PK\x03\x04", 4) + "and then rubbish that is not a zip directory");
        CHECK(!ReadQ3bspFile((dir / "bad.pk3").string(), m, &err) && err.find("ZIP") != std::string::npos);
        CHECK(!ReadQ3bspFile((dir / "missing.bsp").string(), m, &err) && err.find("cannot open") != std::string::npos);
        // two maps: the first by name is read and the other is mentioned
        fx::ZipEntry m1, m2; m1.name = "maps/b.bsp"; m1.data = q3::Basic().Build(); m2.name = "maps/a.bsp"; m2.data = q3::Basic().Build();
        WriteFile(dir / "two.pk3", fx::MakeZip({m1, m2}));
        ImportedModel m3;
        CHECK(ReadQ3bspFile((dir / "two.pk3").string(), m3, &err) && m3.nodes[0].name == "Q3BSP maps/a.bsp" && HasNote(m3, "2 maps"));
        std::error_code ec; fs::remove_all(dir, ec);
    }
    TEST_MAIN_RESULT();
}
