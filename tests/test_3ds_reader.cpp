// Our own 3DS reader: fixtures from scripts/gen_3ds_fixtures.py (tests/data/models/*.3ds), checked
// against values computed by hand, plus the import seam (opt-in switch, merged mesh, scene import).
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
// the file's Z-up space -> our Y-up space
static Vec3 Up(float x, float y, float z) { return Vec3{x, z, -y}; }
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

int main() {
    RUN_SUITE("3ds_reader");
    std::string err;

    // ---------------- cube: smooth, one material, UVs, colours, texture ----------------
    {
        ImportedModel m;
        CHECK(Read3dsFile(kDir + "cube.3ds", m, &err));
        CHECK(m.backend == "own");
        CHECK(m.notes.empty());
        for (const std::string& n : m.notes) std::printf("  note: %s\n", n.c_str());
        CHECK(m.parts.size() == 1 && m.materials.size() == 1);
        if (m.parts.size() == 1) {
            const Mesh& me = m.parts[0].mesh;
            CHECK(me.vertices.size() == 8 && me.TriangleCount() == 12);   // all one smoothing group: welded
            CHECK(me.HasNormals() && me.HasUVs());
            CHECK(m.parts[0].material == 0 && m.parts[0].node >= 0 && m.parts[0].node < (int)m.nodes.size());
            // Stored positions are in the file's space; the root node turns them to Y-up. Bake it by hand:
            const Mat4 w = WorldOf(m, m.parts[0].node);
            Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
            for (const Vec3& v : me.vertices) { const Vec3 p = Apply(w, v); lo = Vec3{std::fmin(lo.x, p.x), std::fmin(lo.y, p.y), std::fmin(lo.z, p.z)}; hi = Vec3{std::fmax(hi.x, p.x), std::fmax(hi.y, p.y), std::fmax(hi.z, p.z)}; }
            CHECK(Near(lo, Vec3{-1, -1, -1}) && Near(hi, Vec3{1, 1, 1}));
            // a vertex's normal: the corner direction in world space
            for (std::size_t i = 0; i < me.vertices.size(); ++i) {
                const Vec3 p = Apply(w, me.vertices[i]);
                const Vec3 n = Apply(w, me.vertices[i] + me.normals[i]) - p;
                CHECK(std::fabs(n.Magnitude() - 1.0f) < 1e-3f);
                CHECK(Near(n, Vec3{p.x * 0.57735f, p.y * 0.57735f, p.z * 0.57735f}, 2e-3f));
            }
            // UV v is flipped: file vertex 0 has (0,0), vertex 3 has (0,1)
            bool uv00 = false;
            for (std::size_t i = 0; i < me.vertices.size(); ++i)
                if (Near(Apply(w, me.vertices[i]), Vec3{-1, -1, 1}) ) { CHECK(std::fabs(me.uvs[i].x) < 1e-6f && std::fabs(me.uvs[i].y - 1.0f) < 1e-6f); uv00 = true; }
            CHECK(uv00);
        }
        // root node: Z up -> Y up, unit scale
        CHECK(!m.nodes.empty() && m.nodes[0].parent == -1);
        if (!m.nodes.empty()) {
            CHECK(Near(m.nodes[0].rotation * Vec3{0, 0, 1}, Vec3{0, 1, 0}));
            CHECK(Near(m.nodes[0].rotation * Vec3{0, 1, 0}, Vec3{0, 0, -1}));
            CHECK(Near(m.nodes[0].scale, Vec3{1, 1, 1}));
        }
        const ImportedMaterial& mat = m.materials[0];
        CHECK(mat.name == "Red" && mat.hasDiffuse);
        CHECK_NEAR(mat.diffuse.r, 1.0f, 1e-5); CHECK_NEAR(mat.diffuse.g, 0.0f, 1e-5);
        CHECK_NEAR(mat.shininess, 50.0f, 1e-4);
        CHECK(mat.hasEmissive && std::fabs(mat.emissive.r - 0.2f) < 1e-5f);
        CHECK(mat.texture == kDir + "RED.TGA");
        // flattened: transforms baked, no clips
        ImportedModel f;
        CHECK(Read3dsFile(kDir + "cube.3ds", f, &err, true));
        CHECK(f.nodes.size() == 1 && f.parts.size() == 1 && f.clips.empty());
        if (!f.parts.empty()) {
            CHECK(f.parts[0].mesh.vertices.size() == 8);
            // corner (-1,-1,-1) -> (-1,-1,1)
            bool ok = false;
            for (std::size_t i = 0; i < 8; ++i) if (Near(f.parts[0].mesh.vertices[i], Vec3{-1, -1, 1})) ok = Near(f.parts[0].mesh.normals[i], Vec3{-0.57735f, -0.57735f, 0.57735f}, 2e-3f);
            CHECK(ok);
        }
    }

    // ---------------- box: flat shading, two materials, master scale, notes ----------------
    {
        ImportedModel m;
        CHECK(Read3dsFile(kDir + "box_flat.3ds", m, &err));
        CHECK(m.parts.size() == 2 && m.materials.size() == 2);
        if (m.parts.size() == 2) {
            CHECK(m.parts[0].material == 0 && m.parts[1].material == 1);
            CHECK(m.parts[0].mesh.vertices.size() == 12 && m.parts[0].mesh.TriangleCount() == 6);   // flat: 4 per cube face
            CHECK(m.parts[1].mesh.vertices.size() == 12 && m.parts[1].mesh.TriangleCount() == 6);
            for (const ImportedPart& p : m.parts)
                for (const Vec3& n : p.mesh.normals) {
                    const float mx = std::fmax(std::fabs(n.x), std::fmax(std::fabs(n.y), std::fabs(n.z)));
                    CHECK(std::fabs(mx - 1.0f) < 1e-4f);   // each normal is one axis
                }
        }
        CHECK(Near(m.nodes[0].scale, Vec3{0.5f, 0.5f, 0.5f}));   // master scale on the root
        CHECK(m.materials[0].name == "Wood" && m.materials[1].name == "Metal");
        CHECK_NEAR(m.materials[0].diffuse.g, 0.25f, 1e-5);
        CHECK(m.materials[0].texture == kDir + "wood.png");
        CHECK(m.materials[1].normalMap == kDir + "BUMP.PNG" && m.materials[1].specularMap == kDir + "SPEC.PNG");
        CHECK(HasNote(m, "light") && HasNote(m, "camera") && HasNote(m, "material map") && HasNote(m, "tiling") && HasNote(m, "transparency"));
        ImportedModel f;
        CHECK(Read3dsFile(kDir + "box_flat.3ds", f, &err, true));
        Mesh merged = MergeModelMeshes(f);
        Vec3 lo, hi; merged.Bounds(lo, hi);
        CHECK(Near(lo, Vec3{-0.5f, -0.5f, -0.5f}) && Near(hi, Vec3{0.5f, 0.5f, 0.5f}));   // scaled by 0.5
    }

    // ---------------- hierarchy: keyframe nodes, instances, tracks ----------------
    {
        ImportedModel m;
        CHECK(Read3dsFile(kDir + "hierarchy.3ds", m, &err));
        CHECK(m.nodes.size() == 5);   // root + Base, Arm, $$$DUMMY, Arm.copy
        if (m.nodes.size() == 5) {
            CHECK(m.nodes[1].name == "Base" && m.nodes[1].parent == 0);
            CHECK(m.nodes[2].name == "Arm" && m.nodes[2].parent == 1);
            CHECK(m.nodes[3].name == "$$$DUMMY" && m.nodes[3].parent == 2);
            CHECK(m.nodes[4].name == "Arm.copy" && m.nodes[4].parent == 1);
            CHECK(Near(m.nodes[1].position, Vec3{10, 0, 0}) && Near(m.nodes[2].position, Vec3{0, 0, 3}));
        }
        CHECK(m.parts.size() == 3);   // Base, Arm, and the Arm instance
        // At rest every part sits where the file stored it (the pivot and an instance's own placement move it).
        const Mat4 up = WorldOf(m, 0);
        for (const ImportedPart& p : m.parts) {
            const Mat4 w = WorldOf(m, p.node);
            Vec3 c{0, 0, 0};
            for (const Vec3& v : p.mesh.vertices) c = c + Apply(w, v);
            c = c / (float)p.mesh.vertices.size();
            const std::string& nm = m.nodes[(std::size_t)p.node].name;
            if (nm == "Base") CHECK(Near(c, Up(11, 1, 0)));
            if (nm == "Arm") CHECK(Near(c, Up(9.8333f, 0, 3.3333f), 1e-3f));   // stored - pivot (0.5, 0, 0)
            if (nm == "Arm.copy") CHECK(Near(c, Up(6.3333f, 0, 3.3333f), 1e-3f));
            for (const Vec3& n : p.mesh.normals) CHECK(std::fabs(n.Magnitude() - 1.0f) < 1e-3f);
        }
        (void)up;
        CHECK(m.clips.size() == 1 && m.clips[0].name == "animation");
        if (m.clips.size() == 1) {
            const ImportedClip& c = m.clips[0];
            CHECK(c.channels.size() == 2);   // Base and Arm; the dummy and the instance have one key each (placement only)
            for (const ImportedChannel& ch : c.channels) {
                if (ch.node == "Base") {
                    CHECK(ch.position.size() == 2 && ch.rotation.size() == 2 && ch.scale.size() == 1);   // the spline parameters were skipped
                    if (ch.position.size() == 2) {
                        CHECK_NEAR(ch.position[0].time, 0.0f, 1e-6); CHECK_NEAR(ch.position[1].time, 1.0f, 1e-6);   // frame 30 at 30 fps
                        CHECK(Near(ch.position[1].value, Vec3{10, 0, 5}));
                    }
                    if (ch.rotation.size() == 2) {   // +90 degrees in the file turns clockwise about z (+x goes to -y)
                        const Quat q = ch.rotation[1].value;
                        CHECK(Near(q * Vec3{1, 0, 0}, Vec3{0, -1, 0}));
                    }
                }
            }
        }
        // flattened: no hierarchy, no clips
        ImportedModel f;
        CHECK(Read3dsFile(kDir + "hierarchy.3ds", f, &err, true));
        CHECK(f.nodes.size() == 1 && f.clips.empty() && f.parts.size() == 2);
    }

    // ---------------- malformed input fails cleanly ----------------
    {
        ImportedModel m;
        const std::string cube = Slurp(kDir + "cube.3ds");
        CHECK(!cube.empty());
        CHECK(!Read3dsBuffer("", "", m, &err) && !err.empty());
        CHECK(!Read3dsBuffer("not a 3ds file at all", "", m, &err));
        CHECK(!Read3dsBuffer(std::string("\x4D\x4D\x05\x00\x00\x00", 6), "", m, &err));   // chunk shorter than its header
        for (std::size_t cut : {(std::size_t)5, (std::size_t)20, cube.size() / 2, cube.size() - 1}) {
            ImportedModel t; std::string e;
            Read3dsBuffer(cube.substr(0, cut), "", t, &e);   // must not crash; result either way is consistent
            CHECK(t.parts.empty() || !t.parts[0].mesh.vertices.empty());
        }
        CHECK(!Read3dsFile(kDir + "does_not_exist.3ds", m, &err) && err == "cannot open file");
    }

    // ---------------- the seam ----------------
    {
        CHECK(HasOwnReader(".3ds") && HasOwnReader(".3DS"));
        unsetenv("OKAY_OWN_3DS");
        SetImportBackend(".3ds", ImportBackend::Default);
        ImportedModel m;
        if (!AssimpAvailable()) {
            if (EffectiveImportBackend(".3ds") == ImportBackend::Default) {   // not built with -DOKAY_OWN_3DS=ON
                CHECK(!ImportModelData(kDir + "cube.3ds", m, ImportBackend::Default, false, &err));
                CHECK(err.find("OKAY_OWN_3DS") != std::string::npos);
                CHECK(ImportableExtensions().find(".3ds") == std::string::npos);
                bool ok = true; ImportModel(kDir + "cube.3ds", &ok); CHECK(!ok);
            }
        }
        setenv("OKAY_OWN_3DS", "1", 1);
        CHECK(EffectiveImportBackend(".3ds") == ImportBackend::Own);
        CHECK(ImportableExtensions().find(".3ds") != std::string::npos);
        CHECK(ImportModelData(kDir + "cube.3ds", m, ImportBackend::Default, false, &err) && m.backend == "own" && m.parts.size() == 1);
        setenv("OKAY_OWN_3DS", "0", 1);
        CHECK(EffectiveImportBackend(".3ds") != ImportBackend::Own);
        CHECK(SetImportBackend(".3ds", ImportBackend::Own));   // run-time beats the environment
        CHECK(EffectiveImportBackend(".3ds") == ImportBackend::Own);
        bool ok = false; std::string tex;
        Mesh merged = ImportModel(kDir + "cube.3ds", &ok, &tex);   // merged-mesh path, flattened
        CHECK(ok && merged.TriangleCount() == 12 && merged.vertices.size() == 8);
        CHECK(tex == kDir + "RED.TGA");
        // scene import: meshes on the hierarchy, a clip on the animator
        Scene scene; scene.physicsEnabled = false;
        bool sok = false;
        GameObject* root = ImportModelScene(scene, kDir + "hierarchy.3ds", &sok);
        CHECK(sok && root != nullptr);
        if (root) {
            int meshes = 0;
            for (MeshRenderer* mr : root->GetComponentsInChildren<MeshRenderer>(true)) { (void)mr; ++meshes; }
            CHECK(meshes == 3);
            ModelAnimator* ma = root->GetComponent<ModelAnimator>();
            CHECK(ma && ma->clips.size() == 1);
        }
        CHECK(!ImportModelData(kDir + "does_not_exist.3ds", m, ImportBackend::Own, false, &err) && !err.empty());
        SetImportBackend(".3ds", ImportBackend::Default);
        unsetenv("OKAY_OWN_3DS");
    }

    TEST_MAIN_RESULT();
}
