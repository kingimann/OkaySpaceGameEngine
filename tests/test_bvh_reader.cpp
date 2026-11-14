// Our own BVH reader: fixtures from scripts/gen_bvh_fixtures.py (tests/data/models/*.bvh), values computed by hand,
// the import seam (opt-in switch, skeleton-only scene import) and malformed input.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

using namespace okay;

// setenv / unsetenv exist on POSIX only; the Windows C runtime has _putenv_s (an empty value removes the variable).
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
static bool Near(const Vec3& a, const Vec3& b, float e = 1e-4f) {
    return std::fabs(a.x - b.x) < e && std::fabs(a.y - b.y) < e && std::fabs(a.z - b.z) < e;
}
static const ImportedChannel* Chan(const ImportedModel& m, const char* node) {
    if (m.clips.empty()) return nullptr;
    for (const ImportedChannel& c : m.clips[0].channels) if (c.node == node) return &c;
    return nullptr;
}
static bool HasNote(const ImportedModel& m, const char* part) {
    for (const std::string& n : m.notes) if (n.find(part) != std::string::npos) return true;
    return false;
}

int main() {
    RUN_SUITE("bvh_reader");
    std::string err;

    for (const char* file : {"walk.bvh", "walk_crlf.bvh"}) {
        ImportedModel m;
        CHECK(ReadBvhFile(kDir + file, m, &err));
        CHECK(m.backend == "own" && m.parts.empty());
        CHECK(m.nodes.size() == 4);
        if (m.nodes.size() != 4) continue;
        CHECK(m.nodes[0].name == "Hips" && m.nodes[0].parent == -1 && Near(m.nodes[0].position, Vec3{0, 1, 0}));
        CHECK(m.nodes[1].name == "Spine" && m.nodes[1].parent == 0 && Near(m.nodes[1].position, Vec3{0, 0.5f, 0}));
        CHECK(m.nodes[2].name == "Head" && m.nodes[2].parent == 1);
        CHECK(m.nodes[3].name == "Left Arm" && m.nodes[3].parent == 0 && Near(m.nodes[3].position, Vec3{0.4f, 0.4f, 0}));   // a name with a space
        CHECK(HasNote(m, "End Site"));
        CHECK(m.clips.size() == 1 && m.clips[0].name == "motion" && m.clips[0].channels.size() == 4);
        const ImportedChannel* h = Chan(m, "Hips");
        const ImportedChannel* s = Chan(m, "Spine");
        const ImportedChannel* a = Chan(m, "Left Arm");
        CHECK(h && s && a);
        if (!(h && s && a)) continue;
        CHECK(h->position.size() == 3 && h->rotation.size() == 3);
        CHECK_NEAR(h->position[1].time, 0.5f, 1e-6); CHECK_NEAR(h->position[2].time, 1.0f, 1e-6);   // frame time 0.5
        CHECK(Near(h->position[0].value, Vec3{0, 1, 0}));            // offset + 0
        CHECK(Near(h->position[1].value, Vec3{1, 1.5f, -2}));        // offset + channels
        CHECK(Near(h->position[2].value, Vec3{2, 1, 0}));
        CHECK(Near(h->rotation[0].value * Vec3{1, 0, 0}, Vec3{1, 0, 0}));
        CHECK(Near(h->rotation[1].value * Vec3{1, 0, 0}, Vec3{0, 1, 0}));   // Zrotation 90
        // frame 2: Zrotation 90 listed before Xrotation 90, so the z turn is outermost: Rz * Rx
        CHECK(Near(h->rotation[2].value * Vec3{0, 0, 1}, Vec3{1, 0, 0}));
        CHECK(Near(h->rotation[2].value * Vec3{0, 1, 0}, Vec3{0, 0, 1}));
        CHECK(s->position.empty() && s->rotation.size() == 3);   // rotation-only joint has no position keys
        CHECK(Near(s->rotation[2].value * Vec3{1, 0, 0}, Vec3{std::cos(0.5235988f), std::sin(0.5235988f), 0}));   // Zrotation 30
        CHECK(Near(a->rotation[1].value * Vec3{0, 0, 1}, Vec3{1, 0, 0}));   // Yrotation 90
        for (const ImportedKeyQ& k : h->rotation) CHECK(std::fabs(std::sqrt(k.value.x * k.value.x + k.value.y * k.value.y + k.value.z * k.value.z + k.value.w * k.value.w) - 1.0f) < 1e-5f);
    }

    {   // two ROOT joints, a brace on the name line, partial channels
        ImportedModel m;
        CHECK(ReadBvhFile(kDir + "two_roots.bvh", m, &err));
        CHECK(m.nodes.size() == 3 && m.nodes[0].name == "Skeleton" && m.nodes[1].name == "A" && m.nodes[2].name == "B");
        if (m.nodes.size() == 3) CHECK(m.nodes[1].parent == 0 && m.nodes[2].parent == 0 && Near(m.nodes[2].position, Vec3{5, 0, 0}));
        const ImportedChannel* a = Chan(m, "A");
        const ImportedChannel* b = Chan(m, "B");
        CHECK(a && b);
        if (a && b) {
            CHECK(a->position.size() == 2 && a->rotation.empty() && b->position.empty() && b->rotation.size() == 2);
            CHECK(Near(a->position[1].value, Vec3{1, 2, 3}));
            CHECK_NEAR(a->position[1].time, 0.1f, 1e-6);
            CHECK(Near(b->rotation[1].value * Vec3{0, 0, 1}, Vec3{std::sin(0.7853982f), 0, std::cos(0.7853982f)}));   // Yrotation 45
        }
        CHECK(HasNote(m, "ROOT joints"));
    }

    {   // malformed input
        ImportedModel m;
        const char* bad[] = {
            "", "HIERARCHY", "MOTION\nFrames: 1\nFrame Time: 1\n0\n",
            "HIERARCHY\nROOT A\n{\nOFFSET 0 0 0\nCHANNELS 3 Xposition Yposition\n}\nMOTION\nFrames: 0\nFrame Time: 1\n",   // channel count
            "HIERARCHY\nROOT A\n{\nOFFSET 0 0 0\nCHANNELS 1 Wrotation\n}\nMOTION\nFrames: 0\nFrame Time: 1\n",          // unknown channel
            "HIERARCHY\nROOT A\n{\nOFFSET 0 0 0\nCHANNELS 1 Xrotation\n}\nMOTION\nFrames: 2\nFrame Time: 1\n1\n",        // too few values
            "HIERARCHY\nROOT A\n{\nOFFSET 0 0 0\nCHANNELS 1 Xrotation\n}\nMOTION\nFrames: 1\nFrame Time: 0\n1\n",        // frame time 0
            "HIERARCHY\nROOT A\n{\nOFFSET 0 0 0\nCHANNELS 1 Xrotation\n}\nMOTION\nFrames: 99999999\nFrame Time: 1\n1\n",  // frame count claim
            "HIERARCHY\nROOT A\n{\nOFFSET 0 0 0\nCHANNELS 1 Xrotation\n}\nMOTION\nFrames: 1\nFrame Time: 1\nnan\n",       // non-finite
            "HIERARCHY\nROOT A\n{\nOFFSET 0 0 0\nCHANNELS 1 Xrotation\n",                                                 // never closed
            "HIERARCHY\nROOT A\n}\n",
            "HIERARCHY\nJOINT A\n{\n}\nMOTION\n",
        };
        for (const char* b : bad) { CHECK(!ReadBvhBuffer(b, m, &err) && !err.empty() && m.nodes.empty()); }
        // A skeleton without frames is fine: nodes, no clip.
        CHECK(ReadBvhBuffer("HIERARCHY\nROOT A\n{\nOFFSET 1 2 3\nCHANNELS 3 Xposition Yposition Zposition\n}\nMOTION\nFrames: 0\nFrame Time: 0.033\n", m, &err));
        CHECK(m.nodes.size() == 1 && m.clips.empty() && Near(m.nodes[0].position, Vec3{1, 2, 3}));
        // Old Mac line breaks (\r only) read too.
        CHECK(ReadBvhBuffer("HIERARCHY\rROOT A\r{\rOFFSET 0 0 0\rCHANNELS 1 Xrotation\r}\rMOTION\rFrames: 1\rFrame Time: 1\r10\r", m, &err) && m.clips.size() == 1);
        CHECK(!ReadBvhFile(kDir + "does_not_exist.bvh", m, &err) && err == "cannot open file");
    }

    {   // the seam
        CHECK(HasOwnReader(".bvh"));
        TestUnsetEnv("OKAY_OWN_BVH");
        SetImportBackend(".bvh", ImportBackend::Default);
        ImportedModel m;
        if (!AssimpAvailable() && EffectiveImportBackend(".bvh") == ImportBackend::Default) {
            CHECK(!ImportModelData(kDir + "walk.bvh", m, ImportBackend::Default, false, &err));
            CHECK(err.find("OKAY_OWN_BVH") != std::string::npos);
            CHECK(ImportableExtensions().find(".bvh") == std::string::npos);
        }
        TestSetEnv("OKAY_OWN_BVH", "1");
        CHECK(EffectiveImportBackend(".bvh") == ImportBackend::Own);
        CHECK(ImportableExtensions().find(".bvh") != std::string::npos);
        CHECK(ImportModelData(kDir + "walk.bvh", m, ImportBackend::Default, false, &err) && m.backend == "own" && m.nodes.size() == 4);
        TestSetEnv("OKAY_OWN_BVH", "0");
        CHECK(EffectiveImportBackend(".bvh") != ImportBackend::Own);
        CHECK(SetImportBackend(".bvh", ImportBackend::Own));
        CHECK(EffectiveImportBackend(".bvh") == ImportBackend::Own);
        // A skeleton-only file builds a hierarchy of objects with one clip and no meshes.
        Scene scene; scene.physicsEnabled = false;
        bool ok = false;
        GameObject* root = ImportModelScene(scene, kDir + "walk.bvh", &ok);
        CHECK(ok && root != nullptr);
        if (root) {
            CHECK(root->GetComponentsInChildren<MeshRenderer>(true).empty());
            ModelAnimator* ma = root->GetComponent<ModelAnimator>();
            CHECK(ma && ma->clips.size() == 1 && ma->clips[0].name == "motion" && ma->clips[0].nodes.size() == 4);
        }
        // The merged-mesh path has nothing to merge.
        bool mok = true; ImportModel(kDir + "walk.bvh", &mok); CHECK(!mok);
        SetImportBackend(".bvh", ImportBackend::Default);
        TestUnsetEnv("OKAY_OWN_BVH");
    }

    TEST_MAIN_RESULT();
}
