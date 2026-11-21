// Our own Q3D reader (engine/src/Render/Q3dReader.cpp): Quick3D objects and scenes. SYNTHETIC fixture built by hand in memory
// (BuildQ3d in batche2_fixtures.hpp), checked against hand-computed numbers. The four real files of the
// model corpus are checked by model_corpus (docs/content/model_corpus.md). Builds on its own with -DOKAY_STANDALONE_TEST (reader source only).
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "batche2_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include <cmath>
#include <cstring>
#include <string>

using namespace okay;

static bool HasNote(const ImportedModel& m, const char* what) { for (const std::string& n : m.notes) if (n.find(what) != std::string::npos) return true; return false; }

int main() {
    RUN_SUITE("q3d_reader");
    for (bool be : {false, true}) for (bool scene : {false, true}) {
        ImportedModel m; std::string err;
        CHECK(ReadQ3dBuffer(fx2::BuildQ3d(be, scene, true), m, &err));
        if (err.size()) std::fprintf(stderr, "  %s\n", err.c_str());
        CHECK(m.backend == "own" && fx::Sane(m));
        CHECK(m.materials.size() == 2 && m.materials[0].name == "red" && m.materials[1].name == "blue");
        CHECK_NEAR(m.materials[0].diffuse.r, 1.0, 1e-6); CHECK_NEAR(m.materials[0].diffuse.g, 0.0, 1e-6);
        CHECK_NEAR(m.materials[0].diffuse.a, 0.75, 1e-6);   // transparency 0.25
        CHECK_NEAR(m.materials[1].diffuse.b, 1.0, 1e-6); CHECK_NEAR(m.materials[1].diffuse.a, 1.0, 1e-6);
        // one part per material: the quad (2 triangles, 4 vertices) and the triangle
        CHECK(m.parts.size() == 2 && fx::TriCount(m) == 3);
        CHECK(m.parts[0].material == 0 && m.parts[0].mesh.vertices.size() == 4 && m.parts[0].mesh.triangles.size() == 6);
        CHECK(m.parts[1].material == 1 && m.parts[1].mesh.vertices.size() == 3 && m.parts[1].mesh.triangles.size() == 3);
        CHECK_NEAR(fx::Area(m), 2.0, 1e-5);
        // the texture coordinates of the quad are its x and y
        CHECK(m.parts[0].mesh.uvs.size() == 4);
        for (std::size_t i = 0; i < 4; ++i) { CHECK_NEAR(m.parts[0].mesh.uvs[i].x, m.parts[0].mesh.vertices[i].x, 1e-6); CHECK_NEAR(m.parts[0].mesh.uvs[i].y, m.parts[0].mesh.vertices[i].y, 1e-6); }
        // the triangle 0 1 4 has uv indices 0 1 2: (0,0) (1,0) (1,1) on (0,0,0) (1,0,0) (0,0,2)
        CHECK(m.parts[1].mesh.uvs.size() == 3);
        for (std::size_t i = 0; i < 3; ++i) {
            const Vec3 p = m.parts[1].mesh.vertices[i]; const Vec2 t = m.parts[1].mesh.uvs[i];
            if (p.z > 1.5f) { CHECK_NEAR(t.x, 1.0, 1e-6); CHECK_NEAR(t.y, 1.0, 1e-6); }
            else if (p.x > 0.5f) { CHECK_NEAR(t.x, 1.0, 1e-6); CHECK_NEAR(t.y, 0.0, 1e-6); }
            else { CHECK_NEAR(t.x, 0.0, 1e-6); CHECK_NEAR(t.y, 0.0, 1e-6); }
        }
        CHECK(HasNote(m, be ? "big endian" : "little endian"));
        CHECK(HasNote(m, "scene chunk") == scene);
    }
    {   // no textures in the header: the texture coordinate fields are not in the file
        ImportedModel m; std::string err;
        CHECK(ReadQ3dBuffer(fx2::BuildQ3d(false, false, false), m, &err) && fx::TriCount(m) == 3 && m.parts[0].mesh.uvs.empty());
        // the corpus file E-AT-AT.q3o: a texture coordinate count of 230 in a file without textures, and no coordinates after it
        std::string s = fx2::BuildQ3d(false, false, false);
        s[22 + 1 + 4 + 5 * 12 + 4 + 2 * 2 + 7 * 4 + 2 * 4 + 4] = (char)230;
        CHECK(ReadQ3dBuffer(s, m, &err) && fx::TriCount(m) == 3 && m.parts[0].mesh.uvs.empty() && HasNote(m, "no UVs"));
    }
    {   // the corpus file WusonOrange.q3s: signature "quick3Do" but a scene chunk at the end, then more bytes
        std::string s = fx2::BuildQ3d(false, true, true);
        s[7] = 'o';
        s += "quick3Ds";
        ImportedModel m; std::string err;
        CHECK(ReadQ3dBuffer(s, m, &err) && fx::TriCount(m) == 3 && HasNote(m, "scene chunk"));
    }
    {   // transparency 1 (WusonOrange's only material): read as opaque, with a note; 0.25 stays alpha 0.75
        std::string s = fx2::BuildQ3d();
        const std::size_t at = s.find(std::string("red\0", 4)) + 4 + 9 * 4;
        const float one = 1.0f; std::memcpy(&s[at], &one, 4);
        ImportedModel m; std::string err;
        CHECK(ReadQ3dBuffer(s, m, &err) && m.materials.size() == 2);
        CHECK_NEAR(m.materials[0].diffuse.a, 1.0, 1e-6);
        CHECK(HasNote(m, "read as opaque"));
    }
    {   // refusals with a reason
        ImportedModel m; std::string err;
        CHECK(!ReadQ3dBuffer("", m, &err) && err.find("too short") != std::string::npos);
        std::string s = fx2::BuildQ3d();
        s[0] = 'Q';
        CHECK(!ReadQ3dBuffer(s, m, &err) && err.find("signature") != std::string::npos);
        s = fx2::BuildQ3d();
        CHECK(!ReadQ3dBuffer(s.substr(0, s.size() - 3), m, &err));   // texture chunk cut
        s = fx2::BuildQ3d();
        s[10 + 3] = (char)0x80;   // mesh count in the header: huge negative / huge
        CHECK(!ReadQ3dBuffer(s, m, &err));
        s = fx2::BuildQ3d();
        s[22 + 1 + 4 + 5 * 12 + 4 + 2 * 2] = (char)9;   // first vertex index of face 0 = 9 (only 5 vertices): the quad is skipped, the rest read
        CHECK(ReadQ3dBuffer(s, m, &err) && fx::TriCount(m) == 1 && HasNote(m, "out-of-range"));
    }
    TEST_MAIN_RESULT();
}
