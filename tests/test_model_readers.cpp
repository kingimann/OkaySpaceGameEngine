// Our own model readers (OBJ+MTL, STL ascii/binary, PLY ascii/binary) and the
// import seam (okay/Render/ModelData.hpp): fixtures, malformed input, a
// deterministic fuzz sweep (run it under -DOKAY_SANITIZE=address,undefined), the
// backend switch, and, when the build has Assimp, the same files read by both
// backends and compared (counts, triangles, normals, UVs, materials, skeleton, clips).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace okay;

static const std::string kDir = std::string(OKAY_SOURCE_DIR) + "/tests/data/models/";

static std::string Slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
static void Spit(const std::string& path, const std::string& bytes) {
    std::ofstream f(path, std::ios::binary); f << bytes;
}

// Every mesh in the model satisfies what the renderer assumes.
static bool Sane(const ImportedModel& m) {
    for (const ImportedPart& p : m.parts) {
        const Mesh& me = p.mesh;
        if (me.triangles.size() % 3) return false;
        for (int t : me.triangles) if (t < 0 || t >= (int)me.vertices.size()) return false;
        for (const Vec3& v : me.vertices) if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return false;
        if (!me.normals.empty() && me.normals.size() != me.vertices.size()) return false;
        if (!me.uvs.empty() && me.uvs.size() != me.vertices.size()) return false;
    }
    return true;
}

// A triangle as three rounded corners (position, normal, uv), rotated so the smallest
// corner is first: comparing two sorted lists of these compares the meshes regardless of
// vertex order. `flipV`: the OBJ reader keeps v as written, Assimp flips it.
using Corner = std::array<long, 8>;
using Tri = std::array<Corner, 3>;
static long Q(float v) { return std::lround(v * 1000.0f); }
static std::vector<Tri> Triangles(const ImportedModel& m, bool withNormals, bool withUV, bool flipV) {
    std::vector<Tri> out;
    for (const ImportedPart& p : m.parts) {
        const Mesh& me = p.mesh;
        for (std::size_t i = 0; i + 2 < me.triangles.size(); i += 3) {
            Tri t;
            for (int k = 0; k < 3; ++k) {
                int v = me.triangles[i + k];
                Corner c{}; const Vec3& a = me.vertices[(std::size_t)v];
                c[0] = Q(a.x); c[1] = Q(a.y); c[2] = Q(a.z);
                if (withNormals && !me.normals.empty()) { const Vec3& n = me.normals[(std::size_t)v]; c[3] = Q(n.x); c[4] = Q(n.y); c[5] = Q(n.z); }
                if (withUV && !me.uvs.empty()) { c[6] = Q(me.uvs[(std::size_t)v].x); c[7] = Q(flipV ? 1.0f - me.uvs[(std::size_t)v].y : me.uvs[(std::size_t)v].y); }
                t[(std::size_t)k] = c;
            }
            std::size_t lo = 0;
            for (std::size_t k = 1; k < 3; ++k) if (t[k] < t[lo]) lo = k;
            std::rotate(t.begin(), t.begin() + (std::ptrdiff_t)lo, t.end());
            out.push_back(t);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

int main() {
    RUN_SUITE("model_readers");

    // ---------------- STL ----------------
    ImportedModel stlA, stlB;
    {
        std::string err;
        CHECK(ReadStlFile(kDir + "cube_ascii.stl", stlA, &err));
        CHECK(ReadStlFile(kDir + "cube_binary.stl", stlB, &err));
        CHECK(stlA.parts.size() == 1 && stlB.parts.size() == 1);
        CHECK(Sane(stlA) && Sane(stlB));
        CHECK(stlA.parts[0].mesh.TriangleCount() == 12);
        CHECK(stlB.parts[0].mesh.TriangleCount() == 12);
        // 6 faces x 4 corners: welded per position+normal, so a cube is 24 vertices, not 36 and not 8.
        CHECK(stlA.parts[0].mesh.vertices.size() == 24);
        CHECK(stlB.parts[0].mesh.vertices.size() == 24);
        CHECK(stlA.parts[0].mesh.HasNormals());
        CHECK(Triangles(stlA, true, false, false) == Triangles(stlB, true, false, false));   // ascii == binary
        Vec3 lo, hi; stlA.parts[0].mesh.Bounds(lo, hi);
        CHECK_NEAR(lo.x, -1, 1e-5); CHECK_NEAR(hi.z, 1, 1e-5);
        for (const Vec3& n : stlA.parts[0].mesh.normals) CHECK_NEAR(n.Magnitude(), 1.0, 1e-4);
        // Normals point out of the cube (the winding in the fixture is CCW from outside).
        const Mesh& me = stlA.parts[0].mesh;
        for (std::size_t i = 0; i < me.triangles.size(); i += 3) {
            Vec3 g = Vec3::Cross(me.vertices[(std::size_t)me.triangles[i + 1]] - me.vertices[(std::size_t)me.triangles[i]],
                                 me.vertices[(std::size_t)me.triangles[i + 2]] - me.vertices[(std::size_t)me.triangles[i]]);
            CHECK(Vec3::Dot(g, me.normals[(std::size_t)me.triangles[i]]) > 0.0f);
        }
        ImportedModel two;
        CHECK(ReadStlFile(kDir + "two_solids.stl", two, &err));
        CHECK(two.parts.size() == 2);
        CHECK(two.parts[0].mesh.name == "a" && two.parts[1].mesh.name == "b");
        // A binary file whose 80-byte header starts with "solid" is still binary (size rule).
        std::string tricky = Slurp(kDir + "cube_binary.stl");
        tricky.replace(0, 5, "solid");
        ImportedModel tm; CHECK(ReadStlBuffer(tricky, tm, &err)); CHECK(tm.parts.size() == 1 && tm.parts[0].mesh.TriangleCount() == 12);
        // Padded binary (extra trailing bytes) still reads.
        ImportedModel pm; CHECK(ReadStlBuffer(Slurp(kDir + "cube_binary.stl") + std::string(7, '\0'), pm, &err)); CHECK(pm.parts[0].mesh.TriangleCount() == 12);
        // A zero normal in the file falls back to the winding.
        std::string z = Slurp(kDir + "two_solids.stl");
        for (std::size_t p = z.find("normal 0 0 1"); p != std::string::npos; p = z.find("normal 0 0 1")) z.replace(p, 12, "normal 0 0 0");
        ImportedModel zm; CHECK(ReadStlBuffer(z, zm, &err));
        CHECK_NEAR(zm.parts[0].mesh.normals[0].z, 1.0, 1e-5);
    }

    // ---------------- PLY ----------------
    ImportedModel plyA, plyL, plyB;
    {
        std::string err;
        CHECK(ReadPlyFile(kDir + "cube_ascii.ply", plyA, &err));
        CHECK(ReadPlyFile(kDir + "cube_le.ply", plyL, &err));
        CHECK(ReadPlyFile(kDir + "cube_be.ply", plyB, &err));
        for (const ImportedModel* m : {&plyA, &plyL, &plyB}) {
            CHECK(m->parts.size() == 1 && Sane(*m));
            const Mesh& me = m->parts[0].mesh;
            CHECK(me.vertices.size() == 8);
            CHECK(me.TriangleCount() == 12);          // 6 quads fan-triangulated
            CHECK(me.HasNormals() && me.HasUVs());
        }
        CHECK(Triangles(plyA, true, true, false) == Triangles(plyL, true, true, false));
        CHECK(Triangles(plyL, true, true, false) == Triangles(plyB, true, true, false));
        // UV v is flipped on read: vertex 3 has t = 1 in the file.
        CHECK_NEAR(plyL.parts[0].mesh.uvs[3].y, 0.0, 1e-6);
        CHECK_NEAR(plyL.parts[0].mesh.uvs[0].y, 1.0, 1e-6);
        ImportedModel tet;
        CHECK(ReadPlyFile(kDir + "tetra_plain.ply", tet, &err));   // extra "edge" element is skipped
        CHECK(tet.parts[0].mesh.TriangleCount() == 4 && tet.parts[0].mesh.HasNormals() && !tet.parts[0].mesh.HasUVs());
        for (const Vec3& n : tet.parts[0].mesh.normals) CHECK_NEAR(n.Magnitude(), 1.0, 1e-4);
    }

    // ---------------- OBJ + MTL ----------------
    ImportedModel obj;
    {
        std::string err;
        CHECK(ReadObjFile(kDir + "cube.obj", obj, &err));
        CHECK(Sane(obj));
        CHECK(obj.parts.size() == 2);                 // two usemtl runs: red (faces 1-3), blue (4-6)
        CHECK(obj.materials.size() == 2);
        // A missing/unreadable fixture must fail the test, not crash it on parts[0].
        if (obj.parts.size() != 2 || obj.materials.size() != 2) { std::printf("  cube.obj fixture missing or wrong\n"); TEST_MAIN_RESULT(); }
        CHECK(obj.parts[0].mesh.name == "red" && obj.parts[1].mesh.name == "blue");
        CHECK(obj.parts[0].mesh.TriangleCount() == 6 && obj.parts[1].mesh.TriangleCount() == 6);
        CHECK(obj.parts[0].mesh.HasNormals() && obj.parts[0].mesh.HasUVs());
        const ImportedMaterial& red = obj.materials[(std::size_t)obj.parts[0].material];
        const ImportedMaterial& blue = obj.materials[(std::size_t)obj.parts[1].material];
        CHECK(red.name == "red" && red.hasDiffuse);
        CHECK_NEAR(red.diffuse.r, 0.8, 1e-5); CHECK_NEAR(red.diffuse.g, 0.1, 1e-5);
        CHECK_NEAR(red.shininess, 50.0, 1e-5);
        CHECK(red.texture == kDir + "textures/red.png");        // backslash fixed, resolved next to the model
        CHECK(blue.texture == kDir + "blue.png");               // "-s 1 1 1" option skipped
        CHECK(blue.normalMap == kDir + "blue_n.png");           // "-bm 1.0" option skipped
        CHECK(blue.specularMap == kDir + "spec.png");
        // Same triangles as the engine's long-standing Mesh::LoadOBJ (one merged mesh).
        bool lok = false; Mesh legacy = Mesh::LoadOBJ(kDir + "cube.obj", &lok);
        CHECK(lok);
        ImportedModel lm; lm.parts.resize(1); lm.parts[0].mesh = legacy;
        CHECK(Triangles(obj, true, true, false) == Triangles(lm, true, true, false));
        // Plain OBJ: no mtl, relative negative indices.
        ImportedModel pl; CHECK(ReadObjFile(kDir + "plain.obj", pl, &err));
        CHECK(pl.parts.size() == 1 && pl.parts[0].mesh.TriangleCount() == 2 && pl.parts[0].material == -1);
        // A missing .mtl is not an error; the usemtl name is still carried.
        ImportedModel nm; CHECK(ReadObjBuffer("mtllib nope.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl ghost\nf 1 2 3\n", kDir, nm, &err));
        CHECK(nm.materials.size() == 1 && nm.materials[0].name == "ghost" && !nm.materials[0].hasDiffuse);
        // v/vt without vn on part of the faces: the attribute is dropped for that part, not invented.
        ImportedModel mixed;
        CHECK(ReadObjBuffer("v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvn 0 0 1\nf 1/1 2/1 3/1\n", "", mixed, &err));
        CHECK(mixed.parts[0].mesh.HasUVs() && !mixed.parts[0].mesh.HasNormals());
    }

    // ---------------- The seam: backend switch ----------------
    {
        CHECK(HasOwnReader(".obj") && HasOwnReader(".STL") && HasOwnReader(".ply") && HasOwnReader(".dae") && HasOwnReader(".3ds") && HasOwnReader(".bvh") && !HasOwnReader(".blend"));
        CHECK(!SetImportBackend(".blend", ImportBackend::Own));
        SetImportBackend("*", ImportBackend::Default);
        CHECK(EffectiveImportBackend(".obj") == ImportBackend::Own);          // OBJ never went through Assimp
        if (AssimpAvailable()) CHECK(EffectiveImportBackend(".stl") == ImportBackend::Assimp || EffectiveImportBackend(".stl") == ImportBackend::Own);
        else { CHECK(EffectiveImportBackend(".stl") == ImportBackend::Own); CHECK(EffectiveImportBackend(".dae") == ImportBackend::Default || EffectiveImportBackend(".dae") == ImportBackend::Own); }
        SetImportBackend(".stl", ImportBackend::Own);
        CHECK(EffectiveImportBackend(".stl") == ImportBackend::Own);
        SetImportBackend(".stl", ImportBackend::Assimp);
        CHECK(EffectiveImportBackend(".stl") == (AssimpAvailable() ? ImportBackend::Assimp : ImportBackend::Own));
        SetImportBackend("*", ImportBackend::Default);

        const std::string exts = " " + ImportableExtensions() + " ";
        for (const char* e : {".obj", ".stl", ".ply", ".fbx", ".glb", ".gltf"}) CHECK(exts.find(std::string(" ") + e + " ") != std::string::npos);

        ImportedModel m; std::string err;
        CHECK(ImportModelData(kDir + "cube_ascii.stl", m, ImportBackend::Own, false, nullptr));
        CHECK(m.backend == "own" && m.parts.size() == 1);
        CHECK(ImportModelData(kDir + "cube_ascii.stl", m, ImportBackend::Own));
        CHECK(!ImportModelData(kDir + "cube.mtl", m, ImportBackend::Own, false, &err) && !err.empty());   // not a model
        CHECK(!ImportModelData(kDir + "x.dae", m, ImportBackend::Own, false, &err));                       // no own reader
        if (!AssimpAvailable()) CHECK(!ImportModelData(kDir + "cube_ascii.stl", m, ImportBackend::Assimp, false, &err));

        // ImportModel (one merged mesh) now reads STL / PLY in every build.
        for (const char* f : {"cube_ascii.stl", "cube_binary.stl", "cube_ascii.ply", "cube_le.ply", "cube_be.ply"}) {
            bool ok = false; Mesh mm = ImportModel(kDir + f, &ok);
            CHECK(ok); CHECK(mm.TriangleCount() == 12);
            Vec3 lo, hi; mm.Bounds(lo, hi); CHECK_NEAR(hi.x - lo.x, 2.0, 1e-4);
        }
        { bool ok = true; Mesh mm = ImportModel(kDir + "cube_binary.stl", &ok); (void)mm; CHECK(ok); }
        { bool ok = true; ImportModel(kDir + "nothing.stl", &ok); CHECK(!ok); }

        // ImportModelScene: an OBJ with two materials becomes two MeshRenderers with their own looks.
        Scene sc; sc.physicsEnabled = false;
        bool ok = false; GameObject* root = ImportModelScene(sc, kDir + "cube.obj", &ok);
        CHECK(ok && root != nullptr);
        int renderers = 0; bool sawRed = false, sawBlue = false;
        for (const auto& go : sc.Objects()) if (auto* mr = go->GetComponent<MeshRenderer>()) {
            ++renderers;
            if (mr->texture == kDir + "textures/red.png") sawRed = true;
            if (mr->texture == kDir + "blue.png" && mr->normalMap == kDir + "blue_n.png") sawBlue = true;
        }
        CHECK(renderers == 2 && sawRed && sawBlue);
        Scene sc2; sc2.physicsEnabled = false;
        GameObject* r2 = ImportModelScene(sc2, kDir + "cube_binary.stl", &ok);
        // (with Assimp as the STL/PLY default the mesh hangs on a child node, so look at every object)
        auto firstMesh = [](Scene& s) -> MeshRenderer* { for (const auto& go : s.Objects()) if (auto* mr = go->GetComponent<MeshRenderer>()) return mr; return nullptr; };
        CHECK(ok && r2 && firstMesh(sc2) && firstMesh(sc2)->mesh.TriangleCount() == 12);
        Scene sc3; sc3.physicsEnabled = false;
        GameObject* r3 = ImportModelScene(sc3, kDir + "cube_le.ply", &ok);
        CHECK(ok && r3 && firstMesh(sc3) && firstMesh(sc3)->mesh.HasUVs());
        // An oversized export (centimetres) is scaled to a usable size, like the other importers.
        Spit("mr_big.stl", "solid big\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 500 0 0\nvertex 0 500 0\nendloop\nendfacet\nendsolid big\n");
        bool bok = false; Mesh big = ImportModel("mr_big.stl", &bok);
        CHECK(bok); Vec3 blo, bhi; big.Bounds(blo, bhi); CHECK(bhi.x - blo.x < 20.0f);
        std::remove("mr_big.stl");
    }

    // ---------------- Same files through both backends (needs Assimp) ----------------
#ifdef OKAY_HAVE_ASSIMP
    {
        std::printf("  Assimp available: comparing backends\n");
        struct Case { const char* file; bool normals; bool uv; bool flipV; };
        const Case cases[] = {
            {"cube_ascii.stl", true, false, false}, {"cube_binary.stl", true, false, false},
            {"cube_ascii.ply", true, true, false},  {"cube_le.ply", true, true, false}, {"cube_be.ply", true, true, false},
            {"tetra_plain.ply", false, false, false},
            {"plain.obj", false, false, false},
            {"cube.obj", true, true, true},
        };
        for (const Case& c : cases) {
            ImportedModel own, ai; std::string e1, e2;
            const bool a = ImportModelData(kDir + c.file, own, ImportBackend::Own, false, &e1);
            const bool b = ImportModelData(kDir + c.file, ai, ImportBackend::Assimp, false, &e2);
            if (!a || !b) std::printf("  %s: own=%d (%s) assimp=%d (%s)\n", c.file, a, e1.c_str(), b, e2.c_str());
            CHECK(a && b);
            if (!(a && b)) continue;
            const ModelCounts co = CountModel(own), ca = CountModel(ai);
            if (co.parts != ca.parts || co.vertices != ca.vertices || co.triangles != ca.triangles)
                std::printf("  %s: parts %d/%d vertices %d/%d triangles %d/%d (own/assimp)\n", c.file, co.parts, ca.parts, co.vertices, ca.vertices, co.triangles, ca.triangles);
            CHECK(co.parts == ca.parts);
            CHECK(co.triangles == ca.triangles);
            CHECK(co.vertices == ca.vertices);
            CHECK(co.skinnedParts == 0 && ca.skinnedParts == 0 && co.clips == 0 && ca.clips == 0);
            // The meshes are the same triangles (positions, plus normals / UVs where the file has them).
            const bool same = Triangles(own, c.normals, c.uv, c.flipV) == Triangles(ai, c.normals, c.uv, false);
            if (!same) std::printf("  %s: triangle data differs\n", c.file);
            CHECK(same);
            // Files without normals: Assimp always generates smooth ones; so do our PLY reader (STL files
            // carry facet normals). Our OBJ reader leaves them out (flat shading), like Mesh::LoadOBJ.
            if (!c.normals) {
                const bool ownGenerates = std::string(c.file).find(".ply") != std::string::npos;
                for (const ImportedPart& p : ai.parts) { CHECK(p.mesh.HasNormals()); for (const Vec3& n : p.mesh.normals) CHECK_NEAR(n.Magnitude(), 1.0, 1e-3); }
                for (const ImportedPart& p : own.parts) {
                    CHECK(p.mesh.HasNormals() == ownGenerates);
                    for (const Vec3& n : p.mesh.normals) CHECK_NEAR(n.Magnitude(), 1.0, 1e-3);
                }
            }
        }
        {   // Materials of the OBJ: Assimp reads the same .mtl.
            ImportedModel own, ai;
            CHECK(ImportModelData(kDir + "cube.obj", own, ImportBackend::Own));
            CHECK(ImportModelData(kDir + "cube.obj", ai, ImportBackend::Assimp));
            CHECK(own.parts.size() == 2 && ai.parts.size() == 2);
            if (own.parts.size() == 2 && ai.parts.size() == 2) {
                // Assimp may order its meshes differently: match by diffuse texture.
                for (const ImportedPart& po : own.parts) {
                    const ImportedMaterial& mo = own.materials[(std::size_t)po.material];
                    bool found = false;
                    for (const ImportedPart& pa : ai.parts) {
                        const ImportedMaterial& ma = ai.materials[(std::size_t)pa.material];
                        if (ma.texture == mo.texture) {
                            found = true;
                            CHECK(ma.hasDiffuse == mo.hasDiffuse);
                            CHECK_NEAR(ma.diffuse.r, mo.diffuse.r, 1e-4); CHECK_NEAR(ma.diffuse.b, mo.diffuse.b, 1e-4);
                            CHECK(ma.normalMap == mo.normalMap);
                            CHECK(ma.specularMap == mo.specularMap);
                        }
                    }
                    CHECK(found);
                }
            }
        }
        // The merged one-mesh path agrees whichever backend runs it.
        for (const char* f : {"cube_binary.stl", "cube_le.ply"}) {
            SetImportBackend("*", ImportBackend::Own);
            bool o1 = false; Mesh m1 = ImportModel(kDir + f, &o1);
            SetImportBackend("*", ImportBackend::Assimp);
            bool o2 = false; Mesh m2 = ImportModel(kDir + f, &o2);
            SetImportBackend("*", ImportBackend::Default);
            CHECK(o1 && o2);
            CHECK(m1.TriangleCount() == m2.TriangleCount() && m1.vertices.size() == m2.vertices.size());
        }
        // Skeleton and animation: Assimp's reading of the rig fixtures matches the glTF reader's.
        const std::string rigs = std::string(OKAY_SOURCE_DIR) + "/tests/assets/rigs/";
        for (const char* f : {"fox.glb", "cc_t.glb", "daz_t.glb", "mixamo_t_cm_clips.glb", "mixamo_a_blender_short.glb"}) {
            ImportedModel ai; std::string e;
            if (!ImportModelData(rigs + f, ai, ImportBackend::Assimp, false, &e)) { std::printf("  %s: assimp failed: %s\n", f, e.c_str()); CHECK(false); continue; }
            Scene native; native.physicsEnabled = false; bool nok = false;
            GameObject* nr = ImportModelScene(native, rigs + f, &nok);
            CHECK(nok && nr);
            if (!nr) continue;
            ModelReport rep = AnalyzeModel(native, nr);
            const ModelCounts ca = CountModel(ai);
            std::printf("  %-28s native: meshes %d skinned %d bones %d clips %d | assimp: parts %d skinned %d bones %d clips %d\n",
                        f, rep.meshes, rep.skinnedMeshes, rep.bones, rep.clips, ca.parts, ca.skinnedParts, ca.bones, ca.clips);
            CHECK(ca.clips == rep.clips);
            CHECK(ca.bones == rep.bones);
            CHECK(ca.skinnedParts == rep.skinnedMeshes);
            // And the shared scene builder gives Assimp's result the same shape.
            Scene viaSeam; viaSeam.physicsEnabled = false;
            GameObject* sr = BuildSceneFromModel(viaSeam, ai, rigs + f);
            CHECK(sr != nullptr);
            if (sr) { ModelReport r2 = AnalyzeModel(viaSeam, sr); CHECK(r2.clips == rep.clips && r2.bones == rep.bones); }
        }
    }
#else
    std::printf("  Assimp not built: backend comparison skipped\n");
#endif

    TEST_MAIN_RESULT();
}
