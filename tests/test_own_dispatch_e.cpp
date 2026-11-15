// The import seam routes the batch-E own-reader formats (slots 31 and 33: .q3o / .q3s and .raw) to THEIR readers. Each fixture
// (built in memory, tests/batche_fixtures.hpp) goes through ImportModelData with the own backend on; the model must say backend
// "own" and equal what the format's own Read*File gives directly. Also: the environment variable OKAY_OWN_<FMT> and SetImportBackend
// switch a format on and off, ImportableExtensions follows, a reader that is off by default says which switch turns it on, other
// extensions of a format reach the same reader, and a file that hostile counts or a wrong signature make unreadable is refused
// through the seam (not crashed). Slot 32 (SIB) has no reader: ".sib" must not be routed anywhere.
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "batche_fixtures.hpp"
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

struct Case { const char* file; const char* ext; const char* env; std::string bytes; std::size_t tris; bool (*direct)(const std::string&, ImportedModel&, std::string*); };
static bool DQ(const std::string& p, ImportedModel& m, std::string* e) { return ReadQ3dFile(p, m, e); }
static bool DR(const std::string& p, ImportedModel& m, std::string* e) { return ReadRawFile(p, m, e); }

int main() {
    RUN_SUITE("own_dispatch_e");
    const fs::path dir = fs::temp_directory_path() / "okay_own_dispatch_e";
    fs::create_directories(dir);
    const Case cases[] = {
        {"t.q3o", ".q3o", "OKAY_OWN_Q3D", fx::BuildQ3d(), 3, DQ},
        {"t.raw", ".raw", "OKAY_OWN_RAW", fx::BuildRaw(), 4, DR},
    };
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
        CHECK(a.parts.size() == b.parts.size() && Verts(a) == Verts(b) && a.nodes.size() == b.nodes.size() && a.materials.size() == b.materials.size());
        CHECK(fx::TriCount(a) == c.tris && fx::TriCount(b) == c.tris);
        ImportedModel f;
        CHECK(ImportModelData(path, f, ImportBackend::Own, true, &err) && f.nodes.size() == 1 && f.clips.empty() && fx::TriCount(f) == c.tris);
        CHECK((" " + ImportableExtensions() + " ").find(std::string(" ") + c.ext + " ") != std::string::npos);
        SetImportBackend(c.ext, ImportBackend::Default);
        TestSetEnv(c.env, "1");
        CHECK(EffectiveImportBackend(c.ext) == ImportBackend::Own);
        ImportedModel e1;
        CHECK(ImportModelData(path, e1, ImportBackend::Default, false, &err) && e1.backend == "own");
        TestSetEnv(c.env, "0");
        CHECK(EffectiveImportBackend(c.ext) != ImportBackend::Own);
        TestUnsetEnv(c.env);
        if (!AssimpAvailable()) {   // off by default: not importable, and the message names the switch
            CHECK(EffectiveImportBackend(c.ext) != ImportBackend::Own);
            CHECK((" " + ImportableExtensions() + " ").find(std::string(" ") + c.ext + " ") == std::string::npos);
            ImportedModel d; std::string e2;
            CHECK(!ImportModelData(path, d, ImportBackend::Default, false, &e2) && e2.find(c.env) != std::string::npos);
        }
    }
    SetImportBackend("*", ImportBackend::Own);
    {   // .q3s is the scene form of the same reader; the extension case does not matter
        for (const char* name : {"s.q3s", "UP.Q3O"}) {
            const std::string path = (dir / name).string();
            WriteFile(path, fx::BuildQ3d(false, name[0] == 's', true));
            ImportedModel m; std::string err;
            CHECK(HasOwnReader(name[0] == 's' ? ".q3s" : ".Q3O"));
            CHECK(ImportModelData(path, m, ImportBackend::Own, false, &err) && m.backend == "own" && fx::TriCount(m) == 3);
        }
        // a hostile or wrong file is refused through the seam, with a message
        const std::string bad = (dir / "bad.q3o").string();
        std::string s = fx::BuildQ3d();
        for (int k = 0; k < 4; ++k) s[23 + (std::size_t)k] = (char)0xFF;
        WriteFile(bad, s);
        ImportedModel m; std::string err;
        CHECK(!ImportModelData(bad, m, ImportBackend::Own, false, &err) && !err.empty() && m.parts.empty());
        WriteFile(bad, "this is not a quick3D file at all, only text");
        CHECK(!ImportModelData(bad, m, ImportBackend::Own, false, &err) && err.find("Q3D") != std::string::npos);
        const std::string badRaw = (dir / "bad.raw").string();
        WriteFile(badRaw, "1 2 3\n");
        CHECK(!ImportModelData(badRaw, m, ImportBackend::Own, false, &err) && err.find("RAW") != std::string::npos);
        // SIB has no own reader (slot 32 is reserved, empty)
        CHECK(!HasOwnReader(".sib"));
    }
    SetImportBackend("*", ImportBackend::Default);
    std::error_code ec; fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
