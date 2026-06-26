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

    // New variable arithmetic ops: set / mul / div / copy.
    {
        ActionList::ResetVars();
        Scene s("V"); s.physicsEnabled = false;
        GameObject* o = s.CreateGameObject("Math");
        auto* al = o->AddComponent<ActionList>();
        al->trigger = ActionList::Trigger::OnStart;
        al->instructions = {
            I("set_var", {"x", "10"}),
            I("mul_var", {"x", "3"}),     // 30
            I("div_var", {"x", "2"}),     // 15
            I("copy_var", {"y", "x"}),    // y = 15
        };
        s.Start(); s.Update(0.016f);
        CHECK_NEAR(ActionList::Vars()["x"], 15.0f, 1e-4f);
        CHECK_NEAR(ActionList::Vars()["y"], 15.0f, 1e-4f);
    }

    // var_neq condition gates a list, and prefs ops persist a value.
    {
        ActionList::ResetVars();
        Prefs::SetFloat("score", 0.0f);
        Scene s("P"); s.physicsEnabled = false;
        GameObject* o = s.CreateGameObject("Scorer");
        auto* al = o->AddComponent<ActionList>();
        al->trigger = ActionList::Trigger::OnStart;
        al->conditions   = { I("var_neq", {"done", "1"}) };  // done==0 -> passes
        al->instructions = { I("add_prefs", {"score", "5"}), I("set_var", {"done", "1"}) };
        s.Start(); s.Update(0.016f);
        CHECK_NEAR(Prefs::GetFloat("score", 0.0f), 5.0f, 1e-4f);
        CHECK_NEAR(ActionList::Vars()["done"], 1.0f, 1e-4f);
    }

    // Survival kit driven from visual scripting (ActionList instructions).
    {
        ActionList::ResetVars();
        Scene s("surv"); s.physicsEnabled = false;
        GameObject* p = s.CreateGameObject("Player");
        auto* sv = p->AddComponent<SurvivalStats>(); sv->regenWhenFed = 0.0f;
        auto* al = p->AddComponent<ActionList>();
        al->trigger = ActionList::Trigger::OnStart;
        al->instructions = { I("hurt", {"30"}), I("heal", {"10"}),
                             I("eat", {"5"}), I("survival", {"Drink", "7"}) };
        s.Start(); s.Update(0.016f);
        CHECK_NEAR(sv->health, 80.0f, 1e-3f);          // -30 +10
        CHECK_NEAR(sv->hunger, 100.0f, 1e-3f);         // eat clamps at max (started full)
        CHECK(sv->thirst <= 100.0f);
    }

    // survival_on targets a named object; prefs_lt branches on a published stat.
    {
        ActionList::ResetVars();
        Scene s("surv2"); s.physicsEnabled = false;
        GameObject* player = s.CreateGameObject("Player");
        auto* hp = player->AddComponent<HealthStat>(); hp->regenPerSecond = 0.0f;
        GameObject* trap = s.CreateGameObject("Trap");
        auto* al = trap->AddComponent<ActionList>();
        al->trigger = ActionList::Trigger::OnStart;
        al->instructions = { I("survival_on", {"Player", "Damage", "40"}) };
        s.Start(); s.Update(0.016f);
        CHECK_NEAR(hp->health, 60.0f, 1e-3f);          // trap damaged the player
        // HealthStat publishes "health" to prefs; a prefs_lt condition can branch on it.
        GameObject* g = s.CreateGameObject("Gate");
        auto* ga = g->AddComponent<ActionList>();
        ga->trigger = ActionList::Trigger::OnStart;
        ga->conditions = { I("prefs_lt", {"health", "70"}) };   // 60 < 70 -> passes
        ga->instructions = { I("set_var", {"lowhp", "1"}) };
        s.Update(0.016f);
        CHECK_NEAR(ActionList::Vars()["lowhp"], 1.0f, 1e-3f);
    }

    // New variable instructions: clamp, ease (lerp), add-var-to-var; ge/le conditions.
    {
        ActionList::ResetVars();
        Scene s("vars"); s.physicsEnabled = false;
        GameObject* o = s.CreateGameObject("V");
        auto* al = o->AddComponent<ActionList>();
        al->trigger = ActionList::Trigger::OnStart;
        ActionList::Vars()["a"] = 50.0f; ActionList::Vars()["b"] = 5.0f;
        al->instructions = { I("clamp_var", {"a", "0", "20"}),       // 50 -> 20
                             I("add_var_var", {"a", "b"}),           // 20 + 5 -> 25
                             I("lerp_var", {"c", "10", "4"}) };      // 0 -> 4 toward 10
        s.Start(); s.Update(0.016f);
        CHECK_NEAR(ActionList::Vars()["a"], 25.0f, 1e-3f);
        CHECK_NEAR(ActionList::Vars()["c"], 4.0f, 1e-3f);

        // var_ge / var_le conditions
        GameObject* g = s.CreateGameObject("G");
        auto* ga = g->AddComponent<ActionList>();
        ga->trigger = ActionList::Trigger::OnStart;
        ga->conditions = { I("var_ge", {"a", "25"}), I("var_le", {"a", "25"}) };  // 25>=25 && 25<=25
        ga->instructions = { I("set_var", {"hit", "1"}) };
        s.Update(0.016f);
        CHECK_NEAR(ActionList::Vars()["hit"], 1.0f, 1e-3f);
    }

    // set_parent attaches under a named object; toggle_active flips active.
    {
        ActionList::ResetVars();
        Scene s("obj"); s.physicsEnabled = false;
        GameObject* parent = s.CreateGameObject("Parent");
        parent->transform->localPosition = {10, 0, 0};
        GameObject* child = s.CreateGameObject("Child");
        auto* al = child->AddComponent<ActionList>();
        al->trigger = ActionList::Trigger::OnStart;
        al->instructions = { I("set_parent", {"Parent"}) };
        GameObject* mover = s.CreateGameObject("Mover");
        auto* ma = mover->AddComponent<ActionList>();
        ma->trigger = ActionList::Trigger::OnStart;
        ma->instructions = { I("toggle_active", {}) };
        s.Start(); s.Update(0.016f);
        CHECK(child->transform->Parent() == parent->transform);     // parented
        CHECK(!mover->active);                                       // toggled off
    }

    // set_active with a target name shows/hides a chosen object (not just self).
    {
        ActionList::ResetVars();
        Scene s("showhide"); s.physicsEnabled = false;
        GameObject* door = s.CreateGameObject("Door");
        GameObject* lever = s.CreateGameObject("Lever");
        auto* al = lever->AddComponent<ActionList>();
        al->trigger = ActionList::Trigger::OnStart;
        al->instructions = { I("set_active", {"0", "Door"}) };       // hide the Door, not the Lever
        s.Start(); s.Update(0.016f);
        CHECK(!door->active);
        CHECK(lever->active);                                        // the lever stays visible
    }

    TEST_MAIN_RESULT();
}
