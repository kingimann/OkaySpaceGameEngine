// Fuzz every modeling operator with random meshes and random (often hostile) selections: nothing may crash, loop,
// or leave a non-finite vertex or a triangle that points outside the vertex list. Same seeds every run.
#include "test_framework.hpp"
#include "okay/Modeling/Modifiers.hpp"
#include "okay/Modeling/Curve.hpp"
#include "okay/Modeling/Remesh.hpp"
#include "okay/Modeling/PolyOps.hpp"
#include "okay/Modeling/SelectOps.hpp"
#include "okay/Optimize/MeshSimplify.hpp"
#include <cmath>
#include <cstdlib>
#include <functional>
#include <random>
#include <set>

using namespace okay;
using namespace okay::modeling;

namespace {
std::mt19937 rng(20261116u);
int R(int n) { return n <= 0 ? 0 : (int)(rng() % (unsigned)n); }
float F(float lo, float hi) { return lo + (hi - lo) * (float)(rng() % 100000) / 100000.0f; }

Mesh RandomMesh() {
    switch (R(9)) {
        case 0: return Mesh::Cube(F(0.1f, 3.0f));
        case 1: return Mesh::Sphere(F(0.2f, 2.0f), 3 + R(8), 3 + R(10));
        case 2: return Mesh::Cylinder(F(0.1f, 1.0f), F(0.2f, 2.0f), 3 + R(10));
        case 3: return Mesh::Torus(0.5f, 0.2f, 4 + R(6), 3 + R(5));
        case 4: { Mesh m = Mesh::Cube(); m.Combine(Mesh::Cube(0.5f).Transformed({1, 1, 1}, {3, 0, 0})); return m; }
        case 5: { Mesh m = Mesh::Cube(); m.DeleteFaces({R(12), R(12)}); return m; }          // open
        case 6: { Mesh m = Mesh::Quad(1.0f); return m; }
        case 7: { Mesh m; m.vertices = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {1, 0, 0}}; m.triangles = {0, 1, 2, 0, 2, 3, 0, 3, 3}; return m; }   // degenerate
        default: return Mesh{};
    }
}
std::vector<int> RandomTris(const Mesh& m) {
    std::vector<int> v;
    const int nt = m.TriangleCount();
    switch (R(6)) {
        case 0: break;                                                                   // nothing
        case 1: for (int t = 0; t < nt; ++t) v.push_back(t); break;                      // everything
        case 2: v = {-1, nt + 5, 1 << 28}; break;                                        // out of range
        case 3: for (int i = 0; i < 3; ++i) v.push_back(R(std::max(1, nt))); v.push_back(v[0]); v.push_back(v[0]); break;   // repeats
        default: for (int i = 0, n = 1 + R(std::max(1, nt)); i < n; ++i) v.push_back(R(std::max(1, nt)));
    }
    return v;
}
std::vector<int> RandomVerts(const Mesh& m) {
    std::vector<int> v;
    const int nv = (int)m.vertices.size();
    switch (R(4)) {
        case 0: break;
        case 1: v = {-3, nv + 2}; break;
        case 2: for (int i = 0; i < nv; ++i) v.push_back(i); break;
        default: for (int i = 0, n = 1 + R(std::max(1, nv)); i < n; ++i) v.push_back(R(std::max(1, nv)));
    }
    return v;
}
std::vector<std::pair<int, int>> RandomEdges(const Mesh& m) {
    std::vector<std::pair<int, int>> e;
    const int nv = (int)m.vertices.size();
    for (int i = 0, n = R(6); i < n; ++i) e.push_back({R(std::max(1, nv)), R(std::max(1, nv))});
    if (R(3) == 0) { const PolyMesh pm = PolyMesh::FromMesh(m); for (const auto& ed : pm.Edges()) e.push_back({ed.a, ed.b}); }
    if (R(5) == 0) e.push_back({-1, nv + 3});
    return e;
}
float RandomAmount() {
    const float specials[] = {0.0f, -1.0f, 1e-9f, 1e9f, -1e9f, std::nanf(""), INFINITY, 0.5f, 100.0f};
    return R(3) == 0 ? specials[R(9)] : F(-2.0f, 2.0f);
}
const char* Sane(const Mesh& m) {
    for (const Vec3& v : m.vertices) if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return "non-finite vertex";
    for (int idx : m.triangles) if (idx < 0 || idx >= (int)m.vertices.size()) return "triangle index out of range";
    if (m.triangles.size() % 3 != 0) return "triangle list not a multiple of 3";
    if (!m.triColors.empty() && m.triColors.size() != m.triangles.size() / 3) { /* allowed: ignored by renderers */ }
    for (const Vec3& n : m.normals) if (!std::isfinite(n.x) || !std::isfinite(n.y) || !std::isfinite(n.z)) return "non-finite normal";
    for (const Vec2& u : m.uvs) if (!std::isfinite(u.x) || !std::isfinite(u.y)) return "non-finite uv";
    return nullptr;
}
const char* SanePoly(const PolyMesh& p) {
    for (const Vec3& v : p.verts) if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return "non-finite vertex";
    for (const PolyFace& f : p.faces) for (int v : f.verts) if (v < 0 || v >= (int)p.verts.size()) return "face index out of range";
    return nullptr;
}
int g_failedOps = 0;
void Op(const char* name, int rounds, const std::function<const char*()>& body) {
    int bad = 0; const char* first = nullptr;
    for (int i = 0; i < rounds; ++i) {
        const char* why = body();
        if (why) { ++bad; if (!first) first = why; }
    }
    ++okaytest::g_checks;
    if (bad) { ++okaytest::g_failures; ++g_failedOps; std::cerr << "  FAIL: " << name << ": " << bad << " of " << rounds << " runs broke the mesh (" << first << ")\n"; }
}
} // namespace

int main() {
    RUN_SUITE("fuzz: Mesh operators");
    const int N = std::getenv("OKAY_FUZZ_ROUNDS") ? std::max(1, std::atoi(std::getenv("OKAY_FUZZ_ROUNDS"))) : 120;   // more rounds: OKAY_FUZZ_ROUNDS=2000
#define MESHOP(name, call) Op(name, N, [&]() -> const char* { Mesh m = RandomMesh(); const auto t = RandomTris(m); const auto v = RandomVerts(m); const auto e = RandomEdges(m); const float a = RandomAmount(); (void)t; (void)v; (void)e; (void)a; call; return Sane(m); })
    MESHOP("ExtrudeFaces", m.ExtrudeFaces(t, a));
    MESHOP("InsetFaces", m.InsetFaces(t, a));
    MESHOP("SubdivideFaces", m.SubdivideFaces(t));
    MESHOP("PokeFaces", m.PokeFaces(t, a));
    MESHOP("DeleteFaces", m.DeleteFaces(t));
    MESHOP("DuplicateFaces", m.DuplicateFaces(t));
    MESHOP("ExtrudeFacesIndividual", m.ExtrudeFacesIndividual(t, a));
    MESHOP("InsetFacesIndividual", m.InsetFacesIndividual(t, a));
    MESHOP("SeparateFaces", m.SeparateFaces(t));
    MESHOP("FlipNormals", m.FlipNormals());
    MESHOP("OrientFacesOutward", m.OrientFacesOutward());
    MESHOP("WeldVertices", m.WeldVertices(a));
    MESHOP("FillHoles", m.FillHoles());
    MESHOP("Repair", m.Repair(a, true));
    MESHOP("Decimate", m.Decimate(a));
    MESHOP("BevelVertices", m.BevelVertices(v, a));
    MESHOP("SnapToGrid", m.SnapToGrid(a, v));
    MESHOP("MoveVerticesSoft", m.MoveVerticesSoft(v, Vec3{a, 1, 0}, a));
    MESHOP("SmoothVertices", m.SmoothVertices(v, a));
    MESHOP("CollapseEdges", m.CollapseEdges(e));
    MESHOP("SplitEdges", m.SplitEdges(e));
    MESHOP("PaintFaces", m.PaintFaces(Vec3{a, 0, 0}, a, Color{1, 0, 0, 1}, a));
    MESHOP("FillFacesColor", m.FillFacesColor(t, Color{1, 0, 0, 1}));
    MESHOP("PaintVertices", m.PaintVertices(Vec3{a, 0, 0}, a, Color{1, 0, 0, 1}, a, a));
    MESHOP("FillVertexColors", m.FillVertexColors(v, Color{1, 0, 0, 1}));
    MESHOP("Subdivide", m.Subdivide());
    MESHOP("Smooth", m.Smooth(a));
    MESHOP("Spherify", m.Spherify(a));
    MESHOP("Twist", m.Twist(a, R(5) - 1));
    MESHOP("Taper", m.Taper(R(5) - 1, a));
    MESHOP("Bend", m.Bend(a, R(5) - 1));
    MESHOP("Stretch", m.Stretch(R(5) - 1, a));
    MESHOP("Solidify", m.Solidify(a));
    MESHOP("Mirror", m.Mirror(R(5) - 1, R(2) == 0));
    MESHOP("Bisect", m.Bisect(Vec3{0, a, 0}, Vec3{R(2) ? 1.0f : 0.0f, 1, 0}, R(2) == 0, R(2) == 0));
    MESHOP("Displace", m.Displace(a, a, R(100)));
    MESHOP("BevelWeights+Weld", { for (const auto& ed : e) m.SetBevelWeight(ed.first, ed.second, a); m.WeldVertices(F(0.0f, 0.5f)); });
    Op("Quad remesh + TrisToQuads", N / 4 + 1, []() -> const char* {
        Mesh m = RandomMesh(); okay::modeling::QuadRemeshOptions o; o.targetQuads = R(60) - 5; o.allQuads = R(2) == 0; o.relaxIterations = R(8) - 1; o.snapToSource = R(2) == 0;
        okay::modeling::QuadRemeshReport rep; const PolyMesh p = okay::modeling::QuadRemesh(m, o, &rep);
        if (const char* w = SanePoly(p)) return w;
        int l = 0; const PolyMesh q = okay::modeling::TrisToQuads(m, RandomAmount(), RandomAmount(), &l);
        return SanePoly(q);
    });
    Op("Curve builders", N, []() -> const char* {
        std::vector<Vec3> pts; for (int i = 0, n = R(7); i < n; ++i) pts.push_back(R(8) == 0 ? Vec3{RandomAmount(), 0, 0} : Vec3{F(-3, 3), F(-3, 3), F(-3, 3)});
        if (R(4) == 0 && !pts.empty()) pts.push_back(pts[0]);
        const auto c = okay::modeling::CurveThrough(pts, R(2) == 0, RandomAmount());
        std::vector<Vec2> prof; for (int i = 0, n = R(6); i < n; ++i) prof.push_back({F(-1, 1), F(-1, 1)});
        const Mesh a = okay::modeling::CurvePipe(c, RandomAmount(), R(20) - 2, R(12) - 2, R(2) == 0);
        const Mesh b = okay::modeling::CurveSweep(c, prof, R(2) == 0, R(12) - 2, RandomAmount(), R(2) == 0);
        const Mesh r = okay::modeling::CurveRibbon(c, RandomAmount(), R(12) - 2);
        for (const Mesh* m : {&a, &b, &r}) if (const char* w = Sane(*m)) return w;
        return nullptr;
    });
    Op("Bevel modifier with weights", N, []() -> const char* {
        Mesh m = RandomMesh(); const auto e = RandomEdges(m);
        for (const auto& ed : e) m.SetBevelWeight(ed.first, ed.second, RandomAmount());
        MeshModifier mod = MeshModifier::Make(MeshModifier::Type::Bevel); mod.width = RandomAmount(); mod.segments = R(6) - 1; mod.useBevelWeights = R(3) != 0;
        Mesh out; std::string err; ApplyModifier(m, mod, ModifierContext(), out, &err);
        return Sane(out);
    });
    MESHOP("HideReveal", { m.HideTriangles(t); m.RevealHidden(); });
    MESHOP("Isolate", { m.IsolateTriangles(t); m.RevealHidden(); });
    MESHOP("Combine", m.Combine(RandomMesh()));
    MESHOP("AssignSlot+Apply", { m.AssignSlot(t, R(5) - 1); m.ApplySlotColors(); });
    MESHOP("Hide then tools", { m.HideTriangles(t); m.ExtrudeFaces(RandomTris(m), a); m.DeleteFaces(RandomTris(m)); m.RevealHidden(); });
    RUN_SUITE("fuzz: polygon tools");
#define POLYOP(name, call) Op(name, N, [&]() -> const char* { Mesh base = RandomMesh(); PolyMesh pm = PolyMesh::FromMesh(base); const auto e = RandomEdges(base); EdgeList el(e.begin(), e.end()); const auto t = RandomTris(base); const auto v = RandomVerts(base); const float a = RandomAmount(); (void)el; (void)t; (void)v; (void)a; std::string err; (void)err; call; if (const char* bad = SanePoly(pm)) return bad; return Sane(pm.ToMesh()); })
    POLYOP("LoopCut", LoopCut(pm, R(8), R(8), 1 + R(4), a));
    POLYOP("BevelEdges", BevelEdges(pm, el, a, &err, R(6) - 1, a));
    POLYOP("DissolveEdges", DissolveEdges(pm, el, R(2) == 0));
    POLYOP("DissolveVerts", DissolveVerts(pm, v));
    POLYOP("SplitEdge", SplitEdge(pm, R(8), R(8), a));
    POLYOP("MergeVerts", MergeVerts(pm, v, Vec3{a, 0, 0}));
    POLYOP("BridgeEdgeLoops", BridgeEdgeLoops(pm, el, &err));
    POLYOP("FillFaces", FillFaces(pm, el, &err));
    POLYOP("GridFill", GridFill(pm, el, R(8) - 1, &err));
    POLYOP("EdgeSlide", EdgeSlide(pm, el, a));
    POLYOP("CatmullClark", pm = CatmullClark(pm, R(3)));
    POLYOP("SpinEdges", SpinEdges(pm, el, Vec3{a, 0, 0}, Vec3{0, 1, 0}, a * 180.0f, R(40) - 2, &err));
    POLYOP("ExtrudeAlongPath", ExtrudeAlongPath(pm, t, std::vector<Vec3>{Vec3{0, 0, 0}, Vec3{a, 1, 0}, Vec3{0, 2, a}}, &err));
    POLYOP("CompactVerts", CompactVerts(pm));
    POLYOP("Creased CatmullClark", { for (const auto& ed : pm.Edges()) if (R(2)) pm.SetCrease(ed.a, ed.b, F(0.0f, 3.0f)); pm = CatmullClark(pm, R(3)); });
    POLYOP("KnifeCut", KnifeCut(pm, std::vector<KnifePoint>{{R(8), R(8), a}, {R(8), -1, 0}}, &err));
    RUN_SUITE("fuzz: selection, recipes, modifiers");
    MESHOP("Shrinkwrap", Shrinkwrap(m, v, RandomMesh(), R(2) ? WrapMode::Nearest : WrapMode::Project, a, a, Vec3{a, 1, 0}));
    MESHOP("SetCrease", { for (const auto& ed : e) m.SetCrease(ed.first, ed.second, a); });
    MESHOP("SimplifyMesh", { okay::optimize::SimplifyOptions so; so.targetRatio = a; m = okay::optimize::SimplifyMesh(m, so).mesh; });
    MESHOP("GrowFaces", GrowFaces(m, t));
    MESHOP("ShrinkFaces", ShrinkFaces(m, t));
    MESHOP("SelectSimilar", SelectSimilar(m, t, (SimilarKind)R(4), a));
    MESHOP("SelectByNormal", SelectByNormal(m, Vec3{a, 1, 0}, a));
    MESHOP("LooseParts", LooseParts(m));
    MESHOP("BakeTransform", BakeTransform(m, Vec3{a, 1, a}, Quat::AngleAxis(a * 90.0f, Vec3{0, 1, 0})));
    MESHOP("MakeWindowHole", MakeWindowHole(m, t, a));
    MESHOP("RoundEdges", RoundEdges(m, e, a, R(20) - 2));
    MESHOP("MakeTable", m = MakeTable(a, a, a, a, a));
    for (int ty = 0; ty < (int)MeshModifier::Type::Count; ++ty) {
        if ((MeshModifier::Type)ty == MeshModifier::Type::Boolean) continue;                // needs a scene
        Op(MeshModifier::TypeName((MeshModifier::Type)ty), N, [&]() -> const char* {
            Mesh m = RandomMesh(); MeshModifier mod = MeshModifier::Make((MeshModifier::Type)ty);
            mod.count = R(40) - 3; mod.levels = R(7) - 1; mod.thickness = RandomAmount(); mod.width = RandomAmount(); mod.segments = R(12) - 2;
            mod.angle = RandomAmount(); mod.cellSize = RandomAmount(); mod.strength = RandomAmount(); mod.frequency = RandomAmount();
            mod.offset = Vec3{RandomAmount(), 0, 0}; mod.relative = R(2) == 0;
            if (R(4) == 0) mod.path.assign(R(3), Vec3{RandomAmount(), 0, 0});
            Mesh out; std::string err;
            ApplyModifier(m, mod, ModifierContext(), out, &err);
            return Sane(out);
        });
    }
    std::cout << g_failedOps << " operators broke the mesh\n";
    TEST_MAIN_RESULT();
}
