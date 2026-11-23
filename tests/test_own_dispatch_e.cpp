// The import seam routes every batch-E own-reader format (slots 28 to 30: .mdc .hmp .csm) to ITS reader. Slot 27 belongs to
// Terragen (another branch) and is a reserved, unmapped row of the table here: nothing may reach it.
// Each format's fixture (built in memory, tests/batche_fixtures.hpp) goes through ImportModelData with the own backends switched on;
// the model must say backend "own" and must equal what the format's own Read*File gives directly, so a wrong arm in the dispatch
// chain (a format absorbed by the last arm, or by a neighbour) fails here. Also checked: the environment variable OKAY_OWN_<FMT>
// and SetImportBackend switch a format on and off, ImportableExtensions follows, flatten bakes the node tree, a file of one format
// under another's extension is refused by the reader of that extension, and the extension is matched in any letter case.
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "own_seam_checks.hpp"
#include "batche_fixtures.hpp"
#include "batchc_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

static void TestSetEnv(const char* n, const char* v) {
#ifdef _WIN32
    _putenv_s(n, v);
#else
    ::setenv(n, v, 1);
#endif
}
static void TestUnsetEnv(const char* n) {
#ifdef _WIN32
    _putenv_s(n, "");
#else
    ::unsetenv(n);
#endif
}

static void WriteFile(const fs::path& p, const std::string& s) { std::ofstream f(p, std::ios::binary); f.write(s.data(), (std::streamsize)s.size()); }
static std::size_t Verts(const ImportedModel& m) { std::size_t n = 0; for (const ImportedPart& p : m.parts) n += p.mesh.vertices.size(); return n; }

// tests/CMakeLists.txt defines OKAY_EXPECT_OWN_<FMT> when the build was configured with -DOKAY_OWN_<FMT>=ON.
#ifdef OKAY_EXPECT_OWN_MDC
constexpr bool EXPECT_MDC = true;
#else
constexpr bool EXPECT_MDC = false;
#endif
#ifdef OKAY_EXPECT_OWN_HMP
constexpr bool EXPECT_HMP = true;
#else
constexpr bool EXPECT_HMP = false;
#endif
#ifdef OKAY_EXPECT_OWN_CSM
constexpr bool EXPECT_CSM = true;
#else
constexpr bool EXPECT_CSM = false;
#endif

struct Case { const char* file; const char* ext; const char* env; std::string bytes; std::size_t tris, nodes, clips; bool expectOwn; bool (*direct)(const std::string&, ImportedModel&, std::string*); };
static bool DMdc(const std::string& p, ImportedModel& m, std::string* e) { return ReadMdcFile(p, m, e); }
static bool DHmp(const std::string& p, ImportedModel& m, std::string* e) { return ReadHmpFile(p, m, e); }
static bool DCsm(const std::string& p, ImportedModel& m, std::string* e) { return ReadCsmFile(p, m, e); }

int main() {
    RUN_SUITE("own_dispatch_e");
    const fs::path dir = fs::temp_directory_path() / "okay_own_dispatch_e";
    fs::create_directories(dir);
    const Case cases[] = {
        {"t.mdc", ".mdc", "OKAY_OWN_MDC", fx::BuildMdc(), 2, 3, 0, EXPECT_MDC, DMdc},     // the quad (2 triangles) and two tags as nodes
        {"t.hmp", ".hmp", "OKAY_OWN_HMP", fx::BuildHmp(), 8, 1, 0, EXPECT_HMP, DHmp},     // 3 x 3 grid points: 8 triangles
        {"t.csm", ".csm", "OKAY_OWN_CSM", fx::BuildCsm(), 0, 4, 1, EXPECT_CSM, DCsm},     // three markers and a root; one clip
    };
    // The build default, before anything is switched at run time: with -DOKAY_OWN_<FMT>=ON the own reader must be the default and a
    // file must import through it (this fails if the engine did not get OKAY_OWN_<FMT>_DEFAULT); with the option OFF it must not be.
    for (const Case& c : cases) {
        TestUnsetEnv(c.env);
        const std::string path = (dir / (std::string("default_") + c.file)).string();
        WriteFile(path, c.bytes);
        ImportedModel m; std::string err;
        const bool ok = ImportModelData(path, m, ImportBackend::Default, false, &err);
        if (c.expectOwn) {
            if (!ok) std::fprintf(stderr, "  %s: %s\n", c.file, err.c_str());
            CHECK(EffectiveImportBackend(c.ext) == ImportBackend::Own);
            CHECK(ok && m.backend == "own" && fx::TriCount(m) == c.tris);
        } else {
            CHECK(EffectiveImportBackend(c.ext) != ImportBackend::Own);
            CHECK(!ok || m.backend != "own");
        }
    }
    for (const Case& c : cases) {
        const std::string path = (dir / c.file).string();
        WriteFile(path, c.bytes);
        CHECK(HasOwnReader(c.ext));
        SetImportBackend(c.ext, ImportBackend::Own);
        CHECK(EffectiveImportBackend(c.ext) == ImportBackend::Own);
        ImportedModel a, b; std::string err;
        const bool ok = ImportModelData(path, a, ImportBackend::Default, false, &err);
        if (!ok) std::fprintf(stderr, "  %s: %s\n", c.file, err.c_str());
        CHECK(ok && a.backend == "own");
        CHECK(c.direct(path, b, &err));
        CHECK(a.parts.size() == b.parts.size() && Verts(a) == Verts(b) && a.nodes.size() == b.nodes.size() && a.materials.size() == b.materials.size() && a.clips.size() == b.clips.size());
        CHECK(fx::TriCount(a) == c.tris && fx::TriCount(b) == c.tris && a.nodes.size() == c.nodes && a.clips.size() == c.clips);
        // flatten: one root node, no clips; the geometry is the same (the root's Z-up turn is baked in)
        seam::CheckFlattened(path, c.tris);
        // the extension list follows the switch
        CHECK((" " + ImportableExtensions() + " ").find(std::string(" ") + c.ext + " ") != std::string::npos);
        // the format-specific variable decides when nothing was chosen at run time, and each variable only its own format
        SetImportBackend(c.ext, ImportBackend::Default);
        TestSetEnv(c.env, "1");
        CHECK(EffectiveImportBackend(c.ext) == ImportBackend::Own);
        for (const Case& other : cases) if (&other != &c && !other.expectOwn) CHECK(EffectiveImportBackend(other.ext) != ImportBackend::Own);
        ImportedModel e1;
        CHECK(ImportModelData(path, e1, ImportBackend::Default, false, &err) && e1.backend == "own");
        TestSetEnv(c.env, "0");
        CHECK(EffectiveImportBackend(c.ext) != ImportBackend::Own);
        TestUnsetEnv(c.env);
        // off by default (when the build has no Assimp the format cannot be read at all, and the message names the switch)
        if (!AssimpAvailable() && !c.expectOwn) {
            CHECK(EffectiveImportBackend(c.ext) != ImportBackend::Own);
            CHECK((" " + ImportableExtensions() + " ").find(std::string(" ") + c.ext + " ") == std::string::npos);
            ImportedModel d; std::string e2;
            CHECK(!ImportModelData(path, d, ImportBackend::Default, false, &e2) && e2.find(c.env) != std::string::npos);
        }
    }
    {   // any letter case of the extension reaches the reader
        SetImportBackend("*", ImportBackend::Own);
        for (const Case& c : cases) {
            std::string up = c.ext; for (char& ch : up) ch = (char)std::toupper((unsigned char)ch);
            const std::string path = (dir / (std::string("UP") + up)).string();
            WriteFile(path, c.bytes);
            ImportedModel m; std::string err;
            CHECK(HasOwnReader(up) && ImportModelData(path, m, ImportBackend::Own, false, &err) && m.backend == "own" && fx::TriCount(m) == c.tris);
        }
    }
    // a format is not absorbed by a neighbour's reader: each fixture under another format's extension is refused by THAT reader
    {
        SetImportBackend("*", ImportBackend::Own);
        const struct { const char* ext; const char* name; std::string bytes; } wrong[] = {
            {".mdc", "MDC", fx::BuildHmp()}, {".mdc", "MDC", fx::BuildCsm()}, {".mdc", "MDC", fx::BuildMd3()},   // an MD3 file is not an MDC file
            {".hmp", "HMP", fx::BuildMdc()}, {".hmp", "HMP", fx::BuildCsm()},
            {".csm", "CSM", fx::BuildMdc()}, {".csm", "CSM", fx::BuildHmp()},
            {".md3", "MD3", fx::BuildMdc()},                                                                       // and an MDC file is not an MD3 file
        };
        int i = 0;
        for (const auto& w : wrong) {
            const std::string path = (dir / ("wrong" + std::to_string(i++) + w.ext)).string();
            WriteFile(path, w.bytes);
            ImportedModel m; std::string err;
            CHECK(!ImportModelData(path, m, ImportBackend::Own, false, &err) && err.find(w.name) != std::string::npos && m.parts.empty() && m.nodes.empty());
        }
    }
    // the reserved slot 27 maps no extension, and the slots after it still work
    {
        SetImportBackend("*", ImportBackend::Own);   // sets every slot, the reserved one included: nothing may break
        const std::string path = (dir / "after.csm").string();
        WriteFile(path, fx::BuildCsm());
        ImportedModel m; std::string err;
        CHECK(ImportModelData(path, m, ImportBackend::Own, false, &err) && m.nodes.size() == 4);
    }
    SetImportBackend("*", ImportBackend::Default);
    std::error_code ec; fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
