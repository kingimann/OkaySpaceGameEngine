// Our own SMD reader: fixtures from scripts/gen_smd_fixtures.py (tests/data/models/*.smd, synthetic), values computed by hand,
// the import seam (opt-in switch, scene import, UTF-8 paths, locale) and malformed input.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/SkinnedMesh.hpp"
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;

static void TestSetEnv(const char* n, const char* v) {
#ifdef _WIN32
    _putenv_s(n, v);
#else
    setenv(n, v, 1);
#endif
}
static void TestUnsetEnv(const char* n) {
#ifdef _WIN32
    _putenv_s(n, "");
#else
    unsetenv(n);
#endif
}
static const std::string kDir = std::string(OKAY_SOURCE_DIR) + "/tests/data/models/";
static bool HasNote(const ImportedModel& m, const char* part) {
    for (const std::string& n : m.notes) if (n.find(part) != std::string::npos) return true;
    return false;
}
static bool Near(const Vec3& a, const Vec3& b, float e = 1e-4f) {
    return std::fabs(a.x - b.x) < e && std::fabs(a.y - b.y) < e && std::fabs(a.z - b.z) < e;
}
static int NodeByName(const ImportedModel& m, const char* n) {
    for (std::size_t i = 0; i < m.nodes.size(); ++i) if (m.nodes[i].name == n) return (int)i;
    return -1;
}

int main() {
    RUN_SUITE("smd_reader");
    std::string err;

    for (const char* file : {"arm.smd", "arm_crlf.smd"}) {
        ImportedModel m;
        CHECK(ReadSmdFile(kDir + file, m, &err));
        CHECK(m.backend == "own" && m.nodes.size() == 5 && m.clips.empty());
        if (m.nodes.size() != 5) continue;
        CHECK(m.nodes[0].name == "SMD" && m.nodes[0].parent == -1);
        CHECK(m.nodes[1].name == "root" && m.nodes[1].parent == 0);
        CHECK(m.nodes[2].name == "upper arm" && m.nodes[2].parent == 1 && Near(m.nodes[2].position, Vec3{0, 1, 0}));   // a name with a space
        CHECK(m.nodes[3].name == "lower" && m.nodes[3].parent == 2);
        CHECK(Near(m.nodes[3].rotation * Vec3{1, 0, 0}, Vec3{0, 1, 0}));                                  // yaw 90 degrees
        CHECK(m.nodes[4].name == "tip" && Near(m.nodes[4].position, Vec3{1, 0, 0}));
        CHECK(Near(m.nodes[4].rotation * Vec3{0, 1, 0}, Vec3{0, 0, 1}));                                  // Rz * Ry * Rx with roll 90 and yaw 90
        // the root turns Z up into Y up
        CHECK(Near(m.nodes[0].rotation * Vec3{0, 0, 1}, Vec3{0, 1, 0}) && Near(m.nodes[0].rotation * Vec3{0, 1, 0}, Vec3{0, 0, -1}));
        CHECK(m.materials.size() == 2 && m.materials[0].name == "arm_a.tga" && m.materials[1].texture.find("arm_b.tga") != std::string::npos);
        CHECK(m.parts.size() == 2);
        if (m.parts.size() != 2) continue;
        const ImportedPart& a = m.parts[0];
        CHECK(a.material == 0 && a.mesh.triangles.size() == 6 && a.mesh.vertices.size() == 4 && a.bones.size() == 4 && a.jointIdx.size() == 4);   // identical corners welded
        CHECK(m.parts[1].material == 1 && m.parts[1].mesh.triangles.size() == 3);
        CHECK(a.bones[0].name == "root" && a.bones[1].name == "upper arm");
        // vertex 0: links to bones 1 and 2 (0.75, 0.25)
        {
            float w1 = 0, w2 = 0;
            for (int k = 0; k < 4; ++k) { if (a.jointIdx[0][(std::size_t)k] == 1) w1 += a.jointWt[0][(std::size_t)k]; if (a.jointIdx[0][(std::size_t)k] == 2) w2 += a.jointWt[0][(std::size_t)k]; }
            CHECK_NEAR(w1, 0.75f, 1e-5); CHECK_NEAR(w2, 0.25f, 1e-5);
            CHECK_NEAR(a.mesh.uvs[0].y, 1.0f, 1e-6);   // v flipped: 0 -> 1
        }
        // the vertex with 6 links keeps the four strongest (bones 0..3, 0.1 0.2 0.3 0.4)
        {
            std::size_t vi = 99;
            for (std::size_t i = 0; i < a.mesh.vertices.size(); ++i) if (Near(a.mesh.vertices[i], Vec3{1, 2, 0})) vi = i;
            CHECK(vi < 4);
            if (vi < 4) { float wsum = 0; for (int k = 0; k < 4; ++k) { wsum += a.jointWt[vi][(std::size_t)k]; if (a.jointIdx[vi][(std::size_t)k] == 3) CHECK_NEAR(a.jointWt[vi][(std::size_t)k], 0.4f, 1e-5); } CHECK_NEAR(wsum, 1.0f, 1e-5); }
        }
        // inverse bind of "lower" (world = T(0,2,0) * Rz(90)): the world point (0,3,0) is (1,0,0) in the bone's space
        CHECK(Near(a.bones[2].inverseBind.MultiplyPoint(Vec3{0, 3, 0}), Vec3{1, 0, 0}));
        CHECK(Near(a.bones[2].inverseBind.MultiplyPoint(Vec3{0, 2, 0}), Vec3{0, 0, 0}));
        // vertices without a link list belong to their own bone with weight 1
        {
            const ImportedPart& b = m.parts[1];
            CHECK(b.jointIdx.size() == 3 && b.jointIdx[0][0] == 0 && std::fabs(b.jointWt[0][0] - 1.0f) < 1e-6f);
        }
        CHECK(HasNote(m, "beyond four per vertex"));
        CHECK(HasNote(m, "Z-up into Y-up"));
    }
    {   // an animation file: skeleton and frames, no mesh
        ImportedModel m;
        CHECK(ReadSmdFile(kDir + "arm_anim.smd", m, &err));
        CHECK(m.parts.empty() && m.nodes.size() == 4 && m.clips.size() == 1 && m.clips[0].name == "animation");
        if (m.clips.size() == 1) {
            const ImportedClip& c = m.clips[0];
            CHECK(c.channels.size() == 3);
            const ImportedChannel* root = nullptr; const ImportedChannel* up = nullptr;
            for (const ImportedChannel& ch : c.channels) { if (ch.node == "root") root = &ch; if (ch.node == "upper arm") up = &ch; }
            CHECK(root && up);
            if (root && up) {
                CHECK(root->position.size() == 3 && up->rotation.size() == 3);
                CHECK_NEAR(root->position[1].time, 5.0f / 30.0f, 1e-6); CHECK_NEAR(root->position[2].time, 10.0f / 30.0f, 1e-6);
                CHECK(Near(root->position[2].value, Vec3{6, 0, 0}));
                CHECK(Near(up->rotation[1].value * Vec3{1, 0, 0}, Vec3{0, 1, 0}));    // yaw 90
                CHECK(Near(up->rotation[2].value * Vec3{1, 0, 0}, Vec3{-1, 0, 0}));   // yaw 180
            }
        }
        CHECK(HasNote(m, "30 frames per second"));
    }
    {   // malformed input
        ImportedModel m;
        const std::string head = "version 1\nnodes\n 0 \"a\" -1\nend\nskeleton\ntime 0\n 0 0 0 0 0 0 0\nend\n";
        CHECK(ReadSmdBuffer(head, "", m, &err) && m.nodes.size() == 2 && m.parts.empty() && m.clips.empty());   // a skeleton alone
        CHECK(ReadSmdBuffer(head + "triangles\nm.tga\n 0 0 0 0 0 0 0 0 0\n 0 1 0 0 0 0 0 1 0\n 0 0 1 0 0 0 0 0 1\nend\n", "", m, &err) && m.parts.size() == 1 && m.parts[0].mesh.triangles.size() == 3);
        CHECK(HasNote(m, "zero or invalid"));   // zero normals: smooth normals generated
        CHECK(m.parts[0].mesh.normals.size() == 3 && std::fabs(m.parts[0].mesh.normals[0].Magnitude() - 1.0f) < 1e-5f);
        const char* bad[] = {
            "", "hello world\n", "version 2\nnodes\nend\n", "nodes\n 0 \"a\"\nend\n", "nodes\n x \"a\" -1\nend\n", "skeleton\n 0 0 0 0 0 0 0\nend\n",
            "nodes\n 0 \"a\" -1\nend\nskeleton\ntime 0\n 0 0 0 0 0 0\nend\n", "nodes\n 0 \"a\" -1\nend\nskeleton\ntime 0\n 0 0 0 0 0 0 nan\nend\n",
            "nodes\n 0 \"a\" -1\nend\nskeleton\ntime 0\n 0 0 0 0 0 0 1e99\nend\n", "nodes\n 0 \"a\" -1\nend\nskeleton\ntime x\nend\n",
            "nodes\n 0 \"a\" -1\nend\ntriangles\nm.tga\n 0 0 0 0 0 0 1 0 0\n 0 1 0 0 0 0 1 1 0\n",   // ends inside a triangle
            "nodes\n 0 \"a\" -1\nend\ntriangles\nm.tga\n 0 0 0 0 0 0 1 0\n",                      // a vertex with too few values
            "nodes\n 0 \"a\" -1\nend\ntriangles\nm.tga\n 0 0 0 0 0 0 1 0 0 3 0 1.0\n",            // links claim more than they hold
            "end\n", "version 1\n",
        };
        for (const char* b : bad) { CHECK(!ReadSmdBuffer(b, "", m, &err) && !err.empty() && m.nodes.empty() && m.parts.empty()); }
        CHECK(!ReadSmdFile(kDir + "does_not_exist.smd", m, &err) && err == "cannot open file");
        // a parent loop and a missing parent do not fail: the bones go to the root, with a note
        CHECK(ReadSmdBuffer("nodes\n 0 \"a\" 1\n 1 \"b\" 0\n 2 \"c\" 77\n 3 \"d\" 3\nend\nskeleton\ntime 0\n 0 0 0 0 0 0 0\nend\n", "", m, &err) && m.nodes.size() == 5 && HasNote(m, "missing or looping parent"));
        for (std::size_t i = 0; i < m.nodes.size(); ++i) CHECK(m.nodes[i].parent < (int)i);
        // repeated bone names are made unique
        CHECK(ReadSmdBuffer("nodes\n 0 \"a\" -1\n 1 \"a\" 0\nend\nskeleton\ntime 0\n 0 0 0 0 0 0 0\nend\n", "", m, &err) && m.nodes.size() == 3 && m.nodes[1].name != m.nodes[2].name && HasNote(m, "repeated"));
        // a vertexanimation section is skipped with a note
        CHECK(ReadSmdBuffer(head + "vertexanimation\ntime 0\n 1 0 0 0 0 0 1\nend\n", "", m, &err) && HasNote(m, "vertexanimation"));
    }
    {   // a decimal-comma locale does not change what a '.' number means
        ImportedModel m;
        const char* got = std::setlocale(LC_ALL, "de_DE.UTF-8");
        if (!got) got = std::setlocale(LC_ALL, "de_DE");
        if (got) {
            CHECK(ReadSmdBuffer("nodes\n 0 \"a\" -1\nend\nskeleton\ntime 0\n 0 1.5 2.25 0 0 0 0\nend\n", "", m, &err));
            if (m.nodes.size() == 2) CHECK(Near(m.nodes[1].position, Vec3{1.5f, 2.25f, 0}));
            std::setlocale(LC_ALL, "C");
        } else std::printf("  (no German locale installed: locale check skipped)\n");
    }
    {   // a UTF-8 path with characters outside the ANSI code page
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / fs::u8path(u8"okay_smd_Müller_立方");
        std::error_code ec;
        fs::create_directories(dir, ec);
        const fs::path file = dir / fs::u8path(u8"立方.smd");
        { std::ofstream f(file, std::ios::binary); f << "nodes\n 0 \"a\" -1\nend\nskeleton\ntime 0\n 0 0 0 0 0 0 0\nend\n"; }
        ImportedModel m;
        CHECK(ReadSmdFile(file.u8string(), m, &err) && m.nodes.size() == 2);
        fs::remove_all(dir, ec);
    }

    {   // the seam
        CHECK(HasOwnReader(".smd") && HasOwnReader(".SMD"));
        TestUnsetEnv("OKAY_OWN_SMD");
        SetImportBackend(".smd", ImportBackend::Default);
        ImportedModel m;
        if (!AssimpAvailable() && EffectiveImportBackend(".smd") == ImportBackend::Default) {
            CHECK(!ImportModelData(kDir + "arm.smd", m, ImportBackend::Default, false, &err));
            CHECK(err.find("OKAY_OWN_SMD") != std::string::npos);
            CHECK(ImportableExtensions().find(".smd") == std::string::npos);
        }
        TestSetEnv("OKAY_OWN_SMD", "1");
        CHECK(EffectiveImportBackend(".smd") == ImportBackend::Own);
        CHECK(ImportableExtensions().find(".smd") != std::string::npos);
        CHECK(ImportModelData(kDir + "arm.smd", m, ImportBackend::Default, false, &err) && m.backend == "own" && m.parts.size() == 2);
        TestSetEnv("OKAY_OWN_SMD", "0");
        CHECK(EffectiveImportBackend(".smd") != ImportBackend::Own);
        CHECK(SetImportBackend(".smd", ImportBackend::Own));
        CHECK(EffectiveImportBackend(".smd") == ImportBackend::Own);
        {   // a skinned mesh builds joints and a SkinnedMesh
            Scene scene; scene.physicsEnabled = false;
            bool ok = false;
            GameObject* root = ImportModelScene(scene, kDir + "arm.smd", &ok);
            CHECK(ok && root != nullptr);
            if (root) {
                CHECK(root->GetComponentsInChildren<MeshRenderer>(true).size() == 2);
                auto sk = root->GetComponentsInChildren<SkinnedMesh>(true);
                CHECK(sk.size() == 2);
                if (!sk.empty()) { CHECK(sk[0]->joints.size() == 4); for (Transform* j : sk[0]->joints) CHECK(j != nullptr); }
            }
        }
        {   // an animation-only file builds a skeleton with a clip and no meshes
            Scene scene; scene.physicsEnabled = false;
            bool ok = false;
            GameObject* root = ImportModelScene(scene, kDir + "arm_anim.smd", &ok);
            CHECK(ok && root != nullptr);
            if (root) {
                CHECK(root->GetComponentsInChildren<MeshRenderer>(true).empty());
                ModelAnimator* ma = root->GetComponent<ModelAnimator>();
                CHECK(ma && ma->clips.size() == 1 && ma->clips[0].nodes.size() == 3);
            }
        }
        bool mok = false; Mesh merged = ImportModel(kDir + "arm.smd", &mok);
        CHECK(mok && merged.TriangleCount() == 3);
        {   // the one-static-mesh mode: the root's Z-up turn is baked into the vertices, skins and clips are gone
            ImportedModel f;
            CHECK(ImportModelData(kDir + "arm.smd", f, ImportBackend::Default, true, &err) && f.nodes.size() == 1 && f.parts.size() == 2 && f.clips.empty() && f.parts[0].bones.empty());
            bool found = false;
            if (f.parts.size() == 2) for (const Vec3& v : f.parts[0].mesh.vertices) if (Near(v, Vec3{0, 0, -2})) found = true;   // (0,2,0) in the file
            CHECK(found);
        }
        SetImportBackend(".smd", ImportBackend::Default);
        TestUnsetEnv("OKAY_OWN_SMD");
    }

    TEST_MAIN_RESULT();
}
