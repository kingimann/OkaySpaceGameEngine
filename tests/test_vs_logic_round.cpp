// Every logic-round node, run on an object in a real scene.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Scene/Scene.hpp"

#include <cmath>
#include <set>
#include <string>
#include <vector>

using namespace okay;

static std::vector<std::string> g_errs;
static std::set<std::string> g_seen;

static void OnErr(const vs::VsRuntimeError& e) { g_errs.push_back(e.nodeType + " " + e.message); }

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
    RUN_SUITE("vs_logic_round");
    vs::SetRuntimeErrorHook(&OnErr);
    Time::SetTimeScale(1);
    const char* types[] = {
        "NotAnd", "NotOr", "AllTrue", "AnyTrue", "IsEven", "IsOdd", "PickByIndex", "FirstValid",
        "BranchInOrder", "DoWhile", "ForStep", "WaitUntil", "WaitWhile", "BecameTrue", "BecameFalse",
        "CountUntil", "WeightedBranch", "WaitSteady", "StepThrough", "WaitLimit",
        "SetState", "GetState", "IsInState", "WhenStateBecomes", "WhenStateLeaves",
    };

    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Nand",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 Const false\nnode 3 Const true\nnode 4 NotAnd\n"
            "data 4 0 2 0\ndata 4 1 3 0\n"
            "node 5 SetVar out\nexec 1 0 5\ndata 5 0 4 0\n");
        s.Start(); s.Update(0.05f);
        CHECK(Var(v, "out").AsBool());
        g_seen.insert("NotAnd");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Nor",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 Const false\nnode 3 Const false\nnode 4 NotOr\n"
            "data 4 0 2 0\ndata 4 1 3 0\n"
            "node 5 SetVar out\nexec 1 0 5\ndata 5 0 4 0\n");
        s.Start(); s.Update(0.05f);
        CHECK(Var(v, "out").AsBool());
        g_seen.insert("NotOr");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Lists",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 NewList\nnode 3 Const true\nnode 4 Const false\n"
            "node 5 SetVar items\nexec 1 0 5\ndata 5 0 2 0\n"
            "node 6 GetVar items\nnode 7 ListAdd\nnode 8 ListAdd\n"
            "exec 5 0 7\nexec 7 0 8\n"
            "data 7 0 6 0\ndata 7 1 3 0\ndata 8 0 6 0\ndata 8 1 4 0\n"
            "node 9 AllTrue\ndata 9 0 6 0\n"
            "node 10 AnyTrue\ndata 10 0 6 0\n"
            "node 11 SetVar all\nexec 8 0 11\ndata 11 0 9 0\n"
            "node 12 SetVar any\nexec 11 0 12\ndata 12 0 10 0\n");
        s.Start(); s.Update(0.05f);
        CHECK(!Var(v, "all").AsBool());
        CHECK(Var(v, "any").AsBool());
        g_seen.insert("AllTrue");
        g_seen.insert("AnyTrue");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Parity",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 Const 4\nnode 3 IsEven\nnode 4 IsOdd\n"
            "data 3 0 2 0\ndata 4 0 2 0\n"
            "node 5 SetVar even\nexec 1 0 5\ndata 5 0 3 0\n"
            "node 6 SetVar odd\nexec 5 0 6\ndata 6 0 4 0\n");
        s.Start(); s.Update(0.05f);
        CHECK(Var(v, "even").AsBool());
        CHECK(!Var(v, "odd").AsBool());
        g_seen.insert("IsEven");
        g_seen.insert("IsOdd");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Pick",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 NewList\nnode 3 Const 10\nnode 4 Const 20\n"
            "node 5 SetVar items\nexec 1 0 5\ndata 5 0 2 0\n"
            "node 6 GetVar items\nnode 7 ListAdd\nnode 8 ListAdd\n"
            "exec 5 0 7\nexec 7 0 8\n"
            "data 7 0 6 0\ndata 7 1 3 0\ndata 8 0 6 0\ndata 8 1 4 0\n"
            "node 9 PickByIndex\ndefault 9 0 1\ndata 9 1 6 0\n"
            "node 10 SetVar item\nexec 8 0 10\ndata 10 0 9 0\n");
        s.Start(); s.Update(0.05f);
        CHECK(std::fabs(Var(v, "item").AsFloat() - 20.0f) < 0.01f);
        g_seen.insert("PickByIndex");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "First",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 FirstValid\ndefault 2 0 \"\"\ndefault 2 1 \"cat\"\n"
            "node 3 SetVar got\nexec 1 0 3\ndata 3 0 2 0\n");
        s.Start(); s.Update(0.05f);
        CHECK(Var(v, "got").AsString() == "cat");
        g_seen.insert("FirstValid");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Chain",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 BranchInOrder\ndefault 2 0 false\ndefault 2 1 true\n"
            "node 3 SetVar which\ndefault 3 0 \"no\"\n"
            "node 4 SetVar which\ndefault 4 0 \"yes\"\n"
            "exec 1 0 2\nexec 2 0 3\nexec 2 1 4\n");
        s.Start(); s.Update(0.05f);
        CHECK(Var(v, "which").AsString() == "yes");
        g_seen.insert("BranchInOrder");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Do",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 DoWhile\ndefault 2 0 false\n"
            "node 3 AddVar n\ndefault 3 0 1\n"
            "exec 2 0 3\nexec 1 0 2\n"
            "node 4 SetVar done\ndefault 4 0 true\nexec 2 1 4\n");
        s.Start(); s.Update(0.05f);
        CHECK(std::fabs(Var(v, "n").AsFloat() - 1.0f) < 0.01f);
        CHECK(Var(v, "done").AsBool());
        g_seen.insert("DoWhile");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Step",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 ForStep\ndefault 2 0 4\ndefault 2 1 0\ndefault 2 2 -2\n"
            "node 3 AddVar n\ndefault 3 0 1\n"
            "exec 2 0 3\nexec 1 0 2\n");
        s.Start(); s.Update(0.05f);
        // 4, 2, 0
        CHECK(std::fabs(Var(v, "n").AsFloat() - 3.0f) < 0.01f);
        g_seen.insert("ForStep");
    }
    {
        g_errs.clear();
        Scene s; s.physicsEnabled = false;
        Attach(s, "BadStep",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 ForStep\ndefault 2 0 0\ndefault 2 1 5\ndefault 2 2 0\n"
            "node 3 AddVar n\ndefault 3 0 1\nexec 2 0 3\nexec 1 0 2\n");
        s.Start(); s.Update(0.05f);
        bool saw = false;
        for (const auto& e : g_errs) if (e.find("Step is 0") != std::string::npos) saw = true;
        CHECK(saw);
        g_errs.clear();
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Until",
            "node 1 OnUpdate\nentry OnUpdate 1\n"
            "node 2 GetVar go\nnode 3 WaitUntil\ndata 3 0 2 0\n"
            "exec 1 0 3\nnode 4 SetVar done\ndefault 4 0 true\nexec 3 0 4\n");
        s.Start();
        s.Update(0.05f); s.Update(0.05f);
        CHECK(!Var(v, "done").AsBool());
        v->SetVariable("go", vs::VsValue(true));
        s.Update(0.05f);
        CHECK(Var(v, "done").AsBool());
        g_seen.insert("WaitUntil");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "While",
            "node 1 OnUpdate\nentry OnUpdate 1\n"
            "node 2 GetVar go\nnode 3 WaitWhile\ndata 3 0 2 0\n"
            "exec 1 0 3\nnode 4 SetVar done\ndefault 4 0 true\nexec 3 0 4\n");
        v->SetVariable("go", vs::VsValue(true));
        s.Start();
        s.Update(0.05f);
        CHECK(!Var(v, "done").AsBool());
        v->SetVariable("go", vs::VsValue(false));
        s.Update(0.05f);
        CHECK(Var(v, "done").AsBool());
        g_seen.insert("WaitWhile");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Rise",
            "node 1 OnUpdate\nentry OnUpdate 1\n"
            "node 2 GetVar go\nnode 3 BecameTrue\ndata 3 0 2 0\n"
            "exec 1 0 3\nnode 4 SetVar edged\ndefault 4 0 true\nexec 3 0 4\n");
        s.Start(); s.Update(0.05f);
        CHECK(!Var(v, "edged").AsBool());
        v->SetVariable("go", vs::VsValue(true));
        s.Update(0.05f);
        CHECK(Var(v, "edged").AsBool());
        g_seen.insert("BecameTrue");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Fall",
            "node 1 OnUpdate\nentry OnUpdate 1\n"
            "node 2 GetVar go\nnode 3 BecameFalse\ndata 3 0 2 0\n"
            "exec 1 0 3\nnode 4 SetVar edged\ndefault 4 0 true\nexec 3 0 4\n");
        v->SetVariable("go", vs::VsValue(true));
        s.Start(); s.Update(0.05f);
        CHECK(!Var(v, "edged").AsBool());
        v->SetVariable("go", vs::VsValue(false));
        s.Update(0.05f);
        CHECK(Var(v, "edged").AsBool());
        g_seen.insert("BecameFalse");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Count",
            "node 1 OnUpdate\nentry OnUpdate 1\n"
            "node 2 CountUntil\ndefault 2 0 3\n"
            "node 3 SetVar n\nexec 2 0 3\ndata 3 0 2 0\n"
            "node 4 SetVar done\ndefault 4 0 true\nexec 2 1 4\n"
            "exec 1 0 2\n");
        s.Start();
        s.Update(0.05f); s.Update(0.05f);
        CHECK(!Var(v, "done").AsBool());
        CHECK(std::fabs(Var(v, "n").AsFloat() - 2.0f) < 0.01f);
        s.Update(0.05f);
        CHECK(Var(v, "done").AsBool());
        g_seen.insert("CountUntil");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Weight",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 WeightedBranch\ndefault 2 0 1\ndefault 2 1 0\n"
            "node 3 SetVar first\ndefault 3 0 true\n"
            "node 4 SetVar second\ndefault 4 0 true\n"
            "exec 1 0 2\nexec 2 0 3\nexec 2 1 4\n");
        s.Start(); s.Update(0.05f);
        CHECK(Var(v, "first").AsBool());
        CHECK(!Var(v, "second").AsBool());
        g_seen.insert("WeightedBranch");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Steady",
            "node 1 OnUpdate\nentry OnUpdate 1\n"
            "node 2 WaitSteady\ndefault 2 0 true\ndefault 2 1 0.1\n"
            "node 3 SetVar done\ndefault 3 0 true\n"
            "exec 1 0 2\nexec 2 0 3\n");
        s.Start();
        s.Update(0.05f);
        CHECK(!Var(v, "done").AsBool());
        s.Update(0.05f);
        CHECK(Var(v, "done").AsBool());
        g_seen.insert("WaitSteady");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Cycle",
            "node 1 OnUpdate\nentry OnUpdate 1\n"
            "node 2 StepThrough\n"
            "node 3 SetVar which\ndefault 3 0 \"1\"\n"
            "node 4 SetVar which\ndefault 4 0 \"2\"\n"
            "node 5 SetVar which\ndefault 5 0 \"3\"\n"
            "exec 1 0 2\nexec 2 0 3\nexec 2 1 4\nexec 2 2 5\n");
        s.Start();
        s.Update(0.05f); CHECK(Var(v, "which").AsString() == "1");
        s.Update(0.05f); CHECK(Var(v, "which").AsString() == "2");
        s.Update(0.05f); CHECK(Var(v, "which").AsString() == "3");
        s.Update(0.05f); CHECK(Var(v, "which").AsString() == "1");
        g_seen.insert("StepThrough");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Limit",
            "node 1 OnUpdate\nentry OnUpdate 1\n"
            "node 2 WaitLimit\ndefault 2 0 false\ndefault 2 1 0.1\n"
            "node 3 SetVar ready\ndefault 3 0 true\n"
            "node 4 SetVar late\ndefault 4 0 true\n"
            "exec 1 0 2\nexec 2 0 3\nexec 2 1 4\n");
        s.Start();
        s.Update(0.05f);
        CHECK(!Var(v, "late").AsBool());
        s.Update(0.05f);
        s.Update(0.05f);
        CHECK(Var(v, "late").AsBool());
        CHECK(!Var(v, "ready").AsBool());
        g_seen.insert("WaitLimit");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "State",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 SetState\ndefault 2 0 \"play\"\n"
            "node 3 GetState\nnode 4 SetVar got\n"
            "exec 1 0 2\nexec 2 0 4\ndata 4 0 3 0\n"
            "node 5 IsInState\ndefault 5 0 \"play\"\n"
            "node 6 SetVar inn\nexec 4 0 6\ndata 6 0 5 0\n");
        s.Start(); s.Update(0.05f);
        CHECK(Var(v, "got").AsString() == "play");
        CHECK(Var(v, "inn").AsBool());
        g_seen.insert("SetState");
        g_seen.insert("GetState");
        g_seen.insert("IsInState");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Enter",
            "node 1 OnUpdate\nentry OnUpdate 1\n"
            "node 2 WhenStateBecomes\ndefault 2 0 \"play\"\n"
            "node 3 SetVar edged\ndefault 3 0 true\n"
            "exec 1 0 2\nexec 2 0 3\n");
        s.Start(); s.Update(0.05f);
        CHECK(!Var(v, "edged").AsBool());
        v->SetVariable("state", vs::VsValue(std::string("play")));
        s.Update(0.05f);
        CHECK(Var(v, "edged").AsBool());
        g_seen.insert("WhenStateBecomes");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Leave",
            "node 1 OnUpdate\nentry OnUpdate 1\n"
            "node 2 WhenStateLeaves\ndefault 2 0 \"play\"\n"
            "node 3 SetVar edged\ndefault 3 0 true\n"
            "exec 1 0 2\nexec 2 0 3\n");
        v->SetVariable("state", vs::VsValue(std::string("play")));
        s.Start(); s.Update(0.05f);
        CHECK(!Var(v, "edged").AsBool());
        v->SetVariable("state", vs::VsValue(std::string("idle")));
        s.Update(0.05f);
        CHECK(Var(v, "edged").AsBool());
        g_seen.insert("WhenStateLeaves");
    }
    {
        Scene s; s.physicsEnabled = false;
        auto* v = Attach(s, "Text",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 Compare ==\ndefault 2 0 \"cat\"\ndefault 2 1 \"cat\"\n"
            "node 3 Compare >\ndefault 3 0 \"b\"\ndefault 3 1 \"a\"\n"
            "node 4 SetVar same\nexec 1 0 4\ndata 4 0 2 0\n"
            "node 5 SetVar after\nexec 4 0 5\ndata 5 0 3 0\n"
            "node 6 Self\nnode 7 Compare ==\ndata 7 0 6 0\ndata 7 1 6 0\n"
            "node 8 SetVar obj\nexec 5 0 8\ndata 8 0 7 0\n");
        s.Start(); s.Update(0.05f);
        CHECK(Var(v, "same").AsBool());
        CHECK(Var(v, "after").AsBool());
        CHECK(Var(v, "obj").AsBool());
    }
    {
        g_errs.clear();
        Scene s; s.physicsEnabled = false;
        Attach(s, "ObjOrder",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 Self\nnode 3 Compare >\ndata 3 0 2 0\ndata 3 1 2 0\n"
            "node 4 SetVar bad\nexec 1 0 4\ndata 4 0 3 0\n");
        s.Start(); s.Update(0.05f);
        bool saw = false;
        for (const auto& e : g_errs) if (e.find("same or not the same") != std::string::npos) saw = true;
        CHECK(saw);
        g_errs.clear();
    }

    for (const char* t : types) {
        if (!g_seen.count(t)) std::printf("  logic node not covered: %s\n", t);
        CHECK(g_seen.count(t) == 1);
    }
    CHECK(g_errs.empty());
    vs::SetRuntimeErrorHook(nullptr);
    Time::SetTimeScale(1);
    TEST_MAIN_RESULT();
}
