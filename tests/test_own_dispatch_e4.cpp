// The import seam routes .ogex (slot 37) and .mdl (slot 38) to their own readers. Each fixture (built in memory,
// tests/e4_fixtures.hpp) goes through ImportModelData with the own backend switched on; the model must say backend "own" and
// equal what the format's own Read*File gives directly, so a wrong arm in the dispatch chain fails here. Also checked: the
// environment variables OKAY_OWN_OPENGEX / OKAY_OWN_MDL and SetImportBackend switch a format on and off, ImportableExtensions
// follows, flatten bakes the node tree, a Quake and a Half-Life file both reach the one MDL reader, the 3D GameStudio magics are
// refused with their own message, and the format is off by default (switched off, the message names the switch).
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "e4_fixtures.hpp"
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

// True when the build was configured with -DOKAY_OWN_OPENGEX=ON / -DOKAY_OWN_MDL=ON (tests/CMakeLists.txt then defines OKAY_EXPECT_OWN_<FMT> for this
// test): the format is then ON by default, so the "off by default" checks below must not run for it (they failed on such a build before this was added).
#ifdef OKAY_EXPECT_OWN_OPENGEX
static const bool kOgexOnByDefault = true;
#else
static const bool kOgexOnByDefault = false;
#endif
#ifdef OKAY_EXPECT_OWN_MDL
static const bool kMdlOnByDefault = true;
#else
static const bool kMdlOnByDefault = false;
#endif

struct Case { const char* file; const char* ext; const char* env; std::string bytes; std::size_t tris; bool (*direct)(const std::string&, ImportedModel&, std::string*); bool onByDefault; };
static bool DOg(const std::string& p, ImportedModel& m, std::string* e) { return ReadOgexFile(p, m, e); }
static bool DMd(const std::string& p, ImportedModel& m, std::string* e) { return ReadMdlFile(p, m, e); }

int main() {
    RUN_SUITE("own_dispatch_e4");
    const fs::path dir = fs::temp_directory_path() / "okay_own_dispatch_e4";
    fs::create_directories(dir);
    const Case cases[] = {
        {"t.ogex", ".ogex", "OKAY_OWN_OPENGEX", e4::OgexQuad(), 2, DOg, kOgexOnByDefault},
        {"q.mdl", ".mdl", "OKAY_OWN_MDL", e4::QuakeMdl(), 2, DMd, kMdlOnByDefault},
        {"h.mdl", ".mdl", "OKAY_OWN_MDL", e4::HalfLifeMdl(), 1, DMd, kMdlOnByDefault},
    };
    for (const Case& c : cases) {
        const std::string path = (dir / c.file).string();
        Write(path, c.bytes);
        CHECK(HasOwnReader(c.ext));
        seam::CheckSeamMatchesDirect(path, c.file, c.ext, c.direct, c.tris, true);
        std::string err;
        // flatten: one root node, no skins and no clips, same geometry
        seam::CheckFlattened(path, c.tris);
        CHECK(seam::Listed(c.ext));
        // the format-specific variable decides when nothing was chosen at run time
        seam::CheckEnvSwitch(path, c.ext, c.env);
        if (c.onByDefault) {   // a build with -DOKAY_OWN_<FMT>=ON: ours is the default with nothing set at run time
            CHECK(EffectiveImportBackend(c.ext) == ImportBackend::Own);
            CHECK(seam::Listed(c.ext));
            ImportedModel d; std::string e2;
            CHECK(ImportModelData(path, d, ImportBackend::Default, false, &e2) && d.backend == "own");
        }
        // off by default (the format cannot be read at all, and the message names the switch)
        if (!c.onByDefault) seam::CheckSwitchedOff(path, c.ext, c.env);
    }
    {   // upper case extension; the other formats' readers do not take these files; GameStudio magics get their own message
        SetImportBackend("*", ImportBackend::Own);
        Write(dir / "UP.MDL", e4::QuakeMdl());
        ImportedModel m; std::string err;
        CHECK(ImportModelData((dir / "UP.MDL").string(), m, ImportBackend::Own, false, &err) && m.backend == "own");
        Write(dir / "gs.mdl", "MDL7" + std::string(300, '\0'));
        CHECK(!ImportModelData((dir / "gs.mdl").string(), m, ImportBackend::Own, false, &err) && err.find("3D GameStudio") != std::string::npos);
        Write(dir / "wrong.ogex", e4::QuakeMdl());
        CHECK(!ImportModelData((dir / "wrong.ogex").string(), m, ImportBackend::Own, false, &err) && err.find("OpenGEX") != std::string::npos);
        Write(dir / "wrong2.mdl", e4::OgexQuad());
        CHECK(!ImportModelData((dir / "wrong2.mdl").string(), m, ImportBackend::Own, false, &err) && err.find("MDL") != std::string::npos);
        // a texture path that leaves the model's folder is not passed on
        std::string t = e4::OgexQuad();
        t.replace(t.find("tex/red.png"), 11, "../x.png");
        Write(dir / "esc.ogex", t);
        ImportedModel e;
        CHECK(ImportModelData((dir / "esc.ogex").string(), e, ImportBackend::Own, false, &err) && e.materials[0].texture.empty());
        Write(dir / "ok.ogex", e4::OgexQuad());
        ImportedModel o;
        CHECK(ImportModelData((dir / "ok.ogex").string(), o, ImportBackend::Own, false, &err) && o.materials[0].texture == (dir / "tex/red.png").string());
    }
    SetImportBackend("*", ImportBackend::Default);
    std::error_code ec; fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
