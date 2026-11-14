// Our own 3MF reader (engine/src/Render/ThreeMfReader.cpp). SYNTHETIC fixtures: zip packages built in memory by
// model_fixture_util.hpp, checked against hand-computed numbers; plus the import seam (opt-in switch, UTF-8 path).
// Real 3MF files are checked by model_corpus (tests/data/model_corpus_manifest.tsv).
// Builds on its own with -DOKAY_STANDALONE_TEST (reader sources + ImageDecode only), no engine library.
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "okay/Render/ThreeMfReader.hpp"
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

static const char* kCubeVerts =
    "<vertices><vertex x=\"0\" y=\"0\" z=\"0\"/><vertex x=\"10\" y=\"0\" z=\"0\"/><vertex x=\"10\" y=\"7\" z=\"0\"/><vertex x=\"0\" y=\"7\" z=\"0\"/>"
    "<vertex x=\"0\" y=\"0\" z=\"5\"/><vertex x=\"10\" y=\"0\" z=\"5\"/><vertex x=\"10\" y=\"7\" z=\"5\"/><vertex x=\"0\" y=\"7\" z=\"5\"/></vertices>";
static const int kCubeTris[12][3] = {{3,2,1},{3,1,0},{4,5,6},{4,6,7},{0,1,5},{0,5,4},{1,2,6},{1,6,5},{2,3,7},{2,7,6},{3,0,4},{3,4,7}};

static std::string Tris(const char* extra0 = "", const char* extra1 = "") {
    std::string s = "<triangles>";
    for (int i = 0; i < 12; ++i)
        s += "<triangle v1=\"" + std::to_string(kCubeTris[i][0]) + "\" v2=\"" + std::to_string(kCubeTris[i][1]) + "\" v3=\"" + std::to_string(kCubeTris[i][2]) + "\" " + (i < 6 ? extra0 : extra1) + "/>";
    return s + "</triangles>";
}
static std::string Model(const std::string& resources, const std::string& build, const std::string& attrs = "") {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?><model unit=\"millimeter\" xml:lang=\"en-US\" xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\" " + attrs +
           "><resources>" + resources + "</resources><build>" + build + "</build></model>";
}
static std::string CubeObject(int id, const char* props = "", const std::string& tris = Tris(), const char* type = "model") {
    return std::string("<object id=\"") + std::to_string(id) + "\" type=\"" + type + "\" " + props + "><mesh>" + kCubeVerts + tris + "</mesh></object>";
}
static std::string Package(const std::string& model, bool deflate = false) {
    fx::ZipEntry a; a.name = "[Content_Types].xml"; a.data = "<Types/>";
    fx::ZipEntry r; r.name = "_rels/.rels"; r.data = "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Target=\"/3D/3dmodel.model\" Id=\"rel0\" Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/></Relationships>";
    fx::ZipEntry m; m.name = "3D/3dmodel.model"; m.data = model; m.deflate = deflate;
    return fx::MakeZip({a, r, m});
}
static bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) < e; }
static double Volume(const ImportedPart& p) {   // signed volume; positive for outward counter-clockwise winding
    double v = 0;
    for (std::size_t i = 0; i + 2 < p.mesh.triangles.size(); i += 3) {
        const Vec3 a = p.mesh.vertices[(std::size_t)p.mesh.triangles[i]], b = p.mesh.vertices[(std::size_t)p.mesh.triangles[i + 1]], c = p.mesh.vertices[(std::size_t)p.mesh.triangles[i + 2]];
        v += ((double)a.x * ((double)b.y * c.z - (double)b.z * c.y) - (double)a.y * ((double)b.x * c.z - (double)b.z * c.x) + (double)a.z * ((double)b.x * c.y - (double)b.y * c.x)) / 6.0;
    }
    return v;
}
static void Bounds(const ImportedModel& m, Vec3& lo, Vec3& hi) {
    lo = Vec3{1e30f, 1e30f, 1e30f}; hi = Vec3{-1e30f, -1e30f, -1e30f};
    for (const ImportedPart& p : m.parts) for (const Vec3& v : p.mesh.vertices) {
        lo = Vec3{std::fmin(lo.x, v.x), std::fmin(lo.y, v.y), std::fmin(lo.z, v.z)};
        hi = Vec3{std::fmax(hi.x, v.x), std::fmax(hi.y, v.y), std::fmax(hi.z, v.z)};
    }
}
static bool HasNote(const ImportedModel& m, const char* part) {
    for (const std::string& n : m.notes) if (n.find(part) != std::string::npos) return true;
    return false;
}

int main() {
    RUN_SUITE("3mf: a cube");
    {
        ImportedModel m; std::string err;
        const std::string pkg = Package(Model(CubeObject(1), "<item objectid=\"1\"/>"));
        CHECK(Read3mfBuffer(pkg, m, &err, true));
        CHECK(m.backend == "own" && m.parts.size() == 1 && m.materials.empty());
        CHECK(m.parts[0].mesh.triangles.size() == 36 && m.parts[0].mesh.vertices.size() == 24);   // 90 degree edges stay sharp
        CHECK(m.parts[0].mesh.normals.size() == 24 && fx::Sane(m));
        Vec3 lo, hi; Bounds(m, lo, hi);   // flattened: Z-up baked to Y-up (x, z, -y)
        CHECK(Near(lo.x, 0) && Near(hi.x, 10) && Near(lo.y, 0) && Near(hi.y, 5) && Near(lo.z, -7) && Near(hi.z, 0));
        CHECK_NEAR(Volume(m.parts[0]), 350.0, 1e-3);
        CHECK(m.nodes.size() == 2 && m.nodes[0].rotation.w > 0.99f);
        CHECK(m.notes.empty());
        // not flattened: the root carries the rotation, the vertices stay in file space
        CHECK(Read3mfBuffer(pkg, m, &err, false));
        CHECK(Near(m.nodes[0].rotation.x, -0.70710678f) && Near(m.nodes[0].rotation.w, 0.70710678f));
        Bounds(m, lo, hi);
        CHECK(Near(hi.y, 7) && Near(hi.z, 5));
        // a flat normal points out of the face: the bottom face (z = 0) has normal -z
        bool found = false;
        for (std::size_t i = 0; i < m.parts[0].mesh.vertices.size(); ++i) if (Near(m.parts[0].mesh.normals[i].z, -1)) found = true;
        CHECK(found);
        // the same package, deflated
        CHECK(Read3mfBuffer(Package(Model(CubeObject(1), "<item objectid=\"1\"/>"), true), m, &err) && m.parts.size() == 1 && m.parts[0].mesh.triangles.size() == 36);
    }

    RUN_SUITE("3mf: materials and colours");
    {
        ImportedModel m; std::string err;
        const std::string res = "<basematerials id=\"5\"><base name=\"Red\" displaycolor=\"#FF0000\"/><base name=\"Glass\" displaycolor=\"#00FF0080\"/></basematerials>";
        const std::string pkg = Package(Model(res + CubeObject(1, "pid=\"5\" pindex=\"0\"", Tris("p1=\"0\"", "p1=\"1\"")), "<item objectid=\"1\"/>"));
        CHECK(Read3mfBuffer(pkg, m, &err, true));
        CHECK(m.parts.size() == 2 && m.materials.size() == 2);
        CHECK(m.materials[0].name == "Red" && m.materials[0].hasDiffuse && Near(m.materials[0].diffuse.r, 1) && Near(m.materials[0].diffuse.g, 0) && Near(m.materials[0].diffuse.a, 1));
        CHECK(m.materials[1].name == "Glass" && Near(m.materials[1].diffuse.g, 1) && Near(m.materials[1].diffuse.a, 128 / 255.0f));
        CHECK(m.parts[0].material == 0 && m.parts[1].material == 1);
        CHECK(m.parts[0].mesh.triangles.size() == 18 && m.parts[1].mesh.triangles.size() == 18);
        // colour group, object level colour
        const std::string res2 = "<colorgroup id=\"9\"><color color=\"#0000FF\"/></colorgroup>";
        CHECK(Read3mfBuffer(Package(Model(res2 + CubeObject(1, "pid=\"9\" pindex=\"0\""), "<item objectid=\"1\"/>")), m, &err));
        CHECK(m.parts.size() == 1 && m.materials.size() == 1 && Near(m.materials[0].diffuse.b, 1));
        // texture groups are not read: a note, no material
        const std::string res3 = "<texture2d id=\"3\" path=\"/3D/Texture/a.png\" contenttype=\"image/png\"/><texture2dgroup id=\"4\" texid=\"3\"><tex2coord u=\"0\" v=\"0\"/></texture2dgroup>";
        CHECK(Read3mfBuffer(Package(Model(res3 + CubeObject(1, "pid=\"4\" pindex=\"0\""), "<item objectid=\"1\"/>")), m, &err));
        CHECK(m.materials.empty() && HasNote(m, "texture"));
    }

    RUN_SUITE("3mf: components, build items, transforms");
    {
        ImportedModel m; std::string err;
        // object 2 = the cube twice: once as is, once shifted by (20, 0, 0); the build item then scales x2 and lifts z by 100
        const std::string comp = "<object id=\"2\"><components><component objectid=\"1\"/><component objectid=\"1\" transform=\"1 0 0 0 1 0 0 0 1 20 0 0\"/></components></object>";
        const std::string pkg = Package(Model(CubeObject(1) + comp, "<item objectid=\"2\" transform=\"2 0 0 0 2 0 0 0 2 0 0 100\" partnumber=\"P-7\"/>"));
        CHECK(Read3mfBuffer(pkg, m, &err, false));
        CHECK(m.parts.size() == 1 && m.parts[0].mesh.triangles.size() == 72 && m.nodes.size() == 2 && m.nodes[1].name == "P-7");
        Vec3 lo, hi; Bounds(m, lo, hi);
        CHECK(Near(lo.x, 0) && Near(hi.x, 60) && Near(hi.y, 14) && Near(lo.z, 100) && Near(hi.z, 110));
        CHECK_NEAR(Volume(m.parts[0]), 350.0 * 8 * 2, 0.1);
        // row-vector convention: a 90 degree turn about z, "0 1 0 -1 0 0 0 0 1": (x,y,z) -> (-y, x, z)
        CHECK(Read3mfBuffer(Package(Model(CubeObject(1), "<item objectid=\"1\" transform=\"0 1 0 -1 0 0 0 0 1 0 0 0\"/>")), m, &err));
        Bounds(m, lo, hi);
        CHECK(Near(lo.x, -7) && Near(hi.x, 0) && Near(lo.y, 0) && Near(hi.y, 10));
        CHECK_NEAR(Volume(m.parts[0]), 350.0, 1e-3);
        // a mirror (negative determinant) flips the winding back: the volume stays positive
        CHECK(Read3mfBuffer(Package(Model(CubeObject(1), "<item objectid=\"1\" transform=\"-1 0 0 0 1 0 0 0 1 0 0 0\"/>")), m, &err));
        CHECK_NEAR(Volume(m.parts[0]), 350.0, 1e-3);
        // a component cycle fails with a message, it does not loop
        const std::string cyc = "<object id=\"1\"><components><component objectid=\"2\"/></components></object><object id=\"2\"><components><component objectid=\"1\"/></components></object>";
        CHECK(!Read3mfBuffer(Package(Model(cyc, "<item objectid=\"1\"/>")), m, &err) && !err.empty() && m.parts.empty());
        // a reference to a missing object is skipped and reported
        CHECK(Read3mfBuffer(Package(Model(CubeObject(1), "<item objectid=\"1\"/><item objectid=\"77\"/>")), m, &err) && HasNote(m, "missing object"));
        // support objects are not output
        CHECK(Read3mfBuffer(Package(Model(CubeObject(1) + CubeObject(2, "", Tris(), "support"), "<item objectid=\"1\"/><item objectid=\"2\"/>")), m, &err));
        CHECK(m.parts.size() == 1 && HasNote(m, "support"));
        // a unit other than millimetre is reported, not applied
        std::string inch = Model(CubeObject(1), "<item objectid=\"1\"/>");
        inch.replace(inch.find("millimeter"), 10, "inch");
        CHECK(Read3mfBuffer(Package(inch), m, &err) && HasNote(m, "inch"));
        // no build section: every top level object is used, with a note
        CHECK(Read3mfBuffer(Package(Model(CubeObject(1), "")), m, &err) && m.parts.size() == 1 && HasNote(m, "no build items"));
        // triangles with an out-of-range index are skipped and counted
        std::string bad = Tris(); bad.insert(bad.find("</triangles>"), "<triangle v1=\"0\" v2=\"1\" v3=\"99\"/>");
        CHECK(Read3mfBuffer(Package(Model(CubeObject(1, "", bad), "<item objectid=\"1\"/>")), m, &err) && HasNote(m, "out-of-range") && m.parts[0].mesh.triangles.size() == 36);
        // numbers are read with a point whatever the locale
        const char* loc[] = {"de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "de_DE"};
        for (const char* l : loc) if (std::setlocale(LC_ALL, l)) {
            std::string fr = Model(CubeObject(1), "<item objectid=\"1\"/>");
            fr.replace(fr.find("x=\"10\""), 6, "x=\"10.5\"");
            CHECK(Read3mfBuffer(Package(fr), m, &err));
            Bounds(m, lo, hi);
            CHECK(Near(hi.x, 10.5f));
            std::setlocale(LC_ALL, "C");
            break;
        }
    }

    RUN_SUITE("3mf: refused files");
    {
        ImportedModel m; std::string err;
        CHECK(!Read3mfBuffer("not a zip at all", m, &err) && err.find("zip") != std::string::npos);
        CHECK(!Read3mfBuffer("", m, &err) && !err.empty());
        fx::ZipEntry e; e.name = "other.txt"; e.data = "x";
        CHECK(!Read3mfBuffer(fx::MakeZip({e}), m, &err) && err.find("3D model part") != std::string::npos);
        CHECK(!Read3mfBuffer(Package("<model><resources/><build/></model>"), m, &err) && err.find("no objects") != std::string::npos);
        CHECK(!Read3mfBuffer(Package(Model("<object id=\"1\"><mesh><vertices/><triangles/></mesh></object>", "<item objectid=\"1\"/>")), m, &err) && err.find("no triangles") != std::string::npos);
        CHECK(!Read3mfBuffer(Package(Model(CubeObject(1), "<item objectid=\"1\" transform=\"1e30 0 0 0 1 0 0 0 1 0 0 0\"/>")), m, &err) && err.find("transform") != std::string::npos);
        CHECK(!Read3mfBuffer(Package(Model(CubeObject(1), "<item objectid=\"1\" transform=\"9e14 0 0 0 9e14 0 0 0 9e14 0 0 0\"/>")), m, &err) && err.find("overflow") != std::string::npos);
        CHECK(!Read3mfBuffer(Package(Model(CubeObject(1), "<item objectid=\"1\" transform=\"1 2 3\"/>")), m, &err));
        CHECK(!Read3mfBuffer(Package("<model><resources><object id=\"1\"><mesh><vertices><vertex x=\"nan\" y=\"0\" z=\"0\"/></vertices></mesh></object></resources></model>"), m, &err));
        // DTD entities are never expanded
        CHECK(!Read3mfBuffer(Package("<!DOCTYPE m [<!ENTITY a \"aaaa\"><!ENTITY b \"&a;&a;&a;\">]><model/>"), m, &err) && err.find("entity") != std::string::npos);
        // encrypted entry
        std::string pkg = Package(Model(CubeObject(1), "<item objectid=\"1\"/>"));
        for (std::size_t i = 0; i + 8 < pkg.size(); ++i) if (pkg.compare(i, 4, "PK\x01\x02") == 0) pkg[i + 8] |= 1;
        CHECK(!Read3mfBuffer(pkg, m, &err) && err.find("encrypted") != std::string::npos);
    }

#ifndef OKAY_STANDALONE_TEST
    RUN_SUITE("3mf: the import seam");
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / fs::u8path("okay_3mf_m\xC3\xBCller_\xE7\xAB\x8B\xE6\x96\xB9");
        fs::create_directories(dir);
        const fs::path file = dir / fs::u8path("w\xC3\xBCrfel.3mf");
        { std::ofstream f(file, std::ios::binary); const std::string pkg = Package(Model(CubeObject(1), "<item objectid=\"1\"/>")); f.write(pkg.data(), (std::streamsize)pkg.size()); }
        const std::string path = file.u8string();
        ImportedModel m; std::string err;
        CHECK(HasOwnReader(".3mf") && HasOwnReader(".3MF"));
        CHECK(ImportModelData(path, m, ImportBackend::Own, false, &err) && m.backend == "own" && m.parts.size() == 1);
        CHECK(Read3mfFile(path, m, &err) && m.parts.size() == 1);
        CHECK(!Read3mfFile((dir / "missing.3mf").u8string(), m, &err) && !err.empty());
        // off by default: with no Assimp the file does not import, and the message says how to switch the reader on
        SetImportBackend(".3mf", ImportBackend::Default);
        unsetenv("OKAY_OWN_3MF");
        if (EffectiveImportBackend(".3mf") != ImportBackend::Own) {
            CHECK(!ImportModelData(path, m, ImportBackend::Default, false, &err) && err.find("OKAY_OWN_3MF") != std::string::npos);
            CHECK(ImportableExtensions().find(".3mf") == std::string::npos);
        }
        setenv("OKAY_OWN_3MF", "1", 1);
        CHECK(EffectiveImportBackend(".3mf") == ImportBackend::Own);
        CHECK(ImportableExtensions().find(".3mf") != std::string::npos);
        CHECK(ImportModelData(path, m, ImportBackend::Default, true, &err) && m.parts.size() == 1);
        bool ok = false; std::string tex;
        Mesh merged = ImportModel(path, &ok, &tex);
        CHECK(ok && merged.TriangleCount() == 12);
        setenv("OKAY_OWN_3MF", "0", 1);
        CHECK(EffectiveImportBackend(".3mf") != ImportBackend::Own);
        CHECK(SetImportBackend(".3mf", ImportBackend::Own) && EffectiveImportBackend(".3mf") == ImportBackend::Own);
        SetImportBackend(".3mf", ImportBackend::Default);
        unsetenv("OKAY_OWN_3MF");
        std::error_code ec; fs::remove_all(dir, ec);
    }
#endif

    TEST_MAIN_RESULT();
}
