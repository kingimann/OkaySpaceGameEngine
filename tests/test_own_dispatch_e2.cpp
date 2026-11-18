// The import seam routes the batch-E own-reader formats (slots 31 and 33: .q3o / .q3s and .raw) to THEIR readers. Each fixture
// (built in memory, tests/batche2_fixtures.hpp) goes through ImportModelData with the own backend on; the model must say backend
// "own" and equal what the format's own Read*File gives directly. Also: the environment variable OKAY_OWN_<FMT> and SetImportBackend
// switch a format on and off, ImportableExtensions follows, a reader that is off by default says which switch turns it on, other
// extensions of a format reach the same reader, and a file that hostile counts or a wrong signature make unreadable is refused
// through the seam (not crashed). Slot 32 (SIB) has no reader: ".sib" must not be routed anywhere.
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "batche2_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
// When the build was configured with -DOKAY_OWN_Q3D=ON / -DOKAY_OWN_RAW=ON, tests/CMakeLists.txt defines OKAY_EXPECT_OWN_Q3D / _RAW: the own
// reader must then be the default with no run-time override. That fails if the engine did not get OKAY_OWN_<FMT>_DEFAULT (for example
// when the option is dropped from the foreach list of engine/CMakeLists.txt). With the option OFF the asserts say it is off.
#ifdef OKAY_EXPECT_OWN_Q3D
constexpr bool EXPECT_Q3D = true;
#else
constexpr bool EXPECT_Q3D = false;
#endif
#ifdef OKAY_EXPECT_OWN_RAW
constexpr bool EXPECT_RAW = true;
#else
constexpr bool EXPECT_RAW = false;
#endif
static std::size_t Verts(const ImportedModel& m) { std::size_t n = 0; for (const ImportedPart& p : m.parts) n += p.mesh.vertices.size(); return n; }

struct Case { const char* file; const char* ext; const char* env; std::string bytes; std::size_t tris; bool (*direct)(const std::string&, ImportedModel&, std::string*); bool expectOn; };
static bool DQ(const std::string& p, ImportedModel& m, std::string* e) { return ReadQ3dFile(p, m, e); }
static bool DR(const std::string& p, ImportedModel& m, std::string* e) { return ReadRawFile(p, m, e); }

int main() {
    RUN_SUITE("own_dispatch_e2");
    const fs::path dir = fs::temp_directory_path() / "okay_own_dispatch_e2";
    fs::create_directories(dir);
    const Case cases[] = {
        {"t.q3o", ".q3o", "OKAY_OWN_Q3D", fx2::BuildQ3d(), 3, DQ, EXPECT_Q3D},
        {"t.raw", ".raw", "OKAY_OWN_RAW", fx2::BuildRaw(), 4, DR, EXPECT_RAW},
    };
    for (const Case& c : cases) {   // the compile-time default, no run-time choice at all
        const std::string path = (dir / c.file).string();
        WriteFile(path, c.bytes);
        TestUnsetEnv(c.env);
        ImportedModel d; std::string e0;
        const bool ok = ImportModelData(path, d, ImportBackend::Default, false, &e0);
        if (c.expectOn) {
            if (!ok) std::fprintf(stderr, "  %s: %s\n", c.file, e0.c_str());
            CHECK(EffectiveImportBackend(c.ext) == ImportBackend::Own);
            CHECK(ok && d.backend == "own" && !d.parts.empty());
        } else if (AssimpAvailable() && std::string(c.ext) == ".raw") {
            // The RAW fixture has two groups of triangles. Assimp 5.4.3's RAW importer leaves null child nodes for such a file and
            // the import crashed; the seam now reads it with our reader even though the default for .raw is Assimp.
            std::printf("  note: %s has two groups of triangles, which Assimp's RAW importer cannot read safely: the Assimp side is skipped, our reader reads it\n", c.file);
            CHECK(RawFileUnsafeForAssimp(path));
            CHECK(EffectiveImportBackend(c.ext) != ImportBackend::Own);
            CHECK(ok && d.backend == "own" && fx::TriCount(d) == c.tris);
            ImportedModel x; std::string ex;   // asked for Assimp by name: refused with a message, not crashed
            CHECK(!ImportModelData(path, x, ImportBackend::Assimp, false, &ex) && ex.find("Assimp") != std::string::npos && x.parts.empty());
        } else {
            CHECK(!ok || d.backend != "own");
            CHECK(EffectiveImportBackend(c.ext) != ImportBackend::Own);
        }
    }
    {   // the guard: which RAW files Assimp is kept away from (RawFileUnsafeForAssimp errs on the side of "unsafe")
        using namespace std::string_literals;   // ""s keeps the NUL bytes of a case
        struct G { std::string bytes; bool unsafe; };
        const G gs[] = {
            {"0 0 0 1 0 0 0 1 0\n1 0 0 0 0 0 0 0 2\n"s, false},                          // only the default group
            {"wing\n0 0 0 1 0 0 0 1 0\n"s, false},                                      // one named group
            {"# notes\nwing\n0 0 0 1 0 0 0 1 0\n"s, false},                           // a name line without triangles is not counted
            {"0 0 0 1 0 0 0 1 0\nwing\n0 0 0 1 0 0 0 1 0\n"s, true},                   // default group and a named one
            {"a\n0 0 0 1 0 0 0 1 0\nb\n0 0 0 1 0 0 0 1 0\n"s, true},                 // two named groups
            {"a\n0 0 0 1 0 0 0 1 0\nb\na\n0 0 0 1 0 0 0 1 0\n"s, false},            // the same group named again
            {"a \n0 0 0 1 0 0 0 1 0\nb\na \n0 0 0 1 0 0 0 1 0\n"s, true},           // a blank after the name: not found again, a new group
            {"a\n0 0 0 1 0 0 0 1 0\nb\na x\n0 0 0 1 0 0 0 1 0\n"s, true},           // "a x" is not the whole name: a new group
            {"a\r\n0 0 0 1 0 0 0 1 0\r\nb\r\n-1 0 0 1 0 0 0 1 0\r\n"s, true},      // CR LF; a minus sign starts a number
            {"a\n0 0 0 1 0 0 0 1 0\n\0b\n0 0 0 1 0 0 0 1 0\n"s, false},             // the text ends at a NUL
            {"\xEF\xBB\xBF" "0 0 0 1 0 0 0 1 0\n"s, false},                           // a UTF-8 mark is skipped
            {"\xFF\xFE" "0\0"s, true},                                                 // UTF-16: not modelled, unsafe
        };
        int k = 0;
        for (const G& g : gs) {
            const std::string p = (dir / ("guard" + std::to_string(k) + ".raw")).string();
            WriteFile(p, g.bytes);
            if (RawFileUnsafeForAssimp(p) != g.unsafe) std::fprintf(stderr, "  guard case %d: expected %s\n", k, g.unsafe ? "unsafe" : "safe");
            CHECK(RawFileUnsafeForAssimp(p) == g.unsafe);
            ++k;
        }
        std::string longLine(5000, '1');
        WriteFile((dir / "long.raw").string(), longLine + "\n");
        CHECK(RawFileUnsafeForAssimp((dir / "long.raw").string()));
        CHECK(!RawFileUnsafeForAssimp((dir / "no_such_file.raw").string()));
        // the corpus files (docs/model_corpus.md): Assimp crashed on WithColor and WithTexture; Wuson has one group and stays with Assimp
        if (const char* corpus = std::getenv("OKAY_MODEL_CORPUS")) {
            const fs::path r = fs::path(corpus) / "models" / "RAW";
            if (fs::exists(r / "Wuson.raw")) {
                CHECK(RawFileUnsafeForAssimp((r / "WithColor.raw").string()));
                CHECK(RawFileUnsafeForAssimp((r / "WithTexture.raw").string()));
                CHECK(!RawFileUnsafeForAssimp((r / "Wuson.raw").string()));
                if (AssimpAvailable()) {
                    std::printf("  note: WithColor.raw and WithTexture.raw are not given to Assimp (known crash); our reader reads them\n");
                    for (const char* nm : {"WithColor.raw", "WithTexture.raw"}) {
                        ImportedModel m; std::string e;
                        CHECK(ImportModelData((r / nm).string(), m, ImportBackend::Default, false, &e) && m.backend == "own" && !m.parts.empty());
                    }
                    ImportedModel w; std::string e;
                    CHECK(ImportModelData((r / "Wuson.raw").string(), w, ImportBackend::Default, false, &e) && w.backend == (EXPECT_RAW ? "own" : "assimp"));
                }
            }
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
        if (!AssimpAvailable() && !c.expectOn) {   // off by default: not importable, and the message names the switch
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
            WriteFile(path, fx2::BuildQ3d(false, name[0] == 's', true));
            ImportedModel m; std::string err;
            CHECK(HasOwnReader(name[0] == 's' ? ".q3s" : ".Q3O"));
            CHECK(ImportModelData(path, m, ImportBackend::Own, false, &err) && m.backend == "own" && fx::TriCount(m) == 3);
        }
        // a hostile or wrong file is refused through the seam, with a message
        const std::string bad = (dir / "bad.q3o").string();
        std::string s = fx2::BuildQ3d();
        for (int k = 0; k < 4; ++k) s[23 + (std::size_t)k] = (char)0xFF;
        WriteFile(bad, s);
        ImportedModel m; std::string err;
        CHECK(!ImportModelData(bad, m, ImportBackend::Own, false, &err) && !err.empty() && m.parts.empty());
        WriteFile(bad, "this is not a quick3D file at all, only text");
        CHECK(!ImportModelData(bad, m, ImportBackend::Own, false, &err) && err.find("Q3D") != std::string::npos);
        const std::string badRaw = (dir / "bad.raw").string();
        WriteFile(badRaw, "1 2 3\n");
        CHECK(!ImportModelData(badRaw, m, ImportBackend::Own, false, &err) && err.find("RAW") != std::string::npos);
        // a face whose material index is not in the file: the part has material -1, and the seam takes it
        {
            std::string q = fx2::BuildQ3d();
            q[123] = 7;   // material index of face 0 (the quad): out of range
            const std::string qp = (dir / "nomat.q3o").string();
            WriteFile(qp, q);
            ImportedModel nm;
            CHECK(ImportModelData(qp, nm, ImportBackend::Own, false, &err) && nm.backend == "own" && fx::Sane(nm));
            bool none = false; for (const ImportedPart& p : nm.parts) if (p.material == -1) none = true;
            CHECK(none && fx::TriCount(nm) == 3);
        }
        // SIB has no own reader (slot 32 is reserved, empty)
        CHECK(!HasOwnReader(".sib"));
    }
    SetImportBackend("*", ImportBackend::Default);
    std::error_code ec; fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
