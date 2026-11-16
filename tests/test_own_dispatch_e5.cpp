// The import seam routes the PMX and LWS / MOT formats (slots 39 and 40) to THEIR readers. Each fixture (built in memory,
// tests/pmxlws_fixtures.hpp) goes through ImportModelData with the own backends switched on; the model must say backend "own" and must
// equal what the format's own Read*File gives directly, so a wrong arm in the dispatch chain fails here. Also checked: the environment
// variable OKAY_OWN_PMX / OKAY_OWN_LWS and SetImportBackend switch a format on and off, ImportableExtensions follows, flatten bakes the
// node tree, .mot reaches the LWS reader, and a scene reads the object files it names through the seam (an .obj here, always ours) but
// never another scene, never itself, and never a file outside its own folder. The other slots are checked by test_own_dispatch*.cpp.
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "pmxlws_fixtures.hpp"
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
static bool HasNote(const ImportedModel& m, const char* what) { for (const std::string& n : m.notes) if (n.find(what) != std::string::npos) return true; return false; }

struct Case { const char* file; const char* ext; const char* env; std::string bytes; std::size_t tris; std::size_t nodes; bool (*direct)(const std::string&, ImportedModel&, std::string*); };
static bool DPmx(const std::string& p, ImportedModel& m, std::string* e) { return ReadPmxFile(p, m, e); }
static bool DLws(const std::string& p, ImportedModel& m, std::string* e) { return ReadLwsFile(p, m, e); }

int main() {
    RUN_SUITE("own_dispatch_e5");
    const fs::path dir = fs::temp_directory_path() / "okay_own_dispatch_e5";
    std::error_code ec; fs::remove_all(dir, ec);
    fs::create_directories(dir);
    WriteFile(dir / "tri.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    // the scene names tri.obj next to it; with the direct call (no loader) the object is an empty node, so the model has no triangles
    const std::string scene = "LWSC\n3\nLoadObject tri.obj\nObjectMotion\nNumChannels 9\n" + fx::Chan(0, {{0, 1}, {1, 3}});
    const Case cases[] = {
        {"t.pmx", ".pmx", "OKAY_OWN_PMX", fx::BuildPmx(), 3, 4, DPmx},
        {"t.lws", ".lws", "OKAY_OWN_LWS", scene, 1, 3, nullptr},
        {"t.mot", ".mot", "OKAY_OWN_LWS", fx::BuildMot(), 0, 2, DLws},
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
        CHECK(fx::TriCount(a) == c.tris && a.nodes.size() == c.nodes && a.clips.empty() == (c.direct == DPmx));   // a PMX file has no animation, a scene and a motion file do
        if (c.direct) {
            CHECK(c.direct(path, b, &err));
            CHECK(a.parts.size() == b.parts.size() && a.nodes.size() == b.nodes.size() && a.materials.size() == b.materials.size() && a.clips.size() == b.clips.size());
        }
        // flatten: one root node, no skins and no clips
        ImportedModel f;
        CHECK(ImportModelData(path, f, ImportBackend::Own, true, &err) && f.nodes.size() == 1 && f.clips.empty() && fx::TriCount(f) == c.tris);
        for (const ImportedPart& p : f.parts) CHECK(p.bones.empty() && p.node == 0);
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
        bool builtOn = false;   // -DOKAY_OWN_<FMT>=ON makes our reader the default, so the off-by-default checks do not apply
#ifdef OKAY_EXPECT_OWN_PMX
        if (std::string(c.ext) == ".pmx") builtOn = true;
#endif
#ifdef OKAY_EXPECT_OWN_LWS
        if (std::string(c.ext) == ".lws" || std::string(c.ext) == ".mot") builtOn = true;
#endif
        if (!AssimpAvailable() && !builtOn) {
            CHECK(EffectiveImportBackend(c.ext) != ImportBackend::Own);
            CHECK((" " + ImportableExtensions() + " ").find(std::string(" ") + c.ext + " ") == std::string::npos);
            ImportedModel d; std::string e2;
            CHECK(!ImportModelData(path, d, ImportBackend::Default, false, &e2) && e2.find(c.env) != std::string::npos);
        }
    }
    // one switch per format: the .lws and .mot extensions share OKAY_OWN_LWS, and the other formats' switches do not move them
    {
        SetImportBackend("*", ImportBackend::Default);
        TestSetEnv("OKAY_OWN_LWS", "1");
        CHECK(EffectiveImportBackend(".lws") == ImportBackend::Own && EffectiveImportBackend(".mot") == ImportBackend::Own && EffectiveImportBackend(".LWS") == ImportBackend::Own);
        TestSetEnv("OKAY_OWN_PMX", "0");   // pinned off so that a -DOKAY_OWN_PMX=ON build still has PMX off here
        CHECK(EffectiveImportBackend(".pmx") != ImportBackend::Own);
        TestUnsetEnv("OKAY_OWN_PMX");
        TestUnsetEnv("OKAY_OWN_LWS");
        TestSetEnv("OKAY_OWN_PMX", "1");
        TestSetEnv("OKAY_OWN_LWS", "0");
        TestSetEnv("OKAY_OWN_NFF", "0");   // pinned off so that a -DOKAY_OWN_NFF=ON (or -DOKAY_OWN_STACK=ON) build still has NFF off here
        CHECK(EffectiveImportBackend(".pmx") == ImportBackend::Own && EffectiveImportBackend(".lws") != ImportBackend::Own && EffectiveImportBackend(".nff") != ImportBackend::Own && EffectiveImportBackend(".obj") == ImportBackend::Own);
        TestUnsetEnv("OKAY_OWN_PMX");
        TestUnsetEnv("OKAY_OWN_LWS");
        TestUnsetEnv("OKAY_OWN_NFF");
    }
    // a format is not absorbed by a neighbour's reader
    {
        SetImportBackend("*", ImportBackend::Own);
        WriteFile(dir / "wrong.pmx", fx::BuildMot());
        WriteFile(dir / "wrong.lws", fx::BuildPmx());
        ImportedModel m; std::string err;
        CHECK(!ImportModelData((dir / "wrong.pmx").string(), m, ImportBackend::Own, false, &err) && err.find("PMX") != std::string::npos);
        CHECK(!ImportModelData((dir / "wrong.lws").string(), m, ImportBackend::Own, false, &err) && err.find("LWS") != std::string::npos);
    }
    // the scene reads the object it names through the seam, and not another scene or itself, and not a file outside its folder
    {
        SetImportBackend("*", ImportBackend::Own);
        ImportedModel m; std::string err;
        CHECK(ImportModelData((dir / "t.lws").string(), m, ImportBackend::Own, false, &err) && fx::TriCount(m) == 1 && m.parts.size() == 1);
        // the object node under the item: root, item "tri", the obj's own node
        CHECK(m.nodes.size() == 3 && m.nodes[1].name == "tri" && m.nodes[2].parent == 1 && m.parts[0].node == 2);
        // a scene that names itself, one that names another scene, a motion file, and a good object next to them
        WriteFile(dir / "self.lws", "LWSC\n3\nLoadObject self.lws\nLoadObject t.lws\nLoadObject t.mot\nLoadObject tri.obj\n");
        ImportedModel s;
        CHECK(ImportModelData((dir / "self.lws").string(), s, ImportBackend::Own, false, &err) && fx::TriCount(s) == 1 && HasNote(s, "scene file refused"));
        // only a scene: refused, with the reason
        WriteFile(dir / "only.lws", "LWSC\n3\nLoadObject only.lws\n");
        ImportedModel o;
        CHECK(!ImportModelData((dir / "only.lws").string(), o, ImportBackend::Own, false, &err) && err.find("may not load another scene") != std::string::npos);
        // outside the folder
        fs::create_directories(dir / "inner");
        for (const std::string& ref : {(dir / "tri.obj").string(), std::string("../tri.obj"), std::string("sub/../../tri.obj")}) {
            WriteFile(dir / "inner" / "out.lws", "LWSC\n3\nLoadObject " + ref + "\n");
            ImportedModel x;
            CHECK(!ImportModelData((dir / "inner" / "out.lws").string(), x, ImportBackend::Own, false, &err) && err.find("refused") != std::string::npos);
        }
    }
    SetImportBackend("*", ImportBackend::Default);
    fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
