// The beginner tools run the engine's own operations on a real mesh (editor/ModelingEasyOps.hpp), headless.
#include "ModelingEasyOps.hpp"
#include "ModelingStarters.hpp"
#include <cmath>
#include <cstdio>
using namespace oked::easymodel;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

// The triangles of a cube face that look toward +Z.
static std::vector<int> FrontTris(const okay::Mesh& m) {
    std::vector<int> f;
    for (int t = 0; t < m.TriangleCount(); ++t) {
        const okay::Vec3 &a = m.vertices[m.triangles[t*3]], &b = m.vertices[m.triangles[t*3+1]], &c = m.vertices[m.triangles[t*3+2]];
        if (a.z > 0.49f && b.z > 0.49f && c.z > 0.49f) f.push_back(t);
    }
    return f;
}
static float Height(const okay::Mesh& m) { return m.Size().y; }

int main() {
    ToolArgs a;
    { okay::Mesh m = okay::Mesh::Cube(1.0f); const int n = m.TriangleCount();
      auto r = RunMeshTool(m, "detail.add", a); CHECK(r.handled && r.ok); CHECK(m.TriangleCount() > n); CHECK(m.Topology().Closed()); }
    { okay::Mesh m = okay::Mesh::Cube(1.0f); const float h = Height(m);
      RunMeshTool(m, "squash", a); CHECK(Height(m) < h); CHECK(m.Topology().Closed()); }
    { okay::Mesh m = okay::Mesh::Cube(1.0f); const int n = m.TriangleCount();
      auto r = RunMeshTool(m, "cut.half", a); CHECK(r.ok && m.TriangleCount() > n); CHECK(m.Topology().Closed()); }
    { okay::Mesh m = okay::Mesh::Cube(1.0f); auto r = RunMeshTool(m, "round.it", a); CHECK(r.ok && m.TriangleCount() > 12); }
    { okay::Mesh m = okay::Mesh::Cube(1.0f); const int n = m.TriangleCount(); RunMeshTool(m, "hollow", a); CHECK(m.TriangleCount() > n); }
    { okay::Mesh m = okay::Mesh::Cube(1.0f); const int n = m.TriangleCount(); RunMeshTool(m, "mirror", a); CHECK(m.TriangleCount() >= n); }
    // Needs a pick: a plain message, and the mesh is left alone.
    for (const char* id : {"push.out", "make.hole", "delete.faces", "paint.faces"}) {
        okay::Mesh m = okay::Mesh::Cube(1.0f); const int n = m.TriangleCount();
        auto r = RunMeshTool(m, id, a); CHECK(r.handled && !r.ok && r.message.find("Select at least one face") == 0);
        CHECK(m.TriangleCount() == n);
        CHECK(FindBannedWord(r.message).empty());
    }
    { okay::Mesh m = okay::Mesh::Cube(1.0f); auto r = RunMeshTool(m, "round.corners", a); CHECK(!r.ok && r.message.find("corner") != std::string::npos); }
    // Out-of-range picks (a stale selection) are refused, not crashed on.
    { okay::Mesh m = okay::Mesh::Cube(1.0f); ToolArgs b; b.faces = {9999}; auto r = RunMeshTool(m, "push.out", b); CHECK(!r.ok && m.TriangleCount() == 12); }
    // Push out grows the model; make hole opens it; delete faces removes.
    { okay::Mesh m = okay::Mesh::Cube(1.0f); ToolArgs b; b.faces = FrontTris(m); CHECK(b.faces.size() == 2);
      const float d = m.Size().z; auto r = RunMeshTool(m, "push.out", b); CHECK(r.ok); CHECK(m.Size().z > d); CHECK(m.Topology().Closed()); }
    { okay::Mesh m = okay::Mesh::Cube(1.0f); ToolArgs b; b.faces = FrontTris(m);
      auto r = RunMeshTool(m, "make.hole", b); CHECK(r.ok); CHECK(m.Topology().boundaryEdges > 0); CHECK(m.TriangleCount() > 10); }
    { okay::Mesh m = okay::Mesh::Cube(1.0f); ToolArgs b; b.faces = FrontTris(m);
      RunMeshTool(m, "delete.faces", b); CHECK(m.TriangleCount() == 10); }
    { okay::Mesh m = okay::Mesh::Cube(1.0f); ToolArgs b; b.corners = {0}; const int n = m.TriangleCount();
      auto r = RunMeshTool(m, "round.corners", b); CHECK(r.ok && m.TriangleCount() > n); }
    // Paint: all faces, or just the picked ones; the colour count stays in step.
    { okay::Mesh m = okay::Mesh::Cube(1.0f); ToolArgs b; b.r = 1; b.g = 0; b.b = 0;
      RunMeshTool(m, "paint.all", b); CHECK((int)m.triColors.size() == 12 && m.triColors[5].r == 1 && m.triColors[5].g == 0);
      ToolArgs c; c.faces = {0}; c.r = 0; c.g = 1; c.b = 0; RunMeshTool(m, "paint.faces", c);
      CHECK(m.triColors[0].g == 1 && m.triColors[1].g == 0 && (int)m.triColors.size() == 12); }
    // Fix problems: a box with a missing face is closed again, and the report says so in plain words.
    { okay::Mesh m = okay::Mesh::Cube(1.0f); m.DeleteFaces({0});
      auto r = RunMeshTool(m, "fix.problems", a); CHECK(r.handled && r.ok); CHECK(m.Topology().Closed()); CHECK(r.message.find("closed") != std::string::npos);
      CHECK(FindBannedWord(r.message).empty()); }
    { okay::Mesh m = okay::Mesh::Cube(1.0f); auto r = RunMeshTool(m, "fix.problems", a); CHECK(r.ok && r.message.find("No problems") == 0); }
    // Every starter survives the beginner tools and stays closed after the whole-model ones.
    for (const Starter& s : Starters()) for (const char* id : {"smooth", "round.it", "squash", "on.ground"}) {
        okay::Mesh m = BuildStarter(s.id); const bool wasClosed = m.Topology().Closed();
        RunMeshTool(m, id, a);
        for (const okay::Vec3& v : m.vertices) CHECK(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z));
        if (wasClosed && std::string(id) != "smooth") CHECK(m.Topology().Closed());
    }
    // Strength is clamped; a NaN slider cannot break a tool.
    { okay::Mesh m = okay::Mesh::Cube(1.0f); ToolArgs b; b.strength = NAN; RunMeshTool(m, "smooth", b); for (auto& v : m.vertices) CHECK(std::isfinite(v.x)); }
    CHECK(ClampStrength(5) == 1.0f && ClampStrength(-1) == 0.1f);
    // Window-only ids are left to the window.
    { okay::Mesh m = okay::Mesh::Cube(1.0f); CHECK(!RunMeshTool(m, "add.ball", a).handled && !RunMeshTool(m, "save.asset", a).handled && !RunMeshTool(m, "move", a).handled); }
    // Pick whole part: one box of the table, not the whole table; works from a face or a corner.
    {
        okay::Mesh t = BuildStarter("table"); ToolArgs b; b.faces = {0};
        auto r = RunPickTool(t, "select.part", b);
        CHECK(r.handled && r.ok && r.faces.size() >= 12 && (int)r.faces.size() < t.TriangleCount());
        ToolArgs c; c.corners = {0}; auto rc = RunPickTool(t, "select.part", c);
        CHECK(rc.ok && !rc.corners.empty() && rc.faces.empty() && rc.corners.size() < t.vertices.size());
        CHECK(!RunPickTool(t, "select.part", ToolArgs{}).ok);                                  // nothing picked: a plain message
        CHECK(RunPickTool(t, "select.part", ToolArgs{}).message.find("Pick something") == 0);
        ToolArgs bad; bad.faces = {99999}; CHECK(!RunPickTool(t, "select.part", bad).ok);
        CHECK(!RunPickTool(t, "squash", b).handled);
    }
    // Pick similar: the faces that look the same way.
    {
        okay::Mesh cube = okay::Mesh::Cube(1.0f); ToolArgs b; b.faces = FrontTris(cube);
        auto r = RunPickTool(cube, "select.similar", b);
        CHECK(r.ok && r.faces.size() == 2);                                                      // only the front face
        CHECK(!RunPickTool(cube, "select.similar", ToolArgs{}).ok);
        okay::Mesh sub = cube; sub.Subdivide(); ToolArgs c; c.faces = FrontTris(sub);
        CHECK(RunPickTool(sub, "select.similar", c).faces.size() == c.faces.size());          // a cut-up flat face still counts as one look
    }
    // Live preview: dragging never piles up, and letting go restores the model exactly so one real run is one undo step.
    {
        okay::Mesh m = okay::Mesh::Cube(1.0f); const okay::Mesh orig = m;
        StrengthPreview pv; ToolArgs a1; a1.strength = 0.2f;
        pv.Update(m, "round.it", a1); const int t1 = m.TriangleCount();
        a1.strength = 0.8f; pv.Update(m, "round.it", a1); pv.Update(m, "round.it", a1);
        okay::Mesh once = orig; RunMeshTool(once, "round.it", a1);
        CHECK(m.vertices.size() == once.vertices.size() && m.TriangleCount() == once.TriangleCount() && t1 > 12);   // same as one run, not two or three
        for (std::size_t i = 0; i < m.vertices.size(); ++i) CHECK(m.vertices[i].x == once.vertices[i].x && m.vertices[i].y == once.vertices[i].y);
        CHECK(pv.active && pv.End(m) && !pv.active);
        CHECK(m.vertices.size() == orig.vertices.size() && m.triangles == orig.triangles);
        for (std::size_t i = 0; i < m.vertices.size(); ++i) CHECK(m.vertices[i].x == orig.vertices[i].x && m.vertices[i].z == orig.vertices[i].z);
        CHECK(!pv.End(m));                                                                         // nothing to end twice
    }
    CHECK(UsesStrength("smooth") && UsesStrength("round.corners") && !UsesStrength("mirror"));
    if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
    std::printf("modeling_easy_ops: all passed\n");
    return 0;
}
