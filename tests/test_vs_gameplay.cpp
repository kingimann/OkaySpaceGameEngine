// High-level gameplay nodes: one check per family, on a real graph.
#include "test_framework.hpp"
#include "test_sandbox.hpp"
#include <Okay.hpp>
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/UIProgressBar.hpp"
#include "okay/Components/UIPanel.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Core/SaveData.hpp"
#include "okay/Core/Time.hpp"
#include "okay/VisualScript/VsVariables.hpp"

#include <cmath>
#include <string>

using namespace okay;

static std::vector<std::string> g_errs;

static VisualScriptComponent* Attach(Scene& s, const char* name, const std::string& text) {
    GameObject* go = s.CreateGameObject(name);
    auto* vc = go->AddComponent<VisualScriptComponent>();
    std::string err;
    if (!vc->LoadFromText(text, &err)) std::printf("  load: %s\n", err.c_str());
    return vc;
}

static void Updates(Scene& s, int n, float dt) {
    for (int i = 0; i < n; ++i) s.Update(dt);
}

int main() {
    RUN_SUITE("vs_gameplay");
    okaytest::Sandbox sandbox("vs_gameplay", OKAY_SOURCE_DIR);   // files the nodes write land in a temp folder, not the checkout
    Time::SetTimeScale(1.0f);

    // Movement: spin, bob, follow path, smooth follow, knockback, orbit.
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Mover",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 Spin\ndefault 2 0 90\nexec 1 0 2\n"
            "node 3 Bob\ndefault 3 0 1\ndefault 3 1 1\nexec 2 0 3\n");
        s.Start();
        Updates(s, 10, 0.1f);
        const float yaw = v->gameObject->transform->localRotation.ToEuler().y;
        CHECK(std::fabs(yaw) > 20.0f);
        CHECK(std::fabs(v->gameObject->transform->Position().y) > 0.05f);
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Walker",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 FollowPath \"0,0,0; 2,0,0\"\ndefault 2 0 10\nexec 1 0 2\n");
        s.Start();
        Updates(s, 30, 0.05f);
        CHECK(v->gameObject->transform->Position().x > 1.5f);
    }
    {
        Scene s; s.physicsEnabled = false;
        GameObject* lead = s.CreateGameObject("Lead");
        lead->transform->SetPosition({4, 0, 0});
        auto* v = Attach(s, "Tail",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 FindObject Lead\nnode 3 SmoothFollow\n"
            "exec 1 0 3\ndata 3 0 2 0\ndefault 3 1 8\n");
        s.Start();
        Updates(s, 20, 0.05f);
        CHECK(v->gameObject->transform->Position().x > 1.0f);
    }
    {
        Scene s; s.physicsEnabled = false;
        GameObject* from = s.CreateGameObject("From");
        from->transform->SetPosition({-2, 0, 0});
        auto* v = Attach(s, "Pushed",
            "node 1 OnStart\nentry OnStart 1\nnode 2 FindObject From\nnode 3 Knockback\n"
            "exec 1 0 3\ndata 3 0 2 0\ndefault 3 1 8\n");
        s.Start();
        CHECK(v->gameObject->transform->Position().x > 0.2f);
    }

    // Tweens and a door swing. The caller waits.
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Tween",
            "node 1 OnStart\nentry OnStart 1\nnode 2 TweenPosition inout\n"
            "node 3 MakeVec3\ndefault 3 0 3\ndefault 3 1 0\ndefault 3 2 0\n"
            "exec 1 0 2\ndata 2 0 3 0\ndefault 2 1 0.2\nnode 4 SetVar done\ndefault 4 0 1\nexec 2 0 4\n");
        s.Start();
        CHECK(v->Variables().count("done") == 0);
        Updates(s, 5, 0.05f);
        CHECK(v->GetVariable("done").AsBool());
        CHECK_NEAR(v->gameObject->transform->Position().x, 3.0f, 0.05f);
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Door",
            "node 1 OnStart\nentry OnStart 1\nnode 2 DoorOpen\ndefault 2 0 90\ndefault 2 1 0.2\nexec 1 0 2\n");
        s.Start();
        Updates(s, 6, 0.05f);
        CHECK(std::fabs(v->gameObject->transform->localRotation.ToEuler().y) > 60.0f);
    }

    // Score, countdown, lists, ammo, proximity, checkpoint.
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Score",
            "node 1 OnStart\nentry OnStart 1\nnode 2 ScoreAdd\ndefault 2 0 3\nexec 1 0 2\n"
            "node 3 ScoreAdd\ndefault 3 0 2\nexec 2 0 3\nnode 4 ScoreGet\nnode 5 SetVar total\n"
            "exec 3 0 5\ndata 5 0 4 0\n");
        s.Start();
        CHECK_NEAR(v->GetVariable("total").AsFloat(), 5.0f, 1e-3f);
        CHECK_NEAR(vs::SceneVariables(&s)["score"].AsFloat(), 5.0f, 1e-3f);
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Clock",
            "node 1 OnStart\nentry OnStart 1\nnode 2 Countdown 0.2\nexec 1 0 2\n"
            "node 3 SetVar done\ndefault 3 0 1\nexec 2 0 3\n");
        s.Start();
        CHECK(v->Variables().count("done") == 0);
        Updates(s, 5, 0.05f);
        CHECK(v->GetVariable("done").AsBool());
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Lists",
            "node 1 OnStart\nentry OnStart 1\nnode 2 NewList\nnode 3 SetVar bag\nnode 4 GetVar bag\n"
            "node 5 Const 4\nnode 6 Const 1\nnode 7 ListAdd\nnode 8 ListAdd\nnode 9 ListMin\nnode 10 ListMax\n"
            "node 11 SetVar lo\nnode 12 SetVar hi\nnode 13 ShuffleList\nnode 14 ListLength\nnode 15 SetVar n\n"
            "exec 1 0 3\ndata 3 0 2 0\nexec 3 0 7\ndata 7 0 4 0\ndata 7 1 5 0\n"
            "exec 7 0 8\ndata 8 0 4 0\ndata 8 1 6 0\nexec 8 0 11\ndata 11 0 9 0\ndata 9 0 4 0\n"
            "exec 11 0 12\ndata 12 0 10 0\ndata 10 0 4 0\nexec 12 0 15\ndata 15 0 14 0\ndata 14 0 13 0\ndata 13 0 4 0\n");
        s.Start();
        CHECK_NEAR(v->GetVariable("lo").AsFloat(), 1.0f, 1e-3f);
        CHECK_NEAR(v->GetVariable("hi").AsFloat(), 4.0f, 1e-3f);
        CHECK_NEAR(v->GetVariable("n").AsFloat(), 2.0f, 1e-3f);
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Gun",
            "node 1 OnStart\nentry OnStart 1\nnode 2 Ammo 2\nnode 3 AddVar shots\ndefault 3 0 1\n"
            "exec 1 0 2\nexec 2 0 3\nexec 3 0 2\nnode 4 SetVar empty\ndefault 4 0 1\nexec 2 1 4\n");
        s.Start();
        CHECK_NEAR(v->GetVariable("shots").AsFloat(), 2.0f, 1e-3f);
        CHECK(v->GetVariable("empty").AsBool());
    }
    {
        Scene s; s.physicsEnabled = false;
        GameObject* other = s.CreateGameObject("Other");
        other->transform->SetPosition({1, 0, 0});
        auto* v = Attach(s, "Here",
            "node 1 OnStart\nentry OnStart 1\nnode 2 FindObject Other\nnode 3 Proximity\n"
            "default 3 1 2\nnode 4 SetVar near\nexec 1 0 4\ndata 4 0 3 0\ndata 3 0 2 0\n");
        s.Start();
        CHECK(v->GetVariable("near").AsBool());
    }

    // Health component: damage, heal, death, respawn. No death animation.
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Body",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 Once\nnode 3 Damage\ndefault 3 0 30\n"
            "exec 1 0 2\nexec 2 0 3\nnode 4 Heal\ndefault 4 0 10\nexec 3 0 4\n"
            "node 5 IsDead\nnode 6 SetVar dead\nexec 4 0 6\ndata 6 0 5 0\n");
        auto* hp = v->gameObject->AddComponent<HealthStat>();
        hp->playDeathAnim = false;
        hp->maxHealth = 100;
        s.Start();
        s.Update(0.016f);
        CHECK_NEAR(hp->health, 80.0f, 0.1f);   // 100 - 30 + 10
        CHECK(!v->GetVariable("dead").AsBool());
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Body2",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 Once\nnode 3 Damage\ndefault 3 0 500\n"
            "exec 1 0 2\nexec 2 0 3\n"
            "node 4 OnDeath\nentry OnDeath 4\nnode 5 SetVar died\ndefault 5 0 1\nexec 4 0 5\n"
            "node 6 Respawn\nexec 5 0 6\n");
        auto* hp = v->gameObject->AddComponent<HealthStat>();
        hp->playDeathAnim = false;
        hp->destroyOnDeath = false;
        hp->respawn = false;
        s.Start();
        Updates(s, 1, 0.016f);
        CHECK(v->GetVariable("died").AsBool());
        CHECK(!hp->IsDead());
    }

    // Checkpoint uses the existing save API.
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Hero",
            "node 1 OnStart\nentry OnStart 1\nnode 2 CheckpointSave\nexec 1 0 2\n");
        v->gameObject->transform->SetPosition({3, 1, 4});
        s.Start();
        CHECK(Save::GetString("vs_checkpoint_ok", "") == "1");
        v->gameObject->transform->SetPosition({0, 0, 0});
        std::string err;
        CHECK(v->LoadFromText(
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 Once\nnode 3 CheckpointLoad\n"
            "exec 1 0 2\nexec 2 0 3\nnode 4 SetVar back\ndefault 4 0 1\nexec 3 0 4\n", &err));
        s.Update(0.016f);
        CHECK(v->GetVariable("back").AsBool());
        CHECK_NEAR(v->gameObject->transform->Position().x, 3.0f, 0.05f);
    }

    // UI, audio and a missing component is an error, not a crash.
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Hud",
            "node 1 OnStart\nentry OnStart 1\nnode 2 SetProgress\ndefault 2 0 0.25\nexec 1 0 2\n"
            "node 3 ShowFloatingText\ndefault 3 0 \"Hi\"\nexec 2 0 3\n"
            "node 4 FadeScreen\ndefault 4 0 0.5\nexec 3 0 4\n");
        v->gameObject->AddComponent<UIProgressBar>();
        v->gameObject->AddComponent<TextRenderer>();
        v->gameObject->AddComponent<UIPanel>();
        s.Start();
        CHECK_NEAR(v->gameObject->GetComponent<UIProgressBar>()->value, 0.25f, 1e-3f);
        CHECK(v->gameObject->GetComponent<TextRenderer>()->text == "Hi");
        CHECK_NEAR(v->gameObject->GetComponent<UIPanel>()->color.a, 0.5f, 1e-3f);
    }
    {
        Scene s; s.physicsEnabled = false;
        g_errs.clear();
        vs::SetRuntimeErrorHook([](const vs::VsRuntimeError& e) { g_errs.push_back(e.message); });
        Attach(s, "Quiet",
            "node 1 OnStart\nentry OnStart 1\nnode 2 PlaySoundAt\nexec 1 0 2\nnode 3 StopSound\nexec 2 0 3\n");
        s.Start();
        CHECK(!g_errs.empty());
        vs::SetRuntimeErrorHook(nullptr);
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Speaker",
            "node 1 OnStart\nentry OnStart 1\nnode 2 PlaySoundAt\nexec 1 0 2\n");
        auto* au = v->gameObject->AddComponent<AudioSource>();
        s.Start();
        CHECK(au->IsPlaying());
        CHECK(au->spatial);
    }

    // AI: chase and can-see, with no physics so the cone is the whole test.
    {
        Scene s; s.physicsEnabled = false;
        GameObject* player = s.CreateGameObject("Player");
        player->tag = "Player";
        player->transform->SetPosition({3, 0, 0});
        auto* v = Attach(s, "Enemy",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 FindWithTag Player\nnode 3 Chase\n"
            "exec 1 0 3\ndata 3 0 2 0\ndefault 3 1 10\n");
        s.Start();
        Updates(s, 10, 0.05f);
        CHECK(v->gameObject->transform->Position().x > 1.0f);
    }
    {
        Scene s; s.physicsEnabled = false;
        GameObject* foe = s.CreateGameObject("Foe");
        foe->transform->SetPosition({0, 0, 2});
        auto* v = Attach(s, "Looker",
            "node 1 OnStart\nentry OnStart 1\nnode 2 FindObject Foe\nnode 3 CanSee\n"
            "default 3 1 10\ndefault 3 2 120\nnode 4 SetVar seen\nexec 1 0 4\ndata 4 0 3 0\ndata 3 0 2 0\n");
        // Face +Z, where Forward() points for an unrotated object.
        s.Start();
        CHECK(v->GetVariable("seen").AsBool());
    }

    // Lever flips, collect adds score. Win freezes time; put it back.
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Switch",
            "node 1 OnStart\nentry OnStart 1\nnode 2 Lever\nnode 3 SetVar on\ndefault 3 0 1\n"
            "node 4 SetVar off\ndefault 4 0 1\nexec 1 0 2\nexec 2 0 3\nexec 3 0 2\nexec 2 1 4\n");
        s.Start();
        CHECK(v->GetVariable("on").AsBool());
        CHECK(v->GetVariable("off").AsBool());
    }
    {
        Scene s; s.physicsEnabled = false;
        Attach(s, "Coin",
            "node 1 OnStart\nentry OnStart 1\nnode 2 Collect\ndefault 2 0 4\nexec 1 0 2\n");
        s.Start();
        CHECK_NEAR(vs::SceneVariables(&s)["score"].AsFloat(), 4.0f, 1e-3f);
    }
    {
        Time::SetTimeScale(1.0f);
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "End",
            "node 1 OnStart\nentry OnStart 1\nnode 2 Win\nexec 1 0 2\n");
        s.Start();
        CHECK(vs::SceneVariables(&s)["outcome"].AsString() == "win");
        CHECK(Time::TimeScale() == 0.0f);
        Time::SetTimeScale(1.0f);
        (void)v;
    }

    CHECK(sandbox.Verify());   // nothing was written into the source tree
    TEST_MAIN_RESULT();
}
