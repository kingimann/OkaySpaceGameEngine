// Modeling fixes from issue #888, findings 134 to 139 (each block is the repro of one finding):
//  134  Recenter / Ground / Fit 1u on a primitive or ready-made prop must survive save and reload.
//  135  every whole-mesh button is exactly one undo step on the active stack (mesh stack in Edit Mesh).
//  136  a typed NaN or infinity must not turn the mesh into NaN.
//  137  Solidify on an open surface closes the sides (a rim along every open border).
//  138  Export OBJ keeps vertex colours, face colours (.mtl), polygons, normals and full float precision.
//  139  one implementation per tool: Symmetrize is Mesh::Symmetrize, the axis "Loop Cut" is Mesh::SliceAxis.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Modeling/MeshExport.hpp"
#include "okay/Modeling/MeshUndo.hpp"
#include "okay/Modeling/ModelOpUndo.hpp"
#include "okay/Modeling/Modifiers.hpp"
#include "okay/Modeling/ShapeOps.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::modeling;

namespace {

bool AllFinite(const Mesh& m) {
    for (const Vec3& v : m.vertices) if (!std::isfinite(v.x + v.y + v.z)) return false;
    for (const Vec2& t : m.uvs) if (!std::isfinite(t.x + t.y)) return false;
    for (const Vec2& t : m.cornerUVs) if (!std::isfinite(t.x + t.y)) return false;
    return true;
}

// Open edges, counted by position (two vertices at one point are one corner).
int OpenEdges(const Mesh& m) {
    std::map<std::tuple<float, float, float>, int> id;
    std::vector<int> c(m.vertices.size());
    for (std::size_t i = 0; i < m.vertices.size(); ++i)
        c[i] = id.emplace(std::make_tuple(m.vertices[i].x, m.vertices[i].y, m.vertices[i].z), (int)id.size()).first->second;
    std::map<std::pair<int, int>, int> e;
    for (std::size_t t = 0; t + 2 < m.triangles.size(); t += 3)
        for (int k = 0; k < 3; ++k) {
            int a = c[m.triangles[t + k]], b = c[m.triangles[t + (k + 1) % 3]];
            if (a != b) ++e[{a, b}];
        }
    int open = 0;
    for (const auto& kv : e) if (!e.count({kv.first.second, kv.first.first})) ++open;
    return open;
}

// Signed volume (positive when the faces point outward).
float SignedVolume(const Mesh& m) {
    double v = 0;
    for (std::size_t t = 0; t + 2 < m.triangles.size(); t += 3) {
        const Vec3 &a = m.vertices[m.triangles[t]], &b = m.vertices[m.triangles[t + 1]], &c = m.vertices[m.triangles[t + 2]];
        v += (double)a.x * (b.y * c.z - b.z * c.y) - (double)a.y * (b.x * c.z - b.z * c.x) + (double)a.z * (b.x * c.y - b.y * c.x);
    }
    return (float)(v / 6.0);
}

// Save a one-object scene and load it back; the reloaded mesh.
Mesh SaveAndReload(const Mesh& m) {
    Scene s;
    auto* mr = s.CreateGameObject("Box")->AddComponent<MeshRenderer>();
    mr->mesh = m;
    const std::string text = SceneSerializer::Serialize(s);
    Scene back;
    std::string err;
    if (!SceneSerializer::Deserialize(back, text, &err)) { std::cout << "  reload failed: " << err << "\n"; return Mesh{}; }
    GameObject* go = back.Find("Box");
    auto* r = go ? go->GetComponent<MeshRenderer>() : nullptr;
    return r ? r->mesh : Mesh{};
}

std::string Tmp(const std::string& name) {
    auto dir = std::filesystem::temp_directory_path() / "okay_test_modeling_fixes_134_139";
    std::filesystem::create_directories(dir);
    return (dir / name).string();
}

std::string ReadAll(const std::string& p) { std::ifstream f(p); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }

} // namespace

int main() {
    // ---------------------------------------------------------------- 134
    RUN_SUITE("134: Recenter / Ground / Fit 1u survive save and reload");
    {
        Mesh cube = Mesh::Cube();
        cube.GroundPivot();
        Vec3 lo, hi; cube.Bounds(lo, hi);
        CHECK_NEAR(lo.y, 0.0f, 1e-6f);
        CHECK(cube.name.empty());                                    // marked custom, so its geometry is saved
        Mesh back = SaveAndReload(cube);
        back.Bounds(lo, hi);
        std::cout << "  Cube Ground after reload: y " << lo.y << ".." << hi.y << "\n";
        CHECK_NEAR(lo.y, 0.0f, 1e-5f); CHECK_NEAR(hi.y, 1.0f, 1e-5f);

        Mesh fit = Mesh::Cube(); fit.ScaleToFit(3.0f);
        back = SaveAndReload(fit); back.Bounds(lo, hi);
        std::cout << "  Cube Fit(3) after reload: x " << lo.x << ".." << hi.x << "\n";
        CHECK_NEAR(hi.x - lo.x, 3.0f, 1e-4f);

        Mesh tree = Mesh::Tree(); tree.GroundPivot();
        back = SaveAndReload(tree); back.Bounds(lo, hi);
        std::cout << "  Tree Ground after reload: y " << lo.y << ".." << hi.y << "\n";
        CHECK_NEAR(lo.y, 0.0f, 1e-5f);

        Mesh off = Mesh::Cube(); for (Vec3& v : off.vertices) v.x += 2.0f;   // a named mesh moved off centre
        off.RecenterToOrigin();
        CHECK(off.name.empty());
        back = SaveAndReload(off); back.Bounds(lo, hi);
        CHECK_NEAR(lo.x, -0.5f, 1e-5f);

        // A no-op leaves a primitive a primitive (its scene line stays the short name).
        Mesh c2 = Mesh::Cube(); c2.RecenterToOrigin(); c2.ScaleToFit(1.0f);
        CHECK(c2.name == "Cube");
    }

    // ---------------------------------------------------------------- 135
    RUN_SUITE("135: each whole-mesh button is one undo step on the active stack");
    {
        // Edit Mesh on: UV Box, then Twist. One undo takes back the twist only.
        Mesh live = Mesh::Cube();
        std::vector<int> sv, sf; std::vector<std::pair<int, int>> se;
        MeshUndoTarget t; t.mesh = &live; t.verts = &sv; t.faces = &sf; t.edges = &se;
        std::vector<MeshUndoStep> undo, redo;
        int sceneSteps = 0;
        auto scene = [&] { ++sceneSteps; };
        RunUndoableMeshOp(true, undo, redo, t, "UV Box", scene, [](Mesh& m) { m.ProjectUVBox(1.0f); });
        const Mesh afterUV = live;
        RunUndoableMeshOp(true, undo, redo, t, "Twist Y", scene, [](Mesh& m) { m.Twist(90.0f, 1); });
        CHECK(undo.size() == 2);
        CHECK(sceneSteps == 0);                                      // no stray scene step in edit mode
        CHECK(StepMeshUndo(undo, redo, t, false));
        CHECK(SameMesh(live, afterUV));                              // the UVs are still there
        CHECK(!live.uvs.empty());
        CHECK(StepMeshUndo(undo, redo, t, true));                    // redo brings the twist back alone
        CHECK(!SameMesh(live, afterUV));

        // Object mode: Ground is a scene step (it had none at all before).
        Mesh obj = Mesh::Cube();
        MeshUndoTarget t2; t2.mesh = &obj;
        std::vector<MeshUndoStep> u2, r2;
        bool changed = RunUndoableMeshOp(false, u2, r2, t2, "Ground", scene, [](Mesh& m) { m.GroundPivot(); });
        CHECK(changed);
        CHECK(sceneSteps == 1);
        CHECK(u2.empty());

        // A button that changes nothing leaves no empty mesh step.
        Mesh centred = Mesh::Cube();
        MeshUndoTarget t3; t3.mesh = &centred;
        std::vector<MeshUndoStep> u3, r3;
        CHECK(!RunUndoableMeshOp(true, u3, r3, t3, "Recenter", scene, [](Mesh& m) { m.RecenterToOrigin(); }));
        CHECK(u3.empty());
    }

    // ---------------------------------------------------------------- 136
    RUN_SUITE("136: a typed NaN or infinity does not turn the mesh into NaN");
    {
        const float bad[] = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                             -std::numeric_limits<float>::infinity()};
        const char* badName[] = {"nan", "inf", "-inf"};
        struct Op { const char* name; std::function<void(Mesh&, float)> f; };
        const std::vector<Op> ops = {
            {"Shear", [](Mesh& m, float x) { m.Shear(0, 1, x); }},
            {"Jitter", [](Mesh& m, float x) { m.JitterVertices({}, x, 1); }},
            {"CastToCylinder radius", [](Mesh& m, float x) { m.CastToCylinder(x, 1, 1.0f); }},
            {"CastToCylinder amount", [](Mesh& m, float x) { m.CastToCylinder(1.0f, 1, x); }},
            {"ShrinkFatten", [](Mesh& m, float x) { m.ShrinkFatten(x); }},
            {"Wireframe", [](Mesh& m, float x) { m.Wireframe(x); }},
            {"ScaleToFit", [](Mesh& m, float x) { m.ScaleToFit(x); }},
            {"ProjectUVPlanar tile", [](Mesh& m, float x) { m.ProjectUVPlanar(1, x); }},
            {"ProjectUVBox tile", [](Mesh& m, float x) { m.ProjectUVBox(x); }},
            {"ProjectUVCylinder tile", [](Mesh& m, float x) { m.ProjectUVCylinder(x); }},
            {"ProjectUVSphere tile", [](Mesh& m, float x) { m.ProjectUVSphere(x); }},
            {"ShrinkFattenVertices", [](Mesh& m, float x) { m.ShrinkFattenVertices({0, 1, 2}, x); }},
            {"SphereizeVertices", [](Mesh& m, float x) { m.SphereizeVertices({0, 1, 2, 3}, x); }},
            {"ProjectToSphere", [](Mesh& m, float x) { m.ProjectToSphere(x); }},
            {"SculptBrush radius", [](Mesh& m, float x) { m.SculptBrush({0, 0, 0}, {0, 1, 0}, x, 0.5f, 0); }},
            {"SculptBrush strength", [](Mesh& m, float x) { m.SculptBrush({0, 0, 0}, {0, 1, 0}, 1.0f, x, 1); }},
            {"Twist", [](Mesh& m, float x) { m.Twist(x, 1); }},
            {"Taper", [](Mesh& m, float x) { m.Taper(1, x); }},
            {"Spherify", [](Mesh& m, float x) { m.Spherify(x); }},
            {"Bend", [](Mesh& m, float x) { m.Bend(x, 0); }},
            {"Stretch", [](Mesh& m, float x) { m.Stretch(0, x); }},
            {"Solidify", [](Mesh& m, float x) { m.Solidify(x); }},
            {"Smooth", [](Mesh& m, float x) { m.Smooth(x); }},
            {"SnapToGrid", [](Mesh& m, float x) { m.SnapToGrid(x); }},
            {"Displace", [](Mesh& m, float x) { m.Displace(x, 1.0f, 0); }},
        };
        int broken = 0;
        for (const Op& op : ops)
            for (int b = 0; b < 3; ++b) {
                Mesh m = Mesh::Cube();
                op.f(m, bad[b]);
                if (!AllFinite(m)) { ++broken; std::cout << "  non-finite after " << op.name << "(" << badName[b] << ")\n"; }
                CHECK(AllFinite(m));
            }
        std::cout << "  " << broken << " operator/value pairs broke the mesh\n";
    }

    // ---------------------------------------------------------------- 137
    RUN_SUITE("137: Solidify closes the sides of an open surface");
    {
        Mesh plane = Mesh::Plane(1.0f);
        const int openBefore = OpenEdges(plane);
        plane.Solidify(0.1f);
        std::cout << "  Plane(1)+Solidify(0.1): open edges " << openBefore << " -> " << OpenEdges(plane)
                  << ", volume " << SignedVolume(plane) << "\n";
        CHECK(OpenEdges(plane) == 0);
        CHECK(SignedVolume(plane) > 0.0f);                                 // faces point outward
        CHECK_NEAR(SignedVolume(plane), 0.1f, 2e-3f);

        Mesh neg = Mesh::Plane(1.0f);
        neg.Solidify(-0.1f);
        CHECK(OpenEdges(neg) == 0);
        CHECK(SignedVolume(neg) > 0.0f);

        // A sheet with a hole: both borders get a rim.
        Mesh grid = Mesh::Plane(3.0f);
        grid.Subdivide();
        grid.Subdivide();
        grid.DeleteFaces({5});
        const int ob = OpenEdges(grid);
        grid.Solidify(0.2f);
        std::cout << "  holed sheet: open edges " << ob << " -> " << OpenEdges(grid) << "\n";
        CHECK(ob > 0);
        CHECK(OpenEdges(grid) == 0);
        CHECK(SignedVolume(grid) > 0.0f);

        // A closed cube stays two closed shells (no rim).
        Mesh cube = Mesh::Cube(); const int tris = cube.TriangleCount();
        cube.Solidify(0.1f);
        CHECK(cube.TriangleCount() == tris * 2);
        CHECK(OpenEdges(cube) == 0);

        // Face colours follow the rim.
        Mesh col = Mesh::Plane(1.0f);
        col.triColors.assign((std::size_t)col.TriangleCount(), Color{1, 0, 0, 1});
        col.Solidify(0.1f);
        CHECK(col.HasFaceColors());

        // The Solidify modifier uses the same function.
        MeshModifier mod = MeshModifier::Make(MeshModifier::Type::Solidify);
        mod.thickness = 0.1f;
        Mesh out; std::string err;
        CHECK(ApplyModifier(Mesh::Plane(1.0f), mod, ModifierContext(), out, &err));
        CHECK(OpenEdges(out) == 0);
    }

    // ---------------------------------------------------------------- 138
    RUN_SUITE("138: Export OBJ keeps colours, polygons, normals and precision");
    {
        // Face colours -> .mtl, read back as face colours.
        Mesh m = Mesh::Cube();
        m.triColors.clear();
        for (int t = 0; t < m.TriangleCount(); ++t) m.triColors.push_back(t < 6 ? Color{1, 0, 0, 1} : Color{0, 0.5f, 1, 0.5f});
        const std::string p1 = Tmp("colours.obj");
        CHECK(m.SaveOBJ(p1));
        bool ok = false;
        Mesh back = Mesh::LoadOBJ(p1, &ok);
        CHECK(ok);
        std::cout << "  face colours: " << back.triColors.size() << " of " << back.TriangleCount() << "\n";
        CHECK(back.HasFaceColors());
        if (back.HasFaceColors()) {
            CHECK_NEAR(back.triColors[0].r, 1.0f, 1e-6f); CHECK_NEAR(back.triColors[0].g, 0.0f, 1e-6f);
            CHECK_NEAR(back.triColors[back.TriangleCount() - 1].b, 1.0f, 1e-6f);
            CHECK_NEAR(back.triColors[back.TriangleCount() - 1].a, 0.5f, 1e-6f);
        }
        CHECK(std::filesystem::exists(Tmp("colours.mtl")));

        // Vertex colours -> `v x y z r g b`.
        Mesh vc = Mesh::Cube(); vc.uvs.clear(); vc.cornerUVs.clear(); vc.normals.clear();   // one OBJ vertex per mesh vertex
        vc.vertexColors.assign(vc.vertices.size(), Color{0.25f, 0.5f, 0.75f, 1});
        vc.vertexColors[0] = Color{1, 0, 0, 1};
        const std::string p2 = Tmp("vcol.obj");
        CHECK(vc.SaveOBJ(p2));
        back = Mesh::LoadOBJ(p2, &ok);
        std::cout << "  vertex colours: " << back.vertexColors.size() << " of " << back.vertices.size() << "\n";
        CHECK(back.HasVertexColors());
        if (back.HasVertexColors()) {
            CHECK_NEAR(back.vertexColors[0].r, 1.0f, 1e-6f);
            CHECK_NEAR(back.vertexColors[1].g, 0.5f, 1e-6f);
        }

        // A `v x y z w` weight is not a colour.
        { std::ofstream f(Tmp("w.obj")); f << "v 0 0 0 1\nv 1 0 0 1\nv 0 1 0 1\nf 1 2 3\n"; }
        back = Mesh::LoadOBJ(Tmp("w.obj"), &ok);
        CHECK(ok && back.vertexColors.empty());

        // Polygons: a cube made of 6 quads comes back as 6 quads.
        Mesh q = Mesh::Cube();
        q.polyFace.clear();
        for (int t = 0; t < q.TriangleCount(); ++t) q.polyFace.push_back(t / 2);
        const std::string p3 = Tmp("quads.obj");
        CHECK(q.SaveOBJ(p3));
        int quadLines = 0, faceLines = 0;
        { std::istringstream in(ReadAll(p3)); std::string line;
          while (std::getline(in, line)) if (line.rfind("f ", 0) == 0) { ++faceLines; std::istringstream ls(line.substr(2)); std::string w; int n = 0; while (ls >> w) ++n; if (n == 4) ++quadLines; } }
        std::cout << "  face lines " << faceLines << ", quads " << quadLines << "\n";
        CHECK(faceLines == 6 && quadLines == 6);
        back = Mesh::LoadOBJ(p3, &ok);
        CHECK(back.TriangleCount() == 12);
        CHECK((int)back.polyFace.size() == back.TriangleCount());
        CHECK(std::abs(SignedVolume(back) - 1.0f) < 1e-4f);                // winding kept

        // Normals and smooth shading.
        Mesh sm = Mesh::Sphere(0.5f, 8, 6); sm.ComputeSmoothNormals();
        const std::string p4 = Tmp("smooth.obj");
        CHECK(sm.SaveOBJ(p4));
        CHECK(ReadAll(p4).find("\nvn ") != std::string::npos);
        back = Mesh::LoadOBJ(p4, &ok);
        CHECK(back.HasNormals());

        // Precision: an offset cube comes back to within float rounding.
        Mesh far = Mesh::Cube(); far.uvs.clear(); far.cornerUVs.clear(); far.normals.clear();
        for (Vec3& v : far.vertices) v += Vec3{1234.5678f, 0.000123f, -98765.4f};
        const std::string p5 = Tmp("far.obj");
        CHECK(far.SaveOBJ(p5));
        back = Mesh::LoadOBJ(p5, &ok);
        float err = 0;
        for (std::size_t i = 0; i < far.vertices.size() && i < back.vertices.size(); ++i)
            err = std::max(err, (far.vertices[i] - back.vertices[i]).Magnitude());
        std::cout << "  max position error " << err << "\n";
        CHECK(back.vertices.size() == far.vertices.size());
        CHECK(err == 0.0f);

        // Export never overwrites: the next free name.
        auto dir = std::filesystem::path(Tmp("x")).parent_path() / "assets";
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        const std::string a = NextFreeExportPath(dir, "Box");
        CHECK(std::filesystem::path(a).filename() == "Box.obj");
        { std::ofstream f(a); f << "#\n"; }
        const std::string b = NextFreeExportPath(dir, "Box");
        CHECK(std::filesystem::path(b).filename() == "Box_2.obj");
        CHECK(ExportFileStem("a/b:c?") == "a_b_c_");
        CHECK(ExportFileStem("") == "mesh");
        std::filesystem::remove_all(std::filesystem::path(Tmp("x")).parent_path());
    }

    // ---------------------------------------------------------------- 139
    RUN_SUITE("139: one implementation per tool");
    {
        // Symmetrize = keep the + half and mirror it (the Modeling window used to inline this).
        Mesh a = Mesh::Cube(); for (Vec3& v : a.vertices) v.x += 0.25f;
        Mesh b = a;
        a.Symmetrize(0);
        Vec3 lo, hi; a.Bounds(lo, hi);
        CHECK_NEAR(lo.x, -hi.x, 1e-6f);
        CHECK_NEAR(hi.x, 0.75f, 1e-6f);
        CHECK(OpenEdges(a) == 0);
        b.Bisect({0, 0, 0}, {1, 0, 0}, false, true); b.Mirror(0);
        CHECK(a.vertices.size() == b.vertices.size() && a.TriangleCount() == b.TriangleCount());

        // The axis slicer (was "Loop Cut X/Y/Z") is SliceAxis: two slices across X on a cube add two rings.
        Mesh c = Mesh::Cube(); c.WeldVertices();
        const std::size_t v0 = c.vertices.size();
        c.SliceAxis(0, 2);
        std::cout << "  SliceAxis(X, 2): " << v0 << " -> " << c.vertices.size() << " vertices\n";
        CHECK(c.vertices.size() > v0);
        CHECK(OpenEdges(c) == 0);

        // The pivot / detail buttons of the Modeling window and the Mesh Renderer inspector share ApplyShapeOp.
        for (int k = 0; k < (int)ShapeOp::Count; ++k) {
            CHECK(std::string(ShapeOpLabel((ShapeOp)k)).size() > 0);
            CHECK(std::string(ShapeOpTip((ShapeOp)k)).size() > 0);
        }
        Mesh g1 = Mesh::Cube(), g2 = Mesh::Cube();
        ApplyShapeOp(g1, ShapeOp::Ground); g2.GroundPivot();
        CHECK(SameMesh(g1, g2));
        CHECK(g1.name.empty());
        Mesh w = Mesh::Cube();
        CHECK(ApplyShapeOp(w, ShapeOp::Weld).find("Welded") == 0);
        Mesh sub = Mesh::Cube(); const int t0 = sub.TriangleCount();
        ApplyShapeOp(sub, ShapeOp::Subdivide, {0});
        CHECK(sub.TriangleCount() > t0 && sub.TriangleCount() < t0 * 4);   // only the picked face
    }

    TEST_MAIN_RESULT();
}
