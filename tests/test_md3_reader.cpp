// Our own MD3 (Quake III Arena) reader (engine/src/Render/Md3Reader.cpp). SYNTHETIC fixtures built in memory, checked
// against hand-computed numbers (see BuildMd3 in batchc_fixtures.hpp). Real MD3 files are checked by model_corpus.
// Builds on its own with -DOKAY_STANDALONE_TEST (reader source only).
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "batchc_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include <cmath>
#include <string>

using namespace okay;

int main() {
    RUN_SUITE("md3_reader");
    {
        ImportedModel m; std::string err;
        CHECK(ReadMd3Buffer(fx::BuildMd3(), m, &err));
        CHECK(m.backend == "own" && fx::Sane(m));
        CHECK(m.parts.size() == 1 && fx::TriCount(m) == 1);   // the empty second surface is skipped
        const Mesh& me = m.parts[0].mesh;
        CHECK(me.vertices.size() == 3 && me.normals.size() == 3 && me.uvs.size() == 3);
        CHECK_NEAR(fx::Area(m), 1.0, 1e-5);   // edges (1,0,0) and (0,2,0): the 1/64 units give 1 and 2, so area 1
        CHECK(m.parts[0].mesh.name == "body" && m.materials.size() == 1 && m.materials[0].texture == "models/x/tex.tga");
        // find each vertex by position and check its normal and uv (second frame ignored: positions are the first frame's)
        bool seen[3] = {false, false, false};
        for (std::size_t i = 0; i < me.vertices.size(); ++i) {
            const Vec3 p = me.vertices[i], n = me.normals[i]; const Vec2 u = me.uvs[i];
            if (fx::Near(p.x, 0) && fx::Near(p.y, 0)) { seen[0] = true; CHECK(fx::Near(n.z, 1) && fx::Near(u.x, 0) && fx::Near(u.y, 1)); }       // v flipped: 1 - 0
            else if (fx::Near(p.x, 1) && fx::Near(p.y, 0)) { seen[1] = true; CHECK(fx::Near(n.x, 1) && fx::Near(u.x, 1) && fx::Near(u.y, 1)); }
            else if (fx::Near(p.x, 0) && fx::Near(p.y, 2)) { seen[2] = true; CHECK(fx::Near(n.y, 1) && fx::Near(u.x, 0) && fx::Near(u.y, 0)); }
        }
        CHECK(seen[0] && seen[1] && seen[2]);
        // winding reversed: the file's (0,1,2) is (0,2,1): the geometric normal of that triangle is -Z
        const Vec3 a = me.vertices[(std::size_t)me.triangles[0]], b = me.vertices[(std::size_t)me.triangles[1]], c = me.vertices[(std::size_t)me.triangles[2]];
        const float nz = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        CHECK(nz < 0);
        // nodes: root with the Z-up turn, then the tag with its position and the 90 degree turn about Z
        CHECK(m.nodes.size() == 2 && m.nodes[0].name == "models/test/m.md3");
        CHECK_NEAR(m.nodes[0].rotation.x, -0.70710678f, 1e-5f);
        CHECK(m.nodes[1].name == "tag_weapon" && m.nodes[1].parent == 0);
        CHECK(fx::Near(m.nodes[1].position.x, 5) && fx::Near(m.nodes[1].position.y, 6) && fx::Near(m.nodes[1].position.z, 7));
        CHECK(fx::Near(m.nodes[1].rotation.z, 0.70710678f) && fx::Near(m.nodes[1].rotation.w, 0.70710678f) && fx::Near(m.nodes[1].rotation.x, 0));
        bool frames = false, tags = false;
        for (const std::string& n : m.notes) { if (n.find("2 frames") != std::string::npos) frames = true; if (n.find("1 tag") != std::string::npos) tags = true; }
        CHECK(frames && tags);
    }
    {   // no tag, no shader, one frame, no second surface
        fx::Md3Opts o; o.tag = false; o.shader = false; o.numFrames = 1; o.emptySurface = false;
        ImportedModel m; std::string err;
        CHECK(ReadMd3Buffer(fx::BuildMd3(o), m, &err) && m.nodes.size() == 1 && m.materials[0].texture.empty());
        for (const std::string& n : m.notes) CHECK(n.find("frames") == std::string::npos);
    }
    {   // refusals and truncations
        ImportedModel m; std::string err;
        const std::string good = fx::BuildMd3();
        CHECK(!ReadMd3Buffer("", m, &err) && !err.empty());
        std::string bad = good; bad[0] = 'X';
        CHECK(!ReadMd3Buffer(bad, m, &err) && err.find("IDP3") != std::string::npos);
        bad = good; bad[4] = 14;
        CHECK(!ReadMd3Buffer(bad, m, &err) && err.find("version") != std::string::npos);
        for (std::size_t n = 0; n < good.size(); ++n) {
            ImportedModel t; std::string e;
            const bool ok = ReadMd3Buffer(good.substr(0, n), t, &e);
            CHECK(ok ? fx::Sane(t) : (!e.empty() && t.parts.empty() && t.nodes.empty()));
        }
        // a skewed tag axis is not a rotation: the tag stays unrotated and the notes say so
        bad = good;
        const std::size_t tagAt = 108 + 56 * 2 + 64 + 12;   // first axis vector of frame 0's tag
        const float two = 2.0f; std::uint32_t u; std::memcpy(&u, &two, 4); std::memcpy(&bad[tagAt + 4], &u, 4);
        CHECK(ReadMd3Buffer(bad, m, &err) && fx::Near(m.nodes[1].rotation.w, 1.0f));
        bool note = false; for (const std::string& n : m.notes) if (n.find("not a rotation") != std::string::npos) note = true;
        CHECK(note);
    }
    TEST_MAIN_RESULT();
}
