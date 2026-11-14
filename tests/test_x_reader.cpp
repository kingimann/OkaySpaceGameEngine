// Our own DirectX .x reader (engine/src/Render/XFileReader.cpp). SYNTHETIC fixtures built in memory (text, binary with 32
// and 64 bit floats, MSZIP compressed), checked against hand-computed numbers (see BuildXText in batchc_fixtures.hpp).
// Real .x files are checked by model_corpus and against Assimp. Builds on its own with -DOKAY_STANDALONE_TEST (reader
// source and the engine's inflate).
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "batchc_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include <cmath>
#include <string>

using namespace okay;
static bool HasNote(const ImportedModel& m, const char* what) { for (const std::string& n : m.notes) if (n.find(what) != std::string::npos) return true; return false; }

int main() {
    RUN_SUITE("x_reader");
    const float h = 0.70710678f;
    {
        ImportedModel m; std::string err;
        CHECK(ReadXBuffer(fx::BuildXText(), "dir/", m, &err));
        CHECK(m.backend == "own" && fx::Sane(m));
        // nodes: Root with the translation (1,2,3) mirrored to (1,2,-3), Bone1 (0,1,4) -> (0,1,-4), Bone2 with scale 2 at (1,0,0)
        CHECK(m.nodes.size() == 3 && m.nodes[0].name == "Root" && m.nodes[1].name == "Bone1" && m.nodes[2].name == "Bone2" && m.nodes[1].parent == 0 && m.nodes[2].parent == 0);
        CHECK(fx::Near(m.nodes[0].position.x, 1) && fx::Near(m.nodes[0].position.y, 2) && fx::Near(m.nodes[0].position.z, -3));
        CHECK(fx::Near(m.nodes[1].position.y, 1) && fx::Near(m.nodes[1].position.z, -4));
        CHECK(fx::Near(m.nodes[2].scale.x, 2) && fx::Near(m.nodes[2].scale.y, 2) && fx::Near(m.nodes[2].scale.z, 2) && fx::Near(m.nodes[2].position.x, 1) && fx::Near(m.nodes[2].rotation.w, 1));
        // materials: Red (diffuse, alpha 0.5, power 20, emissive, texture joined with the folder), Green
        CHECK(m.materials.size() == 2 && m.materials[0].name == "Red" && m.materials[1].name == "Green");
        CHECK(fx::Near(m.materials[0].diffuse.r, 1) && fx::Near(m.materials[0].diffuse.g, 0.5f) && fx::Near(m.materials[0].diffuse.b, 0.25f) && fx::Near(m.materials[0].diffuse.a, 0.5f));
        CHECK(fx::Near(m.materials[0].shininess, 20) && m.materials[0].hasEmissive && fx::Near(m.materials[0].emissive.r, 0.4f) && m.materials[0].texture == "dir/tex/a.png");
        CHECK(fx::Near(m.materials[1].diffuse.g, 1) && fx::Near(m.materials[1].shininess, 5) && !m.materials[1].hasEmissive);
        // two parts, 3 triangles, area 3 + 6 = 9; the degenerate and the bad face are skipped
        CHECK(m.parts.size() == 2 && fx::TriCount(m) == 3);
        CHECK_NEAR(fx::Area(m), 9.0, 1e-4);
        CHECK(m.parts[0].material == 0 && m.parts[1].material == 1 && m.parts[0].node == 0 && m.parts[0].mesh.name == "Root");
        CHECK(m.parts[0].mesh.vertices.size() == 3 && m.parts[1].mesh.vertices.size() == 4);
        // normals: the triangle's (0,0,1) is mirrored to (0,0,-1), the quad's (0,0,-1) to (0,0,1); uv as written
        for (const Vec3& n : m.parts[0].mesh.normals) CHECK(fx::Near(n.z, -1));
        for (const Vec3& n : m.parts[1].mesh.normals) CHECK(fx::Near(n.z, 1));
        for (std::size_t i = 0; i < 3; ++i) { const Vec3 v = m.parts[0].mesh.vertices[i]; const Vec2 u = m.parts[0].mesh.uvs[i]; if (fx::Near(v.x, 2) && fx::Near(v.y, 3)) CHECK(fx::Near(u.x, 1) && fx::Near(u.y, 1)); }
        // winding reversed: the geometric normal of the first triangle (0,1,2) is (0,0,-1) like its stored normal
        { const ImportedPart& p = m.parts[0]; const Vec3 a = p.mesh.vertices[(std::size_t)p.mesh.triangles[0]], b = p.mesh.vertices[(std::size_t)p.mesh.triangles[1]], c = p.mesh.vertices[(std::size_t)p.mesh.triangles[2]];
          CHECK((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x) < 0); }
        // skin: bones Bone1 (0) and Bone2 (1); v0: Bone1 1.0; v1: 0.5 / 0.5; v2: 0.25 -> 1.0; the quad's vertex 3 has no weight: the root bone
        {
            const ImportedPart& p = m.parts[0];
            CHECK(p.bones.size() == 2 && p.bones[0].name == "Bone1" && p.bones[1].name == "Bone2" && p.jointIdx.size() == 3);
            for (std::size_t i = 0; i < 3; ++i) {
                const Vec3 v = p.mesh.vertices[i];
                if (fx::Near(v.x, 0)) CHECK(p.jointIdx[i][0] == 0 && fx::Near(p.jointWt[i][0], 1));
                if (fx::Near(v.x, 2) && fx::Near(v.y, 0)) CHECK(fx::Near(p.jointWt[i][0], 0.5f) && fx::Near(p.jointWt[i][1], 0.5f) && p.jointIdx[i][0] != p.jointIdx[i][1]);
                if (fx::Near(v.y, 3)) CHECK(p.jointIdx[i][0] == 0 && fx::Near(p.jointWt[i][0], 1) && fx::Near(p.jointWt[i][1], 0));
            }
            const Vec3 ib = p.bones[0].inverseBind.MultiplyPoint(Vec3{0, 0, 0});   // the offset matrix translation (0,-1,-4), mirrored: (0,-1,4)
            CHECK(fx::Near(ib.x, 0) && fx::Near(ib.y, -1) && fx::Near(ib.z, 4));
            const ImportedPart& q = m.parts[1];
            CHECK(q.bones.size() == 3 && q.bones[2].name == "Root" && fx::Near(q.bones[2].inverseBind.MultiplyPoint(Vec3{1, 2, 3}).x, 1));
            int onRoot = 0; for (const auto& ji : q.jointIdx) if (ji[0] == 2) ++onRoot;
            CHECK(onRoot == 1);
        }
        // animation: 30 ticks per second; Bone1 position keys at 0 s and 1 s (z mirrored), rotation (h 0 h 0) -> (0 h 0 h); Bone2 one matrix key
        CHECK(m.clips.size() == 1 && m.clips[0].name == "Walk" && m.clips[0].channels.size() == 2);
        {
            const ImportedChannel& c = m.clips[0].channels[0];
            CHECK(c.node == "Bone1" && c.position.size() == 2 && c.rotation.size() == 2 && c.scale.empty());
            CHECK(fx::Near(c.position[1].time, 1.0f) && fx::Near(c.position[1].value.y, 2) && fx::Near(c.position[1].value.z, -4));
            CHECK(fx::Near(c.rotation[0].value.w, 1) && fx::Near(c.rotation[1].value.y, h) && fx::Near(c.rotation[1].value.w, h) && fx::Near(c.rotation[1].value.x, 0) && fx::Near(c.rotation[1].value.z, 0));
            const ImportedChannel& d = m.clips[0].channels[1];
            CHECK(d.node == "Bone2" && d.position.size() == 1 && d.scale.size() == 1 && d.rotation.size() == 1);
            CHECK(fx::Near(d.position[0].value.x, 1) && fx::Near(d.scale[0].value.x, 2) && fx::Near(d.rotation[0].value.w, 1));
        }
        CHECK(HasNote(m, "left-handed") && HasNote(m, "out-of-range") == false && HasNote(m, "naming a missing vertex") && HasNote(m, "repeat a vertex"));
    }
    {   // two top-level frames get a new root
        fx::XOpts o; o.twoTop = true;
        ImportedModel m; std::string err;
        CHECK(ReadXBuffer(fx::BuildXText(o), "", m, &err) && m.nodes[0].name == "X" && m.nodes[1].name == "Root" && m.nodes[1].parent == 0 && m.nodes[4].name == "Other" && m.nodes[4].parent == 0);
        CHECK(HasNote(m, "new root"));
    }
    for (bool f64 : {false, true}) for (bool sep : {true, false}) {   // binary, 32 and 64 bit floats, with and without separator tokens
        ImportedModel m; std::string err;
        CHECK(ReadXBuffer(fx::BuildXBin(f64, sep), "", m, &err));
        CHECK(fx::Sane(m) && m.nodes.size() == 1 && m.nodes[0].name == "Root" && fx::Near(m.nodes[0].position.z, -3) && fx::TriCount(m) == 1);
        CHECK_NEAR(fx::Area(m), 0.5, 1e-5);
    }
    {   // MSZIP: one block, and two blocks where the second needs the first as its dictionary
        ImportedModel m; std::string err;
        CHECK(ReadXBuffer(fx::BuildXTzip(true), "", m, &err) && fx::TriCount(m) == 1);
        CHECK_NEAR(fx::Area(m), 0.5, 1e-5);
        ImportedModel n;
        CHECK(!ReadXBuffer(fx::BuildXTzip(false), "", n, &err));   // without the second block the frame is not closed
    }
    {   // refusals and truncations
        ImportedModel m; std::string err;
        const std::string good = fx::BuildXText();
        CHECK(!ReadXBuffer("", "", m, &err) && !err.empty());
        std::string bad = good; bad[0] = 'Y';
        CHECK(!ReadXBuffer(bad, "", m, &err) && err.find("xof") != std::string::npos);
        bad = good; bad.replace(12, 4, "0016");
        CHECK(!ReadXBuffer(bad, "", m, &err) && err.find("float size") != std::string::npos);
        bad = good; bad.replace(8, 4, "zzzz");
        CHECK(!ReadXBuffer(bad, "", m, &err) && err.find("unsupported format") != std::string::npos);
        bad = good; bad.replace(bad.find("5;\n  0.0;0.0;0.0;,"), 2, "9;");   // more vertices than the file has numbers for
        CHECK(!ReadXBuffer(bad, "", m, &err));
        bad = good; bad.replace(bad.find("2.0;0.0;0.0;,"), 3, "x.y");
        CHECK(!ReadXBuffer(bad, "", m, &err) && err.find("number") != std::string::npos);
        for (const std::string& f : {good, fx::BuildXBin(false, true), fx::BuildXBin(true, false), fx::BuildXTzip(true)})
            for (std::size_t n = 0; n < f.size(); ++n) {
                ImportedModel t; std::string e;
                const bool ok = ReadXBuffer(f.substr(0, n), "", t, &e);
                CHECK(ok ? fx::Sane(t) : (!e.empty() && t.parts.empty() && t.nodes.empty()));
            }
    }
    TEST_MAIN_RESULT();
}
