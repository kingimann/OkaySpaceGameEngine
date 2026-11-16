// Scenes shared by tests/test_perf_nav_fallback.cpp (Finding 107): static levels for the no-NavMesh
// grid search, built the same way every time (a fixed pseudo-random sequence), so the paths found in
// them can be compared against golden values recorded before the probe index existed.
#pragma once
#include <Okay.hpp>
#include "okay/AI/NavGrid3D.hpp"
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace navperf {

using namespace okay;

inline GameObject* Box(Scene& s, const char* name, Vec3 pos, Vec3 scale, Quat rot = Quat::Identity, bool trigger = false) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->localPosition = pos; g->transform->localScale = scale; g->transform->localRotation = rot;
    auto* c = g->AddComponent<BoxCollider3D>();
    c->size = {1, 1, 1};
    c->isTrigger = trigger;
    g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    return g;
}

// A small linear congruential sequence (the constants are the common 32-bit ones from Knuth's MMIX
// discussion, reduced to 32 bits); only used to place rocks the same way on every run.
struct Lcg {
    std::uint32_t s;
    explicit Lcg(std::uint32_t seed) : s(seed) {}
    float Next() { s = s * 1664525u + 1013904223u; return (float)((s >> 8) & 0xFFFFFFu) / (float)0x1000000; }
    float Range(float a, float b) { return a + (b - a) * Next(); }
};

// Golden scene: a 60 x 60 floor with 150 one-metre rocks, a few rotated slabs, a sphere and a capsule
// collider, a trigger volume (ignored by the search), a raised platform with a step, and a hole.
inline void BuildGoldenScene(Scene& s) {
    s.physicsEnabled = false;
    Box(s, "Ground", {0, -0.5f, 0}, {60, 1, 60});
    Lcg r(12345u);
    for (int i = 0; i < 150; ++i) {
        float x = r.Range(-25.0f, 25.0f), z = r.Range(-25.0f, 25.0f);
        if (x * x + z * z < 4.0f) continue;   // keep the origin clear for the starts
        Box(s, "Rock", {x, 0.5f, z}, {1, 1, 1});
    }
    Box(s, "Slab", {6, 0.75f, -4}, {6, 1.5f, 0.6f}, Quat::Euler(0, 35, 0));
    Box(s, "Slab", {-7, 0.75f, 8}, {0.6f, 1.5f, 7}, Quat::Euler(0, -20, 0));
    {
        GameObject* g = s.CreateGameObject("Ball");
        g->transform->localPosition = {3, 0.8f, 9};
        g->AddComponent<SphereCollider3D>()->radius = 1.2f;
        g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    }
    {
        GameObject* g = s.CreateGameObject("Pillar");
        g->transform->localPosition = {-4, 1.0f, -6};
        auto* c = g->AddComponent<CapsuleCollider3D>(); c->radius = 0.7f; c->height = 2.0f;
        g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    }
    Box(s, "Trigger", {0, 1, 5}, {4, 2, 4}, Quat::Identity, true);
    Box(s, "Platform", {14, 0.15f, 14}, {6, 0.3f, 6});    // a 0.3 m step up: walkable
    Box(s, "Ledge", {-14, 1.0f, -14}, {6, 2.0f, 6});      // 2 m up: a wall, not a step
    // A walled-off pen: the goal inside cannot be reached.
    for (int k = -3; k <= 3; ++k) {
        Box(s, "Pen", {20.0f + k, 1, -20.0f - 3.5f}, {1, 2, 1});
        Box(s, "Pen", {20.0f + k, 1, -20.0f + 3.5f}, {1, 2, 1});
        Box(s, "Pen", {20.0f - 3.5f, 1, -20.0f + k}, {1, 2, 1});
        Box(s, "Pen", {20.0f + 3.5f, 1, -20.0f + k}, {1, 2, 1});
    }
}

struct Query { Vec3 from, to; int maxExpand; };
inline std::vector<Query> GoldenQueries() {
    return {
        {{0, 0, 0}, {18, 0, 3}, 4096},
        {{0, 0, 0}, {-15, 0, 12}, 4096},
        {{1, 0, -1}, {9, 0, -9}, 4096},
        {{0, 0, 0}, {14, 0.3f, 14}, 4096},       // onto the platform
        {{0, 0, 0}, {-14, 2.0f, -14}, 4096},     // onto the ledge: no route (a wall)
        {{-1, 0, 1}, {20, 0, -20}, 1500},        // into the pen: no route within the budget
        {{0, 0, 0}, {0, 0, 9}, 4096},            // through the trigger and past the ball
        {{-2, 0, 2}, {-22, 0, 22}, 4096},
        {{0.3f, 0, -0.2f}, {23, 0, 18}, 4096},
        {{0, 0, 0}, {0.5f, 0, 0.2f}, 4096},      // adjacent: no plan
    };
}

// Prints a path with exact (hexadecimal) floats, one line per query: used to record the goldens.
inline std::string PathText(const std::vector<Vec3>& p) {
    std::string out = std::to_string(p.size());
    char buf[96];
    for (const Vec3& v : p) { std::snprintf(buf, sizeof buf, " %a,%a,%a", v.x, v.y, v.z); out += buf; }
    return out;
}

// Timing scene (the shape of Finding 107's repro, smaller floor count kept): a 400 x 400 floor with
// `rocks` one-metre rocks. `pen` walls off the goal at (60, 0, 0).
inline void BuildRockField(Scene& s, int rocks, bool pen) {
    s.physicsEnabled = false;
    Box(s, "Ground", {0, -0.5f, 0}, {400, 1, 400});
    Lcg r(777u);
    for (int i = 0; i < rocks; ++i) {
        float x = r.Range(-150.0f, 150.0f), z = r.Range(-150.0f, 150.0f);
        if ((x - 60.0f) * (x - 60.0f) + z * z < 25.0f) continue;   // keep the goal area free
        if (x * x + z * z < 100.0f) continue;                       // and the agents' start area
        Box(s, "Rock", {x, 0.5f, z}, {1, 1, 1});
    }
    if (pen)
        for (int k = -3; k <= 3; ++k) {
            Box(s, "Pen", {60.0f + k, 1, -3.5f}, {1, 2, 1});
            Box(s, "Pen", {60.0f + k, 1, 3.5f}, {1, 2, 1});
            Box(s, "Pen", {60.0f - 3.5f, 1, (float)k}, {1, 2, 1});
            Box(s, "Pen", {60.0f + 3.5f, 1, (float)k}, {1, 2, 1});
        }
}

// The example scene shipped as examples/navigation/nav_crowd.okayscene (docs/try_it.md): no NavMesh, a 120 m
// floor, 600 rocks in 12 groups of 50 (so the Hierarchy has a long list too), a "Player" that wanders the
// field and 40 NPCs that follow it with pathfinding on. Written by test_perf_nav_fallback when
// OKAY_NAV_WRITE_EXAMPLE names the output file; the shipped file came from that.
inline void BuildCrowdExample(Scene& s) {
    s.SetName("Navigation crowd (no NavMesh)");
    auto meshed = [&](GameObject* g, Mesh m, Color c) {
        auto* mr = g->AddComponent<MeshRenderer>(); mr->mesh = std::move(m); mr->color = c;
    };
    GameObject* floor = Box(s, "Ground", {0, -0.5f, 0}, {120, 1, 120});
    meshed(floor, Mesh::Cube(), Color{0.36f, 0.42f, 0.34f, 1.0f});
    Lcg r(2026u);
    for (int gi = 0; gi < 12; ++gi) {
        GameObject* group = s.CreateGameObject(("Rocks " + std::to_string(gi + 1)).c_str());
        for (int k = 0; k < 50; ++k) {
            float x = 0, z = 0;
            do { x = r.Range(-55.0f, 55.0f); z = r.Range(-55.0f, 55.0f); } while (x * x + z * z < 36.0f);
            const float h = r.Range(0.6f, 1.6f), w = r.Range(0.7f, 1.6f);
            const std::string rockName = "Rock " + std::to_string(k + 1);   // unique names: the Problems panel warns about duplicates
            GameObject* rock = Box(s, rockName.c_str(), {x, h * 0.5f, z}, {w, h, w}, Quat::Euler(0, r.Range(0.0f, 90.0f), 0));
            meshed(rock, Mesh::Cube(), Color{0.52f, 0.5f, 0.47f, 1.0f});
            rock->transform->SetParent(group->transform, true);
        }
    }
    GameObject* player = s.CreateGameObject("Player");
    player->transform->localPosition = {0, 0.5f, 0};
    meshed(player, Mesh::Sphere(0.5f), Color{0.95f, 0.75f, 0.2f, 1.0f});
    auto* wander = player->AddComponent<NPCController>();
    wander->behavior = (int)NPCController::Behavior::Wander;
    wander->wanderRadius = 45.0f; wander->moveSpeed = 3.0f; wander->targetName = "";
    GameObject* crowd = s.CreateGameObject("Crowd");
    for (int i = 0; i < 40; ++i) {
        GameObject* g = s.CreateGameObject(("Follower " + std::to_string(i + 1)).c_str());
        g->transform->localPosition = {(float)(i % 8) * 1.6f - 5.6f, 0.5f, (float)(i / 8) * 1.6f + 10.0f};
        meshed(g, Mesh::Capsule(0.35f, 1.0f), Color{0.3f, 0.55f, 0.95f, 1.0f});
        auto* npc = g->AddComponent<NPCController>();
        npc->behavior = (int)NPCController::Behavior::Follow;
        npc->targetName = "Player";
        npc->usePathfinding = true;
        npc->sightRange = 500.0f; npc->fieldOfView = 360.0f;
        npc->attackRange = 2.0f; npc->moveSpeed = 3.5f; npc->runSpeed = 4.5f;
        g->transform->SetParent(crowd->transform, true);
    }
    GameObject* light = s.CreateGameObject("Directional Light");
    light->transform->localPosition = {0, 10, 0};
    light->transform->localRotation = Quat::Euler(50, -30, 0);
    light->AddComponent<Light>();
    GameObject* cam = s.CreateGameObject("Main Camera");
    cam->transform->localPosition = {0, 45, 55};   // the engine camera looks down -Z
    cam->transform->localRotation = Quat::Euler(-40, 0, 0);   // tilted down at the field
    cam->AddComponent<Camera>()->main = true;
}

} // namespace navperf
