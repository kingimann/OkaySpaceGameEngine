// Our own DXF reader (engine/src/Render/DxfReader.cpp). SYNTHETIC fixtures (ASCII DXF text built in the test), checked
// against hand-computed numbers; plus the import seam. Real DXF files are checked by model_corpus.
// Builds on its own with -DOKAY_STANDALONE_TEST (reader source only), no engine library.
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "okay/Render/DxfReader.hpp"
#ifndef OKAY_STANDALONE_TEST
#include "okay/Render/ModelImport.hpp"
#endif
#include <clocale>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;

static std::string G(int code, const std::string& v) { char b[16]; std::snprintf(b, sizeof b, "%3d\n", code); return b + v + "\n"; }
static std::string G(int code, double v) { char b[64]; std::snprintf(b, sizeof b, "%g", v); return G(code, std::string(b)); }
static std::string Pt(int c0, double x, double y, double z) { return G(c0, x) + G(c0 + 10, y) + G(c0 + 20, z); }
static std::string Face(const std::string& layer, const double* a, const double* b, const double* c, const double* d = nullptr) {
    return G(0, "3DFACE") + G(8, layer) + Pt(10, a[0], a[1], a[2]) + Pt(11, b[0], b[1], b[2]) + Pt(12, c[0], c[1], c[2]) + (d ? Pt(13, d[0], d[1], d[2]) : "");
}
static std::string Dxf(const std::string& entities, const std::string& blocks = "") {
    return G(0, "SECTION") + G(2, "HEADER") + G(0, "ENDSEC") + (blocks.empty() ? "" : G(0, "SECTION") + G(2, "BLOCKS") + blocks + G(0, "ENDSEC")) +
           G(0, "SECTION") + G(2, "ENTITIES") + entities + G(0, "ENDSEC") + G(0, "EOF");
}
static bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) < e; }
static void Bounds(const ImportedModel& m, Vec3& lo, Vec3& hi) {
    lo = Vec3{1e30f, 1e30f, 1e30f}; hi = Vec3{-1e30f, -1e30f, -1e30f};
    for (const ImportedPart& p : m.parts) for (const Vec3& v : p.mesh.vertices) {
        lo = Vec3{std::fmin(lo.x, v.x), std::fmin(lo.y, v.y), std::fmin(lo.z, v.z)};
        hi = Vec3{std::fmax(hi.x, v.x), std::fmax(hi.y, v.y), std::fmax(hi.z, v.z)};
    }
}
static std::size_t Tris(const ImportedModel& m) { std::size_t n = 0; for (const ImportedPart& p : m.parts) n += p.mesh.triangles.size() / 3; return n; }
static bool HasNote(const ImportedModel& m, const char* part) { for (const std::string& n : m.notes) if (n.find(part) != std::string::npos) return true; return false; }
// the tests read with flatten = true: the Z-up to Y-up turn is baked in, (x, y, z) -> (x, z, -y)
static bool Read(const std::string& s, ImportedModel& m, std::string& e) { return ReadDxfBuffer(s, m, &e, true); }

int main() {
    ImportedModel m; std::string err;
    const double O[3] = {0, 0, 0}, X[3] = {1, 0, 0}, Y[3] = {0, 1, 0}, XY[3] = {1, 1, 0};
    RUN_SUITE("dxf: 3DFACE");
    {
        const double a[3] = {0, 0, 1}, b[3] = {1, 0, 1}, c[3] = {1, 1, 1}, d[3] = {0, 1, 1};
        CHECK(Read(Dxf(Face("L1", O, X, Y) + Face("L1", a, b, c, d)), m, err));
        CHECK(m.backend == "own" && m.parts.size() == 1 && Tris(m) == 3 && m.nodes.size() == 2 && m.nodes[1].name == "L1" && fx::Sane(m) && m.notes.empty());
        // two layers: two parts and two nodes
        CHECK(Read(Dxf(Face("A", O, X, Y) + Face("B", a, b, c)), m, err) && m.parts.size() == 2 && m.nodes.size() == 3);
        // two coplanar triangles sharing an edge are welded: 4 vertices, one smooth normal each
        CHECK(Read(Dxf(Face("L", O, X, XY) + Face("L", O, XY, Y)), m, err) && m.parts[0].mesh.vertices.size() == 4);
        for (const Vec3& n : m.parts[0].mesh.normals) CHECK(Near(n.y, 1));   // the file's +z is +y after the turn
        // a 90 degree fold stays sharp: 6 vertices
        const double up[3] = {0, 0, 1};
        CHECK(Read(Dxf(Face("L", O, X, Y) + Face("L", X, O, up)), m, err) && m.parts[0].mesh.vertices.size() == 6);
        // a quad with the fourth corner equal to the third is a triangle
        CHECK(Read(Dxf(Face("L", O, X, Y, Y)), m, err) && Tris(m) == 1);
        // CRLF line ends, a leading space before a code, a missing layer (layer 0)
        std::string crlf = Dxf(Face("L", O, X, Y));
        std::string r; for (char ch : crlf) { if (ch == '\n') r += "\r\n"; else r += ch; }
        CHECK(Read(r, m, err) && Tris(m) == 1);
        CHECK(Read(Dxf(G(0, "3DFACE") + Pt(10, 0, 0, 0) + Pt(11, 1, 0, 0) + Pt(12, 0, 1, 0)), m, err) && m.nodes[1].name == "0");
    }

    {   // not flattened: the root carries the turn and the vertices stay as written
        ImportedModel u;
        CHECK(ReadDxfBuffer(Dxf(Face("L", O, X, Y)), u, &err) && Near(u.nodes[0].rotation.x, -0.70710678f) && Near(u.nodes[0].rotation.w, 0.70710678f) && Near(u.parts[0].mesh.vertices[2].y, 1) );
    }
    RUN_SUITE("dxf: POLYLINE polyface and polygon mesh, MESH");
    {
        auto vert = [](double x, double y, double z, int flags) { return G(0, "VERTEX") + G(8, "P") + Pt(10, x, y, z) + G(70, (double)flags); };
        auto frec = [](int a, int b, int c, int d) { return G(0, "VERTEX") + G(8, "P") + Pt(10, 0, 0, 0) + G(70, 128.0) + G(71, (double)a) + G(72, (double)b) + G(73, (double)c) + G(74, (double)d); };
        const std::string pf = G(0, "POLYLINE") + G(8, "P") + G(66, 1.0) + G(70, 64.0) + G(71, 4.0) + G(72, 2.0) +
            vert(0, 0, 0, 192) + vert(1, 0, 0, 192) + vert(1, 1, 0, 192) + vert(0, 1, 0, 192) + frec(1, 2, 3, 0) + frec(1, 3, -4, 0) + G(0, "SEQEND") + G(8, "P");
        CHECK(Read(Dxf(pf), m, err) && Tris(m) == 2 && m.parts[0].mesh.vertices.size() == 4);   // the shared vertices are the file's own
        const std::string pq = G(0, "POLYLINE") + G(8, "P") + G(70, 64.0) + vert(0, 0, 0, 192) + vert(1, 0, 0, 192) + vert(1, 1, 0, 192) + vert(0, 1, 0, 192) + frec(1, 2, 3, 4) + G(0, "SEQEND");
        CHECK(Read(Dxf(pq), m, err) && Tris(m) == 2);
        // a face that names vertex 9 of 4 is skipped and reported
        CHECK(Read(Dxf(pf + G(0, "POLYLINE") + G(8, "Q") + G(70, 64.0) + vert(0, 0, 0, 192) + vert(1, 0, 0, 192) + vert(1, 1, 0, 192) + frec(1, 2, 3, 0) + frec(1, 2, 9, 0) + G(0, "SEQEND")), m, err) && HasNote(m, "out-of-range") && m.parts.size() == 2);
        // a polygon mesh of 3 x 2 vertices: (3-1)*(2-1)*2 = 4 triangles; closed in the M direction (flag 1): 3*1*2 = 6
        auto grid = [&](int flags) { return G(0, "POLYLINE") + G(8, "G") + G(70, (double)flags) + G(71, 3.0) + G(72, 2.0) +
            vert(0, 0, 0, 64) + vert(0, 1, 0, 64) + vert(1, 0, 0, 64) + vert(1, 1, 0, 64) + vert(2, 0, 1, 64) + vert(2, 1, 1, 64) + G(0, "SEQEND"); };
        CHECK(Read(Dxf(grid(16)), m, err) && Tris(m) == 4);
        CHECK(Read(Dxf(grid(17)), m, err) && Tris(m) == 6);
        // a 2D / 3D line polyline is not surface geometry
        CHECK(!Read(Dxf(G(0, "POLYLINE") + G(8, "L") + G(70, 8.0) + vert(0, 0, 0, 32) + vert(1, 0, 0, 32) + G(0, "SEQEND")), m, err) && err.find("POLYLINE") != std::string::npos);
        // a polyline that lost its SEQEND does not swallow the next entity
        CHECK(Read(Dxf(pq.substr(0, pq.rfind(G(0, "SEQEND"))) + Face("F", O, X, Y)), m, err) && m.parts.size() == 2);
        // MESH: 4 vertices, one quad face "4 0 1 2 3"
        const std::string mesh = G(0, "MESH") + G(8, "M") + G(71, 2.0) + G(72, 0.0) + G(91, 0.0) + G(92, 4.0) + Pt(10, 0, 0, 0) + Pt(10, 2, 0, 0) + Pt(10, 2, 2, 0) + Pt(10, 0, 2, 0) +
            G(93, 5.0) + G(90, 4.0) + G(90, 0.0) + G(90, 1.0) + G(90, 2.0) + G(90, 3.0) + G(94, 0.0) + G(95, 0.0);
        CHECK(Read(Dxf(mesh), m, err) && Tris(m) == 2 && m.parts[0].mesh.vertices.size() == 4);
        Vec3 lo, hi; Bounds(m, lo, hi); CHECK(Near(hi.x, 2) && Near(lo.z, -2));
        // a MESH that claims 1000 vertices but holds 4 is read as far as it goes, and says so
        std::string lie = mesh; lie.replace(lie.find(" 92\n4\n"), 6, " 92\n1000\n");
        CHECK(Read(Dxf(lie), m, err) && HasNote(m, "do not match"));
    }

    RUN_SUITE("dxf: blocks and INSERT");
    {
        const double B1[3] = {1, 0, 0}, B2[3] = {2, 0, 0}, B3[3] = {1, 1, 0};
        // block B has its base at (1,0,0) and one triangle (1,0,0) (2,0,0) (1,1,0); inserted at (10,0,0), scale 2, turned 90 degrees
        const std::string blk = G(0, "BLOCK") + G(8, "0") + G(2, "B") + G(70, 0.0) + Pt(10, 1, 0, 0) + G(3, "B") + Face("0", B1, B2, B3) + G(0, "ENDBLK") + G(8, "0");
        const std::string ins = G(0, "INSERT") + G(8, "Lay") + G(2, "B") + Pt(10, 10, 0, 0) + G(41, 2.0) + G(42, 2.0) + G(43, 2.0) + G(50, 90.0);
        CHECK(Read(Dxf(ins, blk), m, err));
        CHECK(m.parts.size() == 1 && Tris(m) == 1 && m.nodes[1].name == "Lay");   // layer "0" content takes the INSERT's layer
        Vec3 lo, hi; Bounds(m, lo, hi);   // (0,0) (2,0) (0,2) turned 90 degrees = (0,0) (0,2) (-2,0); moved by 10
        CHECK(Near(lo.x, 8) && Near(hi.x, 10) && Near(lo.z, -2) && Near(hi.z, 0) && Near(lo.y, 0) && Near(hi.y, 0));   // file (8..10, 0..2, 0) turned to (x, z, -y)
        // two inserts of the same block, one mirrored (negative scale): the winding follows the mirror
        CHECK(Read(Dxf(ins + G(0, "INSERT") + G(8, "Lay") + G(2, "B") + Pt(10, 0, 0, 0) + G(41, -1.0), blk), m, err) && Tris(m) == 2);
        // a block that inserts itself: stops at the nesting limit, no hang
        const std::string loop = G(0, "BLOCK") + G(2, "R") + Pt(10, 0, 0, 0) + G(0, "INSERT") + G(8, "0") + G(2, "R") + Pt(10, 1, 0, 0) + Face("0", O, X, Y) + G(0, "ENDBLK");
        CHECK(Read(Dxf(G(0, "INSERT") + G(8, "A") + G(2, "R") + Pt(10, 0, 0, 0), loop), m, err) && HasNote(m, "nested deeper"));
        // an unknown block, an array and an extrusion direction are reported
        CHECK(Read(Dxf(Face("L", O, X, Y) + G(0, "INSERT") + G(8, "L") + G(2, "nope") + Pt(10, 0, 0, 0)), m, err) && HasNote(m, "not defined"));
        CHECK(Read(Dxf(ins + G(0, "INSERT") + G(8, "L") + G(2, "B") + G(70, 3.0) + G(71, 2.0) + G(210, 0.0) + G(220, 1.0) + G(230, 0.0), blk), m, err) && HasNote(m, "array") && HasNote(m, "extrusion"));
    }

    RUN_SUITE("dxf: not surface geometry, refused files");
    {
        const std::string noise = G(0, "LINE") + G(8, "0") + Pt(10, 0, 0, 0) + Pt(11, 1, 1, 1) + G(0, "CIRCLE") + G(8, "0") + G(0, "TEXT") + G(8, "0");
        CHECK(Read(Dxf(Face("L", O, X, Y) + noise), m, err) && HasNote(m, "LINE x1") && HasNote(m, "CIRCLE") && HasNote(m, "TEXT"));
        CHECK(!Read(Dxf(noise), m, err) && err.find("no surface geometry") != std::string::npos);
        CHECK(Read(Dxf(G(0, "3DFACE") + G(8, "L") + G(62, 3.0) + Pt(10, 0, 0, 0) + Pt(11, 1, 0, 0) + Pt(12, 0, 1, 0)), m, err) && HasNote(m, "colour"));
        CHECK(!Read("", m, err) && !err.empty());
        CHECK(!Read("hello\nworld\n", m, err) && err.find("not a number") != std::string::npos);
        CHECK(!Read(std::string("AutoCAD Binary DXF\r\n\x1a\0", 22), m, err) && err.find("binary") != std::string::npos);
        CHECK(!Read("  0\nSECTION\n  2\nENTITIES\n  0\n3DFACE\n 10\n", m, err));   // the file ends after a code
        CHECK(!Read(Dxf(G(0, "3DFACE") + G(10, "abc") + Pt(11, 1, 0, 0) + Pt(12, 0, 1, 0)), m, err) && err.find("3DFACE") != std::string::npos);
        CHECK(!Read(Dxf(G(0, "3DFACE") + G(10, "1e999") + Pt(11, 1, 0, 0) + Pt(12, 0, 1, 0)), m, err));
        CHECK(!Read(Dxf(G(0, "3DFACE") + G(10, "1e300") + G(20, "0") + G(30, "0") + Pt(11, 1, 0, 0) + Pt(12, 0, 1, 0)), m, err));   // beyond the coordinate range
        const char* loc[] = {"de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "de_DE"};
        for (const char* l : loc) if (std::setlocale(LC_ALL, l)) {
            const double h[3] = {1.5, 0, 0};
            CHECK(Read(Dxf(Face("L", O, h, Y)), m, err));
            Vec3 lo, hi; Bounds(m, lo, hi); CHECK(Near(hi.x, 1.5f));
            std::setlocale(LC_ALL, "C");
            break;
        }
    }

#ifndef OKAY_STANDALONE_TEST
    RUN_SUITE("dxf: the import seam");
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / fs::u8path("okay_dxf_m\xC3\xBCller");
        fs::create_directories(dir);
        const fs::path file = dir / fs::u8path("pi\xC3\xA8ce.dxf");
        { std::ofstream f(file, std::ios::binary); const std::string x = Dxf(Face("L", O, X, Y)); f.write(x.data(), (std::streamsize)x.size()); }
        const std::string path = file.u8string();
        CHECK(HasOwnReader(".dxf"));
        CHECK(ImportModelData(path, m, ImportBackend::Own, false, &err) && m.backend == "own" && m.parts.size() == 1);
        SetImportBackend(".dxf", ImportBackend::Default);
        unsetenv("OKAY_OWN_DXF");
        if (EffectiveImportBackend(".dxf") != ImportBackend::Own) CHECK(!ImportModelData(path, m, ImportBackend::Default, false, &err) && err.find("OKAY_OWN_DXF") != std::string::npos);
        setenv("OKAY_OWN_DXF", "1", 1);
        CHECK(EffectiveImportBackend(".dxf") == ImportBackend::Own && ImportableExtensions().find(".dxf") != std::string::npos);
        bool ok = false; std::string tex;
        Mesh merged = ImportModel(path, &ok, &tex);
        CHECK(ok && merged.TriangleCount() == 1);
        setenv("OKAY_OWN_DXF", "0", 1);
        CHECK(EffectiveImportBackend(".dxf") != ImportBackend::Own);
        unsetenv("OKAY_OWN_DXF");
        std::error_code ec; fs::remove_all(dir, ec);
    }
#endif

    TEST_MAIN_RESULT();
}
