// Our own X3D reader (engine/src/Render/X3dReader.cpp). SYNTHETIC fixtures (XML text built in the test), checked against
// hand-computed numbers; plus the import seam. Real X3D files are checked by model_corpus.
// Builds on its own with -DOKAY_STANDALONE_TEST (reader source only), no engine library.
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "okay/Render/X3dReader.hpp"
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

static std::string X3d(const std::string& scene) {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE X3D PUBLIC \"ISO//Web3D//DTD X3D 3.3//EN\" \"http://www.web3d.org/specifications/x3d-3.3.dtd\">\n"
           "<X3D profile=\"Immersive\" version=\"3.3\"><head><meta name=\"title\" content=\"t\"/></head><Scene>" + scene + "</Scene></X3D>";
}
static const char* kQuad = "<Coordinate point=\"0 0 0, 1 0 0, 1 1 0, 0 1 0\"/>";
static std::string Quad(const char* attrs = "", const char* extra = "") {
    return std::string("<Shape><Appearance><Material diffuseColor=\"1 0 0\" transparency=\"0.25\" shininess=\"0.5\"/></Appearance><IndexedFaceSet coordIndex=\"0 1 2 3 -1\" ") + attrs + ">" + kQuad + extra + "</IndexedFaceSet></Shape>";
}
static bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) < e; }
static void Bounds(const ImportedModel& m, Vec3& lo, Vec3& hi) {
    lo = Vec3{1e30f, 1e30f, 1e30f}; hi = Vec3{-1e30f, -1e30f, -1e30f};
    for (const ImportedPart& p : m.parts) for (const Vec3& v : p.mesh.vertices) {
        lo = Vec3{std::fmin(lo.x, v.x), std::fmin(lo.y, v.y), std::fmin(lo.z, v.z)};
        hi = Vec3{std::fmax(hi.x, v.x), std::fmax(hi.y, v.y), std::fmax(hi.z, v.z)};
    }
}
static double Volume(const ImportedModel& m) {   // signed volume, positive for outward counter-clockwise winding
    double v = 0;
    for (const ImportedPart& p : m.parts) for (std::size_t i = 0; i + 2 < p.mesh.triangles.size(); i += 3) {
        const Vec3 a = p.mesh.vertices[(std::size_t)p.mesh.triangles[i]], b = p.mesh.vertices[(std::size_t)p.mesh.triangles[i + 1]], c = p.mesh.vertices[(std::size_t)p.mesh.triangles[i + 2]];
        v += ((double)a.x * ((double)b.y * c.z - (double)b.z * c.y) - (double)a.y * ((double)b.x * c.z - (double)b.z * c.x) + (double)a.z * ((double)b.x * c.y - (double)b.y * c.x)) / 6.0;
    }
    return v;
}
static double Area(const ImportedModel& m) {
    double a = 0;
    for (const ImportedPart& p : m.parts) for (std::size_t i = 0; i + 2 < p.mesh.triangles.size(); i += 3)
        a += 0.5 * Vec3::Cross(p.mesh.vertices[(std::size_t)p.mesh.triangles[i + 1]] - p.mesh.vertices[(std::size_t)p.mesh.triangles[i]], p.mesh.vertices[(std::size_t)p.mesh.triangles[i + 2]] - p.mesh.vertices[(std::size_t)p.mesh.triangles[i]]).Magnitude();
    return a;
}
static std::size_t Tris(const ImportedModel& m) { std::size_t n = 0; for (const ImportedPart& p : m.parts) n += p.mesh.triangles.size() / 3; return n; }
static bool HasNote(const ImportedModel& m, const char* part) { for (const std::string& n : m.notes) if (n.find(part) != std::string::npos) return true; return false; }
static bool Read(const std::string& s, ImportedModel& m, std::string& e) { return ReadX3dBuffer(s, "", m, &e); }

int main() {
    ImportedModel m; std::string err;
    RUN_SUITE("x3d: IndexedFaceSet and materials");
    {
        CHECK(Read(X3d(Quad()), m, err));
        CHECK(m.backend == "own" && m.parts.size() == 1 && m.nodes.size() == 2 && m.materials.size() == 1 && fx::Sane(m));
        CHECK(m.parts[0].mesh.triangles.size() == 6 && m.parts[0].mesh.vertices.size() == 4 && m.parts[0].mesh.uvs.empty());
        CHECK_NEAR(Area(m), 1.0, 1e-5);
        for (const Vec3& n : m.parts[0].mesh.normals) CHECK(Near(n.z, 1) && Near(n.x, 0));   // counter-clockwise: faces +z
        CHECK(Near(m.materials[0].diffuse.r, 1) && Near(m.materials[0].diffuse.g, 0) && Near(m.materials[0].diffuse.a, 0.75f) && Near(m.materials[0].shininess, 64));
        CHECK(m.notes.empty());   // head, meta and Viewpoint-less scene: nothing to report
        CHECK(Read(X3d(Quad("ccw=\"false\"")), m, err));
        for (const Vec3& n : m.parts[0].mesh.normals) CHECK(Near(n.z, -1));
        // a polygon of five corners with a concave notch: two corners of the L shape turn the right way
        CHECK(Read(X3d("<Shape><IndexedFaceSet coordIndex=\"0 1 2 3 4 5 -1\"><Coordinate point=\"0 0 0 2 0 0 2 1 0 1 1 0 1 2 0 0 2 0\"/></IndexedFaceSet></Shape>"), m, err));
        CHECK_NEAR(Area(m), 3.0, 1e-5);
        // a face with an index out of range is dropped and reported; a missing trailing -1 still closes the face
        CHECK(Read(X3d("<Shape><IndexedFaceSet coordIndex=\"0 1 9 -1 0 1 2\"><Coordinate point=\"0 0 0 1 0 0 0 1 0\"/></IndexedFaceSet></Shape>"), m, err) && HasNote(m, "out-of-range") && m.parts[0].mesh.triangles.size() == 3);
        // no Appearance: no material
        CHECK(Read(X3d("<Shape><IndexedFaceSet coordIndex=\"0 1 2 -1\"><Coordinate point=\"0 0 0 1 0 0 0 1 0\"/></IndexedFaceSet></Shape>"), m, err) && m.materials.empty() && m.parts[0].material == -1);
    }

    RUN_SUITE("x3d: normals, texture coordinates, crease angle");
    {
        // explicit per-vertex normals and texture coordinates
        CHECK(Read(X3d(Quad("", "<Normal vector=\"0 0 1 0 0 1 0 0 1 0 0 1\"/><TextureCoordinate point=\"0 0 1 0 1 1 0 1\"/>")), m, err));
        CHECK(m.parts[0].mesh.uvs.size() == 4);
        bool found = false;
        for (std::size_t i = 0; i < 4; ++i) if (Near(m.parts[0].mesh.vertices[i].y, 1) && Near(m.parts[0].mesh.vertices[i].x, 0)) { found = true; CHECK(Near(m.parts[0].mesh.uvs[i].x, 0) && Near(m.parts[0].mesh.uvs[i].y, 0)); }   // v is flipped
        CHECK(found);
        // per-face normals (normalPerVertex false)
        CHECK(Read(X3d(Quad("normalPerVertex=\"false\"", "<Normal vector=\"0 1 0\"/>")), m, err));
        for (const Vec3& n : m.parts[0].mesh.normals) CHECK(Near(n.y, 1));
        // normal indices that do not fit: generated normals, a note
        CHECK(Read(X3d(Quad("normalIndex=\"0 1 2 7 -1\"", "<Normal vector=\"0 0 1 0 0 1 0 0 1\"/>")), m, err) && HasNote(m, "normal indices"));
        // crease angle: two triangles folded 90 degrees along an edge: flat at creaseAngle 0 (6 vertices), shared when it is large (4)
        const std::string fold = "<Shape><IndexedFaceSet coordIndex=\"0 1 2 -1 1 0 3 -1\" creaseAngle=\"%s\"><Coordinate point=\"0 0 0 1 0 0 0 1 0 0 0 1\"/></IndexedFaceSet></Shape>";
        char buf[400];
        std::snprintf(buf, sizeof buf, fold.c_str(), "0"); CHECK(Read(X3d(buf), m, err) && m.parts[0].mesh.vertices.size() == 6);
        std::snprintf(buf, sizeof buf, fold.c_str(), "1.7"); CHECK(Read(X3d(buf), m, err) && m.parts[0].mesh.vertices.size() == 4);
        std::snprintf(buf, sizeof buf, fold.c_str(), "1.0"); CHECK(Read(X3d(buf), m, err) && m.parts[0].mesh.vertices.size() == 6);   // 57 degrees: the 90 degree fold stays sharp
        // IndexedTriangleSet and TriangleSet
        CHECK(Read(X3d("<Shape><IndexedTriangleSet index=\"0 1 2 0 2 3\"><Coordinate point=\"0 0 0 1 0 0 1 1 0 0 1 0\"/></IndexedTriangleSet></Shape>"), m, err) && m.parts[0].mesh.triangles.size() == 6);
        CHECK(Read(X3d("<Shape><TriangleSet><Coordinate point=\"0 0 0 1 0 0 0 1 0  0 0 1 1 0 1 0 1 1\"/></TriangleSet></Shape>"), m, err) && m.parts[0].mesh.triangles.size() == 6);
    }

    RUN_SUITE("x3d: transforms, DEF/USE, Switch");
    {
        // (1,0,0) scaled by 2, turned 90 degrees about z, moved by (1,2,3) -> (1,4,3)
        CHECK(Read(X3d("<Transform translation=\"1 2 3\" scale=\"2 2 2\" rotation=\"0 0 1 1.5707963267949\">" + Quad() + "</Transform>"), m, err));
        Vec3 lo, hi; Bounds(m, lo, hi);   // the quad (0..1)^2 -> x = -2y + 1 in [-1, 1], y = 2x + 2 in [2, 4], z = 3
        CHECK(Near(lo.x, -1) && Near(hi.x, 1) && Near(lo.y, 2) && Near(hi.y, 4) && Near(lo.z, 3) && Near(hi.z, 3));
        CHECK_NEAR(Area(m), 4.0, 1e-4);
        // a mirror keeps the faces pointing the same way
        CHECK(Read(X3d("<Transform scale=\"-1 1 1\">" + Quad() + "</Transform>"), m, err));
        for (const Vec3& n : m.parts[0].mesh.normals) CHECK(Near(n.z, 1));
        // centre and scale orientation: scale 2 along x about the centre (1,0,0): x in [0,1] -> [-1, 1]
        CHECK(Read(X3d("<Transform center=\"1 0 0\" scale=\"2 1 1\">" + Quad() + "</Transform>"), m, err));
        Bounds(m, lo, hi); CHECK(Near(lo.x, -1) && Near(hi.x, 1));
        // DEF once, USE under another transform
        CHECK(Read(X3d("<Group><Transform DEF=\"A\" translation=\"5 0 0\"><Shape DEF=\"S\"><IndexedFaceSet coordIndex=\"0 1 2 -1\"><Coordinate point=\"0 0 0 1 0 0 0 1 0\"/></IndexedFaceSet></Shape></Transform><Transform translation=\"0 7 0\"><Shape USE=\"S\"/></Transform><Transform USE=\"A\"/></Group>"), m, err));
        CHECK(m.parts.size() == 3);
        Bounds(m, lo, hi); CHECK(Near(lo.x, 0) && Near(hi.x, 6) && Near(hi.y, 8));
        // a USE of the group it sits in is skipped (no endless loop)
        CHECK(Read(X3d("<Group DEF=\"G\">" + Quad() + "<Group USE=\"G\"/></Group>"), m, err) && HasNote(m, "back into their own parent") && m.parts.size() == 1);
        // a USE of an undefined name is reported
        CHECK(Read(X3d(Quad() + "<Shape USE=\"nope\"/>"), m, err) && HasNote(m, "not defined"));
        // Switch: default shows nothing, whichChoice picks one child
        CHECK(!Read(X3d("<Switch>" + Quad() + "</Switch>"), m, err));
        CHECK(Read(X3d("<Switch whichChoice=\"1\">" + Quad() + Quad() + "</Switch>"), m, err) && m.parts.size() == 1);
        CHECK(Read(X3d("<Switch>" + Quad() + "</Switch>" + Quad()), m, err) && HasNote(m, "Switch"));
    }

    RUN_SUITE("x3d: primitives");
    {
        CHECK(Read(X3d("<Shape><Box size=\"2 4 6\"/></Shape>"), m, err));
        Vec3 lo, hi; Bounds(m, lo, hi);
        CHECK(Near(lo.x, -1) && Near(hi.x, 1) && Near(hi.y, 2) && Near(hi.z, 3) && m.parts[0].mesh.triangles.size() == 36 && m.parts[0].mesh.vertices.size() == 24);
        CHECK_NEAR(Volume(m), 48.0, 1e-3);
        CHECK(Read(X3d("<Shape><Box/></Shape>"), m, err)); CHECK_NEAR(Volume(m), 8.0, 1e-3);
        CHECK(Read(X3d("<Shape><Sphere radius=\"2\"/></Shape>"), m, err));
        CHECK(Volume(m) > 0.9 * (4.0 / 3 * 3.14159265 * 8) && Volume(m) < 4.0 / 3 * 3.14159265 * 8 && fx::Sane(m));
        CHECK(Area(m) > 0.9 * 4 * 3.14159265 * 4 && Area(m) < 4 * 3.14159265 * 4);
        CHECK(Read(X3d("<Shape><Cylinder radius=\"1\" height=\"3\"/></Shape>"), m, err));
        CHECK(Volume(m) > 0.95 * 3.14159265 * 3 && Volume(m) < 3.14159265 * 3);
        CHECK(Read(X3d("<Shape><Cylinder radius=\"1\" height=\"3\" top=\"false\" bottom=\"false\"/></Shape>"), m, err));
        CHECK_NEAR(Area(m), 3.0 * 2 * 24 * std::sin(3.14159265 / 24), 1e-3);   // side only
        CHECK(Read(X3d("<Shape><Cone bottomRadius=\"2\" height=\"3\"/></Shape>"), m, err));
        CHECK(Volume(m) > 0.95 * 3.14159265 * 4 && Volume(m) < 3.14159265 * 4 && fx::Sane(m));
        Bounds(m, lo, hi); CHECK(Near(lo.y, -1.5f) && Near(hi.y, 1.5f));
    }

    RUN_SUITE("x3d: Extrusion");
    {
        Vec3 lo, hi;
        CHECK(Read(X3d("<Shape><Extrusion/></Shape>"), m, err));   // the defaults: a 2 x 2 square swept along +Y by 1
        Bounds(m, lo, hi);
        CHECK(Near(lo.x, -1) && Near(hi.x, 1) && Near(lo.y, 0) && Near(hi.y, 1) && Near(lo.z, -1) && Near(hi.z, 1));
        CHECK(Tris(m) == 12 && fx::Sane(m));
        CHECK_NEAR(Volume(m), 4.0, 1e-5);
        CHECK(Read(X3d("<Shape><Extrusion beginCap=\"false\" endCap=\"false\"/></Shape>"), m, err) && Tris(m) == 8);
        // a clockwise cross-section with ccw false is the same solid, faces still outward
        CHECK(Read(X3d("<Shape><Extrusion ccw=\"false\" crossSection=\"1 1 -1 1 -1 -1 1 -1 1 1\"/></Shape>"), m, err));
        CHECK_NEAR(Volume(m), 4.0, 1e-5);
        // a straight spine along x: the cross-section follows it
        CHECK(Read(X3d("<Shape><Extrusion spine=\"0 0 0 5 0 0\"/></Shape>"), m, err));
        Bounds(m, lo, hi);
        CHECK(Near(lo.x, 0) && Near(hi.x, 5) && Near(hi.y, 1) && Near(lo.y, -1) && Near(hi.z, 1) && Near(lo.z, -1));
        CHECK_NEAR(Volume(m), 20.0, 1e-4);
        // a scale per spine point
        CHECK(Read(X3d("<Shape><Extrusion scale=\"1 1 2 2\" beginCap=\"false\" endCap=\"false\"/></Shape>"), m, err));
        Bounds(m, lo, hi); CHECK(Near(hi.x, 2) && Near(lo.x, -2));
        // a bent spine: two segments, four ring points, caps: 2 * 4 * 2 + 4 triangles
        CHECK(Read(X3d("<Shape><Extrusion spine=\"0 0 0 0 1 0 1 1 0\" crossSection=\"1 1 1 -1 -1 -1 -1 1 1 1\" scale=\"0.1 0.1\"/></Shape>"), m, err));
        CHECK(Tris(m) == 20 && fx::Sane(m));
        CHECK(Read(X3d("<Shape><Extrusion spine=\"0 0 0 0 1 0 1 1 0 1 0 0 0 0 0\" scale=\"0.1 0.1\"/></Shape>"), m, err) && fx::Sane(m));   // a closed spine: no caps
        CHECK(!Read(X3d("<Shape><Extrusion spine=\"0 0\"/></Shape>"), m, err));
        CHECK(!Read(X3d("<Shape><Extrusion crossSection=\"1 2 3\"/></Shape>"), m, err));
    }

    RUN_SUITE("x3d: unsupported content is named");
    {
        CHECK(Read(X3d(Quad() + "<Viewpoint position=\"0 0 5\"/><Shape><ElevationGrid/></Shape><Shape><Text string=\"x\"/></Shape><TimeSensor DEF=\"t\"/><ROUTE fromNode=\"t\" fromField=\"a\" toNode=\"t\" toField=\"b\"/>"), m, err));
        CHECK(HasNote(m, "ElevationGrid") && HasNote(m, "Text") && HasNote(m, "Viewpoint") && HasNote(m, "TimeSensor"));
        CHECK(Read(X3d(Quad("", "<Color color=\"1 0 0 0 1 0 0 0 1 1 1 1\"/>")), m, err) && HasNote(m, "colours"));
        CHECK(!Read(X3d("<Shape><IndexedLineSet coordIndex=\"0 1 -1\"><Coordinate point=\"0 0 0 1 0 0\"/></IndexedLineSet></Shape>"), m, err) && err.find("IndexedLineSet") != std::string::npos);
        // texture: the first url, resolved against the base directory
        CHECK(ReadX3dBuffer(X3d("<Shape><Appearance><ImageTexture url='\"my tex.png\" \"b.png\"'/></Appearance><IndexedFaceSet coordIndex=\"0 1 2 -1\"><Coordinate point=\"0 0 0 1 0 0 0 1 0\"/></IndexedFaceSet></Shape>"), "dir/", m, &err));
        CHECK(m.materials.size() == 1 && m.materials[0].texture == "dir/my tex.png");
    }

    RUN_SUITE("x3d: refused files");
    {
        CHECK(!Read("", m, err) && !err.empty());
        CHECK(!Read("<Scene/>", m, err) && err.find("X3D") != std::string::npos);
        CHECK(!Read("#VRML V2.0 utf8\nShape {}", m, err) && !err.empty());
        CHECK(!Read(X3d(""), m, err) && err.find("no triangle geometry") != std::string::npos);
        CHECK(!Read(X3d("<Shape><IndexedFaceSet coordIndex=\"0 1 x -1\"><Coordinate point=\"0 0 0 1 0 0 0 1 0\"/></IndexedFaceSet></Shape>"), m, err) && err.find("coordIndex") != std::string::npos);
        CHECK(!Read(X3d("<Shape><IndexedFaceSet coordIndex=\"0 1 2 -1\"><Coordinate point=\"0 0 0 1 0 0 0 1\"/></IndexedFaceSet></Shape>"), m, err));   // not a multiple of 3
        CHECK(!Read(X3d("<Shape><IndexedFaceSet coordIndex=\"0 1 2 -1\"><Coordinate point=\"0 0 0 1 0 0 1e999 1 0\"/></IndexedFaceSet></Shape>"), m, err));
        CHECK(!Read(X3d("<Transform translation=\"1 2\">" + Quad() + "</Transform>"), m, err) && err.find("translation") != std::string::npos);
        CHECK(!Read("<!DOCTYPE X3D [<!ENTITY a \"b\">]><X3D/>", m, err) && err.find("entity") != std::string::npos);
        const char* loc[] = {"de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "de_DE"};
        for (const char* l : loc) if (std::setlocale(LC_ALL, l)) {
            CHECK(Read(X3d("<Shape><IndexedFaceSet coordIndex=\"0 1 2 -1\"><Coordinate point=\"0 0 0 1.5 0 0 0 1 0\"/></IndexedFaceSet></Shape>"), m, err));
            Vec3 lo, hi; Bounds(m, lo, hi); CHECK(Near(hi.x, 1.5f));
            std::setlocale(LC_ALL, "C");
            break;
        }
    }

#ifndef OKAY_STANDALONE_TEST
    RUN_SUITE("x3d: the import seam");
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / fs::u8path("okay_x3d_m\xC3\xBCller");
        fs::create_directories(dir);
        const fs::path file = dir / fs::u8path("sz\xC3\xA9n.x3d");
        { std::ofstream f(file, std::ios::binary); const std::string x = X3d(Quad()); f.write(x.data(), (std::streamsize)x.size()); }
        const std::string path = file.u8string();
        CHECK(HasOwnReader(".x3d"));
        CHECK(ImportModelData(path, m, ImportBackend::Own, false, &err) && m.backend == "own" && m.parts.size() == 1);
        SetImportBackend(".x3d", ImportBackend::Default);
        unsetenv("OKAY_OWN_X3D");
        if (EffectiveImportBackend(".x3d") != ImportBackend::Own) CHECK(!ImportModelData(path, m, ImportBackend::Default, false, &err) && err.find("OKAY_OWN_X3D") != std::string::npos);
        setenv("OKAY_OWN_X3D", "1", 1);
        CHECK(EffectiveImportBackend(".x3d") == ImportBackend::Own && ImportableExtensions().find(".x3d") != std::string::npos);
        bool ok = false; std::string tex;
        Mesh merged = ImportModel(path, &ok, &tex);
        CHECK(ok && merged.TriangleCount() == 2);
        setenv("OKAY_OWN_X3D", "0", 1);
        CHECK(EffectiveImportBackend(".x3d") != ImportBackend::Own);
        unsetenv("OKAY_OWN_X3D");
        std::error_code ec; fs::remove_all(dir, ec);
    }
#endif

    TEST_MAIN_RESULT();
}
