// Modeling "more": per-face material slots (the M5b gap) and the audit fixes.
// Written from the spec in docs/modeling.md and docs/modeling_audit.md.
#include "test_framework.hpp"
#include "okay/Modeling/MaterialLibrary.hpp"
#include "okay/Modeling/Arrange.hpp"
#include "okay/Modeling/EditTransform.hpp"
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
    TEST_MAIN_RESULT();
}
