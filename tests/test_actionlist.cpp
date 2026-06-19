#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

static ActionList::Item I(const std::string& op, std::vector<std::string> a = {}) {
    return ActionList::Item{op, std::move(a)};
}

int main() {
    RUN_SUITE("actionlist");

    // OnStart: run instructions top-to-bottom (set a var, move the transform).
    {
        ActionList::ResetVars();
        Scene s("A"); s.physicsEnabled = false;
        GameObject* o = s.CreateGameObject("Hero");
        auto* al = o->AddComponent<ActionList>();
        al->trigger = ActionList::Trigger::OnStart;
        al->instructions = { I("set_var", {"score", "10"}), I("move", {"2", "3", "0"}) };
        s.Start();
        s.Update(0.016f);
        CHECK_NEAR(ActionList::Vars()["score"], 10.0f, 1e-4f);
        CHECK_NEAR(o->transform->localPosition.x, 2.0f, 1e-4f);
        CHECK_NEAR(o->transform->localPosition.y, 3.0f, 1e-4f);
    }

    // Conditions gate the list (AND of all). var_eq false -> nothing runs.
    {
        ActionList::ResetVars();
        ActionList::Vars()["lvl"] = 1.0f;
        Scene s("B"); s.physicsEnabled = false;
        GameObject* o = s.CreateGameObject("Gate");
        auto* al = o->AddComponent<ActionList>();
        al->trigger = ActionList::Trigger::OnStart;
        al->conditions = { I("var_eq", {"lvl", "2"}) };   // 1 != 2 -> blocked
        al->instructions = { I("move", {"5", "0", "0"}) };
        s.Start(); s.Update(0.016f);
        CHECK_NEAR(o->transform->localPosition.x, 0.0f, 1e-4f);

        // Same setup but the condition now passes -> the move runs.
        ActionList::Vars()["lvl"] = 2.0f;
        Scene s2("B2"); s2.physicsEnabled = false;
        GameObject* o2 = s2.CreateGameObject("Gate2");
        auto* al2 = o2->AddComponent<ActionList>();
        al2->trigger = ActionList::Trigger::OnStart;
        al2->conditions = { I("var_eq", {"lvl", "2"}) };
        al2->instructions = { I("move", {"5", "0", "0"}) };
        s2.Start(); s2.Update(0.016f);
        CHECK_NEAR(o2->transform->localPosition.x, 5.0f, 1e-4f);
    }

    // Wait pauses between steps.
    {
        ActionList::ResetVars();
        Scene s("C"); s.physicsEnabled = false;
        GameObject* o = s.CreateGameObject("Seq");
        auto* al = o->AddComponent<ActionList>();
        al->trigger = ActionList::Trigger::OnStart;
        al->instructions = { I("move", {"1", "0", "0"}), I("wait", {"0.5"}), I("move", {"1", "0", "0"}) };
        s.Start();
        s.Update(0.1f);   // first move done, now waiting
        CHECK_NEAR(o->transform->localPosition.x, 1.0f, 1e-4f);
        s.Update(0.2f);   // still waiting (0.3 < 0.5)
        CHECK_NEAR(o->transform->localPosition.x, 1.0f, 1e-4f);
        s.Update(0.4f);   // wait elapses, second move runs
        CHECK_NEAR(o->transform->localPosition.x, 2.0f, 1e-4f);
    }

    // OnKey trigger with `once`: fires on key-down, only the first time.
    {
        ActionList::ResetVars();
        Scene s("D"); s.physicsEnabled = false;
        GameObject* o = s.CreateGameObject("Jump");
        auto* al = o->AddComponent<ActionList>();
        al->trigger = ActionList::Trigger::OnKey; al->triggerKey = "w"; al->once = true;
        al->instructions = { I("add_var", {"jumps", "1"}) };
        s.Start();
        Input::FeedKeys({'w'}); s.Update(0.016f);   // key-down edge
        Input::FeedKeys({'w'}); s.Update(0.016f);   // held, no new edge
        Input::FeedKeys({});    s.Update(0.016f);
        Input::FeedKeys({'w'}); s.Update(0.016f);   // second press, but once=true
        CHECK_NEAR(ActionList::Vars()["jumps"], 1.0f, 1e-4f);
    }

    // Text round-trip preserves trigger, key, once, conditions and instructions.
    {
        ActionList al;
        al.trigger = ActionList::Trigger::OnKey; al.triggerKey = "space"; al.once = true;
        al.conditions = { I("chance", {"0.5"}) };
        al.instructions = { I("move", {"0", "1", "0"}), I("set_text", {"Hi", "there"}) };
        std::string text = al.ToText();
        ActionList b; b.FromText(text);
        CHECK(b.trigger == ActionList::Trigger::OnKey);
        CHECK(b.triggerKey == "space");
        CHECK(b.once);
        CHECK(b.conditions.size() == 1 && b.conditions[0].op == "chance");
        CHECK(b.instructions.size() == 2);
        CHECK(b.instructions[1].op == "set_text");
        CHECK(b.instructions[1].args.size() == 2);   // "Hi" + "there"
    }

    // Signals: one list sends a message, another (OnMessage) reacts.
    {
        ActionList::ResetVars();
        Scene s("M"); s.physicsEnabled = false;
        GameObject* sender = s.CreateGameObject("Sender");
        auto* sa = sender->AddComponent<ActionList>();
        sa->trigger = ActionList::Trigger::OnStart;
        sa->instructions = { I("send", {"door_open"}) };

        GameObject* door = s.CreateGameObject("Door");
        auto* da = door->AddComponent<ActionList>();
        da->trigger = ActionList::Trigger::OnMessage; da->triggerKey = "door_open";
        da->instructions = { I("set_var", {"opened", "1"}) };

        s.Start(); s.Update(0.016f);
        CHECK_NEAR(ActionList::Vars()["opened"], 1.0f, 1e-4f);
    }

    // `stop` ends the list early; later instructions don't run.
    {
        ActionList::ResetVars();
        Scene s("E"); s.physicsEnabled = false;
        GameObject* o = s.CreateGameObject("Stopper");
        auto* al = o->AddComponent<ActionList>();
        al->trigger = ActionList::Trigger::OnStart;
        al->instructions = { I("move", {"1", "0", "0"}), I("stop"), I("move", {"9", "0", "0"}) };
        s.Start(); s.Update(0.016f);
        CHECK_NEAR(o->transform->localPosition.x, 1.0f, 1e-4f);
    }

    TEST_MAIN_RESULT();
}
