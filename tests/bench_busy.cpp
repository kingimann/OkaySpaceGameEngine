// Busy-scene frame bench. Not a test and not in CMake: a scene with 100 NPCs, 2000 static colliders, 50 animated
// characters, a voxel terrain and a first-person player, stepped for a few thousand frames; prints Scene::Update
// microseconds (median, p95) and the per-system split (SystemCost rows, medians in ms) so a per-frame waste can be
// found and its fix measured before / after:
//   OKAY_OLD_SCENE_QUERIES=1 bench_busy      # the old scene-wide type search
//   bench_busy [--frames N] [--npcs N] [--colliders N] [--chars N] [--cg N]   (--cg N: N frames only, for callgrind)
#include <Okay.hpp>
#include "okay/Core/SystemCost.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

using namespace okay;
using Clock = std::chrono::steady_clock;

int main(int argc, char** argv) {
    int frames = 1500, npcs = 100, colliders = 2000, chars = 50, cg = 0;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) frames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--npcs") && i + 1 < argc) npcs = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--colliders") && i + 1 < argc) colliders = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--chars") && i + 1 < argc) chars = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--cg") && i + 1 < argc) cg = std::atoi(argv[++i]);
    }
    Scene sc("busy");
    sc.physicsEnabled = true;
    sc.UseFixedStepPhysics(true);
    GameObject* f = sc.CreateGameObject("Floor");
    f->transform->localPosition = {0, -0.5f, 0};
    f->AddComponent<BoxCollider3D>()->size = {800, 1, 800};
    unsigned s = 777;
    auto rnd = [&]() { s = s * 1664525u + 1013904223u; return (float)((s >> 8) & 0xFFFF) / 65535.0f; };
    for (int i = 0; i < colliders; ++i) {
        GameObject* b = sc.CreateGameObject("Prop");
        b->transform->localPosition = {-350.0f + rnd() * 700.0f, 1.0f, 30.0f + rnd() * 320.0f};
        b->AddComponent<BoxCollider3D>()->size = {2, 2, 2};
    }
    for (int i = 0; i < npcs; ++i) {
        GameObject* p = sc.CreateGameObject("NPC");
        p->transform->localPosition = {-60.0f + rnd() * 120.0f, 0.05f, -60.0f + rnd() * 110.0f};
        p->AddComponent<Rigidbody3D>();
        auto* c = p->AddComponent<BoxCollider3D>(); c->size = {0.6f, 1.8f, 0.6f}; c->offset = {0, 0.9f, 0};
        auto* n = p->AddComponent<NPCController>();
        n->behavior = (int)NPCController::Behavior::Wander; n->targetName = "Nobody";
    }
    for (int i = 0; i < chars; ++i) {
        GameObject* p = sc.CreateGameObject("Animated");
        p->transform->localPosition = {-30.0f + rnd() * 60.0f, 0.05f, -100.0f - rnd() * 40.0f};
        p->AddComponent<Character>()->Apply();
        HumanoidLibrary::EnsureAnimator(p);
        p->GetComponent<Character>()->moveSpeed = 1.5f;
    }
    {
        GameObject* v = sc.CreateGameObject("Voxel");
        v->transform->localPosition = {0, 0, -160.0f};
        auto* vt = v->AddComponent<VoxelTerrain>();
        vt->Resize(32, 16, 32); vt->voxelSize = 0.8f; vt->FillSlab(0.12f); vt->Add({0, 1.4f, 0}, 3.0f, 1.0f);
    }
    GameObject* pl = Templates::BuildPlayerBody(sc, {0, 0.05f, 0}, "Player");
    PlayerController::Add(pl, PlayerController::ViewMode::First, PlayerView::View::First);
    Templates::MountEyeCamera(sc, pl, "Main Camera");
    sc.Start();
    const float dt = 1.0f / 60.0f;
    const int warm = cg > 0 ? 30 : 240;
    for (int i = 0; i < warm; ++i) { Input::FeedKeys({'w'}); sc.Update(dt); }
    if (cg > 0) { for (int i = 0; i < cg; ++i) { Input::FeedKeys({'w'}); sc.Update(dt); } return 0; }
    Prof().enabled = true;
    std::vector<double> us;
    std::vector<std::vector<double>> rows(9);
    for (int i = 0; i < frames; ++i) {
        Input::FeedKeys(i < frames - 200 ? std::vector<char>{'w'} : std::vector<char>{});
        Prof().BeginFrame();
        const auto t0 = Clock::now();
        sc.Update(dt);
        const auto t1 = Clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        Prof().EndFrame(ms);
        us.push_back(ms * 1000.0);
        const auto r = SystemCost::Rows(Prof(), 1);
        for (size_t k = 0; k < r.size() && k < rows.size(); ++k) rows[k].push_back(r[k].avgMs);
    }
    auto med = [](std::vector<double> v) { if (v.empty()) return 0.0; std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
    auto p95 = [](std::vector<double> v) { if (v.empty()) return 0.0; std::sort(v.begin(), v.end()); return v[(size_t)(v.size() * 0.95)]; };
    std::printf("busy scene: %zu objects (%d NPCs, %d colliders, %d animated characters, voxel terrain, 1 player)%s\n", sc.Objects().size(), npcs, colliders, chars,
                Scene::OldSceneQueries() ? "  [OLD scene queries]" : "");
    std::printf("Scene::Update median %.1f us  p95 %.1f us\n", med(us), p95(us));
    const char* names[9] = {"Physics", "Scripts", "Animation", "Audio", "UI", "Render", "Terrain", "Other CPU", "Idle"};
    for (int k = 0; k < 7; ++k) std::printf("  %-10s median %7.3f ms\n", names[k], med(rows[(size_t)k]));
    return 0;
}
