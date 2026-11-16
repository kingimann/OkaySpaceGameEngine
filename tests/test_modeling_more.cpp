// Modeling "more": per-face material slots (the M5b gap) and the audit fixes.
// Written from the spec in docs/modeling.md and docs/modeling_audit.md.
#include "test_framework.hpp"
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
        // A big selection stays fast: all edges of an ~19k-triangle sphere (the old
        // version scanned every face for every edge and needed minutes at 100k triangles).
        PolyMesh big = PolyMesh::FromMesh(Mesh::Sphere(1, 80, 120));
        EdgeList bigEdges;
        for (const auto& e : big.Edges()) bigEdges.push_back({e.a, e.b});
        const auto t0 = std::chrono::steady_clock::now();
        const int n = DissolveEdges(big, bigEdges);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK(n > 1000);
        CHECK(!big.faces.empty());
        CHECK(ms < 4000.0);
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
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK(ms < 2000.0);
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
    TEST_MAIN_RESULT();
}
