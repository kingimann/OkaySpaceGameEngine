// Our own MD5 reader: fixtures from scripts/gen_md5_fixtures.py (tests/data/models/skel*.md5mesh / .md5anim, synthetic),
// values computed by hand, the import seam (opt-in switch, scene import, UTF-8 paths, locale) and malformed input.
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
#include <iterator>
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
static std::string Slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
static bool HasNote(const ImportedModel& m, const char* part) {
    for (const std::string& n : m.notes) if (n.find(part) != std::string::npos) return true;
    return false;
}
static bool Near(const Vec3& a, const Vec3& b, float e = 1e-4f) {
    return std::fabs(a.x - b.x) < e && std::fabs(a.y - b.y) < e && std::fabs(a.z - b.z) < e;
}
static const ImportedChannel* Chan(const ImportedModel& m, const char* node) {
    if (m.clips.empty()) return nullptr;
    for (const ImportedChannel& c : m.clips[0].channels) if (c.node == node) return &c;
    return nullptr;
}

int main() {
    RUN_SUITE("md5_reader");
    std::string err;

    for (const char* stem : {"skel", "skel_crlf"}) {
        ImportedModel m;
        CHECK(ReadMd5File(kDir + stem + ".md5mesh", m, &err));   // the .md5anim next to it is read too
        CHECK(m.backend == "own" && m.nodes.size() == 4 && m.parts.size() == 2 && m.clips.size() == 1);
        if (m.nodes.size() != 4 || m.parts.size() != 2) continue;
        CHECK(m.nodes[0].name == "MD5" && m.nodes[1].name == "base" && m.nodes[2].name == "arm" && m.nodes[3].name == "tip");
        CHECK(Near(m.nodes[0].rotation * Vec3{0, 0, 1}, Vec3{0, 1, 0}));                       // Z up becomes Y up
        CHECK(m.nodes[2].parent == 1 && Near(m.nodes[2].position, Vec3{0, 10, 0}));
        CHECK(Near(m.nodes[2].rotation * Vec3{1, 0, 0}, Vec3{0, -1, 0}));                       // -90 degrees about Z
        CHECK(m.nodes[3].parent == 2 && Near(m.nodes[3].position, Vec3{0, 0, 5}));              // model space (0,10,5) relative to the arm
        CHECK(Near(m.nodes[3].rotation * Vec3{1, 0, 0}, Vec3{0, 1, 0}));                        // undoes the arm's turn
        const ImportedPart& a = m.parts[0];
        CHECK(a.mesh.vertices.size() == 3 && a.mesh.triangles.size() == 3 && a.bones.size() == 3);
        if (a.mesh.vertices.size() == 3) {
            CHECK(Near(a.mesh.vertices[0], Vec3{0, 9, 0}));
            CHECK(Near(a.mesh.vertices[1], Vec3{2, 5, 0}));
            CHECK(Near(a.mesh.vertices[2], Vec3{0, 7.5f, 2.5f}));
            CHECK(a.mesh.triangles[0] == 2 && a.mesh.triangles[1] == 1 && a.mesh.triangles[2] == 0);   // winding reversed
            CHECK_NEAR(a.mesh.uvs[2].y, 1.0f, 1e-6);                                                  // texture coordinates kept as written
            CHECK(a.mesh.normals.size() == 3);
            // vertex 1 is half base, half arm
            float wBase = 0, wArm = 0;
            for (int k = 0; k < 4; ++k) { if (a.jointIdx[1][(std::size_t)k] == 0) wBase += a.jointWt[1][(std::size_t)k]; if (a.jointIdx[1][(std::size_t)k] == 1) wArm += a.jointWt[1][(std::size_t)k]; }
            CHECK_NEAR(wBase, 0.5f, 1e-5); CHECK_NEAR(wArm, 0.5f, 1e-5);
            CHECK(a.jointIdx[0][0] == 1 && std::fabs(a.jointWt[0][0] - 1.0f) < 1e-6f);
        }
        // inverse bind of the arm: its own origin goes to zero, a point 1 up in model space is (0,-1... in its turned frame
        CHECK(Near(a.bones[1].inverseBind.MultiplyPoint(Vec3{0, 10, 0}), Vec3{0, 0, 0}));
        CHECK(Near(a.bones[1].inverseBind.MultiplyPoint(Vec3{0, 9, 0}), Vec3{1, 0, 0}));
        CHECK(m.materials.size() == 2 && m.materials[0].texture.find("arm.tga") != std::string::npos && m.materials[1].texture.empty() && m.materials[1].name == "models/zombie/body");
        CHECK(m.parts[1].mesh.triangles.size() == 3 && m.parts[1].mesh.triangles[0] == 1 && m.parts[1].mesh.triangles[1] == 2 && m.parts[1].mesh.triangles[2] == 0);
        CHECK(HasNote(m, "1 mesh(es) without vertices"));
        CHECK(HasNote(m, "material declarations"));
        CHECK(HasNote(m, "smooth normals were generated"));
        // the animation: frame times at 10 per second
        const ImportedChannel* base = Chan(m, "base");
        const ImportedChannel* arm = Chan(m, "arm");
        CHECK(base && arm && m.clips[0].channels.size() == 3);
        if (base && arm) {
            CHECK(base->position.size() == 3 && arm->rotation.size() == 3);
            CHECK_NEAR(base->position[1].time, 0.1f, 1e-6); CHECK_NEAR(base->position[2].time, 0.2f, 1e-6);
            CHECK(Near(base->position[1].value, Vec3{1, 2, 0}) && Near(base->position[2].value, Vec3{2, 4, 0}));
            CHECK(Near(arm->position[2].value, Vec3{0, 10, 0}));                                    // not animated: the base frame
            CHECK(Near(arm->rotation[0].value * Vec3{1, 0, 0}, Vec3{1, 0, 0}));
            CHECK(Near(arm->rotation[1].value * Vec3{1, 0, 0}, Vec3{0.70710678f, -0.70710678f, 0}));   // -45 degrees
            CHECK(Near(arm->rotation[2].value * Vec3{1, 0, 0}, Vec3{0, -1, 0}));                        // -90 degrees
        }
    }
    {   // the mesh alone, the animation alone
        ImportedModel m;
        CHECK(ReadMd5Buffer(Slurp(kDir + "skel.md5mesh"), "", "", m, &err) && m.clips.empty() && m.parts.size() == 2);
        CHECK(ReadMd5File(kDir + "skel.md5anim", m, &err));
        CHECK(m.parts.empty() && m.nodes.size() == 4 && m.clips.size() == 1 && m.nodes[1].name == "base" && Near(m.nodes[2].position, Vec3{0, 10, 0}));
        // an animation whose joints are not in the mesh: keys dropped, with a note
        std::string anim = Slurp(kDir + "skel.md5anim");
        std::size_t p;
        while ((p = anim.find("\"tip\"")) != std::string::npos) anim.replace(p, 5, "\"nope\"");
        CHECK(ReadMd5Buffer(Slurp(kDir + "skel.md5mesh"), anim, "", m, &err) && m.clips.size() == 1 && m.clips[0].channels.size() == 2 && HasNote(m, "not in the mesh"));
        // a broken animation next to a good mesh: the mesh is still read, the animation is reported
        CHECK(ReadMd5Buffer(Slurp(kDir + "skel.md5mesh"), "MD5Version 10\nnumFrames x\n", "", m, &err) && m.parts.size() == 2 && m.clips.empty() && HasNote(m, "animation was not read"));
        CHECK(!ReadMd5Buffer("", "MD5Version 10\nnumFrames x\n", "", m, &err) && !err.empty());
    }
    {   // more than four joints on one vertex: the strongest four are kept
        ImportedModel m;
        std::string t = "MD5Version 10\nnumJoints 6\nnumMeshes 1\njoints {\n";
        for (int i = 0; i < 6; ++i) t += "\"j" + std::to_string(i) + "\" -1 ( " + std::to_string(i) + " 0 0 ) ( 0 0 0 )\n";
        t += "}\nmesh {\nnumverts 3\nvert 0 ( 0 0 ) 0 6\nvert 1 ( 1 0 ) 0 6\nvert 2 ( 0 1 ) 0 6\nnumtris 1\ntri 0 0 1 2\nnumweights 6\n";
        const float b[6] = {0.05f, 0.3f, 0.1f, 0.25f, 0.2f, 0.1f};
        for (int i = 0; i < 6; ++i) t += "weight " + std::to_string(i) + " " + std::to_string(i) + " " + std::to_string(b[i]) + " ( 0 0 0 )\n";
        t += "}\n";
        CHECK(ReadMd5Buffer(t, "", "", m, &err) && m.parts.size() == 1);
        if (m.parts.size() == 1) {
            float s = 0; bool has5 = false, has0 = false;
            for (int k = 0; k < 4; ++k) { s += m.parts[0].jointWt[0][(std::size_t)k]; if (m.parts[0].jointIdx[0][(std::size_t)k] == 5) has5 = true; if (m.parts[0].jointIdx[0][(std::size_t)k] == 0) has0 = true; }
            CHECK_NEAR(s, 1.0f, 1e-5); CHECK(!has0);   // the weakest (joint 0, 0.05) is gone
            (void)has5;
        }
        CHECK(HasNote(m, "beyond four per vertex"));
    }
    {   // malformed input
        ImportedModel m;
        const std::string good = Slurp(kDir + "skel.md5mesh");
        const char* bad[] = {
            "", "hello", "MD5Version 9\n", "MD5Version\n", "MD5Version 10\n", "MD5Version 10\nnumJoints 1\n", "MD5Version 10\nnumJoints 99999\n",
            "MD5Version 10\nnumJoints 1\nnumMeshes 0\njoints {\n\"a\" -1 ( 0 0 0 ) ( 0 0 0 )\n", "MD5Version 10\nnumJoints 1\nnumMeshes 0\njoints {\n\"a\" 5 ( 0 0 0 ) ( 0 0 0 )\n}\n",
            "MD5Version 10\nnumJoints 1\nnumMeshes 0\njoints {\n\"a\" -1 ( 0 0 nan ) ( 0 0 0 )\n}\n", "MD5Version 10\nnumJoints 1\nnumMeshes 0\njoints {\n\"a\" -1 ( 0 0 0 ) ( 0 0 )\n}\n",
            "MD5Version 10\nmesh {\n}\n", "MD5Version 10\nnumJoints 1\nnumMeshes 1\njoints {\n\"a\" -1 ( 0 0 0 ) ( 0 0 0 )\n}\nmesh {\nnumverts 2000000000\n}\n",
            "MD5Version 10\nnumJoints 1\nnumMeshes 1\njoints {\n\"a\" -1 ( 0 0 0 ) ( 0 0 0 )\n}\nmesh {\nnumverts 1\nvert 0 ( 0 0 ) 0 5\n}\n",
            "MD5Version 10\nnumJoints 1\nnumMeshes 1\njoints {\n\"a\" -1 ( 0 0 0 ) ( 0 0 0 )\n}\nmesh {\nnumverts 1\nnumweights 1\nweight 0 7 1 ( 0 0 0 )\n}\n",
            "MD5Version 10\nnumJoints 1\nnumMeshes 1\njoints {\n\"a\" -1 ( 0 0 0 ) ( 0 0 0 )\n}\nbogus\n",
        };
        for (const char* b : bad) { CHECK(!ReadMd5Buffer(b, "", "", m, &err) && !err.empty() && m.nodes.empty() && m.parts.empty()); }
        const char* badAnim[] = {
            "MD5Version 10\nnumFrames 1\nnumJoints 1\nnumAnimatedComponents 0\nhierarchy {\n\"a\" -1 0 0\n}\n",                                      // no baseframe
            "MD5Version 10\nnumFrames 1\nnumJoints 1\nnumAnimatedComponents 0\nhierarchy {\n\"a\" -1 63 0\n}\nbaseframe {\n( 0 0 0 ) ( 0 0 0 )\n}\n",   // flags need 6 components, 0 exist
            "MD5Version 10\nnumFrames 1\nnumJoints 1\nnumAnimatedComponents 1\nhierarchy {\n\"a\" -1 1 0\n}\nbaseframe {\n( 0 0 0 ) ( 0 0 0 )\n}\nframe 0 {\n}\n",
            "MD5Version 10\nnumFrames 1\nnumJoints 1\nnumAnimatedComponents 1\nhierarchy {\n\"a\" -1 1 0\n}\nbaseframe {\n( 0 0 0 ) ( 0 0 0 )\n}\nframe 5 {\n1\n}\n",
            "MD5Version 10\nnumFrames 1\nnumJoints 1\nnumAnimatedComponents 1\nframeRate -3\n", "MD5Version 10\nnumFrames 100001\n",
            "MD5Version 10\nnumFrames 1\nnumJoints 1\nnumAnimatedComponents 1\nhierarchy {\n\"a\" -1 1 0\n}\nbaseframe {\n( 0 0 0 ) ( 0 0 0 )\n}\nframe 0 {\ninf\n}\n",
        };
        for (const char* b : badAnim) { CHECK(!ReadMd5Buffer("", b, "", m, &err) && !err.empty() && m.nodes.empty()); }
        CHECK(!ReadMd5File(kDir + "does_not_exist.md5mesh", m, &err) && err == "cannot open file");
        (void)good;
    }
    {   // a decimal-comma locale does not change what a '.' number means
        ImportedModel m;
        const char* got = std::setlocale(LC_ALL, "de_DE.UTF-8");
        if (!got) got = std::setlocale(LC_ALL, "de_DE");
        if (got) {
            CHECK(ReadMd5Buffer(Slurp(kDir + "skel.md5mesh"), Slurp(kDir + "skel.md5anim"), "", m, &err) && m.parts.size() == 2);
            if (m.parts.size() == 2) CHECK(Near(m.parts[0].mesh.vertices[2], Vec3{0, 7.5f, 2.5f}));
            const ImportedChannel* arm = Chan(m, "arm");
            if (arm && arm->rotation.size() == 3) CHECK(Near(arm->rotation[1].value * Vec3{1, 0, 0}, Vec3{0.70710678f, -0.70710678f, 0}));
            std::setlocale(LC_ALL, "C");
        } else std::printf("  (no German locale installed: locale check skipped)\n");
    }
    {   // a UTF-8 path with characters outside the ANSI code page: the animation is found next to the mesh
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / fs::u8path(u8"okay_md5_Müller_立方");
        std::error_code ec;
        fs::create_directories(dir, ec);
        { std::ofstream f(dir / fs::u8path(u8"立方.md5mesh"), std::ios::binary); f << Slurp(kDir + "skel.md5mesh"); }
        { std::ofstream f(dir / fs::u8path(u8"立方.md5anim"), std::ios::binary); f << Slurp(kDir + "skel.md5anim"); }
        ImportedModel m;
        CHECK(ReadMd5File((dir / fs::u8path(u8"立方.md5mesh")).u8string(), m, &err) && m.parts.size() == 2 && m.clips.size() == 1);
        fs::remove_all(dir, ec);
    }

    {   // the seam
        CHECK(HasOwnReader(".md5mesh") && HasOwnReader(".MD5ANIM"));
        TestUnsetEnv("OKAY_OWN_MD5");
        SetImportBackend(".md5mesh", ImportBackend::Default);
        ImportedModel m;
        if (!AssimpAvailable() && EffectiveImportBackend(".md5mesh") == ImportBackend::Default) {
            CHECK(!ImportModelData(kDir + "skel.md5mesh", m, ImportBackend::Default, false, &err));
            CHECK(err.find("OKAY_OWN_MD5") != std::string::npos);
            CHECK(ImportableExtensions().find(".md5mesh") == std::string::npos);
        }
        TestSetEnv("OKAY_OWN_MD5", "1");
        CHECK(EffectiveImportBackend(".md5mesh") == ImportBackend::Own && EffectiveImportBackend(".md5anim") == ImportBackend::Own);
        CHECK(ImportableExtensions().find(".md5mesh") != std::string::npos && ImportableExtensions().find(".md5anim") != std::string::npos);
        CHECK(ImportModelData(kDir + "skel.md5mesh", m, ImportBackend::Default, false, &err) && m.backend == "own" && m.parts.size() == 2);
        TestSetEnv("OKAY_OWN_MD5", "0");
        CHECK(EffectiveImportBackend(".md5mesh") != ImportBackend::Own);
        CHECK(SetImportBackend(".md5mesh", ImportBackend::Own));
        CHECK(EffectiveImportBackend(".md5mesh") == ImportBackend::Own && EffectiveImportBackend(".md5anim") == ImportBackend::Own);   // one switch for both
        {
            Scene scene; scene.physicsEnabled = false;
            bool ok = false;
            GameObject* root = ImportModelScene(scene, kDir + "skel.md5mesh", &ok);
            CHECK(ok && root != nullptr);
            if (root) {
                CHECK(root->GetComponentsInChildren<MeshRenderer>(true).size() == 2);
                auto sk = root->GetComponentsInChildren<SkinnedMesh>(true);
                CHECK(sk.size() == 2);
                if (!sk.empty()) { CHECK(sk[0]->joints.size() == 3); for (Transform* j : sk[0]->joints) CHECK(j != nullptr); }
                ModelAnimator* ma = root->GetComponent<ModelAnimator>();
                CHECK(ma && ma->clips.size() == 1 && ma->clips[0].nodes.size() == 3);
            }
        }
        {   // an animation file alone builds a skeleton with a clip
            Scene scene; scene.physicsEnabled = false;
            bool ok = false;
            GameObject* root = ImportModelScene(scene, kDir + "skel.md5anim", &ok);
            CHECK(ok && root != nullptr);
            if (root) { CHECK(root->GetComponentsInChildren<MeshRenderer>(true).empty()); CHECK(root->GetComponent<ModelAnimator>() != nullptr); }
        }
        {   // the one-static-mesh mode: the root's Z-up turn is baked in, skins and clips are gone
            ImportedModel f;
            CHECK(ImportModelData(kDir + "skel.md5mesh", f, ImportBackend::Default, true, &err) && f.nodes.size() == 1 && f.parts.size() == 2 && f.clips.empty() && f.parts[0].bones.empty());
            bool found = false;
            if (f.parts.size() == 2) for (const Vec3& v : f.parts[0].mesh.vertices) if (Near(v, Vec3{0, 2.5f, -7.5f})) found = true;   // (0,7.5,2.5) in the file
            CHECK(found);
        }
        SetImportBackend(".md5mesh", ImportBackend::Default);
        TestUnsetEnv("OKAY_OWN_MD5");
    }

    TEST_MAIN_RESULT();
}
