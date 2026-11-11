// Visual-script logic and runtime: latent loops, maps, switches, coercion,
// hot reload of node state, script/graph calls, and runtime errors.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Core/Log.hpp"
#include "okay/VisualScript/VsVariables.hpp"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace okay;

static std::string Ex(const char* name) {
    return std::string(OKAY_SOURCE_DIR) + "/examples/visual_script/" + name;
}

static VisualScriptComponent* Attach(Scene& s, const char* name, const std::string& text) {
    GameObject* go = s.CreateGameObject(name);
    auto* vc = go->AddComponent<VisualScriptComponent>();
    std::string err;
    if (!vc->LoadFromText(text, &err)) std::printf("  load: %s\n", err.c_str());
    return vc;
}

static VisualScriptComponent* AttachFile(Scene& s, const char* name, const std::string& path) {
    GameObject* go = s.CreateGameObject(name);
    auto* vc = go->AddComponent<VisualScriptComponent>();
    std::string err;
    if (!vc->LoadFromFile(path, &err)) std::printf("  load file %s: %s\n", path.c_str(), err.c_str());
    return vc;
}

static void Updates(Scene& s, int n, float dt) {
    for (int i = 0; i < n; ++i) s.Update(dt);
}

static std::vector<std::string> g_errs;
static void OnErr(const vs::VsRuntimeError& e) {
    g_errs.push_back(e.nodeType + "#" + std::to_string(e.fileId) + " " + e.message);
}

int main() {
    RUN_SUITE("vs_runtime");
    vs::SetRuntimeErrorHook(&OnErr);

    // ---- Example graphs -------------------------------------------------------
    {
        Scene s; s.physicsEnabled = false;
        auto* v = AttachFile(s, "Door", Ex("door.okayvs"));
        s.Start();
        CHECK(v->GetVariable("has_key").AsBool());
        CHECK(v->Variables().count("door") == 0);          // still waiting
        Updates(s, 2, 0.1f);
        CHECK(v->Variables().count("door") == 0);
        Updates(s, 4, 0.1f);
        CHECK(v->GetVariable("door").AsString() == "open");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = AttachFile(s, "Patrol", Ex("patrol.okayvs"));
        s.Start();
        CHECK(v->GetVariable("state").AsString() == "patrol");
        Updates(s, 3, 0.016f);
        CHECK(v->GetVariable("state").AsString() == "wait");
        Updates(s, 2, 0.016f);
        CHECK(v->GetVariable("state").AsString() == "return");
        Updates(s, 3, 0.016f);
        CHECK(v->GetVariable("state").AsString() == "patrol");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = AttachFile(s, "Bag", Ex("inventory.okayvs"));
        s.Start();
        CHECK_NEAR(v->GetVariable("total").AsFloat(), 11.0f, 1e-4f);   // arrow 10 + key 1; potion skipped
        CHECK(v->GetVariable("done").AsBool());
        auto bag = v->GetVariable("bag").AsMap();
        CHECK(bag && bag->size() == 3);
        CHECK_NEAR(bag->at("potion").AsFloat(), 2.0f, 1e-4f);
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = AttachFile(s, "Loot", Ex("foreach_break.okayvs"));
        s.Start();
        CHECK_NEAR(v->GetVariable("seen").AsFloat(), 2.0f, 1e-4f);
        CHECK(v->GetVariable("last").AsString() == "key");
    }

    // ---- A Delay inside For Each waits between items -------------------------
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Slow",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 NewList\nnode 3 SetVar xs\nexec 1 0 3\ndata 3 0 2 0\n"
            "node 4 GetVar xs\nnode 5 Const 1\nnode 6 ListAdd\nexec 3 0 6\ndata 6 0 4 0\ndata 6 1 5 0\n"
            "node 7 GetVar xs\nnode 8 Const 2\nnode 9 ListAdd\nexec 6 0 9\ndata 9 0 7 0\ndata 9 1 8 0\n"
            "node 10 GetVar xs\nnode 11 Const 3\nnode 12 ListAdd\nexec 9 0 12\ndata 12 0 10 0\ndata 12 1 11 0\n"
            "node 13 GetVar xs\nnode 14 ForEach\nexec 12 0 14\ndata 14 0 13 0\n"
            "node 15 Delay 0.25\nexec 14 0 15\n"
            "node 16 AddVar n\ndefault 16 0 1\nexec 15 0 16\n");
        s.Start();
        CHECK(v->GetVariable("n").AsFloat() == 0.0f);
        Updates(s, 2, 0.1f);
        CHECK(v->GetVariable("n").AsFloat() == 0.0f);     // still on the first delay
        Updates(s, 1, 0.1f);
        CHECK_NEAR(v->GetVariable("n").AsFloat(), 1.0f, 1e-4f);
        Updates(s, 6, 0.1f);
        CHECK_NEAR(v->GetVariable("n").AsFloat(), 3.0f, 1e-4f);
        CHECK(v->Graph() && !v->Graph()->HasSuspended());
    }

    // ---- Timer keeps the leftover ---------------------------------------------
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "T",
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 Timer 1\nnode 3 AddVar n\ndefault 3 0 1\n"
            "exec 1 0 2\nexec 2 0 3\n");
        s.Start();
        s.Update(0.6f);
        CHECK(v->GetVariable("n").AsFloat() == 0.0f);
        s.Update(0.6f);                                   // 1.2s: fires, 0.2s left
        CHECK_NEAR(v->GetVariable("n").AsFloat(), 1.0f, 1e-4f);
        s.Update(0.9f);                                   // 1.1s: fires again
        CHECK_NEAR(v->GetVariable("n").AsFloat(), 2.0f, 1e-4f);
    }

    // ---- Coercion --------------------------------------------------------------
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "C",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 Const \"3\"\nnode 3 Const 1\nnode 4 Add\nnode 5 SetVar sum\n"
            "exec 1 0 5\ndata 4 0 2 0\ndata 4 1 3 0\ndata 5 0 4 0\n"
            "node 6 Const \"false\"\nnode 7 Branch\nnode 8 SetVar bad\ndefault 8 0 1\n"
            "node 9 SetVar good\ndefault 9 0 1\n"
            "exec 5 0 7\ndata 7 0 6 0\nexec 7 0 8\nexec 7 1 9\n"
            "node 10 Const \"0\"\nnode 11 Branch\nnode 12 SetVar zero_bad\ndefault 12 0 1\n"
            "exec 9 0 11\ndata 11 0 10 0\nexec 11 0 12\n");
        s.Start();
        CHECK_NEAR(v->GetVariable("sum").AsFloat(), 4.0f, 1e-4f);
        CHECK(v->GetVariable("good").AsBool());
        CHECK(v->Variables().count("bad") == 0);
        CHECK(v->Variables().count("zero_bad") == 0);
    }

    // ---- Switches, latch, reset ------------------------------------------------
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Sw",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 Const 2\nnode 3 SwitchCases 0,2,5,9\nnode 4 SetVar which\nnode 5 Const \"second\"\n"
            "exec 1 0 3\ndata 3 0 2 0\nexec 3 1 4\ndata 4 0 5 0\n"
            "node 6 LatchGate\nnode 7 AddVar closed\ndefault 7 0 1\n"
            "node 8 LatchGate open\nnode 9 AddVar opened\ndefault 9 0 1\n"
            "exec 4 0 8\nexec 8 0 9\nexec 9 0 6\nexec 6 0 7\n");
        s.Start();
        CHECK(v->GetVariable("which").AsString() == "second");
        CHECK(v->Variables().count("closed") == 0);
        CHECK_NEAR(v->GetVariable("opened").AsFloat(), 1.0f, 1e-4f);
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Gate",
            "node 1 OnUpdate\nentry OnUpdate 1\n"
            "node 2 LatchGate\ndefault 2 2 true\nnode 3 AddVar n\ndefault 3 0 1\n"
            "exec 1 0 2\nexec 2 0 3\n");
        s.Start();
        Updates(s, 4, 0.016f);
        CHECK_NEAR(v->GetVariable("n").AsFloat(), 2.0f, 1e-4f);   // open, closed, open, closed
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Once",
            "node 1 OnUpdate\nentry OnUpdate 1\n"
            "node 2 Once\nnode 3 AddVar n\ndefault 3 0 1\nexec 1 0 2\nexec 2 0 3\n"
            "node 4 GetVar reset\nexec 2 0 2\n");
        // Reset is a data pin, not a second exec. Drive it from a variable.
        std::string text =
            "node 1 OnUpdate\nentry OnUpdate 1\n"
            "node 2 Once\nnode 3 AddVar n\ndefault 3 0 1\n"
            "node 4 GetVar reset\n"
            "exec 1 0 2\nexec 2 0 3\ndata 2 0 4 0\n";
        std::string err;
        CHECK(v->LoadFromText(text, &err));
        s.Start();
        s.Update(0.016f);
        CHECK_NEAR(v->GetVariable("n").AsFloat(), 1.0f, 1e-4f);
        s.Update(0.016f);
        CHECK_NEAR(v->GetVariable("n").AsFloat(), 1.0f, 1e-4f);
        v->SetVariable("reset", vs::VsValue{true});
        s.Update(0.016f);                                     // reset, does not pass
        CHECK_NEAR(v->GetVariable("n").AsFloat(), 1.0f, 1e-4f);
        v->SetVariable("reset", vs::VsValue{false});
        s.Update(0.016f);
        CHECK_NEAR(v->GetVariable("n").AsFloat(), 2.0f, 1e-4f);
    }

    // ---- Hot reload keeps Once and a parked Delay -----------------------------
    {
        Scene s; s.physicsEnabled = false;
        const char* text =
            "node 1 OnStart\nentry OnStart 1\nnode 2 Once\nnode 3 AddVar n\ndefault 3 0 1\n"
            "exec 1 0 2\nexec 2 0 3\n";
        auto* v = Attach(s, "Hot", text);
        s.Start();
        CHECK_NEAR(v->GetVariable("n").AsFloat(), 1.0f, 1e-4f);
        std::string err;
        CHECK(v->LoadFromText(text, &err));
        v->Start();                                           // On Start again
        CHECK_NEAR(v->GetVariable("n").AsFloat(), 1.0f, 1e-4f);
    }
    {
        Scene s; s.physicsEnabled = false;
        const char* text =
            "node 1 OnStart\nentry OnStart 1\nnode 2 Delay 0.3\nnode 3 SetVar done\ndefault 3 0 1\n"
            "exec 1 0 2\nexec 2 0 3\n";
        auto* v = Attach(s, "Hold", text);
        s.Start();
        s.Update(0.1f);
        CHECK(v->Variables().count("done") == 0);
        std::string err;
        CHECK(v->LoadFromText(text, &err));                   // same nodes, delay still counting
        Updates(s, 1, 0.1f);
        CHECK(v->Variables().count("done") == 0);
        Updates(s, 2, 0.1f);
        CHECK(v->GetVariable("done").AsBool());
    }

    // ---- Call a text script, and run another graph ----------------------------
    {
        Scene s; s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Mix");
        auto* sc = go->AddComponent<ScriptComponent>();
        std::string err;
        CHECK(sc->LoadSource("function bump(n) { return n + 1; }\n", &err));
        auto* v = go->AddComponent<VisualScriptComponent>();
        CHECK(v->LoadFromText(
            "node 1 OnStart\nentry OnStart 1\nnode 2 Const 4\nnode 3 CallScript bump\n"
            "node 4 SetVar out\nexec 1 0 3\nexec 3 0 4\ndata 3 0 2 0\ndata 4 0 3 0\n", &err));
        s.Start();
        CHECK_NEAR(v->GetVariable("out").AsFloat(), 5.0f, 1e-4f);
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = AttachFile(s, "Parent", Ex("call_sub.okayvs"));
        s.Start();
        CHECK_NEAR(v->GetVariable("from_sub").AsFloat(), 7.0f, 1e-4f);
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = AttachFile(s, "ParentWait", Ex("call_wait.okayvs"));
        s.Start();
        CHECK(v->Variables().count("from_sub") == 0);
        Updates(s, 4, 0.1f);
        CHECK_NEAR(v->GetVariable("from_sub").AsFloat(), 9.0f, 1e-4f);
    }

    // ---- Is Valid, missing object, divide by zero, bad function argument ------
    g_errs.clear();
    {
        Scene s; s.physicsEnabled = false;
        GameObject* other = s.CreateGameObject("Other");
        auto* v = Attach(s, "Self",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 FindObject Other\nnode 3 IsValid\nnode 4 SetVar alive\n"
            "exec 1 0 4\ndata 3 0 2 0\ndata 4 0 3 0\n"
            "node 5 Const 1\nnode 6 Const 0\nnode 7 Div\nnode 8 SetVar q\n"
            "exec 4 0 8\ndata 7 0 5 0\ndata 7 1 6 0\ndata 8 0 7 0\n"
            "node 9 SetObjectVar hp\ndefault 9 0 \"Nobody\"\ndefault 9 1 1\nexec 8 0 9\n");
        s.Start();
        CHECK(v->GetVariable("alive").AsBool());
        CHECK(v->GetVariable("q").AsFloat() == 0.0f);
        s.Destroy(other);
        s.Update(0.016f);                                 // flush the queued destroy
        v->SetVariable("alive", vs::VsValue{true});
        v->Start();
        CHECK(!v->GetVariable("alive").AsBool());
    }
    {
        bool sawDiv = false, sawMiss = false, sawNode = false;
        for (const auto& e : g_errs) {
            if (e.find("divided by zero") != std::string::npos) sawDiv = true;
            if (e.find("Nobody") != std::string::npos) sawMiss = true;
            if (e.rfind("Div#", 0) == 0) sawNode = true;
        }
        CHECK(sawDiv);
        CHECK(sawMiss);
        CHECK(sawNode);
    }
    g_errs.clear();
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Fn",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 FunctionStart \"Take(xs: List)\"\nnode 3 Return\nexec 2 0 3\n"
            "node 4 CallFunction Take\nnode 5 Const 3\nexec 1 0 4\ndata 4 0 5 0\n");
        s.Start();
        bool saw = false;
        for (const auto& e : g_errs) if (e.find("needs a List") != std::string::npos) saw = true;
        CHECK(saw);
        CHECK(v->Graph() && v->Graph()->ErrorCount() >= 1);
    }

    // ---- A numeric string and a map round-trip through Get / Has --------------
    {
        CHECK(vs::VsValue("3").AsFloat() == 3.0f);
        CHECK(!vs::VsValue("false").AsBool());
        CHECK(!vs::VsValue("0").AsBool());
        CHECK(vs::VsValue("hello").AsBool());
        CHECK(!vs::VsValue(std::numeric_limits<float>::quiet_NaN()).AsBool());
        auto m = vs::VsValue::MakeMap();
        (*m.AsMap())["a"] = vs::VsValue{1.0f};
        CHECK(m.AsFloat() == 1.0f);
        CHECK(m.AsBool());
    }

    // ---- Any number of switch cases, and an old Default wire on pin 4 --------
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Many",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 Const 5\nnode 3 SwitchCases 0,1,2,3,4,5\n"
            "node 4 SetVar hit\nnode 5 Const \"five\"\n"
            "node 6 SetVar hit\nnode 7 Const \"no\"\n"
            "exec 1 0 3\ndata 3 0 2 0\nexec 3 5 4\ndata 4 0 5 0\nexec 3 6 6\ndata 6 0 7 0\n");
        s.Start();
        CHECK(v->GetVariable("hit").AsString() == "five");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Old",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 Const \"z\"\nnode 3 SwitchString a,b\n"
            "node 4 SetVar hit\nnode 5 Const \"def\"\n"
            "exec 1 0 3\ndata 3 0 2 0\nexec 3 4 4\ndata 4 0 5 0\n");
        s.Start();
        CHECK(v->GetVariable("hit").AsString() == "def");
    }

    // ---- A Delay inside a function waits the caller, and keeps the local ------
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "FnWait",
            "node 10 FunctionStart \"AddOne(n: Number) -> out: Number\"\n"
            "node 11 SetLocal kept\nnode 12 Delay 0.25\nnode 13 GetLocal kept\nnode 14 Return AddOne\n"
            "exec 10 0 11\nexec 11 0 12\nexec 12 0 14\ndata 11 0 10 0\ndata 14 0 13 0\n"
            "node 1 OnStart\nentry OnStart 1\nnode 2 Const 5\nnode 3 CallFunction AddOne\nnode 4 SetVar out\n"
            "exec 1 0 3\nexec 3 0 4\ndata 3 0 2 0\ndata 4 0 3 0\n");
        s.Start();
        CHECK(v->Variables().count("out") == 0);
        s.Update(0.1f);
        CHECK(v->Variables().count("out") == 0);
        Updates(s, 2, 0.1f);
        CHECK_NEAR(v->GetVariable("out").AsFloat(), 5.0f, 1e-4f);
    }

    // ---- Reloading the parent keeps the called graph's Delay ------------------
    {
        Scene s; s.physicsEnabled = false;
        auto* v = AttachFile(s, "ParentWait2", Ex("call_wait.okayvs"));
        s.Start();
        s.Update(0.1f);                                   // 0.15s left of a 0.25s delay
        std::string err;
        CHECK(v->LoadFromText(v->Source(), &err));
        s.Update(0.1f);
        CHECK(v->Variables().count("from_sub") == 0);     // not restarted from a full 0.25s
        s.Update(0.1f);
        CHECK_NEAR(v->GetVariable("from_sub").AsFloat(), 9.0f, 1e-4f);
    }

    // ---- Ready-made recipes, headless ---------------------------------------
    {
        Scene s; s.physicsEnabled = false;
        auto* v = AttachFile(s, "Coins", Ex("recipe_coins.okayvs"));
        s.Start();
        CHECK(v->GetVariable("banked").AsBool());
        CHECK_NEAR(vs::SceneVariables(&s)["score"].AsFloat(), 1.0f, 1e-3f);
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = AttachFile(s, "Clock", Ex("recipe_countdown.okayvs"));
        s.Start();
        CHECK(v->Variables().count("finished") == 0);
        Updates(s, 6, 0.05f);
        CHECK(v->GetVariable("finished").AsBool());
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = AttachFile(s, "Door", Ex("recipe_door.okayvs"));
        s.Start();
        CHECK(v->GetVariable("has_key").AsBool());
        Updates(s, 6, 0.05f);
        CHECK(std::fabs(v->gameObject->transform->localRotation.ToEuler().y) > 60.0f);
    }
    {
        Scene s; s.physicsEnabled = false;
        GameObject* player = s.CreateGameObject("Player");
        player->tag = "Player";
        player->transform->SetPosition({4, 0, 0});
        auto* v = AttachFile(s, "Enemy", Ex("recipe_chase.okayvs"));
        s.Start();
        Updates(s, 15, 0.05f);
        CHECK(v->gameObject->transform->Position().x > 1.0f);
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = AttachFile(s, "Body", Ex("recipe_health.okayvs"));
        auto* hp = v->gameObject->AddComponent<HealthStat>();
        hp->playDeathAnim = false;
        hp->maxHealth = 50;
        s.Start();
        s.Update(0.016f);
        CHECK(hp->health < 50.0f);
        CHECK(!v->GetVariable("dead").AsBool());
    }

    vs::SetRuntimeErrorHook(nullptr);
    TEST_MAIN_RESULT();
}
