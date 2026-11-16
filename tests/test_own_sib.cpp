// The import seam routes .sib (slot 32) to our own SIB reader, and the one real Silo file we have (examples/own_readers/heffalump.sib
// by Richard Mitton, public domain, with its two pictures) imports through it with the numbers worked out from its bytes by a separate
// script (see docs/own_sib_reader.md): 5 shapes, 676 triangles, 4 materials, a box of x and z from -4.91 to 4.91 and y from -0.023 to 3.165.
// With -DOKAY_OWN_SIB=ON (tests/CMakeLists.txt defines OKAY_EXPECT_OWN_SIB) the reader is the default with nothing set at run time.
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

#ifndef OKAY_SOURCE_DIR
#error "OKAY_SOURCE_DIR is not defined"
#endif
static void SetEnv(const char* n, const char* v) {
#ifdef _WIN32
    _putenv_s(n, v);
#else
    ::setenv(n, v, 1);
#endif
}
static void UnsetEnv(const char* n) {
#ifdef _WIN32
    _putenv_s(n, "");
#else
    ::unsetenv(n);
#endif
}
#ifdef OKAY_EXPECT_OWN_SIB
static const bool kOnByDefault = true;
#else
static const bool kOnByDefault = false;
#endif

int main() {
    RUN_SUITE("own_sib");
    UnsetEnv("OKAY_OWN_SIB");
    const std::string path = (fs::path(OKAY_SOURCE_DIR) / "examples" / "own_readers" / "heffalump.sib").string();
    std::string err;
    CHECK(HasOwnReader(".sib"));
    if (!kOnByDefault) {
        CHECK(EffectiveImportBackend(".sib") != ImportBackend::Own);
        if (!AssimpAvailable()) {
            ImportedModel d; std::string e2;
            CHECK(!ImportModelData(path, d, ImportBackend::Default, false, &e2) && e2.find("OKAY_OWN_SIB") != std::string::npos);
        }
    } else {
        ImportedModel d; std::string e2;
        CHECK(ImportModelData(path, d, ImportBackend::Default, false, &e2) && d.backend == "own");
    }
    SetEnv("OKAY_OWN_SIB", "1");
    CHECK(EffectiveImportBackend(".sib") == ImportBackend::Own);
    CHECK((" " + ImportableExtensions() + " ").find(" .sib ") != std::string::npos);
    ImportedModel m;
    const bool ok = ImportModelData(path, m, ImportBackend::Default, false, &err);
    if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
    CHECK(ok && m.backend == "own" && fx::Sane(m));
    CHECK(m.nodes.size() == 6 && m.nodes[1].name == "Heffalump" && m.nodes[5].name == "Mirrored");
    CHECK(m.materials.size() == 4 && m.materials[0].name == "HeffalumpSkin" && m.materials[1].name == "UpMtl" && m.materials[2].name == "Mapping" && m.materials[3].name == "Ground");
    CHECK(m.materials[1].texture.size() > 15 && m.materials[1].texture.find("This Way Up.png") != std::string::npos && fs::exists(m.materials[1].texture));
    CHECK(m.materials[2].texture.find("UV Mapping.png") != std::string::npos && fs::exists(m.materials[2].texture) && m.materials[0].texture.empty());
    CHECK(m.parts.size() == 6 && fx::TriCount(m) == 676);
    CHECK(std::fabs(fx::Area(m) - 158.5977) < 0.5);   // fan area of the faces from the bytes; ear clipping may differ a little on a bent concave face
    float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
    for (const ImportedPart& p : m.parts) for (const Vec3& v : p.mesh.vertices) { lo[0] = std::min(lo[0], v.x); lo[1] = std::min(lo[1], v.y); lo[2] = std::min(lo[2], v.z); hi[0] = std::max(hi[0], v.x); hi[1] = std::max(hi[1], v.y); hi[2] = std::max(hi[2], v.z); }
    CHECK(std::fabs(lo[0] + 4.91f) < 0.01f && std::fabs(hi[0] - 4.91f) < 0.01f && std::fabs(lo[1] + 0.023f) < 0.01f && std::fabs(hi[1] - 3.165f) < 0.01f && std::fabs(lo[2] + 4.91f) < 0.01f && std::fabs(hi[2] - 4.91f) < 0.01f);
    bool note = false;
    for (const std::string& n : m.notes) if (n.find("4 instance(s)") != std::string::npos) note = true;
    CHECK(note);
    // the closed shapes face outward: the Heffalump (shape 0) has a positive signed volume
    {
        double vol = 0;
        const ImportedPart& p = m.parts[0];
        for (std::size_t i = 0; i + 2 < p.mesh.triangles.size(); i += 3) {
            const Vec3 &a = p.mesh.vertices[(std::size_t)p.mesh.triangles[i]], &b = p.mesh.vertices[(std::size_t)p.mesh.triangles[i + 1]], &c = p.mesh.vertices[(std::size_t)p.mesh.triangles[i + 2]];
            vol += ((double)a.x * ((double)b.y * c.z - (double)b.z * c.y) - (double)a.y * ((double)b.x * c.z - (double)b.z * c.x) + (double)a.z * ((double)b.x * c.y - (double)b.y * c.x)) / 6.0;
        }
        CHECK(vol > 16.0 && vol < 16.3);   // 16.157 from the bytes
    }
    // flatten and the off switch
    ImportedModel f;
    CHECK(ImportModelData(path, f, ImportBackend::Own, true, &err) && f.nodes.size() == 1 && fx::TriCount(f) == 676);
    SetEnv("OKAY_OWN_SIB", "0");
    CHECK(EffectiveImportBackend(".sib") != ImportBackend::Own);
    UnsetEnv("OKAY_OWN_SIB");
    TEST_MAIN_RESULT();
}
