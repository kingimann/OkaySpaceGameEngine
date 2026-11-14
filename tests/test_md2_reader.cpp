// Our own MD2 (Quake II) reader (engine/src/Render/Md2Reader.cpp). SYNTHETIC fixtures built in memory, checked against
// hand-computed numbers. Real MD2 files are checked by model_corpus. Builds on its own with -DOKAY_STANDALONE_TEST
// (reader source only).
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "batchc_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include <cmath>
#include <string>

using namespace okay;

int main() {
    RUN_SUITE("md2_reader");
    {   // geometry, uv, winding, node, material (all numbers by hand, see the builder)
        ImportedModel m; std::string err;
        CHECK(ReadMd2Buffer(fx::BuildMd2(), "models/", m, &err));
        CHECK(m.backend == "own");
        CHECK(fx::Sane(m));
        CHECK(m.parts.size() == 1 && fx::TriCount(m) == 2);
        const Mesh& me = m.parts[0].mesh;
        CHECK(me.vertices.size() == 4);   // 4 positions, each with one uv: welded to 4
        CHECK(me.normals.size() == 4 && me.uvs.size() == 4);
        // area: triangle (0,1,2) has edges (1,0,0), (0,2,0): 1; triangle (0,2,3): (0,2,0), (0,0,3): 3
        CHECK_NEAR(fx::Area(m), 4.0, 1e-5);
        // positions: 1*x+10, 2*y+20, 3*z+30 (Quake axes, before the root's Z-up turn)
        float lo = 1e9f, hi = -1e9f;
        for (const Vec3& v : me.vertices) { lo = std::min(lo, v.x); hi = std::max(hi, v.x); }
        CHECK_NEAR(lo, 10.0f, 1e-5f); CHECK_NEAR(hi, 11.0f, 1e-5f);
        // winding is reversed: the first triangle (0,1,2) is (v2,v1,v0) now, its normal is -Z
        const Vec3 a = me.vertices[(std::size_t)me.triangles[0]], b = me.vertices[(std::size_t)me.triangles[1]];
        CHECK_NEAR(a.y, 22.0f, 1e-5f); CHECK_NEAR(a.x, 10.0f, 1e-5f);   // v2
        CHECK_NEAR(b.x, 11.0f, 1e-5f);                                 // v1
        CHECK(me.normals[(std::size_t)me.triangles[1]].z < -0.5f);      // v1 belongs to the first triangle only
        // uv: s/64 and 1 - t/32: v1 has (64,0) -> (1, 1); v2 has (0,32) -> (0, 0)
        CHECK_NEAR(me.uvs[(std::size_t)me.triangles[1]].x, 1.0f, 1e-5f); CHECK_NEAR(me.uvs[(std::size_t)me.triangles[1]].y, 1.0f, 1e-5f);
        CHECK_NEAR(me.uvs[(std::size_t)me.triangles[0]].x, 0.0f, 1e-5f); CHECK_NEAR(me.uvs[(std::size_t)me.triangles[0]].y, 0.0f, 1e-5f);
        // the root turns Z-up into Y-up: -90 degrees about X
        CHECK(m.nodes.size() == 1);
        CHECK_NEAR(m.nodes[0].rotation.x, -0.70710678f, 1e-5f); CHECK_NEAR(m.nodes[0].rotation.w, 0.70710678f, 1e-5f);
        CHECK(m.materials.size() == 1 && m.materials[0].texture == "models/players/male/skin.pcx");
        CHECK(m.parts[0].material == 0 && m.parts[0].node == 0 && m.parts[0].mesh.name == "frame0");
        bool noteFrames = false; for (const std::string& n : m.notes) if (n.find("frames") != std::string::npos) noteFrames = true;
        CHECK(!noteFrames);
    }
    {   // several frames: only the first is read, and it says so
        fx::Md2Opts o; o.numFrames = 3;
        ImportedModel m; std::string err;
        CHECK(ReadMd2Buffer(fx::BuildMd2(o), "", m, &err));
        CHECK_NEAR(fx::Area(m), 4.0, 1e-5);
        bool note = false; for (const std::string& n : m.notes) if (n.find("3 frames") != std::string::npos) note = true;
        CHECK(note);
    }
    {   // no skin: no texture; zero skin size: uv divisor 1
        fx::Md2Opts o; o.skin = false; o.skinW = 0; o.skinH = 0;
        ImportedModel m; std::string err;
        CHECK(ReadMd2Buffer(fx::BuildMd2(o), "", m, &err));
        CHECK(m.materials[0].texture.empty());
        float maxU = 0; for (const Vec2& u : m.parts[0].mesh.uvs) maxU = std::max(maxU, std::fabs(u.x));
        CHECK_NEAR(maxU, 64.0f, 1e-4f);
    }
    {   // an out-of-range vertex index or texture index skips that triangle and says so
        fx::Md2Opts o; o.badVertIdx = 200;
        ImportedModel m; std::string err;
        CHECK(ReadMd2Buffer(fx::BuildMd2(o), "", m, &err) && fx::TriCount(m) == 1);
        bool note = false; for (const std::string& n : m.notes) if (n.find("out-of-range vertex") != std::string::npos) note = true;
        CHECK(note);
        fx::Md2Opts o2; o2.badStIdx = 77;
        ImportedModel m2;
        CHECK(ReadMd2Buffer(fx::BuildMd2(o2), "", m2, &err) && fx::TriCount(m2) == 1);
    }
    {   // refusals
        ImportedModel m; std::string err;
        std::string good = fx::BuildMd2();
        CHECK(!ReadMd2Buffer("", "", m, &err) && !err.empty() && m.parts.empty());
        std::string bad = good; bad[0] = 'X';
        CHECK(!ReadMd2Buffer(bad, "", m, &err) && err.find("IDP2") != std::string::npos);
        bad = good; bad[4] = 9;
        CHECK(!ReadMd2Buffer(bad, "", m, &err) && err.find("version") != std::string::npos);
        bad = good; bad[24] = 0; bad[25] = 0; bad[26] = 0; bad[27] = 0;   // zero vertices
        CHECK(!ReadMd2Buffer(bad, "", m, &err));
        // every truncation fails cleanly or reads fewer things; none crashes
        for (std::size_t n = 0; n < good.size(); ++n) {
            ImportedModel t; std::string e;
            const bool ok = ReadMd2Buffer(good.substr(0, n), "", t, &e);
            CHECK(ok ? fx::Sane(t) : (!e.empty() && t.parts.empty()));
        }
        // NaN scale
        bad = good; const std::size_t fo = 68 + 64 + 16 + 24;
        const std::uint32_t nan = 0x7FC00000u; std::memcpy(&bad[fo], &nan, 4);
        CHECK(!ReadMd2Buffer(bad, "", m, &err) && err.find("finite") != std::string::npos);
    }
    TEST_MAIN_RESULT();
}
