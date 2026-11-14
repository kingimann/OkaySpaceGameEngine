// Our own OFF reader: fixtures from scripts/gen_off_fixtures.py (tests/data/models/*.off, synthetic), values computed by hand,
// the import seam (opt-in switch, UTF-8 paths, locale) and malformed input.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;

static void TestSetEnv(const char* n, const char* v) {
#ifdef _WIN32
    _putenv_s(n, v);
#else
    setenv(n, v, 1);
#endif
}
static void TestUnsetEnv(const char* n) {
#ifdef _WIN32
    _putenv_s(n, "");
#else
    unsetenv(n);
#endif
}
static const std::string kDir = std::string(OKAY_SOURCE_DIR) + "/tests/data/models/";
static bool HasNote(const ImportedModel& m, const char* part) {
    for (const std::string& n : m.notes) if (n.find(part) != std::string::npos) return true;
    return false;
}
static double Area(const Mesh& m) {
    double a = 0;
    for (std::size_t i = 0; i + 2 < m.triangles.size(); i += 3) {
        const Vec3 c = Vec3::Cross(m.vertices[(std::size_t)m.triangles[i + 1]] - m.vertices[(std::size_t)m.triangles[i]], m.vertices[(std::size_t)m.triangles[i + 2]] - m.vertices[(std::size_t)m.triangles[i]]);
        a += 0.5 * c.Magnitude();
    }
    return a;
}

int main() {
    RUN_SUITE("off_reader");
    std::string err;

    for (const char* file : {"cube.off", "cube_crlf.off"}) {
        ImportedModel m;
        CHECK(ReadOffFile(kDir + file, m, &err));
        CHECK(m.backend == "own" && m.nodes.size() == 1 && m.parts.size() == 1 && m.clips.empty());
        if (m.parts.size() != 1) continue;
        const Mesh& me = m.parts[0].mesh;
        CHECK(me.vertices.size() == 8 && me.triangles.size() == 36);   // 6 quads -> 12 triangles
        CHECK(me.normals.size() == 8 && me.uvs.empty());               // smooth normals generated
        CHECK_NEAR(Area(me), 6.0, 1e-5);
        CHECK(m.notes.empty());
        // wound counter clockwise seen from outside: the face normal of triangle 0 (the z = 0 face) points to -z
        const Vec3 c = Vec3::Cross(me.vertices[(std::size_t)me.triangles[1]] - me.vertices[(std::size_t)me.triangles[0]], me.vertices[(std::size_t)me.triangles[2]] - me.vertices[(std::size_t)me.triangles[0]]);
        CHECK(c.z < 0);
    }
    {   // per-vertex normals and texture coordinates are kept
        ImportedModel m;
        CHECK(ReadOffFile(kDir + "square_stnoff.off", m, &err));
        CHECK(m.parts.size() == 1);
        if (m.parts.size() == 1) {
            const Mesh& me = m.parts[0].mesh;
            CHECK(me.vertices.size() == 4 && me.triangles.size() == 6 && me.normals.size() == 4 && me.uvs.size() == 4);
            CHECK_NEAR(me.normals[2].z, 1.0f, 1e-6); CHECK_NEAR(me.uvs[2].x, 1.0f, 1e-6); CHECK_NEAR(me.uvs[2].y, 1.0f, 1e-6);
            CHECK_NEAR(me.uvs[3].x, 0.0f, 1e-6); CHECK_NEAR(me.uvs[3].y, 1.0f, 1e-6);   // kept as written
        }
        CHECK(HasNote(m, "texture coordinates are kept as written"));
    }
    {   // colors, a concave polygon, a short face, a bad index: all reported
        ImportedModel m;
        CHECK(ReadOffFile(kDir + "mixed_coff.off", m, &err));
        CHECK(m.parts.size() == 1);
        if (m.parts.size() == 1) {
            const Mesh& me = m.parts[0].mesh;
            CHECK(me.triangles.size() == 5 * 3);
            CHECK_NEAR(Area(me), 4.0, 1e-5);   // the L (area 3) + a right triangle (area 1)
        }
        CHECK(HasNote(m, "per-vertex colors not imported"));
        CHECK(HasNote(m, "2 face(s) carry colors"));
        CHECK(HasNote(m, "1 face(s) with fewer than 3 corners"));
        CHECK(HasNote(m, "1 face(s) with an out-of-range"));
    }
    {   // header variants and malformed input
        ImportedModel m;
        const std::string tri = "0 0 0\n1 0 0\n0 1 0\n3 0 1 2\n";
        CHECK(ReadOffBuffer("OFF 3 1 0\n" + tri, m, &err) && m.parts[0].mesh.triangles.size() == 3);   // counts on the OFF line
        CHECK(ReadOffBuffer("off\n3 1\n" + tri, m, &err));                                             // lower case, no edge count
        CHECK(ReadOffBuffer("\xEF\xBB\xBF" "OFF\n3 1 0\n" + tri, m, &err));                              // UTF-8 byte order mark
        CHECK(ReadOffBuffer("OFF\n3 1 0\n0 0 0 # origin\n1 0 0\n0 1 0\n3 0 1 2 # one face\n", m, &err));   // comments
        CHECK(ReadOffBuffer("OFF\n3 1 0\n0 0 0\n1e0 0 0\n0 +1.5e0 0\n3 0 1 2", m, &err));              // exponent forms, no final newline
        CHECK(ReadOffBuffer("OFF\r3 1 0\r0 0 0\r1 0 0\r0 1 0\r3 0 1 2\r", m, &err));                      // \r only
        const char* bad[] = {
            "", "OFF", "OFF\n", "PLY\n3 1 0\n", "OFF\n3\n", "OFF\n-3 1 0\n", "OFF\n3 1 0\n0 0 0\n1 0 0\n",
            "OFF\n3 1 0\n0 0 0\n1 0 0\n0 1 0\n", "OFF\n3 1 0\n0 0 0\n1 0 0\n0 1 0\n3 0 1\n", "OFF\n3 1 0\n0 0 0\n1 0 0\n0 1 0\n3 0 1 x\n",
            "OFF\n3 1 0\n0 0 0\n1 0 nan\n0 1 0\n3 0 1 2\n", "OFF\n3 1 0\n0 0 0\n1 0 inf\n0 1 0\n3 0 1 2\n", "OFF\n3 1 0\n0 0 0\n1 0 0x1\n0 1 0\n3 0 1 2\n",
            "OFF\n3 1 0\n0 0 0\n1 0 1e99\n0 1 0\n3 0 1 2\n", "OFF\n2000000000 1 0\n", "OFF\n3 2000000000 0\n", "OFF\n3 1 0\n0 0\n1 0 0\n0 1 0\n3 0 1 2\n",
            "4OFF\n3 1 0\n0 0 0 1\n1 0 0 1\n0 1 0 1\n3 0 1 2\n", "nOFF\n3\n3 1 0\n0 0 0\n1 0 0\n0 1 0\n3 0 1 2\n", "OFF\n3 1 0\n0 0 0\n1 0 0\n0 1 0\n3 0 0 0\n",   // no triangles left
            "OFF\n3 1 0\n0 0 0\n1 0 0\n0 1 0\n99999999 0 1 2\n", "OFF\n3 1 0\n0 0 0\n1 0 0\n0 1 0\n-3 0 1 2\n", "OFFX\n3 1 0\n",
        };
        for (const char* b : bad) { CHECK(!ReadOffBuffer(b, m, &err) && !err.empty() && m.nodes.empty() && m.parts.empty()); }
        CHECK(!ReadOffFile(kDir + "does_not_exist.off", m, &err) && err == "cannot open file");
    }
    {   // UTF-16 text (with a byte order mark) is converted
        ImportedModel m;
        const std::string ascii = "OFF\n3 1 0\n0 0 0\n1 0 0\n0 1 0\n3 0 1 2\n";
        std::string le = "\xFF\xFE", be = "\xFE\xFF";
        for (char c : ascii) { le += c; le += '\0'; be += '\0'; be += c; }
        CHECK(ReadOffBuffer(le, m, &err) && m.parts.size() == 1 && HasNote(m, "UTF-16"));
        CHECK(ReadOffBuffer(be, m, &err) && m.parts.size() == 1);
        CHECK(!ReadOffBuffer(le.substr(0, le.size() - 1), m, &err));   // odd length
    }
    {   // a decimal-comma locale does not change what a '.' number means
        ImportedModel m;
        const char* got = std::setlocale(LC_ALL, "de_DE.UTF-8");
        if (!got) got = std::setlocale(LC_ALL, "de_DE");
        if (got) {
            CHECK(ReadOffBuffer("OFF\n3 1 0\n0 0 0\n1.5 0 0\n0 2.25 0\n3 0 1 2\n", m, &err));
            if (m.parts.size() == 1) { CHECK_NEAR(m.parts[0].mesh.vertices[1].x, 1.5f, 1e-6); CHECK_NEAR(m.parts[0].mesh.vertices[2].y, 2.25f, 1e-6); }
            std::setlocale(LC_ALL, "C");
        } else std::printf("  (no German locale installed: locale check skipped)\n");
    }
    {   // a UTF-8 path with characters outside the ANSI code page
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / fs::u8path(u8"okay_off_Müller_立方");
        std::error_code ec;
        fs::create_directories(dir, ec);
        const fs::path file = dir / fs::u8path(u8"立方.off");
        { std::ofstream f(file, std::ios::binary); f << "OFF\n3 1 0\n0 0 0\n1 0 0\n0 1 0\n3 0 1 2\n"; }
        ImportedModel m;
        const std::string u8 = file.u8string();
        CHECK(ReadOffFile(u8, m, &err) && m.parts.size() == 1);
        fs::remove_all(dir, ec);
    }

    {   // the seam
        CHECK(HasOwnReader(".off") && HasOwnReader(".OFF"));
        TestUnsetEnv("OKAY_OWN_OFF");
        SetImportBackend(".off", ImportBackend::Default);
        ImportedModel m;
        if (!AssimpAvailable() && EffectiveImportBackend(".off") == ImportBackend::Default) {
            CHECK(!ImportModelData(kDir + "cube.off", m, ImportBackend::Default, false, &err));
            CHECK(err.find("OKAY_OWN_OFF") != std::string::npos);
            CHECK(ImportableExtensions().find(".off") == std::string::npos);
        }
        TestSetEnv("OKAY_OWN_OFF", "1");
        CHECK(EffectiveImportBackend(".off") == ImportBackend::Own);
        CHECK(ImportableExtensions().find(".off") != std::string::npos);
        CHECK(ImportModelData(kDir + "cube.off", m, ImportBackend::Default, false, &err) && m.backend == "own" && m.parts.size() == 1);
        TestSetEnv("OKAY_OWN_OFF", "0");
        CHECK(EffectiveImportBackend(".off") != ImportBackend::Own);
        CHECK(SetImportBackend(".off", ImportBackend::Own));
        CHECK(EffectiveImportBackend(".off") == ImportBackend::Own);
        Scene scene; scene.physicsEnabled = false;
        bool ok = false;
        GameObject* root = ImportModelScene(scene, kDir + "cube.off", &ok);
        CHECK(ok && root != nullptr);
        bool mok = false; Mesh merged = ImportModel(kDir + "cube.off", &mok);
        CHECK(mok && merged.TriangleCount() == 12);
        // the one-static-mesh mode bakes the (identity) node transforms and leaves one root
        CHECK(ImportModelData(kDir + "cube.off", m, ImportBackend::Default, true, &err) && m.nodes.size() == 1 && m.parts.size() == 1 && m.parts[0].node == 0);
        SetImportBackend(".off", ImportBackend::Default);
        TestUnsetEnv("OKAY_OWN_OFF");
    }

    TEST_MAIN_RESULT();
}
