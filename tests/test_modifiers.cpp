#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Modeling/PolyMesh.hpp"
#include "okay/Modeling/PolyOps.hpp"
#include "okay/Modeling/Modifiers.hpp"
#include <cmath>
#include <string>

#ifndef CHECK_EQ
#define CHECK_EQ(a, b) CHECK((a) == (b))
#endif

using namespace okay;
using namespace okay::modeling;

// Modeling M4: Catmull-Clark subdivision, each modifier type, the
// non-destructive MeshModifiers component (base stays editable, result drawn),
// apply / reorder / toggle, Boolean targets, and save + reload.
namespace {
void MeshBounds(const Mesh& m, Vec3& lo, Vec3& hi) { m.Bounds(lo, hi); }
GameObject* MakeObj(Scene& s, const char* name, Mesh m, Vec3 pos = {0, 0, 0}) {
    GameObject* go = s.CreateGameObject(name);
    go->transform->localPosition = pos;
    go->AddComponent<MeshRenderer>()->mesh = std::move(m);
    return go;
}
} // namespace

int main() {
    RUN_SUITE("modifiers");

    // --- Catmull-Clark on a cube ------------------------------------------------
    {
        PolyMesh cube = PolyMesh::FromMesh(Mesh::Cube(1.0f));
        CHECK_EQ((int)cube.faces.size(), 6);
        PolyMesh s1 = CatmullClark(cube, 1);
        CHECK_EQ((int)s1.faces.size(), 24);
        CHECK_EQ(s1.CountFaces(4), 24);                  // all quads
        CHECK_EQ((int)s1.verts.size(), 8 + 12 + 6);       // corners + edge points + face points
        PolyMesh::Report r = s1.Check();
        CHECK(r.Closed() && r.Valid());
        CHECK_EQ(s1.EulerCharacteristic(), 2);
        // A cube corner moves to (F + 2R + (n-3)P)/n = 5/18 on each axis.
        bool cornerOk = false;
        for (const Vec3& v : s1.verts)
            if (std::fabs(v.x - 5.0f / 18.0f) < 1e-4f && std::fabs(v.y - 5.0f / 18.0f) < 1e-4f &&
                std::fabs(v.z - 5.0f / 18.0f) < 1e-4f) cornerOk = true;
        CHECK(cornerOk);
        // Face points stay on the face centres; the shape stays inside the cube.
        for (const Vec3& v : s1.verts) CHECK(std::fabs(v.x) <= 0.5f + 1e-5f && std::fabs(v.y) <= 0.5f + 1e-5f);
        // Normals keep pointing out (winding preserved): every face normal points away from the centre.
        int outward = 0;
        for (int f = 0; f < (int)s1.faces.size(); ++f) {
            Vec3 c{0, 0, 0};
            for (int v : s1.faces[f].verts) c = c + s1.verts[v];
            c = c * 0.25f;
            if (Vec3::Dot(s1.FaceNormal(f), c) > 0.0f) ++outward;
        }
        CHECK_EQ(outward, 24);
        PolyMesh s2 = CatmullClark(cube, 2);
        CHECK_EQ((int)s2.faces.size(), 96);
        CHECK(s2.Check().Closed());
        // Converges toward a rounded shape: corner distance shrinks each level.
        auto maxR = [](const PolyMesh& p) { float m = 0; for (const Vec3& v : p.verts) m = std::fmax(m, v.Magnitude()); return m; };
        CHECK(maxR(s2) < maxR(s1));
        CHECK(maxR(s1) < 0.8661f);
    }
    // --- Open border + UVs ---------------------------------------------------------
    {
        PolyMesh quad;
        quad.verts = {{0, 0, 0}, {1, 0, 0}, {1, 0, -1}, {0, 0, -1}};
        PolyFace f; f.verts = {0, 1, 2, 3}; f.uvs = {{0, 0}, {1, 0}, {1, 1}, {0, 1}}; f.material = 2;
        quad.faces.push_back(f);
        PolyMesh s = CatmullClark(quad, 1);
        CHECK_EQ((int)s.faces.size(), 4);
        CHECK_EQ(s.Check().boundaryEdges, 8);
        // Border corner: 3/4 P + 1/8 (two border neighbours).
        CHECK_NEAR(s.verts[0].x, 0.125f, 1e-5f);
        CHECK_NEAR(s.verts[0].z, -0.125f, 1e-5f);
        for (const PolyFace& q : s.faces) { CHECK_EQ((int)q.uvs.size(), 4); CHECK_EQ(q.material, 2); }
        CHECK_NEAR(s.faces[0].uvs[0].x, 0.0f, 1e-6f);     // corners keep their UVs
        CHECK_NEAR(s.faces[0].uvs[2].x, 0.5f, 1e-6f);     // face centre UV
    }

    // --- Each modifier --------------------------------------------------------------
    ModifierContext ctx;
    {
        Mesh cube = Mesh::Cube(1.0f);
        Mesh out; std::string err;
        // Mirror: a half moved to +X becomes symmetric.
        Mesh half = Mesh::Cube(1.0f);
        for (Vec3& v : half.vertices) v.x += 1.0f;
        MeshModifier mir = MeshModifier::Make(MeshModifier::Type::Mirror);
        CHECK(ApplyModifier(half, mir, ctx, out, &err));
        Vec3 lo, hi; MeshBounds(out, lo, hi);
        CHECK_NEAR(lo.x, -1.5f, 1e-4f); CHECK_NEAR(hi.x, 1.5f, 1e-4f);
        CHECK_EQ(out.TriangleCount(), 2 * half.TriangleCount());
        // Array: 3 copies 10% apart.
        MeshModifier arr = MeshModifier::Make(MeshModifier::Type::Array);
        arr.count = 3; arr.relative = true; arr.offset = {1.1f, 0, 0};
        CHECK(ApplyModifier(cube, arr, ctx, out, &err));
        MeshBounds(out, lo, hi);
        CHECK_NEAR(hi.x - lo.x, 1.0f + 2.0f * 1.1f, 1e-4f);
        CHECK_EQ(out.TriangleCount(), 3 * cube.TriangleCount());
        CHECK_EQ((int)PolyMesh::FromMesh(out).faces.size(), 18);
        {   // A mesh carrying its polygon map keeps it across the copies.
            Mesh polyCube = PolyMesh::FromMesh(cube).ToMesh();
            CHECK_EQ((int)polyCube.polyFace.size(), polyCube.TriangleCount());
            Mesh arrOut;
            CHECK(ApplyModifier(polyCube, arr, ctx, arrOut, &err));
            CHECK_EQ((int)arrOut.polyFace.size(), arrOut.TriangleCount());
            CHECK_EQ((int)PolyMesh::FromMesh(arrOut).faces.size(), 18);
        }
        arr.relative = false; arr.offset = {0, 0, 2.0f};
        CHECK(ApplyModifier(cube, arr, ctx, out, &err));
        MeshBounds(out, lo, hi);
        CHECK_NEAR(hi.z - lo.z, 5.0f, 1e-4f);
        // Subdivision: quads, smooth normals, rounder.
        MeshModifier sub = MeshModifier::Make(MeshModifier::Type::Subdivision);
        sub.levels = 2;
        CHECK(ApplyModifier(cube, sub, ctx, out, &err));
        CHECK_EQ((int)PolyMesh::FromMesh(out).faces.size(), 96);
        CHECK(out.HasNormals());
        sub.levels = 4;   // 6 * 256 faces: fine
        CHECK(ApplyModifier(cube, sub, ctx, out, &err));
        Mesh big = Mesh::Sphere(1.0f, 96, 128);   // ~24k triangles: 4 levels would be millions of faces
        CHECK(!ApplyModifier(big, sub, ctx, out, &err));   // runaway: refused with a reason
        CHECK(!err.empty());
        CHECK_EQ(out.TriangleCount(), big.TriangleCount()); // ...and left unchanged
        // Solidify a plane: becomes a closed slab.
        MeshModifier sol = MeshModifier::Make(MeshModifier::Type::Solidify);
        sol.thickness = 0.2f;
        Mesh plane = Mesh::Plane(2.0f);
        CHECK(ApplyModifier(plane, sol, ctx, out, &err));
        MeshBounds(out, lo, hi);
        CHECK_NEAR(hi.y - lo.y, 0.2f, 1e-3f);
        // Bevel: every cube edge rounded, size unchanged, more faces.
        MeshModifier bev = MeshModifier::Make(MeshModifier::Type::Bevel);
        bev.width = 0.1f; bev.segments = 2;
        err.clear();
        CHECK(ApplyModifier(cube, bev, ctx, out, &err));
        CHECK(err.empty());
        MeshBounds(out, lo, hi);
        CHECK_NEAR(hi.x, 0.5f, 1e-4f); CHECK_NEAR(lo.y, -0.5f, 1e-4f);
        CHECK((int)PolyMesh::FromMesh(out).faces.size() > 6 + 12);
        bev.angle = 120.0f;   // nothing that sharp on a cube: unchanged
        CHECK(ApplyModifier(cube, bev, ctx, out, &err));
        CHECK_EQ(out.TriangleCount(), cube.TriangleCount());
        // Decimate a dense sphere.
        Mesh sph = Mesh::Sphere(1.0f, 32, 48);
        MeshModifier dec = MeshModifier::Make(MeshModifier::Type::Decimate);
        dec.cellSize = 0.3f;
        CHECK(ApplyModifier(sph, dec, ctx, out, &err));
        CHECK(out.TriangleCount() < sph.TriangleCount() / 2);
        // Displace moves vertices, same topology.
        MeshModifier dis = MeshModifier::Make(MeshModifier::Type::Displace);
        dis.strength = 0.1f;
        CHECK(ApplyModifier(sph, dis, ctx, out, &err));
        CHECK_EQ(out.TriangleCount(), sph.TriangleCount());
        float moved = 0.0f;
        for (std::size_t i = 0; i < sph.vertices.size(); ++i) moved += (out.vertices[i] - sph.vertices[i]).Magnitude();
        CHECK(moved > 0.1f);
        // Boolean without a scene/target: refused with a reason, mesh untouched.
        MeshModifier boo = MeshModifier::Make(MeshModifier::Type::Boolean);
        err.clear();
        CHECK(!ApplyModifier(cube, boo, ctx, out, &err));
        CHECK(!err.empty());
        CHECK_EQ(out.TriangleCount(), cube.TriangleCount());

        // A stack: disabled modifiers are skipped; edit mode hides "not in edit mode" ones.
        std::vector<MeshModifier> stack = {arr, sub};
        stack[0].relative = true; stack[0].offset = {1.1f, 0, 0}; stack[0].count = 2;
        stack[1].levels = 1;
        std::vector<std::string> errs;
        Mesh r = EvaluateModifiers(cube, stack, ctx, &errs);
        CHECK_EQ((int)errs.size(), 2);
        CHECK_EQ((int)PolyMesh::FromMesh(r).faces.size(), 48);   // 2 cubes x 24
        stack[1].enabled = false;
        r = EvaluateModifiers(cube, stack, ctx);
        CHECK_EQ(r.TriangleCount(), 2 * cube.TriangleCount());
        stack[1].enabled = true; stack[1].showInEditMode = false;
        ModifierContext edit = ctx; edit.editMode = true;
        r = EvaluateModifiers(cube, stack, edit);
        CHECK_EQ(r.TriangleCount(), 2 * cube.TriangleCount());
        // Order matters: subdivide then array = same faces, different from array then
        // subdivide only in how the copies are joined (here: separate, so equal counts).
        std::vector<MeshModifier> rev = {stack[1], stack[0]}; rev[0].showInEditMode = true;
        CHECK_EQ((int)PolyMesh::FromMesh(EvaluateModifiers(cube, rev, ctx)).faces.size(), 48);
    }

    // --- The component: non-destructive, refresh, apply, reorder, remove -------------
    {
        Scene s("mods");
        GameObject* go = MakeObj(s, "Box", Mesh::Cube(1.0f));
        auto* mr = go->GetComponent<MeshRenderer>();
        auto* mm = go->AddComponent<MeshModifiers>();
        MeshModifier sub = MeshModifier::Make(MeshModifier::Type::Subdivision);
        mm->modifiers.push_back(sub);
        s.Start();
        CHECK(mr->modifiedMesh != nullptr);
        CHECK_EQ(mr->mesh.TriangleCount(), 12);                       // base untouched
        CHECK_EQ(mr->DisplayMesh().TriangleCount(), 48);              // result drawn
        const Mesh* resultObj = mr->modifiedMesh.get();
        CHECK(!mm->Refresh());                                         // nothing changed: no rebuild
        // Editing the base rebuilds the result (into the same Mesh object).
        for (Vec3& v : mr->mesh.vertices) v.y *= 2.0f;
        CHECK(mm->Refresh());
        CHECK(mr->modifiedMesh.get() == resultObj);
        Vec3 lo, hi; mr->DisplayMesh().Bounds(lo, hi);
        CHECK(hi.y > 0.5f);
        // Changing a setting rebuilds too.
        mm->modifiers[0].levels = 2;
        s.Update(0.016f);                                              // Update refreshes in a game
        CHECK_EQ(mr->DisplayMesh().TriangleCount(), 192);
        // Toggle off: draws the base again.
        mm->modifiers[0].enabled = false;
        mm->Refresh();
        CHECK(mr->modifiedMesh == nullptr);
        CHECK(&mr->DisplayMesh() == &mr->mesh);
        mm->modifiers[0].enabled = true;
        // Reorder.
        MeshModifier arr = MeshModifier::Make(MeshModifier::Type::Array);
        arr.count = 2;
        mm->modifiers.push_back(arr);
        mm->Move(1, 0);
        CHECK(mm->modifiers[0].type == MeshModifier::Type::Array);
        mm->Refresh();
        CHECK_EQ(mr->DisplayMesh().TriangleCount(), 2 * 192);
        // Apply the first one: baked into the base, gone from the stack.
        std::string err;
        CHECK(mm->Apply(0, &err));
        CHECK_EQ((int)mm->modifiers.size(), 1);
        CHECK_EQ(mr->mesh.TriangleCount(), 24);
        CHECK_EQ(mr->DisplayMesh().TriangleCount(), 2 * 192);       // same look as before applying
        // Apply all.
        mm->ApplyAll();
        CHECK(mm->modifiers.empty());
        CHECK_EQ(mr->mesh.TriangleCount(), 2 * 192);
        CHECK(mr->modifiedMesh == nullptr);
        // Removing the component drops its result.
        mm->modifiers.push_back(sub);
        mm->Refresh();
        CHECK(mr->modifiedMesh != nullptr);
        go->RemoveComponent(mm);
        CHECK(mr->modifiedMesh == nullptr);
    }

    // --- Boolean against another object; follows the cutter ----------------------------
    {
        Scene s("bool");
        GameObject* a = MakeObj(s, "Block", Mesh::Cube(2.0f));
        MakeObj(s, "Cutter", Mesh::Cube(2.0f), {1.0f, 0, 0});        // covers the +X half
        auto* mm = a->AddComponent<MeshModifiers>();
        MeshModifier b = MeshModifier::Make(MeshModifier::Type::Boolean);
        b.target = "Cutter"; b.boolOp = 1; b.voxel = 0.1f;
        mm->modifiers.push_back(b);
        s.Start();
        CHECK(mm->errors.size() == 1 && mm->errors[0].empty());
        Vec3 lo, hi; a->GetComponent<MeshRenderer>()->DisplayMesh().Bounds(lo, hi);
        CHECK(hi.x < 0.2f);                                            // the +X half is cut away
        CHECK(lo.x < -0.8f);
        // Move the cutter away: the result follows on the next refresh.
        s.Find("Cutter")->transform->localPosition = {10, 0, 0};
        CHECK(mm->Refresh());
        a->GetComponent<MeshRenderer>()->DisplayMesh().Bounds(lo, hi);
        CHECK(hi.x > 0.8f);
        // A missing target reports why and leaves the mesh as it is.
        mm->modifiers[0].target = "Nope";
        mm->Refresh();
        CHECK(!mm->errors[0].empty());
        CHECK_EQ(a->GetComponent<MeshRenderer>()->DisplayMesh().TriangleCount(), 12);
    }

    // --- Save + reload ------------------------------------------------------------------
    {
        Scene s("save");
        GameObject* go = MakeObj(s, "Thing", Mesh::Cube(1.0f));
        auto* mm = go->AddComponent<MeshModifiers>();
        MeshModifier a = MeshModifier::Make(MeshModifier::Type::Array);
        a.count = 4; a.relative = false; a.offset = {0, 1.5f, 0}; a.showInEditMode = false;
        MeshModifier sub = MeshModifier::Make(MeshModifier::Type::Subdivision);
        sub.levels = 2; sub.smoothShading = false; sub.enabled = false;
        MeshModifier b = MeshModifier::Make(MeshModifier::Type::Boolean);
        b.target = "Cutter \"A\""; b.boolOp = 2; b.voxel = 0.07f;
        MeshModifier bev = MeshModifier::Make(MeshModifier::Type::Bevel);
        bev.width = 0.03f; bev.segments = 3; bev.angle = 45.0f;
        mm->modifiers = {a, sub, b, bev};
        std::string text = SceneSerializer::Serialize(s);
        Scene r("reload");
        std::string err;
        CHECK(SceneSerializer::Deserialize(r, text, &err));
        GameObject* g2 = r.Find("Thing");
        CHECK(g2 != nullptr);
        auto* m2 = g2 ? g2->GetComponent<MeshModifiers>() : nullptr;
        CHECK(m2 != nullptr);
        if (m2) {
            CHECK_EQ((int)m2->modifiers.size(), 4);
            CHECK(m2->modifiers[0].type == MeshModifier::Type::Array);
            CHECK_EQ(m2->modifiers[0].count, 4);
            CHECK(!m2->modifiers[0].relative);
            CHECK_NEAR(m2->modifiers[0].offset.y, 1.5f, 1e-5f);
            CHECK(!m2->modifiers[0].showInEditMode);
            CHECK(!m2->modifiers[1].enabled);
            CHECK_EQ(m2->modifiers[1].levels, 2);
            CHECK(!m2->modifiers[1].smoothShading);
            CHECK(m2->modifiers[2].target == "Cutter \"A\"");
            CHECK_EQ(m2->modifiers[2].boolOp, 2);
            CHECK_NEAR(m2->modifiers[2].voxel, 0.07f, 1e-5f);
            CHECK_EQ(m2->modifiers[3].segments, 3);
            CHECK_NEAR(m2->modifiers[3].angle, 45.0f, 1e-4f);
            r.Start();
            auto* mr2 = g2->GetComponent<MeshRenderer>();
            CHECK(mr2->modifiedMesh != nullptr);
            CHECK_EQ(mr2->mesh.TriangleCount(), 12);              // the base is what's saved
            Vec3 lo, hi; mr2->DisplayMesh().Bounds(lo, hi);
            CHECK_NEAR(hi.y - lo.y, 1.0f + 3.0f * 1.5f, 1e-3f);   // array evaluated on load
        }
        // An object without a stack writes nothing new.
        Scene p("plain");
        MakeObj(p, "Box", Mesh::Cube(1.0f));
        CHECK(SceneSerializer::Serialize(p).find("modstack") == std::string::npos);
        CHECK(SceneSerializer::Serialize(p).find("meshenabled") == std::string::npos);
        // A hidden cutter stays hidden after a reload.
        p.Find("Box")->GetComponent<MeshRenderer>()->enabled = false;
        Scene q("q");
        CHECK(SceneSerializer::Deserialize(q, SceneSerializer::Serialize(p), &err));
        CHECK(q.Find("Box") && !q.Find("Box")->GetComponent<MeshRenderer>()->enabled);
    }

    TEST_MAIN_RESULT();
}
