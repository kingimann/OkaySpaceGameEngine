// Our own Terragen (.ter) reader: synthetic files built in memory (tests/terragen_test_build.hpp) with values worked out
// by hand from the published specification, the optional chunks and their defaults, curve mode 1, every refusal, and the
// import seam (opt-in switch, UTF-8 path). The real files are checked by test_model_corpus (docs/model_corpus.md).
#include "test_framework.hpp"
#include "terragen_test_build.hpp"
#include <Okay.hpp>
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;
using namespace terbuild;

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
static bool HasNote(const ImportedModel& m, const char* part) {
    for (const std::string& n : m.notes) if (n.find(part) != std::string::npos) return true;
    return false;
}
static bool Near(const Vec3& a, const Vec3& b, double eps) {
    return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps && std::fabs(a.z - b.z) <= eps;
}
static const Mesh& M(const ImportedModel& m) { return m.parts[0].mesh; }

int main() {
    RUN_SUITE("terragen_reader");
    std::string err;

    {   // exact values: 3 by 2 points, HeightScale 16384 (a quarter), BaseHeight 10, uniform scale 2 m per unit
        // altitude (terrain units) = 10 + e * 16384 / 65536 = 10 + e / 4; metres = altitude * 2
        const std::vector<std::int16_t> e = {0, 4, 8, -4, 40, 100};
        ImportedModel m;
        CHECK(ReadTerragenBuffer(Grid(3, 2, 16384, 10, e, 2.0f), m, &err));
        CHECK(m.backend == "own" && m.nodes.size() == 1 && m.parts.size() == 1 && m.parts[0].node == 0 && m.notes.empty());
        if (m.parts.size() == 1) {
            const Mesh& me = M(m);
            CHECK(me.vertices.size() == 6 && me.normals.size() == 6 && me.uvs.size() == 6 && me.TriangleCount() == 4);
            const double alt[6] = {10, 11, 12, 9, 20, 35};
            for (int j = 0; j < 2; ++j)
                for (int i = 0; i < 3; ++i) {
                    const int k = j * 3 + i;
                    // Z-up to Y-up: (x, y, z) -> (x, z, -y)
                    CHECK(Near(me.vertices[(std::size_t)k], Vec3{(float)(i * 2), (float)(alt[k] * 2), (float)(-j * 2)}, 1e-4));
                    CHECK_NEAR(me.uvs[(std::size_t)k].x, i / 2.0, 1e-6);
                    CHECK_NEAR(me.uvs[(std::size_t)k].y, (double)j, 1e-6);
                }
            for (const Vec3& n : me.normals) CHECK_NEAR(n.Magnitude(), 1.0, 1e-5);
            for (int t : me.triangles) CHECK(t >= 0 && t < 6);
        }
    }
    {   // winding: on a flat grid every triangle faces up (+Y), and the engine's own smooth normals agree with ours
        ImportedModel m;
        CHECK(ReadTerragenBuffer(Grid(4, 3, 0, 0, std::vector<std::int16_t>(12, 0)), m, &err));
        if (m.parts.size() == 1) {
            const Mesh& me = M(m);
            for (std::size_t t = 0; t + 2 < me.triangles.size(); t += 3) {
                const Vec3 a = me.vertices[(std::size_t)me.triangles[t]], b = me.vertices[(std::size_t)me.triangles[t + 1]], c = me.vertices[(std::size_t)me.triangles[t + 2]];
                const Vec3 n = Vec3::Cross(b - a, c - a);
                CHECK(n.y > 0 && std::fabs(n.x) < 1e-6 && std::fabs(n.z) < 1e-6);
            }
            for (const Vec3& n : me.normals) CHECK(Near(n, Vec3{0, 1, 0}, 1e-6));
            Mesh copy = me;
            copy.ComputeSmoothNormals();
            for (std::size_t k = 0; k < copy.normals.size(); ++k) CHECK(Near(copy.normals[k], me.normals[k], 1e-4));
        }
    }
    {   // normals of a tilted plane: elevation rises 1 unit per point along x (HeightScale 65536 is not an int16, so use
        // BaseHeight 0, HeightScale 32767 and e = 2 per point: 2 * 32767 / 65536 = 0.99998... units per point)
        std::vector<std::int16_t> e;
        for (int j = 0; j < 4; ++j) for (int i = 0; i < 5; ++i) e.push_back((std::int16_t)(2 * i));
        ImportedModel m;
        CHECK(ReadTerragenBuffer(Grid(5, 4, 32767, 0, e, 30.0f), m, &err));
        if (m.parts.size() == 1) {
            // Y = slope * X with slope = (2 * 32767 / 65536 * 30) / 30; normal (-slope, 1, 0) normalised
            const double slope = 2.0 * 32767.0 / 65536.0;
            const double len = std::sqrt(slope * slope + 1.0);
            for (const Vec3& n : M(m).normals) CHECK(Near(n, Vec3{(float)(-slope / len), (float)(1.0 / len), 0.0f}, 1e-5));
        }
        // the same tilt along y (rows): Z = -y * s, so the normal leans toward +Z
        e.clear();
        for (int j = 0; j < 4; ++j) for (int i = 0; i < 5; ++i) e.push_back((std::int16_t)(2 * j));
        CHECK(ReadTerragenBuffer(Grid(5, 4, 32767, 0, e, 30.0f), m, &err));
        if (m.parts.size() == 1) {
            const double slope = 2.0 * 32767.0 / 65536.0;
            const double len = std::sqrt(slope * slope + 1.0);
            for (const Vec3& n : M(m).normals) CHECK(Near(n, Vec3{0.0f, (float)(1.0 / len), (float)(slope / len)}, 1e-5));
        }
    }
    {   // defaults: no SCAL means 30 m per unit; SIZE alone means a square of SIZE + 1 points; the EOF marker is optional
        const std::string f = Header() + Dim("SIZE", 1) + Altw(0, 1, {0, 0, 0, 0}) + Eof();
        ImportedModel m;
        CHECK(ReadTerragenBuffer(f, m, &err) && m.notes.empty());
        if (m.parts.size() == 1) {
            CHECK(M(m).vertices.size() == 4);
            CHECK(Near(M(m).vertices[3], Vec3{30, 30, -30}, 1e-4));   // BaseHeight 1 unit = 30 m
        }
        CHECK(ReadTerragenBuffer(Header() + Dim("SIZE", 1) + Altw(0, 1, {0, 0, 0, 0}), m, &err) && HasNote(m, "EOF"));
    }
    {   // the optional chunks in any order before the data, and odd point counts padded to 4 bytes
        const std::string f = Header() + Crvm(0) + Scal(10, 10, 10) + Dim("YPTS", 3) + Dim("SIZE", 2) + Crad(6370) + Dim("XPTS", 3) +
                              Altw(0, 0, std::vector<std::int16_t>(9, 0)) + Eof();
        ImportedModel m;
        CHECK(ReadTerragenBuffer(f, m, &err) && m.notes.empty() && M(m).vertices.size() == 9);
    }
    {   // non-square: XPTS and YPTS win; a SIZE that disagrees with them is reported; only one of the two is reported
        ImportedModel m;
        CHECK(ReadTerragenBuffer(Grid(4, 2, 0, 0, std::vector<std::int16_t>(8, 0)), m, &err) && m.notes.empty() && M(m).vertices.size() == 8);
        const std::string wrongSize = Header() + Dim("SIZE", 9) + Dim("XPTS", 4) + Dim("YPTS", 2) + Altw(0, 0, std::vector<std::int16_t>(8, 0)) + Eof();
        CHECK(ReadTerragenBuffer(wrongSize, m, &err) && HasNote(m, "XPTS and YPTS were used") && M(m).vertices.size() == 8);
        const std::string onlyX = Header() + Dim("SIZE", 1) + Dim("XPTS", 3) + Altw(0, 0, std::vector<std::int16_t>(6, 0)) + Eof();
        CHECK(ReadTerragenBuffer(onlyX, m, &err) && HasNote(m, "only XPTS") && M(m).vertices.size() == 6);
    }
    {   // a scale that differs per axis is applied per axis (and reported: Terragen itself cannot use it)
        const std::string f = Header() + Dim("SIZE", 1) + Scal(2, 3, 5) + Altw(0, 1, {0, 0, 0, 0}) + Eof();
        ImportedModel m;
        CHECK(ReadTerragenBuffer(f, m, &err) && HasNote(m, "not the same on x, y and z"));
        if (m.parts.size() == 1) CHECK(Near(M(m).vertices[3], Vec3{2, 5, -3}, 1e-5));
    }
    {   // curve mode 1: a sphere of radius CRAD * 1000 / zscale terrain units, top at the middle of the grid.
        // 5 by 5 points, scale 30, CRAD 3 km: radius 100 units. A corner is sqrt(2^2 + 2^2) = sqrt(8) units from the middle,
        // so it drops by 100 - sqrt(10000 - 8) units; the middle does not move.
        const std::string f = Header() + Dim("SIZE", 4) + Scal(30, 30, 30) + Crad(3) + Crvm(1) + Altw(0, 0, std::vector<std::int16_t>(25, 0)) + Eof();
        ImportedModel m;
        CHECK(ReadTerragenBuffer(f, m, &err) && m.notes.empty());
        if (m.parts.size() == 1) {
            const double drop = (100.0 - std::sqrt(10000.0 - 8.0)) * 30.0;
            CHECK_NEAR(M(m).vertices[12].y, 0.0, 1e-6);            // the middle point (2, 2)
            CHECK_NEAR(M(m).vertices[0].y, -drop, 1e-4);           // the corner (0, 0)
            CHECK_NEAR(M(m).vertices[2].y, -(100.0 - std::sqrt(10000.0 - 4.0)) * 30.0, 1e-4);   // (2, 0): 2 units away
        }
        // a sphere smaller than the terrain cannot hold it
        const std::string tiny = Header() + Dim("SIZE", 4) + Crad(0.03f) + Crvm(1) + Altw(0, 0, std::vector<std::int16_t>(25, 0)) + Eof();
        CHECK(!ReadTerragenBuffer(tiny, m, &err) && err.find("CRAD") != std::string::npos);
        // an undefined curve mode is reported and drawn flat
        const std::string mode7 = Header() + Dim("SIZE", 1) + Crvm(7) + Altw(0, 0, {0, 0, 0, 0}) + Eof();
        CHECK(ReadTerragenBuffer(mode7, m, &err) && HasNote(m, "curve mode 7"));
        // curve mode 1 without CRAD uses the Earth default
        const std::string noCrad = Header() + Dim("SIZE", 1) + Crvm(1) + Altw(0, 0, {0, 0, 0, 0}) + Eof();
        CHECK(ReadTerragenBuffer(noCrad, m, &err) && HasNote(m, "6370 km"));
    }
    {   // what is tolerated and reported: no padding before EOF, data after EOF, unknown data after the elevations,
        // SCAL after the elevations (still applied)
        ImportedModel m;
        const std::string noPad = Header() + Dim("SIZE", 2) + Altw(0, 0, std::vector<std::int16_t>(9, 0), false) + Eof();
        CHECK(ReadTerragenBuffer(noPad, m, &err) && HasNote(m, "no padding"));
        CHECK(ReadTerragenBuffer(Grid(2, 2, 0, 0, {0, 0, 0, 0}) + "junk", m, &err) && HasNote(m, "after the 'EOF '"));
        const std::string tail = Header() + Dim("SIZE", 1) + Altw(0, 0, {0, 0, 0, 0}) + "ABCDxxxxxxxx";
        CHECK(ReadTerragenBuffer(tail, m, &err) && HasNote(m, "unknown data after the elevations"));
        const std::string late = Header() + Dim("SIZE", 1) + Altw(0, 1, {0, 0, 0, 0}) + Scal(10, 10, 10) + Eof();
        CHECK(ReadTerragenBuffer(late, m, &err) && HasNote(m, "still applied"));
        if (m.parts.size() == 1) CHECK(Near(M(m).vertices[3], Vec3{10, 10, -10}, 1e-5));
        const std::string shortTail = Header() + Dim("SIZE", 1) + Altw(0, 0, {0, 0, 0, 0}) + "EO";
        CHECK(ReadTerragenBuffer(shortTail, m, &err) && HasNote(m, "too short for a chunk"));
    }
    {   // refusals: each leaves the model empty and says why
        const std::vector<std::int16_t> four = {0, 0, 0, 0};
        const struct { std::string file; const char* why; } bad[] = {
            {"", "not a Terragen"},
            {"TERRAGENTERRAIN", "not a Terragen"},
            {"TERRAGENterrain " + Dim("SIZE", 1) + Altw(0, 0, four), "not a Terragen"},
            {Header(), "no SIZE"},
            {Header() + Dim("SIZE", 1) + Eof(), "no ALTW"},
            {Header() + Altw(0, 0, four) + Dim("SIZE", 1), "before the SIZE"},
            {Header() + Dim("SIZE", 1) + Altw(0, 0, four) + Altw(0, 0, four), "two ALTW"},
            {Header() + Dim("SIZE", 1) + Altw(0, 0, four) + Dim("XPTS", 2), "after the elevation"},
            {Header() + Dim("SIZE", 1) + Altw(0, 0, {0, 0, 0}, false), "ends inside the elevation"},
            {Header() + Dim("SIZE", 0) + Altw(0, 0, {0}), "at least 2 points"},
            {Header() + Dim("SIZE", 1) + Dim("XPTS", 1) + Dim("YPTS", 9) + Altw(0, 0, std::vector<std::int16_t>(9, 0)), "at least 2 points"},
            {Header() + Dim("SIZE", 2049) + Altw(0, 0, {}), "the limit is 2049"},
            {Header() + Dim("SIZE", 1) + Dim("XPTS", 4098) + Dim("YPTS", 2) + Altw(0, 0, {}), "the limit is 2049"},
            {Header() + Dim("SIZE", 65535) + Altw(0, 0, {}), "the limit is 2049"},
            {Header() + Dim("SIZE", 1) + Scal(0, 0, 0) + Altw(0, 0, four), "bad SCAL"},
            {Header() + Dim("SIZE", 1) + Scal(-30, 30, 30) + Altw(0, 0, four), "bad SCAL"},
            {Header() + Dim("SIZE", 1) + Scal(std::nanf(""), 30, 30) + Altw(0, 0, four), "bad SCAL"},
            {Header() + Dim("SIZE", 1) + Scal(30, 1e7f, 30) + Altw(0, 0, four), "bad SCAL"},
            {Header() + Dim("SIZE", 1) + Scal(1e-7f, 30, 30) + Altw(0, 0, four), "bad SCAL"},
            {Header() + Dim("SIZE", 1) + Crad(-1) + Crvm(1) + Altw(0, 0, four), "bad CRAD"},
            {Header() + Dim("SIZE", 1) + Crad(std::nanf("")) + Crvm(1) + Altw(0, 0, four), "bad CRAD"},
            {Header() + Dim("SIZE", 1) + "WXYZ" + Altw(0, 0, four), "unknown chunk 'WXYZ'"},
            {Header() + Dim("SIZE", 1) + std::string("SC"), "no ALTW"},
            {Header() + "SIZE" + "\x01", "ends inside the SIZE"},
            {Header() + Dim("SIZE", 1) + "SCAL" + std::string(4, '\0'), "ends inside the SCAL"},
            {Header() + Dim("SIZE", 1) + "CRAD", "ends inside the CRAD"},
            {Header() + Dim("SIZE", 1) + "CRVM" + "\x01\x00", "ends inside the CRVM"},
        };
        for (const auto& b : bad) {
            ImportedModel m;
            err.clear();
            const bool ok = ReadTerragenBuffer(b.file, m, &err);
            CHECK(!ok && m.nodes.empty() && m.parts.empty() && err.rfind("Terragen: ", 0) == 0);
            if (err.find(b.why) == std::string::npos) std::printf("  refusal text '%s' does not contain '%s'\n", err.c_str(), b.why);
            CHECK(err.find(b.why) != std::string::npos);
        }
        // CRAD that is never used (flat terrain) does not matter
        ImportedModel m;
        CHECK(ReadTerragenBuffer(Header() + Dim("SIZE", 1) + Crad(-1) + Altw(0, 0, four) + Eof(), m, &err));
    }
    {   // the largest grid allowed: 2049 by 2049 points
        const std::string f = Header() + Dim("SIZE", 2048) + Altw(1, 0, std::vector<std::int16_t>(2049u * 2049u, 7)) + Eof();
        ImportedModel m;
        CHECK(ReadTerragenBuffer(f, m, &err) && M(m).vertices.size() == 2049u * 2049u && M(m).TriangleCount() == 2 * 2048 * 2048);
    }
    {   // a UTF-8 path with characters outside the ANSI code page
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / fs::u8path(u8"okay_ter_Müller_地形");
        std::error_code ec;
        fs::create_directories(dir, ec);
        const fs::path file = dir / fs::u8path(u8"地形.ter");
        { std::ofstream f(file, std::ios::binary); f << Grid(3, 3, 0, 0, std::vector<std::int16_t>(9, 0)); }
        ImportedModel m;
        CHECK(ReadTerragenFile(file.u8string(), m, &err) && m.parts.size() == 1);
        CHECK(!ReadTerragenFile((dir / "missing.ter").u8string(), m, &err) && m.parts.empty());

        // the seam: off by default, OKAY_OWN_TER=1 or SetImportBackend switch it on
        const std::string path = file.u8string();
        CHECK(HasOwnReader(".ter") && HasOwnReader(".TER"));
        TestUnsetEnv("OKAY_OWN_TER");
        SetImportBackend(".ter", ImportBackend::Default);
        if (!AssimpAvailable() && EffectiveImportBackend(".ter") == ImportBackend::Default) {
            CHECK(!ImportModelData(path, m, ImportBackend::Default, false, &err));
            CHECK(err.find("OKAY_OWN_TER") != std::string::npos);
            CHECK(ImportableExtensions().find(".ter") == std::string::npos);
        }
        TestSetEnv("OKAY_OWN_TER", "1");
        CHECK(EffectiveImportBackend(".ter") == ImportBackend::Own);
        CHECK(ImportableExtensions().find(".ter") != std::string::npos);
        CHECK(ImportModelData(path, m, ImportBackend::Default, false, &err) && m.backend == "own" && m.parts.size() == 1);
        TestSetEnv("OKAY_OWN_TER", "0");
        CHECK(EffectiveImportBackend(".ter") != ImportBackend::Own);
        CHECK(SetImportBackend(".ter", ImportBackend::Own));
        CHECK(EffectiveImportBackend(".ter") == ImportBackend::Own);
        Scene scene; scene.physicsEnabled = false;
        bool ok = false;
        GameObject* root = ImportModelScene(scene, path, &ok);
        CHECK(ok && root != nullptr);
        bool mok = false; Mesh merged = ImportModel(path, &mok);
        CHECK(mok && merged.TriangleCount() == 8);
        CHECK(ImportModelData(path, m, ImportBackend::Default, true, &err) && m.nodes.size() == 1 && m.parts.size() == 1 && m.parts[0].node == 0);
        SetImportBackend(".ter", ImportBackend::Default);
        TestUnsetEnv("OKAY_OWN_TER");
        fs::remove_all(dir, ec);
    }

    TEST_MAIN_RESULT();
}
