// Modeling "more": per-face material slots (the M5b gap) and the audit fixes.
// Written from the spec in docs/modeling.md and docs/modeling_audit.md.
#include "test_framework.hpp"
#include "okay/Modeling/MeshAsset.hpp"
#include "okay/Modeling/MeshUndo.hpp"
#include "okay/Modeling/MaterialLibrary.hpp"
#include "okay/Modeling/Curve.hpp"
#include "okay/Modeling/Remesh.hpp"
#include "okay/Modeling/Reference.hpp"
#include "okay/Modeling/Arrange.hpp"
#include "okay/Modeling/EditTransform.hpp"
#include "okay/Modeling/MeshLibrary.hpp"
#include "okay/Optimize/MeshSimplify.hpp"
#include "okay/Modeling/Measure.hpp"
#include "okay/Modeling/ScreenSelect.hpp"
#include "okay/Modeling/Modifiers.hpp"
#include "okay/Modeling/PolyOps.hpp"
#include "okay/Modeling/SelectOps.hpp"
#include <chrono>
#include <cmath>
#include <set>
#include <tuple>
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace okay;
using namespace okay::modeling;

int main() {
    RUN_SUITE("slots: Mesh helpers");
    {
        Mesh m = Mesh::Cube();
        CHECK(!m.HasSlots());
        CHECK(m.SlotCount() == 0);
        CHECK(m.AssignSlot({0, 1}, 2) == 2);
        CHECK(m.HasSlots());
        CHECK(m.SlotCount() == 3);
        CHECK(m.slotColors.size() == 3);
        CHECK(m.AssignSlot({0}, 2) == 0);               // already there
        CHECK(m.AssignSlot({-1, 9999}, 1) == 0);        // out of range ignored
        CHECK(m.AssignSlot({3}, -1) == 0 && m.AssignSlot({3}, 999) == 0);
        CHECK(m.TrianglesInSlot(2).size() == 2);
        CHECK((int)m.TrianglesInSlot(0).size() == m.TriangleCount() - 2);
        m.slotColors[2] = Color{1, 0, 0, 1};
        m.ApplySlotColors();
        CHECK(m.HasFaceColors());
        CHECK(m.triColors[0].r == 1 && m.triColors[0].g == 0);
        CHECK(m.triColors[5].g == 1);                   // slot 0 stays white
        m.ClearSlots();
        CHECK(!m.HasSlots() && m.slotColors.empty());
        Mesh n = Mesh::Cube();
        n.ApplySlotColors();                            // no slots: nothing happens
        CHECK(!n.HasFaceColors());
    }
    RUN_SUITE("slots: Mesh edits keep them aligned");
    {
        Mesh m = Mesh::Cube();
        m.AssignSlot({0, 1, 2}, 1);
        const int tris = m.TriangleCount();
        m.DeleteFaces({0});
        CHECK(m.TriangleCount() == tris - 1);
        CHECK(m.HasSlots());
        CHECK(m.TrianglesInSlot(1).size() == 2);
        Mesh p = Mesh::Cube();
        p.AssignSlot({0}, 1);
        p.PokeFaces({0}, 0.1f);
        CHECK(p.HasSlots());
        CHECK(p.TrianglesInSlot(1).size() == 3);       // the poked triangle's three children
        Mesh w = Mesh::Cube();
        w.AssignSlot({4, 5}, 2);
        w.WeldVertices();
        CHECK(w.HasSlots());
        CHECK(w.TrianglesInSlot(2).size() == 2);
    }
    RUN_SUITE("slots: survive Mesh -> PolyMesh -> Mesh");
    {
        Mesh m = Mesh::Cube();
        const PolyMesh base = PolyMesh::FromMesh(m);
        // Slot per polygon through the polygon mesh, as the editor does.
        PolyMesh pm = base;
        for (int i = 0; i < (int)pm.faces.size(); ++i) pm.faces[i].material = i % 3;
        pm.slotColors = {Color{1, 0, 0, 1}, Color{0, 1, 0, 1}, Color{0, 0, 1, 1}};
        const Mesh out = pm.ToMesh();
        CHECK(out.HasSlots());
        CHECK(out.slotColors.size() == 3);
        CHECK(out.HasFaceColors());
        for (int t = 0; t < out.TriangleCount(); ++t) {
            const int slot = out.triSlot[t];
            CHECK(slot >= 0 && slot < 3);
            CHECK(out.triColors[t].r == out.slotColors[slot].r && out.triColors[t].g == out.slotColors[slot].g);
        }
        const PolyMesh back = PolyMesh::FromMesh(out);
        int sum = 0, want = 0;
        for (const auto& f : back.faces) sum += f.material;
        for (const auto& f : pm.faces) want += f.material;
        CHECK(sum == want);                              // audit: used to be 0
        CHECK(back.slotColors.size() == 3);
        // Subdivision keeps slots: each child face inherits its parent's slot and the table.
        const PolyMesh cc = CatmullClark(pm, 1);
        CHECK(cc.slotColors.size() == 3);
        int hist[3] = {0, 0, 0};
        for (const auto& f : cc.faces) hist[f.material % 3]++;
        CHECK(hist[0] == 8 && hist[1] == 8 && hist[2] == 8);
        // A face with its own colour (paint) keeps it; the slot colour fills the rest.
        PolyMesh painted = pm;
        painted.faces[0].hasColor = true; painted.faces[0].color = Color{0, 0, 0, 1};
        const Mesh pout = painted.ToMesh();
        CHECK(pout.triColors[0].r == 0 && pout.triColors[0].g == 0 && pout.triColors[0].b == 0);
        CHECK(pout.triColors[pout.TriangleCount() - 1].r + pout.triColors[pout.TriangleCount() - 1].g + pout.triColors[pout.TriangleCount() - 1].b > 0);
        // A mesh with no slots stays without slots.
        CHECK(!base.ToMesh().HasSlots());
    }
    RUN_SUITE("slots: .okaymesh file");
    {
        PolyMesh pm = PolyMesh::FromMesh(Mesh::Cube());
        for (int i = 0; i < (int)pm.faces.size(); ++i) pm.faces[i].material = i % 2;
        pm.slotColors = {Color{0.5f, 0.25f, 0.125f, 1}, Color{0, 1, 1, 1}};
        PolyMesh r; std::string err;
        CHECK(PolyMesh::Parse(pm.Serialize(), r, &err));
        CHECK(r.slotColors.size() == 2);
        CHECK(r.slotColors[0].r == 0.5f && r.slotColors[0].g == 0.25f && r.slotColors[1].b == 1);
        int sum = 0; for (const auto& f : r.faces) sum += f.material;
        CHECK(sum == 3);
        // An old file without slot lines loads with no table.
        PolyMesh plain = PolyMesh::FromMesh(Mesh::Cube());
        CHECK(plain.Serialize().find("slot") == std::string::npos);
        PolyMesh o; CHECK(PolyMesh::Parse(plain.Serialize(), o, &err) && o.slotColors.empty());
        // Bad slot lines are refused with a reason.
        CHECK(!PolyMesh::Parse("okaymesh 1\nverts 0\nfaces 0\nslot 999 1 1 1 1\n", o, &err) && err.find("slot") != std::string::npos);
        CHECK(!PolyMesh::Parse("okaymesh 1\nverts 0\nfaces 0\nslot 0 1 1\n", o, &err));
        // A table with a gap fills the gap with white.
        CHECK(PolyMesh::Parse("okaymesh 1\nverts 0\nfaces 0\nslot 2 0 0 1 1\n", o, &err) && o.slotColors.size() == 3 && o.slotColors[0].r == 1);
    }
    RUN_SUITE("audit: dissolve edges");
    {
        // Four edges of a cube used to leave 0 faces (the valence-2 clean-up ate everything).
        PolyMesh cube = PolyMesh::FromMesh(Mesh::Cube());
        EdgeList el; int i = 0;
        for (const auto& e : cube.Edges()) if (i++ % 3 == 0) el.push_back({e.a, e.b});
        PolyMesh a = cube;
        CHECK(DissolveEdges(a, el) == 4);
        CHECK(a.faces.size() >= 1);
        for (const auto& f : a.faces) CHECK(f.Size() >= 3);
        // Dissolving every edge never erases the whole mesh.
        PolyMesh all = cube; EdgeList every;
        for (const auto& e : all.Edges()) every.push_back({e.a, e.b});
        DissolveEdges(all, every);
        CHECK(!all.faces.empty());
        // Edges that aren't edges, or border edges, are skipped.
        PolyMesh b = cube;
        CHECK(DissolveEdges(b, {{0, 0}, {-1, 5}, {99, 100}}) == 0);
        CHECK(b.faces.size() == 6);
        // A big selection stays fast: all edges of a 240k-triangle sphere (the old version scanned
        // every face for every edge and needed minutes; this one takes about a second).
        PolyMesh big = PolyMesh::FromMesh(Mesh::Sphere(1, 300, 400));
        EdgeList bigEdges;
        for (const auto& e : big.Edges()) bigEdges.push_back({e.a, e.b});
        const auto t0 = std::chrono::steady_clock::now();
        const int n = DissolveEdges(big, bigEdges);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(n > 1000);
        CHECK(!big.faces.empty());
        CHECK(secs < 20.0);
    }
    RUN_SUITE("audit: Mesh tools with odd input");
    {
        Mesh d = Mesh::Cube();
        const int tris = d.TriangleCount();
        d.DeleteFaces({0, 0, 0});                       // a repeated number is one face (used to delete three)
        CHECK(d.TriangleCount() == tris - 1);
        Mesh n = Mesh::Cube();
        n.Decimate(std::nanf(""));                      // used to erase the whole mesh
        CHECK(n.TriangleCount() == tris);
        Mesh w = Mesh::Cube();
        w.WeldVertices(-1.0f);                          // a negative distance used to merge everything within 1 unit
        CHECK(w.vertices.size() == Mesh::Cube().vertices.size() && w.TriangleCount() == tris);
        // Weld finds the same merges as an all-pairs search (the first vertex within the distance wins).
        Mesh small = Mesh::Sphere(1, 40, 56);
        small.vertices[5] = small.vertices[9] + Vec3{4e-6f, 0, 0};   // near, but not the same, position
        std::vector<Vec3> uniq;
        for (const Vec3& v : small.vertices) {
            bool found = false;
            for (const Vec3& u : uniq) if ((u - v).SqrMagnitude() <= 1e-10f) { found = true; break; }
            if (!found) uniq.push_back(v);
        }
        const int expectRemoved = (int)small.vertices.size() - (int)uniq.size();
        Mesh copy = small;
        CHECK(copy.WeldVertices() == expectRemoved && expectRemoved > 0);
        CHECK(copy.vertices.size() == uniq.size());
        // ...and is fast on a big mesh.
        Mesh big = Mesh::Sphere(1, 300, 400);
        const auto t0 = std::chrono::steady_clock::now();
        big.WeldVertices();
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(secs < 4.0);
    }
    RUN_SUITE("repair mesh");
    {
        auto volume = [](const Mesh& m) {
            float v = 0;
            for (int t = 0; t < m.TriangleCount(); ++t) {
                const Vec3& p = m.vertices[m.triangles[t * 3]]; const Vec3& q = m.vertices[m.triangles[t * 3 + 1]]; const Vec3& r = m.vertices[m.triangles[t * 3 + 2]];
                v += Vec3::Dot(p, Vec3::Cross(q, r)) / 6.0f;
            }
            return v;
        };
        // Filling a hole keeps the paint and the slots of the other faces.
        Mesh h = Mesh::Cube();
        h.AssignSlot({4, 5}, 1); h.slotColors[1] = Color{1, 0, 0, 1}; h.ApplySlotColors();
        h.DeleteFaces({0, 1});
        const int keptTris = h.TriangleCount();
        CHECK(h.FillHoles() == 1);
        CHECK(h.HasFaceColors() && h.HasSlots() && h.TriangleCount() > keptTris);
        CHECK(h.TrianglesInSlot(1).size() == 2);
        CHECK(h.Topology().Closed());
        // A broken mesh: a face missing, one face flipped, a repeated corner and a collapsed triangle.
        Mesh b = Mesh::Cube();
        b.DeleteFaces({0, 1});
        std::swap(b.triangles[3 * 3 + 1], b.triangles[3 * 3 + 2]);                       // flipped face
        b.vertices.push_back(b.vertices[b.triangles[0]]);                                // a copy of a corner...
        b.triangles[1] = (int)b.vertices.size() - 1;                                     // ...used by one face
        b.triangles.insert(b.triangles.end(), {0, 0, 1});                                // collapsed triangle
        CHECK(!b.Topology().Closed());
        const Mesh::RepairReport rep = b.Repair();
        CHECK(rep.mergedVerts >= 1 && rep.holesFilled >= 1 && rep.flippedFaces >= 1);
        CHECK(rep.openEdgesBefore > 0 && rep.openEdgesAfter == 0 && rep.ClosedAfter());
        CHECK(volume(b) > 0.9f && volume(b) < 1.1f);                                    // outward, about the cube's volume
        CHECK(b.Topology().degenerateTris == 0);
        // A sound mesh is left alone.
        Mesh ok = Mesh::Cube();
        const Mesh::RepairReport none = ok.Repair();
        CHECK(none.mergedVerts == 0 && none.holesFilled == 0 && none.flippedFaces == 0 && ok.TriangleCount() == 12);
        Mesh empty;
        CHECK(empty.Repair().mergedVerts == 0);
    }
    RUN_SUITE("audit: NaN never reaches the vertices");
    {
        auto finite = [](const Mesh& m) { for (const Vec3& v : m.vertices) if (!std::isfinite(v.x + v.y + v.z)) return false; return true; };
        const float nan = std::nanf("");
        Mesh a = Mesh::Cube(); a.ExtrudeFaces({0}, nan); CHECK(finite(a) && a.TriangleCount() == 12);
        Mesh b = Mesh::Cube(); b.InsetFaces({0}, nan); CHECK(finite(b) && b.TriangleCount() == 12);
        Mesh c = Mesh::Cube(); c.InsetFacesIndividual({0}, nan); CHECK(finite(c));
        Mesh d = Mesh::Cube(); d.ExtrudeFacesIndividual({0}, nan); CHECK(finite(d));
        PolyMesh pm = PolyMesh::FromMesh(Mesh::Cube());
        EdgeList all; for (const auto& e : pm.Edges()) all.push_back({e.a, e.b});
        std::string err;
        CHECK(BevelEdges(pm, all, nan, &err) == 0 && !err.empty());
        bool ok = true; for (const Vec3& v : pm.verts) if (!std::isfinite(v.x + v.y + v.z)) ok = false;
        CHECK(ok);
    }
    RUN_SUITE("spin (lathe of selected edges)");
    {
        std::string err;
        // A vertical line on the +X side, turned a full circle in 12 steps: a tube wall.
        PolyMesh wall; wall.verts = {{0.5f, 0, 0}, {0.5f, 1, 0}};
        CHECK(SpinEdges(wall, {{0, 1}}, {0, 0, 0}, {0, 1, 0}, 360.0f, 12, &err) == 12);
        CHECK(wall.faces.size() == 12 && wall.verts.size() == 2 + 11 * 2);
        const auto rep = wall.Check();
        CHECK(rep.Valid() && rep.nonManifoldEdges == 0 && rep.boundaryEdges == 24);     // open at the top and the bottom
        for (int f = 0; f < (int)wall.faces.size(); ++f) {                              // every quad faces away from the axis
            Vec3 c{0, 0, 0}; for (int v : wall.faces[f].verts) c += wall.verts[v]; c = c * 0.25f; c.y = 0;
            CHECK(Vec3::Dot(wall.FaceNormal(f), c) > 0);
        }
        // A radius lying on the floor spun about Y: a disc of triangles around the centre point.
        PolyMesh disc; disc.verts = {{0, 0, 0}, {0.5f, 0, 0}};
        CHECK(SpinEdges(disc, {{0, 1}}, {0, 0, 0}, {0, 1, 0}, 360.0f, 16, &err) == 16);
        CHECK(disc.faces.size() == 16 && disc.CountFaces(3) == 16 && disc.Check().Valid());
        CHECK(disc.verts.size() == 2 + 15);                                           // the centre is one vertex
        // A quarter turn is open: 4 steps give 5 rings.
        PolyMesh q; q.verts = {{0.5f, 0, 0}, {0.5f, 1, 0}};
        CHECK(SpinEdges(q, {{0, 1}}, {0, 0, 0}, {0, 1, 0}, 90.0f, 4, &err) == 4 && q.verts.size() == 10);
        float maxZ = 0; for (const Vec3& v : q.verts) maxZ = std::max(maxZ, std::fabs(v.z));
        CHECK_NEAR(maxZ, 0.5f, 1e-4);
        // Wrong input says why and changes nothing.
        PolyMesh bad = wall; const size_t nf = bad.faces.size();
        CHECK(SpinEdges(bad, {}, {0, 0, 0}, {0, 1, 0}, 90, 4, &err) == 0 && !err.empty());
        CHECK(SpinEdges(bad, {{0, 1}}, {0, 0, 0}, {0, 0, 0}, 90, 4, &err) == 0);
        CHECK(SpinEdges(bad, {{0, 1}}, {0, 0, 0}, {0, 1, 0}, 0, 4, &err) == 0);
        CHECK(SpinEdges(bad, {{0, 1}}, {0, 0, 0}, {0, 1, 0}, std::nanf(""), 4, &err) == 0);
        CHECK(SpinEdges(bad, {{0, 999}, {-1, 0}}, {0, 0, 0}, {0, 1, 0}, 90, 4, &err) == 0);
        CHECK(SpinEdges(bad, {{0, 1}}, {0.5f, 0, 0}, {0, 1, 0}, 90, 4, &err) == 0 && bad.faces.size() == nf);   // the edge lies on the axis
    }
    RUN_SUITE("extrude along a path");
    {
        std::string err;
        auto topFace = [](const PolyMesh& pm) { for (int f = 0; f < (int)pm.faces.size(); ++f) if (pm.FaceNormal(f).y > 0.9f) return f; return -1; };
        PolyMesh cube = PolyMesh::FromMesh(Mesh::Cube());
        cube.faces[topFace(cube)].material = 2;
        const int top = topFace(cube);
        CHECK(top >= 0);
        // Straight up in two steps: 6 faces + 2 x 4 walls.
        PolyMesh a = cube;
        const float topY = a.verts[a.faces[top].verts[0]].y;
        CHECK(ExtrudeAlongPath(a, {top}, {{0, topY, 0}, {0, topY + 1, 0}, {0, topY + 2, 0}}, &err) == 9);
        CHECK(a.faces.size() == 14 && a.Check().Closed() && a.Check().Valid() && a.EulerCharacteristic() == 2);
        float maxY = -9; for (const Vec3& v : a.verts) maxY = std::max(maxY, v.y);
        CHECK_NEAR(maxY, topY + 2, 1e-4);
        int slot2 = 0; for (const auto& f : a.faces) if (f.material == 2) ++slot2;
        CHECK(slot2 == 1 + 8);                                   // the cap and the walls keep the slot
        // A bent path (up, then sideways) stays a closed, valid solid and the end cap turns with it.
        PolyMesh b = cube;
        CHECK(ExtrudeAlongPath(b, {top}, {{0, topY, 0}, {0, topY + 1, 0}, {1, topY + 2, 0}, {2, topY + 2, 0}}, &err) > 0);
        CHECK(b.Check().Closed() && b.Check().Valid());
        float maxX = -9; for (const Vec3& v : b.verts) maxX = std::max(maxX, v.x);
        CHECK(maxX > 1.9f);
        // Against the face's own direction (into the cube): still closed and valid.
        PolyMesh c = cube;
        CHECK(ExtrudeAlongPath(c, {top}, {{0, topY, 0}, {0, topY - 0.3f, 0}}, &err) > 0 && c.Check().Closed());
        // Two neighbouring faces share their edge, which makes no wall.
        PolyMesh d = cube; int second = -1;
        for (int f = 0; f < (int)d.faces.size(); ++f) if (f != top && d.FaceNormal(f).z > 0.9f) second = f;
        CHECK(second >= 0);
        const size_t before = d.faces.size();
        CHECK(ExtrudeAlongPath(d, {top, second}, {{0, 0, 0}, {0, 1, 0}}, &err) == 6 + 2);   // 6 wall edges + 2 caps
        CHECK(d.faces.size() == before + 6);
        // Bad input.
        PolyMesh e = cube;
        CHECK(ExtrudeAlongPath(e, {top}, {{0, 0, 0}}, &err) == 0 && !err.empty());
        CHECK(ExtrudeAlongPath(e, {}, {{0, 0, 0}, {0, 1, 0}}, &err) == 0);
        CHECK(ExtrudeAlongPath(e, {-3, 99}, {{0, 0, 0}, {0, 1, 0}}, &err) == 0 && e.faces.size() == 6);
    }
    RUN_SUITE("array along a curve (modifier)");
    {
        std::vector<Vec3> pos, dir;
        SamplePath({{0, 0, 0}, {2, 0, 0}, {4, 0, 0}}, 5, pos, dir);
        CHECK(pos.size() == 5 && dir.size() == 5);
        for (int i = 0; i < 5; ++i) { CHECK_NEAR(pos[i].x, (float)i, 1e-4); CHECK_NEAR(dir[i].x, 1.0f, 1e-4); }
        SamplePath({{0, 0, 0}, {0, 0, 0}, {0, 3, 0}}, 2, pos, dir);                  // a zero-length first segment
        CHECK_NEAR(pos[1].y, 3.0f, 1e-4); CHECK_NEAR(dir[0].y, 1.0f, 1e-4);
        SamplePath({{1, 2, 3}}, 3, pos, dir);
        CHECK(pos.size() == 3 && pos[2].x == 1 && dir[0].x == 1);
        SamplePath({{0, 0, 0}, {1, 0, 0}}, 1, pos, dir);
        CHECK(pos.size() == 1 && pos[0].x == 0);
        MeshModifier m = MeshModifier::Make(MeshModifier::Type::CurveArray);
        CHECK(m.path.size() >= 2 && m.count == 8);
        m.path = {{0, 0, 0}, {0, 4, 0}};
        m.count = 5; m.align = false;
        Mesh in = Mesh::Cube(0.5f), out; std::string err;
        CHECK(ApplyModifier(in, m, ModifierContext(), out, &err));
        CHECK(out.TriangleCount() == 5 * in.TriangleCount());
        Vec3 lo, hi; out.Bounds(lo, hi);
        CHECK_NEAR(lo.y, -0.25f, 1e-4); CHECK_NEAR(hi.y, 4.25f, 1e-4); CHECK_NEAR(hi.x, 0.25f, 1e-4);   // not turned
        m.align = true;                                                                // +X now follows +Y: copies stand up
        Mesh long1 = Mesh::Cube(1.0f); long1.vertices[0].x -= 0.5f;                    // make it uneven so a turn shows
        Mesh turned;
        CHECK(ApplyModifier(long1, m, ModifierContext(), turned, &err));
        CHECK(turned.TriangleCount() == 5 * long1.TriangleCount());
        // Slots and per-face colours carry to every copy.
        Mesh sl = Mesh::Cube(); sl.AssignSlot({0, 1}, 1); sl.slotColors[1] = Color{1, 0, 0, 1}; sl.ApplySlotColors();
        Mesh slOut; CHECK(ApplyModifier(sl, m, ModifierContext(), slOut, &err));
        CHECK(slOut.HasSlots() && slOut.TrianglesInSlot(1).size() == 5 * 2 && slOut.HasFaceColors());
        // Too few curve points says so and leaves the mesh as it was.
        m.path = {{0, 0, 0}};
        Mesh unchanged;
        CHECK(!ApplyModifier(in, m, ModifierContext(), unchanged, &err) && !err.empty() && unchanged.TriangleCount() == in.TriangleCount());
        // Cost per modifier: one entry each, zero for one that is switched off.
        std::vector<MeshModifier> stack = {MeshModifier::Make(MeshModifier::Type::Array), MeshModifier::Make(MeshModifier::Type::Displace)};
        stack[1].enabled = false;
        std::vector<float> cost; std::vector<std::string> errs;
        EvaluateModifiers(Mesh::Cube(), stack, ModifierContext(), &errs, &cost);
        CHECK(cost.size() == 2 && cost[0] >= 0.0f && cost[1] == 0.0f);
        // A changed curve changes the stack's identity (so it is rebuilt).
        std::vector<MeshModifier> a = {MeshModifier::Make(MeshModifier::Type::CurveArray)}, b = a;
        CHECK(SameModifiers(a, b));
        b[0].path[1].y += 0.5f;
        CHECK(!SameModifiers(a, b));
        CHECK(ModifierInputHash(Mesh::Cube(), a, ModifierContext()) != ModifierInputHash(Mesh::Cube(), b, ModifierContext()));
        b = a; b[0].align = false;
        CHECK(!SameModifiers(a, b));
    }
    RUN_SUITE("measure");
    {
        const Mesh cube = Mesh::Cube();
        Measurement w = MeasureMesh(cube);
        CHECK_NEAR(w.size.x, 1.0f, 1e-5); CHECK_NEAR(w.area, 6.0f, 1e-4); CHECK_NEAR(w.volume, 1.0f, 1e-4);
        CHECK_NEAR(w.diagonal, std::sqrt(3.0f), 1e-4);
        Measurement sc = MeasureMesh(cube, Vec3{2, 1, 3});                    // read in world units through the object's scale
        CHECK_NEAR(sc.size.x, 2.0f, 1e-5); CHECK_NEAR(sc.size.z, 3.0f, 1e-5); CHECK_NEAR(sc.volume, 6.0f, 1e-3);
        CHECK(MeasureMesh(Mesh{}).verts == 0 && MeasureMesh(Mesh{}).area == 0.0f);
        // Two vertices: the distance and the difference.
        const Vec3 d = cube.vertices[3] - cube.vertices[0];
        Measurement two = MeasureSelection(cube, {0, 3}, {}, {});
        CHECK(two.hasDistance && two.verts == 2);
        CHECK_NEAR(two.distance, d.Magnitude(), 1e-5); CHECK_NEAR(two.delta.y, d.y, 1e-5);
        CHECK(two.Summary().find("Distance") == 0);
        CHECK(!MeasureSelection(cube, {0, 0}, {}, {}).hasDistance);            // the same vertex twice is one vertex
        CHECK(!MeasureSelection(cube, {0, 1, 2}, {}, {}).hasDistance);
        // Edges add up (a repeated edge counts once, in either order); faces add their area.
        const float e01 = (cube.vertices[1] - cube.vertices[0]).Magnitude(), e12 = (cube.vertices[2] - cube.vertices[1]).Magnitude();
        Measurement ed = MeasureSelection(cube, {}, {}, {{0, 1}, {1, 0}, {1, 2}});
        CHECK(ed.edges == 2 && !ed.hasDistance); CHECK_NEAR(ed.edgeLength, e01 + e12, 1e-5);
        Measurement fc = MeasureSelection(cube, {}, {0, 0, 1}, {});
        CHECK(fc.faces == 2); CHECK_NEAR(fc.area, 1.0f, 1e-4);                 // two triangles of a unit square
        // Nothing, or nonsense, selected measures nothing and never crashes.
        Measurement none = MeasureSelection(cube, {}, {}, {});
        CHECK(none.verts == 0 && none.edges == 0 && none.faces == 0 && none.size.x == 0.0f);
        Measurement junk = MeasureSelection(cube, {-1, 999}, {-5, 999}, {{-1, 2}, {3, 3}, {0, 99}});
        CHECK(junk.verts == 0 && junk.edges == 0 && junk.faces == 0 && junk.edgeLength == 0.0f);
        CHECK(!w.Summary().empty() && !fc.Summary().empty() && !ed.Summary().empty() && !none.Summary().empty());
    }
    RUN_SUITE("snap to vertex, edge and face");
    {
        const Mesh cube = Mesh::Cube();                                        // corners at +-0.5
        // Vertex: the nearest corner within the radius.
        SnapHit v = SnapToGeometry(cube, Vec3{0.55f, 0.52f, 0.48f}, SnapTarget::Vertex, 0.2f);
        CHECK(v.found); CHECK_NEAR(v.point.x, 0.5f, 1e-5); CHECK_NEAR(v.point.y, 0.5f, 1e-5); CHECK_NEAR(v.point.z, 0.5f, 1e-5);
        CHECK(!SnapToGeometry(cube, Vec3{0.8f, 0.8f, 0.8f}, SnapTarget::Vertex, 0.1f).found);   // too far
        // The corner being moved is left out, so it snaps to another one (or nothing).
        SnapHit self = SnapToGeometry(cube, Vec3{0.5f, 0.5f, 0.5f}, SnapTarget::Vertex, 0.2f, {v.a});
        CHECK(!self.found || self.a != v.a);
        // Edge: a point just off the middle of the top front edge lands on the edge.
        SnapHit e = SnapToGeometry(cube, Vec3{0.0f, 0.55f, 0.5f}, SnapTarget::Edge, 0.2f);
        CHECK(e.found); CHECK_NEAR(e.point.x, 0.0f, 1e-4); CHECK_NEAR(e.point.y, 0.5f, 1e-4); CHECK_NEAR(e.point.z, 0.5f, 1e-4);
        CHECK(e.a >= 0 && e.b > e.a);
        // Face: straight above the middle of the top face lands on the face.
        SnapHit f = SnapToGeometry(cube, Vec3{0.1f, 0.62f, -0.1f}, SnapTarget::Face, 0.2f);
        CHECK(f.found); CHECK_NEAR(f.point.x, 0.1f, 1e-4); CHECK_NEAR(f.point.y, 0.5f, 1e-4); CHECK_NEAR(f.point.z, -0.1f, 1e-4);
        CHECK_NEAR(f.distance, 0.12f, 1e-4);
        // Skipping every corner leaves no edge or face to snap to.
        std::vector<int> all; for (int i = 0; i < (int)cube.vertices.size(); ++i) all.push_back(i);
        CHECK(!SnapToGeometry(cube, Vec3{0, 0.55f, 0.5f}, SnapTarget::Edge, 0.2f, all).found);
        CHECK(!SnapToGeometry(cube, Vec3{0.1f, 0.6f, 0}, SnapTarget::Face, 0.2f, all).found);
        // Grid is not geometry; a bad radius or position finds nothing.
        CHECK(!SnapToGeometry(cube, Vec3{0.5f, 0.5f, 0.5f}, SnapTarget::Grid, 1.0f).found);
        CHECK(!SnapToGeometry(cube, Vec3{0.5f, 0.5f, 0.5f}, SnapTarget::Vertex, 0.0f).found);
        CHECK(!SnapToGeometry(cube, Vec3{std::nanf(""), 0, 0}, SnapTarget::Vertex, 1.0f).found);
        CHECK(!SnapToGeometry(Mesh{}, Vec3{0, 0, 0}, SnapTarget::Face, 1.0f).found);
        // Closest-point helpers on their own.
        const Vec3 c = ClosestOnSegment(Vec3{2, 1, 0}, Vec3{0, 0, 0}, Vec3{1, 0, 0});
        CHECK_NEAR(c.x, 1.0f, 1e-6); CHECK_NEAR(c.y, 0.0f, 1e-6);                  // past the end: the end
        const Vec3 t = ClosestOnTriangle(Vec3{5, 5, 1}, Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{0, 1, 0});
        CHECK_NEAR(t.x + t.y, 1.0f, 1e-5); CHECK_NEAR(t.z, 0.0f, 1e-6);            // outside: on the far edge
    }
    RUN_SUITE("material library");
    {
        const auto& lib = MaterialLibrary();
        CHECK(lib.size() >= 30);
        std::set<std::string> names, groups;
        for (const auto& m : lib) {
            CHECK(names.insert(m.name).second);                                    // names are unique
            groups.insert(m.group);
            CHECK(m.color.r >= 0 && m.color.r <= 1 && m.color.g >= 0 && m.color.g <= 1 && m.color.b >= 0 && m.color.b <= 1 && m.color.a == 1.0f);
        }
        CHECK(groups.size() >= 6);
        CHECK(FindLibraryMaterial("Brick") && FindLibraryMaterial("Brick")->color.r > FindLibraryMaterial("Brick")->color.b);
        CHECK(FindLibraryMaterial("Nope") == nullptr && FindLibraryMaterial("") == nullptr);
        CHECK(SlotPalettes().size() >= 6);
        for (const auto& p : SlotPalettes()) CHECK(p.slots.size() >= 3 && p.slots.size() <= 8 && std::string(p.name).size() > 0);
        // A palette can be handed to a mesh as its slot colours.
        Mesh m = Mesh::Cube();
        m.AssignSlot({0}, 3);
        const auto& pal = SlotPalettes()[0];
        for (size_t i = 0; i < pal.slots.size(); ++i) m.slotColors[i] = pal.slots[i].color;
        m.ApplySlotColors();
        CHECK(m.triColors[0].r == pal.slots[3].color.r);
    }
    RUN_SUITE("faster selecting: grow, shrink, similar, by direction, by area");
    {
        const Mesh sph = Mesh::Sphere(1.0f, 12, 16);
        std::vector<int> one = {40};
        const auto g1 = GrowFaces(sph, one);
        CHECK(g1.size() > 1 && std::find(g1.begin(), g1.end(), 40) != g1.end());
        const auto g2 = GrowFaces(sph, g1);
        CHECK(g2.size() > g1.size());                                           // keeps growing
        const auto back = ShrinkFaces(sph, g1);
        CHECK(!back.empty() && back.size() < g1.size());                        // shrinking peels the rim
        for (int t : back) CHECK(std::find(g1.begin(), g1.end(), t) != g1.end());
        CHECK(ShrinkFaces(sph, one).empty());                                    // a lone face has only a rim
        std::vector<int> all; for (int t = 0; t < sph.TriangleCount(); ++t) all.push_back(t);
        CHECK(GrowFaces(sph, all).size() == all.size() && ShrinkFaces(sph, all).size() == all.size());   // a closed mesh has no rim
        CHECK(GrowFaces(sph, {}).empty() && GrowFaces(sph, {-1, 99999}).empty() && ShrinkFaces(sph, {-4}).empty());
        // Similar and by-direction on a cube: 12 triangles, 2 per side.
        const Mesh cube = Mesh::Cube();
        const auto up = SelectByNormal(cube, Vec3{0, 1, 0}, 10.0f);
        CHECK(up.size() == 2);
        CHECK(SelectByNormal(cube, Vec3{0, 1, 0}, 100.0f).size() == 2 + 8);    // 90 degrees away counts inside 100
        CHECK(SelectByNormal(cube, Vec3{0, 0, 0}, 10.0f).empty() && SelectByNormal(cube, Vec3{std::nanf(""), 1, 0}, 10.0f).empty());
        CHECK(SelectSimilar(cube, {up[0]}, SimilarKind::Normal, 5.0f) == up);
        CHECK(SelectSimilar(cube, {up[0]}, SimilarKind::Coplanar, 0.01f) == up);
        CHECK(SelectSimilar(cube, {up[0]}, SimilarKind::Area, 0.1f).size() == 12);   // every cube triangle has the same area
        CHECK(SelectSimilar(cube, {}, SimilarKind::Normal, 5.0f).empty());
        Mesh sl = Mesh::Cube(); sl.AssignSlot({0, 1, 2}, 1);
        CHECK(SelectSimilar(sl, {1}, SimilarKind::Slot, 0.0f).size() == 3);
        CHECK(SelectSimilar(sl, {5}, SimilarKind::Slot, 0.0f).size() == 9);
        // Coplanar only picks faces on the same plane: a stepped mesh has two levels facing up.
        Mesh step = Mesh::Cube(); step.Add(Mesh::Cube(), {2, 0.5f, 0});             // a second cube whose top is higher
        const auto top0 = SelectByNormal(step, Vec3{0, 1, 0}, 5.0f);
        CHECK(top0.size() == 4);
        CHECK(SelectSimilar(step, {top0[0]}, SimilarKind::Coplanar, 0.01f).size() == 2);
        CHECK(SelectSimilar(step, {top0[0]}, SimilarKind::Normal, 5.0f).size() == 4);
        CHECK(SelectByArea(cube, 0.4f, 0.6f).size() == 12 && SelectByArea(cube, 0.6f, -1.0f).empty() && SelectByArea(cube, 0.0f, 0.1f).empty());
    }
    RUN_SUITE("apply scale and rotation");
    {
        Mesh m = Mesh::Cube();
        m.vertices[0] = Vec3{-0.5f, -0.5f, -0.5f};
        CHECK(BakeTransform(m, Vec3{2, 3, 4}, Quat::Identity));
        Vec3 lo, hi; m.Bounds(lo, hi);
        CHECK_NEAR(hi.x - lo.x, 2.0f, 1e-4); CHECK_NEAR(hi.y - lo.y, 3.0f, 1e-4); CHECK_NEAR(hi.z - lo.z, 4.0f, 1e-4);
        Mesh r = Mesh::Cube(1.0f);
        CHECK(BakeTransform(r, Vec3{1, 1, 1}, Quat::AngleAxis(90.0f, Vec3{0, 0, 1})));
        r.Bounds(lo, hi);
        CHECK_NEAR(hi.x - lo.x, 1.0f, 1e-4);                                      // still a unit cube, turned
        // A mirror (one negative axis) flips the winding so the volume stays positive.
        Mesh f = Mesh::Cube();
        CHECK(BakeTransform(f, Vec3{-1, 1, 1}, Quat::Identity));
        CHECK(MeasureMesh(f).volume > 0.9f);
        // Normals follow: a non-uniform scale on a smooth sphere keeps unit normals pointing out.
        Mesh sp = Mesh::Sphere(1.0f, 8, 12); sp.ComputeSmoothNormals();
        CHECK(BakeTransform(sp, Vec3{3, 1, 1}, Quat::Identity));
        bool unit = true; for (const Vec3& n : sp.normals) if (std::fabs(n.Magnitude() - 1.0f) > 1e-3f) unit = false;
        CHECK(unit);
        Mesh bad = Mesh::Cube();
        CHECK(!BakeTransform(bad, Vec3{0, 1, 1}, Quat::Identity) && !BakeTransform(bad, Vec3{std::nanf(""), 1, 1}, Quat::Identity));
        CHECK(MeasureMesh(bad).volume > 0.9f);                                    // untouched on refusal
    }
    RUN_SUITE("how-do-I recipes");
    {
        Mesh wall = Mesh::Cube();
        const auto front = SelectByNormal(wall, Vec3{0, 0, 1}, 5.0f);
        CHECK(front.size() == 2);
        const int before = wall.TriangleCount();
        std::string err;
        CHECK(MakeWindowHole(wall, front, 0.3f, &err));
        CHECK(wall.TriangleCount() > before - 2);                                 // a frame is left around the opening
        CHECK(!wall.Topology().Closed());                                         // and the opening is a real hole
        CHECK(wall.Topology().boundaryEdges >= 4);
        Mesh none = Mesh::Cube();
        CHECK(!MakeWindowHole(none, {}, 0.3f, &err) && !err.empty() && none.TriangleCount() == 12);
        Mesh e = Mesh::Cube();
        PolyMesh pm = PolyMesh::FromMesh(e);
        std::vector<std::pair<int, int>> edges;
        for (const auto& ed : pm.Edges()) edges.push_back({ed.a, ed.b});
        CHECK(RoundEdges(e, edges, 0.1f, 3, &err) > 0);
        CHECK(e.TriangleCount() > 12 && e.Topology().Closed());
        const Measurement rm = MeasureMesh(e);
        CHECK(rm.volume > 0.7f && rm.volume < 1.0f);                              // rounding takes a little off the corners
        Mesh e2 = Mesh::Cube();
        CHECK(RoundEdges(e2, {}, 0.1f, 3, &err) == 0 && !err.empty() && e2.TriangleCount() == 12);
        CHECK(RoundEdges(e2, {{0, 1}}, std::nanf(""), 3, &err) == 0);
        const Mesh table = MakeTable();
        CHECK(table.TriangleCount() == 60 && table.HasSlots() && table.TrianglesInSlot(0).size() == 12 && table.TrianglesInSlot(1).size() == 48);
        Vec3 tlo, thi; table.Bounds(tlo, thi);
        CHECK_NEAR(thi.y, 0.75f, 1e-4); CHECK_NEAR(tlo.y, 0.0f, 1e-4); CHECK_NEAR(thi.x - tlo.x, 1.6f, 1e-4);
        const Mesh huge = MakeTable(1e9f, -5.0f, std::nanf(""), 0.0f, 0.0f);       // sizes are clamped to something sound
        Vec3 hlo, hhi; huge.Bounds(hlo, hhi);
        CHECK(std::isfinite(hhi.x + hhi.y + hhi.z) && hhi.x - hlo.x <= 20.01f);
    }
    RUN_SUITE("hide, reveal, isolate");
    {
        Mesh m = Mesh::Cube();
        m.AssignSlot({0, 1, 2, 3}, 1); m.slotColors[1] = Color{1, 0, 0, 1}; m.ApplySlotColors();   // two whole quads
        PolyMesh pm0 = PolyMesh::FromMesh(m);
        m = pm0.ToMesh();                                                          // a mesh with polygon ids, like edit mode has
        const int before = m.TriangleCount();
        const Measurement w0 = MeasureMesh(m);
        const int hid = m.HideTriangles({0, 1, 1, -4, 9999});
        CHECK(hid == 2 && m.TriangleCount() == before - 2 && m.HiddenTriangleCount() == 2 && m.HasHidden());
        CHECK(m.HiddenConsistent());
        CHECK(MeasureMesh(m).area < w0.area);                                       // hidden faces are not in the mesh any more
        // Hiding the same thing twice, or nothing, does nothing more.
        CHECK(m.HideTriangles({}) == 0 && m.HiddenTriangleCount() == 2);
        // Undo is a copy: a copy taken while hidden comes back hidden; one taken before comes back whole.
        Mesh whileHidden = m;
        CHECK(whileHidden.HiddenTriangleCount() == 2);
        const std::vector<int> back = m.RevealHidden();
        CHECK(back.size() == 2 && m.TriangleCount() == before && !m.HasHidden());
        CHECK_NEAR(MeasureMesh(m).area, w0.area, 1e-4); CHECK_NEAR(MeasureMesh(m).volume, w0.volume, 1e-4);
        CHECK(m.Topology().Closed());                                               // the revealed faces joined up again
        CHECK(m.vertices.size() == Mesh::Cube().vertices.size());                    // no duplicate vertices were made
        CHECK(m.HasSlots() && m.TrianglesInSlot(1).size() == 4 && m.HasFaceColors());
        CHECK(PolyMesh::FromMesh(m).CountFaces(4) == 6);                             // the quads stayed quads
        CHECK(whileHidden.RevealHidden().size() == 2 && whileHidden.TriangleCount() == before);
        // Isolate keeps only the chosen faces, Reveal gives the rest back.
        Mesh iso = Mesh::Cube();
        CHECK(iso.IsolateTriangles({4, 5}) == iso.HiddenTriangleCount() && iso.TriangleCount() == 2 && iso.HiddenTriangleCount() == 10);
        CHECK(iso.RevealHidden().size() == 10 && iso.TriangleCount() == 12);
        // Edits made while faces were hidden: a moved shared vertex drags the revealed faces along; a vertex only a
        // hidden face used can be gone (renumbered away) and is rebuilt from the stored position.
        Mesh e = Mesh::Cube();
        e.HideTriangles({0, 1});
        PolyMesh pm = PolyMesh::FromMesh(e, {false, true, 1.0f});
        CompactVerts(pm);
        Mesh renumbered = pm.ToMesh();
        renumbered.hiddenPos = e.hiddenPos; renumbered.hiddenUV = e.hiddenUV; renumbered.hiddenSlot = e.hiddenSlot;
        renumbered.hiddenPoly = e.hiddenPoly; renumbered.hiddenColor = e.hiddenColor;
        const int vBefore = (int)renumbered.vertices.size();
        CHECK(renumbered.RevealHidden().size() == 2 && renumbered.TriangleCount() == 12);
        CHECK(renumbered.vertices.size() >= (size_t)vBefore && renumbered.Topology().Closed());
        // Hide everything: an empty visible mesh, reveal gives it all back. UVs survive.
        Mesh uvm = Mesh::Cube(); uvm.ProjectUVBox(1.0f);
        const Vec2 uv0 = uvm.uvs[uvm.triangles[3]];
        std::vector<int> all; for (int t = 0; t < uvm.TriangleCount(); ++t) all.push_back(t);
        CHECK(uvm.HideTriangles(all) == 12 && uvm.TriangleCount() == 0);
        CHECK(uvm.RevealHidden().size() == 12 && uvm.TriangleCount() == 12 && uvm.HasUVs());
        bool sawUV = false; for (const Vec2& u : uvm.uvs) if (std::fabs(u.x - uv0.x) < 1e-5f && std::fabs(u.y - uv0.y) < 1e-5f) sawUV = true;
        CHECK(sawUV);
        // A damaged hidden list is dropped, not trusted.
        Mesh bad = Mesh::Cube(); bad.hiddenPos = {Vec3{0, 0, 0}, Vec3{1, 0, 0}};
        CHECK(!bad.HiddenConsistent() && bad.RevealHidden().empty() && !bad.HasHidden() && bad.TriangleCount() == 12);
    }
    RUN_SUITE("box, circle and lasso select, X-ray");
    {
        // A cube seen from the front (+Z), flattened onto the screen: x, y as they are.
        const Mesh cube = Mesh::Cube();
        const Vec3 eye{0, 0, 5};
        std::vector<ScreenPoint> proj;
        for (const Vec3& v : cube.vertices) proj.push_back({v.x * 100.0f, v.y * 100.0f, true});
        const SelectShape all = SelectShape::Box({-80, -80}, {80, 80});
        // Without X-ray only the front four corners (z = +0.5) can be picked, with it all eight.
        CHECK(SelectVerticesIn(cube, proj, all, false, eye).size() == 4);
        CHECK(SelectVerticesIn(cube, proj, all, true, eye).size() == 8);
        for (int v : SelectVerticesIn(cube, proj, all, false, eye)) CHECK(cube.vertices[v].z > 0);
        // A box around one corner picks just that corner; a corner box can be dragged in any direction.
        CHECK(SelectVerticesIn(cube, proj, SelectShape::Box({60, 60}, {40, 40}), true, eye).size() == 2);   // two corners project there (front and back)
        CHECK(SelectVerticesIn(cube, proj, SelectShape::Box({60, 60}, {40, 40}), false, eye).size() == 1);
        // Circle and lasso.
        CHECK(SelectVerticesIn(cube, proj, SelectShape::Circle({50, 50}, 5.0f), false, eye).size() == 1);
        CHECK(SelectVerticesIn(cube, proj, SelectShape::Circle({0, 0}, 20.0f), true, eye).empty());
        const SelectShape tri = SelectShape::Lasso({{-100, -100}, {100, -100}, {0, 100}});               // a triangle with its base at the bottom
        const auto inTri = SelectVerticesIn(cube, proj, tri, true, eye);
        CHECK(!inTri.empty());
        for (int v : inTri) CHECK(cube.vertices[v].y <= 0.5f);
        CHECK(!SelectShape::Lasso({{0, 0}, {1, 1}}).Valid() && !SelectShape::Circle({0, 0}, 0).Valid());
        CHECK(SelectVerticesIn(cube, proj, SelectShape::Lasso({{0, 0}, {1, 1}}), true, eye).empty());
        // Faces: a whole quad at a time, only the ones facing the camera without X-ray.
        const auto faces = SelectFacesIn(cube, proj, all, false, eye);
        CHECK(faces.size() == 2);                                                                        // the front quad
        const Vec3 n = Vec3::Cross(cube.vertices[cube.triangles[faces[0] * 3 + 1]] - cube.vertices[cube.triangles[faces[0] * 3]],
                                   cube.vertices[cube.triangles[faces[0] * 3 + 2]] - cube.vertices[cube.triangles[faces[0] * 3]]);
        CHECK(n.z > 0);
        CHECK(SelectFacesIn(cube, proj, all, true, eye).size() >= 4);                                    // through the mesh: front and back
        Mesh pf = PolyMesh::FromMesh(cube).ToMesh();                                                     // with polygon ids a quad is picked whole
        std::vector<ScreenPoint> p2; for (const Vec3& v : pf.vertices) p2.push_back({v.x * 100.0f, v.y * 100.0f, true});
        const auto f2 = SelectFacesIn(pf, p2, all, false, eye);
        CHECK(f2.size() == 2 && pf.polyFace[f2[0]] == pf.polyFace[f2[1]]);
        // Edges need both ends inside.
        std::vector<std::pair<int, int>> edges;
        for (const auto& ed : PolyMesh::FromMesh(cube).Edges()) edges.push_back({ed.a, ed.b});
        CHECK(SelectEdgesIn(cube, edges, proj, all, false, eye).size() == 4);                            // the front square's four edges
        CHECK(SelectEdgesIn(cube, edges, proj, all, true, eye).size() == 12);
        CHECK(SelectEdgesIn(cube, edges, proj, SelectShape::Box({40, 40}, {60, 60}), true, eye).size() == 1);   // front and back corner project together: the edge between them
        // A vertex behind the camera (ok = false) is never picked.
        std::vector<ScreenPoint> behind = proj; for (auto& q : behind) q.ok = false;
        CHECK(SelectVerticesIn(cube, behind, all, true, eye).empty() && SelectFacesIn(cube, behind, all, true, eye).empty());
        // Wrong-sized projection lists and NaN are ignored, never a crash.
        CHECK(SelectVerticesIn(cube, {}, all, true, eye).empty());
        CHECK(SelectVerticesIn(cube, proj, SelectShape::Box({std::nanf(""), 0}, {1, 1}), true, eye).empty());
    }
    RUN_SUITE("loose parts, join, align, distribute, drop on a surface");
    {
        Mesh two = Mesh::Cube(); Mesh far = Mesh::Cube(0.5f); far = far.Transformed({1, 1, 1}, {5, 0, 0});
        two.Combine(far);
        const auto parts = LooseParts(two);
        CHECK(parts.size() == 2 && parts[0].size() == 12 && parts[1].size() == 12 && parts[0][0] == 0);
        Mesh three = two; three.Combine(Mesh::Cube(0.2f).Transformed({1, 1, 1}, {0, 5, 0}));
        CHECK(LooseParts(three).size() == 3);
        CHECK(LooseParts(Mesh{}).empty());
        // Join keeps colours, slots, UVs and polygon ids of both sides.
        Mesh a = Mesh::Cube(); a.AssignSlot({0, 1}, 1); a.slotColors[1] = Color{1, 0, 0, 1}; a.ApplySlotColors(); a.ProjectUVBox(1.0f);
        Mesh b = Mesh::Cube(); b.AssignSlot({0}, 1); b.slotColors[1] = Color{0, 0, 1, 1}; b.ApplySlotColors();
        Mesh j = a; j.Combine(b.Transformed({1, 1, 1}, {3, 0, 0}));
        CHECK(j.TriangleCount() == 24 && j.HasSlots() && j.HasFaceColors() && j.HasUVs() && j.uvs.size() == j.vertices.size());
        CHECK(j.SlotCount() == 4);                                                     // a's two slots, then b's two after them
        CHECK(j.TrianglesInSlot(1).size() == 2 && j.TrianglesInSlot(3).size() == 1);
        CHECK(j.triColors[12].b > 0.9f || j.triColors[12].r > 0.9f);                   // b's first triangle is coloured by its own slot
        Mesh plain = Mesh::Cube(), plain2 = Mesh::Cube();
        plain.Combine(plain2);
        CHECK(!plain.HasSlots() && !plain.HasFaceColors() && plain.TriangleCount() == 24);
        // Separate keeps slots.
        Mesh sep = a; Mesh piece = sep.SeparateFaces({0, 1});
        CHECK(piece.TriangleCount() == 2 && piece.HasSlots() && piece.TrianglesInSlot(1).size() == 2 && sep.TriangleCount() == 10 && sep.HasSlots());
        // Align and distribute.
        const std::vector<Range1D> r = {{0, 2}, {5, 6}, {10, 14}};
        const auto lo = AlignShifts(r, AlignTo::Min), hi = AlignShifts(r, AlignTo::Max), mid = AlignShifts(r, AlignTo::Centre);
        CHECK(lo[0] == 0 && lo[1] == -5 && lo[2] == -10);
        CHECK(hi[0] == 12 && hi[1] == 8 && hi[2] == 0);
        CHECK_NEAR(mid[2], -5.0f, 1e-5); CHECK_NEAR(mid[0], 6.0f, 1e-5);
        const auto d = DistributeShifts({{0, 1}, {1, 2}, {9, 10}});                  // equal gaps between the three
        CHECK_NEAR(d[0], 0.0f, 1e-5); CHECK_NEAR(d[2], 0.0f, 1e-5); CHECK_NEAR(d[1], 3.5f, 1e-5);
        CHECK(AlignShifts({{0, 1}}, AlignTo::Min)[0] == 0 && DistributeShifts({{0, 1}, {2, 3}})[1] == 0);
        CHECK(AlignShifts({{0, 1}, {std::nanf(""), 2}}, AlignTo::Max)[0] == 0);
        // A ray down onto the top of a cube lands on its top face; one that misses finds nothing.
        const RayHit hit = RayMeshHit(Mesh::Cube(), Vec3{0.1f, 5, 0.2f}, Vec3{0, -1, 0});
        CHECK(hit.found && std::fabs(hit.point.y - 0.5f) < 1e-4 && std::fabs(hit.t - 4.5f) < 1e-4 && hit.normal.y > 0.99f);
        CHECK(!RayMeshHit(Mesh::Cube(), Vec3{3, 5, 0}, Vec3{0, -1, 0}).found && !RayMeshHit(Mesh::Cube(), Vec3{0, 5, 0}, Vec3{0, 0, 0}).found);
        CHECK(RayMeshHit(Mesh::Cube(), Vec3{0, 0.4f, 0}, Vec3{0, 1, 0}).normal.y < 0);   // from inside: the normal faces the ray
    }
    RUN_SUITE("mirror edit on Y and Z");
    {
        // Four vertices of a symmetric square in the XZ plane at y = +-1: moving one moves its partners.
        Mesh m;
        m.vertices = {{1, 1, 1}, {-1, 1, 1}, {1, -1, 1}, {1, 1, -1}, {-1, -1, -1}, {0, 0, 0}};
        m.triangles = {0, 1, 2, 0, 3, 1, 4, 5, 2};
        auto run = [&](bool x, bool y, bool z) {
            Mesh c = m; EditTransformSession s; EditTransformOptions o; o.mirrorX = x; o.mirrorY = y; o.mirrorZ = z;
            s.Begin(c, EditXform::Move, {0}, Mat4::Identity(), o);
            s.Apply(c, Mat4::Translate(Vec3{0.5f, 0.25f, 0.125f}));
            return c;
        };
        Mesh none = run(false, false, false);
        CHECK(none.vertices[0].x == 1.5f && none.vertices[1].x == -1.0f && none.vertices[2].y == -1.0f);
        Mesh y = run(false, true, false);                                              // the vertex under it (y mirrored) follows, mirrored
        CHECK_NEAR(y.vertices[2].x, 1.5f, 1e-5); CHECK_NEAR(y.vertices[2].y, -1.25f, 1e-5); CHECK_NEAR(y.vertices[2].z, 1.125f, 1e-5);
        CHECK_NEAR(y.vertices[1].x, -1.0f, 1e-5);                                      // the X partner stays put with only Y on
        Mesh z = run(false, false, true);
        CHECK_NEAR(z.vertices[3].z, -1.125f, 1e-5); CHECK_NEAR(z.vertices[3].x, 1.5f, 1e-5);
        Mesh xyz = run(true, true, true);                                              // the opposite corner moves point-mirrored
        CHECK_NEAR(xyz.vertices[4].x, -1.5f, 1e-5); CHECK_NEAR(xyz.vertices[4].y, -1.25f, 1e-5); CHECK_NEAR(xyz.vertices[4].z, -1.125f, 1e-5);
        CHECK_NEAR(xyz.vertices[1].x, -1.5f, 1e-5); CHECK_NEAR(xyz.vertices[1].y, 1.25f, 1e-5);
        CHECK_NEAR(xyz.vertices[5].x, 0.0f, 1e-6);                                     // a vertex on every plane is its own partner: untouched
        // Cancel puts everything back.
        Mesh c = m; EditTransformSession s; EditTransformOptions o; o.mirrorY = true; o.mirrorZ = true;
        s.Begin(c, EditXform::Move, {0}, Mat4::Identity(), o); s.Apply(c, Mat4::Translate(Vec3{1, 1, 1})); s.Cancel(c);
        CHECK(c.vertices == m.vertices);
    }
    RUN_SUITE("extrude and move: a zero extrude is a sound start");
    {
        Mesh m = Mesh::Cube();
        const auto top = SelectByNormal(m, Vec3{0, 1, 0}, 5.0f);
        m.ExtrudeFaces(top, 0.0f);
        CHECK(m.TriangleCount() > 12 && m.Topology().Closed());
        CHECK(MeasureMesh(m).volume > 0.99f && MeasureMesh(m).volume < 1.01f);
        // The extruded (selected) faces can then be moved up and the solid grows by exactly that much.
        std::set<int> up; for (int t : top) for (int k = 0; k < 3; ++k) up.insert(m.triangles[t * 3 + k]);
        for (int v : up) m.vertices[v].y += 0.5f;
        CHECK_NEAR(MeasureMesh(m).volume, 1.5f, 1e-3);
        CHECK(m.Topology().Closed());
    }
    RUN_SUITE("vertex colours");
    {
        Mesh m = Mesh::Sphere(1.0f, 12, 16);
        CHECK(!m.HasVertexColors());
        // A brush: full colour at the centre, nothing outside the radius, a smooth falloff between.
        const Color red{1, 0, 0, 1};
        int top = 0; for (int i = 0; i < (int)m.vertices.size(); ++i) if (m.vertices[i].y > m.vertices[top].y) top = i;
        const int n = m.PaintVertices(m.vertices[top], 0.6f, red, 1.0f);
        CHECK(n > 1 && m.HasVertexColors() && m.vertexColors.size() == m.vertices.size());
        CHECK(m.vertexColors[top].r == 1 && m.vertexColors[top].g == 0 && m.vertexColors[top].b == 0);
        int bottom = 0; for (int i = 0; i < (int)m.vertices.size(); ++i) if (m.vertices[i].y < m.vertices[bottom].y) bottom = i;
        CHECK(m.vertexColors[bottom].g == 1 && m.vertexColors[bottom].r == 1);                       // untouched: white
        bool mid = false;
        for (int i = 0; i < (int)m.vertices.size(); ++i) {
            const float d = (m.vertices[i] - m.vertices[top]).Magnitude();
            if (d > 0.1f && d < 0.55f) { CHECK(m.vertexColors[i].g > 0.0f && m.vertexColors[i].g < 1.0f); mid = true; }   // in between
            if (d >= 0.6f) CHECK(m.vertexColors[i].g == 1.0f);
        }
        CHECK(mid);
        // Half strength blends halfway; a second stroke goes further. Bad input paints nothing.
        Mesh h = Mesh::Cube(); h.PaintVertices(h.vertices[0], 5.0f, Color{0, 0, 0, 1}, 0.5f, 0.99f);
        CHECK(h.vertexColors[0].r > 0.3f && h.vertexColors[0].r < 0.8f);
        Mesh bad = Mesh::Cube();
        CHECK(bad.PaintVertices({0, 0, 0}, 0.0f, red, 1) == 0 && bad.PaintVertices({0, 0, 0}, -1, red, 1) == 0 && bad.PaintVertices({0, 0, 0}, 1, red, 0) == 0 &&
              bad.PaintVertices({std::nanf(""), 0, 0}, 1, red, 1) == 0 && !bad.HasVertexColors());
        CHECK(Mesh{}.PaintVertices({0, 0, 0}, 1, red, 1) == 0);
        // Fill, smooth, sync.
        Mesh f = Mesh::Cube();
        CHECK(f.FillVertexColors({0, 1, 99, -2}, Color{0, 1, 0, 1}) == 2 && f.vertexColors[0].g == 1 && f.vertexColors[0].r == 0 && f.vertexColors[5].r == 1);
        f.SmoothVertexColors(3);
        CHECK(f.vertexColors[0].r > 0.0f && f.vertexColors[0].r < 1.0f);                             // the green vertex lost some of its strength
        Mesh sy = Mesh::Cube(); sy.FillVertexColors({0}, red);
        sy.vertices.push_back(Vec3{9, 9, 9});                                                        // a tool added a vertex
        CHECK(!sy.HasVertexColors() && sy.SyncVertexColors() && sy.HasVertexColors() && sy.vertexColors.back().g == 1 && sy.vertexColors[0].g == 0);
        sy.vertices.pop_back(); sy.vertices.pop_back();                                              // vertices were removed: cannot match, dropped
        CHECK(sy.SyncVertexColors() && sy.vertexColors.empty() && !sy.SyncVertexColors());
        // They follow their vertices through Weld, Combine and SeparateFaces.
        Mesh w = Mesh::Cube(); w.FillVertexColors({0}, red);
        const Vec3 p0 = w.vertices[0];
        w.vertices.push_back(p0); w.vertexColors.push_back(Color{1, 1, 1, 1}); w.triangles.insert(w.triangles.end(), {8, 1, 2});
        w.WeldVertices();
        CHECK(w.HasVertexColors() && w.vertexColors.size() == w.vertices.size() && w.vertexColors[0].g == 0);
        Mesh c1 = Mesh::Cube(); c1.FillVertexColors({2}, red); Mesh c2 = Mesh::Cube();
        c2.Combine(c1);
        CHECK(c2.HasVertexColors() && c2.vertexColors.size() == 16 && c2.vertexColors[0].g == 1 && c2.vertexColors[8 + 2].g == 0);
        Mesh sp = c1; Mesh piece = sp.SeparateFaces({0, 1});
        CHECK(piece.HasVertexColors() && piece.vertexColors.size() == piece.vertices.size());
        // Hide / reveal rebuilds vertices: still one colour per vertex.
        Mesh hr = Mesh::Cube(); hr.FillVertexColors({0}, red); hr.HideTriangles({0, 1}); hr.RevealHidden();
        CHECK(hr.vertexColors.size() == hr.vertices.size());
        // Through the polygon mesh and the .okaymesh file; white vertices are not written; old files load with none.
        Mesh pcm = Mesh::Cube(); pcm.FillVertexColors({0, 6}, Color{0.25f, 0.5f, 0.75f, 1});
        PolyMesh pm = PolyMesh::FromMesh(pcm);
        CHECK(pm.vertColors.size() == pm.verts.size());
        const Mesh back = pm.ToMesh();
        CHECK(back.HasVertexColors());
        int tinted = 0; for (const Color& c : back.vertexColors) if (c.r == 0.25f && c.g == 0.5f && c.b == 0.75f) ++tinted;
        CHECK(tinted == 2);
        const std::string text = pm.Serialize();
        int lines = 0; for (std::size_t at = 0; (at = text.find("vcolor ", at)) != std::string::npos; ++at) ++lines;
        CHECK(lines == 2);
        PolyMesh parsed; std::string err;
        CHECK(PolyMesh::Parse(text, parsed, &err) && parsed.ToMesh().HasVertexColors());
        PolyMesh plain = PolyMesh::FromMesh(Mesh::Cube());
        CHECK(plain.Serialize().find("vcolor") == std::string::npos && !plain.ToMesh().HasVertexColors());
        CHECK(!PolyMesh::Parse("okaymesh 1\nverts 0\nfaces 0\nvcolor 3 1 1 1 1\n", parsed, &err) && err.find("vcolor") != std::string::npos);
        CHECK(!PolyMesh::Parse("okaymesh 1\nverts 0\nfaces 0\nvcolor 0 1 1\n", parsed, &err));
        // Subdividing keeps the look: the new points between a red and a white vertex are in between, and the cube stays one colour per vertex.
        PolyMesh cube = PolyMesh::FromMesh(Mesh::Cube());
        cube.vertColors.assign(cube.verts.size(), Color{1, 1, 1, 1}); cube.vertColors[0] = red;
        const PolyMesh cc = CatmullClark(cube, 1);
        CHECK(cc.vertColors.size() == cc.verts.size());
        bool pink = false; for (const Color& c : cc.vertColors) if (c.r == 1 && c.g > 0.3f && c.g < 0.9f) pink = true;
        CHECK(pink);
        // Splitting an edge blends; compacting unused vertices keeps each colour with its vertex.
        PolyMesh se = PolyMesh::FromMesh(Mesh::Cube());
        se.vertColors.assign(se.verts.size(), Color{0, 0, 0, 1}); se.vertColors[se.Edges()[0].b] = Color{1, 1, 1, 1};
        const auto e0 = se.Edges()[0];
        const int nv0 = SplitEdge(se, e0.a, e0.b, 0.5f);
        CHECK(nv0 >= 0 && std::fabs(se.vertColors[nv0].r - 0.5f) < 1e-5f);
        PolyMesh un = PolyMesh::FromMesh(Mesh::Cube());
        un.vertColors.assign(un.verts.size(), Color{0, 0, 0, 1});
        un.vertColors[7] = red; un.verts.push_back(Vec3{5, 5, 5}); un.vertColors.push_back(Color{0, 1, 0, 1});
        const std::vector<int> remap = CompactVerts(un);
        CHECK(un.vertColors.size() == un.verts.size() && un.vertColors[remap[7]].r == 1);
        // Array and the modifier stack keep them too.
        Mesh arr = Mesh::Cube(); arr.FillVertexColors({0}, red);
        MeshModifier mod = MeshModifier::Make(MeshModifier::Type::Array); mod.count = 3; mod.relative = false; mod.offset = Vec3{3, 0, 0};
        Mesh arrOut; std::string merr;
        CHECK(ApplyModifier(arr, mod, ModifierContext(), arrOut, &merr) && arrOut.HasVertexColors() && arrOut.vertexColors.size() == arrOut.vertices.size());
        // A stack's identity changes when only a colour changes (so it is rebuilt).
        Mesh k1 = Mesh::Cube(), k2 = Mesh::Cube(); k1.FillVertexColors({0}, red); k2.FillVertexColors({0}, Color{0, 0, 1, 1});
        CHECK(ModifierInputHash(k1, {}, ModifierContext()) != ModifierInputHash(k2, {}, ModifierContext()));
    }
    RUN_SUITE("shrinkwrap and snap to surface");
    {
        const Mesh cube = Mesh::Cube();                                                  // faces at +-0.5
        auto onCube = [](const Vec3& p) { return std::fabs(std::max(std::fabs(p.x), std::max(std::fabs(p.y), std::fabs(p.z))) - 0.5f) < 1e-4f; };
        Mesh sph = Mesh::Sphere(1.2f, 10, 14);
        const WrapReport r = Shrinkwrap(sph, {}, cube, WrapMode::Nearest);
        CHECK(r.moved == (int)sph.vertices.size() && r.missed == 0 && r.maxMove > 0.3f);
        bool all = true; for (const Vec3& v : sph.vertices) if (!onCube(v)) all = false;
        CHECK(all);                                                                      // every vertex now lies on the cube's surface
        // An offset pushes the result away from the surface (along the triangle normal).
        Mesh off = Mesh::Sphere(1.2f, 10, 14);
        Shrinkwrap(off, {}, cube, WrapMode::Nearest, 0.1f);
        float far = 0; for (const Vec3& v : off.vertices) far = std::max(far, std::max(std::fabs(v.x), std::max(std::fabs(v.y), std::fabs(v.z))));
        CHECK(far > 0.59f && far < 0.61f);
        // Only the chosen vertices move; a repeated or bad number is harmless.
        Mesh part = Mesh::Sphere(1.2f, 10, 14); const Mesh before = part;
        const WrapReport rp = Shrinkwrap(part, {3, 3, -1, 99999, 5}, cube, WrapMode::Nearest);
        CHECK(rp.moved == 2 && part.vertices[3] != before.vertices[3] && part.vertices[5] != before.vertices[5] && part.vertices[4] == before.vertices[4]);
        // maxDistance leaves far vertices alone (and counts them as missed).
        Mesh near = Mesh::Sphere(3.0f, 8, 10);
        const WrapReport rn = Shrinkwrap(near, {}, cube, WrapMode::Nearest, 0.0f, 0.5f);
        CHECK(rn.moved == 0 && rn.missed == (int)near.vertices.size());
        // Project: a flat sheet above the cube falls onto its top where it is over the cube, and misses elsewhere.
        Mesh sheet = Mesh::Plane(4.0f);
        for (Vec3& v : sheet.vertices) v.y += 2.0f;
        const WrapReport rj = Shrinkwrap(sheet, {}, cube, WrapMode::Project, 0.0f, 0.0f, Vec3{0, -1, 0});
        bool top = false, same = false;
        for (const Vec3& v : sheet.vertices) { if (std::fabs(v.y - 0.5f) < 1e-4f) top = true; if (std::fabs(v.y - 2.0f) < 1e-4f) same = true; }
        CHECK(rj.moved + rj.missed == (int)sheet.vertices.size() && (top || rj.moved == 0) && (same || rj.missed == 0));
        // Nothing to wrap onto, NaN, or an empty mesh does nothing.
        Mesh e = Mesh::Sphere(1, 6, 6); const Mesh e0 = e;
        CHECK(Shrinkwrap(e, {}, Mesh{}, WrapMode::Nearest).moved == 0 && Shrinkwrap(e, {}, cube, WrapMode::Nearest, std::nanf("")).moved == 0 && e.vertices == e0.vertices);
        CHECK(Shrinkwrap(*new Mesh(), {}, cube, WrapMode::Nearest).moved == 0);
        // A big mesh stays fast (the grid keeps it from comparing every vertex with every triangle).
        Mesh big = Mesh::Sphere(1.3f, 100, 140); const Mesh target = Mesh::Sphere(1.0f, 100, 140);
        const auto t0 = std::chrono::steady_clock::now();
        const WrapReport rb = Shrinkwrap(big, {}, target, WrapMode::Nearest);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(rb.moved == (int)big.vertices.size() && secs < 20.0);
        for (const Vec3& v : big.vertices) { CHECK(std::fabs(v.Magnitude() - 1.0f) < 0.02f); break; }
    }
    RUN_SUITE("subdivision creases");
    {
        PolyMesh cube = PolyMesh::FromMesh(Mesh::Cube());
        auto maxAbs = [](const PolyMesh& p) { return MeasureMesh(p.ToMesh()).volume; };   // the volume: 1 for the cube, less as it rounds off
        const PolyMesh smooth = CatmullClark(cube, 2);
        CHECK(maxAbs(smooth) < 0.75f);                                                    // a plain cube rounds off and shrinks
        // Every edge creased for two levels: the cube stays a (finer) cube.
        PolyMesh sharp = cube;
        for (const auto& e : sharp.Edges()) sharp.SetCrease(e.a, e.b, 2.0f);
        const PolyMesh keep = CatmullClark(sharp, 2);
        CHECK(maxAbs(keep) > 0.99f && keep.faces.size() == smooth.faces.size() && keep.Check().Closed());
        // Sharpness is used up one level at a time: 1 level of sharpness, 2 levels of subdivision, ends up rounder than 2 / 2.
        PolyMesh one = cube;
        for (const auto& e : one.Edges()) one.SetCrease(e.a, e.b, 1.0f);
        const float m1 = maxAbs(CatmullClark(one, 2));
        CHECK(m1 > maxAbs(smooth) && m1 < maxAbs(keep));
        // The edges of one face only: that face stays flat and the rest rounds off.
        PolyMesh topOnly = cube;
        int top = -1; for (int f = 0; f < (int)topOnly.faces.size(); ++f) if (topOnly.FaceNormal(f).y > 0.9f) top = f;
        const PolyFace tf = topOnly.faces[top];
        for (std::size_t k = 0; k < tf.verts.size(); ++k) topOnly.SetCrease(tf.verts[k], tf.verts[(k + 1) % tf.verts.size()], 1.0f);
        const PolyMesh t1 = CatmullClark(topOnly, 1);
        int flat = 0; for (const Vec3& v : t1.verts) if (v.y > 0.5f - 1e-5f) ++flat;
        CHECK(flat >= 9);                                                                 // the 4 corners, 4 edge middles and the face point
        int flatPlain = 0; for (const Vec3& v : CatmullClark(cube, 1).verts) if (v.y > 0.5f - 1e-5f) ++flatPlain;
        CHECK(flatPlain <= 1);                                                            // (only the face point lies in the plane)
        // SetCrease: order does not matter, 0 clears, nonsense is ignored.
        PolyMesh c; c.SetCrease(5, 2, 1.5f);
        CHECK(c.CreaseOf(2, 5) == 1.5f && c.creases.size() == 1);
        c.SetCrease(2, 5, 3.0f); CHECK(c.CreaseOf(5, 2) == 3.0f && c.creases.size() == 1);
        c.SetCrease(2, 2, 1.0f); c.SetCrease(-1, 4, 1.0f); CHECK(c.creases.size() == 1);
        c.SetCrease(2, 5, 0.0f); CHECK(c.creases.empty() && c.CreaseOf(2, 5) == 0.0f);
        // Through Mesh <-> PolyMesh, the .okaymesh file and compacting.
        Mesh cm = Mesh::Cube(); cm.creases = {{0, 1, 2.0f}};
        const PolyMesh fromM = PolyMesh::FromMesh(cm);
        CHECK(fromM.creases.size() == 1 && fromM.creases[0].w == 2.0f);
        PolyMesh two = PolyMesh::FromMesh(Mesh::Cube()); const auto e0 = two.Edges()[0]; two.SetCrease(e0.a, e0.b, 2.5f);
        CHECK(two.ToMesh().creases.size() == 1 && two.ToMesh().creases[0].w == 2.5f);
        PolyMesh round; std::string err;
        CHECK(PolyMesh::Parse(two.Serialize(), round, &err) && round.CreaseOf(e0.a, e0.b) == 2.5f);
        CHECK(two.Serialize().find("crease ") != std::string::npos && PolyMesh::FromMesh(Mesh::Cube()).Serialize().find("crease") == std::string::npos);
        CHECK(!PolyMesh::Parse("okaymesh 1\nverts 0\nfaces 0\ncrease 4 5\n", round, &err) && !PolyMesh::Parse("okaymesh 1\nverts 0\nfaces 0\ncrease 4 5 1\n", round, &err));
        CHECK(PolyMesh::Parse("okaymesh 1\nverts 0\nfaces 0\n", round, &err) && round.creases.empty());
        PolyMesh cp = PolyMesh::FromMesh(Mesh::Cube()); cp.verts.push_back(Vec3{9, 9, 9}); cp.SetCrease(7, 6, 1.0f);
        const std::vector<int> rm = CompactVerts(cp);
        CHECK(cp.creases.size() == 1 && cp.creases[0].a == std::min(rm[6], rm[7]));
        Mesh sc = Mesh::Cube();
        sc.SetCrease(3, 1, 2.0f); sc.SetCrease(1, 3, 5.0f); sc.SetCrease(0, 0, 1.0f); sc.SetCrease(0, 2, std::nanf(""));
        CHECK(sc.creases.size() == 1 && sc.creases[0].a == 1 && sc.creases[0].b == 3 && sc.creases[0].w == 5.0f);
        sc.SetCrease(1, 3, 99.0f); CHECK(sc.creases[0].w == 10.0f);
        sc.SetCrease(3, 1, 0.0f); CHECK(sc.creases.empty());
        // The modifier stack's Subdivision keeps creased edges sharp, and a change to a crease rebuilds it.
        Mesh sub = Mesh::Cube();
        { PolyMesh pc = PolyMesh::FromMesh(sub); for (const auto& e : pc.Edges()) pc.SetCrease(e.a, e.b, 3.0f); sub = pc.ToMesh(); }
        MeshModifier mod = MeshModifier::Make(MeshModifier::Type::Subdivision); mod.levels = 2;
        Mesh out; std::string merr;
        CHECK(ApplyModifier(sub, mod, ModifierContext(), out, &merr));
        Vec3 lo, hi; out.Bounds(lo, hi);
        CHECK(hi.x > 0.499f && out.TriangleCount() > 12);
        Mesh plainSub = Mesh::Cube(), plainOut;
        CHECK(ApplyModifier(plainSub, mod, ModifierContext(), plainOut, &merr));
        plainOut.Bounds(lo, hi); CHECK(hi.x < 0.46f);
        Mesh h1 = Mesh::Cube(), h2 = Mesh::Cube(); h1.creases = {{0, 1, 1.0f}}; h2.creases = {{0, 1, 2.0f}};
        CHECK(ModifierInputHash(h1, {}, ModifierContext()) != ModifierInputHash(h2, {}, ModifierContext()));
        // Weld renumbers vertices: the crease follows.
        Mesh w = Mesh::Cube(); const Vec3 pa = w.vertices[2];
        w.vertices.push_back(pa); w.triangles.insert(w.triangles.end(), {8, 0, 1});
        w.creases = {{8, 1, 2.0f}};
        w.WeldVertices();
        CHECK(w.creases.size() == 1 && w.creases[0].a >= 0 && w.creases[0].b < (int)w.vertices.size() && w.creases[0].w == 2.0f);
    }
    RUN_SUITE("mesh library: browsing .okaymesh parts");
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "okay_mesh_library_test";
        fs::remove_all(dir); fs::create_directories(dir / "props" / "deep"); fs::create_directories(dir / "walls");
        std::string err;
        CHECK(PolyMesh::FromMesh(Mesh::Cube()).Save((dir / "Crate.okaymesh").string(), &err));
        CHECK(PolyMesh::FromMesh(Mesh::Sphere(1, 6, 8)).Save((dir / "props" / "Ball.okaymesh").string(), &err));
        CHECK(PolyMesh::FromMesh(Mesh::Cylinder(0.5f, 1.0f, 8)).Save((dir / "props" / "deep" / "Barrel.okaymesh").string(), &err));
        CHECK(PolyMesh::FromMesh(Mesh::Cube()).Save((dir / "walls" / "UPPER.OKAYMESH").string(), &err));
        { std::ofstream o(dir / "notes.txt"); o << "okaymesh 1\n"; }                                  // not a mesh file
        { std::ofstream o(dir / "Broken.okaymesh"); o << "this is not a mesh\n"; }                   // listed, but flagged unreadable
        { std::ofstream o(dir / "walls" / "Empty.okaymesh"); }
        const auto all = ListMeshAssets(dir.string());
        CHECK(all.size() == 6);
        // Sorted by folder, then name; "" (the top folder) first.
        CHECK(all[0].folder.empty() && all[1].folder.empty() && all[0].name == "Broken" && all[1].name == "Crate");
        const auto crate = std::find_if(all.begin(), all.end(), [](const MeshAssetInfo& a) { return a.name == "Crate"; });
        CHECK(crate != all.end() && crate->readable && crate->vertices == 8 && crate->faces == 6 && crate->bytes > 20);
        const auto barrel = std::find_if(all.begin(), all.end(), [](const MeshAssetInfo& a) { return a.name == "Barrel"; });
        CHECK(barrel != all.end() && barrel->folder == "props/deep" && barrel->readable && barrel->faces > 8);
        const auto brk = std::find_if(all.begin(), all.end(), [](const MeshAssetInfo& a) { return a.name == "Broken"; });
        CHECK(brk != all.end() && !brk->readable && brk->vertices == 0);
        const auto up = std::find_if(all.begin(), all.end(), [](const MeshAssetInfo& a) { return a.name == "UPPER"; });
        CHECK(up != all.end() && up->readable);                                                         // the extension is matched in any case
        // A listed file really loads.
        PolyMesh loaded; CHECK(PolyMesh::Load(crate->path, loaded, &err) && loaded.faces.size() == 6);
        // Filtering by name or folder, in any case; limits; missing and empty folders.
        CHECK(FilterMeshAssets(all, "ball").size() == 1 && FilterMeshAssets(all, "PROPS").size() == 2 && FilterMeshAssets(all, "").size() == 6 && FilterMeshAssets(all, "zzz").empty());
        CHECK(ListMeshAssets(dir.string(), 2).size() == 2);
        CHECK(ListMeshAssets(dir.string(), 500, 1).size() == 5);                                       // depth 1: the props/deep file is too deep
        CHECK(ListMeshAssets((dir / "nope").string()).empty() && ListMeshAssets("").empty() && ListMeshAssets((dir / "Crate.okaymesh").string()).empty());
        fs::remove_all(dir);
    }
    RUN_SUITE("reduce polygons (edge collapse) as the modeler uses it");
    {
        Mesh sph = Mesh::Sphere(1.0f, 40, 56);
        const int before = sph.TriangleCount();
        const float vol0 = MeasureMesh(sph).volume;
        okay::optimize::SimplifyOptions o; o.targetRatio = 0.25f;
        const okay::optimize::SimplifyResult r = okay::optimize::SimplifyMesh(sph, o);
        CHECK(r.reachedTarget && r.mesh.TriangleCount() < before * 0.3f && r.mesh.TriangleCount() > before * 0.2f);
        CHECK(r.mesh.Topology().Closed());                                                  // still a closed solid
        CHECK(std::fabs(MeasureMesh(r.mesh).volume - vol0) < vol0 * 0.08f);               // and still about the same size
        // The same quality-versus-clustering check: the old Decimate (vertex clustering) at a similar count is rougher.
        CHECK(r.error < 0.05f);
        okay::optimize::SimplifyOptions ones; ones.targetRatio = 1.0f;
        CHECK(okay::optimize::SimplifyMesh(sph, ones).mesh.TriangleCount() == before);
        Mesh tiny = Mesh::Cube();
        CHECK(okay::optimize::SimplifyMesh(tiny, o).mesh.TriangleCount() >= 4);
        CHECK(okay::optimize::SimplifyMesh(Mesh{}, o).mesh.TriangleCount() == 0);
    }
    RUN_SUITE("slot groups (what the OpenGL renderer draws one range per slot from)");
    {
        Mesh m = Mesh::Cube();
        m.AssignSlot({0, 5, 7}, 2); m.AssignSlot({1, 2}, 1);
        const Mesh::SlotGroups g = m.GroupBySlot();
        CHECK(g.order.size() == 12 && g.groups.size() == 3);
        CHECK(g.groups[0][0] == 0 && g.groups[0][2] == 7 && g.groups[1][0] == 1 && g.groups[1][2] == 2 && g.groups[2][0] == 2 && g.groups[2][2] == 3);
        CHECK(g.groups[1][1] == 7 && g.groups[2][1] == 9);                       // each group starts where the last one ended
        std::set<int> seen(g.order.begin(), g.order.end());
        CHECK(seen.size() == 12);                                                // every triangle exactly once
        CHECK(g.order[7] == 1 && g.order[8] == 2 && g.order[9] == 0 && g.order[10] == 5 && g.order[11] == 7);   // stable inside a slot
        for (const auto& gr : g.groups) for (int i = 0; i < gr[2]; ++i) CHECK(m.triSlot[g.order[gr[1] + i]] == gr[0]);
        const Mesh::SlotGroups none = Mesh::Cube().GroupBySlot();
        CHECK(none.groups.size() == 1 && none.groups[0][2] == 12 && none.order[5] == 5);
        CHECK(Mesh{}.GroupBySlot().groups.empty());
    }
    RUN_SUITE("reference image planes");
    {
        const auto f = RefPlaneCorners(RefPlane::Front, Vec3{1, 2, 3}, 4.0f, 0.5f);       // 4 wide, 2 tall
        CHECK(f[0].x == -1 && f[0].y == 3 && f[0].z == 3 && f[1].x == 3 && f[1].y == 3 && f[2].x == 3 && f[2].y == 1 && f[3].x == -1 && f[3].y == 1);
        const auto fl = RefPlaneCorners(RefPlane::Front, Vec3{0, 0, 0}, 4.0f, 0.5f, true);
        CHECK(fl[0].x == 2 && fl[1].x == -2 && fl[0].y == 1 && fl[3].y == -1);          // mirrored left to right, same height
        const auto sd = RefPlaneCorners(RefPlane::Side, Vec3{0, 0, 0}, 4.0f, 0.5f);      // the picture's left edge is toward +Z
        CHECK(sd[0].z == 2 && sd[1].z == -2 && sd[0].x == 0 && sd[0].y == 1 && sd[3].y == -1);
        const auto tp = RefPlaneCorners(RefPlane::Top, Vec3{0, 5, 0}, 4.0f, 0.5f);       // the picture's top edge is toward -Z
        CHECK(tp[0].z == -1 && tp[3].z == 1 && tp[0].x == -2 && tp[1].x == 2 && tp[0].y == 5);
        // Bad sizes give a point (nothing is drawn), never a crash or NaN.
        for (const auto& bad : {RefPlaneCorners(RefPlane::Front, Vec3{1, 1, 1}, 0.0f, 1.0f), RefPlaneCorners(RefPlane::Front, Vec3{1, 1, 1}, 2.0f, -1.0f),
                                RefPlaneCorners(RefPlane::Top, Vec3{1, 1, 1}, std::nanf(""), 1.0f), RefPlaneCorners(RefPlane::Side, Vec3{1, 1, 1}, INFINITY, 1.0f)})
            for (const Vec3& v : bad) CHECK(!std::isnan(v.y) && (v - bad[0]).Magnitude() < 1e-6f);
        // The corners go clockwise on screen from the viewing side: top-left, top-right, bottom-right, bottom-left.
        const Vec3 e1 = f[1] - f[0], e2 = f[2] - f[1];
        CHECK(Vec3::Cross(e1, e2).z < 0);                                                  // (viewed from +Z, clockwise)
    }
    RUN_SUITE("bezier curves to meshes");
    {
        const std::vector<Vec3> pts = {{0, 0, 0}, {1, 1, 0}, {2, 0, 0}, {3, 1, 0}};
        const BezierCurve c = CurveThrough(pts);
        CHECK(c.pts.size() == 4 && !c.closed && c.Spans() == 3);
        const auto s8 = SampleCurve(c, 8);
        CHECK(s8.size() == 3 * 8 + 1);
        CHECK((s8.front() - pts[0]).Magnitude() < 1e-5f && (s8.back() - pts[3]).Magnitude() < 1e-5f);
        for (int i = 0; i < 4; ++i) CHECK((s8[i * 8] - pts[i]).Magnitude() < 1e-5f);                    // it passes through every control point
        // Smooth: the direction going into a point matches the direction leaving it.
        for (int i = 1; i < 3; ++i) { const auto fine = SampleCurve(c, 64); const Vec3 din = (fine[i * 64] - fine[i * 64 - 1]).Normalized(), dout = (fine[i * 64 + 1] - fine[i * 64]).Normalized(); CHECK(Vec3::Dot(din, dout) > 0.99f); }
        // Collinear points stay a straight line of the right length; tension 0 is straight segments.
        const BezierCurve line = CurveThrough({{0, 0, 0}, {1, 0, 0}, {3, 0, 0}, {4, 0, 0}});
        CHECK_NEAR(CurveLength(line), 4.0f, 1e-3);
        for (const Vec3& q : SampleCurve(line, 6)) CHECK(std::fabs(q.y) < 1e-6f && q.x >= -1e-5f && q.x <= 4.0f + 1e-5f);
        CHECK_NEAR(CurveLength(CurveThrough({{0, 0, 0}, {3, 0, 0}, {3, 4, 0}}, false, 0.0f), 4), 7.0f, 1e-3);
        // Closed: no repeated first point, a loop.
        const BezierCurve loop = CurveThrough({{2, 0, 0}, {0, 0, 2}, {-2, 0, 0}, {0, 0, -2}}, true);
        CHECK(loop.closed && loop.Spans() == 4 && SampleCurve(loop, 5).size() == 20);
        CHECK(CurveLength(loop, 16) > 2 * 3.14159f * 2 * 0.9f && CurveLength(loop, 16) < 2 * 3.14159f * 2 * 1.05f);   // about a circle of radius 2
        // Not enough points, NaN points, and absurd sample counts are handled.
        CHECK(SampleCurve(CurveThrough({{0, 0, 0}}), 8).empty() && SampleCurve(BezierCurve{}, 8).empty());
        CHECK(CurveThrough({{0, 0, 0}, {std::nanf(""), 0, 0}, {1, 0, 0}}).pts.size() == 2);
        CHECK(SampleCurve(c, 0).size() == 3 + 1 && SampleCurve(c, 100000).size() == 3 * 256 + 1);
        // A straight pipe is a closed solid with the right volume.
        const BezierCurve straight = CurveThrough({{0, 0, 0}, {4, 0, 0}}, false, 0.0f);
        const Mesh pipe = CurvePipe(straight, 0.25f, 8, 4, true);
        CHECK(pipe.TriangleCount() > 0 && pipe.Topology().Closed());
        const float octArea = 0.5f * 8.0f * 0.25f * 0.25f * std::sin(6.28318530718f / 8.0f);
        CHECK_NEAR(MeasureMesh(pipe).volume, octArea * 4.0f, 0.01);
        CHECK(!CurvePipe(straight, 0.25f, 8, 4, false).Topology().Closed());                            // no caps: open at both ends
        PolyMesh pp = PolyMesh::FromMesh(pipe); CHECK(pp.EulerCharacteristic() == 2);
        // A pipe round a closed loop is a ring (torus: Euler 0), closed, positive volume.
        const Mesh ringPipe = CurvePipe(loop, 0.2f, 10, 6);
        PolyMesh rp = PolyMesh::FromMesh(ringPipe);
        CHECK(ringPipe.Topology().Closed() && rp.EulerCharacteristic() == 0 && MeasureMesh(ringPipe).volume > 0);
        // A ribbon: a flat strip facing up, area = width x length.
        const Mesh rib = CurveRibbon(straight, 1.0f, 4);
        CHECK_NEAR(MeasureMesh(rib).area, 4.0f, 1e-3);
        Vec3 nsum{0, 0, 0}; for (int t = 0; t < rib.TriangleCount(); ++t) { const Vec3& a = rib.vertices[rib.triangles[t * 3]]; nsum += Vec3::Cross(rib.vertices[rib.triangles[t * 3 + 1]] - a, rib.vertices[rib.triangles[t * 3 + 2]] - a); }
        CHECK(nsum.y > 0 && std::fabs(nsum.x) < 1e-4f && std::fabs(nsum.z) < 1e-4f);
        const Mesh rib2 = CurveRibbon(straight, 1.0f, 4, true);
        CHECK(rib2.TriangleCount() == rib.TriangleCount() * 2);
        { Vec3 ns{0, 0, 0}; for (int t = 0; t < rib2.TriangleCount(); ++t) { const Vec3& a = rib2.vertices[rib2.triangles[t * 3]]; ns += Vec3::Cross(rib2.vertices[rib2.triangles[t * 3 + 1]] - a, rib2.vertices[rib2.triangles[t * 3 + 2]] - a); } CHECK(ns.Magnitude() < 1e-3f); }   // up and down faces cancel
        // A square profile tapering to half size loses volume; a profile can be open (a flat strip).
        const std::vector<Vec2> sq = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}};
        const float vFull = MeasureMesh(CurveSweep(straight, sq, true, 4, 1.0f, true)).volume, vTaper = MeasureMesh(CurveSweep(straight, sq, true, 4, 0.5f, true)).volume;
        CHECK_NEAR(vFull, 4.0f, 0.01); CHECK(vTaper > 0.3f * vFull && vTaper < 0.6f * vFull);
        // Bad input gives an empty mesh and never a crash or NaN.
        CHECK(CurvePipe(straight, 0.0f).TriangleCount() == 0 && CurvePipe(straight, std::nanf("")).TriangleCount() == 0 && CurvePipe(CurveThrough({{0, 0, 0}}), 1.0f).TriangleCount() == 0);
        CHECK(CurveSweep(straight, {{0, 0}}, true).TriangleCount() == 0 && CurveSweep(straight, {{0, 0}, {std::nanf(""), 1}}, false).TriangleCount() == 0);
        CHECK(CurveRibbon(straight, -1.0f).TriangleCount() == 0 && CurveSweep(straight, sq, true, 4, std::nanf("")).TriangleCount() == 0);
        // A twisty path stays sound (the carried frame never flips or goes NaN).
        std::vector<Vec3> wild; for (int i = 0; i < 40; ++i) wild.push_back({std::cos(i * 0.7f) * 2, i * 0.3f, std::sin(i * 0.7f) * 2});
        const Mesh helix = CurvePipe(CurveThrough(wild), 0.1f, 8, 6);
        bool fin = true; for (const Vec3& v : helix.vertices) if (!std::isfinite(v.x + v.y + v.z)) fin = false;
        CHECK(fin && helix.TriangleCount() > 1000 && helix.Topology().Closed() && MeasureMesh(helix).volume > 0);
        // A straight-up path (the usual "up" is along it) still builds.
        CHECK(CurvePipe(CurveThrough({{0, 0, 0}, {0, 3, 0}}, false, 0.0f), 0.3f, 6, 2).Topology().Closed());
        // Big: many points, high sample counts, many sides stays quick.
        std::vector<Vec3> many; for (int i = 0; i < 60; ++i) many.push_back({(float)i, std::sin(i * 0.5f), 0});
        const auto t0 = std::chrono::steady_clock::now();
        const Mesh bigp = CurvePipe(CurveThrough(many), 0.1f, 32, 64);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(bigp.TriangleCount() > 100000 && secs < 20.0);
    }
    RUN_SUITE("bevel weights");
    {
        Mesh m = Mesh::Cube();
        m.SetBevelWeight(3, 1, 0.5f); m.SetBevelWeight(1, 3, 5.0f); m.SetBevelWeight(2, 2, 1.0f); m.SetBevelWeight(0, 4, std::nanf(""));
        CHECK(m.bevelWeights.size() == 1 && m.bevelWeights[0].a == 1 && m.bevelWeights[0].b == 3 && m.bevelWeights[0].w == 1.0f);   // order does not matter; the weight is capped at 1
        m.SetBevelWeight(3, 1, 0.0f); CHECK(m.bevelWeights.empty());
        // Through the polygon mesh and the .okaymesh file.
        PolyMesh pm = PolyMesh::FromMesh(Mesh::Cube());
        const auto e0 = pm.Edges()[0], e1 = pm.Edges()[1];
        pm.SetBevelWeight(e0.a, e0.b, 0.7f); pm.SetBevelWeight(e1.b, e1.a, 0.2f);
        CHECK(pm.BevelWeightOf(e0.b, e0.a) == 0.7f && pm.ToMesh().bevelWeights.size() == 2);
        PolyMesh round; std::string err;
        CHECK(PolyMesh::Parse(pm.Serialize(), round, &err) && round.BevelWeightOf(e1.a, e1.b) == 0.2f);
        CHECK(PolyMesh::FromMesh(Mesh::Cube()).Serialize().find("bevelw") == std::string::npos);
        CHECK(!PolyMesh::Parse("okaymesh 1\nverts 0\nfaces 0\nbevelw 3 4 0.5\n", round, &err) && !PolyMesh::Parse("okaymesh 1\nverts 0\nfaces 0\nbevelw 3 4\n", round, &err));
        PolyMesh cp = PolyMesh::FromMesh(Mesh::Cube()); cp.verts.push_back(Vec3{9, 9, 9}); cp.SetBevelWeight(7, 6, 1.0f);
        const std::vector<int> rm = CompactVerts(cp);
        CHECK(cp.bevelWeights.size() == 1 && cp.bevelWeights[0].a == std::min(rm[6], rm[7]));
        // The Bevel modifier with "use weights" rounds only the weighted edges; without weights it leaves the mesh alone.
        auto volume = [](const Mesh& x) { return MeasureMesh(x).volume; };
        Mesh cube = Mesh::Cube();
        PolyMesh base = PolyMesh::FromMesh(cube);
        int top = -1; for (int f = 0; f < (int)base.faces.size(); ++f) if (base.FaceNormal(f).y > 0.9f) top = f;
        const PolyFace tf = base.faces[top];
        auto weighted = [&](float w) {
            PolyMesh b = base;
            for (std::size_t k = 0; k < tf.verts.size(); ++k) b.SetBevelWeight(tf.verts[k], tf.verts[(k + 1) % tf.verts.size()], w);
            return b.ToMesh();
        };
        MeshModifier mod = MeshModifier::Make(MeshModifier::Type::Bevel); mod.width = 0.1f; mod.segments = 2; mod.useBevelWeights = true;
        Mesh out; std::string merr;
        CHECK(ApplyModifier(cube, mod, ModifierContext(), out, &merr) && out.TriangleCount() == 12);                // no weights: unchanged
        Mesh full, half;
        CHECK(ApplyModifier(weighted(1.0f), mod, ModifierContext(), full, &merr) && full.TriangleCount() > 12 && full.Topology().Closed());
        CHECK(ApplyModifier(weighted(0.5f), mod, ModifierContext(), half, &merr) && half.TriangleCount() > 12);
        CHECK(volume(full) < 1.0f && volume(half) < 1.0f && volume(full) < volume(half));                           // a heavier weight takes a wider bevel
        // Only the top edges were rounded: the bottom corners are still sharp (lowest vertices stay at y = -0.5, the full width).
        float lowest = 9, widest = 0; for (const Vec3& v : full.vertices) { lowest = std::min(lowest, v.y); if (v.y < -0.49f) widest = std::max(widest, std::fabs(v.x)); }
        CHECK(lowest < -0.499f && widest > 0.499f);
        MeshModifier off = mod; off.useBevelWeights = false; off.angle = 30.0f;
        Mesh angled; CHECK(ApplyModifier(weighted(1.0f), off, ModifierContext(), angled, &merr) && angled.TriangleCount() > full.TriangleCount());   // by angle it bevels all 12 edges
        // A changed weight or the flag changes the stack's identity.
        Mesh w1 = weighted(0.5f), w2 = weighted(1.0f);
        CHECK(ModifierInputHash(w1, {}, ModifierContext()) != ModifierInputHash(w2, {}, ModifierContext()));
        std::vector<MeshModifier> a = {mod}, b = a; b[0].useBevelWeights = false;
        CHECK(!SameModifiers(a, b));
        // Weld renumbers vertices: the weight follows.
        Mesh wm = Mesh::Cube(); const Vec3 pa = wm.vertices[2]; wm.vertices.push_back(pa); wm.triangles.insert(wm.triangles.end(), {8, 0, 1});
        wm.bevelWeights = {{8, 1, 0.5f}}; wm.WeldVertices();
        CHECK(wm.bevelWeights.size() == 1 && wm.bevelWeights[0].b < (int)wm.vertices.size() && wm.bevelWeights[0].w == 0.5f);
    }
    RUN_SUITE("quad remesh");
    {
        // A grid of triangles pairs into quads: a flat 6x6 grid is 72 triangles, which become 36 quads with none left.
        Mesh grid = Mesh::Quad(1.0f);
        for (int i = 0; i < 3; ++i) grid.Subdivide();
        int left = -1;
        PolyMesh q = TrisToQuads(grid, 40.0f, 0.3f, &left);
        CHECK(q.faces.size() > 0 && left == 0 && (int)q.faces.size() * 2 == grid.TriangleCount());
        for (const PolyFace& f : q.faces) CHECK(f.Size() == 4);
        // A bent seam (90 degrees) is not paired when the angle limit is smaller, and is when it is larger.
        Mesh bent; bent.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {1, 1, 1}}; bent.triangles = {0, 2, 1, 1, 2, 3};
        int l2 = 0; TrisToQuads(bent, 10.0f, 0.0f, &l2); CHECK(l2 == 2);
        // A dart shape (not convex) is never paired.
        Mesh dart; dart.vertices = {{0, 0, 0}, {2, 0, 0}, {1, 0.2f, 0}, {1, 2, 0}}; dart.triangles = {0, 1, 2, 0, 2, 3};
        int l3 = 0; TrisToQuads(dart, 80.0f, 0.0f, &l3); CHECK(l3 == 2 || l3 == 0);
        // Hostile input: bad indices and NaN do not crash.
        Mesh bad = Mesh::Cube(); bad.triangles.push_back(99); bad.triangles.push_back(-1); bad.triangles.push_back(2); bad.vertices[3].x = std::nanf("");
        QuadRemeshReport br; PolyMesh bq = QuadRemesh(bad, QuadRemeshOptions(), &br); (void)bq;
        CHECK(br.inputTriangles == bad.TriangleCount());
        QuadRemeshReport er; PolyMesh eq = QuadRemesh(Mesh{}, QuadRemeshOptions(), &er);
        CHECK(!er.ok && !er.error.empty() && eq.faces.empty());
        // A sphere with thousands of triangles becomes about the asked number of quads, with no triangles, on the same surface.
        Mesh sphere = Mesh::Sphere(1.0f, 40, 56);
        QuadRemeshOptions o; o.targetQuads = 300;
        QuadRemeshReport r; PolyMesh out = QuadRemesh(sphere, o, &r);
        CHECK(r.ok && r.outputTriangles == 0 && r.outputQuads == (int)out.faces.size());
        CHECK(r.outputQuads > 100 && r.outputQuads < 900 && r.outputQuads < sphere.TriangleCount() / 3);
        float lo = 9, hi = 0; for (const Vec3& v : out.verts) { const float d = v.Magnitude(); lo = std::min(lo, d); hi = std::max(hi, d); }
        CHECK(lo > 0.93f && hi < 1.02f);                                  // stays on the unit sphere
        const Mesh om = out.ToMesh();
        CHECK(om.Topology().Closed());
        CHECK(MeasureMesh(om).volume > 0.8f * MeasureMesh(sphere).volume);
        // Quad-dominant mode keeps triangles where they did not pair.
        QuadRemeshOptions o2; o2.targetQuads = 300; o2.allQuads = false;
        QuadRemeshReport r2; PolyMesh out2 = QuadRemesh(sphere, o2, &r2);
        CHECK(r2.ok && r2.outputQuads > 0 && r2.outputQuads + r2.outputTriangles == (int)out2.faces.size());
        // An open mesh keeps its border: a flat patch stays flat.
        Mesh plane = Mesh::Quad(2.0f); for (int i = 0; i < 5; ++i) plane.Subdivide();
        QuadRemeshOptions o3; o3.targetQuads = 40; QuadRemeshReport r3; PolyMesh out3 = QuadRemesh(plane, o3, &r3);
        float maxY = 0; for (const Vec3& v : out3.verts) maxY = std::max(maxY, std::fabs(v.z));
        CHECK(r3.ok && r3.outputQuads >= 8 && maxY < 1e-3f);
    }
    RUN_SUITE("GeometryHash sees every kind of edit (so leaving edit mode records an undo step)");
    {
        Mesh base = Mesh::Cube();
        const std::uint64_t h0 = okay::modeling::GeometryHash(base);
        Mesh a = base; a.seams.push_back({0, 1});                                  CHECK(okay::modeling::GeometryHash(a) != h0);   // a UV seam
        Mesh b = base; b.cornerUVs.assign(base.triangles.size(), Vec2{0.5f, 0.5f}); CHECK(okay::modeling::GeometryHash(b) != h0);   // per-corner UVs
        Mesh c = base; if (c.polyFace.empty()) c.polyFace.assign((size_t)c.TriangleCount(), 0);
        Mesh c2 = c; c2.polyFace[0] = 7;                                            CHECK(okay::modeling::GeometryHash(c2) != okay::modeling::GeometryHash(c));   // which triangles form a polygon
        Mesh d = base; d.hiddenPos.assign(3, Vec3{0, 0, 0}); d.hiddenSlot.assign(1, -1); d.hiddenPoly.assign(1, -1);   CHECK(okay::modeling::GeometryHash(d) != h0);   // hidden faces
        Mesh e = base; e.vertices[0].x += 1e-3f;                                    CHECK(okay::modeling::GeometryHash(e) != h0);
        CHECK(okay::modeling::GeometryHash(base) == h0);                            // and it is stable
    }
    RUN_SUITE("mirror keeps every per-vertex and per-face attribute in step");
    {
        Mesh m = Mesh::Cube();
        for (Vec3& v : m.vertices) v.x += 1.0f;                                   // a half model clear of the plane
        m.uvs.assign(m.vertices.size(), Vec2{0.25f, 0.75f});
        m.vertexColors.assign(m.vertices.size(), Color{1, 0, 0, 1});
        m.triSlot.assign((size_t)m.TriangleCount(), 1); m.slotColors = {Color{1, 1, 1, 1}, Color{0, 1, 0, 1}};
        m.polyFace.clear(); for (int f = 0; f < m.TriangleCount(); ++f) m.polyFace.push_back(f / 2);
        const int nv0 = (int)m.vertices.size(), nt0 = m.TriangleCount();
        m.Mirror(0, /*weld=*/false);
        CHECK((int)m.vertices.size() == nv0 * 2 && m.TriangleCount() == nt0 * 2);
        CHECK(m.uvs.size() == m.vertices.size());                                 // UVs follow the copied vertices
        CHECK(m.vertexColors.size() == m.vertices.size());
        CHECK((int)m.triSlot.size() == m.TriangleCount());                        // slots follow the copied faces
        CHECK(m.polyFace.empty() || (int)m.polyFace.size() == m.TriangleCount());
        CHECK(m.slotColors.size() == 2);
        // With the weld on (the default) the lists must still match the welded mesh.
        Mesh w = Mesh::Cube(); for (Vec3& v : w.vertices) v.x += 0.5f;
        w.uvs.assign(w.vertices.size(), Vec2{0.5f, 0.5f}); w.triSlot.assign((size_t)w.TriangleCount(), 0); w.slotColors = {Color{1, 1, 1, 1}};
        w.Mirror(0, true);
        CHECK(w.uvs.size() == w.vertices.size() && (int)w.triSlot.size() == w.TriangleCount());
        for (int t : w.triangles) CHECK(t >= 0 && t < (int)w.vertices.size());
    }
    RUN_SUITE("edge snapping: skipped vertices and polygon diagonals");
    {
        // Moving vertex 2 of a quad must still let it snap to edges that do not touch it (here the edge 0-1).
        Mesh q = Mesh::Quad(2.0f);                                       // corners 0..3; triangles (0,1,2) and (0,2,3)
        SnapHit h = SnapToGeometry(q, Vec3{0.0f, -1.05f, 0.0f}, SnapTarget::Edge, 0.2f, {2});
        CHECK(h.found && h.a == 0 && h.b == 1);
        // The diagonal 0-2 is inside one polygon, not an edge the artist sees: with the polygon known it is not offered.
        q.polyFace = {0, 0};
        SnapHit d = SnapToGeometry(q, Vec3{0.02f, 0.0f, 0.0f}, SnapTarget::Edge, 0.3f);
        CHECK(!d.found);
        SnapHit b = SnapToGeometry(q, Vec3{0.0f, -0.95f, 0.0f}, SnapTarget::Edge, 0.3f);
        CHECK(b.found && b.a == 0 && b.b == 1);                         // the border still snaps
    }
    RUN_SUITE("selection and snapping speed on a 240k-triangle mesh (times are printed)");
    {
        Mesh big = Mesh::Sphere(1, 300, 400);
        const int nt = big.TriangleCount();
        std::vector<int> some; for (int t = 0; t < nt; t += 50) some.push_back(t);
        auto timeIt = [&](const char* what, auto&& fn) {
            const auto t0 = std::chrono::steady_clock::now(); fn();
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            std::printf("  PERF select %-22s %8.1f ms\n", what, ms); return ms; };
        std::vector<double> all;
        all.push_back(timeIt("GrowFaces", [&] { (void)GrowFaces(big, some); }));
        all.push_back(timeIt("ShrinkFaces", [&] { (void)ShrinkFaces(big, some); }));
        all.push_back(timeIt("SelectSimilar(area)", [&] { (void)SelectSimilar(big, {0}, SimilarKind::Area, 0.1f); }));
        all.push_back(timeIt("SelectByNormal", [&] { (void)SelectByNormal(big, Vec3{0, 1, 0}, 20.0f); }));
        all.push_back(timeIt("SelectByArea", [&] { (void)SelectByArea(big, 0.0f, 1.0f); }));
        all.push_back(timeIt("LooseParts", [&] { (void)LooseParts(big); }));
        std::vector<ScreenPoint> proj(big.vertices.size());
        for (size_t i = 0; i < proj.size(); ++i) { proj[i].x = big.vertices[i].x * 300 + 400; proj[i].y = big.vertices[i].y * 300 + 400; proj[i].ok = true; }
        const SelectShape box = SelectShape::Box(Vec2{200, 200}, Vec2{500, 500});
        const Vec3 eye{0, 0, 10};
        all.push_back(timeIt("SelectVerticesIn", [&] { (void)SelectVerticesIn(big, proj, box, false, eye); }));
        all.push_back(timeIt("SelectFacesIn", [&] { (void)SelectFacesIn(big, proj, box, false, eye); }));
        all.push_back(timeIt("SnapToGeometry vertex", [&] { (void)SnapToGeometry(big, Vec3{0, 0, 1}, SnapTarget::Vertex, 0.05f); }));
        all.push_back(timeIt("SnapToGeometry edge", [&] { (void)SnapToGeometry(big, Vec3{0, 0, 1}, SnapTarget::Edge, 0.05f); }));
        all.push_back(timeIt("SnapToGeometry face", [&] { (void)SnapToGeometry(big, Vec3{0, 0, 1}, SnapTarget::Face, 0.05f); }));
        (void)all;   // the times are printed for people to read; a seconds bound cannot fail meaningfully, so the checks below count results
        CHECK((int)GrowFaces(big, some).size() > (int)some.size());
        CHECK(SnapToGeometry(big, Vec3{0, 0, 1}, SnapTarget::Vertex, 0.05f).found);
        CHECK(SnapToGeometry(big, Vec3{0, 0, 1}, SnapTarget::Edge, 0.05f).found);
        CHECK(SnapToGeometry(big, Vec3{0, 0, 1}, SnapTarget::Face, 0.05f).found);
    }
    RUN_SUITE("mesh edit undo steps carry the modifier list");
    {
        using namespace okay::modeling;
        Mesh mesh = Mesh::Cube(); std::vector<int> v{0, 1}, f{2}; std::vector<std::pair<int, int>> e{{0, 1}};
        std::vector<MeshModifier> mods(1); mods[0].type = MeshModifier::Type::Mirror;
        MeshUndoTarget tg; tg.mesh = &mesh; tg.verts = &v; tg.faces = &f; tg.edges = &e; tg.mods = &mods;
        std::vector<MeshUndoStep> undo, redo;
        PushMeshUndo(undo, redo, tg, "Add Array");                       // step: mesh + [Mirror]
        MeshModifier arr; arr.type = MeshModifier::Type::Array; arr.count = 5; mods.push_back(arr);   // the artist adds an Array...
        mesh.vertices[0].x += 2.0f; v = {3};                              // ...and moves a vertex
        CHECK(StepMeshUndo(undo, redo, tg, false));
        CHECK(mods.size() == 1 && mods[0].type == MeshModifier::Type::Mirror);    // the modifier change is undone with the mesh
        CHECK(mesh.vertices[0].x < 1.0f && v.size() == 2);
        CHECK(redo.size() == 1 && redo[0].label == "Add Array");
        CHECK(StepMeshUndo(undo, redo, tg, true));                        // redo brings both back
        CHECK(mods.size() == 2 && mods[1].count == 5 && mesh.vertices[0].x > 1.0f && v.size() == 1);
        CHECK(!StepMeshUndo(undo, redo, tg, true));
        // An object without modifiers: steps still work and the pointer stays null.
        MeshUndoTarget tn = tg; tn.mods = nullptr; std::vector<MeshUndoStep> u2, r2;
        PushMeshUndo(u2, r2, tn, ""); CHECK(u2[0].label == "Edit");
        CHECK(StepMeshUndo(u2, r2, tn, false));
        // Bounded history.
        std::vector<MeshUndoStep> u3, r3; for (int i = 0; i < 250; ++i) PushMeshUndo(u3, r3, tg, "x", 100);
        CHECK(u3.size() == 100);
    }
    RUN_SUITE("operators keep UVs, vertex colours, slots and face colours (new faces take the look of the face they came from)");
    {
        auto rich = [] { Mesh m = Mesh::Cube(); const int nv = (int)m.vertices.size(), nt = m.TriangleCount();
            m.uvs.clear(); for (int i = 0; i < nv; ++i) m.uvs.push_back(Vec2{m.vertices[(size_t)i].x + 0.5f, m.vertices[(size_t)i].y + 0.5f});
            m.vertexColors.assign((size_t)nv, Color{1, 0, 0, 1});
            m.triSlot.assign((size_t)nt, 0); m.slotColors = {Color{1, 1, 1, 1}, Color{0, 1, 0, 1}};
            for (int t : {0, 1}) m.triSlot[(size_t)t] = 1;                       // two faces use slot 1
            m.triColors.assign((size_t)nt, Color{0, 0, 1, 1}); return m; };
        auto allIn = [](const Mesh& m) { return m.HasUVs() && m.HasVertexColors() && m.HasSlots() && m.HasFaceColors(); };
        { Mesh m = rich(); m.ExtrudeFaces({0, 1}, 0.2f); CHECK(allIn(m));
          // the side walls and the cap came from slot-1 faces, so slot 1 grew
          int s1 = 0; for (int s : m.triSlot) s1 += (s == 1); CHECK(s1 >= 2); }
        { Mesh m = rich(); m.InsetFaces({0, 1}, 0.2f); CHECK(allIn(m)); int s1 = 0; for (int s : m.triSlot) s1 += (s == 1); CHECK(s1 > 2); }
        { Mesh m = rich(); m.SubdivideFaces({0, 1}); CHECK(allIn(m)); int s1 = 0; for (int s : m.triSlot) s1 += (s == 1); CHECK(s1 > 2); }
        { Mesh m = rich(); m.PokeFaces({0}, 0.1f); CHECK(allIn(m)); }
        { Mesh m = rich(); m.DeleteFaces({5}); CHECK(allIn(m) && m.TriangleCount() == 11); }
        { Mesh m = rich(); m.DuplicateFaces({0, 1}); CHECK(allIn(m)); CHECK(m.triSlot.back() == 1); }
        { Mesh m = rich(); m.Subdivide(); CHECK(allIn(m)); }
        { Mesh m = rich(); m.Array(3, Vec3{2, 0, 0}); CHECK(allIn(m) && m.TriangleCount() == 36); }
        { Mesh m = rich(); m.Array(3, Vec3{std::nanf(""), 0, 0}); CHECK(m.TriangleCount() == 12); for (const Vec3& v : m.vertices) CHECK(std::isfinite(v.x)); }
        { Mesh m = rich(); m.SplitEdges({{0, 1}}); CHECK(allIn(m)); }
        { Mesh m = rich(); m.Decimate(0.01f); CHECK(allIn(m)); }
        { Mesh m = rich(); m.BevelVertices({0}, 0.1f); CHECK(allIn(m)); }
        // A mesh that did not change keeps exactly the values it had.
        { Mesh m = rich(); m.FlipNormals(); CHECK(allIn(m) && m.triSlot[0] == 1 && m.triSlot[5] == 0); }
    }
    RUN_SUITE("which attributes survive each operator (printed)");
    {
        auto rich = [] { Mesh m = Mesh::Cube(); const int nv = (int)m.vertices.size(), nt = m.TriangleCount();
            m.uvs.assign((size_t)nv, Vec2{0.1f, 0.2f}); m.vertexColors.assign((size_t)nv, Color{1, 0, 0, 1});
            m.triSlot.assign((size_t)nt, 1); m.slotColors = {Color{1, 1, 1, 1}, Color{0, 1, 0, 1}};
            m.triColors.assign((size_t)nt, Color{0, 0, 1, 1}); m.polyFace.clear(); for (int t = 0; t < nt; ++t) m.polyFace.push_back(t / 2); return m; };
        auto rep = [](const char* n, const Mesh& m) { std::printf("  KEEPS %-22s uv=%d vcol=%d slot=%d fcol=%d poly=%d\n", n, (int)m.HasUVs(), (int)m.HasVertexColors(), (int)m.HasSlots(), (int)m.HasFaceColors(), (int)(!m.polyFace.empty() && (int)m.polyFace.size() == m.TriangleCount())); };
        { Mesh m = rich(); m.ExtrudeFaces({0, 1}, 0.2f); rep("ExtrudeFaces", m); }
        { Mesh m = rich(); m.InsetFaces({0, 1}, 0.2f); rep("InsetFaces", m); }
        { Mesh m = rich(); m.SubdivideFaces({0, 1}); rep("SubdivideFaces", m); }
        { Mesh m = rich(); m.PokeFaces({0, 1}, 0.1f); rep("PokeFaces", m); }
        { Mesh m = rich(); m.DeleteFaces({0, 1}); rep("DeleteFaces", m); }
        { Mesh m = rich(); m.Subdivide(); rep("Subdivide", m); }
        { Mesh m = rich(); m.Solidify(0.1f); rep("Solidify", m); }
        { Mesh m = rich(); m.Array(3, Vec3{2, 0, 0}); rep("Array", m); }
        { Mesh m = rich(); m.Decimate(0.01f); rep("Decimate", m); }
        { Mesh m = rich(); m.WeldVertices(); rep("WeldVertices", m); }
        { Mesh m = rich(); m.FlipNormals(); rep("FlipNormals", m); }
        { Mesh m = rich(); m.DuplicateFaces({0, 1}); rep("DuplicateFaces", m); }
        { Mesh m = rich(); m.SplitEdges({{0, 1}}); rep("SplitEdges", m); }
        { MeshModifier mod = MeshModifier::Make(MeshModifier::Type::Array); mod.count = 3; Mesh out; std::string err; okay::ApplyModifier(rich(), mod, okay::ModifierContext(), out, &err); rep("modifier Array", out);
          CHECK(out.HasSlots() && out.HasUVs() && out.HasVertexColors() && out.HasFaceColors());      // every copy keeps its material slots
          CHECK(out.slotColors.size() == 2 && out.triSlot.size() == (size_t)out.TriangleCount() && out.triSlot.back() == 1); }
        { MeshModifier mod = MeshModifier::Make(MeshModifier::Type::Solidify); Mesh out; std::string err; okay::ApplyModifier(rich(), mod, okay::ModifierContext(), out, &err); rep("modifier Solidify", out);
          CHECK(out.HasSlots() && out.HasUVs() && out.HasVertexColors() && out.HasFaceColors());     // the inner shell keeps the outer one's UVs, colours and slots
          CHECK(out.TriangleCount() == 2 * Mesh::Cube().TriangleCount() && out.triSlot.back() == 1); }
        { MeshModifier mod = MeshModifier::Make(MeshModifier::Type::Subdivision); Mesh out; std::string err; okay::ApplyModifier(rich(), mod, okay::ModifierContext(), out, &err); rep("modifier Subdivision", out); }
        { MeshModifier mod = MeshModifier::Make(MeshModifier::Type::Mirror); Mesh in = rich(); for (Vec3& v : in.vertices) v.x += 1.0f; Mesh out; std::string err; okay::ApplyModifier(in, mod, okay::ModifierContext(), out, &err); rep("modifier Mirror", out); }
    }
    TEST_MAIN_RESULT();
}
