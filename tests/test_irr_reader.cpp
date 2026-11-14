// Our own Irrlicht scene (.irr) reader (engine/src/Render/IrrSceneReader.cpp). SYNTHETIC fixtures built in memory, checked against
// hand-computed numbers (BuildIrrScene in batchd_fixtures.hpp); the mesh a node names is supplied by a test loader. Real .irr files
// are checked by model_corpus. Builds on its own with -DOKAY_STANDALONE_TEST (reader source only).
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "batchd_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include <cmath>
#include <string>

using namespace okay;

static bool HasNote(const ImportedModel& m, const char* what) { for (const std::string& n : m.notes) if (n.find(what) != std::string::npos) return true; return false; }

// A "file" with one root node, one child node with a translation of (0,0,1) and a triangle (0,0,0) (1,0,0) (0,1,0) on the child, one material.
static int g_loads = 0;
static std::string g_lastPath;
static bool TestLoader(const std::string& path, ImportedModel& m, std::string* err) {
    ++g_loads; g_lastPath = path;
    if (path.find("missing") != std::string::npos) { if (err) *err = "no such file"; return false; }
    ImportedNode r; r.name = "root";
    ImportedNode c; c.name = "child"; c.parent = 0; c.position = Vec3{0, 0, 1};
    m.nodes = {r, c};
    ImportedPart p;
    p.mesh.vertices = {Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{0, 1, 0}};
    p.mesh.triangles = {0, 1, 2};
    p.node = 1; p.material = 0;
    m.parts.push_back(p);
    ImportedMaterial mt; mt.name = "loaded"; mt.texture = "loaded.png";
    m.materials.push_back(mt);
    m.backend = "own";
    return true;
}

int main() {
    RUN_SUITE("irr_reader");
    const IrrMeshLoader loader = TestLoader;
    for (const char* eol : {"\n", "\r\n"}) {
        ImportedModel m; std::string err;
        g_loads = 0;
        CHECK(ReadIrrSceneBuffer(fx::BuildIrrScene(eol), "dir/", m, &err, &loader));
        CHECK(m.backend == "own" && fx::Sane(m));
        CHECK(g_loads == 1 && g_lastPath == "dir/tri.irrmesh");
        // nodes: root, camera, cube, group, ball, m, and the loaded file's two nodes under "m"
        CHECK(m.nodes.size() == 8);
        CHECK(m.nodes[0].name == "IRRScene" && m.nodes[1].name == "cam" && m.nodes[2].name == "cube" && m.nodes[3].name == "group" && m.nodes[4].name == "ball" && m.nodes[5].name == "m" && m.nodes[6].name == "root" && m.nodes[7].name == "child");
        CHECK(m.nodes[4].parent == 3 && m.nodes[5].parent == 3 && m.nodes[6].parent == 5 && m.nodes[7].parent == 6 && m.nodes[1].parent == 0);
        CHECK(fx::Near(m.nodes[2].position.x, 1) && fx::Near(m.nodes[2].position.y, 2) && fx::Near(m.nodes[2].position.z, 3) && fx::Near(m.nodes[3].position.y, 10) && fx::Near(m.nodes[5].position.z, 5));
        const Quat q = m.nodes[2].rotation;   // Rotation 0, 0, 90: Rz(90)
        CHECK(fx::Near(q.x, 0) && fx::Near(q.y, 0) && fx::Near(q.z, 0.70710678f) && fx::Near(q.w, 0.70710678f));
        // parts: cube (12 triangles, area 24), sphere (48), the loaded triangle
        CHECK(m.parts.size() == 3 && fx::TriCount(m) == 12 + 48 + 1);
        CHECK(m.parts[0].mesh.triangles.size() == 36 && m.parts[0].mesh.vertices.size() == 24 && m.parts[1].mesh.triangles.size() == 144 && m.parts[2].node == 7);
        { ImportedModel c; c.nodes.push_back(ImportedNode{}); c.parts.push_back(m.parts[0]); CHECK_NEAR(fx::Area(c), 24.0, 1e-4); }
        for (const Vec3 v : m.parts[0].mesh.vertices) CHECK(fx::Near(std::fabs(v.x), 1) && fx::Near(std::fabs(v.y), 1) && fx::Near(std::fabs(v.z), 1));
        // cube faces wind outward
        for (std::size_t i = 0; i + 2 < m.parts[0].mesh.triangles.size(); i += 3) {
            const Vec3 a = m.parts[0].mesh.vertices[(std::size_t)m.parts[0].mesh.triangles[i]], b = m.parts[0].mesh.vertices[(std::size_t)m.parts[0].mesh.triangles[i + 1]], c = m.parts[0].mesh.vertices[(std::size_t)m.parts[0].mesh.triangles[i + 2]];
            const Vec3 n = Vec3::Cross(b - a, c - a), ctr = Vec3{(a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3, (a.z + b.z + c.z) / 3};
            CHECK(n.x * ctr.x + n.y * ctr.y + n.z * ctr.z > 0);
        }
        // the sphere: radius 1, normals outward, area close to 4 pi
        for (std::size_t i = 0; i < m.parts[1].mesh.vertices.size(); ++i) { CHECK_NEAR(m.parts[1].mesh.vertices[i].Magnitude(), 1.0, 1e-5); CHECK(fx::Near(m.parts[1].mesh.normals[i].Magnitude(), 1)); }
        { ImportedModel c; c.nodes.push_back(ImportedNode{}); c.parts.push_back(m.parts[1]); const double a = fx::Area(c); CHECK(a > 0.6 * 4 * 3.14159265 && a < 4 * 3.14159265); }
        // the cube's material (diffuse green, shininess 5, texture joined to the folder); the loaded material is reused
        const ImportedMaterial& mt = m.materials[(std::size_t)m.parts[0].material];
        CHECK(mt.hasDiffuse && fx::Near(mt.diffuse.g, 1) && fx::Near(mt.diffuse.r, 0) && fx::Near(mt.shininess, 5) && mt.texture == "dir/t/a.png" && !mt.hasEmissive);
        CHECK(m.parts[1].material == -1 && m.materials[(std::size_t)m.parts[2].material].name == "loaded");
        CHECK(HasNote(m, "animator") && HasNote(m, "camera x1") && HasNote(m, "left-handed"));
        // world position of the loaded triangle: group (0,10,0) * m (0,0,5) * child (0,0,1): its vertices are at z = 6
        const Mat4 w = Mat4::TRS(m.nodes[3].position, m.nodes[3].rotation, m.nodes[3].scale) * Mat4::TRS(m.nodes[5].position, m.nodes[5].rotation, m.nodes[5].scale) * Mat4::TRS(m.nodes[6].position, m.nodes[6].rotation, m.nodes[6].scale) * Mat4::TRS(m.nodes[7].position, m.nodes[7].rotation, m.nodes[7].scale);
        const Vec3 p0 = w.MultiplyPoint(m.parts[2].mesh.vertices[0]);
        CHECK(fx::Near(p0.y, 10) && fx::Near(p0.z, 6));
    }
    {   // UTF-16 reads the same; without a loader the mesh node is named in the notes
        ImportedModel a, b, c; std::string err;
        CHECK(ReadIrrSceneBuffer(fx::BuildIrrScene(), "", a, &err, &loader) && ReadIrrSceneBuffer(fx::ToUtf16leD(fx::BuildIrrScene()), "", b, &err, &loader));
        CHECK(a.parts.size() == b.parts.size() && a.nodes.size() == b.nodes.size() && fx::TriCount(a) == fx::TriCount(b));
        CHECK(ReadIrrSceneBuffer(fx::BuildIrrScene(), "", c, &err, nullptr) && c.parts.size() == 2 && HasNote(c, "no mesh loader"));
        // a loader that fails: the scene still reads and says which file
        std::string s = fx::BuildIrrScene();
        s.replace(s.find("tri.irrmesh"), 11, "missing.irrmesh");
        CHECK(ReadIrrSceneBuffer(s, "", c, &err, &loader) && c.parts.size() == 2 && HasNote(c, "could not be read, first: missing.irrmesh"));
    }
    {   // the same mesh file twice loads once and shares its material
        std::string s = "<irr_scene><node type=\"mesh\"><attributes><string name=\"Mesh\" value=\"a.x\"/></attributes></node><node type=\"animatedMesh\"><attributes><string name=\"Mesh\" value=\"a.x\"/><vector3d name=\"Position\" value=\"3,0,0\"/></attributes></node></irr_scene>";
        ImportedModel m; std::string err;
        g_loads = 0;
        CHECK(ReadIrrSceneBuffer(s, "", m, &err, &loader) && g_loads == 1 && m.parts.size() == 2 && m.materials.size() == 1 && m.parts[0].material == m.parts[1].material && m.nodes.size() == 7);
    }
    {   // a mesh file with no node tree of its own (OBJ and STL readers give one static part): the part still lands under the scene node
        IrrMeshLoader flat = [](const std::string&, ImportedModel& m, std::string*) {
            ImportedPart p; p.mesh.vertices = {Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{0, 1, 0}}; p.mesh.triangles = {0, 1, 2}; p.node = 0; p.material = -1;
            m.parts.push_back(p); m.backend = "own"; return true; };
        ImportedModel m; std::string err;
        CHECK(ReadIrrSceneBuffer("<irr_scene><node type=\"mesh\"><attributes><string name=\"Mesh\" value=\"a.obj\"/><vector3d name=\"Position\" value=\"0,4,0\"/></attributes></node></irr_scene>", "", m, &err, &flat));
        CHECK(m.parts.size() == 1 && m.nodes.size() == 3 && m.parts[0].node == 2 && m.nodes[2].parent == 1 && fx::Sane(m));
    }
    {   // a bad transform is replaced by the identity and said
        std::string s = "<irr_scene><node type=\"cube\"><attributes><vector3d name=\"Position\" value=\"1, x, 3\"/></attributes></node></irr_scene>";
        ImportedModel m; std::string err;
        CHECK(ReadIrrSceneBuffer(s, "", m, &err, nullptr) && m.parts.size() == 1 && fx::Near(m.nodes[1].position.x, 0) && HasNote(m, "bad Position"));
    }
    {   // refusals
        ImportedModel m; std::string err;
        const std::string good = fx::BuildIrrScene();
        CHECK(!ReadIrrSceneBuffer("", "", m, &err, &loader) && !err.empty());
        CHECK(!ReadIrrSceneBuffer("<mesh/>", "", m, &err, &loader) && err.find("not an Irrlicht scene") != std::string::npos);
        CHECK(!ReadIrrSceneBuffer("<irr_scene></irr_scene>", "", m, &err, &loader) && err.find("no <node>") != std::string::npos);
        CHECK(!ReadIrrSceneBuffer("<irr_scene><node type=\"light\"/></irr_scene>", "", m, &err, &loader) && err.find("no geometry") != std::string::npos);
        CHECK(!ReadIrrSceneBuffer("<!DOCTYPE s [<!ENTITY a \"x\">]><irr_scene/>", "", m, &err, &loader));
        // a sphere asked for with absurd poly counts is clamped, not allocated
        CHECK(ReadIrrSceneBuffer("<irr_scene><node type=\"sphere\"><attributes><float name=\"Radius\" value=\"1\"/><int name=\"PolyCountX\" value=\"2000000000\"/><int name=\"PolyCountY\" value=\"-5\"/></attributes></node></irr_scene>", "", m, &err, &loader) && fx::Sane(m));
        for (std::size_t n = 0; n < good.size(); ++n) {
            ImportedModel t; std::string e;
            const bool ok = ReadIrrSceneBuffer(good.substr(0, n), "", t, &e, &loader);
            CHECK(ok ? fx::Sane(t) : (!e.empty() && t.parts.empty() && t.nodes.empty()));
        }
    }
    TEST_MAIN_RESULT();
}
