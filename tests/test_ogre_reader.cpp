// Our own OGRE XML mesh reader (engine/src/Render/OgreXmlReader.cpp). SYNTHETIC fixtures built in memory, checked against
// hand-computed numbers (BuildOgreXml and BuildOgreMaterial in batchd_fixtures.hpp). Real .mesh.xml files are checked by
// model_corpus. Builds on its own with -DOKAY_STANDALONE_TEST (reader source only).
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "batchd_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include <cmath>
#include <string>
#include <vector>

using namespace okay;

static double PartAreaHelper(const ImportedPart& p) { ImportedModel m; m.nodes.push_back(ImportedNode{}); m.parts.push_back(p); return fx::Area(m); }
static bool HasNote(const ImportedModel& m, const char* what) { for (const std::string& n : m.notes) if (n.find(what) != std::string::npos) return true; return false; }

int main() {
    RUN_SUITE("ogre_reader");
    for (const bool crlf : {false, true}) {
        std::string text = fx::BuildOgreXml();
        if (crlf) { std::string t; for (char c : text) { if (c == '\n') t += "\r\n"; else t += c; } text = t; }
        ImportedModel m; std::string err;
        const std::vector<std::string> scripts{fx::BuildOgreMaterial()};
        CHECK(ReadOgreXmlBuffer(text, "dir/", m, &err, &scripts));
        CHECK(m.backend == "own" && fx::Sane(m));
        CHECK(m.parts.size() == 2 && fx::TriCount(m) == 3 && m.nodes.size() == 1 && m.materials.size() == 2);
        CHECK_NEAR(fx::Area(m), 10.0, 1e-5);
        // submesh 0: shared vertices over two buffers, uv and normals; named from <submeshnames>
        CHECK(m.parts[0].mesh.name == "left" && m.parts[0].mesh.vertices.size() == 4 && m.parts[0].mesh.uvs.size() == 4 && m.parts[0].mesh.normals.size() == 4);
        for (std::size_t i = 0; i < 4; ++i) {
            const Vec3 p = m.parts[0].mesh.vertices[i]; const Vec2 uv = m.parts[0].mesh.uvs[i];
            CHECK(fx::Near(uv.x, p.x / 2) && fx::Near(uv.y, p.y / 2) && fx::Near(m.parts[0].mesh.normals[i].z, 1));
        }
        // submesh 1: its own geometry, no uv, named by its material
        CHECK(m.parts[1].mesh.name == "Mat/B" && m.parts[1].mesh.vertices.size() == 3 && m.parts[1].mesh.uvs.empty());
        CHECK_NEAR(PartAreaHelper(m.parts[1]), 6.0, 1e-5);
        // the material script: first pass colours, specular's shininess, the first texture unit's texture; Mat/B is not in the script
        const ImportedMaterial& a = m.materials[(std::size_t)m.parts[0].material];
        CHECK(a.name == "Mat/A" && a.hasDiffuse && fx::Near(a.diffuse.r, 0.5f) && fx::Near(a.diffuse.g, 0.25f) && fx::Near(a.diffuse.b, 0.75f) && fx::Near(a.diffuse.a, 0.5f));
        CHECK(a.hasEmissive && fx::Near(a.emissive.g, 0.2f) && fx::Near(a.shininess, 32) && a.texture == "dir/tex_a.png");
        const ImportedMaterial& b = m.materials[(std::size_t)m.parts[1].material];
        CHECK(b.name == "Mat/B" && !b.hasDiffuse && b.texture.empty());
        CHECK(HasNote(m, "skeleton 'x.skeleton'") && !HasNote(m, "no .material script"));
    }
    {   // without a script: names only, with a note
        ImportedModel m; std::string err;
        CHECK(ReadOgreXmlBuffer(fx::BuildOgreXml(), "", m, &err) && HasNote(m, "no .material script") && m.materials.size() == 2 && !m.materials[0].hasDiffuse);
    }
    {   // strips and fans are read as the triangles the converter writes; a second texture coordinate set, vertex colours and tangents are named
        std::string s = fx::BuildOgreXml();
        auto rep = [&](const std::string& from, const std::string& to) { const std::size_t at = s.find(from); CHECK(at != std::string::npos); if (at != std::string::npos) s.replace(at, from.size(), to); };
        rep("operationtype=\"triangle_list\"", "operationtype=\"triangle_strip\"");
        rep("<vertex><position x=\"0\" y=\"0\" z=\"0\"/>", "<vertex><colour_diffuse value=\"1 1 1 1\"/><tangent x=\"1\" y=\"0\" z=\"0\"/><position x=\"0\" y=\"0\" z=\"0\"/>");
        rep("<texcoord u=\"0\" v=\"0\"/>", "<texcoord u=\"0\" v=\"0\"/><texcoord u=\"5\" v=\"5\"/>");
        ImportedModel m; std::string err;
        CHECK(ReadOgreXmlBuffer(s, "", m, &err) && fx::TriCount(m) == 3 && HasNote(m, "after the first") && HasNote(m, "vertex colour") && HasNote(m, "tangents"));
        CHECK_NEAR(fx::Area(m), 10.0, 1e-5);
        // first texture coordinate set is the one kept
        bool found = false;
        for (std::size_t i = 0; i < m.parts[0].mesh.vertices.size(); ++i) if (fx::Near(m.parts[0].mesh.vertices[i].x, 0) && fx::Near(m.parts[0].mesh.vertices[i].y, 0)) found = fx::Near(m.parts[0].mesh.uvs[i].x, 0);
        CHECK(found);
    }
    {   // refusals
        ImportedModel m; std::string err;
        const std::string good = fx::BuildOgreXml();
        CHECK(!ReadOgreXmlBuffer("", "", m, &err) && !err.empty());
        CHECK(!ReadOgreXmlBuffer("<scene/>", "", m, &err) && err.find("not an OGRE mesh") != std::string::npos);
        CHECK(!ReadOgreXmlBuffer("<mesh></mesh>", "", m, &err) && err.find("no <submeshes>") != std::string::npos);
        std::string bad = good; bad.replace(bad.find("vertexcount=\"4\""), 15, "vertexcount=\"5\"");
        CHECK(!ReadOgreXmlBuffer(bad, "", m, &err) && err.find("does not match") != std::string::npos);
        bad = good; bad.replace(bad.find("x=\"2\" y=\"0\" z=\"0\""), 17, "x=\"2\" y=\"q\" z=\"0\"");
        CHECK(!ReadOgreXmlBuffer(bad, "", m, &err) && err.find("bad <position>") != std::string::npos);
        bad = good; bad.replace(bad.find("usesharedvertices=\"true\""), 24, "usesharedvertices=\"false\"");
        CHECK(ReadOgreXmlBuffer(bad, "", m, &err) && m.parts.size() == 1 && HasNote(m, "without geometry"));   // submesh 0 now has no geometry of its own
        // an index past the vertex list skips that triangle
        bad = good; bad.replace(bad.find("v3=\"3\""), 6, "v3=\"9\"");
        CHECK(ReadOgreXmlBuffer(bad, "", m, &err) && fx::TriCount(m) == 2 && HasNote(m, "past the vertex list"));
        // line lists are skipped
        bad = good; bad.replace(bad.find("operationtype=\"triangle_list\""), 29, "operationtype=\"line_list\"");
        CHECK(ReadOgreXmlBuffer(bad, "", m, &err) && m.parts.size() == 1 && HasNote(m, "not triangle lists"));
        CHECK(!ReadOgreXmlBuffer("<!DOCTYPE m [<!ENTITY a \"x\">]><mesh/>", "", m, &err));
        for (std::size_t n = 0; n < good.size(); ++n) {
            ImportedModel t; std::string e;
            const bool ok = ReadOgreXmlBuffer(good.substr(0, n), "", t, &e);
            CHECK(ok ? fx::Sane(t) : (!e.empty() && t.parts.empty() && t.nodes.empty()));
        }
    }
    TEST_MAIN_RESULT();
}
