// Our own model readers (OBJ+MTL, STL ascii/binary, PLY ascii/binary) and the
// import seam (okay/Render/ModelData.hpp): fixtures, malformed input, a
// deterministic fuzz sweep (run it under -DOKAY_SANITIZE=address,undefined), the
// backend switch, and, when the build has Assimp, the same files read by both
// backends and compared (counts, triangles, normals, UVs, materials, skeleton, clips).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/ModelData.hpp"
#include "okay/Render/FbxReader.hpp"
#include "okay/Render/Gltf.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <clocale>
#include <cstring>
#include <filesystem>
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

    // A PLY that declares the vertex element twice (the second block with normals) used to
    // come out with hasN set and fewer normals than vertices; it is rejected now.
    {
        const std::string twice =
            "ply\nformat ascii 1.0\n"
            "element vertex 3\nproperty float x\nproperty float y\nproperty float z\n"
            "element vertex 3\nproperty float x\nproperty float y\nproperty float z\n"
            "property float nx\nproperty float ny\nproperty float nz\n"
            "element face 1\nproperty list uchar int vertex_indices\nend_header\n"
            "0 0 0\n1 0 0\n0 1 0\n0 0 1 0 0 1\n1 0 1 0 0 1\n0 1 1 0 0 1\n3 0 1 2\n";
        ImportedModel m; std::string e;
        CHECK(!ReadPlyBuffer(twice, m, &e));
        CHECK(e.find("more than one vertex element") != std::string::npos);
        // The same data with one vertex element is fine, and normals match vertices.
        const std::string once =
            "ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\n"
            "property float nx\nproperty float ny\nproperty float nz\n"
            "element face 1\nproperty list uchar int vertex_indices\nend_header\n"
            "0 0 0 0 0 1\n1 0 0 0 0 1\n0 1 0 0 0 1\n3 0 1 2\n";
        ImportedModel ok;
        CHECK(ReadPlyBuffer(once, ok, &e));
        CHECK(ok.parts[0].mesh.normals.size() == ok.parts[0].mesh.vertices.size());
    }

    // Issue #752. Two ASCII files that declare `element vertex` twice. The first
    // block carries nx/ny/nz and the second does not, so the second block's
    // vertices used to come back with the first block's normals (the reported
    // value was (0,1,0)). The second file gives the blocks different property
    // subsets, which used to concatenate normals and UVs that no longer lined up
    // with positions. Both are a parse error, and a single block of the same
    // data still keeps the authored normal (not the face normal) and its UVs.
    {
        auto rejected = [](const std::string& ply) {
            ImportedModel m; std::string e;
            CHECK(!ReadPlyBuffer(ply, m, &e));
            CHECK(e.find("more than one vertex element") != std::string::npos);
            CHECK(m.parts.empty());
        };
        rejected(
            "ply\nformat ascii 1.0\n"
            "element vertex 2\nproperty float x\nproperty float y\nproperty float z\n"
            "property float nx\nproperty float ny\nproperty float nz\n"
            "element vertex 2\nproperty float x\nproperty float y\nproperty float z\n"
            "element face 1\nproperty list uchar int vertex_indices\nend_header\n"
            "0 0 0 0 1 0\n1 0 0 0 1 0\n0 1 0\n0 0 1\n3 0 1 2\n");
        rejected(
            "ply\nformat ascii 1.0\n"
            "element vertex 2\nproperty float x\nproperty float y\nproperty float z\n"
            "property float nx\nproperty float ny\nproperty float nz\nproperty float s\nproperty float t\n"
            "element vertex 2\nproperty float x\nproperty float y\nproperty float z\n"
            "property float nx\nproperty float ny\nproperty float nz\n"
            "element face 1\nproperty list uchar int vertex_indices\nend_header\n"
            "0 0 0 0 1 0 0 0\n1 0 0 0 1 0 1 0\n0 1 0 0 0 1\n0 0 1 0 0 1\n3 0 1 2\n");
        rejected(
            "ply\nformat ascii 1.0\n"
            "element vertex 2\nproperty float x\nproperty float y\nproperty float z\n"
            "property float nx\nproperty float ny\nproperty float nz\n"
            "element vertex 2\nproperty float x\nproperty float y\nproperty float z\n"
            "property float s\nproperty float t\n"
            "element face 1\nproperty list uchar int vertex_indices\nend_header\n"
            "0 0 0 0 1 0\n1 0 0 0 1 0\n0 1 0 0 0\n0 0 1 1 1\n3 0 1 2\n");
        const std::string single =
            "ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\n"
            "property float nx\nproperty float ny\nproperty float nz\nproperty float s\nproperty float t\n"
            "element face 1\nproperty list uchar int vertex_indices\nend_header\n"
            "0 0 0 0 1 0 0 0\n1 0 0 0 1 0 1 0\n0 1 0 0 1 0 0 1\n3 0 1 2\n";
        ImportedModel one; std::string e;
        CHECK(ReadPlyBuffer(single, one, &e));
        if (!one.parts.empty()) {
            const Mesh& me = one.parts[0].mesh;
            CHECK(me.vertices.size() == 3 && me.normals.size() == 3 && me.uvs.size() == 3);
            for (const Vec3& n : me.normals) { CHECK_NEAR(n.x, 0.0, 1e-6); CHECK_NEAR(n.y, 1.0, 1e-6); CHECK_NEAR(n.z, 0.0, 1e-6); }
            CHECK_NEAR(me.uvs[0].y, 1.0, 1e-6);
            CHECK_NEAR(me.uvs[2].x, 0.0, 1e-6); CHECK_NEAR(me.uvs[2].y, 0.0, 1e-6);
        }
        // The same bytes through ImportModelScene (the path the report exercised).
        SetImportBackend(".ply", ImportBackend::Own);
        Spit("mr_dup_vertex.ply",
             "ply\nformat ascii 1.0\n"
             "element vertex 2\nproperty float x\nproperty float y\nproperty float z\n"
             "property float nx\nproperty float ny\nproperty float nz\n"
             "element vertex 2\nproperty float x\nproperty float y\nproperty float z\n"
             "element face 1\nproperty list uchar int vertex_indices\nend_header\n"
             "0 0 0 0 1 0\n1 0 0 0 1 0\n0 1 0\n0 0 1\n3 0 1 2\n");
        Scene dup; dup.physicsEnabled = false;
        bool dok = true; GameObject* dr = ImportModelScene(dup, "mr_dup_vertex.ply", &dok);
        CHECK(!dok && dr == nullptr);
        int meshes = 0; for (const auto& go : dup.Objects()) if (go->GetComponent<MeshRenderer>()) ++meshes;
        CHECK(meshes == 0);
        // Assimp loads this file. The seam must still refuse it, including when
        // Assimp is the backend the default choice would have used.
        SetImportBackend(".ply", ImportBackend::Default);
        Scene dupAssimp; dupAssimp.physicsEnabled = false;
        bool aok = true; GameObject* ar = ImportModelScene(dupAssimp, "mr_dup_vertex.ply", &aok);
        CHECK(!aok && ar == nullptr);
        ImportedModel via; std::string ae;
        CHECK(!ImportModelData("mr_dup_vertex.ply", via, ImportBackend::Assimp, false, &ae));
        CHECK(ae.find("more than one vertex element") != std::string::npos);
        CHECK(via.parts.empty());
        std::remove("mr_dup_vertex.ply");
        SetImportBackend(".ply", ImportBackend::Default);
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
        // #753: a texture that climbs out of the model's directory (or is absolute) must not be
        // kept: an untrusted model must not point the engine at any file on the machine. In-tree
        // ".."s still resolve. Scratch files in the current directory (the build dir), like above.
        {
            Spit("mr_trav.obj", "mtllib mr_trav.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl climb\nf 1 2 3\n");
            Spit("mr_trav.mtl", "newmtl climb\nmap_Kd ../../../../../../etc/passwd\n");
            ImportedModel t1;
            CHECK(ReadObjFile("mr_trav.obj", t1, &err));
            CHECK(t1.materials.size() == 1 && t1.materials[0].texture.empty());   // dropped, not stored
            Spit("mr_abs.obj", "mtllib mr_abs.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl a\nf 1 2 3\n");
            Spit("mr_abs.mtl", "newmtl a\nmap_Kd /etc/hostname\n");
            ImportedModel t3;
            CHECK(ReadObjFile("mr_abs.obj", t3, &err));
            CHECK(t3.materials.size() == 1 && t3.materials[0].texture.empty());   // absolute path dropped (#753)
            std::remove("mr_abs.obj"); std::remove("mr_abs.mtl");
            Spit("mr_inside.obj", "mtllib mr_inside.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl ok\nf 1 2 3\n");
            Spit("mr_inside.mtl", "newmtl ok\nmap_Kd ./sub/../mr_inside.png\n");
            ImportedModel t2;
            CHECK(ReadObjFile("mr_inside.obj", t2, &err));
            CHECK(t2.materials.size() == 1 && t2.materials[0].texture == "mr_inside.png");   // in-tree ".." still resolves
            std::remove("mr_trav.obj"); std::remove("mr_trav.mtl");
            std::remove("mr_inside.obj"); std::remove("mr_inside.mtl");
        }
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

    // ---------------- Found by the real-file corpus (docs/model_corpus.md) ----------------
    {
        // An L-shaped (concave) hexagon in the XY plane, area 3, wound counter-clockwise: a fan from the first corner covers
        // the notch (area 3.5 or more) or flips triangles; ear clipping gives area 3 with every triangle facing +Z.
        const char* lx[6] = {"0 0 0", "2 0 0", "2 1 0", "1 1 0", "1 2 0", "0 2 0"};
        auto planarArea = [](const Mesh& me, bool& allUp) {
            double a = 0; allUp = true;
            for (std::size_t i = 0; i + 2 < me.triangles.size(); i += 3) {
                const Vec3& p0 = me.vertices[(std::size_t)me.triangles[i]]; const Vec3& p1 = me.vertices[(std::size_t)me.triangles[i + 1]]; const Vec3& p2 = me.vertices[(std::size_t)me.triangles[i + 2]];
                const double z = (p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x);
                if (z <= 0) allUp = false;
                a += 0.5 * z;
            }
            return a;
        };
        std::string obj; for (const char* v : lx) obj += std::string("v ") + v + "\n"; obj += "f 1 2 3 4 5 6\n";
        ImportedModel om; std::string err; bool up = false;
        CHECK(ReadObjBuffer(obj, "", om, &err) && om.parts.size() == 1);
        if (!om.parts.empty()) { CHECK(om.parts[0].mesh.TriangleCount() == 4); CHECK_NEAR(planarArea(om.parts[0].mesh, up), 3.0, 1e-5); CHECK(up); }
        // a convex quad is still fanned from its first corner (same triangles as before)
        ImportedModel qm; CHECK(ReadObjBuffer("v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf 1 2 3 4\n", "", qm, &err));
        if (!qm.parts.empty()) CHECK(qm.parts[0].mesh.triangles == (std::vector<int>{0, 1, 2, 0, 2, 3}));
        std::string ply = "ply\nformat ascii 1.0\nCreated by an exporter that writes free text here\nelement vertex 6\nproperty float x\nproperty float y\nproperty float z\nelement face 1\nproperty list uchar int vertex_indices\nend_header\n";
        for (const char* v : lx) ply += std::string(v) + "\n";
        ply += "6 0 1 2 3 4 5\n";
        ImportedModel pm2;
        CHECK(ReadPlyBuffer(ply, pm2, &err));   // the free-text header line is ignored (it used to fail the whole file)
        if (!pm2.parts.empty()) { CHECK(pm2.parts[0].mesh.TriangleCount() == 4); CHECK_NEAR(planarArea(pm2.parts[0].mesh, up), 3.0, 1e-5); CHECK(up); }

        // glTF primitive modes and bad indices: positions 0..4 on a line of unit steps in X with Y alternating, indices u16
        auto b64 = [](const std::string& in) {
            static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string o; std::size_t i = 0;
            for (; i + 2 < in.size(); i += 3) { unsigned v = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8) | (unsigned char)in[i + 2]; o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += t[v & 63]; }
            if (i + 1 == in.size()) { unsigned v = (unsigned char)in[i] << 16; o += t[v >> 18]; o += t[(v >> 12) & 63]; o += "=="; }
            else if (i + 2 == in.size()) { unsigned v = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8); o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += '='; }
            return o;
        };
        auto gltfJson = [&](int mode, const std::vector<std::uint16_t>& idx, bool useIdx, int nPos, const char* extra) {
            std::string buf;
            for (int i = 0; i < nPos; ++i) { float f[3] = {(float)i, (i & 1) ? 1.0f : 0.0f, 0.0f}; buf.append((const char*)f, 12); }
            const std::size_t posBytes = buf.size();
            for (std::uint16_t v : idx) buf.append((const char*)&v, 2);
            while (buf.size() % 4) buf.push_back('\0');
            return "{\"asset\":{\"version\":\"2.0\"}," + std::string(extra) + "\"buffers\":[{\"byteLength\":" + std::to_string(buf.size()) + ",\"uri\":\"data:application/octet-stream;base64," + b64(buf) + "\"}],"
                "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":" + std::to_string(posBytes) + "},{\"buffer\":0,\"byteOffset\":" + std::to_string(posBytes) + ",\"byteLength\":" + std::to_string(idx.size() * 2) + "}],"
                "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":" + std::to_string(nPos) + ",\"type\":\"VEC3\"},{\"bufferView\":1,\"componentType\":5123,\"count\":" + std::to_string(idx.size()) + ",\"type\":\"SCALAR\"}],"
                "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}," + (useIdx ? "\"indices\":1," : "") + "\"mode\":" + std::to_string(mode) + "}]}],\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}]}";
        };
        auto gltfTris = [&](int mode, const std::vector<std::uint16_t>& idx, bool useIdx, int nPos, int* trisOut) {
            Spit("mr_mode.gltf", gltfJson(mode, idx, useIdx, nPos, ""));
            bool ok = false; Mesh m = LoadGLTF("mr_mode.gltf", &ok);
            std::remove("mr_mode.gltf");
            *trisOut = m.TriangleCount();
            return ok;
        };
        auto sceneOk = [&](const std::string& json) {
            Spit("mr_scene.gltf", json);
            Scene sc; sc.physicsEnabled = false; bool ok = true;
            GameObject* r = ImportModelScene(sc, "mr_scene.gltf", &ok);
            std::remove("mr_scene.gltf");
            return ok && r != nullptr;
        };
        int n = -1;
        CHECK(gltfTris(4, {0, 1, 2, 3, 4}, true, 5, &n) && n == 1);                 // TRIANGLES: the two trailing indices make no triangle
        CHECK(gltfTris(5, {0, 1, 2, 3, 4}, true, 5, &n) && n == 3);                 // TRIANGLE_STRIP: 5 indices, 3 triangles
        CHECK(gltfTris(6, {0, 1, 2, 3, 4}, true, 5, &n) && n == 3);                 // TRIANGLE_FAN
        CHECK(gltfTris(5, {}, false, 5, &n) && n == 3);                             // strip without indices
        CHECK(!gltfTris(1, {0, 1, 2, 3}, true, 5, &n) || n == 0);                   // LINES: no surface, nothing drawn
        CHECK(!gltfTris(0, {}, false, 5, &n) || n == 0);                            // POINTS
        CHECK(gltfTris(4, {0, 1, 2, 0, 1, 9}, true, 5, &n) && n == 1);              // one triangle with an index past the vertices is dropped, the other stays
        CHECK(gltfTris(5, {0, 1, 1, 2, 3}, true, 5, &n) && n == 1);                 // strip: the two triangles with a repeated index are dropped, (1,2,3) stays
        // a file whose meshes cannot be read fails the import; it does not give an empty model
        CHECK(sceneOk(gltfJson(4, {0, 1, 2}, true, 3, "")));
        { std::string j = gltfJson(4, {0, 1, 2}, true, 3, ""); j.replace(j.find("\"2.0\""), 5, "\"1.0\""); CHECK(!sceneOk(j)); }   // glTF 1.0: refused with a message, not read as garbage
        CHECK(!sceneOk(gltfJson(1, {0, 1, 2}, true, 3, "")));                                         // lines only
        CHECK(!sceneOk(gltfJson(4, {0, 1, 2}, true, 3, "\"extensionsRequired\":[\"KHR_draco_mesh_compression\"],")));   // compressed geometry we cannot decode
        { std::string j = gltfJson(4, {0, 1, 2}, true, 3, ""); const std::size_t u = j.find("data:application/octet-stream;base64,"); j.replace(u, j.find('"', u) - u, "nothere.bin"); CHECK(!sceneOk(j)); }   // missing .bin
        // random modes, index lists (some past the vertices) and vertex counts: never a triangle that reads outside the mesh
        unsigned seed = 12345u;
        auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
        for (int it = 0; it < 300; ++it) {
            const int nPos = 1 + (int)(rnd() % 7), nIdx = (int)(rnd() % 12), mode = (int)(rnd() % 9) - 1;
            std::vector<std::uint16_t> ix; for (int i = 0; i < nIdx; ++i) ix.push_back((std::uint16_t)(rnd() % (unsigned)(nPos + 3)));
            const bool useIdx = (rnd() & 1) != 0 && !ix.empty();
            int tris = -1; const bool ok = gltfTris(mode, ix, useIdx || ix.empty() ? useIdx : false, nPos, &tris);
            CHECK(!ok || (tris >= 0 && tris <= std::max(nPos, nIdx)));
        }
    }

    // ---------------- Non-ASCII directory and file names (paths are UTF-8 strings) ----------------
    {
        // On Windows a narrow fopen() / ifstream path goes through the ANSI code page, so a folder such as "Müller" or a
        // Japanese file name failed to open. Copy the fixtures under such names and read each format by path.
        namespace fsx = std::filesystem;
        const std::string uni = "okay_m\xC3\xBCller_\xE6\x97\xA5\xE6\x9C\xAC";   // "okay_müller_日本"
        std::error_code ec;
        const fsx::path dir = fsx::temp_directory_path(ec) / fsx::u8path(uni);
        fsx::remove_all(dir, ec); fsx::create_directories(dir, ec);
        const std::string d8 = dir.u8string() + "/";
        struct F { const char* src; const char* dst; };
        const F files[] = {{"cube.obj", "w\xC3\xBCrfel.obj"}, {"cube.mtl", "cube.mtl"}, {"cube_ascii.stl", "\xE7\xAB\x8B\xE6\x96\xB9.stl"}, {"cube_ascii.ply", "cube.ply"}, {"cube_le.ply", "cube_le.ply"},
                           {"walk.bvh", "walk.bvh"}, {"zup_cm.dae", "z\xC3\xBCp.dae"}, {"cube.3ds", "cube.3ds"}};
        for (const F& f : files) { std::ofstream o(fsx::u8path(d8 + f.dst), std::ios::binary); const std::string b = Slurp(kDir + f.src); o.write(b.data(), (std::streamsize)b.size()); }
        { std::ofstream o(fsx::u8path(d8 + "rig.fbx"), std::ios::binary); const std::string b = Slurp(std::string(OKAY_SOURCE_DIR) + "/tests/assets/fbx/cube_zup_cm_bin.fbx"); o.write(b.data(), (std::streamsize)b.size()); }
        SetImportBackend(".dae", ImportBackend::Own); SetImportBackend(".bvh", ImportBackend::Own); SetImportBackend(".3ds", ImportBackend::Own);
        for (const F& f : files) {
            const std::string name = f.dst;
            if (name.size() < 4 || name.substr(name.size() - 4) == ".mtl") continue;
            ImportedModel m; std::string e;
            const bool ok = ImportModelData(d8 + name, m, ImportBackend::Own, false, &e);
            if (!ok) std::printf("  %s: %s\n", name.c_str(), e.c_str());
            CHECK(ok);
        }
        if (fsx::exists(fsx::u8path(d8 + "cube.mtl"), ec)) { ImportedModel m; std::string e; CHECK(ReadObjFile(d8 + "w\xC3\xBCrfel.obj", m, &e) && !m.materials.empty()); }   // the .mtl beside it is found too
        fbx::Scene fs_; std::string fe;
        CHECK(fbx::LoadFile(d8 + "rig.fbx", fs_, &fe));
        { bool ok = false; Mesh mm = ImportModel(d8 + "rig.fbx", &ok); CHECK(ok && !mm.vertices.empty()); }
        fsx::remove_all(dir, ec);
    }

    // ---------------- Locale: a decimal comma must not change what a file reads as ----------------
    {
        // setlocale(LC_NUMERIC) in a German / French locale makes strtod stop at the '.' of "1.5". The readers use their own
        // point-safe parse, so every file reads the same. Needs such a locale on the machine (LOCPATH on Linux, or Windows'
        // own names); otherwise this part says so and passes.
        auto fingerprint = [](const ImportedModel& m) {
            std::string f; char b[96];
            for (const ImportedNode& n : m.nodes) { std::snprintf(b, sizeof b, "n %.6g %.6g %.6g|%.6g %.6g %.6g %.6g|%.6g;", n.position.x, n.position.y, n.position.z, n.rotation.x, n.rotation.y, n.rotation.z, n.rotation.w, n.scale.x); f += b; }
            for (const ImportedPart& p : m.parts) { for (const Vec3& v : p.mesh.vertices) { std::snprintf(b, sizeof b, "v %.6g %.6g %.6g;", v.x, v.y, v.z); f += b; } for (const Vec3& v : p.mesh.normals) { std::snprintf(b, sizeof b, "w %.5g %.5g %.5g;", v.x, v.y, v.z); f += b; } }
            for (const ImportedMaterial& mt : m.materials) { std::snprintf(b, sizeof b, "m %.6g %.6g %.6g %.6g;", mt.diffuse.r, mt.diffuse.g, mt.diffuse.b, mt.shininess); f += b; }
            for (const ImportedClip& c : m.clips) for (const ImportedChannel& ch : c.channels) { for (const auto& k : ch.position) { std::snprintf(b, sizeof b, "p %.6g %.6g;", k.time, k.value.x); f += b; } for (const auto& k : ch.rotation) { std::snprintf(b, sizeof b, "r %.6g %.6g;", k.time, k.value.w); f += b; } }
            return f;
        };
        struct Case { const char* file; const char* ext; };
        const Case cases[] = {{"cube.obj", ".obj"}, {"cube_ascii.stl", ".stl"}, {"cube_ascii.ply", ".ply"}, {"walk.bvh", ".bvh"}, {"zup_cm.dae", ".dae"}, {"two_materials.dae", ".dae"}};
        std::string ref[6]; bool refOk[6];
        SetImportBackend(".dae", ImportBackend::Own); SetImportBackend(".bvh", ImportBackend::Own);
        for (int i = 0; i < 6; ++i) { ImportedModel m; std::string e; refOk[i] = ImportModelData(kDir + cases[i].file, m, ImportBackend::Own, false, &e); ref[i] = fingerprint(m); CHECK(refOk[i]); }
        const std::string fbxPath = std::string(OKAY_SOURCE_DIR) + "/tests/assets/fbx/rig_ascii.fbx";
        fbx::Scene fRef; std::string fe; CHECK(fbx::LoadFile(fbxPath, fRef, &fe));
        const std::string savedLocale = std::setlocale(LC_NUMERIC, nullptr) ? std::setlocale(LC_NUMERIC, nullptr) : "C";
        const char* names[] = {"de_DE.UTF-8", "de_DE.utf8", "de_DE", "fr_FR.UTF-8", "fr_FR", "German_Germany.1252", "German_Germany", "German", "French_France.1252"};
        std::string commaLocale;
        for (const char* n : names) if (std::setlocale(LC_NUMERIC, n) && std::localeconv()->decimal_point[0] == ',') { commaLocale = n; std::printf("  locale '%s' (decimal point '%s')\n", n, std::localeconv()->decimal_point); break; }
        std::setlocale(LC_NUMERIC, savedLocale.c_str());
        if (commaLocale.empty()) std::printf("  no decimal-comma locale on this machine: locale check skipped\n");
        else {
            for (int i = 0; i < 6; ++i) {
                ImportedModel m; std::string e;
                std::setlocale(LC_NUMERIC, commaLocale.c_str());   // read under the comma locale, print (fingerprint) under the saved one
                const bool ok = ImportModelData(kDir + cases[i].file, m, ImportBackend::Own, false, &e);
                std::setlocale(LC_NUMERIC, savedLocale.c_str());
                CHECK(ok == refOk[i]);
                if (!(fingerprint(m) == ref[i])) std::printf("  %s reads differently under a decimal-comma locale\n", cases[i].file);
                CHECK(fingerprint(m) == ref[i]);
            }
            std::setlocale(LC_NUMERIC, commaLocale.c_str());
            fbx::Scene fLoc; CHECK(fbx::LoadFile(fbxPath, fLoc, &fe));
            CHECK(fLoc.nodes.size() == fRef.nodes.size());
            for (std::size_t i = 0; i < std::min(fLoc.nodes.size(), fRef.nodes.size()); ++i) for (int k = 0; k < 3; ++k) CHECK_NEAR(fLoc.nodes[i].translation[k], fRef.nodes[i].translation[k], 1e-9);
            CHECK(fLoc.clips.size() == fRef.clips.size());
            // text with a '.' next to the end of a number: "1.5" and "1.5e1" read as 1.5 and 15
            ImportedModel q; std::string e;
            CHECK(ReadObjBuffer("v 1.5 2.5e1 -3.25\nv 0 0 0\nv 1 0 0\nf 1 2 3\n", "", q, &e) && !q.parts.empty());
            if (!q.parts.empty() && q.parts[0].mesh.vertices.size() >= 1) { CHECK_NEAR(q.parts[0].mesh.vertices[0].x, 1.5, 1e-6); CHECK_NEAR(q.parts[0].mesh.vertices[0].y, 25.0, 1e-5); CHECK_NEAR(q.parts[0].mesh.vertices[0].z, -3.25, 1e-6); }
        }
        std::setlocale(LC_NUMERIC, savedLocale.c_str());
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
