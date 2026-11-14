// Our own MS3D reader: fixtures from scripts/gen_ms3d_fixtures.py (tests/data/models/rig*.ms3d, synthetic), values computed by
// hand, the import seam (opt-in switch, scene import, UTF-8 paths) and malformed input.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/SkinnedMesh.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    RUN_SUITE("ms3d_reader");
    std::string err;

    {
        ImportedModel m;
        CHECK(ReadMs3dFile(kDir + "rig.ms3d", m, &err));
        CHECK(m.backend == "own" && m.nodes.size() == 3 && m.parts.size() == 2 && m.materials.size() == 2 && m.clips.size() == 1);
        if (m.nodes.size() == 3 && m.parts.size() == 2) {
            CHECK(m.nodes[0].name == "MS3D" && m.nodes[1].name == "root" && m.nodes[2].name == "arm" && m.nodes[2].parent == 1);
            CHECK(Near(m.nodes[2].position, Vec3{0, 1, 0}) && Near(m.nodes[2].rotation * Vec3{1, 0, 0}, Vec3{0, 1, 0}));   // rest: 90 degrees about Z
            const ImportedPart& body = m.parts[0];
            const ImportedPart& head = m.parts[1];
            CHECK(body.mesh.name == "body" && head.mesh.name == "head");
            CHECK(body.mesh.vertices.size() == 4 && body.mesh.triangles.size() == 6 && head.mesh.vertices.size() == 4 && head.mesh.triangles.size() == 6);   // shared corners welded
            CHECK(body.material == 0 && head.material == 1);
            CHECK(body.bones.size() == 3 && body.bones[2].name == "MS3D");       // the unassigned vertex is tied to the root, as a last "joint"
            CHECK(Near(body.bones[1].inverseBind.MultiplyPoint(Vec3{0, 1, 0}), Vec3{0, 0, 0}));   // the arm's origin is at (0,1,0) in model space
            CHECK(Near(body.bones[1].inverseBind.MultiplyPoint(Vec3{0, 2, 0}), Vec3{1, 0, 0}));   // (0,1,0) away along its turned axis: local +x
            CHECK_NEAR(body.mesh.uvs[2].y, 1.0f, 1e-6);                                            // kept as written
            // vertex 2 (0,1,0): main joint 1 (arm) with 75 percent, 25 percent on joint 0 from the extra block
            {
                std::size_t vi = 99;
                for (std::size_t i = 0; i < body.mesh.vertices.size(); ++i) if (Near(body.mesh.vertices[i], Vec3{0, 1, 0})) vi = i;
                CHECK(vi < 4);
                if (vi < 4) {
                    float wRoot = 0, wArm = 0;
                    for (int k = 0; k < 4; ++k) { if (body.jointIdx[vi][(std::size_t)k] == 0) wRoot += body.jointWt[vi][(std::size_t)k]; if (body.jointIdx[vi][(std::size_t)k] == 1) wArm += body.jointWt[vi][(std::size_t)k]; }
                    CHECK_NEAR(wRoot, 0.25f, 1e-5); CHECK_NEAR(wArm, 0.75f, 1e-5);
                }
                std::size_t v0 = 99;
                for (std::size_t i = 0; i < body.mesh.vertices.size(); ++i) if (Near(body.mesh.vertices[i], Vec3{0, 0, 0})) v0 = i;
                CHECK(v0 < 4 && body.jointIdx[v0][0] == 0 && std::fabs(body.jointWt[v0][0] - 1.0f) < 1e-6f);
                // the unassigned vertex (0,2,0) in the head follows the root bone with weight 1
                std::size_t v4 = 99;
                for (std::size_t i = 0; i < head.mesh.vertices.size(); ++i) if (Near(head.mesh.vertices[i], Vec3{0, 2, 0})) v4 = i;
                CHECK(v4 < 4 && head.jointIdx[v4][0] == 2 && std::fabs(head.jointWt[v4][0] - 1.0f) < 1e-6f);
            }
            // materials: path fixed up, colors, shininess, transparency in alpha
            CHECK(m.materials[0].name == "skin" && m.materials[0].texture == kDir + "skin.jpg" && m.materials[0].hasDiffuse);
            CHECK_NEAR(m.materials[0].diffuse.r, 0.5f, 1e-6); CHECK_NEAR(m.materials[0].diffuse.a, 1.0f, 1e-6);
            CHECK(m.materials[1].hasEmissive && m.materials[1].shininess == 32.0f);
            CHECK_NEAR(m.materials[1].diffuse.a, 0.5f, 1e-6);
        }
        // animation: keys are relative to the joint's rest values
        const ImportedChannel* root = Chan(m, "root");
        const ImportedChannel* arm = Chan(m, "arm");
        CHECK(root && arm);
        if (root && arm) {
            CHECK(root->rotation.size() == 2 && root->position.size() == 2 && arm->rotation.size() == 1 && arm->position.empty());
            CHECK_NEAR(root->rotation[1].time, 1.0f, 1e-6);
            CHECK(Near(root->rotation[1].value * Vec3{1, 0, 0}, Vec3{0, 1, 0}));
            CHECK(Near(root->position[1].value, Vec3{1, 0, 0}));
            CHECK_NEAR(arm->rotation[0].time, 0.5f, 1e-6);
            CHECK(Near(arm->rotation[0].value * Vec3{1, 0, 0}, Vec3{-1, 0, 0}));   // rest 90 + key 90 = 180 degrees
        }
        CHECK(HasNote(m, "1 degenerate"));
        CHECK(HasNote(m, "1 triangle(s) with an out-of-range"));
        CHECK(HasNote(m, "1 group entries naming a triangle that does not exist"));
        CHECK(HasNote(m, "1 group(s) without usable triangles"));
        CHECK(HasNote(m, "1 comment(s) not imported"));
        CHECK(HasNote(m, "1 vertex(es) without a joint"));
        CHECK(HasNote(m, "1 alpha map(s)"));
        CHECK(HasNote(m, "transparency"));
    }
    {   // version 3 (no extra block): read, with a note
        ImportedModel m;
        CHECK(ReadMs3dFile(kDir + "rig_v3.ms3d", m, &err) && m.parts.size() == 2 && HasNote(m, "version 3"));
        CHECK(HasNote(m, "no extra vertex weights"));
    }
    {   // malformed input
        ImportedModel m;
        const std::string good = Slurp(kDir + "rig.ms3d");
        CHECK(!good.empty());
        const std::string hdr4 = std::string("MS3D000000") + std::string("\x04\0\0\0", 4);
        const std::string bad[] = {std::string(), "MS3D", "MS3D000000", std::string("NOTMS3D0000") + std::string("\x04\0\0\0", 4), std::string("MS3D000000") + std::string("\x05\0\0\0", 4) + std::string(8, '\0'),
                                   hdr4 + std::string("\xFF\xFF", 2), hdr4 + std::string(60, '\0')};   // no groups, no triangles
        for (const std::string& b : bad) { CHECK(!ReadMs3dBuffer(b, "", m, &err) && !err.empty() && m.nodes.empty() && m.parts.empty()); }
        // every truncation fails or reads cleanly, never crashes
        for (std::size_t cut = 0; cut < good.size(); ++cut) {
            ImportedModel t; std::string e;
            const bool ok = ReadMs3dBuffer(good.substr(0, cut), "", t, &e);
            CHECK(ok || (!e.empty() && t.nodes.empty()));
        }
        // a vertex position of NaN is refused
        std::string nan = good;
        { const float f = std::nanf(""); std::memcpy(&nan[10 + 4 + 2 + 1], &f, 4); }
        CHECK(!ReadMs3dBuffer(nan, "", m, &err) && !err.empty());
        CHECK(!ReadMs3dFile(kDir + "does_not_exist.ms3d", m, &err) && err == "cannot open file");
    }
    {   // a UTF-8 path with characters outside the ANSI code page
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / fs::u8path(u8"okay_ms3d_Müller_立方");
        std::error_code ec;
        fs::create_directories(dir, ec);
        { std::ofstream f(dir / fs::u8path(u8"立方.ms3d"), std::ios::binary); f << Slurp(kDir + "rig.ms3d"); }
        ImportedModel m;
        CHECK(ReadMs3dFile((dir / fs::u8path(u8"立方.ms3d")).u8string(), m, &err) && m.parts.size() == 2);
        fs::remove_all(dir, ec);
    }

    {   // the seam
        CHECK(HasOwnReader(".ms3d") && HasOwnReader(".MS3D"));
        TestUnsetEnv("OKAY_OWN_MS3D");
        SetImportBackend(".ms3d", ImportBackend::Default);
        ImportedModel m;
        if (!AssimpAvailable() && EffectiveImportBackend(".ms3d") == ImportBackend::Default) {
            CHECK(!ImportModelData(kDir + "rig.ms3d", m, ImportBackend::Default, false, &err));
            CHECK(err.find("OKAY_OWN_MS3D") != std::string::npos);
            CHECK(ImportableExtensions().find(".ms3d") == std::string::npos);
        }
        TestSetEnv("OKAY_OWN_MS3D", "1");
        CHECK(EffectiveImportBackend(".ms3d") == ImportBackend::Own);
        CHECK(ImportableExtensions().find(".ms3d") != std::string::npos);
        CHECK(ImportModelData(kDir + "rig.ms3d", m, ImportBackend::Default, false, &err) && m.backend == "own" && m.parts.size() == 2);
        TestSetEnv("OKAY_OWN_MS3D", "0");
        CHECK(EffectiveImportBackend(".ms3d") != ImportBackend::Own);
        CHECK(SetImportBackend(".ms3d", ImportBackend::Own));
        CHECK(EffectiveImportBackend(".ms3d") == ImportBackend::Own);
        {
            Scene scene; scene.physicsEnabled = false;
            bool ok = false;
            GameObject* root = ImportModelScene(scene, kDir + "rig.ms3d", &ok);
            CHECK(ok && root != nullptr);
            if (root) {
                CHECK(root->GetComponentsInChildren<MeshRenderer>(true).size() == 2);
                auto sk = root->GetComponentsInChildren<SkinnedMesh>(true);
                CHECK(sk.size() == 2);
                if (!sk.empty()) { CHECK(sk[0]->joints.size() == 3); for (Transform* j : sk[0]->joints) CHECK(j != nullptr); }
                ModelAnimator* ma = root->GetComponent<ModelAnimator>();
                CHECK(ma && ma->clips.size() == 1 && ma->clips[0].nodes.size() == 2);
            }
        }
        bool mok = false; Mesh merged = ImportModel(kDir + "rig.ms3d", &mok);
        CHECK(mok && merged.TriangleCount() == 4);
        {   // the one-static-mesh mode: skins and clips are gone, one root
            ImportedModel f;
            CHECK(ImportModelData(kDir + "rig.ms3d", f, ImportBackend::Default, true, &err) && f.nodes.size() == 1 && f.parts.size() == 2 && f.clips.empty() && f.parts[0].bones.empty());
        }
        SetImportBackend(".ms3d", ImportBackend::Default);
        TestUnsetEnv("OKAY_OWN_MS3D");
    }

    TEST_MAIN_RESULT();
}
