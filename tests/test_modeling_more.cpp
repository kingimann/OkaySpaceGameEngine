// Modeling "more": per-face material slots (the M5b gap) and the audit fixes.
// Written from the spec in docs/modeling.md and docs/modeling_audit.md.
#include "test_framework.hpp"
#include "okay/Modeling/Measure.hpp"
#include "okay/Modeling/Modifiers.hpp"
#include "okay/Modeling/PolyOps.hpp"
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
    TEST_MAIN_RESULT();
}
