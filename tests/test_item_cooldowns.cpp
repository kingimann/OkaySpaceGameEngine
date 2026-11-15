// Item cooldowns: an item definition's custom "cooldown" (seconds of game time) makes InventoryOps::Use refuse the
// same item from the same inventory until the time has passed (reason "cooldown"). Other bags keep their own timers,
// the clock is the game clock (Time::Step), ResetCooldowns clears them, and scripts read inv_cooldown(item).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/Consumables.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Inventory/InventoryOps.hpp"
#include "okay/Inventory/ItemDatabase.hpp"
#include "okay/Scripting/OkayScriptVM.hpp"
#include <cstdio>

using namespace okay;

namespace {
struct Drinker {
    GameObject* go = nullptr;
    Inventory* inv = nullptr;
    HealthStat* hp = nullptr;
    Drinker(Scene& s, const char* name) {
        go = s.CreateGameObject(name);
        inv = go->AddComponent<Inventory>();
        inv->slots.assign(4, Inventory::Slot{});
        inv->slots[0] = {"potion", 5, {}};
        inv->slots[1] = {"apple", 5, {}};
        hp = go->AddComponent<HealthStat>();
        hp->publishPrefs = false;
        go->AddComponent<Consumables>();
    }
};
} // namespace

int main() {
    RUN_SUITE("item_cooldowns");
    Time::ResetForTesting();
    InventoryOps::ResetCooldowns();
    ItemDatabase::Get().Clear();
    { ItemDefinition d; d.id = d.name = "potion"; d.useAction = "Heal"; d.useAmount = 5.0f; d.custom = {{"cooldown", "2"}}; ItemDatabase::Get().Put(d); }
    { ItemDefinition d; d.id = d.name = "apple"; d.useAction = "Heal"; d.useAmount = 1.0f; ItemDatabase::Get().Put(d); }

    Scene s("cool");
    Drinker a(s, "A"), b(s, "B");
    s.Start();
    a.hp->health = 50.0f; b.hp->health = 50.0f;

    const InvResult first = InventoryOps::Use(a.inv, 0, a.go);
    CHECK(first.Ok());
    CHECK(a.inv->Count("potion") == 4);
    CHECK_NEAR(InventoryOps::CooldownLeft(a.inv, "potion"), 2.0f, 1e-4f);
    const InvResult again = InventoryOps::Use(a.inv, 0, a.go);
    CHECK(!again.Ok() && again.reason == "cooldown");
    CHECK(a.inv->Count("potion") == 4);                                   // nothing taken
    CHECK(InventoryOps::Use(a.inv, 1, a.go).Ok());                       // an item without a cooldown: any time
    CHECK(InventoryOps::Use(a.inv, 1, a.go).Ok());
    CHECK(InventoryOps::Use(b.inv, 0, b.go).Ok());                       // another bag has its own timer

    Time::Step(1.0f);
    CHECK_NEAR(InventoryOps::CooldownLeft(a.inv, "potion"), 1.0f, 1e-3f);
    CHECK(InventoryOps::Use(a.inv, 0, a.go).reason == "cooldown");
    Time::SetTimeScale(0.0f);                                             // paused: the cooldown does not run
    Time::Step(5.0f);
    CHECK(InventoryOps::Use(a.inv, 0, a.go).reason == "cooldown");
    Time::SetTimeScale(1.0f);
    Time::Step(1.05f);
    CHECK(InventoryOps::CooldownLeft(a.inv, "potion") == 0.0f);
    CHECK(InventoryOps::Use(a.inv, 0, a.go).Ok());
    CHECK(a.inv->Count("potion") == 3);

    InventoryOps::ResetCooldowns();
    CHECK(InventoryOps::CooldownLeft(a.inv, "potion") == 0.0f);
    CHECK(InventoryOps::Use(a.inv, 0, a.go).Ok());                       // cleared: usable at once

    {   // scripts: inv_cooldown(item, [bag])
        auto* sc = a.go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("var mine = -1; var other = -1; var none = -1;\n"
                             "function start() { mine = inv_cooldown(\"potion\"); other = inv_cooldown(\"potion\", \"B\"); none = inv_cooldown(\"apple\"); }\n"));
        sc->Start();
        const float mine = sc->VM()->GetGlobal("mine").AsFloat(), other = sc->VM()->GetGlobal("other").AsFloat();
        std::printf("  inv_cooldown: mine %.2f, B's %.2f\n", mine, other);
        CHECK_NEAR(mine, 2.0f, 1e-3f);
        CHECK(other == 0.0f);                                             // B's timer was cleared by ResetCooldowns
        CHECK(sc->VM()->GetGlobal("none").AsFloat() == 0.0f);
    }
    InventoryOps::ResetCooldowns();
    ItemDatabase::Get().Clear();
    TEST_MAIN_RESULT();
}
