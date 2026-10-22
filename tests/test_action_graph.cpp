#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/ActionList.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/VisualScript/ActionGraph.hpp"
#include <cmath>
#include <string>

using namespace okay;

// One graph editor: the Actions step library runs inside node graphs (Action,
// Actions block, Condition, Delay), and an Actions component converts to a
// graph that behaves the same.
static VisualScriptComponent* Attach(Scene& s, const char* name, const std::string& text) {
    GameObject* go = s.CreateGameObject(name);
    auto* vc = go->AddComponent<VisualScriptComponent>();
    std::string err;
    if (!vc->LoadFromText(text, &err)) std::printf("load error: %s\n", err.c_str());
    return vc;
}
static float Var(const char* n) { return ActionList::GetVar(n); }

int main() {
    RUN_SUITE("action_graph");

    {   // Step encoding survives spaces, quotes, '#', ';' and empty args.
        ActionList::Item it{"say", {"Main Camera", "#ff0000", "a;b", "", "50%"}};
        std::string enc = ActionList::EncodeItem(it);
        CHECK(enc.find(' ') != std::string::npos);
        CHECK(enc.find('#') == std::string::npos && enc.find(';') == std::string::npos && enc.find('"') == std::string::npos);
        ActionList::Item back = ActionList::DecodeItem(enc);
        CHECK(back.op == "say" && back.args.size() == 5);
        CHECK(back.args[0] == "Main Camera" && back.args[1] == "#ff0000" && back.args[2] == "a;b" && back.args[3].empty() && back.args[4] == "50%");
        auto list = ActionList::DecodeItems(ActionList::EncodeItems({it, {"wait", {"1"}}}));
        CHECK(list.size() == 2 && list[1].op == "wait" && list[0].args[2] == "a;b");
    }

    {   // Action + Condition nodes run the step library on the graph's object.
        ActionList::ResetVars();
        Scene s; s.physicsEnabled = false;
        auto* vc = Attach(s, "A",
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 Action \"set_var hp 5\"\nexec 1 0 2\n"
            "node 3 Condition \"var_gt hp 3\"\nexec 2 0 3\n"
            "node 4 Action \"set_var big 1\"\nexec 3 0 4\n"
            "node 5 Action \"set_var big -1\"\nexec 3 1 5\n"
            "node 6 Action \"move 2 0 0\"\nexec 4 0 6\n");
        s.Start();
        CHECK(std::fabs(Var("hp") - 5.0f) < 1e-4f);
        CHECK(std::fabs(Var("big") - 1.0f) < 1e-4f);
        CHECK(std::fabs(vc->gameObject->transform->localPosition.x - 2.0f) < 1e-4f);
    }

    {   // Delay really pauses a chain (and continues it once).
        ActionList::ResetVars();
        Scene s; s.physicsEnabled = false;
        Attach(s, "D",
            "node 1 OnStart\nentry OnStart 1\nnode 2 Delay 0.5\nexec 1 0 2\n"
            "node 3 Action \"add_var n 1\"\nexec 2 0 3\n");
        s.Start();
        CHECK(Var("n") == 0.0f);
        for (int i = 0; i < 20; ++i) s.Update(0.02f);   // 0.4 s
        CHECK(Var("n") == 0.0f);
        for (int i = 0; i < 10; ++i) s.Update(0.02f);   // 0.6 s
        CHECK(std::fabs(Var("n") - 1.0f) < 1e-4f);
        for (int i = 0; i < 50; ++i) s.Update(0.02f);
        CHECK(std::fabs(Var("n") - 1.0f) < 1e-4f);
    }

    {   // An Actions block with a Wait + Repeat keeps its timing, then continues the chain.
        ActionList::ResetVars();
        Scene s; s.physicsEnabled = false;
        std::string steps = ActionList::EncodeItems({{"repeat", {"3"}}, {"add_var", {"k", "1"}}, {"wait", {"0.1"}},
                                                     {"end_repeat", {}}});
        Attach(s, "B", "node 1 OnStart\nentry OnStart 1\nnode 2 Actions \"" + steps + "\"\nexec 1 0 2\n"
                       "node 3 Action \"set_var done 1\"\nexec 2 0 3\n");
        s.Start();
        CHECK(std::fabs(Var("k") - 1.0f) < 1e-4f);
        CHECK(Var("done") == 0.0f);
        for (int i = 0; i < 30; ++i) s.Update(0.02f);
        CHECK(std::fabs(Var("k") - 3.0f) < 1e-4f);
        CHECK(std::fabs(Var("done") - 1.0f) < 1e-4f);
    }

    {   // Converting an Actions component gives a graph that does the same.
        ActionList::ResetVars();
        ActionList al;
        al.trigger = ActionList::Trigger::OnStart;
        al.variables.push_back({"coins", 0, "10"});
        al.instructions = {{"add_var", {"coins", "5"}}, {"wait", {"0.2"}}, {"set_var", {"later", "1"}}};
        ActionList::Handler h;
        h.trigger = ActionList::Trigger::OnMessage; h.triggerKey = "Hit";
        h.conditions = {{"var_gt", {"coins", "1"}}};
        h.instructions = {{"add_var", {"hits", "1"}}};
        al.extraHandlers.push_back(h);
        std::string text = vs::ActionListToGraph(al);
        CHECK(text.find("entry OnEvent:Hit") != std::string::npos);
        CHECK(text.find(" Delay ") != std::string::npos);

        Scene s; s.physicsEnabled = false;
        auto* vc = Attach(s, "C", text);
        CHECK(vc->LastError().empty());
        s.Start();
        CHECK(std::fabs(Var("coins") - 15.0f) < 1e-4f);
        CHECK(Var("later") == 0.0f);
        for (int i = 0; i < 20; ++i) s.Update(0.02f);
        CHECK(std::fabs(Var("later") - 1.0f) < 1e-4f);
        vc->SendEvent("Hit");
        CHECK(std::fabs(Var("hits") - 1.0f) < 1e-4f);

        // Actions' "send" reaches the graph too.
        GameObject* other = s.CreateGameObject("Sender");
        auto* sender = other->AddComponent<ActionList>();
        sender->instructions = {{"send", {"Hit"}}};
        sender->BeginRun(); sender->Update(0.0f);
        CHECK(std::fabs(Var("hits") - 2.0f) < 1e-4f);
    }

    {   // Repeating triggers with a Wait stay one block (no overlapping runs).
        ActionList al;
        al.trigger = ActionList::Trigger::OnUpdate;
        al.instructions = {{"add_var", {"u", "1"}}, {"wait", {"1"}}};
        std::string text = vs::ActionListToGraph(al);
        CHECK(text.find(" Actions ") != std::string::npos);
        CHECK(text.find(" Delay ") == std::string::npos);
    }

    TEST_MAIN_RESULT();
}
