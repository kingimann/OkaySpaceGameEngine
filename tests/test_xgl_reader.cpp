// Our own XGL / ZGL reader (engine/src/Render/XglReader.cpp). SYNTHETIC fixtures built in memory, checked against hand-computed
// numbers (BuildXgl in batchd_fixtures.hpp; ZGL is the same text as a zlib stream). Real files are checked by model_corpus.
// Builds on its own with -DOKAY_STANDALONE_TEST (reader source plus the engine's inflate, Core/ImageDecode*.cpp).
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "batchd_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include <cmath>
#include <string>

using namespace okay;

static bool HasNote(const ImportedModel& m, const char* what) { for (const std::string& n : m.notes) if (n.find(what) != std::string::npos) return true; return false; }

static void CheckGood(const ImportedModel& m) {
    CHECK(m.backend == "own" && fx::Sane(m));
    CHECK(m.nodes.size() == 3 && m.parts.size() == 4 && fx::TriCount(m) == 6 && m.materials.size() == 2);
    CHECK_NEAR(fx::WorldArea(m), 16.0, 1e-4);   // two uses of a mesh of area 2, both inside the scale 2 of object A: 2 * 2 * 4
    CHECK(m.nodes[1].name == "A" && m.nodes[2].name == "B" && m.nodes[2].parent == 1 && m.nodes[1].parent == 0);
    // A: forward (1,0,0), up (0,1,0): a turn of 90 degrees about Y; position (5,0,0); scale 2
    const Quat q = m.nodes[1].rotation;
    CHECK(fx::Near(q.x, 0) && fx::Near(q.y, 0.70710678f) && fx::Near(q.z, 0) && fx::Near(q.w, 0.70710678f));
    CHECK(fx::Near(m.nodes[1].position.x, 5) && fx::Near(m.nodes[1].scale.x, 2) && fx::Near(m.nodes[1].scale.z, 2));
    CHECK(fx::Near(m.nodes[2].rotation.w, 1) && fx::Near(m.nodes[2].scale.x, 1));
    // the world material (red, emissive, shine 64, alpha 0.5) and the mesh's own (blue)
    const ImportedMaterial* red = nullptr; const ImportedMaterial* blue = nullptr;
    for (const ImportedMaterial& mt : m.materials) { if (fx::Near(mt.diffuse.r, 1)) red = &mt; if (fx::Near(mt.diffuse.b, 1)) blue = &mt; }
    CHECK(red != nullptr && blue != nullptr);
    if (red) CHECK(red->hasDiffuse && fx::Near(red->diffuse.a, 0.5f) && fx::Near(red->shininess, 64) && red->hasEmissive && fx::Near(red->emissive.g, 0.2f));
    if (blue) CHECK(fx::Near(blue->diffuse.a, 1));
    // the textured, normalled faces keep uv and normals; the untextured face (blue) has none, so its part has no uvs
    int withUv = 0, withoutUv = 0;
    for (const ImportedPart& p : m.parts) { if (!p.mesh.uvs.empty()) ++withUv; else ++withoutUv; CHECK(p.mesh.normals.size() == p.mesh.vertices.size()); for (const Vec3& n : p.mesh.normals) CHECK(fx::Near(n.z, 1)); }
    CHECK(withUv == 2 && withoutUv == 2);
    // A has the turn: its first-face part, seen through A's matrix, has its x axis on -z: the vertex (1,0,0) is at (5, 0, -2)
    bool found = false;
    const Mat4 w = Mat4::TRS(m.nodes[1].position, m.nodes[1].rotation, m.nodes[1].scale);
    for (const ImportedPart& p : m.parts) if (p.node == 1) for (const Vec3& v : p.mesh.vertices) { const Vec3 x = w.MultiplyPoint(v); if (fx::Near(x.x, 5, 1e-4f) && fx::Near(x.y, 0, 1e-4f) && fx::Near(x.z, -2, 1e-4f)) found = true; }
    CHECK(found);
}

int main() {
    RUN_SUITE("xgl_reader");
    {
        ImportedModel m; std::string err;
        CHECK(ReadXglBuffer(fx::BuildXgl(), "", m, &err));
        CheckGood(m);
        CHECK(HasNote(m, "backgrounds"));
    }
    {   // ZGL: the same text as a zlib stream; also with CRLF line ends and a byte order mark in the text
        ImportedModel m; std::string err;
        CHECK(ReadXglBuffer(fx::ToZlibStored(fx::BuildXgl()), "", m, &err));
        CheckGood(m);
        ImportedModel m2;
        CHECK(ReadXglBuffer("\xEF\xBB\xBF" + fx::BuildXgl(), "", m2, &err) && fx::TriCount(m2) == 6);
        // a stream cut short, a bad header, a corrupted block
        const std::string z = fx::ToZlibStored(fx::BuildXgl());
        CHECK(!ReadXglBuffer(z.substr(0, z.size() / 2), "", m2, &err) && !err.empty());
        std::string bad = z; bad[3] = (char)(bad[3] ^ 0x55);
        CHECK(!ReadXglBuffer(bad, "", m2, &err));
    }
    {   // a mesh with objects that do not use it is read once at the root; objects with no mesh add nodes only
        ImportedModel m; std::string err;
        CHECK(ReadXglBuffer("<WORLD><MESH><P ID=\"0\">0,0,0</P><P ID=\"1\">1,0,0</P><P ID=\"2\">0,1,0</P><F><FV1><PREF>0</PREF></FV1><FV2><PREF>1</PREF></FV2><FV3><PREF>2</PREF></FV3></F></MESH></WORLD>", "", m, &err));
        CHECK(m.parts.size() == 1 && fx::TriCount(m) == 1 && m.nodes.size() == 1 && m.materials.empty());
        CHECK(ReadXglBuffer("<WORLD><OBJECT><NAME>e</NAME></OBJECT><OBJECT><MESH><P ID=\"0\">0,0,0</P><P ID=\"1\">1,0,0</P><P ID=\"2\">0,1,0</P><F><FV1><PREF>0</PREF></FV1><FV2><PREF>1</PREF></FV2><FV3><PREF>2</PREF></FV3></F></MESH></OBJECT></WORLD>", "", m, &err));
        CHECK(m.nodes.size() == 3 && m.parts.size() == 1 && m.parts[0].node == 2);
    }
    {   // a quad face (four corners) is fanned; a lower-case id attribute and tag names are accepted; a face without normals or uv gives a part with neither
        ImportedModel m; std::string err;
        CHECK(ReadXglBuffer("<world><mesh><p id=\"3\">0,0,0</p><p id=\"7\">2,0,0</p><p id=\"9\">2,2,0</p><p id=\"11\">0,2,0</p><f><fv1><pref>3</pref></fv1><fv2><pref>7</pref></fv2><fv3><pref>9</pref></fv3><fv4><pref>11</pref></fv4></f></mesh></world>", "", m, &err));
        CHECK(m.parts.size() == 1 && fx::TriCount(m) == 2 && m.parts[0].mesh.uvs.empty() && m.parts[0].mesh.normals.size() == m.parts[0].mesh.vertices.size());   // normals are made when the file gives none
        CHECK_NEAR(fx::Area(m), 4.0, 1e-5);
    }
    {   // refusals
        ImportedModel m; std::string err;
        const std::string good = fx::BuildXgl();
        CHECK(!ReadXglBuffer("", "", m, &err) && !err.empty());
        CHECK(!ReadXglBuffer("<scene/>", "", m, &err) && err.find("not an XGL file") != std::string::npos);
        CHECK(!ReadXglBuffer("<WORLD></WORLD>", "", m, &err) && err.find("no mesh") != std::string::npos);
        std::string bad = good; bad.replace(bad.find("0,0,0</P>"), 9, "0,x,0</P>");
        CHECK(!ReadXglBuffer(bad, "", m, &err) && err.find("bad <P>") != std::string::npos);
        // a face that names a point that does not exist is skipped and counted; the rest reads
        bad = good; bad.replace(bad.find("<PREF>4</PREF>"), 14, "<PREF>9</PREF>");
        CHECK(ReadXglBuffer(bad, "", m, &err) && HasNote(m, "face(s) with a missing") && fx::TriCount(m) == 4);
        CHECK(!ReadXglBuffer("<!DOCTYPE w [<!ENTITY a \"x\">]><WORLD/>", "", m, &err));
        // every truncation (text and ZGL)
        for (const std::string& g : {good, fx::ToZlibStored(good)})
            for (std::size_t n = 0; n < g.size(); n += (g.size() > 4000 ? 1 : 1)) {
                ImportedModel t; std::string e;
                const bool ok = ReadXglBuffer(g.substr(0, n), "", t, &e);
                CHECK(ok ? fx::Sane(t) : (!e.empty() && t.parts.empty() && t.nodes.empty()));
            }
    }
    TEST_MAIN_RESULT();
}
