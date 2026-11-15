// The import seam routes the batch-E3 own-reader formats (slots 34 to 36: .iqm .cob .ndo) to THEIR readers. Each fixture (built in memory,
// tests/e3_fixtures.hpp) goes through ImportModelData with the own backend switched on; the model must say backend "own" and equal what the
// format's own Read*File gives directly, so a wrong arm in the dispatch chain fails here. Also checked: OKAY_OWN_<FMT> and SetImportBackend
// switch a format on and off, ImportableExtensions follows, flatten drops skins and clips, the error text names the switch while the reader
// is off and the format while it is on, a file of one format named as another is refused by the right reader, and the NDO seam always refuses.
#include "test_framework.hpp"
#include "e3_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

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
static void WriteFile(const fs::path& p, const std::string& s) { std::ofstream f(p, std::ios::binary); f.write(s.data(), (std::streamsize)s.size()); }
static std::size_t Verts(const ImportedModel& m) { std::size_t n = 0; for (const ImportedPart& p : m.parts) n += p.mesh.vertices.size(); return n; }

struct Case { const char* file; const char* ext; const char* env; std::string bytes; std::size_t tris; bool (*direct)(const std::string&, ImportedModel&, std::string*); };
static bool DIq(const std::string& p, ImportedModel& m, std::string* e) { return ReadIqmFile(p, m, e); }
static bool DCo(const std::string& p, ImportedModel& m, std::string* e) { return ReadCobFile(p, m, e); }

int main() {
    RUN_SUITE("own_dispatch_e3");
    const fs::path dir = fs::temp_directory_path() / "okay_own_dispatch_e3";
    fs::create_directories(dir);
    const Case cases[] = {
        {"t.iqm", ".iqm", "OKAY_OWN_IQM", fx::BuildIqm(), 2, DIq},
        {"t.cob", ".cob", "OKAY_OWN_COB", fx::CobBinary(), 3, DCo},
        {"a.cob", ".cob", "OKAY_OWN_COB", fx::CobAscii(), 3, DCo},
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
        CHECK(a.parts.size() == b.parts.size() && Verts(a) == Verts(b) && a.nodes.size() == b.nodes.size() && a.materials.size() == b.materials.size() && a.clips.size() == b.clips.size());
        CHECK(fx::TriCount(a) == c.tris);
        // flatten: one root node, no skins and no clips, same geometry
        ImportedModel f;
        CHECK(ImportModelData(path, f, ImportBackend::Own, true, &err) && f.nodes.size() == 1 && f.clips.empty() && fx::TriCount(f) == c.tris);
        for (const ImportedPart& p : f.parts) CHECK(p.bones.empty() && p.node == 0);
        CHECK((" " + ImportableExtensions() + " ").find(std::string(" ") + c.ext + " ") != std::string::npos);
        // the format's variable decides when nothing was chosen at run time
        SetImportBackend(c.ext, ImportBackend::Default);
        SetEnv(c.env, "1");
        CHECK(EffectiveImportBackend(c.ext) == ImportBackend::Own);
        ImportedModel e1;
        CHECK(ImportModelData(path, e1, ImportBackend::Default, false, &err) && e1.backend == "own");
        SetEnv(c.env, "0");
        CHECK(EffectiveImportBackend(c.ext) != ImportBackend::Own);
        UnsetEnv(c.env);
        // off by default; without Assimp the format cannot be read and the message names the switch
        if (!AssimpAvailable()) {
            CHECK(EffectiveImportBackend(c.ext) != ImportBackend::Own);
            ImportedModel d; std::string e2;
            CHECK(!ImportModelData(path, d, ImportBackend::Default, false, &e2) && e2.find(c.env) != std::string::npos);
        }
    }
    {   // upper case extensions reach the same reader; a skinned IQM keeps its skin and clip when not flattened
        SetImportBackend("*", ImportBackend::Own);
        const std::string p = (dir / "UP.IQM").string();
        WriteFile(p, fx::BuildIqm());
        ImportedModel m; std::string err;
        CHECK(ImportModelData(p, m, ImportBackend::Own, false, &err) && m.backend == "own" && m.clips.size() == 1 && !m.parts[0].bones.empty());
    }
    {   // a file of one format named as another: refused by the right reader
        SetImportBackend("*", ImportBackend::Own);
        WriteFile(dir / "wrong.iqm", fx::CobBinary());
        WriteFile(dir / "wrong.cob", fx::BuildIqm());
        ImportedModel m; std::string err;
        CHECK(!ImportModelData((dir / "wrong.iqm").string(), m, ImportBackend::Own, false, &err) && err.find("IQM") != std::string::npos);
        CHECK(!ImportModelData((dir / "wrong.cob").string(), m, ImportBackend::Own, false, &err) && err.find("COB") != std::string::npos);
    }
    {   // NDO: slot 36 reaches the seam reader, which refuses everything and says so; nothing is read
        SetImportBackend("*", ImportBackend::Own);
        CHECK(HasOwnReader(".ndo"));
        WriteFile(dir / "t.ndo", std::string("nendo 1.2") + std::string(64, '\0'));
        ImportedModel m; std::string err;
        CHECK(!ImportModelData((dir / "t.ndo").string(), m, ImportBackend::Own, false, &err) && err.find("recognised but not read") != std::string::npos && m.parts.empty());
        WriteFile(dir / "bad.ndo", "not nendo at all");
        CHECK(!ImportModelData((dir / "bad.ndo").string(), m, ImportBackend::Own, false, &err) && err.find("not a Nendo file") != std::string::npos);
        // the refusing seam is never advertised as importable, even with the own backend switched on (when Assimp is built it may list .ndo itself)
        if (!AssimpAvailable()) CHECK((" " + ImportableExtensions() + " ").find(" .ndo ") == std::string::npos);
        SetImportBackend("*", ImportBackend::Default);
        if (!AssimpAvailable()) {
            ImportedModel d; std::string e2;
            CHECK(!ImportModelData((dir / "t.ndo").string(), d, ImportBackend::Default, false, &e2) && e2.find("OKAY_OWN_NDO") != std::string::npos);
        }
    }
    SetImportBackend("*", ImportBackend::Default);
    std::error_code ec; fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
