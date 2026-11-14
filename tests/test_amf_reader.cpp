// Our own AMF reader (engine/src/Render/AmfReader.cpp). SYNTHETIC fixtures built in memory (plain XML and zip-compressed
// AMF), checked against hand-computed numbers; plus the import seam. Real AMF files are checked by model_corpus.
// Builds on its own with -DOKAY_STANDALONE_TEST (reader sources + ImageDecode only), no engine library.
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "okay/Render/AmfReader.hpp"
#ifndef OKAY_STANDALONE_TEST
#include "okay/Render/ModelImport.hpp"
#endif
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;

static std::string V(double x, double y, double z) {
    char b[160]; std::snprintf(b, sizeof b, "<vertex><coordinates><x>%g</x><y>%g</y><z>%g</z></coordinates></vertex>", x, y, z); return b;
}
static std::string T(int a, int b, int c, const std::string& extra = "") {
    return "<triangle>" + extra + "<v1>" + std::to_string(a) + "</v1><v2>" + std::to_string(b) + "</v2><v3>" + std::to_string(c) + "</v3></triangle>";
}
static std::string Col(double r, double g, double b, double a = 1) {
    char buf[200]; std::snprintf(buf, sizeof buf, "<color><r>%g</r><g>%g</g><b>%g</b><a>%g</a></color>", r, g, b, a); return buf;
}
// a unit tetrahedron (0,0,0) (1,0,0) (0,1,0) (0,0,1), outward counter-clockwise; the faces are split into two volumes
static std::string Tetra(int id, const std::string& objExtra = "", const std::string& vol1 = "", const std::string& vol2 = "", const std::string& t1extra = "") {
    return "<object id=\"" + std::to_string(id) + "\">" + objExtra + "<mesh><vertices>" + V(0, 0, 0) + V(1, 0, 0) + V(0, 1, 0) + V(0, 0, 1) + "</vertices>" +
           "<volume" + vol1 + ">" + T(0, 2, 1, t1extra) + T(0, 1, 3) + "</volume><volume" + vol2 + ">" + T(1, 2, 3) + T(2, 0, 3) + "</volume></mesh></object>";
}
static std::string Amf(const std::string& body, const char* attrs = "") { return std::string("<?xml version=\"1.0\" encoding=\"utf-8\"?><amf ") + attrs + ">" + body + "</amf>"; }
static bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) < e; }
static void Bounds(const ImportedModel& m, Vec3& lo, Vec3& hi) {
    lo = Vec3{1e30f, 1e30f, 1e30f}; hi = Vec3{-1e30f, -1e30f, -1e30f};
    for (const ImportedPart& p : m.parts) for (const Vec3& v : p.mesh.vertices) {
        lo = Vec3{std::fmin(lo.x, v.x), std::fmin(lo.y, v.y), std::fmin(lo.z, v.z)};
        hi = Vec3{std::fmax(hi.x, v.x), std::fmax(hi.y, v.y), std::fmax(hi.z, v.z)};
    }
}
static double Area(const ImportedModel& m) {
    double a = 0;
    for (const ImportedPart& p : m.parts) for (std::size_t i = 0; i + 2 < p.mesh.triangles.size(); i += 3) {
        const Vec3 x = p.mesh.vertices[(std::size_t)p.mesh.triangles[i]], y = p.mesh.vertices[(std::size_t)p.mesh.triangles[i + 1]], z = p.mesh.vertices[(std::size_t)p.mesh.triangles[i + 2]];
        a += 0.5 * Vec3::Cross(y - x, z - x).Magnitude();
    }
    return a;
}
static bool HasNote(const ImportedModel& m, const char* part) { for (const std::string& n : m.notes) if (n.find(part) != std::string::npos) return true; return false; }

int main() {
    RUN_SUITE("amf: a tetrahedron");
    const double kTetraArea = 1.5 + std::sqrt(3.0) / 2;
    {
        ImportedModel m; std::string err;
        CHECK(ReadAmfBuffer(Amf(Tetra(1)), m, &err, true));
        CHECK(m.backend == "own" && m.parts.size() == 1 && m.parts[0].mesh.triangles.size() == 12 && m.parts[0].mesh.vertices.size() == 12 && fx::Sane(m));
        CHECK_NEAR(Area(m), kTetraArea, 1e-4);
        Vec3 lo, hi; Bounds(m, lo, hi);   // flattened: (x, y, z) -> (x, z, -y)
        CHECK(Near(hi.x, 1) && Near(hi.y, 1) && Near(lo.z, -1) && Near(hi.z, 0));
        CHECK(m.notes.empty());
        CHECK(ReadAmfBuffer(Amf(Tetra(1)), m, &err, false) && Near(m.nodes[0].rotation.w, 0.70710678f) && m.nodes.size() == 2);
    }

    RUN_SUITE("amf: materials and colours");
    {
        ImportedModel m; std::string err;
        const std::string mats = "<material id=\"7\"><metadata type=\"name\">Steel</metadata>" + Col(0.5, 0.25, 0, 1) + "</material><material id=\"8\">" + Col(0, 1, 0, 0.5) + "</material>";
        CHECK(ReadAmfBuffer(Amf(Tetra(1, "", " materialid=\"7\"", " materialid=\"8\"") + mats), m, &err, true));
        CHECK(m.parts.size() == 2 && m.materials.size() == 2);
        CHECK(m.materials[0].name == "Steel" && Near(m.materials[0].diffuse.r, 0.5f) && Near(m.materials[0].diffuse.g, 0.25f) && Near(m.materials[0].diffuse.a, 1));
        CHECK(Near(m.materials[1].diffuse.g, 1) && Near(m.materials[1].diffuse.a, 0.5f));
        // a triangle colour beats the volume's material; the object colour is the last resort
        CHECK(ReadAmfBuffer(Amf(Tetra(1, Col(1, 0, 0), " materialid=\"7\"", "", Col(0, 0, 1)) + mats), m, &err, true));
        CHECK(m.parts.size() == 3 && m.materials.size() == 3);   // blue triangle, Steel (1 triangle), red object colour (2 triangles)
        // colours given as formulas are not read, with a note
        CHECK(ReadAmfBuffer(Amf(Tetra(1, "<color><r>sin(x)</r><g>0</g><b>0</b></color>")), m, &err) && m.materials.empty() && HasNote(m, "formula"));
        // textures and vertex normals are reported
        CHECK(ReadAmfBuffer(Amf(Tetra(1) + "<texture id=\"1\" width=\"1\" height=\"1\" depth=\"1\" type=\"grayscale\">AA==</texture>"), m, &err) && HasNote(m, "texture"));
    }

    RUN_SUITE("amf: constellations, units, zip");
    {
        ImportedModel m; std::string err;
        // a constellation turns object 1 by 90 degrees about z and moves it by (10, 0, 0): (x,y,z) -> (-y + 10, x, z)
        const std::string cs = "<constellation id=\"1\"><instance objectid=\"1\"><deltax>10</deltax><deltay>0</deltay><deltaz>0</deltaz><rx>0</rx><ry>0</ry><rz>90</rz></instance></constellation>";
        CHECK(ReadAmfBuffer(Amf(Tetra(1) + Tetra(2) + cs), m, &err, false));
        Vec3 lo, hi; Bounds(m, lo, hi);
        CHECK(Near(lo.x, 9) && Near(hi.x, 10) && Near(lo.y, 0) && Near(hi.y, 1) && Near(lo.z, 0) && Near(hi.z, 1));   // not flattened: file space
        CHECK(HasNote(m, "not in the constellation") && m.nodes.size() == 2);
        // rotation order x then y then z: rx = 90 sends (0,1,0) to (0,0,1); then rz = 90 sends (1,0,0) to (0,1,0)
        const std::string cs2 = "<constellation id=\"1\"><instance objectid=\"1\"><rx>90</rx><rz>90</rz></instance><instance objectid=\"1\"/></constellation>";
        CHECK(ReadAmfBuffer(Amf(Tetra(1) + cs2), m, &err, false) && m.parts.size() == 2);
        // two instances of one object = two nodes sharing the name
        CHECK(m.nodes.size() == 3);
        // units are kept, a note says so
        CHECK(ReadAmfBuffer(Amf(Tetra(1), "unit=\"inch\""), m, &err) && HasNote(m, "inch"));
        // zip compressed
        fx::ZipEntry e; e.name = "model.amf"; e.data = Amf(Tetra(1)); e.deflate = true;
        CHECK(ReadAmfBuffer(fx::MakeZip({e}), m, &err) && m.parts.size() == 1);
        fx::ZipEntry s; s.name = "readme.txt"; s.data = "hi";
        CHECK(ReadAmfBuffer(fx::MakeZip({s, e}), m, &err) && m.parts.size() == 1);
        // bad index
        std::string bad = Amf(Tetra(1)); bad.replace(bad.find("<v1>0</v1>"), 10, "<v1>99</v1>");
        CHECK(ReadAmfBuffer(bad, m, &err) && HasNote(m, "out-of-range") && m.parts[0].mesh.triangles.size() == 9);
    }

    RUN_SUITE("amf: refused files");
    {
        ImportedModel m; std::string err;
        CHECK(!ReadAmfBuffer("", m, &err) && !err.empty());
        CHECK(!ReadAmfBuffer("<other/>", m, &err) && err.find("<amf>") != std::string::npos);
        CHECK(!ReadAmfBuffer(Amf(""), m, &err) && err.find("no objects") != std::string::npos);
        CHECK(!ReadAmfBuffer(Amf("<object id=\"1\"><mesh><vertices/></mesh></object>"), m, &err) && err.find("no triangles") != std::string::npos);
        std::string f = Amf(Tetra(1)); f.replace(f.find("<x>0</x>"), 8, "<x>1+2</x>");
        CHECK(!ReadAmfBuffer(f, m, &err) && err.find("coordinate") != std::string::npos);
        f = Amf(Tetra(1)); f.replace(f.find("<x>1</x>"), 8, "<x>1e999</x>");
        CHECK(!ReadAmfBuffer(f, m, &err));
        CHECK(!ReadAmfBuffer(Amf(Tetra(1)).substr(0, 200), m, &err));
        CHECK(!ReadAmfBuffer("PK not really a zip", m, &err) && err.find("zip") != std::string::npos);
        CHECK(!ReadAmfBuffer("<!DOCTYPE amf [<!ENTITY x \"y\">]><amf/>", m, &err) && err.find("entity") != std::string::npos);
    }

#ifndef OKAY_STANDALONE_TEST
    RUN_SUITE("amf: the import seam");
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / fs::u8path("okay_amf_m\xC3\xBCller");
        fs::create_directories(dir);
        const fs::path file = dir / fs::u8path("t\xC3\xA9tra.amf");
        { std::ofstream f(file, std::ios::binary); const std::string x = Amf(Tetra(1)); f.write(x.data(), (std::streamsize)x.size()); }
        const std::string path = file.u8string();
        ImportedModel m; std::string err;
        CHECK(HasOwnReader(".amf"));
        CHECK(ImportModelData(path, m, ImportBackend::Own, false, &err) && m.backend == "own" && m.parts.size() == 1);
        SetImportBackend(".amf", ImportBackend::Default);
        unsetenv("OKAY_OWN_AMF");
        if (EffectiveImportBackend(".amf") != ImportBackend::Own) {
            CHECK(!ImportModelData(path, m, ImportBackend::Default, false, &err) && err.find("OKAY_OWN_AMF") != std::string::npos);
        }
        setenv("OKAY_OWN_AMF", "1", 1);
        CHECK(EffectiveImportBackend(".amf") == ImportBackend::Own && ImportableExtensions().find(".amf") != std::string::npos);
        bool ok = false; std::string tex;
        Mesh merged = ImportModel(path, &ok, &tex);
        CHECK(ok && merged.TriangleCount() == 4);
        setenv("OKAY_OWN_AMF", "0", 1);
        CHECK(EffectiveImportBackend(".amf") != ImportBackend::Own);
        unsetenv("OKAY_OWN_AMF");
        std::error_code ec; fs::remove_all(dir, ec);
    }
#endif

    TEST_MAIN_RESULT();
}
