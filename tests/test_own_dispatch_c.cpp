// The import seam routes every batch-C own-reader format (slots 17 to 21: .md2 .md3 .irrmesh .b3d .x) to ITS reader.
// Each format's fixture (built in memory, tests/batchc_fixtures.hpp) goes through ImportModelData with the own backends
// switched on; the model must say backend "own" and must equal what the format's own Read*File gives directly, so a wrong
// arm in the dispatch chain (a format absorbed by the last arm) fails here. Also checked: the environment variable
// OKAY_OWN_<FMT> and SetImportBackend switch a format on and off, ImportableExtensions follows, and flatten bakes the
// node tree. (The batch A and B slots are checked by tests/test_own_dispatch.cpp once the Blender branch that adds it is
// merged; the two are meant to become one file.)
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "batchc_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

// setenv / unsetenv exist on POSIX only; the Windows C runtime has _putenv_s (an empty value removes the variable).
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

struct Case { const char* ext; const char* env; std::string bytes; std::size_t tris; bool (*direct)(const std::string&, ImportedModel&, std::string*); };
static bool DMd2(const std::string& p, ImportedModel& m, std::string* e) { return ReadMd2File(p, m, e); }
static bool DMd3(const std::string& p, ImportedModel& m, std::string* e) { return ReadMd3File(p, m, e); }
static bool DIrr(const std::string& p, ImportedModel& m, std::string* e) { return ReadIrrMeshFile(p, m, e); }
static bool DB3d(const std::string& p, ImportedModel& m, std::string* e) { return ReadB3dFile(p, m, e); }
static bool DX(const std::string& p, ImportedModel& m, std::string* e) { return ReadXFile(p, m, e); }

int main() {
    RUN_SUITE("own_dispatch_c");
    const fs::path dir = fs::temp_directory_path() / "okay_own_dispatch_c";
    fs::create_directories(dir);
    const Case cases[] = {
        {".md2", "OKAY_OWN_MD2", fx::BuildMd2(), 2, DMd2},
        {".md3", "OKAY_OWN_MD3", fx::BuildMd3(), 1, DMd3},
        {".irrmesh", "OKAY_OWN_IRRMESH", fx::BuildIrrMesh(), 4, DIrr},
        {".b3d", "OKAY_OWN_B3D", fx::BuildB3d(), 1, DB3d},
        {".x", "OKAY_OWN_X", fx::BuildXText(), 3, DX},
    };
    for (const Case& c : cases) {
        const std::string path = (dir / (std::string("t") + c.ext)).string();
        WriteFile(path, c.bytes);
        CHECK(HasOwnReader(c.ext));
        // switched on at run time
        SetImportBackend(c.ext, ImportBackend::Own);
        CHECK(EffectiveImportBackend(c.ext) == ImportBackend::Own);
        ImportedModel a, b; std::string err;
        const bool ok = ImportModelData(path, a, ImportBackend::Default, false, &err);
        if (!ok) std::fprintf(stderr, "  %s: %s\n", c.ext, err.c_str());
        CHECK(ok && a.backend == "own");
        CHECK(c.direct(path, b, &err));
        CHECK(a.parts.size() == b.parts.size() && Verts(a) == Verts(b) && a.nodes.size() == b.nodes.size() && a.materials.size() == b.materials.size() && a.clips.size() == b.clips.size());
        CHECK(fx::TriCount(a) == c.tris && fx::TriCount(b) == c.tris);
        // flatten: one root node, no skins and no clips, same geometry
        ImportedModel f;
        CHECK(ImportModelData(path, f, ImportBackend::Own, true, &err) && f.nodes.size() == 1 && f.clips.empty() && fx::TriCount(f) == c.tris);
        for (const ImportedPart& p : f.parts) CHECK(p.bones.empty() && p.node == 0);
        // the extension list follows the switch
        CHECK((" " + ImportableExtensions() + " ").find(std::string(" ") + c.ext + " ") != std::string::npos);
        // the format-specific variable decides when nothing was chosen at run time
        SetImportBackend(c.ext, ImportBackend::Default);
        TestSetEnv(c.env, "1");
        CHECK(EffectiveImportBackend(c.ext) == ImportBackend::Own);
        ImportedModel e1;
        CHECK(ImportModelData(path, e1, ImportBackend::Default, false, &err) && e1.backend == "own");
        TestSetEnv(c.env, "0");
        CHECK(EffectiveImportBackend(c.ext) != ImportBackend::Own);
        TestUnsetEnv(c.env);
        // off by default (when the build has no Assimp the format cannot be read at all, and the message names the switch)
        if (!AssimpAvailable()) {
            ImportedModel d; std::string e2;
            CHECK(!ImportModelData(path, d, ImportBackend::Default, false, &e2) && e2.find(c.env) != std::string::npos);
        }
    }
    // a different extension is not absorbed by a neighbour's reader: an .md3 file named .md2 is refused by the MD2 reader
    {
        const std::string wrong = (dir / "wrong.md2").string();
        WriteFile(wrong, fx::BuildMd3());
        SetImportBackend("*", ImportBackend::Own);
        ImportedModel m; std::string err;
        CHECK(!ImportModelData(wrong, m, ImportBackend::Own, false, &err) && err.find("MD2") != std::string::npos);
    }
    SetImportBackend("*", ImportBackend::Default);
    std::error_code ec; fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
