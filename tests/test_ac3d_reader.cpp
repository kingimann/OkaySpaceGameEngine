// Our own AC3D reader (engine/src/Render/Ac3dReader.cpp). SYNTHETIC fixtures built in memory, checked against hand-computed
// numbers (see BuildAc3d and BuildAc3dCube in batchd_fixtures.hpp). Real .ac files are checked by model_corpus. Builds on
// its own with -DOKAY_STANDALONE_TEST (reader source only).
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "batchd_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include <cmath>
#include <string>

using namespace okay;

static bool HasNote(const ImportedModel& m, const char* what) { for (const std::string& n : m.notes) if (n.find(what) != std::string::npos) return true; return false; }
static const ImportedPart* PartOnNode(const ImportedModel& m, const char* node) {
    for (const ImportedPart& p : m.parts) if (m.nodes[(std::size_t)p.node].name == node) return &p;
    return nullptr;
}

int main() {
    RUN_SUITE("ac3d_reader");
    for (const char* eol : {"\n", "\r\n"}) {
        ImportedModel m; std::string err;
        CHECK(ReadAc3dBuffer(fx::BuildAc3d(eol), "dir/", m, &err));
        CHECK(m.backend == "own" && fx::Sane(m));
        CHECK(m.parts.size() == 3 && fx::TriCount(m) == 5 && m.materials.size() == 3 && m.nodes.size() == 6);
        CHECK_NEAR(fx::Area(m), 7.5 - 0.0, 1e-5);   // the node transforms here are a translation and a rotation: no scale, so local area = world area
        CHECK_NEAR(fx::WorldArea(m), 7.5, 1e-5);
        // nodes: synthetic root, world, quad (loc), group (rot and loc), tri, strip
        CHECK(m.nodes[0].name == "AC3D" && m.nodes[1].name == "w" && m.nodes[2].name == "quad" && m.nodes[3].name == "grp" && m.nodes[4].name == "tri" && m.nodes[5].name == "strip");
        CHECK(m.nodes[2].parent == 1 && m.nodes[3].parent == 1 && m.nodes[4].parent == 3 && m.nodes[5].parent == 1);
        CHECK(fx::Near(m.nodes[2].position.x, 10) && fx::Near(m.nodes[3].position.y, 5));
        const Quat q = m.nodes[3].rotation;   // Rz(90): (0, 0, sin 45, cos 45)
        CHECK(fx::Near(q.x, 0) && fx::Near(q.y, 0) && fx::Near(q.z, 0.70710678f) && fx::Near(q.w, 0.70710678f));
        CHECK(fx::Near(m.nodes[3].scale.x, 1) && fx::Near(m.nodes[3].scale.y, 1));
        // the group turns its child: the triangle (0,0,0) (1,0,0) (0,1,0) seen from the world: x axis -> y axis, so the point (1,0,0) lands at (0, 5+1, 0)
        {
            const ImportedPart* t = PartOnNode(m, "tri");
            CHECK(t != nullptr && t->mesh.vertices.size() == 3);
            const Mat4 w = Mat4::TRS(m.nodes[3].position, m.nodes[3].rotation, m.nodes[3].scale);
            bool found = false;
            for (const Vec3& v : t->mesh.vertices) { const Vec3 p = w.MultiplyPoint(v); if (fx::Near(p.x, 0, 1e-4f) && fx::Near(p.y, 6, 1e-4f)) found = true; }
            CHECK(found);
        }
        // the quad: texture, uv as u * rep + off, material 0 colours
        {
            const ImportedPart* qd = PartOnNode(m, "quad");
            CHECK(qd != nullptr && qd->mesh.triangles.size() == 6 && qd->mesh.vertices.size() == 4 && qd->mesh.uvs.size() == 4);
            const ImportedMaterial& mt = m.materials[(std::size_t)qd->material];
            CHECK(mt.texture == "dir/tex/a.png" && mt.hasDiffuse && fx::Near(mt.diffuse.r, 1) && fx::Near(mt.diffuse.g, 0) && fx::Near(mt.diffuse.a, 0.75f));
            CHECK(mt.hasEmissive && fx::Near(mt.emissive.r, 0.1f) && fx::Near(mt.emissive.g, 0.2f) && fx::Near(mt.emissive.b, 0.3f) && fx::Near(mt.shininess, 50));
            for (std::size_t i = 0; i < qd->mesh.vertices.size(); ++i) {
                const Vec3 p = qd->mesh.vertices[i]; const Vec2 uv = qd->mesh.uvs[i];
                if (fx::Near(p.x, 2) && fx::Near(p.y, 3)) CHECK(fx::Near(uv.x, 2.5f) && fx::Near(uv.y, 1));
                if (fx::Near(p.x, 0) && fx::Near(p.y, 0)) CHECK(fx::Near(uv.x, 0.5f) && fx::Near(uv.y, 0));
            }
        }
        // the strip: stored normals are kept, both triangles face +z (the second one was turned over)
        {
            const ImportedPart* st = PartOnNode(m, "strip");
            CHECK(st != nullptr && st->mesh.triangles.size() == 6 && st->mesh.vertices.size() == 4 && st->mesh.uvs.empty());
            for (const Vec3& n : st->mesh.normals) CHECK(fx::Near(n.z, 1));
            for (std::size_t i = 0; i + 2 < st->mesh.triangles.size(); i += 3) {
                const Vec3 a = st->mesh.vertices[(std::size_t)st->mesh.triangles[i]], b = st->mesh.vertices[(std::size_t)st->mesh.triangles[i + 1]], c = st->mesh.vertices[(std::size_t)st->mesh.triangles[i + 2]];
                CHECK(Vec3::Cross(b - a, c - a).z > 0);
            }
        }
        CHECK(HasNote(m, "line surface") && HasNote(m, "texrep") && HasNote(m, "unknown keyword") && HasNote(m, "(first: bogus)"));
    }
    {   // UTF-16 and a byte order mark read the same
        ImportedModel a, b, c; std::string err;
        CHECK(ReadAc3dBuffer(fx::BuildAc3d(), "", a, &err) && ReadAc3dBuffer(fx::ToUtf16leD(fx::BuildAc3d()), "", b, &err) && ReadAc3dBuffer("\xEF\xBB\xBF" + fx::BuildAc3d(), "", c, &err));
        CHECK(fx::TriCount(a) == fx::TriCount(b) && fx::TriCount(a) == fx::TriCount(c) && a.parts.size() == b.parts.size() && fx::Near((float)fx::Area(a), (float)fx::Area(b)));
    }
    {   // subdivision: one Catmull-Clark level of a cube
        ImportedModel m0, m1, m2; std::string err;
        CHECK(ReadAc3dBuffer(fx::BuildAc3dCube(0), "", m0, &err) && fx::TriCount(m0) == 12);
        CHECK(ReadAc3dBuffer(fx::BuildAc3dCube(1), "", m1, &err) && m1.backend == "own" && fx::Sane(m1));
        CHECK(fx::TriCount(m1) == 48 && HasNote(m1, "Catmull-Clark"));
        bool corner = false, edgePt = false, face = false;
        double maxx = -1e9;
        for (const Vec3& v : m1.parts[0].mesh.vertices) {
            if (fx::Near(std::fabs(v.x), 5.0f / 9) && fx::Near(std::fabs(v.y), 5.0f / 9) && fx::Near(std::fabs(v.z), 5.0f / 9)) corner = true;
            if (fx::Near(std::fabs(v.x), 0.75f) && fx::Near(std::fabs(v.y), 0.75f) && fx::Near(v.z, 0)) edgePt = true;
            if (fx::Near(v.x, 1) && fx::Near(v.y, 0) && fx::Near(v.z, 0)) face = true;
            maxx = std::max<double>(maxx, v.x);
        }
        CHECK(corner && edgePt && face && fx::Near((float)maxx, 1));
        CHECK(ReadAc3dBuffer(fx::BuildAc3dCube(2), "", m2, &err) && fx::TriCount(m2) == 192);   // 6 * 4^2 quads * 2
        CHECK(fx::Area(m1) < fx::Area(m0) && fx::Area(m2) < fx::Area(m1) && fx::Area(m2) > 0.3 * fx::Area(m0));   // the surface shrinks toward the limit surface
    }
    {   // flat and shaded surfaces: flat keeps 4 vertices per face, shaded with crease 180 shares the 8 corners with averaged normals
        ImportedModel flat, smooth; std::string err;
        CHECK(ReadAc3dBuffer(fx::BuildAc3dCube(0, false), "", flat, &err) && ReadAc3dBuffer(fx::BuildAc3dCube(0, true), "", smooth, &err));
        CHECK(flat.parts.size() == 1 && smooth.parts.size() == 1 && flat.parts[0].mesh.vertices.size() == 24 && smooth.parts[0].mesh.vertices.size() == 8);
        for (const Vec3& n : flat.parts[0].mesh.normals) CHECK(fx::Near(std::fabs(n.x) + std::fabs(n.y) + std::fabs(n.z), 1));   // one axis each
        const float r3 = 1.0f / std::sqrt(3.0f);
        for (const Vec3& n : smooth.parts[0].mesh.normals) CHECK(fx::Near(std::fabs(n.x), r3, 1e-3f) && fx::Near(std::fabs(n.y), r3, 1e-3f) && fx::Near(std::fabs(n.z), r3, 1e-3f));
        CHECK_NEAR(fx::Area(flat), 24.0, 1e-4);
    }
    {   // refusals
        ImportedModel m; std::string err;
        const std::string good = fx::BuildAc3d();
        CHECK(!ReadAc3dBuffer("", "", m, &err) && !err.empty());
        CHECK(!ReadAc3dBuffer("hello world\n", "", m, &err) && err.find("not an AC3D file") != std::string::npos);
        CHECK(!ReadAc3dBuffer("AC3D\nOBJECT world\nkids 0\n", "", m, &err));
        CHECK(!ReadAc3dBuffer("AC3Db\n", "", m, &err) && err.find("no OBJECT") != std::string::npos);
        CHECK(!ReadAc3dBuffer("AC3Db\nOBJECT world\nkids 0\n", "", m, &err) && err.find("no polygons") != std::string::npos);
        std::string bad = good;
        bad.replace(bad.find("numvert 4"), 9, "numvert 9");
        CHECK(!ReadAc3dBuffer(bad, "", m, &err) && !err.empty());
        bad = good; bad.replace(bad.find("2 3 0"), 5, "2 x 0");
        CHECK(!ReadAc3dBuffer(bad, "", m, &err) && err.find("bad number") != std::string::npos);
        bad = good; bad.replace(bad.find("numvert 4"), 9, "numvert -1");
        CHECK(!ReadAc3dBuffer(bad, "", m, &err));
        bad = good; bad.replace(bad.find("SURF 0x20"), 9, "SURF 0xzz");
        CHECK(!ReadAc3dBuffer(bad, "", m, &err) && err.find("bad SURF flags") != std::string::npos);
        // a vertex index past the vertex list skips that surface and says so; the rest still reads
        bad = good; bad.replace(bad.find("3 0 1\nkids 0\nOBJECT group"), 3, "9 0 1");
        CHECK(ReadAc3dBuffer(bad, "", m, &err) && HasNote(m, "past the vertex list") && fx::TriCount(m) == 3);
        // a line-only file has nothing to draw
        CHECK(!ReadAc3dBuffer("AC3Db\nMATERIAL \"\" rgb 1 1 1 amb 0 0 0 emis 0 0 0 spec 0 0 0 shi 0 trans 0\nOBJECT world\nkids 1\nOBJECT poly\nnumvert 2\n0 0 0\n1 0 0\nnumsurf 1\nSURF 0x02\nmat 0\nrefs 2\n0 0 0\n1 0 0\nkids 0\n", "", m, &err) && err.find("no polygons") != std::string::npos);
        // every truncation
        for (std::size_t n = 0; n < good.size(); ++n) {
            ImportedModel t; std::string e;
            const bool ok = ReadAc3dBuffer(good.substr(0, n), "", t, &e);
            CHECK(ok ? fx::Sane(t) : (!e.empty() && t.parts.empty() && t.nodes.empty()));
        }
    }
    TEST_MAIN_RESULT();
}
