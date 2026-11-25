// The import seam routes .bsp and .pk3 (slot 42) to our own Q3BSP reader; and with -DOKAY_OWN_Q3BSP=ON (tests/CMakeLists.txt then
// defines OKAY_EXPECT_OWN_Q3BSP) the reader is the default with nothing set at run time. SYNTHETIC maps (tests/q3bsp_fixtures.hpp).
// Checked: the model says backend "own" and equals what ReadQ3bspFile gives directly; the environment variable OKAY_OWN_Q3BSP and
// SetImportBackend switch the format on and off; ImportableExtensions follows; flatten bakes the node tree (Z up to Y up) into
// the vertices; the format is off by default (switched off, the message names the switch); a Quake 1 / Half-Life
// style .bsp (other magic) is refused with its own message.
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "q3bsp_fixtures.hpp"
#include "own_seam_checks.hpp"
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

using seam::SetEnv;
using seam::UnsetEnv;
using seam::Write;

#ifdef OKAY_EXPECT_OWN_Q3BSP
static const bool kOnByDefault = true;
#else
static const bool kOnByDefault = false;
#endif

int main() {
    RUN_SUITE("own_q3bsp_dispatch");
    UnsetEnv("OKAY_OWN_Q3BSP");
    const fs::path dir = fs::temp_directory_path() / "okay_own_q3bsp_dispatch";
    fs::create_directories(dir);
    struct Case { const char* file; const char* ext; std::string bytes; };
    const Case cases[] = {
        {"t.bsp", ".bsp", q3::Basic().Build()},
        {"t.pk3", ".pk3", q3::Pk3(q3::Basic().Build())},
    };
    for (const Case& c : cases) {
        const std::string path = (dir / c.file).string();
        Write(path, c.bytes);
        std::string err;
        CHECK(HasOwnReader(c.ext));
        if (!kOnByDefault) {
            // nothing chosen, nothing set: not ours (the file cannot be read at all and the message names the switch)
            CHECK(EffectiveImportBackend(c.ext) != ImportBackend::Own);
            CHECK(!seam::Listed(c.ext));
            {
                ImportedModel d; std::string e2;
                CHECK(!ImportModelData(path, d, ImportBackend::Default, false, &e2) && e2.find("OKAY_OWN_Q3BSP") != std::string::npos);
            }
        } else {
            CHECK(EffectiveImportBackend(c.ext) == ImportBackend::Own);
            ImportedModel d; std::string e2;
            CHECK(ImportModelData(path, d, ImportBackend::Default, false, &e2) && d.backend == "own");
        }
        seam::CheckSeamMatchesDirect(path, c.file, c.ext, [](const std::string& p, ImportedModel& m, std::string* e) { return ReadQ3bspFile(p, m, e); }, 77, false);
        // flatten: one root node, the Z-up turn baked into the vertices: the quad at z = 0 and the mesh face at z = 10 are at y = 0 and y = 10 now
        ImportedModel f;
        CHECK(ImportModelData(path, f, ImportBackend::Own, true, &err) && f.nodes.size() == 1 && fx::TriCount(f) == 77);
        float maxY = -1e9f, minZ = 1e9f;
        for (const ImportedPart& p : f.parts) for (const Vec3& v : p.mesh.vertices) { maxY = std::max(maxY, v.y); minZ = std::min(minZ, v.z); }
        CHECK(maxY > 19.9f && maxY < 20.1f);   // the patch at z = 20 is the highest: y = 20 (the nodraw quad at 30 is left out)
        CHECK(minZ < -63.9f);                                 // y of the file became -z
        CHECK(seam::Listed(c.ext));
        // the environment variable decides when nothing was chosen at run time
        seam::CheckEnvSwitch(path, c.ext, "OKAY_OWN_Q3BSP");
    }
    {   // upper case extension; a file of another BSP family is refused with the reader's message
        SetImportBackend("*", ImportBackend::Own);
        Write(dir / "UP.BSP", q3::Basic().Build());
        ImportedModel m; std::string err;
        CHECK(ImportModelData((dir / "UP.BSP").string(), m, ImportBackend::Own, false, &err) && m.backend == "own");
        Write(dir / "hl.bsp", std::string("\x1e\0\0\0", 4) + std::string(300, '\0'));
        CHECK(!ImportModelData((dir / "hl.bsp").string(), m, ImportBackend::Own, false, &err) && err.find("IBSP") != std::string::npos);
    }
    SetImportBackend("*", ImportBackend::Default);
    std::error_code ec; fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
