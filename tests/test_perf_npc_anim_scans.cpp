// Two whole-scene scans that ran far too often (Grok's findings 102 and 106 in issue #888).
//
//  * Finding 102: every NPC with a Separation Radius looked for the other NPCs by walking every object
//    and component in the scene, every frame. A crowd in a big level paid (NPCs x scene objects).
//    Now the NPC list is built once per scene change and shared, so the per-NPC cost is the NPC count.
//  * Finding 106: any spawn or destroy made every ModelAnimator do whole-scene Scene::Find searches: a
//    "refresh" call with an empty name that can never match, and the eight "Hips"/"Spine" names of
//    Legs Follow Travel, looked up every frame even with that option off. So one bullet per frame cost
//    (characters x scene objects). Now the refresh only rebuilds the character's own bone map, and the
//    hips/spine are looked up only when there is a turn to write.
//
// The timings are reported, and checked only as RATIOS inside one run with a generous margin (the old
// code was many times slower on these exact scenes), so a slow or busy CI machine does not fail them.
// The behaviour checks compare NPC positions and animated bone rotations with values recorded from
// the old code before the fix. The FNV hash of the raw float bits is printed too; on the machine the
// goldens were taken on it was identical before and after (see the PR text).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Animation/AnimationClip.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/NPCController.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace okay;

namespace {

// A component on every "level" object, so the scene walk has something to look at, like real props.
struct Prop : public Behaviour {};

double MsPerFrame(Scene& s, int frames, const std::function<void(int)>& perFrame = {}) {
    // Best of three batches: the least noisy number a shared machine gives.
    double best = 1e30;
    for (int batch = 0; batch < 3; ++batch) {
        auto t0 = std::chrono::steady_clock::now();
        for (int f = 0; f < frames; ++f) {
            if (perFrame) perFrame(f);
            s.Update(1.0f / 60.0f);
        }
        auto t1 = std::chrono::steady_clock::now();
        best = std::min(best, std::chrono::duration<double, std::milli>(t1 - t0).count() / frames);
    }
    return best;
}

void AddLevel(Scene& s, int count, bool props = true) {
    for (int i = 0; i < count; ++i) {
        GameObject* g = s.CreateGameObject("Level" + std::to_string(i));
        g->transform->localPosition = {500.0f + (float)(i % 100), 0.0f, 500.0f + (float)(i / 100)};
        if (props) g->AddComponent<Prop>();
    }
}

NPCController* AddNpc(Scene& s, const std::string& name, Vec3 at, Vec3 waypoint, float separation) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->localPosition = at;
    auto* ai = g->AddComponent<NPCController>();
    ai->behavior = (int)NPCController::Behavior::Patrol;
    ai->targetName = "Player";
    ai->turnSpeed = 0.0f;
    ai->moveSpeed = 2.0f;
    ai->separationRadius = separation;
    ai->waypoints = {waypoint};
    return ai;
}

double NpcScene(int npcs, int level, float separation) {
    Scene s("npc_perf");
    // The NPCs' target first in the scene and far away: finding it by name is cheap and it is never
    // seen, so the frame is mostly movement plus separation (what this measures).
    s.CreateGameObject("Player")->transform->localPosition = {-1000.0f, 0.0f, -1000.0f};
    AddLevel(s, level);
    const int side = (int)std::ceil(std::sqrt((double)npcs));
    for (int i = 0; i < npcs; ++i) {
        const float x = (float)(i % side), z = (float)(i / side);
        AddNpc(s, "Npc" + std::to_string(i), {x, 0, z}, {x + 30.0f, 0, z}, separation);
    }
    s.Start();
    s.Update(1.0f / 60.0f);
    return MsPerFrame(s, 20);
}

std::uint64_t Fnv(std::uint64_t h, float f) {
    std::uint32_t bits; std::memcpy(&bits, &f, 4);
    for (int i = 0; i < 4; ++i) { h ^= (bits >> (8 * i)) & 0xFFu; h *= 1099511628211ull; }
    return h;
}

// A character: a root with a ModelAnimator and a chain of `bones` children, one clip that turns every
// bone (rotation keys), plus optional tracks for nodes outside the rig.
ModelAnimator* AddCharacter(Scene& s, const std::string& name, Vec3 at, int bones,
                            const std::vector<std::string>& outside = {}) {
    GameObject* root = s.CreateGameObject(name);
    root->transform->localPosition = at;
    Transform* parent = root->transform;
    for (int b = 0; b < bones; ++b) {
        GameObject* bone = s.CreateGameObject("Bone" + std::to_string(b));
        bone->transform->SetParent(parent, false);
        bone->transform->localPosition = {0, 0.1f, 0};
        parent = bone->transform;
    }
    auto* an = root->AddComponent<ModelAnimator>();
    an->blendTime = 0.0f;
    ModelAnimator::Clip c;
    c.name = "walk";
    auto addNode = [&](const std::string& node, int salt) {
        ModelAnimator::NodeClip nc{node, AnimationClip{}};
        for (int k = 0; k <= 4; ++k) {
            const float t = k * 0.25f;
            const float a = 0.3f * std::sin(t * 6.2831853f + 0.1f * (float)salt);
            nc.clip.AddKey("rotation.qx", t, std::sin(a * 0.5f));
            nc.clip.AddKey("rotation.qy", t, 0.0f);
            nc.clip.AddKey("rotation.qz", t, 0.0f);
            nc.clip.AddKey("rotation.qw", t, std::cos(a * 0.5f));
        }
        c.nodes.push_back(nc);
    };
    for (int b = 0; b < bones; ++b) addNode("Bone" + std::to_string(b), b);
    for (std::size_t i = 0; i < outside.size(); ++i) addNode(outside[i], 100 + (int)i);
    ModelAnimator::NormalizeClip(c);
    an->clips.push_back(c);
    return an;
}

double AnimScene(int characters, int bones, int level, bool churn) {
    Scene s("anim_perf");
    AddLevel(s, level, false);
    for (int i = 0; i < characters; ++i) AddCharacter(s, "Char" + std::to_string(i), {(float)i, 0, 0}, bones);
    s.Start();
    s.Update(1.0f / 60.0f);
    GameObject* last = nullptr;
    auto spawnAndDestroy = [&](int f) {
        if (!churn) return;
        if (last) s.Destroy(last);               // one bullet removed...
        last = s.CreateGameObject("Bullet" + std::to_string(f));   // ...and one fired, every frame
    };
    return MsPerFrame(s, 20, spawnAndDestroy);
}

// NPCs whose target is found by name: `playerLast` puts the target after the whole level in scene order,
// so a by-name search walks every object first (what NPCController::Target paid every frame per NPC).
double TargetScene(int npcs, int level, bool playerLast) {
    Scene s("npc_target_perf");
    auto addPlayer = [&] { s.CreateGameObject("Player")->transform->localPosition = {-1000.0f, 0.0f, -1000.0f}; };
    if (!playerLast) addPlayer();
    // Level names as long as "Player" ("Pr0000" ...), like real props ("Rock01"), so each name test is a real compare.
    for (int i = 0; i < level; ++i) {
        char nm[16];
        std::snprintf(nm, sizeof nm, "Pr%04d", i % 10000);
        s.CreateGameObject(nm)->transform->localPosition = {500.0f + (float)(i % 100), 0.0f, 500.0f + (float)(i / 100)};
    }
    if (playerLast) addPlayer();
    const int side = (int)std::ceil(std::sqrt((double)npcs));
    for (int i = 0; i < npcs; ++i) {
        const float x = (float)(i % side) * 2.0f, z = (float)(i / side) * 2.0f;
        AddNpc(s, "Npc" + std::to_string(i), {x, 0, z}, {x + 30.0f, 0, z}, 0.0f);
    }
    s.Start();
    s.Update(1.0f / 60.0f);
    return MsPerFrame(s, 20);
}

}  // namespace

int main() {
    RUN_SUITE("perf_npc_anim_scans");

    // ---- Finding 102: separation cost in a big level -------------------------------------------
    {
        const double off = NpcScene(150, 4000, 0.0f);
        const double on  = NpcScene(150, 4000, 1.5f);
        std::printf("  npc separation, 150 NPCs + 4000 level objects: off %.3f ms, on %.3f ms per frame (x%.2f)\n",
                    off, on, on / std::max(off, 1e-6));
        // The old code: about 8x. Separation should now cost little next to the rest of the NPC update.
        CHECK(on < off * 3.0 + 0.05);
    }

    // ---- Finding 106: one spawn + one destroy per frame with many animated characters ----------
    {
        const double still = AnimScene(300, 20, 8000, false);
        const double churn = AnimScene(300, 20, 8000, true);
        std::printf("  animators, 300 characters x 20 bones + 8000 level objects: still %.3f ms, "
                    "1 spawn + 1 destroy per frame %.3f ms (x%.2f)\n", still, churn, churn / std::max(still, 1e-6));
        CHECK(churn < still * 3.0 + 0.05);
    }

    // ---- Same NPC positions as the old code (separation on, NPCs spawned and destroyed mid-run) --
    {
        Scene s("npc_golden");
        AddLevel(s, 50);
        std::vector<NPCController*> npcs;
        for (int i = 0; i < 25; ++i) {
            const float x = (float)(i % 5) * 0.6f, z = (float)(i / 5) * 0.6f;
            npcs.push_back(AddNpc(s, "Npc" + std::to_string(i), {x, 0, z}, {8.0f, 0, 1.2f}, 1.5f));
        }
        s.Start();
        for (int f = 0; f < 90; ++f) {
            if (f == 20) npcs.push_back(AddNpc(s, "Late", {1.0f, 0, 3.0f}, {8.0f, 0, 1.2f}, 1.5f));
            if (f == 40) { s.Destroy(npcs[7]->gameObject); npcs[7] = nullptr; }
            if (f == 55) npcs[3]->separationRadius = 0.0f;   // one stops keeping its distance (others still avoid it)
            s.Update(1.0f / 60.0f);
        }
        std::uint64_t h = 1469598103934665603ull;
        std::vector<float> got;
        for (NPCController* n : npcs) {
            if (!n) continue;
            const Vec3 p = n->transform->Position();
            h = Fnv(Fnv(h, p.x), p.z);
            got.push_back(p.x); got.push_back(p.z);
        }
        std::printf("  npc golden hash %016llx\n", (unsigned long long)h);
        // Recorded from the old code (before the fix), x then z for each NPC still alive, in order.
        static const float golden[] = {
            2.548892f, -0.616359f,
            3.258224f, -1.196054f,
            3.944515f, -0.757270f,
            4.505771f, -0.282811f,
            5.343775f, -0.500912f,
            2.689558f, 0.225965f,
            3.552862f, 0.144163f,
            4.753953f, 0.276171f,
            5.396178f, 0.511328f,
            2.729758f, 0.981731f,
            3.585099f, 1.063297f,
            4.158503f, 0.852593f,
            4.792378f, 1.139124f,
            5.398780f, 1.271812f,
            2.644098f, 1.698534f,
            3.581930f, 1.841449f,
            4.173918f, 2.091564f,
            4.770896f, 2.168756f,
            5.394835f, 1.941380f,
            2.436632f, 2.718536f,
            3.325777f, 2.571238f,
            3.938365f, 3.223387f,
            4.569314f, 3.374491f,
            5.341156f, 2.868141f,
            3.087967f, 3.801137f,
        };
        const std::size_t n = sizeof(golden) / sizeof(golden[0]);
        CHECK(got.size() == n);
        int bad = 0;
        for (std::size_t i = 0; i < std::min(n, got.size()); ++i)
            if (std::fabs(got[i] - golden[i]) > 1e-3f) ++bad;
        CHECK(bad == 0);
        if (bad || got.size() != n) {
            std::printf("  npc positions now:\n");
            for (std::size_t i = 0; i < got.size(); i += 2) std::printf("    %.6ff, %.6ff,\n", got[i], got[i + 1]);
        }
    }

    // ---- Same animated bones as the old code, with spawns/destroys every frame, a node outside the
    //      rig, a node that only appears later, and one that never exists ---------------------------
    {
        Scene s("anim_golden");
        AddLevel(s, 30);
        GameObject* prop = s.CreateGameObject("SharedProp");     // outside every rig: found scene-wide
        std::vector<ModelAnimator*> chars;
        for (int i = 0; i < 3; ++i)
            chars.push_back(AddCharacter(s, "Char" + std::to_string(i), {(float)i * 2.0f, 0, 0}, 6,
                                         {"SharedProp", "LateProp", "NeverThere"}));
        chars[1]->speed = 1.3f;
        s.Start();
        GameObject* late = nullptr;
        GameObject* bullet = nullptr;
        for (int f = 0; f < 70; ++f) {
            if (bullet) s.Destroy(bullet);
            bullet = s.CreateGameObject("Bullet");
            if (f == 25) late = s.CreateGameObject("LateProp");   // appears: animators must pick it up
            s.Update(1.0f / 60.0f);
        }
        std::uint64_t h = 1469598103934665603ull;
        std::vector<float> got;
        auto rec = [&](Transform* t) {
            const Quat q = t->localRotation;
            h = Fnv(Fnv(h, q.x), q.w);
            got.push_back(q.x); got.push_back(q.w);
        };
        for (ModelAnimator* a : chars) {
            Transform* t = a->transform;
            while (!t->Children().empty()) { t = t->Children()[0]; rec(t); }
        }
        rec(prop->transform);
        rec(late->transform);
        std::printf("  anim golden hash %016llx\n", (unsigned long long)h);
        static const float golden[] = {
            0.110934f, 0.993828f,
            0.114226f, 0.993455f,
            0.116383f, 0.993204f,
            0.117385f, 0.993087f,
            0.117222f, 0.993106f,
            0.115896f, 0.993261f,
            -0.010583f, 0.999944f,
            -0.025224f, 0.999682f,
            -0.039429f, 0.999222f,
            -0.053047f, 0.998592f,
            -0.065927f, 0.997824f,
            -0.077918f, 0.996960f,
            0.110934f, 0.993828f,
            0.114226f, 0.993455f,
            0.116383f, 0.993204f,
            0.117385f, 0.993087f,
            0.117222f, 0.993106f,
            0.115896f, 0.993261f,
            -0.114139f, 0.993465f,
            -0.110810f, 0.993842f,
        };
        const std::size_t n = sizeof(golden) / sizeof(golden[0]);
        CHECK(got.size() == n);
        int bad = 0;
        for (std::size_t i = 0; i < std::min(n, got.size()); ++i)
            if (std::fabs(got[i] - golden[i]) > 1e-4f) ++bad;
        CHECK(bad == 0);
        if (bad || got.size() != n) {
            std::printf("  bone rotations now (qx, qw):\n");
            for (std::size_t i = 0; i < got.size(); i += 2) std::printf("    %.6ff, %.6ff,\n", got[i], got[i + 1]);
        }
        // The node that appeared later is animated (it is not the identity it was created with).
        CHECK(std::fabs(late->transform->localRotation.x) > 1e-4f || std::fabs(late->transform->localRotation.w - 1.0f) > 1e-4f);
    }

    // ---- NPC target lookup: the same target as a by-name search every frame, through renames,
    //      destroys, a target that goes missing, a new object with the name, and a changed targetName --
    {
        Scene s("target_golden");
        s.physicsEnabled = false;
        GameObject* a = s.CreateGameObject("Player");
        a->transform->localPosition = {6.0f, 0.0f, 0.0f};
        AddLevel(s, 40, false);
        std::vector<NPCController*> npcs;
        for (int i = 0; i < 6; ++i) {
            NPCController* n = AddNpc(s, "Hunter" + std::to_string(i), {(float)i * 1.5f - 4.0f, 0, -3.0f},
                                      {(float)i, 0, -8.0f}, 0.0f);
            n->behavior = (int)(i % 2 ? NPCController::Behavior::Chase : NPCController::Behavior::Follow);
            n->sightRange = 60.0f;
            n->fieldOfView = 360.0f;
            n->attackRange = 0.5f;
            n->attackDamage = 0.0f;
            n->turnSpeed = 360.0f;
            npcs.push_back(n);
        }
        s.Start();
        GameObject* b = nullptr;
        GameObject* c = nullptr;
        GameObject* lvl3 = s.Find("Level3");
        for (int f = 0; f < 120; ++f) {
            const float t = (float)f / 60.0f;
            a->transform->localPosition = {6.0f * std::cos(t), 0.0f, 6.0f * std::sin(t)};
            if (f == 15) { b = s.CreateGameObject("Player"); b->transform->localPosition = {-6.0f, 0, 4.0f}; }  // A still first
            if (f == 30) a->name = "Gone";                       // the target renamed away: B now
            if (f == 45) { s.Destroy(b); b = nullptr; }          // the target destroyed: none
            if (f == 50) { lvl3->name = "Player"; lvl3->transform->localPosition = {0.0f, 0, -9.0f}; }   // renamed in
            if (f == 65) { c = s.CreateGameObject("Player"); c->transform->localPosition = {0.0f, 0, 9.0f}; }  // Level3 still first
            if (f == 75) npcs[0]->targetName = "Gone";           // one NPC follows A again
            if (f == 90) lvl3->name = "Level3";                  // renamed away: C now
            s.Update(1.0f / 60.0f);
        }
        std::vector<float> got;
        std::uint64_t h = 1469598103934665603ull;
        for (NPCController* n : npcs) {
            const Vec3 p = n->transform->Position();
            h = Fnv(Fnv(h, p.x), p.z);
            got.push_back(p.x); got.push_back(p.z);
        }
        std::printf("  target golden hash %016llx\n", (unsigned long long)h);
        // Recorded from the old code (Scene::Find every frame), before the target cache.
        static const float golden[] = {
            -2.540854f, -1.422561f,
            -0.820224f, -1.522829f,
            -0.391913f, -2.387288f,
            0.495974f, -1.429344f,
            1.664743f, -2.215282f,
            1.722296f, -0.958107f,
        };
        const std::size_t n = sizeof(golden) / sizeof(golden[0]);
        CHECK(got.size() == n);
        int bad = 0;
        for (std::size_t i = 0; i < std::min(n, got.size()); ++i)
            if (std::fabs(got[i] - golden[i]) > 1e-3f) ++bad;
        CHECK(bad == 0);
        if (bad || got.size() != n) {
            std::printf("  target positions now:\n");
            for (std::size_t i = 0; i < got.size(); i += 2) std::printf("            %.6ff, %.6ff,\n", got[i], got[i + 1]);
        }
        (void)c;
    }

    // ---- NPC target lookup cost: the target last in a big level vs first ----------------------
    {
        const double first = TargetScene(600, 10000, false);
        const double last  = TargetScene(600, 10000, true);
        std::printf("  npc target by name, 600 NPCs + 10000 level objects: target first %.3f ms, target last %.3f ms "
                    "per frame (x%.2f)\n", first, last, last / std::max(first, 1e-6));
        CHECK(last < first * 3.0 + 0.05);
    }

    // ---- Legs Follow Travel never turns another character's hips ----------------------------------
    //      The walker's rig has no "Hips"/"Spine" bones; another object in the scene does. The hip turn
    //      must look only inside the walker's own model (it used to fall back to Scene::Find).
    {
        Scene s("hips_scope");
        s.physicsEnabled = false;
        GameObject* other = s.CreateGameObject("OtherCharacter");
        GameObject* oHips = s.CreateGameObject("Hips");
        oHips->transform->SetParent(other->transform, false);
        GameObject* oSpine = s.CreateGameObject("Spine");
        oSpine->transform->SetParent(oHips->transform, false);
        const Quat hips0 = oHips->transform->localRotation, spine0 = oSpine->transform->localRotation;

        GameObject* walker = s.CreateGameObject("Walker");
        auto* ch = walker->AddComponent<Character>();
        ch->Apply();
        GameObject* model = s.CreateGameObject("Model");
        model->transform->SetParent(walker->transform, false);
        GameObject* pelvis = s.CreateGameObject("Pelvis");
        pelvis->transform->SetParent(model->transform, false);
        auto* ma = model->AddComponent<ModelAnimator>();
        for (const char* name : {"Idle", "Walk"}) {
            ModelAnimator::Clip c; c.name = name;
            ModelAnimator::NodeClip nc; nc.node = "Pelvis";
            nc.clip.AddKey("position.y", 0.0f, 0.0f);
            nc.clip.AddKey("position.y", 1.0f, 0.0f);
            c.nodes.push_back(nc);
            ma->clips.push_back(c);
        }
        ma->idleClip = "Idle"; ma->walkClip = "Walk";
        ma->legsFollowTravel = true;
        ch->moveSpeed = 2.0f;
        ch->moveAngle = 60.0f;
        s.Start();
        for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);
        const float hipsTurn = Quat::Angle(hips0, oHips->transform->localRotation);
        const float spineTurn = Quat::Angle(spine0, oSpine->transform->localRotation);
        std::printf("  other character's hips turned %.3f deg, spine %.3f deg (must be 0)\n", hipsTurn, spineTurn);
        CHECK(hipsTurn < 1e-3f);
        CHECK(spineTurn < 1e-3f);
    }

    TEST_MAIN_RESULT();
}
