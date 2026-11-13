// Our own Blender (.blend) reader: the synthetic fixtures from scripts/gen_blend_fixtures.py (tests/data/models/*.blend),
// checked against values computed by hand, plus the import seam (opt-in switch, merged mesh, scene import).
// The fixtures are NOT Blender output (see the script); the real-file evidence is in docs/own_blend_reader.md and
// tests/test_model_corpus.cpp (OKAY_MODEL_CORPUS).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace okay;

static const std::string kDir = std::string(OKAY_SOURCE_DIR) + "/tests/data/models/";

static std::string Slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
static bool Near(const Vec3& a, const Vec3& b, float e = 1e-4f) {
    return std::fabs(a.x - b.x) < e && std::fabs(a.y - b.y) < e && std::fabs(a.z - b.z) < e;
}
static bool HasNote(const ImportedModel& m, const char* part) {
    for (const std::string& n : m.notes) if (n.find(part) != std::string::npos) return true;
    return false;
}
static Mat4 WorldOf(const ImportedModel& m, int node) {
    Mat4 w = Mat4::Identity();
    std::vector<int> chain;
    for (int i = node; i >= 0; i = m.nodes[(std::size_t)i].parent) chain.push_back(i);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const ImportedNode& n = m.nodes[(std::size_t)*it];
        w = w * Mat4::TRS(n.position, n.rotation, n.scale);
    }
    return w;
}
static Vec3 Apply(const Mat4& w, const Vec3& v) { const Vec4 r = w * Vec4{v.x, v.y, v.z, 1.0f}; return Vec3{r.x, r.y, r.z}; }
static double Area(const Mesh& m) {
    double a = 0;
    for (std::size_t i = 0; i + 2 < m.triangles.size(); i += 3) {
        const Vec3 p = m.vertices[(std::size_t)m.triangles[i]], q = m.vertices[(std::size_t)m.triangles[i + 1]], r = m.vertices[(std::size_t)m.triangles[i + 2]];
        const Vec3 e1 = q - p, e2 = r - p;
        const Vec3 c{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
        a += 0.5 * std::sqrt((double)c.x * c.x + (double)c.y * c.y + (double)c.z * c.z);
    }
    return a;
}
static int FindNode(const ImportedModel& m, const char* name) {
    for (std::size_t i = 0; i < m.nodes.size(); ++i) if (m.nodes[i].name == name) return (int)i;
    return -1;
}

// the attribute-storage fixture in all its encodings must read the same
static void CheckAttr(const char* file, const ImportedModel& ref, ImportedModel* keep = nullptr) {
    std::string err;
    ImportedModel m;
    CHECK(ReadBlendFile(kDir + file, m, &err));
    if (!err.empty()) std::printf("  %s: %s\n", file, err.c_str());
    CHECK(m.backend == "own");
    CHECK(m.nodes.size() == 3 && m.parts.size() == 2 && m.materials.size() == 2);
    CHECK(FindNode(m, "Orphan") < 0);   // not in any collection of the scene
    CHECK(FindNode(m, "Parent") == 1 && FindNode(m, "Child") == 2);
    CHECK(m.nodes.size() == 3 && m.nodes[2].parent == 1 && m.nodes[1].parent == 0);
    if (m.nodes.size() == 3) {
        // root: Z up -> Y up
        CHECK(Near(Apply(WorldOf(m, 0), Vec3{0, 0, 1}), Vec3{0, 1, 0}));
        CHECK(Near(Apply(WorldOf(m, 0), Vec3{0, 1, 0}), Vec3{0, 0, -1}));
        // Parent: loc (1,2,3), 90 degrees about Z, scale (2,1,1): the local point (1,0,0) lands on (1,4,3)... = (1 + 0, 2 + 2, 3)
        const Mat4 pw = WorldOf(m, 1);
        const Mat4 inv = WorldOf(m, 0).Inverse();
        CHECK(Near(Apply(inv * pw, Vec3{1, 0, 0}), Vec3{1, 4, 3}));
        CHECK(Near(Apply(inv * pw, Vec3{0, 1, 0}), Vec3{0, 2, 3}));
        // Child: parent inverse T(-1,-2,-3) then loc (0,0,1): its origin is the parent's matrix applied to (-1,-2,-2)
        CHECK(Near(m.nodes[2].position, Vec3{-1, -2, -2}));
        CHECK(Near(Apply(inv * WorldOf(m, 2), Vec3{0, 0, 0}), Apply(inv * pw, Vec3{-1, -2, -2})));
    }
    if (m.parts.size() == 2) {
        // part 0: material 0 = quad (area 1) + concave L hexagon (area 3, ear clipped into 4 triangles) = 6 triangles
        CHECK(m.parts[0].material == 0 && m.parts[0].mesh.triangles.size() == 18);
        CHECK(std::fabs(Area(m.parts[0].mesh) - 4.0) < 1e-5);
        CHECK(m.parts[1].material == 1 && m.parts[1].mesh.triangles.size() == 3);
        CHECK(std::fabs(Area(m.parts[1].mesh) - 0.5) < 1e-6);
        CHECK(m.parts[0].node == 1 && m.parts[1].node == 1);
        // flat triangle (material 1): normals all (0,0,1); UV (u, 1 - v): (1,0) -> (1,1)
        const Mesh& t = m.parts[1].mesh;
        CHECK(t.vertices.size() == 3 && t.normals.size() == 3 && t.uvs.size() == 3);
        bool nz = true, uvOk = false;
        for (std::size_t i = 0; i < t.vertices.size(); ++i) {
            if (!Near(t.normals[i], Vec3{0, 0, 1}, 1e-5f)) nz = false;
            if (Near(t.vertices[i], Vec3{2, 0, 0}) && std::fabs(t.uvs[i].x - 1.0f) < 1e-6f && std::fabs(t.uvs[i].y - 1.0f) < 1e-6f) uvOk = true;
        }
        CHECK(nz && uvOk);
        // the concave hexagon: every output triangle must face +Z (ear clipping keeps the winding)
        const Mesh& q = m.parts[0].mesh;
        bool up = true;
        for (std::size_t i = 0; i + 2 < q.triangles.size(); i += 3) {
            const Vec3 p = q.vertices[(std::size_t)q.triangles[i]], a = q.vertices[(std::size_t)q.triangles[i + 1]], b = q.vertices[(std::size_t)q.triangles[i + 2]];
            const Vec3 e1 = a - p, e2 = b - p;
            if (e1.x * e2.y - e1.y * e2.x <= 0) up = false;
        }
        CHECK(up);
    }
    if (m.materials.size() == 2) {
        const ImportedMaterial& a = m.materials[0];
        CHECK(a.name == "Mat0" && a.hasDiffuse);
        CHECK(std::fabs(a.diffuse.r - 0.1f) < 1e-6f && std::fabs(a.diffuse.g - 0.2f) < 1e-6f && std::fabs(a.diffuse.b - 0.3f) < 1e-6f);
        CHECK(a.texture == kDir + "tex/diffuse.png");   // "//tex/diffuse.png" is relative to the .blend
        CHECK(std::fabs(a.metallic - 0.5f) < 1e-6f && std::fabs(a.roughness - 0.25f) < 1e-6f);
        const ImportedMaterial& b = m.materials[1];
        CHECK(b.name == "Mat1" && std::fabs(b.diffuse.r - 1.0f) < 1e-6f && b.diffuse.g == 0.0f && b.texture.empty());
    }
    CHECK(HasNote(m, "vertex colours"));         // the colour layer is named, not imported
    CHECK(!HasNote(m, "differs from the matrix"));   // the saved world matrices agree with the computed ones
    if (!ref.parts.empty() && m.parts.size() == ref.parts.size()) {
        for (std::size_t p = 0; p < m.parts.size(); ++p) {
            CHECK(m.parts[p].mesh.vertices.size() == ref.parts[p].mesh.vertices.size() && m.parts[p].mesh.triangles == ref.parts[p].mesh.triangles);
            bool same = m.parts[p].mesh.vertices.size() == ref.parts[p].mesh.vertices.size();
            for (std::size_t i = 0; same && i < m.parts[p].mesh.vertices.size(); ++i) same = Near(m.parts[p].mesh.vertices[i], ref.parts[p].mesh.vertices[i], 0.0f) ;
            CHECK(same);
        }
    }
    if (keep) *keep = m;
}

int main() {
    RUN_SUITE("blend_reader");
    std::string err;

    // ---------------- attribute storage (3.6 / 4.1 naming), node material, collections, parenting ----------------
    ImportedModel ref;
    CheckAttr("blend_attr_41.blend", ImportedModel{}, &ref);
    CheckAttr("blend_attr_36.blend", ref);        // 3.6 field names (totvert / vdata / poly_offset_indices)
    CheckAttr("blend_attr_be32.blend", ref);      // big endian, 32 bit pointers
    CheckAttr("blend_attr_41_gz.blend", ref);     // gzip

    // ---------------- flattened ----------------
    {
        ImportedModel m;
        CHECK(ReadBlendFile(kDir + "blend_attr_41.blend", m, &err, true));
        CHECK(m.nodes.size() == 1 && m.clips.empty() && m.parts.size() == 2);
        // object-space (0,0,0) -> Blender world (1,2,3) -> Y up (1,3,-2)
        bool found = false;
        for (const ImportedPart& p : m.parts) for (const Vec3& v : p.mesh.vertices) if (Near(v, Vec3{1, 3, -2})) found = true;
        CHECK(found);
        CHECK(std::fabs(Area(m.parts[0].mesh) + Area(m.parts[1].mesh) - 9.0) < 1e-4);   // scale (2,1,1) doubles the area
    }

    // ---------------- legacy storage, armature, vertex groups, action ----------------
    {
        ImportedModel m;
        CHECK(ReadBlendFile(kDir + "blend_skin_anim.blend", m, &err));
        CHECK(m.backend == "own");
        CHECK(m.nodes.size() == 5 && m.parts.size() == 1);
        CHECK(FindNode(m, "Rig") == 1 && FindNode(m, "Root") == 2 && FindNode(m, "Child") == 3 && FindNode(m, "Strip") == 4);
        if (m.nodes.size() == 5) {
            CHECK(m.nodes[3].parent == 2 && m.nodes[2].parent == 1 && m.nodes[4].parent == 1);
            CHECK(Near(m.nodes[3].position, Vec3{0, 1, 0}));   // the Child bone sits 1 along its parent's Y axis
        }
        if (m.parts.size() == 1) {
            const ImportedPart& p = m.parts[0];
            CHECK(p.mesh.vertices.size() == 6 && p.mesh.triangles.size() == 12 && std::fabs(Area(p.mesh) - 2.0) < 1e-6);
            CHECK(p.node == 4 && p.bones.size() == 2 && p.bones[0].name == "Root" && p.bones[1].name == "Child");
            CHECK(p.jointIdx.size() == 6 && p.jointWt.size() == 6);
            if (p.bones.size() == 2) {
                // inverse bind: world (0,0,2) is 1 along the Child bone's Y axis; world (0,0,1) is 1 along Root's
                CHECK(Near(Apply(p.bones[1].inverseBind, Vec3{0, 0, 2}), Vec3{0, 1, 0}));
                CHECK(Near(Apply(p.bones[0].inverseBind, Vec3{0, 0, 1}), Vec3{0, 1, 0}));
            }
            for (std::size_t i = 0; i < p.mesh.vertices.size() && i < p.jointWt.size(); ++i) {
                const float z = p.mesh.vertices[i].z;   // Blender z
                float sum = 0; for (float w : p.jointWt[i]) sum += w;
                CHECK(std::fabs(sum - 1.0f) < 1e-5f);
                const int root = p.jointIdx[i][0] == 0 ? 0 : -1;
                if (std::fabs(z) < 1e-6f) CHECK(root == 0 && std::fabs(p.jointWt[i][0] - 1.0f) < 1e-6f);
                if (std::fabs(z - 1.0f) < 1e-6f) CHECK(std::fabs(p.jointWt[i][0] - 0.5f) < 1e-6f && std::fabs(p.jointWt[i][1] - 0.5f) < 1e-6f);
                if (std::fabs(z - 2.0f) < 1e-6f) CHECK(p.jointIdx[i][0] == 1 && std::fabs(p.jointWt[i][0] - 1.0f) < 1e-6f);
            }
            // UVs: u from x, v flipped from z / 2
            CHECK(p.mesh.uvs.size() == 6);
        }
        CHECK(m.clips.size() == 1 && m.clips[0].name == "Walk");
        if (m.clips.size() == 1) {
            const ImportedClip& c = m.clips[0];
            CHECK(c.channels.size() == 2);
            const ImportedChannel* rig = nullptr; const ImportedChannel* child = nullptr;
            for (const ImportedChannel& ch : c.channels) { if (ch.node == "Rig") rig = &ch; if (ch.node == "Child") child = &ch; }
            CHECK(rig && child);
            if (rig) {
                // frames 1..11 at 24 fps, one key per whole frame (a bezier curve); z = 10 (3 t^2 - 2 t^3), t = (f - 1) / 10
                CHECK(rig->position.size() == 11);
                if (rig->position.size() == 11) {
                    CHECK(std::fabs(rig->position[0].time) < 1e-6f && std::fabs(rig->position[10].time - 10.0f / 24.0f) < 1e-5f);
                    CHECK(std::fabs(rig->position[2].value.z - 1.04f) < 1e-3f);    // frame 3
                    CHECK(std::fabs(rig->position[5].value.z - 5.0f) < 1e-3f);     // frame 6
                    CHECK(std::fabs(rig->position[10].value.z - 10.0f) < 1e-4f);
                }
            }
            if (child) {
                CHECK(child->rotation.size() == 2 && child->position.size() == 2);
                if (child->rotation.size() == 2) {
                    const Quat& a = child->rotation[0].value; const Quat& b = child->rotation[1].value;
                    CHECK(std::fabs(a.w - 1.0f) < 1e-5f && std::fabs(a.x) < 1e-5f);
                    CHECK(std::fabs(b.x - std::sqrt(0.5f)) < 1e-4f && std::fabs(b.w - std::sqrt(0.5f)) < 1e-4f && std::fabs(b.y) < 1e-5f && std::fabs(b.z) < 1e-5f);
                    CHECK(Near(child->position[1].value, Vec3{0, 1, 0}));
                }
            }
        }
        CHECK(HasNote(m, "data.lens"));   // the F-Curve on a camera property is named, not dropped silently
        ImportedModel f;
        CHECK(ReadBlendFile(kDir + "blend_skin_anim.blend", f, &err, true));
        CHECK(f.nodes.size() == 1 && f.clips.empty() && f.parts.size() == 1 && f.parts[0].bones.empty());
        bool top = false;   // (x, y, z) = (0.5, 0, 2) in Blender -> (0.5, 2, 0) in Y up
        for (const Vec3& v : f.parts[0].mesh.vertices) if (Near(v, Vec3{0.5f, 2.0f, 0.0f})) top = true;
        CHECK(top);
    }

    // ---------------- container errors, each named ----------------
    {
        ImportedModel m;
        CHECK(!ReadBlendBuffer("", "", m, &err) && err.find("not a .blend") != std::string::npos);
        CHECK(!ReadBlendBuffer("this is not a blender file at all", "", m, &err) && err.find("not a .blend") != std::string::npos);
        CHECK(!ReadBlendBuffer(std::string("\x28\xb5\x2f\xfd", 4) + std::string(64, '\0'), "", m, &err) && err.find("zstd") != std::string::npos);
        CHECK(!ReadBlendBuffer("BLENDER17-01v0500" + std::string(200, '\0'), "", m, &err) && err.find("5.0") != std::string::npos);
        CHECK(!ReadBlendBuffer(std::string("\x1f\x8b\x08\x00\0\0\0\0\0\0garbage", 17), "", m, &err) && err.find("gzip") != std::string::npos);
        const std::string good = Slurp(kDir + "blend_attr_41.blend");
        CHECK(!ReadBlendBuffer(good.substr(0, good.size() / 2), "", m, &err) && !err.empty());   // before the DNA block
        CHECK(!ReadBlendFile(kDir + "does_not_exist.blend", m, &err) && err == "cannot open file");
        for (std::size_t cut : {(std::size_t)5, (std::size_t)12, (std::size_t)40, good.size() - 1}) {
            ImportedModel t; std::string e;
            ReadBlendBuffer(good.substr(0, cut), "", t, &e);   // must not crash; either way the result is consistent
            CHECK(t.parts.empty() || !t.parts[0].mesh.vertices.empty());
        }
    }

    // ---------------- the seam ----------------
    {
        CHECK(HasOwnReader(".blend") && HasOwnReader(".BLEND"));
        unsetenv("OKAY_OWN_BLEND");
        SetImportBackend(".blend", ImportBackend::Default);
        ImportedModel m;
        if (!AssimpAvailable()) {
            if (EffectiveImportBackend(".blend") == ImportBackend::Default) {   // not built with -DOKAY_OWN_BLEND=ON
                CHECK(!ImportModelData(kDir + "blend_attr_41.blend", m, ImportBackend::Default, false, &err));
                CHECK(err.find("OKAY_OWN_BLEND") != std::string::npos);
                CHECK(ImportableExtensions().find(".blend") == std::string::npos);
                bool ok = true; ImportModel(kDir + "blend_attr_41.blend", &ok); CHECK(!ok);
            }
        }
        setenv("OKAY_OWN_BLEND", "1", 1);
        CHECK(EffectiveImportBackend(".blend") == ImportBackend::Own);
        CHECK(ImportableExtensions().find(".blend") != std::string::npos);
        CHECK(ImportModelData(kDir + "blend_attr_41.blend", m, ImportBackend::Default, false, &err) && m.backend == "own" && m.parts.size() == 2);
        setenv("OKAY_OWN_BLEND", "0", 1);
        CHECK(EffectiveImportBackend(".blend") != ImportBackend::Own);
        CHECK(SetImportBackend(".blend", ImportBackend::Own));   // run-time beats the environment
        CHECK(EffectiveImportBackend(".blend") == ImportBackend::Own);
        bool ok = false; std::string tex;
        Mesh merged = ImportModel(kDir + "blend_attr_41.blend", &ok, &tex);   // merged-mesh path, flattened
        CHECK(ok && merged.TriangleCount() == 7 && merged.vertices.size() == 13);
        CHECK(tex == kDir + "tex/diffuse.png");
        Scene scene; scene.physicsEnabled = false;
        bool sok = false;
        GameObject* root = ImportModelScene(scene, kDir + "blend_attr_41.blend", &sok);
        CHECK(sok && root != nullptr);
        if (root) {
            int meshes = 0;
            for (MeshRenderer* mr : root->GetComponentsInChildren<MeshRenderer>(true)) { (void)mr; ++meshes; }
            CHECK(meshes == 2);
        }
        Scene scene2; scene2.physicsEnabled = false;
        bool sok2 = false;
        GameObject* root2 = ImportModelScene(scene2, kDir + "blend_skin_anim.blend", &sok2);
        CHECK(sok2 && root2 != nullptr);
        if (root2) {
            ModelAnimator* ma = root2->GetComponent<ModelAnimator>();
            CHECK(ma && ma->clips.size() == 1);
        }
        CHECK(!ImportModelData(kDir + "does_not_exist.blend", m, ImportBackend::Own, false, &err) && !err.empty());
        SetImportBackend(".blend", ImportBackend::Default);
        unsetenv("OKAY_OWN_BLEND");
    }

    TEST_MAIN_RESULT();
}
