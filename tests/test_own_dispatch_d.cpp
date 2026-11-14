// The import seam routes every batch-D own-reader format (slots 22 to 26: .ac .mesh.xml .xgl / .zgl .irr .nff) to ITS reader.
// Each format's fixture (built in memory, tests/batchd_fixtures.hpp) goes through ImportModelData with the own backends
// switched on; the model must say backend "own" and must equal what the format's own Read*File gives directly, so a wrong
// arm in the dispatch chain fails here. Also checked: the environment variable OKAY_OWN_<FMT> and SetImportBackend switch a
// format on and off, ImportableExtensions follows, flatten bakes the node tree, ".mesh.xml" is told from other .xml files,
// the Irrlicht scene reads the mesh it names through the seam (and not another scene), the extensions of the same format
// (.acc, .ac3d, .zgl, .enff) reach the same reader. The batch A to C slots are checked by test_own_dispatch.cpp and
// test_own_dispatch_c.cpp.
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "batchd_fixtures.hpp"
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

struct Case { const char* file; const char* ext; const char* env; std::string bytes; std::size_t tris; bool (*direct)(const std::string&, ImportedModel&, std::string*); };
static bool DAc(const std::string& p, ImportedModel& m, std::string* e) { return ReadAc3dFile(p, m, e); }
static bool DOg(const std::string& p, ImportedModel& m, std::string* e) { return ReadOgreXmlFile(p, m, e); }
static bool DXg(const std::string& p, ImportedModel& m, std::string* e) { return ReadXglFile(p, m, e); }
static bool DIr(const std::string& p, ImportedModel& m, std::string* e) { return ReadIrrSceneFile(p, m, e); }
static bool DNf(const std::string& p, ImportedModel& m, std::string* e) { return ReadNffFile(p, m, e); }

int main() {
    RUN_SUITE("own_dispatch_d");
    const fs::path dir = fs::temp_directory_path() / "okay_own_dispatch_d";
    fs::create_directories(dir);
    const Case cases[] = {
        {"t.ac", ".ac", "OKAY_OWN_AC3D", fx::BuildAc3d(), 5, DAc},
        {"t.mesh.xml", ".mesh.xml", "OKAY_OWN_OGRE", fx::BuildOgreXml(), 3, DOg},
        {"t.xgl", ".xgl", "OKAY_OWN_XGL", fx::BuildXgl(), 6, DXg},
        {"t.irr", ".irr", "OKAY_OWN_IRR", fx::BuildIrrScene(), 60, DIr},   // the cube (12) and the sphere (48); the mesh node names a file that is not there (the direct call has no loader at all)
        {"t.nff", ".nff", "OKAY_OWN_NFF", fx::BuildNff(), 111, DNf},
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
        const std::size_t expect = c.tris;
        CHECK(a.parts.size() == b.parts.size() && Verts(a) == Verts(b) && a.nodes.size() == b.nodes.size() && a.materials.size() == b.materials.size());
        CHECK(fx::TriCount(a) == expect && fx::TriCount(b) == c.tris);
        // flatten: one root node, no skins and no clips, same geometry (a rotation or scale on a node is baked into the vertices)
        ImportedModel f;
        CHECK(ImportModelData(path, f, ImportBackend::Own, true, &err) && f.nodes.size() == 1 && f.clips.empty() && fx::TriCount(f) == expect);
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
            CHECK(EffectiveImportBackend(c.ext) != ImportBackend::Own);
            CHECK((" " + ImportableExtensions() + " ").find(std::string(" ") + c.ext + " ") == std::string::npos);
            ImportedModel d; std::string e2;
            CHECK(!ImportModelData(path, d, ImportBackend::Default, false, &e2) && e2.find(c.env) != std::string::npos);
        }
    }
    // the other extensions of a format reach the same reader
    {
        SetImportBackend("*", ImportBackend::Own);
        for (const char* ext : {".acc", ".ac3d", ".AC"}) {
            CHECK(HasOwnReader(ext));
            const std::string path = (dir / (std::string("u") + ext)).string();
            WriteFile(path, fx::BuildAc3d());
            ImportedModel m; std::string err;
            CHECK(ImportModelData(path, m, ImportBackend::Own, false, &err) && m.backend == "own" && fx::TriCount(m) == 5);
        }
        {   // ZGL: the same text as one zlib stream
            const std::string path = (dir / "u.zgl").string();
            WriteFile(path, fx::ToZlibStored(fx::BuildXgl()));
            ImportedModel m; std::string err;
            CHECK(HasOwnReader(".zgl") && ImportModelData(path, m, ImportBackend::Own, false, &err) && m.backend == "own" && fx::TriCount(m) == 6);
        }
        {   // .enff is NFF
            const std::string path = (dir / "u.enff").string();
            WriteFile(path, fx::BuildNff());
            ImportedModel m; std::string err;
            CHECK(HasOwnReader(".enff") && ImportModelData(path, m, ImportBackend::Own, false, &err) && fx::TriCount(m) == 111);
        }
        {   // ".mesh.xml" is the OGRE format; another .xml and a plain .mesh are not routed to it
            CHECK(HasOwnReader(".mesh.xml") && !HasOwnReader(".xml") && !HasOwnReader(".mesh"));
            const std::string upper = (dir / "UP.MESH.XML").string();
            WriteFile(upper, fx::BuildOgreXml());
            ImportedModel m; std::string err;
            CHECK(ImportModelData(upper, m, ImportBackend::Own, false, &err) && m.backend == "own" && fx::TriCount(m) == 3);
            const std::string other = (dir / "t.xml").string();
            WriteFile(other, fx::BuildOgreXml());
            CHECK(!ImportModelData(other, m, ImportBackend::Own, false, &err));
        }
    }
    // a format is not absorbed by a neighbour's reader: an NFF file named .ac is refused by the AC3D reader
    {
        const std::string wrong = (dir / "wrong.ac").string();
        WriteFile(wrong, fx::BuildNff());
        SetImportBackend("*", ImportBackend::Own);
        ImportedModel m; std::string err;
        CHECK(!ImportModelData(wrong, m, ImportBackend::Own, false, &err) && err.find("AC3D") != std::string::npos);
        const std::string wrong2 = (dir / "wrong.nff").string();
        WriteFile(wrong2, fx::BuildAc3d());
        CHECK(!ImportModelData(wrong2, m, ImportBackend::Own, false, &err) && err.find("NFF") != std::string::npos);
    }
    // the Irrlicht scene reads the mesh it names through the seam: a .irrmesh (our reader), an .obj (always ours), and not another scene
    {
        SetImportBackend("*", ImportBackend::Own);
        WriteFile(dir / "tri.irrmesh", fx::BuildIrrMesh());
        WriteFile(dir / "scene2.irr", fx::BuildIrrScene());   // this scene names "tri.irrmesh" next to it
        ImportedModel m; std::string err;
        CHECK(ImportModelData((dir / "scene2.irr").string(), m, ImportBackend::Own, false, &err));
        CHECK(fx::TriCount(m) == 60 + 4 && m.parts.size() == 2 + 3);   // cube + sphere + the irrmesh fixture (4 triangles, 3 buffers)
        WriteFile(dir / "obj.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
        std::string s = fx::BuildIrrScene(); s.replace(s.find("tri.irrmesh"), 11, "obj.obj");
        WriteFile(dir / "scene3.irr", s);
        ImportedModel m3;
        CHECK(ImportModelData((dir / "scene3.irr").string(), m3, ImportBackend::Own, false, &err) && fx::TriCount(m3) == 60 + 1);
        // a scene that names a scene: refused for that node, the scene itself still reads
        s = fx::BuildIrrScene(); s.replace(s.find("tri.irrmesh"), 11, "scene3.irr");
        WriteFile(dir / "scene4.irr", s);
        ImportedModel m4;
        CHECK(ImportModelData((dir / "scene4.irr").string(), m4, ImportBackend::Own, false, &err) && fx::TriCount(m4) == 60);
        bool noted = false; for (const std::string& n : m4.notes) if (n.find("may not load another scene") != std::string::npos) noted = true;
        CHECK(noted);
    }
    SetImportBackend("*", ImportBackend::Default);
    std::error_code ec; fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
