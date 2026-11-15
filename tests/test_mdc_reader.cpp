// Our own MDC reader (engine/src/Render/MdcReader.cpp). SYNTHETIC fixtures built in memory (tests/batche_fixtures.hpp), checked
// against numbers worked out by hand; the real file (spider.mdc) is checked by test_batche_hostile when OKAY_MODEL_CORPUS is set.
// Builds on its own with -DOKAY_STANDALONE_TEST (reader source only).
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "batche_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include <cmath>
#include <cstdio>
#include <string>

using namespace okay;
namespace mdcoff = fx::mdcoff;

static bool HasNote(const ImportedModel& m, const char* what) { for (const std::string& n : m.notes) if (n.find(what) != std::string::npos) return true; return false; }
static bool Refused(const std::string& bytes, const char* why) {
    ImportedModel m; std::string err;
    const bool ok = ReadMdcBuffer(bytes, m, &err);
    if (ok) { std::fprintf(stderr, "  expected a refusal (%s) but the file was read\n", why); return false; }
    if (!m.parts.empty() || !m.nodes.empty()) { std::fprintf(stderr, "  refusal left a model behind (%s)\n", why); return false; }
    if (why && err.find(why) == std::string::npos) { std::fprintf(stderr, "  refusal message \"%s\" does not mention \"%s\"\n", err.c_str(), why); return false; }
    return true;
}

int main() {
    RUN_SUITE("mdc_reader");
    {   // the plain fixture: frame 0 comes from base frame 0 with no compressed frame
        ImportedModel m; std::string err;
        CHECK(ReadMdcBuffer(fx::BuildMdc(), m, &err) && fx::Sane(m));
        CHECK(m.backend == "own" && m.parts.size() == 1 && m.materials.size() == 1 && m.nodes.size() == 3 && m.clips.empty());
        const Mesh& me = m.parts[0].mesh;
        CHECK(me.name == "quad" && me.vertices.size() == 4 && me.triangles.size() == 6);
        const float want[4][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
        for (int i = 0; i < 4; ++i) CHECK(fx::Near(me.vertices[(std::size_t)i].x, want[i][0]) && fx::Near(me.vertices[(std::size_t)i].y, want[i][1]) && fx::Near(me.vertices[(std::size_t)i].z, want[i][2]));
        const int tris[6] = {0, 1, 2, 0, 2, 3};   // the file stores (0,2,1) and (0,3,2): clockwise; the reader flips them
        for (int i = 0; i < 6; ++i) CHECK(me.triangles[(std::size_t)i] == tris[i]);
        const float wantUv[4][2] = {{0, 0}, {0.5f, 0}, {0.5f, 0.25f}, {0, 0.25f}};
        CHECK(me.uvs.size() == 4);
        for (int i = 0; i < 4; ++i) CHECK(fx::Near(me.uvs[(std::size_t)i].x, wantUv[i][0]) && fx::Near(me.uvs[(std::size_t)i].y, wantUv[i][1]));
        CHECK(me.normals.size() == 4);
        for (const Vec3& n : me.normals) CHECK(fx::Near(n.x, 0) && fx::Near(n.y, 0) && fx::Near(n.z, 1));   // the packed normal 0 is straight up
        CHECK_NEAR(fx::Area(m), 1.0, 1e-5);
        CHECK(m.materials[0].texture == "textures/base/a.tga" && m.materials[0].hasDiffuse && m.parts[0].material == 0 && m.parts[0].node == 0);
        // the root turns Z-up into Y-up (-90 degrees about X); the tags are children at their FIRST frame position
        const float h = 0.70710678f;
        CHECK(m.nodes[0].name == "testmodel" && fx::Near(m.nodes[0].rotation.x, -h) && fx::Near(m.nodes[0].rotation.w, h) && fx::Near(m.nodes[0].rotation.y, 0));
        CHECK(m.nodes[1].name == "tag_head" && m.nodes[1].parent == 0 && fx::Near(m.nodes[1].position.x, 1) && fx::Near(m.nodes[1].position.y, 2) && fx::Near(m.nodes[1].position.z, 3));
        CHECK(m.nodes[2].name == "tag_weapon" && fx::Near(m.nodes[2].position.x, -0.5f) && fx::Near(m.nodes[2].position.y, 0) && fx::Near(m.nodes[2].position.z, 0.25f));
        CHECK(HasNote(m, "3 frames") && HasNote(m, "2 tag(s)") && HasNote(m, "shader names"));
    }
    {   // a shader name that climbs out or is absolute loses those parts (no file is opened, but the name stays inside)
        for (const char* in : {"../../etc/passwd", "/abs/x.tga", "a/../../b.tga", "C:\\\\x\\y.tga"}) {
            fx::MdcSpec sp; sp.shader = in;
            ImportedModel m; std::string err;
            CHECK(ReadMdcBuffer(fx::BuildMdc(sp), m, &err) && m.materials.size() == 1);
            const std::string t = m.materials[0].texture;
            CHECK(t.find("..") == std::string::npos && (t.empty() || t[0] != '/') && HasNote(m, "segments"));
        }
        fx::MdcSpec sp; sp.shader = "../../etc/passwd";
        ImportedModel m; std::string err;
        CHECK(ReadMdcBuffer(fx::BuildMdc(sp), m, &err) && m.materials[0].texture == "etc/passwd");
    }
    {   // frame 0 is a compressed frame: its offsets (units of 0.05, 127 = zero) move the base vertices
        fx::MdcSpec sp; sp.compFirst = true;
        ImportedModel m; std::string err;
        CHECK(ReadMdcBuffer(fx::BuildMdc(sp), m, &err) && fx::Sane(m) && m.parts.size() == 1);
        const Mesh& me = m.parts[0].mesh;
        const float want[4][3] = {{0.5f, 0, 0}, {1, -1, 0}, {1, 1, 0.1f}, {0, 1, 0}};
        CHECK(me.vertices.size() == 4);
        for (int i = 0; i < 4; ++i) CHECK(fx::Near(me.vertices[(std::size_t)i].x, want[i][0]) && fx::Near(me.vertices[(std::size_t)i].y, want[i][1]) && fx::Near(me.vertices[(std::size_t)i].z, want[i][2]));
        for (const Vec3& n : me.normals) CHECK(fx::Near(n.Magnitude(), 1.0f, 1e-3f) && n.z > 0);   // computed from the moved geometry, all face up
        CHECK(HasNote(m, "compressed frame"));
    }
    {   // two surfaces: two parts, two materials, a texture each
        fx::MdcSpec sp; sp.secondSurface = true;
        ImportedModel m; std::string err;
        CHECK(ReadMdcBuffer(fx::BuildMdc(sp), m, &err) && fx::Sane(m) && m.parts.size() == 2 && m.materials.size() == 2 && fx::TriCount(m) == 3);
        CHECK(m.parts[1].mesh.name == "tri" && m.materials[1].texture == "models/b" && m.parts[1].material == 1);
        CHECK(fx::Near(m.parts[1].mesh.vertices[2].z, 1.0f));
    }
    {   // one frame, no tags
        fx::MdcSpec sp; sp.frames = 1; sp.tags = 0;
        ImportedModel m; std::string err;
        CHECK(ReadMdcBuffer(fx::BuildMdc(sp), m, &err) && m.nodes.size() == 1 && !HasNote(m, "frames of vertex"));
    }
    // ---- refusals ----
    {
        const std::string good = fx::BuildMdc();
        CHECK(Refused("", "too short"));
        CHECK(Refused(good.substr(0, 111), "too short"));
        { std::string s = good; s[0] = 'X'; CHECK(Refused(s, "IDPC")); }
        { std::string s = good; s[1] = 'P'; s[2] = '3'; s[3] = 'D'; CHECK(Refused(s, "IDPC")); }   // an MD3 file is not an MDC file
        { std::string s = good; fx::Put32(s, 4, 3); CHECK(Refused(s, "version")); }
        { std::string s = good; fx::Put32(s, mdcoff::kNumSurfaces, 0); CHECK(Refused(s, "surface count")); }
        { std::string s = good; fx::Put32(s, mdcoff::kNumSurfaces, 0xFFFFFFFFu); CHECK(Refused(s, "surface count")); }
        { std::string s = good; fx::Put32(s, mdcoff::kNumSurfaces, 100000); CHECK(Refused(s, "budget")); }
        { std::string s = good; fx::Put32(s, mdcoff::kNumTags, 0xFFFFFFFFu); CHECK(Refused(s, "tag count")); }
        { std::string s = good; fx::Put32(s, mdcoff::kNumTags, 5000); CHECK(Refused(s, "budget")); }
        { std::string s = good; fx::Put32(s, mdcoff::kNumFrames, 0); CHECK(Refused(s, "frame count")); }
        { std::string s = good; fx::Put32(s, mdcoff::kNumFrames, 0x7FFFFFFFu); CHECK(Refused(s, "budget")); }
        { std::string s = good; fx::Put32(s, mdcoff::kNumFrames, 65000); CHECK(Refused(s, "frame table")); }   // inside the budget, but the file is not that long
        { std::string s = good; fx::Put32(s, mdcoff::kOfsEnd, (std::uint32_t)good.size() + 1); CHECK(Refused(s, "end offset")); }
        { std::string s = good; fx::Put32(s, mdcoff::kOfsSurfaces, 0x7FFFFFFFu); CHECK(Refused(s, "surface")); }
        { std::string s = good; fx::Put32(s, mdcoff::kOfsSurfaces, 0xFFFFFFF0u); CHECK(Refused(s, "offset")); }
        { std::string s = good; fx::Put32(s, mdcoff::kOfsTags, 0x7FFFFFF0u); CHECK(Refused(s, "tag")); }
        { std::string s = good; fx::Put32(s, mdcoff::kOfsTagNames, 0x7FFFFFF0u); CHECK(Refused(s, "tag")); }
        const std::size_t sf = mdcoff::Surface(good);
        { std::string s = good; fx::Put32(s, sf + mdcoff::sNumVerts, 0xFFFFFFFFu); CHECK(Refused(s, "negative")); }
        { std::string s = good; fx::Put32(s, sf + mdcoff::sNumVerts, 500001); CHECK(Refused(s, "vertex budget")); }
        { std::string s = good; fx::Put32(s, sf + mdcoff::sNumTris, 500001); CHECK(Refused(s, "triangle budget")); }
        { std::string s = good; fx::Put32(s, sf + mdcoff::sNumVerts, 400000); CHECK(Refused(s, "vertices of surface")); }   // inside the budget, past the end of the file
        { std::string s = good; fx::Put32(s, sf + mdcoff::sNumTris, 400000); CHECK(Refused(s, "triangles of surface")); }
        { std::string s = good; fx::Put32(s, sf + mdcoff::sNumBase, 0); CHECK(Refused(s, "no base frame")); }
        { std::string s = good; fx::Put32(s, sf + mdcoff::sNumBase, 0x40000000u); CHECK(Refused(s, "vertices of surface")); }   // numBase * numVerts * 8 must not overflow into "fits"
        { std::string s = good; fx::Put32(s, sf + mdcoff::sOfsTris, 0x7FFFFFFFu); CHECK(Refused(s, "triangles of surface")); }
        { std::string s = good; fx::Put32(s, sf + mdcoff::sOfsTris, 4); CHECK(Refused(s, "table offset")); }   // inside the surface header
        { std::string s = good; fx::Put32(s, sf + mdcoff::sOfsEnd, 0); CHECK(Refused(s, "end offset")); }
        { std::string s = good; fx::Put32(s, sf + mdcoff::sOfsSt, 0x7FFFFFFFu); CHECK(Refused(s, "texture coordinates")); }
        { std::string s = good; fx::Put32(s, sf + mdcoff::sOfsXyzN, 0x7FFFFFFFu); CHECK(Refused(s, "vertices of surface")); }
        { std::string s = good; fx::Put32(s, sf + mdcoff::sOfsFrBase, 0x7FFFFFFFu); CHECK(Refused(s, "frame table")); }
        { std::string s = good; fx::Put32(s, sf + mdcoff::sOfsFrComp, 0x7FFFFFFFu); CHECK(Refused(s, "compressed-frame table")); }
        // a frame table that names a frame that does not exist
        { std::string s = good; const std::size_t at = sf + fx::Get32(good, sf + mdcoff::sOfsFrBase); s[at] = 5; CHECK(Refused(s, "base frame that does not exist")); }
        { std::string s = good; const std::size_t at = sf + fx::Get32(good, sf + mdcoff::sOfsFrComp); s[at] = 77; s[at + 1] = 0; CHECK(Refused(s, "compressed frame that does not exist")); }
        { std::string s = good; const std::size_t at = sf + fx::Get32(good, sf + mdcoff::sOfsFrComp); s[at] = (char)0xFE; s[at + 1] = (char)0xFF; CHECK(Refused(s, "compressed frame that does not exist")); }   // -2
        // a triangle with a vertex number past the vertex list is skipped, and if none is left the file is refused
        {
            std::string s = good; const std::size_t at = sf + fx::Get32(good, sf + mdcoff::sOfsTris);
            fx::Put32(s, at, 9);
            ImportedModel m; std::string err;
            CHECK(ReadMdcBuffer(s, m, &err) && fx::TriCount(m) == 1 && HasNote(m, "out-of-range"));
            fx::Put32(s, at + 12, 9);
            CHECK(Refused(s, "no triangles"));
        }
        // a surface chain that points back at itself cannot loop: the end offset must move forward
        { std::string s = good; fx::Put32(s, sf + mdcoff::sOfsEnd, 124); fx::Put32(s, mdcoff::kNumSurfaces, 1000); CHECK(Refused(s, "")); }
        // every truncation and every single byte set to 0xFF or 0x00 in the two headers: refused or sane, never a crash
        for (std::size_t n = 0; n < good.size(); ++n) { ImportedModel m; std::string err; const bool ok = ReadMdcBuffer(good.substr(0, n), m, &err); CHECK(ok ? fx::Sane(m) : (!err.empty() && m.parts.empty() && m.nodes.empty())); }
        for (std::size_t i = 0; i < 112 + 0 && i < good.size(); ++i)
            for (int v : {0x00, 0xFF, 0x80}) { std::string s = good; s[i] = (char)v; ImportedModel m; std::string err; const bool ok = ReadMdcBuffer(s, m, &err); CHECK(ok ? fx::Sane(m) : (!err.empty() && m.parts.empty() && m.nodes.empty())); }
        for (std::size_t i = sf; i < sf + 124; ++i)
            for (int v : {0x00, 0xFF, 0x80}) { std::string s = good; s[i] = (char)v; ImportedModel m; std::string err; const bool ok = ReadMdcBuffer(s, m, &err); CHECK(ok ? fx::Sane(m) : (!err.empty() && m.parts.empty() && m.nodes.empty())); }
    }
    TEST_MAIN_RESULT();
}
