// Our own HMP reader (engine/src/Render/HmpReader.cpp). SYNTHETIC fixtures built in memory (tests/batche_fixtures.hpp), checked
// against numbers worked out by hand; the two real files are checked by test_batche_hostile when OKAY_MODEL_CORPUS is set.
// Builds on its own with -DOKAY_STANDALONE_TEST (reader source only).
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "batche_fixtures.hpp"
#include "gs_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include <cmath>
#include <cstdio>
#include <string>

using namespace okay;
namespace hmpoff = fx::hmpoff;

static bool HasNote(const ImportedModel& m, const char* what) { for (const std::string& n : m.notes) if (n.find(what) != std::string::npos) return true; return false; }
static bool Refused(const std::string& bytes, const char* why) {
    ImportedModel m; std::string err;
    const bool ok = ReadHmpBuffer(bytes, m, &err);
    if (ok) { std::fprintf(stderr, "  expected a refusal (%s) but the file was read\n", why); return false; }
    if (!m.parts.empty() || !m.nodes.empty()) { std::fprintf(stderr, "  refusal left a model behind (%s)\n", why); return false; }
    if (why && err.find(why) == std::string::npos) { std::fprintf(stderr, "  refusal message \"%s\" does not mention \"%s\"\n", err.c_str(), why); return false; }
    return true;
}

int main() {
    RUN_SUITE("hmp_reader");
    {   // the 3 x 3 plane
        ImportedModel m; std::string err;
        CHECK(ReadHmpBuffer(fx::BuildHmp(), m, &err) && fx::Sane(m));
        CHECK(m.backend == "own" && m.parts.size() == 1 && m.nodes.size() == 1 && m.materials.size() == 1 && m.clips.empty());
        const Mesh& me = m.parts[0].mesh;
        CHECK(me.vertices.size() == 9 && me.triangles.size() == 8 * 3 && me.normals.size() == 9 && me.uvs.size() == 9);
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) {
                const std::size_t i = (std::size_t)(r * 3 + c);
                CHECK(fx::Near(me.vertices[i].x, -1.0f + 2.0f * (float)c) && fx::Near(me.vertices[i].y, -2.0f + 3.0f * (float)r));
                CHECK(fx::Near(me.vertices[i].z, 10.0f + (float)c + 3.0f * (float)r));   // 10 + 0.5 * (2c + 6r)
                CHECK(fx::Near(me.uvs[i].x, (float)c / 2.0f) && fx::Near(me.uvs[i].y, 1.0f - (float)r / 2.0f));
                CHECK(fx::Near(me.normals[i].x, -1.0f / 3.0f, 1e-4f) && fx::Near(me.normals[i].y, -2.0f / 3.0f, 1e-4f) && fx::Near(me.normals[i].z, 2.0f / 3.0f, 1e-4f));
            }
        // the first cell (v0 v1 v4, v0 v4 v3) and the last (v4 v5 v8, v4 v8 v7), counter clockwise seen from above
        const int first[6] = {0, 1, 4, 0, 4, 3};
        for (int i = 0; i < 6; ++i) CHECK(me.triangles[(std::size_t)i] == first[i]);
        const int last[6] = {4, 5, 8, 4, 8, 7};
        for (int i = 0; i < 6; ++i) CHECK(me.triangles[18 + (std::size_t)i] == last[i]);
        // the area of a plane with slopes 0.5 and 1 over a 4 x 6 rectangle: 24 * sqrt(1 + 0.25 + 1) = 36
        CHECK_NEAR(fx::Area(m), 24.0 * std::sqrt(2.25), 1e-3);
        const float h = 0.70710678f;
        CHECK(fx::Near(m.nodes[0].rotation.x, -h) && fx::Near(m.nodes[0].rotation.w, h));   // Z-up to Y-up
        CHECK(HasNote(m, "normals are not read") && !HasNote(m, "skin"));
        // every triangle faces up (+z) before the root turn
        for (std::size_t t = 0; t + 2 < me.triangles.size(); t += 3) {
            const Vec3 a = me.vertices[(std::size_t)me.triangles[t]], b = me.vertices[(std::size_t)me.triangles[t + 1]], c = me.vertices[(std::size_t)me.triangles[t + 2]];
            CHECK((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x) > 0);
        }
    }
    {   // a rectangular grid (4 columns, 2 rows) with free heights: the row length decides the shape
        fx::HmpSpec sp; sp.nx = 4; sp.ny = 2;
        const std::vector<std::uint16_t> hs = {0, 100, 200, 300, 400, 500, 600, 700};
        ImportedModel m; std::string err;
        CHECK(ReadHmpBuffer(fx::BuildHmp(sp, &hs), m, &err) && fx::Sane(m));
        const Mesh& me = m.parts[0].mesh;
        CHECK(me.vertices.size() == 8 && fx::TriCount(m) == 6);
        CHECK(fx::Near(me.vertices[5].x, -1.0f + 2.0f) && fx::Near(me.vertices[5].y, -2.0f + 3.0f) && fx::Near(me.vertices[5].z, 10.0f + 250.0f));
        CHECK(fx::Near(me.vertices[7].x, -1.0f + 6.0f) && fx::Near(me.vertices[7].z, 10.0f + 350.0f));
    }
    {   // the embedded skin is skipped, not read
        fx::HmpSpec sp; sp.skin = true;
        ImportedModel m; std::string err;
        CHECK(ReadHmpBuffer(fx::BuildHmp(sp), m, &err) && fx::Sane(m) && fx::TriCount(m) == 8 && HasNote(m, "skin"));
        CHECK(fx::Near(m.parts[0].mesh.vertices[8].z, 10.0f + 2.0f + 6.0f));
    }
    {   // more frames: the first is read
        fx::HmpSpec sp; sp.frames = 3;
        ImportedModel m; std::string err;
        CHECK(ReadHmpBuffer(fx::BuildHmp(sp), m, &err) && HasNote(m, "3 frames"));
    }
    {   // HMP5 and HMP4 (Conitec's published HMP5 layout; HMP4 keeps the skin size in the header): a 2 x 2 grid
        for (char ver : {'5', '4'}) {
            ImportedModel m; std::string err;
            const bool ok = ReadHmpBuffer(gs::Hmp45(ver), m, &err);
            if (!ok) std::fprintf(stderr, "  HMP%c: %s\n", ver, err.c_str());
            CHECK(ok && fx::Sane(m) && fx::TriCount(m) == 2 && HasNote(m, "skin"));
            if (!ok) continue;
            const Mesh& me = m.parts[0].mesh;
            CHECK(me.vertices.size() == 4);
            CHECK(fx::Near(me.vertices[0].x, -2) && fx::Near(me.vertices[0].y, -2) && fx::Near(me.vertices[0].z, 1));
            CHECK(fx::Near(me.vertices[1].x, 2) && fx::Near(me.vertices[1].z, 2));
            CHECK(fx::Near(me.vertices[2].y, 2) && fx::Near(me.vertices[2].z, 3) && fx::Near(me.vertices[3].z, 4));
            CHECK(HasNote(m, ver == '4' ? "HMP4" : "HMP5"));
        }
        {   // HMP5 skin types: 16 bit, 24 bit and 32 bit, with or without mipmaps (the pixel count decides where the grid starts)
            for (int t : {0, 2, 3, 4, 5, 10, 11, 12, 13}) {
                std::string s = gs::Hmp45('5');
                const int bpp = t == 0 ? 1 : (t & 7) == 4 ? 3 : (t & 7) == 5 ? 4 : 2;
                const std::size_t n = (std::size_t)bpp * 4 + ((t & 8) ? (std::size_t)bpp * 1 : 0);   // 2 x 2, then 1 x 1, 0 x 0, 0 x 0 mipmaps
                std::string g = s.substr(0, 84);
                fx::Put32(s, 84, (std::uint32_t)t);
                g += s.substr(84, 12); g.append(n, (char)1); g += s.substr(84 + 12 + 8);
                ImportedModel m; std::string err;
                const bool ok = ReadHmpBuffer(g, m, &err);
                if (!ok) std::fprintf(stderr, "  HMP5 skin type %d: %s\n", t, err.c_str());
                CHECK(ok && fx::TriCount(m) == 2);
            }
        }
    }
    {   // refusals
        const std::string good = fx::BuildHmp();
        CHECK(Refused("", "too short"));
        CHECK(Refused(good.substr(0, 83), "too short"));
        { std::string s = good; s[0] = 'X'; CHECK(Refused(s, "not an HMP")); }
        { fx::HmpSpec sp; sp.version = '6'; CHECK(Refused(fx::BuildHmp(sp), "HMP4, HMP5 and HMP7")); }
        { fx::HmpSpec sp; sp.version = '3'; CHECK(Refused(fx::BuildHmp(sp), "HMP4, HMP5 and HMP7")); }
        { std::string s = gs::Hmp45('5'); fx::Put32(s, 84, 4); CHECK(Refused(s, "height grid")); }   // HMP5: a 2 x 2 skin of type 4 (3 bytes a pixel) takes 4 bytes more: the grid runs past the end
        { std::string s = gs::Hmp45('4'); fx::Put32(s, 52, 0); CHECK(Refused(s, "skin size")); }   // HMP4: the skin size lives in the header
        { std::string s = good; fx::Put32(s, hmpoff::kNumVerts, 0xFFFFFFFFu); CHECK(Refused(s, "vertex count")); }
        { std::string s = good; fx::Put32(s, hmpoff::kNumVerts, 0x7FFFFFFFu); CHECK(Refused(s, "budget")); }
        { std::string s = good; fx::Put32(s, hmpoff::kNumVerts, 1048577); CHECK(Refused(s, "budget")); }
        { std::string s = good; fx::Put32(s, hmpoff::kNumVerts, 3); CHECK(Refused(s, "vertex count")); }
        { std::string s = good; fx::Put32(s, hmpoff::kNumVerts, 10); CHECK(Refused(s, "multiple of the row length")); }
        { std::string s = good; fx::Put32(s, hmpoff::kNumVerts, 3); fx::Put32(s, hmpoff::kNumVerts, 4); CHECK(Refused(s, "multiple")); }   // 4 points, rows of 3
        { std::string s = good; fx::Put32(s, hmpoff::kNumVerts, 900000); CHECK(Refused(s, "height grid")); }
        { std::string s = good; fx::Put32(s, hmpoff::kNumVerts, 999999); fx::Put32(s, hmpoff::kNumX, 0x3F800000u); CHECK(Refused(s, "grid points per row")); }   // row length 1.0
        { fx::HmpSpec sp; sp.nx = 3; std::string s = good; fx::Put32(s, hmpoff::kNumX, 0x7FC00000u); CHECK(Refused(s, "grid points per row")); }   // NaN
        { std::string s = good; fx::Put32(s, hmpoff::kNumX, 0x7F800000u); CHECK(Refused(s, "grid points per row")); }   // infinity
        { std::string s = good; fx::Put32(s, hmpoff::kNumX, 0x40200000u); CHECK(Refused(s, "grid points per row")); }   // 2.5
        { std::string s = good; fx::Put32(s, hmpoff::kNumX, 0x41100000u); fx::Put32(s, hmpoff::kNumVerts, 9); CHECK(Refused(s, "only one row")); }   // 9.0 per row, 9 points: one row only
        { std::string s = good; fx::Put32(s, hmpoff::kNumX, 0x41100000u); CHECK(Refused(s, "only one row")); }   // 9.0 per row and 9 points: a single row
        { std::string s = good; fx::Put32(s, hmpoff::kNumSkins, 0xFFFFFFFFu); CHECK(Refused(s, "skin count")); }
        { std::string s = good; fx::Put32(s, hmpoff::kNumSkins, 1000); CHECK(Refused(s, "skin count")); }
        { std::string s = good; fx::Put32(s, hmpoff::kNumFrames, 0); CHECK(Refused(s, "frame count")); }
        { fx::HmpSpec sp; sp.skin = true; sp.skinType = 9; CHECK(Refused(fx::BuildHmp(sp), "type 9")); }
        { fx::HmpSpec sp; sp.skin = true; std::string s = fx::BuildHmp(sp); fx::Put32(s, 88, 0x7FFFFFFFu); fx::Put32(s, 92, 0x7FFFFFFFu); CHECK(Refused(s, "bad size")); }   // a skin of 2^62 pixels
        { fx::HmpSpec sp; sp.skin = true; std::string s = fx::BuildHmp(sp); fx::Put32(s, 88, 5000); fx::Put32(s, 92, 5000); CHECK(Refused(s, "ends inside skin")); }
        { fx::HmpSpec sp; sp.skin = true; std::string s = fx::BuildHmp(sp); fx::Put32(s, 88, 0); CHECK(Refused(s, "bad size")); }
        // the float fields: NaN and infinite scale / origin / cell size
        for (std::size_t at : {(std::size_t)8, (std::size_t)16, (std::size_t)20, (std::size_t)28, (std::size_t)36, (std::size_t)40})
            for (std::uint32_t v : {0x7FC00000u, 0x7F800000u, 0xFF800000u, 0x7F7FFFFFu}) { std::string s = good; fx::Put32(s, at, v); CHECK(Refused(s, "")); }
        { std::string s = good; fx::Put32(s, 36, 0); CHECK(Refused(s, "cell size")); }              // 0.0
        { std::string s = good; fx::Put32(s, 36, 0xBF800000u); CHECK(Refused(s, "cell size")); }    // -1.0
        // every truncation (the grid ends inside) and every single byte set to 0xFF / 0x00 / 0x80 in the header
        for (std::size_t n = 0; n < good.size(); ++n) { ImportedModel m; std::string err; const bool ok = ReadHmpBuffer(good.substr(0, n), m, &err); CHECK(ok ? fx::Sane(m) : (!err.empty() && m.parts.empty() && m.nodes.empty())); }
        for (std::size_t i = 0; i < 84; ++i)
            for (int v : {0x00, 0xFF, 0x80}) { std::string s = good; s[i] = (char)v; ImportedModel m; std::string err; const bool ok = ReadHmpBuffer(s, m, &err); CHECK(ok ? fx::Sane(m) : (!err.empty() && m.parts.empty() && m.nodes.empty())); }
    }
    TEST_MAIN_RESULT();
}
