// The import seam routes the "Assimp gap" formats to THEIR readers: .mesh (slot 43, OGRE binary), .zae (slot 44), .xml
// (slot 45: an Irrlicht scene or mesh, COLLADA, XGL or OGRE XML, told apart by the root element) and .lxo (slot 46, Modo, read by
// the LWO reader; fixture in tests/lxo_fixture.hpp). Each fixture (built in memory,
// tests/gap_fixtures.hpp, batchc_fixtures.hpp, batchd_fixtures.hpp) goes through ImportModelData with the own backends switched on;
// the model must say backend "own" and equal what the format's own Read*File gives directly, so a wrong arm in the dispatch chain
// fails here. Also checked: OKAY_OWN_OGREBIN / OKAY_OWN_ZAE / OKAY_OWN_XML / OKAY_OWN_LXO and SetImportBackend switch a format on and off,
// ImportableExtensions follows, the four are off by default, and .mesh.xml still goes to the OGRE XML reader (slot 23).
#include "test_framework.hpp"
#include "batchc_fixtures.hpp"
#include "batchd_fixtures.hpp"
#include "gap_fixtures.hpp"
#include "lxo_fixture.hpp"
#include "okay/Render/LwoReader.hpp"
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

using Direct = bool (*)(const std::string&, ImportedModel&, std::string*);
static bool DOgreBin(const std::string& p, ImportedModel& m, std::string* e) { return ReadOgreBinaryFile(p, m, e); }
static bool DZae(const std::string& p, ImportedModel& m, std::string* e) { return ReadZaeFile(p, m, e); }
static bool DDae(const std::string& p, ImportedModel& m, std::string* e) { return ReadColladaFile(p, m, e); }
static bool DXgl(const std::string& p, ImportedModel& m, std::string* e) { return ReadXglFile(p, m, e); }
static bool DOgreXml(const std::string& p, ImportedModel& m, std::string* e) { return ReadOgreXmlFile(p, m, e); }
static bool DLwo(const std::string& p, ImportedModel& m, std::string* e) { return ReadLwoFile(p, m, e); }
static bool DIrrMesh(const std::string& p, ImportedModel& m, std::string* e) { return ReadIrrMeshFile(p, m, e); }

struct Case { const char* file; const char* ext; const char* env; std::string bytes; Direct direct; };

int main() {
    RUN_SUITE("own_dispatch_gap");
    const fs::path dir = fs::temp_directory_path() / "okay_own_dispatch_gap";
    std::error_code ec; fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const Case cases[] = {
        {"a.mesh", ".mesh", "OKAY_OWN_OGREBIN", fx::BuildOgreBin(), DOgreBin},
        {"a.zae", ".zae", "OKAY_OWN_ZAE", fx::BuildZae(), DZae},
        {"dae.xml", ".xml", "OKAY_OWN_XML", fx::BuildMiniDae(""), DDae},
        {"xgl.xml", ".xml", "OKAY_OWN_XML", fx::BuildXgl(), DXgl},
        {"ogre.xml", ".xml", "OKAY_OWN_XML", fx::BuildOgreXml(), DOgreXml},
        {"irrmesh.xml", ".xml", "OKAY_OWN_XML", fx::BuildIrrMesh(), DIrrMesh},
        {"a.lxo", ".lxo", "OKAY_OWN_LXO", fx::lxo::BuildLxo(), DLwo},
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
        CHECK(ok && a.backend == "own" && fx::Sane(a) && fx::TriCount(a) > 0);
        CHECK(c.direct(path, b, &err));
        CHECK(a.parts.size() == b.parts.size() && a.nodes.size() == b.nodes.size() && a.materials.size() == b.materials.size() && fx::TriCount(a) == fx::TriCount(b));
        ImportedModel f;
        CHECK(ImportModelData(path, f, ImportBackend::Own, true, &err) && f.nodes.size() == 1 && fx::TriCount(f) == fx::TriCount(a));
        CHECK((" " + ImportableExtensions() + " ").find(std::string(" ") + c.ext + " ") != std::string::npos);
        SetImportBackend(c.ext, ImportBackend::Default);
        TestSetEnv(c.env, "1");
        CHECK(EffectiveImportBackend(c.ext) == ImportBackend::Own);
        ImportedModel e1;
        CHECK(ImportModelData(path, e1, ImportBackend::Default, false, &err) && e1.backend == "own");
        TestSetEnv(c.env, "0");
        CHECK(EffectiveImportBackend(c.ext) != ImportBackend::Own);
        TestUnsetEnv(c.env);
        // The default: our reader since 2026-11-17 (OKAY_EXPECT_OWN_READERS = every reader option ON in this build). Then the two
        // ways back: OKAY_THIRD_PARTY=1 for every format, OKAY_OWN_<FMT>=0 for this one (the checks below run in that state).
#if defined(OKAY_EXPECT_OWN_READERS)
        CHECK(EffectiveImportBackend(c.ext) == ImportBackend::Own);
        { ImportedModel e0; CHECK(ImportModelData(path, e0, ImportBackend::Default, false, &err) && e0.backend == "own"); }
        TestSetEnv("OKAY_THIRD_PARTY", "1");
        CHECK(EffectiveImportBackend(c.ext) == (AssimpAvailable() ? ImportBackend::Assimp : ImportBackend::Own));
        TestUnsetEnv("OKAY_THIRD_PARTY");
        CHECK(EffectiveImportBackend(c.ext) == ImportBackend::Own);
        TestSetEnv(c.env, "0");
#endif
        CHECK(EffectiveImportBackend(c.ext) != ImportBackend::Own);
        if (!AssimpAvailable()) {
            CHECK((" " + ImportableExtensions() + " ").find(std::string(" ") + c.ext + " ") == std::string::npos);
            ImportedModel d; std::string e2;
            CHECK(!ImportModelData(path, d, ImportBackend::Default, false, &e2) && e2.find(c.env) != std::string::npos);
        }
#if defined(OKAY_EXPECT_OWN_READERS)
        TestUnsetEnv(c.env);
#endif
    }
    {   // an Irrlicht scene as .xml: the scene reader, which reads the meshes it names through the seam
        WriteFile(dir / "tri.irrmesh", fx::BuildIrrMesh());
        WriteFile(dir / "scene.xml", fx::BuildIrrScene());
        SetImportBackend("*", ImportBackend::Own);
        ImportedModel a, b; std::string err;
        CHECK(ImportModelData((dir / "scene.xml").string(), a, ImportBackend::Own, false, &err) && a.backend == "own");
        CHECK(ImportModelData((dir / "scene.xml").string(), b, ImportBackend::Own, false, &err));
        CHECK(a.parts.size() >= 3 && a.parts.size() == b.parts.size());
        // UTF-16 little endian with a byte order mark: the root is still found
        std::string u16 = "\xFF\xFE";
        for (char ch : fx::BuildMiniDae("")) { u16.push_back(ch); u16.push_back('\0'); }
        WriteFile(dir / "dae16.xml", u16);
        ImportedModel u; CHECK(ImportModelData((dir / "dae16.xml").string(), u, ImportBackend::Own, false, &err) || err.find("XML:") == std::string::npos);
        // another root is refused with the reason; .mesh.xml still goes to the OGRE XML reader
        WriteFile(dir / "other.xml", "<?xml version=\"1.0\"?>\n<!-- c --><skeleton><bones/></skeleton>\n");
        ImportedModel o;
        CHECK(!ImportModelData((dir / "other.xml").string(), o, ImportBackend::Own, false, &err) && err.find("<skeleton>") != std::string::npos);
        WriteFile(dir / "b.mesh.xml", fx::BuildOgreXml());
        ImportedModel x; CHECK(ImportModelData((dir / "b.mesh.xml").string(), x, ImportBackend::Own, false, &err) && x.parts.size() == 2);
        // a binary mesh named .xml is not taken for XML, and an XML mesh named .mesh is refused by the binary reader
        WriteFile(dir / "bin.xml", fx::BuildOgreBin());
        CHECK(!ImportModelData((dir / "bin.xml").string(), o, ImportBackend::Own, false, &err));
        WriteFile(dir / "xml.mesh", fx::BuildOgreXml());
        CHECK(!ImportModelData((dir / "xml.mesh").string(), o, ImportBackend::Own, false, &err) && err.find("OGRE") != std::string::npos);
    }
    {   // one switch per format
        SetImportBackend("*", ImportBackend::Default);
        TestSetEnv("OKAY_OWN_ZAE", "1");
        TestSetEnv("OKAY_OWN_OGREBIN", "0");
        TestSetEnv("OKAY_OWN_XML", "0");
        TestSetEnv("OKAY_OWN_OGRE", "0");
        TestSetEnv("OKAY_OWN_COLLADA", "0");
        TestSetEnv("OKAY_OWN_LXO", "1");
        TestSetEnv("OKAY_OWN_LWO", "0");
        CHECK(EffectiveImportBackend(".lxo") == ImportBackend::Own && EffectiveImportBackend(".lwo") != ImportBackend::Own);   // LXO has its own switch
        CHECK(EffectiveImportBackend(".zae") == ImportBackend::Own && EffectiveImportBackend(".ZAE") == ImportBackend::Own);
        CHECK(EffectiveImportBackend(".mesh") != ImportBackend::Own && EffectiveImportBackend(".xml") != ImportBackend::Own);
        CHECK(EffectiveImportBackend(".mesh.xml") != ImportBackend::Own && EffectiveImportBackend(".dae") != ImportBackend::Own);
        for (const char* v : {"OKAY_OWN_ZAE", "OKAY_OWN_OGREBIN", "OKAY_OWN_XML", "OKAY_OWN_OGRE", "OKAY_OWN_COLLADA", "OKAY_OWN_LXO", "OKAY_OWN_LWO"}) TestUnsetEnv(v);
    }
    {   // the samples shipped in examples/own_readers (scripts/gen_gap_fixtures.py): a unit cube each, 12 triangles, area 6
        SetImportBackend("*", ImportBackend::Own);
        for (const char* f : {"cube.mesh", "cube.zae", "cube_irrmesh.xml", "cube.lxo"}) {
            ImportedModel m; std::string err;
            const bool ok = ImportModelData(std::string(OKAY_SOURCE_DIR) + "/examples/own_readers/" + f, m, ImportBackend::Own, false, &err);
            if (!ok) std::fprintf(stderr, "  %s: %s\n", f, err.c_str());
            CHECK(ok && fx::TriCount(m) == 12 && m.backend == "own");
            CHECK_NEAR(fx::Area(m), 6.0, 1e-4);
        }
    }
    SetImportBackend("*", ImportBackend::Default);
    fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
