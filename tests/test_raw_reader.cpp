// Our own RAW reader (engine/src/Render/RawReader.cpp): plain triangle text. SYNTHETIC fixture built by hand (BuildRaw in
// batche2_fixtures.hpp), hand-computed expectations. No real .raw file is in the repository: unverified against real files.
// Builds on its own with -DOKAY_STANDALONE_TEST.
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "batche2_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include <cmath>
#include <string>

using namespace okay;

static bool HasNote(const ImportedModel& m, const char* what) { for (const std::string& n : m.notes) if (n.find(what) != std::string::npos) return true; return false; }
static double PartArea(const ImportedPart& p) { ImportedModel m; m.nodes.push_back(ImportedNode{}); m.parts.push_back(p); return fx::Area(m); }

int main() {
    RUN_SUITE("raw_reader");
    for (const char* eol : {"\n", "\r\n", "\r"}) {
        ImportedModel m; std::string err;
        CHECK(ReadRawBuffer(fx2::BuildRaw(eol), m, &err));
        CHECK(m.backend == "own" && fx::Sane(m) && m.nodes.size() == 1);
        CHECK(m.parts.size() == 3 && fx::TriCount(m) == 4);
        CHECK(m.parts[0].mesh.name == "RAW" && m.parts[1].mesh.name == "wing" && m.parts[2].mesh.name == "wood.tga");
        CHECK_NEAR(PartArea(m.parts[0]), 0.5, 1e-6);
        CHECK_NEAR(PartArea(m.parts[1]), 2.0, 1e-6);
        CHECK_NEAR(PartArea(m.parts[2]), 4.5 + 2.0, 1e-6);
        CHECK(m.parts[1].mesh.vertices.size() == 3 && m.parts[2].mesh.triangles.size() == 6);
        CHECK(HasNote(m, "colour is not imported"));
    }
    {   // two triangles sharing an edge are welded: 4 vertices, 2 triangles
        ImportedModel m; std::string err;
        CHECK(ReadRawBuffer("0 0 0 1 0 0 0 1 0\n1 0 0 1 1 0 0 1 0\n", m, &err) && m.parts.size() == 1);
        CHECK(m.parts[0].mesh.vertices.size() == 4 && m.parts[0].mesh.triangles.size() == 6);
        CHECK_NEAR(fx::Area(m), 1.0, 1e-6);
    }
    {   // numbers: exponents, signs, decimal point; vertex coordinates exact
        ImportedModel m; std::string err;
        CHECK(ReadRawBuffer("-1.5e0 0 0\n", m, &err) == false);   // 3 numbers: not a triangle
        CHECK(err.find("line 1") != std::string::npos);
        CHECK(ReadRawBuffer("-1.5e0 0 0\t+2 0 0\t0 3E-1 0\n", m, &err));
        bool found = false;
        for (const Vec3& v : m.parts[0].mesh.vertices) if (std::fabs(v.x + 1.5f) < 1e-6f) found = true;
        CHECK(found);
        CHECK_NEAR(fx::Area(m), 0.5 * 3.5 * 0.3, 1e-5);
    }
    {   // refusals with the reason
        ImportedModel m; std::string err;
        CHECK(!ReadRawBuffer("", m, &err) && err.find("no triangles") != std::string::npos);
        CHECK(!ReadRawBuffer("# nothing\nname\n", m, &err) && err.find("no triangles") != std::string::npos);
        CHECK(!ReadRawBuffer("0 0 0 1 0 0 0 1\n", m, &err) && err.find("line 1") != std::string::npos);        // 8 numbers
        CHECK(!ReadRawBuffer("0 0 0 1 0 0 0 1 nan\n", m, &err));                                              // nan is not a number
        CHECK(!ReadRawBuffer("0 0 0 1 0 0 0 1 1e999\n", m, &err));                                            // out of range
        CHECK(!ReadRawBuffer("0 0 0 1 0 0 0 1 0 5\n", m, &err));                                              // ten numbers
        CHECK(!ReadRawBuffer(std::string("0 0 0 1 0 0 0 1 0\0\n", 19), m, &err) && err.find("NUL") != std::string::npos);   // binary
        CHECK(!ReadRawBuffer("0 0 0 0 0 0 0 0 0\n", m, &err) && err.find("degenerate") != std::string::npos);   // only a degenerate triangle
    }
    TEST_MAIN_RESULT();
}
