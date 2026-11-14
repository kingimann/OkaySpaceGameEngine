// Our own Irrlicht mesh reader (engine/src/Render/IrrMeshReader.cpp). SYNTHETIC fixtures built in memory, checked
// against hand-computed numbers (see BuildIrrMesh in batchc_fixtures.hpp). Real .irrmesh files are checked by
// model_corpus. Builds on its own with -DOKAY_STANDALONE_TEST (reader source only).
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "batchc_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include <cmath>
#include <string>

using namespace okay;

static bool HasNote(const ImportedModel& m, const char* what) { for (const std::string& n : m.notes) if (n.find(what) != std::string::npos) return true; return false; }

int main() {
    RUN_SUITE("irrmesh_reader");
    for (const char* eol : {"\n", "\r\n"}) {
        ImportedModel m; std::string err;
        CHECK(ReadIrrMeshBuffer(fx::BuildIrrMesh(eol), "dir/", m, &err));
        CHECK(m.backend == "own" && fx::Sane(m));
        CHECK(m.parts.size() == 3 && fx::TriCount(m) == 4 && m.nodes.size() == 1 && m.materials.size() == 3);
        CHECK(m.parts[0].mesh.vertices.size() == 3 && m.parts[1].mesh.vertices.size() == 4 && m.parts[2].mesh.vertices.size() == 3);
        CHECK_NEAR(fx::Area(m), 4.5, 1e-5);
        // buffer 1: edges (2,0,0) and (0,3,0): area 3; its material
        {
            const ImportedMaterial& mt = m.materials[0];
            CHECK(mt.hasDiffuse && fx::Near(mt.diffuse.r, 128 / 255.0f) && fx::Near(mt.diffuse.g, 64 / 255.0f) && fx::Near(mt.diffuse.b, 192 / 255.0f) && fx::Near(mt.diffuse.a, 1.0f));
            CHECK(mt.hasEmissive && fx::Near(mt.emissive.r, 16 / 255.0f) && fx::Near(mt.emissive.g, 32 / 255.0f) && fx::Near(mt.emissive.b, 48 / 255.0f));
            CHECK(fx::Near(mt.shininess, 0.75f) && mt.texture == "dir/assets/a.png" && mt.normalMap == "dir/n.png");
            CHECK(m.materials[1].texture == "dir/b.jpg" && m.materials[1].normalMap.empty() && !m.materials[1].hasDiffuse);
        }
        // uv as written: the vertex at (0,3,0) has uv (0,1)
        for (std::size_t i = 0; i < m.parts[0].mesh.vertices.size(); ++i) {
            const Vec3 p = m.parts[0].mesh.vertices[i]; const Vec2 u = m.parts[0].mesh.uvs[i];
            if (fx::Near(p.y, 3)) CHECK(fx::Near(u.x, 0) && fx::Near(u.y, 1));
            if (fx::Near(p.x, 2)) CHECK(fx::Near(u.x, 1) && fx::Near(u.y, 0));
            CHECK(fx::Near(m.parts[0].mesh.normals[i].z, 1));
        }
        CHECK(HasNote(m, "2tcoords") && HasNote(m, "tangents") && HasNote(m, "vertex colour") && HasNote(m, "not a multiple of 3") && HasNote(m, "left-handed"));
    }
    {   // UTF-16 with a byte order mark reads the same
        ImportedModel a, b; std::string err;
        CHECK(ReadIrrMeshBuffer(fx::BuildIrrMesh(), "", a, &err) && ReadIrrMeshBuffer(fx::ToUtf16le(fx::BuildIrrMesh()), "", b, &err));
        CHECK(fx::TriCount(a) == fx::TriCount(b) && a.parts.size() == b.parts.size() && fx::Near((float)fx::Area(a), (float)fx::Area(b)));
    }
    {   // refusals
        ImportedModel m; std::string err;
        const std::string good = fx::BuildIrrMesh();
        CHECK(!ReadIrrMeshBuffer("", "", m, &err) && !err.empty());
        CHECK(!ReadIrrMeshBuffer("<scene></scene>", "", m, &err) && err.find("not an Irrlicht mesh") != std::string::npos);
        CHECK(!ReadIrrMeshBuffer("<mesh></mesh>", "", m, &err) && err.find("no <buffer>") != std::string::npos);
        std::string bad = good;
        bad.replace(bad.find("vertexCount=\"3\""), 15, "vertexCount=\"9\"");
        CHECK(!ReadIrrMeshBuffer(bad, "", m, &err) && err.find("ends after 3 of 9") != std::string::npos);
        bad = good; bad.replace(bad.find("vertexCount=\"3\""), 15, "vertexCount=\"-1\"");
        CHECK(!ReadIrrMeshBuffer(bad, "", m, &err));
        bad = good; bad.replace(bad.find("2 0 0 0 0 1"), 11, "2 0 x 0 0 1");
        CHECK(!ReadIrrMeshBuffer(bad, "", m, &err) && err.find("bad number") != std::string::npos);
        bad = good; bad.replace(bad.find("ff8040c0"), 8, "ff8040c0");
        bad.replace(bad.find("0 0 0 0 0 1 ffffffff 0 0"), 24, "0 0 0 0 0 1 fffgffff 0 0");
        CHECK(!ReadIrrMeshBuffer(bad, "", m, &err) && err.find("bad colour") != std::string::npos);
        bad = good; bad.replace(bad.find("type=\"standard\""), 15, "type=\"weird\"");
        CHECK(!ReadIrrMeshBuffer(bad, "", m, &err) && err.find("unknown vertex type") != std::string::npos);
        // an index past the vertices skips that triangle and says so; the file still reads
        bad = good; bad.replace(bad.find("0 1 2 0 2 3"), 11, "0 1 2 0 2 9");
        CHECK(ReadIrrMeshBuffer(bad, "", m, &err) && fx::TriCount(m) == 3 && HasNote(m, "out-of-range"));
        // a billion-laughs style entity is refused by the XML reader
        CHECK(!ReadIrrMeshBuffer("<!DOCTYPE m [<!ENTITY a \"x\">]><mesh/>", "", m, &err));
        // every truncation
        for (std::size_t n = 0; n < good.size(); ++n) {
            ImportedModel t; std::string e;
            const bool ok = ReadIrrMeshBuffer(good.substr(0, n), "", t, &e);
            CHECK(ok ? fx::Sane(t) : (!e.empty() && t.parts.empty() && t.nodes.empty()));
        }
    }
    TEST_MAIN_RESULT();
}
