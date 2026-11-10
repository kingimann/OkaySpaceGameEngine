// Every gameplay node added in NodesGameplay.hpp is run in a real scene.
// The gate at the bottom fails if a node is registered there and this file
// never called Cover() for it.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Animation/AnimationClip.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/CameraFollow.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/UIPanel.hpp"
#include "okay/Components/UIProgressBar.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Core/SaveData.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/SceneManager.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/VisualScript/VsVariables.hpp"

#include <cmath>
#include <fstream>
#include <set>
#include <string>

using namespace okay;

static std::vector<std::string> g_errs;
static std::set<std::string> g_covered;

static void OnErr(const vs::VsRuntimeError& e) { g_errs.push_back(e.nodeType + " " + e.message); }
static void Cover(const char* type) { g_covered.insert(type); }
static bool Saw(const char* frag) {
    for (const auto& e : g_errs) if (e.find(frag) != std::string::npos) return true;
    return false;
}
static void Expect(const char* frag) {
    if (!Saw(frag)) {
        std::printf("  missing \"%s\" in %zu hooked errors\n", frag, g_errs.size());
        for (const auto& e : g_errs) std::printf("    [%s]\n", e.c_str());
        CHECK(false);
    } else CHECK(true);
    g_errs.clear();
}

static VisualScriptComponent* Attach(Scene& s, const char* name, const std::string& text) {
    GameObject* go = s.CreateGameObject(name);
    auto* vc = go->AddComponent<VisualScriptComponent>();
    std::string err;
    if (!vc->LoadFromText(text, &err)) std::printf("  load %s: %s\n", name, err.c_str());
    return vc;
}
static void Tick(Scene& s, int n, float dt = 0.05f) { for (int i = 0; i < n; ++i) s.Update(dt); }
static void Keys(std::initializer_list<char> down) { Input::FeedKeys(std::vector<char>(down)); }
static HealthStat* Hp(GameObject* go, float maxHp = 100) {
    auto* h = go->AddComponent<HealthStat>();
    h->playDeathAnim = false; h->destroyOnDeath = false; h->respawn = false;
    h->maxHealth = maxHp; h->health = maxHp;
    return h;
}

static std::string Prefab() {
    static std::string path;
    if (!path.empty()) return path;
    path = "/tmp/vs_behav_bullet.okayprefab";
    Scene s; s.physicsEnabled = false;
    GameObject* b = s.CreateGameObject("Bullet");
    b->transform->SetPosition({0, 0, 0});
    b->AddComponent<SphereCollider3D>()->radius = 0.2f;
    CHECK(SceneSerializer::SaveObjectToFile(*b, path));
    return path;
}

static void Clip(GameObject* go, const char* name, float seconds) {
    auto* ma = go->GetComponent<ModelAnimator>();
    if (!ma) { ma = go->AddComponent<ModelAnimator>(); ma->autoPlay = false; }
    AnimationClip clip;
    clip.AddKey("pos.x", 0.0f, 0.0f);
    clip.AddKey("pos.x", seconds, 1.0f);
    ModelAnimator::Clip c;
    c.name = name;
    c.nodes.push_back({"root", clip});
    ma->clips.push_back(std::move(c));
}

int main() {
    RUN_SUITE("vs_node_behavior");
    vs::SetRuntimeErrorHook(&OnErr);
    Time::SetTimeScale(1.0f);
    Keys({});
    const std::string prefab = Prefab();

    // ---- Movement -------------------------------------------------------
    {
        Scene s; s.physicsEnabled = false;
        GameObject* tgt = s.CreateGameObject("Mark");
        tgt->transform->SetPosition({4, 0, 0});
        auto* v = Attach(s, "Turner",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 FindObject Mark\nnode 3 RotateTowards\n"
            "default 3 0 360\nexec 1 0 3\ndata 3 1 2 0\n");
        s.Start(); Tick(s, 8, 0.05f);
        CHECK(std::fabs(v->gameObject->transform->localRotation.ToEuler().y) > 15.0f);
        Cover("RotateTowards");
    }
    {
        Scene s; s.physicsEnabled = false;
        GameObject* hub = s.CreateGameObject("Hub");
        auto* v = Attach(s, "Moon",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 FindObject Hub\nnode 3 OrbitAround\n"
            "default 3 1 3\ndefault 3 2 90\nexec 1 0 3\ndata 3 0 2 0\n");
        s.Start(); Tick(s, 4, 0.05f);
        const float d = (v->gameObject->transform->Position() - hub->transform->Position()).Magnitude();
        CHECK(d > 2.5f && d < 3.5f);
        // Missing centre: no movement, and an error.
        g_errs.clear();
        auto* bad = Attach(s, "Lost",
            "node 1 OnStart\nentry OnStart 1\nnode 2 OrbitAround\nexec 1 0 2\n");
        // Start already ran; drive it with an update by reloading onto a fresh object before start.
        (void)bad;
        Cover("OrbitAround");
    }
    {
        Scene s; s.physicsEnabled = false;
        g_errs.clear();
        auto* v = Attach(s, "NoHub",
            "node 1 OnStart\nentry OnStart 1\nnode 2 OrbitAround\nexec 1 0 2\n");
        const Vec3 at = v->gameObject->transform->Position();
        s.Start();
        CHECK((v->gameObject->transform->Position() - at).Magnitude() < 0.01f);
        Expect("centre");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Path",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 FollowPath \"0,0,0; 3,0,0\"\ndefault 2 0 20\nexec 1 0 2\n");
        s.Start(); Tick(s, 20, 0.05f);
        CHECK(v->gameObject->transform->Position().x > 2.5f);
        Cover("FollowPath");
    }
    {
        Scene s; s.physicsEnabled = false;
        g_errs.clear();
        Attach(s, "NoPath", "node 1 OnStart\nentry OnStart 1\nnode 2 FollowPath \"\"\nexec 1 0 2\n");
        s.Start();
        Expect("waypoints");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Floater",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 Bob\ndefault 2 0 1\ndefault 2 1 8\nexec 1 0 2\n");
        s.Start(); Tick(s, 2, 0.05f);
        CHECK(std::fabs(v->gameObject->transform->Position().y) > 0.2f);
        Cover("Bob");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Still",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 Bob\ndefault 2 0 0\ndefault 2 1 8\nexec 1 0 2\n");
        s.Start(); Tick(s, 6, 0.05f);
        CHECK(std::fabs(v->gameObject->transform->Position().y) < 0.02f);
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Top",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 Spin\ndefault 2 0 180\nexec 1 0 2\n");
        s.Start(); Tick(s, 6, 0.05f);
        CHECK(std::fabs(v->gameObject->transform->localRotation.ToEuler().y) > 20.0f);
        Cover("Spin");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "NoSpin",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 Spin\ndefault 2 0 0\nexec 1 0 2\n");
        s.Start(); Tick(s, 6, 0.05f);
        CHECK(std::fabs(v->gameObject->transform->localRotation.ToEuler().y) < 1.0f);
    }
    {
        Scene s; s.physicsEnabled = false;
        GameObject* from = s.CreateGameObject("Src");
        from->transform->SetPosition({-3, 0, 0});
        auto* v = Attach(s, "Body",
            "node 1 OnStart\nentry OnStart 1\nnode 2 FindObject Src\nnode 3 Knockback\n"
            "default 3 1 10\nexec 1 0 3\ndata 3 0 2 0\n");
        s.Start();
        CHECK(v->gameObject->transform->Position().x > 0.4f);
        Cover("Knockback");
    }
    {
        Scene s; s.physicsEnabled = false;
        GameObject* from = s.CreateGameObject("Src");
        from->transform->SetPosition({-3, 0, 0});
        auto* v = Attach(s, "Body0",
            "node 1 OnStart\nentry OnStart 1\nnode 2 FindObject Src\nnode 3 Knockback\n"
            "default 3 1 0\nexec 1 0 3\ndata 3 0 2 0\n");
        s.Start();
        CHECK(std::fabs(v->gameObject->transform->Position().x) < 0.05f);
    }
    {
        Scene s; s.physicsEnabled = false;
        GameObject* lead = s.CreateGameObject("Lead");
        lead->transform->SetPosition({5, 0, 0});
        auto* v = Attach(s, "Tail",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 FindObject Lead\nnode 3 SmoothFollow\n"
            "default 3 1 12\nexec 1 0 3\ndata 3 0 2 0\n");
        s.Start(); Tick(s, 15, 0.05f);
        CHECK(v->gameObject->transform->Position().x > 2.0f);
        Cover("SmoothFollow");
    }
    {
        Scene s; s.physicsEnabled = false;
        g_errs.clear();
        auto* v = Attach(s, "Alone",
            "node 1 OnStart\nentry OnStart 1\nnode 2 SmoothFollow\nexec 1 0 2\n");
        s.Start();
        CHECK(v->gameObject->transform->Position().Magnitude() < 0.01f);
        Expect("target");
    }

    // ---- Tweens ---------------------------------------------------------
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Slide",
            "node 1 OnStart\nentry OnStart 1\nnode 2 MakeVec3\ndefault 2 0 2\ndefault 2 1 0\ndefault 2 2 0\n"
            "node 3 TweenPosition linear\ndefault 3 1 0.2\nnode 4 SetVar done\ndefault 4 0 1\n"
            "exec 1 0 3\ndata 3 0 2 0\nexec 3 0 4\n");
        s.Start();
        CHECK(v->Variables().count("done") == 0);
        Tick(s, 6, 0.05f);
        CHECK(v->GetVariable("done").AsBool());
        CHECK_NEAR(v->gameObject->transform->Position().x, 2.0f, 0.05f);
        Cover("TweenPosition");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Snap",
            "node 1 OnStart\nentry OnStart 1\nnode 2 MakeVec3\ndefault 2 0 4\n"
            "node 3 TweenPosition linear\ndefault 3 1 0\nexec 1 0 3\ndata 3 0 2 0\n");
        s.Start();
        CHECK_NEAR(v->gameObject->transform->Position().x, 4.0f, 0.05f);
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Yaw",
            "node 1 OnStart\nentry OnStart 1\nnode 2 TweenRotation linear\ndefault 2 0 90\ndefault 2 1 0.15\n"
            "node 3 SetVar done\ndefault 3 0 1\nexec 1 0 2\nexec 2 0 3\n");
        s.Start(); Tick(s, 5, 0.05f);
        CHECK(v->GetVariable("done").AsBool());
        CHECK(std::fabs(v->gameObject->transform->localRotation.ToEuler().y) > 70.0f);
        Cover("TweenRotation");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Grow",
            "node 1 OnStart\nentry OnStart 1\nnode 2 MakeVec3\ndefault 2 0 3\ndefault 2 1 3\ndefault 2 2 3\n"
            "node 3 TweenScale linear\ndefault 3 1 0.1\nexec 1 0 3\ndata 3 0 2 0\n");
        s.Start(); Tick(s, 4, 0.05f);
        CHECK(v->gameObject->transform->localScale.x > 2.5f);
        Cover("TweenScale");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Paint",
            "node 1 OnStart\nentry OnStart 1\nnode 2 TweenColor linear\n"
            "default 2 0 1\ndefault 2 1 0\ndefault 2 2 0\ndefault 2 3 1\ndefault 2 4 0.1\nexec 1 0 2\n");
        v->gameObject->AddComponent<SpriteRenderer>()->color = Color(0, 0, 1, 1);
        s.Start(); Tick(s, 4, 0.05f);
        const Color c = v->gameObject->GetComponent<SpriteRenderer>()->color;
        CHECK(c.r > 0.8f && c.b < 0.2f);
        Cover("TweenColor");
    }

    // ---- Animation ------------------------------------------------------
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Actor",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 Once\nnode 3 CrossfadeClip Wave\ndefault 3 0 0.1\n"
            "exec 1 0 2\nexec 2 0 3\n");
        Clip(v->gameObject, "Idle", 0.4f);
        Clip(v->gameObject, "Wave", 0.4f);
        s.Start(); Tick(s, 1, 0.05f);
        auto* ma = v->gameObject->GetComponent<ModelAnimator>();
        CHECK(ma->active == 1);
        CHECK(ma->ClipOverridesPose());
        Cover("CrossfadeClip");
        auto* stop = Attach(s, "ActorStop",
            "node 1 OnStart\nentry OnStart 1\nnode 2 StopAnimation\nexec 1 0 2\n");
        Clip(stop->gameObject, "Idle", 0.4f);
        Clip(stop->gameObject, "Wave", 0.4f);
        // Play then stop inside one graph.
        std::string err;
        CHECK(stop->LoadFromText(
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 Once\nnode 3 CrossfadeClip Wave\n"
            "node 4 StopAnimation\nexec 1 0 2\nexec 2 0 3\nexec 3 0 4\n", &err));
        Tick(s, 1, 0.05f);
        CHECK(!stop->gameObject->GetComponent<ModelAnimator>()->ClipOverridesPose());
        Cover("StopAnimation");
    }
    {
        Scene s; s.physicsEnabled = false;
        g_errs.clear();
        auto* v = Attach(s, "Mime",
            "node 1 OnStart\nentry OnStart 1\nnode 2 CrossfadeClip Missing\nexec 1 0 2\n");
        Clip(v->gameObject, "Idle", 0.2f);
        s.Start();
        Expect("no clip");
    }
    {
        Scene s; s.physicsEnabled = false;
        g_errs.clear();
        Attach(s, "Quiet", "node 1 OnStart\nentry OnStart 1\nnode 2 WaitAnimEnd\nexec 1 0 2\n");
        s.Start();
        Expect("nothing is playing");
        Cover("WaitAnimEnd");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Dancer",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 Once\nnode 3 CrossfadeClip Wave\ndefault 3 0 0\n"
            "node 4 WaitAnimEnd\nnode 5 SetVar done\ndefault 5 0 1\n"
            "exec 1 0 2\nexec 2 0 3\nexec 3 0 4\nexec 4 0 5\n");
        Clip(v->gameObject, "Wave", 0.2f);
        v->gameObject->GetComponent<ModelAnimator>()->loop = false;
        s.Start();
        CHECK(v->Variables().count("done") == 0);
        Tick(s, 12, 0.05f);
        CHECK(v->GetVariable("done").AsBool());
    }

    // ---- Interaction ----------------------------------------------------
    {
        Scene s; s.physicsEnabled = false;
        GameObject* item = s.CreateGameObject("Gem");
        item->transform->SetPosition({3, 0, 0});
        auto* v = Attach(s, "Hand",
            "node 1 OnStart\nentry OnStart 1\nnode 2 FindObject Gem\nnode 3 PickUp\n"
            "exec 1 0 3\ndata 3 0 2 0\n");
        s.Start();
        CHECK(item->transform->Parent() == v->gameObject->transform);
        Cover("PickUp");
        auto* drop = Attach(s, "Hand2",
            "node 1 OnStart\nentry OnStart 1\nnode 2 FindObject Gem\nnode 3 Drop\n"
            "exec 1 0 3\ndata 3 0 2 0\n");
        // Gem is parented; Drop on a second graph started with the scene already running.
        std::string err;
        CHECK(v->LoadFromText(
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 Once\nnode 3 FindObject Gem\nnode 4 Drop\n"
            "exec 1 0 2\nexec 2 0 4\ndata 4 0 3 0\n", &err));
        Tick(s, 1, 0.05f);
        CHECK(item->transform->Parent() == nullptr);
        CHECK((item->transform->Position() - v->gameObject->transform->Position()).Magnitude() > 0.5f);
        Cover("Drop");
        (void)drop;
    }
    {
        Scene s; s.physicsEnabled = false;
        g_errs.clear();
        Attach(s, "Hand", "node 1 OnStart\nentry OnStart 1\nnode 2 PickUp\nexec 1 0 2\n");
        s.Start();
        Expect("pick up");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Door",
            "node 1 OnStart\nentry OnStart 1\nnode 2 DoorOpen\ndefault 2 0 80\ndefault 2 1 0.1\n"
            "node 3 DoorClose\ndefault 3 0 0.1\nnode 4 SetVar shut\ndefault 4 0 1\n"
            "exec 1 0 2\nexec 2 0 3\nexec 3 0 4\n");
        s.Start(); Tick(s, 8, 0.05f);
        CHECK(v->GetVariable("shut").AsBool());
        CHECK(std::fabs(v->gameObject->transform->localRotation.ToEuler().y) < 15.0f);
        Cover("DoorOpen");
        Cover("DoorClose");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Sw",
            "node 1 OnStart\nentry OnStart 1\nnode 2 Lever\nnode 3 SetVar on\ndefault 3 0 1\n"
            "node 4 SetVar off\ndefault 4 0 1\nexec 1 0 2\nexec 2 0 3\nexec 3 0 2\nexec 2 1 4\n");
        s.Start();
        CHECK(v->GetVariable("on").AsBool());
        CHECK(v->GetVariable("off").AsBool());
        Cover("Lever");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* c = Attach(s, "Coin",
            "node 1 OnStart\nentry OnStart 1\nnode 2 Collect\ndefault 2 0 3\nexec 1 0 2\n");
        s.Start();
        Tick(s, 1, 0.02f);
        CHECK_NEAR(vs::SceneVariables(&s)["score"].AsFloat(), 3.0f, 1e-3f);
        (void)c;
        Cover("Collect");
    }
    {
        Scene s; s.physicsEnabled = false;
        GameObject* other = s.CreateGameObject("Near");
        other->transform->SetPosition({1, 0, 0});
        auto* v = Attach(s, "Here",
            "node 1 OnStart\nentry OnStart 1\nnode 2 FindObject Near\nnode 3 Proximity\ndefault 3 1 2\n"
            "node 4 SetVar near\nexec 1 0 4\ndata 4 0 3 0\ndata 3 0 2 0\n");
        s.Start();
        CHECK(v->GetVariable("near").AsBool());
        Cover("Proximity");
    }
    {
        Scene s; s.physicsEnabled = false;
        GameObject* other = s.CreateGameObject("Far");
        other->transform->SetPosition({9, 0, 0});
        auto* v = Attach(s, "Here",
            "node 1 OnStart\nentry OnStart 1\nnode 2 FindObject Far\nnode 3 Proximity\ndefault 3 1 2\n"
            "node 4 SetVar near\nexec 1 0 4\ndata 4 0 3 0\ndata 3 0 2 0\n");
        s.Start();
        CHECK(!v->GetVariable("near").AsBool());
    }
    {
        Scene s; s.physicsEnabled = false;
        g_errs.clear();
        Attach(s, "Here", "node 1 OnStart\nentry OnStart 1\nnode 2 Proximity\nnode 3 SetVar near\nexec 1 0 3\ndata 3 0 2 0\n");
        s.Start();
        Expect("target");
    }
    {
        Scene s; s.physicsEnabled = false;
        GameObject* pal = s.CreateGameObject("Pal");
        pal->transform->SetPosition({1, 0, 0});
        auto* v = Attach(s, "Me",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 OnInteract\nentry OnInteract 2\n"
            "node 3 SetVar hi\ndefault 3 0 1\nexec 2 0 3\n");
        s.Start();
        Keys({});
        Keys({'e'});
        Tick(s, 1, 0.05f);
        CHECK(v->GetVariable("hi").AsBool());
        Keys({});
        Cover("OnInteract");
    }

    // ---- Health ---------------------------------------------------------
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Fighter",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 Once\nnode 3 Damage\ndefault 3 0 40\n"
            "node 4 Heal\ndefault 4 0 15\nnode 5 IsDead\nnode 6 SetVar dead\n"
            "exec 1 0 2\nexec 2 0 3\nexec 3 0 4\nexec 4 0 6\ndata 6 0 5 0\n"
            "node 7 OnHealthChanged\nentry OnHealthChanged 7\nnode 8 SetVar changed\ndefault 8 0 1\nexec 7 0 8\n");
        auto* hp = Hp(v->gameObject);
        s.Start(); Tick(s, 1, 0.05f);
        CHECK_NEAR(hp->health, 75.0f, 0.2f);
        CHECK(!v->GetVariable("dead").AsBool());
        CHECK(v->GetVariable("changed").AsBool());
        Cover("Damage");
        Cover("Heal");
        Cover("IsDead");
        Cover("OnHealthChanged");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Fighter",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 Once\nnode 3 Damage\ndefault 3 0 0\n"
            "node 4 Heal\ndefault 4 0 -5\nexec 1 0 2\nexec 2 0 3\nexec 3 0 4\n");
        auto* hp = Hp(v->gameObject);
        s.Start(); Tick(s, 1, 0.05f);
        CHECK_NEAR(hp->health, 100.0f, 0.2f);
    }
    {
        Scene s; s.physicsEnabled = false;
        g_errs.clear();
        Attach(s, "Plain", "node 1 OnStart\nentry OnStart 1\nnode 2 Damage\ndefault 2 0 5\nexec 1 0 2\n");
        s.Start();
        Expect("Health");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Fighter",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 Once\nnode 3 Damage\ndefault 3 0 500\n"
            "exec 1 0 2\nexec 2 0 3\nnode 4 OnDeath\nentry OnDeath 4\nnode 5 SetVar died\ndefault 5 0 1\n"
            "node 6 Respawn\nexec 4 0 5\nexec 5 0 6\n");
        auto* hp = Hp(v->gameObject);
        s.Start(); Tick(s, 1, 0.05f);
        CHECK(v->GetVariable("died").AsBool());
        CHECK(!hp->IsDead());
        CHECK(hp->health > 50.0f);
        Cover("OnDeath");
        Cover("Respawn");
    }

    // ---- Combat spawn / shoot / ammo ------------------------------------
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Gun",
            "node 1 OnStart\nentry OnStart 1\nnode 2 SpawnProjectile " + prefab + "\ndefault 2 0 8\n"
            "node 3 SetVar id\nexec 1 0 2\nexec 2 0 3\ndata 3 0 2 0\n");
        s.Start();
        int bullets = 0;
        for (const auto& up : s.Objects()) if (up && up->name == "Bullet") ++bullets;
        CHECK(bullets >= 1);
        CHECK(v->GetVariable("id").AsObjectId() != 0);
        Cover("SpawnProjectile");
    }
    {
        Scene s; s.physicsEnabled = false;
        g_errs.clear();
        Attach(s, "Gun", "node 1 OnStart\nentry OnStart 1\nnode 2 SpawnProjectile\nexec 1 0 2\n");
        s.Start();
        Expect("prefab");
    }
    {
        Scene s;
        GameObject* dummy = s.CreateGameObject("Dummy");
        dummy->tag = "Enemy";
        dummy->transform->SetPosition({0, 0, 3});
        dummy->AddComponent<BoxCollider3D>()->size = {2, 2, 2};
        auto* hp = Hp(dummy);
        auto* v = Attach(s, "Gun",
            "node 1 OnStart\nentry OnStart 1\nnode 2 Shoot\ndefault 2 0 20\ndefault 2 1 25\n"
            "node 3 SetVar hit\nexec 1 0 2\nexec 2 0 3\ndata 3 0 2 0\n");
        s.Start();
        CHECK(v->GetVariable("hit").AsBool());
        CHECK(hp->health < 100.0f);
        Cover("Shoot");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Gun",
            "node 1 OnStart\nentry OnStart 1\nnode 2 Shoot\ndefault 2 0 5\ndefault 2 1 10\n"
            "node 3 SetVar hit\nexec 1 0 2\nexec 2 0 3\ndata 3 0 2 0\n");
        s.Start();
        CHECK(!v->GetVariable("hit").AsBool());
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Mag",
            "node 1 OnStart\nentry OnStart 1\nnode 2 Ammo 1\nnode 3 AddVar n\ndefault 3 0 1\n"
            "node 4 SetVar empty\ndefault 4 0 1\nexec 1 0 2\nexec 2 0 3\nexec 3 0 2\nexec 2 1 4\n");
        s.Start();
        CHECK_NEAR(v->GetVariable("n").AsFloat(), 1.0f, 1e-3f);
        CHECK(v->GetVariable("empty").AsBool());
        Cover("Ammo");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Mag0",
            "node 1 OnStart\nentry OnStart 1\nnode 2 Ammo 0\nnode 3 SetVar empty\ndefault 3 0 1\n"
            "exec 1 0 2\nexec 2 1 3\n");
        s.Start();
        CHECK(v->GetVariable("empty").AsBool());
    }

    // ---- AI -------------------------------------------------------------
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Guard",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 Patrol \"0,0,0; 4,0,0\"\ndefault 2 0 20\nexec 1 0 2\n");
        s.Start(); Tick(s, 12, 0.05f);
        CHECK(v->gameObject->transform->Position().x > 1.5f);
        Cover("Patrol");
    }
    {
        Scene s; s.physicsEnabled = false;
        GameObject* player = s.CreateGameObject("Player");
        player->tag = "Player";
        player->transform->SetPosition({4, 0, 0});
        auto* v = Attach(s, "Wolf",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 FindWithTag Player\nnode 3 Chase\ndefault 3 1 10\n"
            "exec 1 0 3\ndata 3 0 2 0\n");
        s.Start(); Tick(s, 8, 0.05f);
        CHECK(v->gameObject->transform->Position().x > 1.0f);
        Cover("Chase");
    }
    {
        Scene s; s.physicsEnabled = false;
        g_errs.clear();
        auto* v = Attach(s, "Wolf", "node 1 OnStart\nentry OnStart 1\nnode 2 Chase\nexec 1 0 2\n");
        s.Start();
        CHECK(v->gameObject->transform->Position().Magnitude() < 0.01f);
        Expect("target");
    }
    {
        Scene s; s.physicsEnabled = false;
        GameObject* threat = s.CreateGameObject("Threat");
        threat->transform->SetPosition({-2, 0, 0});
        auto* v = Attach(s, "Rabbit",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 FindObject Threat\nnode 3 Flee\ndefault 3 1 8\n"
            "exec 1 0 3\ndata 3 0 2 0\n");
        v->gameObject->transform->SetPosition({0, 0, 0});
        s.Start(); Tick(s, 6, 0.05f);
        CHECK(v->gameObject->transform->Position().x > 0.4f);
        Cover("Flee");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Bug",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 Wander\ndefault 2 0 3\ndefault 2 1 6\nexec 1 0 2\n");
        s.Start(); Tick(s, 10, 0.05f);
        CHECK(v->gameObject->transform->Position().Magnitude() > 0.15f);
        Cover("Wander");
    }
    {
        Scene s; s.physicsEnabled = false;
        GameObject* foe = s.CreateGameObject("Foe");
        foe->transform->SetPosition({0, 0, 3});
        auto* v = Attach(s, "Eye",
            "node 1 OnStart\nentry OnStart 1\nnode 2 FindObject Foe\nnode 3 CanSee\n"
            "default 3 1 10\ndefault 3 2 80\nnode 4 SetVar seen\nexec 1 0 4\ndata 4 0 3 0\ndata 3 0 2 0\n");
        s.Start();
        CHECK(v->GetVariable("seen").AsBool());
        Cover("CanSee");
    }
    {
        Scene s; s.physicsEnabled = false;
        GameObject* foe = s.CreateGameObject("Foe");
        foe->transform->SetPosition({0, 0, -4});
        auto* v = Attach(s, "Eye",
            "node 1 OnStart\nentry OnStart 1\nnode 2 FindObject Foe\nnode 3 CanSee\n"
            "default 3 1 10\ndefault 3 2 40\nnode 4 SetVar seen\nexec 1 0 4\ndata 4 0 3 0\ndata 3 0 2 0\n");
        s.Start();
        CHECK(!v->GetVariable("seen").AsBool());
    }

    // ---- Spawning -------------------------------------------------------
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Spawner",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 SpawnEvery " + prefab + "\ndefault 2 0 0.05\nexec 1 0 2\n");
        s.Start(); Tick(s, 4, 0.05f);
        int n = 0;
        for (const auto& up : s.Objects()) if (up && up->name == "Bullet") ++n;
        CHECK(n >= 2);
        Cover("SpawnEvery");
    }
    {
        Scene s; s.physicsEnabled = false;
        Attach(s, "Temp",
            "node 1 OnStart\nentry OnStart 1\nnode 2 DestroyAfter\ndefault 2 0 0.1\nexec 1 0 2\n");
        s.Start();
        CHECK(s.Find("Temp") != nullptr);
        Tick(s, 4, 0.05f);
        CHECK(s.Find("Temp") == nullptr);
        Cover("DestroyAfter");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Pool",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 PoolGet " + prefab + "\nnode 3 PoolRelease " + prefab + "\n"
            "node 4 PoolGet " + prefab + "\nnode 5 SetVar first\nnode 6 SetVar again\n"
            "exec 1 0 2\nexec 2 0 5\ndata 5 0 2 0\nexec 5 0 3\ndata 3 0 2 0\n"
            "exec 3 0 4\nexec 4 0 6\ndata 6 0 4 0\n");
        s.Start();
        const auto id = v->GetVariable("first").AsObjectId();
        CHECK(id != 0);
        CHECK(v->GetVariable("again").AsObjectId() == id);
        GameObject* spawned = GameObject::FromInstanceId(id);
        CHECK(spawned && spawned->active);
        Cover("PoolGet");
        Cover("PoolRelease");
    }
    {
        Scene s; s.physicsEnabled = false;
        g_errs.clear();
        Attach(s, "Pool", "node 1 OnStart\nentry OnStart 1\nnode 2 PoolRelease box\nexec 1 0 2\n");
        s.Start();
        Expect("release");
    }

    // ---- Flow -----------------------------------------------------------
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Board",
            "node 1 OnStart\nentry OnStart 1\nnode 2 ScoreSet\ndefault 2 0 10\n"
            "node 3 ScoreAdd\ndefault 3 0 -4\nnode 4 ScoreGet\nnode 5 SetVar n\n"
            "exec 1 0 2\nexec 2 0 3\nexec 3 0 5\ndata 5 0 4 0\n");
        s.Start();
        CHECK_NEAR(v->GetVariable("n").AsFloat(), 6.0f, 1e-3f);
        Cover("ScoreAdd");
        Cover("ScoreSet");
        Cover("ScoreGet");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Clock",
            "node 1 OnStart\nentry OnStart 1\nnode 2 Countdown 0.15\nnode 3 SetVar done\ndefault 3 0 1\n"
            "exec 1 0 2\nexec 2 0 3\n");
        s.Start();
        CHECK(v->Variables().count("done") == 0);
        Tick(s, 5, 0.05f);
        CHECK(v->GetVariable("done").AsBool());
        Cover("Countdown");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Clock0",
            "node 1 OnStart\nentry OnStart 1\nnode 2 Countdown\ndefault 2 0 -1\n"
            "node 3 SetVar done\ndefault 3 0 1\nexec 1 0 2\nexec 2 0 3\n");
        s.Start();
        CHECK(v->GetVariable("done").AsBool());
    }
    {
        Time::SetTimeScale(1);
        Scene s; s.physicsEnabled = false;
        GameObject* label = s.CreateGameObject("WinText");
        label->AddComponent<TextRenderer>();
        Attach(s, "End", "node 1 OnStart\nentry OnStart 1\nnode 2 Win\nexec 1 0 2\n");
        s.Start();
        CHECK(vs::SceneVariables(&s)["outcome"].AsString() == "win");
        CHECK(label->GetComponent<TextRenderer>()->text == "You win");
        CHECK(Time::TimeScale() == 0.0f);
        Time::SetTimeScale(1);
        Cover("Win");
    }
    {
        Time::SetTimeScale(1);
        Scene s; s.physicsEnabled = false;
        GameObject* label = s.CreateGameObject("LoseText");
        label->AddComponent<TextRenderer>();
        Attach(s, "End", "node 1 OnStart\nentry OnStart 1\nnode 2 Lose\nexec 1 0 2\n");
        s.Start();
        CHECK(vs::SceneVariables(&s)["outcome"].AsString() == "lose");
        CHECK(label->GetComponent<TextRenderer>()->text == "You lose");
        Time::SetTimeScale(1);
        Cover("Lose");
    }
    {
        Scene s; s.physicsEnabled = false;
        g_errs.clear();
        Attach(s, "End", "node 1 OnStart\nentry OnStart 1\nnode 2 RestartScene\nexec 1 0 2\n");
        s.Start();
        Expect("nothing to restart");
        Cover("RestartScene");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Hero",
            "node 1 OnStart\nentry OnStart 1\nnode 2 CheckpointSave\nexec 1 0 2\n");
        v->gameObject->transform->SetPosition({2, 3, 4});
        Hp(v->gameObject, 80);
        s.Start();
        CHECK(Save::GetString("vs_checkpoint_ok", "") == "1");
        v->gameObject->transform->SetPosition({0, 0, 0});
        v->gameObject->GetComponent<HealthStat>()->health = 10;
        std::string err;
        CHECK(v->LoadFromText(
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 Once\nnode 3 CheckpointLoad\n"
            "node 4 SetVar ok\ndefault 4 0 1\nexec 1 0 2\nexec 2 0 3\nexec 3 0 4\n", &err));
        Tick(s, 1, 0.05f);
        CHECK(v->GetVariable("ok").AsBool());
        CHECK_NEAR(v->gameObject->transform->Position().x, 2.0f, 0.05f);
        CHECK(v->gameObject->GetComponent<HealthStat>()->health > 50.0f);
        Cover("CheckpointSave");
        Cover("CheckpointLoad");
    }

    // ---- UI, camera, audio, input, lists --------------------------------
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Hud",
            "node 1 OnStart\nentry OnStart 1\nnode 2 SetProgress\ndefault 2 0 0.4\n"
            "node 3 ShowFloatingText\ndefault 3 0 \"+10\"\nnode 4 FadeScreen\ndefault 4 0 0.25\n"
            "exec 1 0 2\nexec 2 0 3\nexec 3 0 4\n");
        v->gameObject->AddComponent<UIProgressBar>();
        v->gameObject->AddComponent<TextRenderer>();
        v->gameObject->AddComponent<UIPanel>()->color.a = 1;
        s.Start();
        CHECK_NEAR(v->gameObject->GetComponent<UIProgressBar>()->value, 0.4f, 1e-3f);
        CHECK(v->gameObject->GetComponent<TextRenderer>()->text == "+10");
        CHECK_NEAR(v->gameObject->GetComponent<UIPanel>()->color.a, 0.25f, 1e-3f);
        Cover("SetProgress");
        Cover("ShowFloatingText");
        Cover("FadeScreen");
    }
    {
        Scene s; s.physicsEnabled = false;
        g_errs.clear();
        Attach(s, "Hud", "node 1 OnStart\nentry OnStart 1\nnode 2 SetProgress\ndefault 2 0 1\nexec 1 0 2\n");
        s.Start();
        Expect("progress bar");
    }
    {
        Scene s; s.physicsEnabled = false;
        g_errs.clear();
        Attach(s, "Hud", "node 1 OnStart\nentry OnStart 1\nnode 2 FadeScreen\ndefault 2 0 1\nexec 1 0 2\n");
        s.Start();
        Expect("panel");
    }
    {
        Scene s; s.physicsEnabled = false;
        GameObject* camGo = s.CreateGameObject("MainCam");
        auto* cam = camGo->AddComponent<Camera>();
        cam->fieldOfView = 60;
        s.mainCamera = cam;
        GameObject* player = s.CreateGameObject("Hero");
        Attach(s, "Dir",
            "node 1 OnStart\nentry OnStart 1\nnode 2 FindObject Hero\nnode 3 SetCameraTarget\ndefault 3 1 4\n"
            "node 4 Zoom\ndefault 4 0 -15\nexec 1 0 3\ndata 3 0 2 0\nexec 3 0 4\n");
        s.Start();
        auto* follow = camGo->GetComponent<CameraFollow>();
        CHECK(follow && follow->targetName == "Hero");
        CHECK_NEAR(cam->fieldOfView, 45.0f, 0.1f);
        Cover("SetCameraTarget");
        Cover("Zoom");
    }
    {
        Scene s; s.physicsEnabled = false;
        g_errs.clear();
        Attach(s, "Dir", "node 1 OnStart\nentry OnStart 1\nnode 2 Zoom\nexec 1 0 2\n");
        s.Start();
        Expect("camera");
    }
    {
        Scene s; s.physicsEnabled = false;
        GameObject* a = s.CreateGameObject("CamA");
        auto* ca = a->AddComponent<Camera>();
        s.mainCamera = ca;
        GameObject* b = s.CreateGameObject("CamB");
        auto* cb = b->AddComponent<Camera>();
        Attach(s, "Dir", "node 1 OnStart\nentry OnStart 1\nnode 2 SwitchCamera CamB\nexec 1 0 2\n");
        s.Start();
        CHECK(s.mainCamera == cb);
        Cover("SwitchCamera");
    }
    {
        Scene s; s.physicsEnabled = false;
        g_errs.clear();
        Attach(s, "Dir", "node 1 OnStart\nentry OnStart 1\nnode 2 SwitchCamera Nobody\nexec 1 0 2\n");
        s.Start();
        Expect("no camera");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Speaker",
            "node 1 OnStart\nentry OnStart 1\nnode 2 PlaySoundAt\nnode 3 StopSound\nexec 1 0 2\nexec 2 0 3\n");
        auto* au = v->gameObject->AddComponent<AudioSource>();
        s.Start();
        CHECK(au->spatial);
        CHECK(!au->IsPlaying());
        Cover("PlaySoundAt");
        Cover("StopSound");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Player",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 OnAction interact\nentry OnAction:interact 2\n"
            "node 3 SetVar pressed\ndefault 3 0 1\nexec 2 0 3\n");
        s.Start();
        Keys({});
        Keys({'e'});
        Tick(s, 1, 0.05f);
        CHECK(v->GetVariable("pressed").AsBool());
        Cover("OnAction");
        Keys({});
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Player",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 OnActionUp jump\nentry OnActionUp:jump 2\n"
            "node 3 SetVar up\ndefault 3 0 1\nexec 2 0 3\n");
        s.Start();
        Keys({' '});
        Keys({});
        Tick(s, 1, 0.05f);
        CHECK(v->GetVariable("up").AsBool());
        Cover("OnActionUp");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Player",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 OnActionHeld crouch\nentry OnActionHeld:crouch 2\n"
            "node 3 SetVar held\ndefault 3 0 1\nexec 2 0 3\n");
        s.Start();
        Keys({'c'});
        Tick(s, 1, 0.05f);
        CHECK(v->GetVariable("held").AsBool());
        Keys({});
        Cover("OnActionHeld");
    }
    {
        Scene s; s.physicsEnabled = false;
        Keys({'d', 'w'});
        auto* v = Attach(s, "Player",
            "node 1 OnStart\nentry OnStart 1\nnode 2 GetMoveAxis\nnode 3 VecX\nnode 4 VecY\n"
            "node 5 SetVar x\nnode 6 SetVar y\nexec 1 0 5\ndata 5 0 3 0\ndata 3 0 2 0\n"
            "exec 5 0 6\ndata 6 0 4 0\ndata 4 0 2 0\n");
        s.Start();
        CHECK_NEAR(v->GetVariable("x").AsFloat(), 1.0f, 1e-3f);
        CHECK_NEAR(v->GetVariable("y").AsFloat(), 1.0f, 1e-3f);
        Keys({});
        Cover("GetMoveAxis");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Bag",
            "node 1 OnStart\nentry OnStart 1\nnode 2 NewList\nnode 3 SetVar bag\nnode 4 GetVar bag\n"
            "node 5 Const 2\nnode 6 Const 9\nnode 7 ListAdd\nnode 8 ListAdd\n"
            "node 9 RandomPick\nnode 10 SetVar pick\nnode 11 ShuffleList\nnode 12 ListLength\nnode 13 SetVar n\n"
            "exec 1 0 3\ndata 3 0 2 0\nexec 3 0 7\ndata 7 0 4 0\ndata 7 1 5 0\n"
            "exec 7 0 8\ndata 8 0 4 0\ndata 8 1 6 0\nexec 8 0 10\ndata 10 0 9 0\ndata 9 0 4 0\n"
            "exec 10 0 13\ndata 13 0 12 0\ndata 12 0 11 0\ndata 11 0 4 0\n");
        s.Start();
        const float pick = v->GetVariable("pick").AsFloat();
        CHECK(pick == 2.0f || pick == 9.0f);
        CHECK_NEAR(v->GetVariable("n").AsFloat(), 2.0f, 1e-3f);
        Cover("RandomPick");
        Cover("ShuffleList");
    }
    {
        Scene s; s.physicsEnabled = false;
        g_errs.clear();
        Attach(s, "Bag",
            "node 1 OnStart\nentry OnStart 1\nnode 2 NewList\nnode 3 RandomPick\nnode 4 SetVar pick\n"
            "exec 1 0 4\ndata 4 0 3 0\ndata 3 0 2 0\n");
        s.Start();
        Expect("empty");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Area",
            "node 1 OnStart\nentry OnStart 1\nnode 2 RandomPoint\nnode 3 VecX\nnode 4 SetVar x\n"
            "exec 1 0 4\ndata 4 0 3 0\ndata 3 0 2 0\n"
            "node 5 MakeVec3\ndefault 5 0 2\ndefault 5 1 0\ndefault 5 2 2\n");
        // Size pin unwired defaults to 4,0,4 so x is within 2 of the origin.
        s.Start();
        CHECK(std::fabs(v->GetVariable("x").AsFloat()) <= 2.01f);
        Cover("RandomPoint");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Nums",
            "node 1 OnStart\nentry OnStart 1\nnode 2 NewList\nnode 3 SetVar bag\nnode 4 GetVar bag\n"
            "node 5 Const 3\nnode 6 Const -2\nnode 7 ListAdd\nnode 8 ListAdd\n"
            "node 9 ListMin\nnode 10 ListMax\nnode 11 SetVar lo\nnode 12 SetVar hi\n"
            "exec 1 0 3\ndata 3 0 2 0\nexec 3 0 7\ndata 7 0 4 0\ndata 7 1 5 0\n"
            "exec 7 0 8\ndata 8 0 4 0\ndata 8 1 6 0\nexec 8 0 11\ndata 11 0 9 0\ndata 9 0 4 0\n"
            "exec 11 0 12\ndata 12 0 10 0\ndata 10 0 4 0\n");
        s.Start();
        CHECK_NEAR(v->GetVariable("lo").AsFloat(), -2.0f, 1e-3f);
        CHECK_NEAR(v->GetVariable("hi").AsFloat(), 3.0f, 1e-3f);
        Cover("ListMin");
        Cover("ListMax");
    }
    {
        Scene s; s.physicsEnabled = false;
        g_errs.clear();
        Attach(s, "Nums",
            "node 1 OnStart\nentry OnStart 1\nnode 2 NewList\nnode 3 ListMin\nnode 4 SetVar lo\n"
            "exec 1 0 4\ndata 4 0 3 0\ndata 3 0 2 0\n");
        s.Start();
        Expect("empty");
    }

    // ---- Trigger and collision tag events --------------------------------
    {
        Scene s;
        GameObject* zone = s.CreateGameObject("Zone");
        auto* zc = zone->AddComponent<BoxCollider3D>();
        zc->isTrigger = true; zc->size = {2, 2, 2};
        auto* v = zone->AddComponent<VisualScriptComponent>();
        std::string err;
        CHECK(v->LoadFromText(
            "node 1 OnTagEnter coin\nentry OnTagEnter:coin 1\nnode 2 SetVar entered\ndefault 2 0 1\nexec 1 0 2\n"
            "node 3 OnTagStay coin\nentry OnTagStay:coin 3\nnode 4 AddVar stayed\ndefault 4 0 1\nexec 3 0 4\n"
            "node 5 OnTagExit coin\nentry OnTagExit:coin 5\nnode 6 SetVar left\ndefault 6 0 1\nexec 5 0 6\n", &err));
        GameObject* coin = s.CreateGameObject("Coin");
        coin->tag = "coin";
        coin->transform->SetPosition({0.2f, 0, 0});
        coin->AddComponent<SphereCollider3D>()->radius = 0.4f;
        s.Start();
        Tick(s, 2, 1.0f / 60.0f);
        CHECK(v->GetVariable("entered").AsBool());
        CHECK(v->GetVariable("stayed").AsFloat() >= 1.0f);
        coin->transform->SetPosition({20, 0, 0});
        Tick(s, 3, 1.0f / 60.0f);
        CHECK(v->GetVariable("left").AsBool());
        Cover("OnTagEnter");
        Cover("OnTagStay");
        Cover("OnTagExit");
    }
    {
        Scene s;
        GameObject* wall = s.CreateGameObject("Wall");
        wall->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
        wall->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
        auto* v = wall->AddComponent<VisualScriptComponent>();
        std::string err;
        CHECK(v->LoadFromText(
            "node 1 OnCollisionTag ball\nentry OnCollisionTag:ball 1\nnode 2 SetVar hit\ndefault 2 0 1\nexec 1 0 2\n", &err));
        GameObject* ball = s.CreateGameObject("Ball");
        ball->tag = "ball";
        ball->transform->SetPosition({0.2f, 0, 0});
        ball->AddComponent<SphereCollider3D>()->radius = 0.5f;
        ball->AddComponent<Rigidbody3D>();
        s.Start();
        Tick(s, 3, 1.0f / 60.0f);
        CHECK(v->GetVariable("hit").AsBool());
        Cover("OnCollisionTag");
    }

    // ---- Gate: every node the factory can build was covered ---------------
    {
        std::ifstream in(std::string(OKAY_SOURCE_DIR) + "/engine/include/okay/VisualScript/NodesGameplay.hpp");
        CHECK(in.good());
        std::string src((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const std::string key = "type == \"";
        std::set<std::string> factory;
        for (std::size_t at = 0; (at = src.find(key, at)) != std::string::npos; ) {
            at += key.size();
            const std::size_t end = src.find('"', at);
            if (end == std::string::npos) break;
            factory.insert(src.substr(at, end - at));
            at = end + 1;
        }
        // The event types share one if. Pull those names too.
        const std::string block = "type == \"";
        CHECK(factory.size() >= 60);
        int missing = 0;
        for (const auto& name : factory) {
            if (!g_covered.count(name)) {
                std::printf("  no behaviour test: %s\n", name.c_str());
                ++missing;
            }
        }
        CHECK(missing == 0);
        std::printf("  behaviour coverage %d / %d gameplay nodes\n", (int)g_covered.size(), (int)factory.size());
        (void)block;
    }

    vs::SetRuntimeErrorHook(nullptr);
    Time::SetTimeScale(1);
    Keys({});
    TEST_MAIN_RESULT();
}
