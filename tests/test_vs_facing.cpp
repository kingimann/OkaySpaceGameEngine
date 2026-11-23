// Visual Script nodes that use "the way this object faces" must agree with Look At.
//
// The engine's characters and cameras face their own -Z (Camera view, VirtualCamera::ViewForward, the
// character controllers' `rotation * (0,0,-1)`), and the Look At / Rotate Towards nodes turn -Z toward the
// target. Before 2026-11-23 Forward Vector, Shoot, Spawn Projectile, Drop, Raycast 3D's default direction,
// Can See, In Cone and the Gl move / facing nodes used +Z, so Look At followed by Shoot fired backwards.
// Each row here turns an object toward a target on +X (with Look At, or with a facing node) and checks that
// the node acts toward that target. Written from the node help text and plain vector maths; no outside code.
#include "test_framework.hpp"
#include "test_sandbox.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Components/VirtualCamera.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/VisualScript/VsFacing.hpp"
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace okay;

static std::vector<std::string> g_errs;
static void OnErr(const vs::VsRuntimeError& e) { g_errs.push_back(e.nodeType + ": " + e.message); }

static bool Near(const Vec3& a, const Vec3& b, float eps = 1e-3f) {
    return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps && std::fabs(a.z - b.z) < eps;
}
static std::string V(const Vec3& v) {
    char b[96];
    std::snprintf(b, sizeof b, "(%.3f, %.3f, %.3f)", v.x, v.y, v.z);
    return b;
}

// The scene every row uses: "Me" at the origin (the graph's object), "Target" 5 units along +X with a box
// collider and a Health component.
struct World {
    Scene s;
    GameObject* me = nullptr;
    GameObject* target = nullptr;
    VisualScriptComponent* vc = nullptr;
};

static void Build(World& w, const std::string& graph) {
    w.me = w.s.CreateGameObject("Me");
    w.target = w.s.CreateGameObject("Target");
    w.target->transform->SetPosition({5, 0, 0});
    w.target->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
    auto* hp = w.target->AddComponent<HealthStat>();
    hp->playDeathAnim = false; hp->destroyOnDeath = false; hp->respawn = false;
    hp->maxHealth = 100; hp->health = 100;
    w.vc = w.me->AddComponent<VisualScriptComponent>();
    std::string err;
    if (!w.vc->LoadFromText(graph, &err)) { std::printf("  load failed: %s\n%s\n", err.c_str(), graph.c_str()); CHECK(false); }
}

// Forward Vector (variable f) reads `want`, and the object's own -Z (the Look At convention) points there too.
static bool FacesAndReads(World& w, std::string& why, const Vec3& want, float eps = 1e-3f) {
    const Vec3 f = w.vc->GetVariable("f").AsVec3();
    const Vec3 negZ = w.me->transform->Rotation() * Vec3{0, 0, -1};
    why = "Forward Vector " + V(f) + ", -Z " + V(negZ);
    return Near(f, want, eps) && Near(negZ, want, eps);
}

// OnStart -> Look At Target -> `then` (node 3 onward; `then` must start with node 3 and wire its flow).
static std::string AfterLookAt(const std::string& then) {
    return "node 1 OnStart\nentry OnStart 1\nnode 2 LookAt Target\nexec 1 0 2\nexec 2 0 3\n"
           "node 90 FindObject Target\n" + then;
}

static std::string BulletPrefab() {
    static std::string path;
    if (!path.empty()) return path;
    path = "/tmp/vs_facing_bullet.okayprefab";
    Scene s; s.physicsEnabled = false;
    GameObject* b = s.CreateGameObject("Bullet");
    auto* rb = b->AddComponent<Rigidbody3D>();
    rb->gravityScale = 0;
    b->AddComponent<SphereCollider3D>()->radius = 0.1f;
    CHECK(SceneSerializer::SaveObjectToFile(*b, path));
    return path;
}

struct Row {
    const char* name;
    std::string graph;
    std::function<bool(World&, std::string&)> check;
    int frames = 1;
    float dt = 0.1f;
};

int main() {
    RUN_SUITE("vs_facing");
    okaytest::Sandbox sandbox("vs_facing", OKAY_SOURCE_DIR);
    vs::SetRuntimeErrorHook(&OnErr);
    Time::SetTimeScale(1.0f);

    // The helper is the engine's camera and controller convention, not a third one.
    {
        const Quat q = Quat::Euler(10, 37, 5);
        CHECK(Near(vs::VsFacing(q), VirtualCamera::ViewForward(q)));
        CHECK(Near(vs::VsFacing(q), q * Vec3{0, 0, -1}));   // CharacterController3D / ThirdPersonController facing
        CHECK(Near(vs::VsFacing(VirtualCamera::AimRotation(Vec3{1, 0, 0})), {1, 0, 0}));
        CHECK(Near(vs::VsFacing((const Transform*)nullptr), {0, 0, -1}));
    }

    const std::string prefab = BulletPrefab();
    std::vector<Row> rows;
    rows.push_back({"Look At turns -Z to the target (the convention)", AfterLookAt("node 3 SetVar done\ndefault 3 0 1\n"),
                    [](World& w, std::string& why) { const Vec3 f = w.me->transform->Rotation() * Vec3{0, 0, -1}; why = V(f); return Near(f, {1, 0, 0}); }});
    rows.push_back({"Forward Vector after Look At points at the target", AfterLookAt("node 3 SetVar f\nnode 4 ForwardVector\ndata 3 0 4 0\n"),
                    [](World& w, std::string& why) { return FacesAndReads(w, why, {1, 0, 0}); }});
    rows.push_back({"Shoot after Look At hits the target", AfterLookAt("node 3 Shoot\ndefault 3 0 20\ndefault 3 1 25\nnode 4 SetVar hit\nexec 3 0 4\ndata 4 0 3 0\n"),
                    [](World& w, std::string& why) {
                        const float hp = w.target->GetComponent<HealthStat>()->health;
                        why = "hit " + std::string(w.vc->GetVariable("hit").AsBool() ? "true" : "false") + ", health " + std::to_string(hp);
                        return w.vc->GetVariable("hit").AsBool() && hp < 100.0f;
                    }});
    rows.push_back({"Spawn Projectile after Look At flies toward the target", AfterLookAt("node 3 SpawnProjectile " + prefab + "\ndefault 3 0 8\n"),
                    [](World& w, std::string& why) {
                        for (const auto& up : w.s.Objects()) {
                            if (!up || up->name != "Bullet") continue;
                            const Vec3 p = up->transform->Position();
                            const auto* rb = up->GetComponent<Rigidbody3D>();
                            why = "bullet at " + V(p) + (rb ? ", velocity " + V(rb->velocity) : ", no body");
                            return p.x > 0.5f && rb && rb->velocity.x > 7.0f && std::fabs(rb->velocity.z) < 0.01f;
                        }
                        why = "no bullet";
                        return false;
                    }});
    rows.push_back({"Raycast 3D (default direction) after Look At hits the target",
                    AfterLookAt("node 3 SetVar hit\nnode 4 Raycast3D\ndata 3 0 4 0\nnode 5 SetVar what\ndata 5 0 4 4\nexec 3 0 5\n"),
                    [](World& w, std::string& why) {
                        const auto obj = w.vc->GetVariable("what");
                        why = std::string("hit ") + (w.vc->GetVariable("hit").AsBool() ? "true" : "false");
                        return w.vc->GetVariable("hit").AsBool() && obj.IsObject() && obj.AsObjectId() == w.target->InstanceId();
                    }});
    rows.push_back({"In Cone after Look At sees the target", AfterLookAt("node 3 SetVar in\nnode 4 InCone\ndata 4 0 90 0\ndefault 4 1 30\ndata 3 0 4 0\n"),
                    [](World& w, std::string& why) { why = w.vc->GetVariable("in").AsBool() ? "true" : "false"; return w.vc->GetVariable("in").AsBool(); }});
    rows.push_back({"Can See after Look At sees the target", AfterLookAt("node 3 SetVar see\nnode 4 CanSee\ndata 4 0 90 0\ndefault 4 1 20\ndefault 4 2 30\ndata 3 0 4 0\n"),
                    [](World& w, std::string& why) { why = w.vc->GetVariable("see").AsBool() ? "true" : "false"; return w.vc->GetVariable("see").AsBool(); }});
    rows.push_back({"Is Facing Object after Look At is true", AfterLookAt("node 3 SetVar face\nnode 4 GlIsFacingObject\ndata 4 0 90 0\ndefault 4 1 20\ndata 3 0 4 0\n"),
                    [](World& w, std::string& why) { why = w.vc->GetVariable("face").AsBool() ? "true" : "false"; return w.vc->GetVariable("face").AsBool(); }});
    rows.push_back({"Is Object Behind after Look At is false", AfterLookAt("node 3 SetVar behind\nnode 4 GlIsObjectBehind\ndata 4 0 90 0\ndata 3 0 4 0\n"),
                    [](World& w, std::string& why) { why = w.vc->GetVariable("behind").AsBool() ? "true" : "false"; return !w.vc->GetVariable("behind").AsBool(); }});
    rows.push_back({"Move Forward (Gl) after Look At moves toward the target",
                    "node 1 OnStart\nentry OnStart 1\nnode 2 LookAt Target\nexec 1 0 2\n"
                    "node 5 OnUpdate\nentry OnUpdate 5\nnode 3 GlMoveForward\ndefault 3 0 2\nexec 5 0 3\n",
                    [](World& w, std::string& why) { const Vec3 p = w.me->transform->Position(); why = V(p); return p.x > 0.3f && std::fabs(p.z) < 0.01f; }, 5});
    rows.push_back({"Drop after Look At puts the item on the target's side",
                    AfterLookAt("node 3 Drop\nnode 91 FindObject Item\ndata 3 0 91 0\n"),
                    [](World& w, std::string& why) { const Vec3 p = w.s.Find("Item")->transform->Position(); why = V(p); return p.x > 1.0f && std::fabs(p.z) < 0.01f; }});
    rows.push_back({"Face Object (Gl) then Forward Vector points at the target",
                    "node 1 OnStart\nentry OnStart 1\nnode 90 FindObject Target\nnode 2 GlFaceObject\ndata 2 0 90 0\nexec 1 0 2\n"
                    "node 3 SetVar f\nnode 4 ForwardVector\ndata 3 0 4 0\nexec 2 0 3\n",
                    [](World& w, std::string& why) { return FacesAndReads(w, why, {1, 0, 0}); }});
    rows.push_back({"Look at Position (Gl) then Forward Vector points at it",
                    "node 1 OnStart\nentry OnStart 1\nnode 2 GlLookAtPosition\ndefault 2 0 5\ndefault 2 1 0\ndefault 2 2 0\nexec 1 0 2\n"
                    "node 3 SetVar f\nnode 4 ForwardVector\ndata 3 0 4 0\nexec 2 0 3\n",
                    [](World& w, std::string& why) { return FacesAndReads(w, why, {1, 0, 0}); }});
    rows.push_back({"Face Direction (Gl) Right 1 then Forward Vector is +X",
                    "node 1 OnStart\nentry OnStart 1\nnode 2 GlFaceDirection\ndefault 2 0 1\ndefault 2 1 0\nexec 1 0 2\n"
                    "node 3 SetVar f\nnode 4 ForwardVector\ndata 3 0 4 0\nexec 2 0 3\n",
                    [](World& w, std::string& why) { return FacesAndReads(w, why, {1, 0, 0}); }});
    rows.push_back({"Face Away From (Gl) then Forward Vector points away",
                    "node 1 OnStart\nentry OnStart 1\nnode 90 FindObject Target\nnode 2 GlFaceAwayFrom\ndata 2 0 90 0\nexec 1 0 2\n"
                    "node 3 SetVar f\nnode 4 ForwardVector\ndata 3 0 4 0\nexec 2 0 3\n",
                    [](World& w, std::string& why) { return FacesAndReads(w, why, {-1, 0, 0}); }});
    rows.push_back({"Look at Object With Camera (Gl) aims a camera's view (-Z) at the target",
                    "node 1 OnStart\nentry OnStart 1\nnode 90 FindObject Target\nnode 2 GlCameraLookAtObject\ndata 2 0 90 0\nexec 1 0 2\n",
                    [](World& w, std::string& why) { const Vec3 f = VirtualCamera::ViewForward(w.me->transform->Rotation()); why = V(f); return Near(f, {1, 0, 0}); }});
    rows.push_back({"Turn Toward Object (Gl) ends up facing the target",
                    "node 5 OnUpdate\nentry OnUpdate 5\nnode 90 FindObject Target\nnode 2 GlTurnTowardObject\ndata 2 0 90 0\ndefault 2 1 360\nexec 5 0 2\n",
                    [](World& w, std::string& why) { const Vec3 f = vs::VsFacing(w.me->transform); why = V(f); return Near(f, {1, 0, 0}, 1e-2f); }, 20});
    rows.push_back({"Rotate Towards ends up facing the target with Forward Vector",
                    "node 5 OnUpdate\nentry OnUpdate 5\nnode 90 FindObject Target\nnode 2 RotateTowards\ndefault 2 0 3600\ndata 2 1 90 0\nexec 5 0 2\n"
                    "node 3 SetVar f\nnode 4 ForwardVector\ndata 3 0 4 0\nexec 2 0 3\n",
                    [](World& w, std::string& why) { const Vec3 f = w.vc->GetVariable("f").AsVec3(); why = V(f); return Near(f, {1, 0, 0}, 1e-2f); }, 10});
    rows.push_back({"Knockback with no From pushes backward (away from where it faces)",
                    AfterLookAt("node 3 Knockback\ndefault 3 1 10\n"),
                    [](World& w, std::string& why) { const Vec3 p = w.me->transform->Position(); why = V(p); return p.x < -0.5f; }});

    int failed = 0;
    for (Row& r : rows) {
        g_errs.clear();
        World w;
        Build(w, r.graph);
        if (std::string(r.name).rfind("Drop", 0) == 0) {   // an item held by Me
            GameObject* item = w.s.CreateGameObject("Item");
            item->transform->SetParent(w.me->transform, false);
        }
        w.s.Start();
        for (int f = 0; f < r.frames; ++f) { Time::Step(r.dt); w.s.Update(r.dt); }
        std::string why;
        const bool ok = r.check(w, why);
        std::printf("  %s %s: %s\n", ok ? "ok  " : "FAIL", r.name, why.c_str());
        for (const auto& e : g_errs) std::printf("    error: %s\n", e.c_str());
        if (!ok) ++failed;
        CHECK(ok);
    }
    std::printf("  rows: %d, failed %d\n", (int)rows.size(), failed);
    TEST_MAIN_RESULT();
}
