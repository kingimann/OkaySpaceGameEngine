// The import seam routes .sib (slot 32) to our own SIB reader: a synthetic file (tests/sib_fixtures.hpp) goes through
// ImportModelData; OKAY_OWN_SIB=1/0 switch it on and off (the CMake default is ON since 2026-11-24, rules 19d / 19e), ImportableExtensions follows, flatten works. With -DOKAY_OWN_SIB=ON
// (tests/CMakeLists.txt defines OKAY_EXPECT_OWN_SIB) the reader is the default with nothing set at run time.
// With OKAY_MODEL_CORPUS set (docs/content/model_corpus.md: the "test" folder of an Assimp checkout, data only) the one real Silo
// file, models/SIB/heffalump.sib (Richard Mitton, public domain; not in this repository), imports with the numbers worked out
// from its bytes by a separate script (docs/content/model_readers.md, section SIB): 5 shapes and 4 instances of the shape
// "Original", 676 + 4 x 12 = 724 triangles, 4 materials, the ground square from -4.91 to 4.91.
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "sib_fixtures.hpp"
#include "own_seam_checks.hpp"
#include "reader_hostile_common.hpp"
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

using seam::SetEnv;
using seam::UnsetEnv;
#ifdef OKAY_EXPECT_OWN_SIB
static const bool kOnByDefault = true;
#else
static const bool kOnByDefault = false;
#endif
using hostile::HasNote;

int main() {
    RUN_SUITE("sib_import");
    UnsetEnv("OKAY_OWN_SIB");
    const fs::path dir = fs::temp_directory_path() / "okay_sib_import";
    fs::create_directories(dir);
    const std::string path = (dir / "cube.sib").string();
    {
        std::ofstream f(path, std::ios::binary);
        const std::string bytes = sibfx::File({sibfx::Matr(0.5f, 0.5f, 0.5f, "Grey", ""), sibfx::Shp(sibfx::Cube())});
        f.write(bytes.data(), (std::streamsize)bytes.size());
    }
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
        CHECK(EffectiveImportBackend(".sib") == ImportBackend::Own);
        CHECK(ImportModelData(path, d, ImportBackend::Default, false, &e2) && d.backend == "own");
        // a file ours refuses: with the default choice the seam hands it to Assimp when the build has it (like every own reader);
        // without Assimp the refusal is ours and says why
        const std::string bad = (dir / "bad.sib").string();
        { std::ofstream w(bad, std::ios::binary); w << "HEAD not a silo file at all"; }
        ImportedModel r; std::string e3;
        const bool read = ImportModelData(bad, r, ImportBackend::Default, false, &e3);
        if (!AssimpAvailable()) CHECK(!read && e3.find("SIB") != std::string::npos);
        else CHECK(read ? r.backend != "own" : !e3.empty());
    }
    SetEnv("OKAY_OWN_SIB", "1");
    CHECK(EffectiveImportBackend(".sib") == ImportBackend::Own);
    CHECK((" " + ImportableExtensions() + " ").find(" .sib ") != std::string::npos);
    {
        ImportedModel m, direct;
        const bool ok = ImportModelData(path, m, ImportBackend::Default, false, &err);
        if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
        CHECK(ok && m.backend == "own" && fx::Sane(m) && m.nodes.size() == 2 && fx::TriCount(m) == 12);
        CHECK(ReadSibFile(path, direct, &err) && direct.parts.size() == m.parts.size() && fx::TriCount(direct) == 12);
        ImportedModel f;
        CHECK(ImportModelData(path, f, ImportBackend::Own, true, &err) && f.nodes.size() == 1 && fx::TriCount(f) == 12);
        // upper case extension, and a file of another format under .sib is refused by the SIB reader
        const std::string up = (dir / "UP.SIB").string();
        fs::copy_file(path, up, fs::copy_options::overwrite_existing);
        ImportedModel u;
        CHECK(ImportModelData(up, u, ImportBackend::Own, false, &err) && u.backend == "own");
        const std::string wrong = (dir / "wrong.sib").string();
        { std::ofstream w(wrong, std::ios::binary); w << "nendo 1.1 not a silo file"; }
        ImportedModel w;
        CHECK(!ImportModelData(wrong, w, ImportBackend::Own, false, &err) && err.find("SIB") != std::string::npos);
    }
    if (const char* c = std::getenv("OKAY_MODEL_CORPUS")) {
        const std::string real = std::string(c) + "/models/SIB/heffalump.sib";
        if (!fs::exists(real)) std::fprintf(stderr, "  %s: not found, skipped\n", real.c_str());
        else {
            ImportedModel m;
            const bool ok = ImportModelData(real, m, ImportBackend::Default, false, &err);
            if (!ok) std::fprintf(stderr, "  %s\n", err.c_str());
            CHECK(ok && m.backend == "own" && fx::Sane(m));
            CHECK(m.nodes.size() == 10 && m.nodes[1].name == "Heffalump" && m.nodes[5].name == "Mirrored" && m.nodes[6].name == "Inst2");
            CHECK(m.materials.size() == 4 && m.materials[0].name == "HeffalumpSkin" && m.materials[1].name == "UpMtl" && m.materials[2].name == "Mapping" && m.materials[3].name == "Ground");
            CHECK(m.materials[1].texture.find("This Way Up.png") != std::string::npos && m.materials[2].texture.find("UV Mapping.png") != std::string::npos && m.materials[0].texture.empty());
            CHECK(m.parts.size() == 14 && fx::TriCount(m) == 724);   // 6 parts of the shapes, 2 for each instance of the two-material cube
            CHECK(HasNote(m, "4 instance(s) placed"));
            // the instances stand on the ground (y = 0) or on each other: Inst1 rests on top of Inst2
            auto box = [&](int node, float lo[3], float hi[3]) {
                for (int k = 0; k < 3; ++k) { lo[k] = 1e9f; hi[k] = -1e9f; }
                for (const ImportedPart& p : m.parts) if (p.node == node) for (const Vec3& v : p.mesh.vertices) { const float q[3] = {v.x, v.y, v.z}; for (int k = 0; k < 3; ++k) { lo[k] = std::min(lo[k], q[k]); hi[k] = std::max(hi[k], q[k]); } }
            };
            float lo[3], hi[3], lo2[3], hi2[3];
            box(6, lo2, hi2);   // Inst2
            box(7, lo, hi);     // Inst1
            CHECK(std::fabs(lo2[1]) < 0.03f && std::fabs(lo[1] - hi2[1]) < 0.03f);
            for (int n : {8, 9}) { box(n, lo, hi); CHECK(std::fabs(lo[1]) < 0.04f); }   // Inst3 and Inst4 on the ground
        }
    }
    SetEnv("OKAY_OWN_SIB", "0");
    CHECK(EffectiveImportBackend(".sib") != ImportBackend::Own);
    UnsetEnv("OKAY_OWN_SIB");
    std::error_code ec; fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
