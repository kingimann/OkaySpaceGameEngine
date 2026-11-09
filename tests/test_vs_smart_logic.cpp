// Higher-level game logic, each node run on an object in a real scene.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/LineRenderer.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Core/SaveData.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Scene/Scene.hpp"

#include <cmath>
#include <set>
#include <string>

using namespace okay;

static std::vector<std::string> g_errs;
static std::set<std::string> g_seen;
static void OnErr(const vs::VsRuntimeError& e) { g_errs.push_back(e.nodeType + " " + e.message); }
static bool Saw(const char* frag) {
    for (const auto& e : g_errs) if (e.find(frag) != std::string::npos) return true;
    return false;
}

static VisualScriptComponent* Attach(Scene& s, const char* name, const std::string& text) {
    GameObject* go = s.CreateGameObject(name);
    auto* vc = go->AddComponent<VisualScriptComponent>();
    std::string err;
    CHECK(vc->LoadFromText(text, &err));
    if (!err.empty()) std::printf("  load %s: %s\n", name, err.c_str());
    return vc;
}
static vs::VsValue Var(VisualScriptComponent* v, const char* name) { return v->GetVariable(name); }

int main() {
    RUN_SUITE("vs_smart_logic");
    vs::SetRuntimeErrorHook(&OnErr);
    Time::SetTimeScale(1);
    Save::SetBaseDir("/tmp/vs_smart_save");
    const char* types[] = {
        "HeldLong", "HappenedNear", "EveryWhile", "EaseToward", "BlendTime", "Stopwatch",
        "IsSlightly", "IsMostly", "IsVery", "PickBest", "ShuffleBag", "ChanceEachSecond",
        "TryFirst", "AllSucceed", "FlipOutcome", "AlwaysSucceed", "RepeatUntilFail", "WaitBetween",
        "SucceedNow", "FailNow", "CheckOutcome",
        "InCone", "LineOfSight", "InsideArea", "CountNearby", "NearestObject", "FarthestObject",
        "SortByDistance", "WhenChanged", "DoUnless", "RememberValue", "RecallValue",
        "HealthEmpty", "HealthFull", "AddBuff", "AdvanceObjective", "AssertThis", "LogOnce", "DrawDebugLine",
    };

    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Held",
            "node 1 OnUpdate\nentry OnUpdate 1\n"
            "node 2 HeldLong\ndefault 2 0 true\ndefault 2 1 0.1\n"
            "node 3 SetVar out\nexec 1 0 3\ndata 3 0 2 0\n");
        s.Start(); s.Update(0.05f);
        CHECK(!Var(v, "out").AsBool());
        s.Update(0.05f);
        CHECK(Var(v, "out").AsBool());
        g_seen.insert("HeldLong");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Near",
            "node 1 OnUpdate\nentry OnUpdate 1\n"
            "node 2 GetVar a\nnode 3 GetVar b\n"
            "node 4 HappenedNear\ndefault 4 2 1\n"
            "data 4 0 2 0\ndata 4 1 3 0\n"
            "node 5 SetVar out\nexec 1 0 5\ndata 5 0 4 0\n");
        s.Start(); s.Update(0.05f);
        CHECK(!Var(v, "out").AsBool());
        v->SetVariable("a", vs::VsValue(true));
        s.Update(0.05f);
        v->SetVariable("b", vs::VsValue(true));
        s.Update(0.05f);
        CHECK(Var(v, "out").AsBool());
        g_seen.insert("HappenedNear");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Every",
            "node 1 OnUpdate\nentry OnUpdate 1\n"
            "node 2 EveryWhile\ndefault 2 0 true\ndefault 2 1 0.1\n"
            "node 3 SetVar out\ndefault 3 0 true\nexec 1 0 2\nexec 2 0 3\n");
        s.Start(); s.Update(0.05f);
        CHECK(!Var(v, "out").AsBool());
        s.Update(0.05f);
        CHECK(Var(v, "out").AsBool());
        g_seen.insert("EveryWhile");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Ease",
            "node 1 OnUpdate\nentry OnUpdate 1\n"
            "node 2 EaseToward\ndefault 2 0 0\ndefault 2 1 10\ndefault 2 2 10\n"
            "node 3 SetVar out\nexec 1 0 3\ndata 3 0 2 0\n");
        s.Start(); s.Update(0.05f);
        CHECK(std::fabs(Var(v, "out").AsFloat() - 0.5f) < 0.02f);
        g_seen.insert("EaseToward");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Blend",
            "node 1 OnUpdate\nentry OnUpdate 1\n"
            "node 2 BlendTime\ndefault 2 0 0\ndefault 2 1 10\ndefault 2 2 0.05\n"
            "node 3 SetVar out\nexec 1 0 2\nexec 2 0 3\ndata 3 0 2 0\n");
        s.Start();
        s.Update(0.05f);
        CHECK(!Var(v, "out").AsBool());
        s.Update(0.06f);
        CHECK(std::fabs(Var(v, "out").AsFloat() - 10.0f) < 0.05f);
        g_seen.insert("BlendTime");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Watch",
            "node 1 OnUpdate\nentry OnUpdate 1\n"
            "node 2 Stopwatch\nnode 3 SetVar out\nexec 1 0 2\nexec 2 0 3\ndata 3 0 2 0\n");
        s.Start(); s.Update(0.05f); s.Update(0.05f);
        CHECK(Var(v, "out").AsFloat() >= 0.09f);
        g_seen.insert("Stopwatch");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Fuzzy",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 Const 0.3\nnode 3 Const 0.6\nnode 4 Const 0.9\n"
            "node 5 IsSlightly\nnode 6 IsMostly\nnode 7 IsVery\n"
            "data 5 0 2 0\ndata 6 0 3 0\ndata 7 0 4 0\n"
            "node 8 SetVar a\nexec 1 0 8\ndata 8 0 5 0\n"
            "node 9 SetVar b\nexec 8 0 9\ndata 9 0 6 0\n"
            "node 10 SetVar c\nexec 9 0 10\ndata 10 0 7 0\n");
        s.Start(); s.Update(0.05f);
        CHECK(Var(v, "a").AsBool());
        CHECK(Var(v, "b").AsBool());
        CHECK(Var(v, "c").AsBool());
        g_seen.insert("IsSlightly"); g_seen.insert("IsMostly"); g_seen.insert("IsVery");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Best",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 PickBest\ndefault 2 0 1\ndefault 2 1 5\ndefault 2 2 3\ndefault 2 3 0\n"
            "node 3 SetVar out\nexec 1 0 3\ndata 3 0 2 0\n");
        s.Start(); s.Update(0.05f);
        CHECK(std::fabs(Var(v, "out").AsFloat() - 1.0f) < 0.01f);
        g_seen.insert("PickBest");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Bag",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 NewList\nnode 3 Const 7\nnode 4 SetVar items\n"
            "exec 1 0 4\ndata 4 0 2 0\n"
            "node 5 GetVar items\nnode 6 ListAdd\nexec 4 0 6\n"
            "data 6 0 5 0\ndata 6 1 3 0\n"
            "node 7 ShuffleBag\ndata 7 0 5 0\n"
            "node 8 SetVar out\nexec 6 0 8\ndata 8 0 7 0\n");
        s.Start(); s.Update(0.05f);
        CHECK(std::fabs(Var(v, "out").AsFloat() - 7.0f) < 0.01f);
        g_seen.insert("ShuffleBag");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Roll",
            "node 1 OnUpdate\nentry OnUpdate 1\n"
            "node 2 ChanceEachSecond\ndefault 2 0 0\n"
            "node 3 ChanceEachSecond\ndefault 3 0 1000\n"
            "node 4 SetVar never\nexec 1 0 4\ndata 4 0 2 0\n"
            "node 5 SetVar always\nexec 4 0 5\ndata 5 0 3 0\n");
        s.Start(); s.Update(0.05f);
        CHECK(!Var(v, "never").AsBool());
        CHECK(Var(v, "always").AsBool());
        g_seen.insert("ChanceEachSecond");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Tree",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 FailNow\nnode 3 CheckOutcome\ndefault 3 0 true\n"
            "node 4 SetVar hit\ndefault 4 0 \"yes\"\nexec 3 0 4\n"
            "node 5 TryFirst\nexec 1 0 5\nexec 5 0 2\nexec 5 1 3\n");
        s.Start(); s.Update(0.05f);
        CHECK(Var(v, "hit").AsString() == "yes");
        g_seen.insert("TryFirst"); g_seen.insert("FailNow"); g_seen.insert("CheckOutcome");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "All",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 CheckOutcome\ndefault 2 0 true\n"
            "node 3 CheckOutcome\ndefault 3 0 false\n"
            "node 4 SetVar hit\ndefault 4 0 \"no\"\n"
            "node 5 AllSucceed\nexec 1 0 5\nexec 5 0 2\nexec 5 1 3\nexec 5 2 4\n");
        s.Start(); s.Update(0.05f);
        CHECK(Var(v, "hit").AsString() != "no");
        g_seen.insert("AllSucceed");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Flip",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 SucceedNow\nnode 3 FlipOutcome\nexec 3 0 2\n"
            "node 4 SetVar hit\ndefault 4 0 \"yes\"\n"
            "node 5 TryFirst\nexec 1 0 5\nexec 5 0 3\nexec 5 1 4\n");
        s.Start(); s.Update(0.05f);
        CHECK(Var(v, "hit").AsString() == "yes");
        g_seen.insert("FlipOutcome"); g_seen.insert("SucceedNow");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Always",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 FailNow\nnode 3 AlwaysSucceed\nexec 3 0 2\n"
            "node 4 SetVar hit\ndefault 4 0 \"no\"\n"
            "node 5 TryFirst\nexec 1 0 5\nexec 5 0 3\nexec 5 1 4\n");
        s.Start(); s.Update(0.05f);
        CHECK(Var(v, "hit").AsString() != "no");
        g_seen.insert("AlwaysSucceed");
    }
    {
        g_errs.clear();
        Scene s; s.physicsEnabled = false;
        Attach(s, "Rep",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 FailNow\nnode 3 RepeatUntilFail\nexec 3 0 2\nexec 1 0 3\n");
        s.Start(); s.Update(0.05f);
        CHECK(!Saw("16 tries"));
        g_seen.insert("RepeatUntilFail");
        g_errs.clear();
    }
    {
        g_errs.clear();
        Scene s; s.physicsEnabled = false;
        Attach(s, "RepBad",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 SucceedNow\nnode 3 RepeatUntilFail\nexec 3 0 2\nexec 1 0 3\n");
        s.Start(); s.Update(0.05f);
        CHECK(Saw("16 tries"));
        g_errs.clear();
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Cool",
            "node 1 OnUpdate\nentry OnUpdate 1\n"
            "node 2 SetVar hit\ndefault 2 0 true\n"
            "node 3 WaitBetween\ndefault 3 0 1\nexec 3 0 2\nexec 1 0 3\n");
        s.Start(); s.Update(0.05f);
        CHECK(Var(v, "hit").AsBool());
        v->SetVariable("hit", vs::VsValue(false));
        s.Update(0.05f);
        CHECK(!Var(v, "hit").AsBool());
        g_seen.insert("WaitBetween");
    }
    {
        Scene s; s.physicsEnabled = false;
        GameObject* post = s.CreateGameObject("Post");
        post->transform->SetPosition({0, 0, 2});
        auto* v = Attach(s, "Eye",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 FindObject Post\nnode 3 InCone\ndefault 3 1 50\n"
            "data 3 0 2 0\nnode 4 SetVar see\nexec 1 0 4\ndata 4 0 3 0\n");
        v->gameObject->transform->SetPosition({0, 0, 0});
        s.Start(); s.Update(0.05f);
        CHECK(Var(v, "see").AsBool());
        g_seen.insert("InCone");
    }
    {
        g_errs.clear();
        Scene s; s.physicsEnabled = false;
        Attach(s, "Los",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 FindObject Missing\nnode 3 LineOfSight\ndata 3 0 2 0\n"
            "node 4 SetVar see\nexec 1 0 4\ndata 4 0 3 0\n");
        s.Start(); s.Update(0.05f);
        CHECK(Saw("physics"));
        g_seen.insert("LineOfSight");
        g_errs.clear();
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Box",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 MakeVec3\ndefault 2 0 0\ndefault 2 1 0\ndefault 2 2 0\n"
            "node 3 MakeVec3\ndefault 3 0 4\ndefault 3 1 4\ndefault 3 2 4\n"
            "node 4 InsideArea\ndata 4 0 2 0\ndata 4 1 3 0\n"
            "node 5 SetVar inn\nexec 1 0 5\ndata 5 0 4 0\n");
        s.Start(); s.Update(0.05f);
        CHECK(Var(v, "inn").AsBool());
        g_seen.insert("InsideArea");
    }
    {
        Scene s; s.physicsEnabled = false;
        GameObject* near = s.CreateGameObject("NearOne");
        near->transform->SetPosition({1, 0, 0});
        GameObject* far = s.CreateGameObject("FarOne");
        far->transform->SetPosition({30, 0, 0});
        auto* v = Attach(s, "Query",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 CountNearby\ndefault 2 0 5\n"
            "node 3 NearestObject\ndefault 3 0 0\n"
            "node 4 FarthestObject\ndefault 4 0 0\n"
            "node 5 SetVar n\nexec 1 0 5\ndata 5 0 2 0\n"
            "node 6 SetVar near\nexec 5 0 6\ndata 6 0 3 0\n"
            "node 7 SetVar far\nexec 6 0 7\ndata 7 0 4 0\n");
        v->gameObject->transform->SetPosition({0, 0, 0});
        s.Start(); s.Update(0.05f);
        CHECK(std::fabs(Var(v, "n").AsFloat() - 1.0f) < 0.01f);
        CHECK(Var(v, "near").AsString() == "NearOne");
        CHECK(Var(v, "far").AsString() == "FarOne");
        g_seen.insert("CountNearby"); g_seen.insert("NearestObject"); g_seen.insert("FarthestObject");
    }
    {
        Scene s; s.physicsEnabled = false;
        GameObject* a = s.CreateGameObject("Aaa");
        a->transform->SetPosition({1, 0, 0});
        GameObject* b = s.CreateGameObject("Bbb");
        b->transform->SetPosition({8, 0, 0});
        auto* v = Attach(s, "Sort",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 NewList\nnode 3 SetVar items\nexec 1 0 3\ndata 3 0 2 0\n"
            "node 4 GetVar items\nnode 5 FindObject Bbb\nnode 6 FindObject Aaa\n"
            "node 7 ListAdd\nnode 8 ListAdd\n"
            "exec 3 0 7\nexec 7 0 8\n"
            "data 7 0 4 0\ndata 7 1 5 0\ndata 8 0 4 0\ndata 8 1 6 0\n"
            "node 9 SortByDistance\ndata 9 0 4 0\n"
            "node 10 ListGet\ndefault 10 1 0\ndata 10 0 9 0\n"
            "node 11 SetVar first\nexec 8 0 11\ndata 11 0 10 0\n");
        v->gameObject->transform->SetPosition({0, 0, 0});
        s.Start(); s.Update(0.05f);
        CHECK(Var(v, "first").AsString() == "Aaa");
        g_seen.insert("SortByDistance");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Change",
            "node 1 OnUpdate\nentry OnUpdate 1\n"
            "node 2 GetVar n\nnode 3 WhenChanged\ndata 3 0 2 0\n"
            "node 4 SetVar hit\ndefault 4 0 true\nexec 1 0 3\nexec 3 0 4\n");
        v->SetVariable("n", vs::VsValue(1.0f));
        s.Start(); s.Update(0.05f);
        CHECK(!Var(v, "hit").AsBool());
        v->SetVariable("n", vs::VsValue(2.0f));
        s.Update(0.05f);
        CHECK(Var(v, "hit").AsBool());
        g_seen.insert("WhenChanged");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Unless",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 DoUnless\ndefault 2 0 true\ndefault 2 1 true\n"
            "node 3 SetVar hit\ndefault 3 0 \"then\"\n"
            "node 4 SetVar hit\ndefault 4 0 \"skip\"\n"
            "exec 1 0 2\nexec 2 0 3\nexec 2 1 4\n");
        s.Start(); s.Update(0.05f);
        CHECK(Var(v, "hit").AsString() == "skip");
        g_seen.insert("DoUnless");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Save",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 RememberValue\ndefault 2 0 flag\ndefault 2 1 \"kept\"\n"
            "node 3 RecallValue\ndefault 3 0 flag\n"
            "node 4 SetVar out\nexec 1 0 2\nexec 2 0 4\ndata 4 0 3 0\n");
        s.Start(); s.Update(0.05f);
        CHECK(Var(v, "out").AsString() == "kept");
        g_seen.insert("RememberValue"); g_seen.insert("RecallValue");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Hp",
            "node 1 OnUpdate\nentry OnUpdate 1\n"
            "node 2 HealthEmpty\nnode 3 SetVar empty\ndefault 3 0 true\n"
            "exec 1 0 2\nexec 2 0 3\n");
        auto* hp = v->gameObject->AddComponent<HealthStat>();
        hp->health = 10; hp->maxHealth = 10;
        s.Start(); s.Update(0.05f);
        CHECK(!Var(v, "empty").AsBool());
        hp->health = 0;
        s.Update(0.05f);
        CHECK(Var(v, "empty").AsBool());
        g_seen.insert("HealthEmpty");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Full",
            "node 1 OnUpdate\nentry OnUpdate 1\n"
            "node 2 HealthFull\nnode 3 SetVar full\ndefault 3 0 true\n"
            "exec 1 0 2\nexec 2 0 3\n");
        auto* hp = v->gameObject->AddComponent<HealthStat>();
        hp->health = 4; hp->maxHealth = 10;
        s.Start();
        hp->health = 4;
        s.Update(0.05f);
        CHECK(!Var(v, "full").AsBool());
        hp->health = 10;
        s.Update(0.05f);
        CHECK(Var(v, "full").AsBool());
        g_seen.insert("HealthFull");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Buff",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 AddBuff\ndefault 2 0 10\ndefault 2 1 5\ndefault 2 2 1\ndefault 2 3 10\n"
            "node 3 SetVar out\nexec 1 0 2\nexec 2 0 3\ndata 3 0 2 0\n");
        s.Start(); s.Update(0.05f);
        CHECK(std::fabs(Var(v, "out").AsFloat() - 15.0f) < 0.05f);
        g_seen.insert("AddBuff");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Quest",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 AdvanceObjective\ndefault 2 0 2\n"
            "node 3 SetVar step\ndefault 3 0 true\n"
            "node 4 SetVar done\ndefault 4 0 true\n"
            "exec 1 0 2\nexec 2 0 3\nexec 2 1 4\n");
        s.Start(); s.Update(0.05f);
        CHECK(Var(v, "step").AsBool());
        CHECK(!Var(v, "done").AsBool());
        g_seen.insert("AdvanceObjective");
    }
    {
        g_errs.clear();
        Scene s; s.physicsEnabled = false;
        Attach(s, "Assert",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 AssertThis\ndefault 2 0 false\ndefault 2 1 \"boom\"\nexec 1 0 2\n");
        s.Start(); s.Update(0.05f);
        CHECK(Saw("boom"));
        g_seen.insert("AssertThis");
        g_errs.clear();
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Once",
            "node 1 OnUpdate\nentry OnUpdate 1\n"
            "node 2 LogOnce\ndefault 2 0 \"hi\"\n"
            "node 3 SetVar hit\ndefault 3 0 true\nexec 1 0 2\nexec 2 0 3\n");
        s.Start(); s.Update(0.05f);
        CHECK(Var(v, "hit").AsBool());
        v->SetVariable("hit", vs::VsValue(false));
        s.Update(0.05f);
        CHECK(!Var(v, "hit").AsBool());
        g_seen.insert("LogOnce");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Line",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 MakeVec3\ndefault 2 0 0\ndefault 2 1 0\ndefault 2 2 0\n"
            "node 3 MakeVec3\ndefault 3 0 0\ndefault 3 1 2\ndefault 3 2 0\n"
            "node 4 DrawDebugLine\ndata 4 0 2 0\ndata 4 1 3 0\nexec 1 0 4\n");
        s.Start(); s.Update(0.05f);
        auto* line = v->gameObject->GetComponent<LineRenderer>();
        CHECK(line && line->PositionCount() == 2);
        g_seen.insert("DrawDebugLine");
    }

    for (const char* t : types) {
        if (!g_seen.count(t)) std::printf("  smart node not covered: %s\n", t);
        CHECK(g_seen.count(t) == 1);
    }
    CHECK(g_errs.empty());
    vs::SetRuntimeErrorHook(nullptr);
    Time::SetTimeScale(1);
    TEST_MAIN_RESULT();
}
