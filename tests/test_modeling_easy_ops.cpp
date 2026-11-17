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
    if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
    std::printf("modeling_easy_ops: all passed\n");
    return 0;
}
