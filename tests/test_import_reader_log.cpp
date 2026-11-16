// One Console line per import that says which reader read the file (docs/own_flip_proposal.md, Step 0.4), and the run-time
// switches OKAY_OWN_STL / OKAY_OWN_PLY (Step 0.8).
//
// Every model import that succeeds writes exactly one line through the engine log (the editor's Console shows it):
//     model import '<file>': read by our own <X> reader
//     model import '<file>': read by Assimp
//     model import '<file>': read by ufbx
//     model import '<file>': read by ufbx (our own FBX reader refused it)
// and every WAV that AudioClip::LoadWAV loads writes one of
//     audio import '<file>': read by our own WAV decoder
//     audio import '<file>': read by the old WAV loader
// A failed import writes no such line. Nested reads (an Irrlicht or LightWave scene reading the meshes it names, the scene
// importer trying the seam and then the one-mesh path) still give one line, naming the reader of the file that was asked for.
//
// The fixtures are our own: the committed files in tests/data/models and tests/assets, and the in-memory fixtures of the
// dispatch tests (tests/*_fixtures.hpp) written to a temporary folder. Every switch is pinned in the environment first, so a
// -DOKAY_OWN_STACK=ON build gives the same answers as a default build. Lines that name Assimp are only checked when the build
// has Assimp (CI job assimp-models); without it the test checks that the format is not read at all.
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "batchd_fixtures.hpp"
#include "batche_fixtures.hpp"
#include "batche2_fixtures.hpp"
#include "pmxlws_fixtures.hpp"
#include "terragen_test_build.hpp"
#include "okay/Audio/AudioClip.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Render/FbxReader.hpp"
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Scene/Scene.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

// The IQM and COB fixtures (tests/e3_fixtures.hpp) clash with batche_fixtures.hpp by name, so they are built in a second file.
std::string ReaderLogIqmFixture();
std::string ReaderLogCobFixture();

static const std::string kModels = std::string(OKAY_SOURCE_DIR) + "/tests/data/models/";
static const std::string kFbx = std::string(OKAY_SOURCE_DIR) + "/tests/assets/fbx/";
static const std::string kGlb = std::string(OKAY_SOURCE_DIR) + "/tests/assets/rigs/fox.glb";

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
static std::string ReadFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static bool IsReaderLine(const std::string& m) {
    return (m.rfind("model import '", 0) == 0 || m.rfind("audio import '", 0) == 0) && m.find("': read by ") != std::string::npos;
}

// Runs `f` and returns the reader lines it logged.
static std::vector<std::string> LinesOf(const std::function<void()>& f) {
    std::vector<std::string> lines;
    const Log::Sink old = Log::sink;
    Log::sink = [&lines](Log::Level, const std::string& m) { if (IsReaderLine(m)) lines.push_back(m); };
    f();
    Log::sink = old;
    return lines;
}

static void ExpectOne(const std::vector<std::string>& lines, const std::string& want, const char* what) {
    const bool good = lines.size() == 1 && lines[0] == want;
    CHECK(good);
    if (!good) {
        std::fprintf(stderr, "  %s: wanted exactly one line \"%s\", got %zu:\n", what, want.c_str(), lines.size());
        for (const std::string& l : lines) std::fprintf(stderr, "    \"%s\"\n", l.c_str());
    }
}
static void ExpectNone(const std::vector<std::string>& lines, const char* what) {
    CHECK(lines.empty());
    if (!lines.empty()) {
        std::fprintf(stderr, "  %s: wanted no reader line, got %zu:\n", what, lines.size());
        for (const std::string& l : lines) std::fprintf(stderr, "    \"%s\"\n", l.c_str());
    }
}
static std::string Line(const std::string& path, const std::string& by) { return "model import '" + path + "': read by " + by; }
static std::string Own(const char* name) { return std::string("our own ") + name + " reader"; }

// The three public import entry points, each counted on its own.
static std::vector<std::string> ViaModel(const std::string& p) { return LinesOf([&] { bool ok = false; ImportModel(p, &ok); }); }
static std::vector<std::string> ViaScene(const std::string& p) { return LinesOf([&] { Scene s; bool ok = false; ImportModelScene(s, p, &ok); }); }
static std::vector<std::string> ViaData(const std::string& p) { return LinesOf([&] { ImportedModel m; ImportModelData(p, m); }); }

// Formats read by our own reader only (OBJ and glTF have no other reader).
static void TestAlwaysOwn() {
    SetEnv("OKAY_OWN_STL", "1");   // (not used here; pinned so nothing else changes the answers)
    const std::string obj = kModels + "cube.obj";
    ExpectOne(ViaModel(obj), Line(obj, Own("OBJ")), "OBJ via ImportModel");
    ExpectOne(ViaScene(obj), Line(obj, Own("OBJ")), "OBJ via ImportModelScene");
    ExpectOne(ViaData(obj), Line(obj, Own("OBJ")), "OBJ via ImportModelData");
    ExpectOne(ViaModel(kGlb), Line(kGlb, Own("glTF")), "glb via ImportModel");
    ExpectOne(ViaScene(kGlb), Line(kGlb, Own("glTF")), "glb via ImportModelScene");
    // A failed import says nothing of the kind.
    ExpectNone(ViaModel(kModels + "no_such_file.obj"), "missing OBJ");
    ExpectNone(ViaScene(kModels + "no_such_file.stl"), "missing STL");
    ExpectNone(ViaData(kModels + "cube.mtl"), "not a model");
    UnsetEnv("OKAY_OWN_STL");
}

// Step 0.8: OKAY_OWN_STL and OKAY_OWN_PLY choose the reader at run time, like the other OKAY_OWN_<FMT> switches.
static void TestStlPlySwitches() {
    SetImportBackend("*", ImportBackend::Default);
    struct C { const char* var; const char* ext; const char* file; const char* name; };
    for (const C& c : {C{"OKAY_OWN_STL", ".stl", "cube_ascii.stl", "STL"}, C{"OKAY_OWN_PLY", ".ply", "cube_ascii.ply", "PLY"}}) {
        const std::string p = kModels + c.file;
        SetEnv(c.var, "1");
        CHECK(EffectiveImportBackend(c.ext) == ImportBackend::Own);
        ExpectOne(ViaModel(p), Line(p, Own(c.name)), c.file);
        ExpectOne(ViaScene(p), Line(p, Own(c.name)), c.file);
        SetEnv(c.var, "0");
        // Off: Assimp reads it when the build has Assimp; without Assimp ours is the only reader and still reads it.
        CHECK(EffectiveImportBackend(c.ext) == (AssimpAvailable() ? ImportBackend::Assimp : ImportBackend::Own));
        ExpectOne(ViaScene(p), Line(p, AssimpAvailable() ? std::string("Assimp") : Own(c.name)), c.file);
        // SetImportBackend still wins over the variable.
        SetImportBackend(c.ext, ImportBackend::Own);
        CHECK(EffectiveImportBackend(c.ext) == ImportBackend::Own);
        SetImportBackend(c.ext, ImportBackend::Default);
        SetEnv(c.var, "1");
        SetImportBackend(c.ext, ImportBackend::Assimp);
        CHECK(EffectiveImportBackend(c.ext) == (AssimpAvailable() ? ImportBackend::Assimp : ImportBackend::Own));
        SetImportBackend(c.ext, ImportBackend::Default);
        // Values other than 0 and 1 are ignored (the build default decides), as for the other switches.
        SetEnv(c.var, "yes");
        const ImportBackend dflt = EffectiveImportBackend(c.ext);
        UnsetEnv(c.var);
        CHECK(EffectiveImportBackend(c.ext) == dflt);
    }
    // The sample files the Windows download ships in examples\own_readers (docs/try_it.md) read with ours when switched on.
    const std::string ex = std::string(OKAY_SOURCE_DIR) + "/examples/own_readers/";
    SetEnv("OKAY_OWN_STL", "1"); SetEnv("OKAY_OWN_PLY", "1"); SetEnv("OKAY_OWN_OFF", "1");
    ExpectOne(ViaScene(ex + "cube.stl"), Line(ex + "cube.stl", Own("STL")), "examples cube.stl");
    ExpectOne(ViaScene(ex + "cube.ply"), Line(ex + "cube.ply", Own("PLY")), "examples cube.ply");
    ExpectOne(ViaScene(ex + "cube.off"), Line(ex + "cube.off", Own("OFF")), "examples cube.off");
    UnsetEnv("OKAY_OWN_OFF");
    // The two are independent.
    SetEnv("OKAY_OWN_STL", "1"); SetEnv("OKAY_OWN_PLY", "0");
    CHECK(EffectiveImportBackend(".stl") == ImportBackend::Own);
    CHECK(EffectiveImportBackend(".ply") == (AssimpAvailable() ? ImportBackend::Assimp : ImportBackend::Own));
    UnsetEnv("OKAY_OWN_STL"); UnsetEnv("OKAY_OWN_PLY");
    CHECK(EffectiveImportBackend(".obj") == ImportBackend::Own);   // OBJ is not touched by either
}

// The opt-in readers with a committed fixture: on, the line names ours; off, Assimp (with Assimp) or nothing at all.
static void TestCommittedFixtures() {
    struct C { const char* var; const char* file; const char* name; };
    const C cases[] = {
        {"OKAY_OWN_OFF", "cube.off", "OFF"},          {"OKAY_OWN_3DS", "cube.3ds", "3DS"},
        {"OKAY_OWN_COLLADA", "skinned_strip.dae", "COLLADA"}, {"OKAY_OWN_BVH", "walk.bvh", "BVH"},
        {"OKAY_OWN_SMD", "arm.smd", "SMD"},           {"OKAY_OWN_MD5", "skel.md5mesh", "MD5"},
        {"OKAY_OWN_MS3D", "rig.ms3d", "MS3D"},        {"OKAY_OWN_ASE", "scene.ase", "ASE"},
        {"OKAY_OWN_BLEND", "blend_attr_41.blend", "Blender"},
    };
    for (const C& c : cases) {
        const std::string p = kModels + c.file;
        SetEnv(c.var, "1");
        ExpectOne(ViaScene(p), Line(p, Own(c.name)), c.file);
        ExpectOne(ViaData(p), Line(p, Own(c.name)), c.file);
        SetEnv(c.var, "0");
        const std::vector<std::string> off = ViaScene(p);
        if (AssimpAvailable()) {
            CHECK(off.size() <= 1);   // Assimp may not read every fixture; when it does, it is named
            if (off.size() == 1) ExpectOne(off, Line(p, "Assimp"), c.file);
        } else {
            ExpectNone(off, c.file);
        }
        UnsetEnv(c.var);
    }
}

// The sixteen readers made visible by Step 0.3 (their fixtures are built in memory by the dispatch tests' helpers).
static void TestNewReaders(const fs::path& dir) {
    struct C { const char* var; const char* file; const char* name; std::string bytes; };
    std::vector<C> cases = {
        {"OKAY_OWN_AC3D", "t.ac", "AC3D", fx::BuildAc3d()},
        {"OKAY_OWN_OGRE", "t.mesh.xml", "OGRE", fx::BuildOgreXml()},
        {"OKAY_OWN_XGL", "t.xgl", "XGL", fx::BuildXgl()},
        {"OKAY_OWN_NFF", "t.nff", "NFF", fx::BuildNff()},
        {"OKAY_OWN_TER", "t.ter", "Terragen", terbuild::Grid(3, 3, 100, 0, std::vector<std::int16_t>(9, 10))},
        {"OKAY_OWN_MDC", "t.mdc", "MDC", fx::BuildMdc()},
        {"OKAY_OWN_HMP", "t.hmp", "HMP", fx::BuildHmp()},
        {"OKAY_OWN_CSM", "t.csm", "CSM", fx::BuildCsm()},
        {"OKAY_OWN_Q3D", "t.q3o", "Q3D", fx2::BuildQ3d()},
        {"OKAY_OWN_RAW", "t.raw", "RAW", fx2::BuildRaw()},
        {"OKAY_OWN_IQM", "t.iqm", "IQM", ReaderLogIqmFixture()},
        {"OKAY_OWN_COB", "t.cob", "COB", ReaderLogCobFixture()},
        {"OKAY_OWN_PMX", "t.pmx", "PMX", fx::BuildPmx()},
    };
    for (const C& c : cases) {
        const std::string p = (dir / c.file).string();
        WriteFile(p, c.bytes);
        SetEnv(c.var, "1");
        ExpectOne(ViaData(p), Line(p, Own(c.name)), c.file);
        SetEnv(c.var, "0");
        if (!AssimpAvailable()) ExpectNone(ViaData(p), c.file);
        UnsetEnv(c.var);
    }
    // Scenes that read the meshes they name through the same seam: still one line, for the scene's reader.
    WriteFile(dir / "obj.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    std::string irr = fx::BuildIrrScene();
    irr.replace(irr.find("tri.irrmesh"), 11, "obj.obj");
    const std::string irrPath = (dir / "scene.irr").string();
    WriteFile(irrPath, irr);
    SetEnv("OKAY_OWN_IRR", "1");
    ExpectOne(ViaData(irrPath), Line(irrPath, Own("IRR")), "IRR scene naming an OBJ");
    ExpectOne(ViaScene(irrPath), Line(irrPath, Own("IRR")), "IRR scene naming an OBJ (scene import)");
    UnsetEnv("OKAY_OWN_IRR");
    WriteFile(dir / "tri.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    const std::string lwsPath = (dir / "t.lws").string();
    WriteFile(lwsPath, "LWSC\n3\nLoadObject tri.obj\nObjectMotion\nNumChannels 9\n" + fx::Chan(0, {{0, 1}, {1, 3}}));
    SetEnv("OKAY_OWN_LWS", "1");
    ExpectOne(ViaData(lwsPath), Line(lwsPath, Own("LWS")), "LWS scene naming an OBJ");
    UnsetEnv("OKAY_OWN_LWS");
}

// FBX: our reader, ufbx, and ufbx after ours refused the file.
static void TestFbx(const fs::path& dir) {
    const std::string rig = kFbx + "rig_bin75.fbx", cube = kFbx + "cube_zup_cm_bin.fbx";
    fbx::SetOwnReaderEnabled(true);
    ExpectOne(ViaScene(rig), Line(rig, Own("FBX")), "FBX rig, ours");
    ExpectOne(ViaModel(cube), Line(cube, Own("FBX")), "FBX cube, ours");
    fbx::SetOwnReaderEnabled(false);
    ExpectOne(ViaScene(rig), Line(rig, "ufbx"), "FBX rig, ufbx");
    ExpectOne(ViaModel(cube), Line(cube, "ufbx"), "FBX cube, ufbx");
    // The same cube marked as FBX 7.0: our reader refuses versions before 7.1, ufbx reads it.
    std::string old = ReadFile(cube);
    CHECK(old.size() > 27);
    if (old.size() > 27) { const unsigned v = 7000; for (int k = 0; k < 4; ++k) old[23 + (std::size_t)k] = (char)((v >> (8 * k)) & 0xff); }
    const std::string oldPath = (dir / "cube_v7000.fbx").string();
    WriteFile(oldPath, old);
    fbx::SetOwnReaderEnabled(true);
    const std::string refused = Line(oldPath, "ufbx (our own FBX reader refused it)");
    ExpectOne(ViaScene(oldPath), refused, "FBX 7.0, scene");
    ExpectOne(ViaModel(oldPath), refused, "FBX 7.0, one mesh");
    fbx::SetOwnReaderEnabled(false);
    ExpectOne(ViaModel(oldPath), Line(oldPath, "ufbx"), "FBX 7.0, ours off");
    ExpectNone(ViaScene((dir / "missing.fbx").string()), "missing FBX");
}

// WAV: one line per AudioClip::LoadWAV that succeeds, naming the decoder.
static void TestWav(const fs::path& dir) {
    AudioClip tone;
    tone.sampleRate = 8000; tone.channels = 1;
    for (int i = 0; i < 80; ++i) tone.samples.push_back((float)((i % 8) - 4) / 8.0f);
    const std::string p = (dir / "tone.wav").string();
    CHECK(tone.SaveWAV(p));
    SetEnv("OKAY_OWN_WAV", "1");
    ExpectOne(LinesOf([&] { AudioClip c; CHECK(c.LoadWAV(p)); }), "audio import '" + p + "': read by our own WAV decoder", "WAV, ours");
    ExpectOne(LinesOf([&] { AudioClip c; CHECK(c.Load(p)); }), "audio import '" + p + "': read by our own WAV decoder", "WAV via Load, ours");
    SetEnv("OKAY_OWN_WAV", "0");
    ExpectOne(LinesOf([&] { AudioClip c; CHECK(c.LoadWAV(p)); }), "audio import '" + p + "': read by the old WAV loader", "WAV, old loader");
    ExpectNone(LinesOf([&] { AudioClip c; CHECK(!c.LoadWAV((dir / "none.wav").string())); }), "missing WAV");
    const std::string junk = (dir / "junk.wav").string();
    WriteFile(junk, "not a wave file at all");
    SetEnv("OKAY_OWN_WAV", "1");
    ExpectNone(LinesOf([&] { AudioClip c; CHECK(!c.LoadWAV(junk)); }), "junk WAV, ours");
    UnsetEnv("OKAY_OWN_WAV");
}

int main() {
    RUN_SUITE("import reader log line and the STL / PLY switches");
    const fs::path dir = fs::temp_directory_path() / "okay_import_reader_log";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    TestAlwaysOwn();
    TestStlPlySwitches();
    TestCommittedFixtures();
    TestNewReaders(dir);
    TestFbx(dir);
    TestWav(dir);
    fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
