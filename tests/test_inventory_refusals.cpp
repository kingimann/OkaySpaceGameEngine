// A refused use is never silent. InventoryOps::Use writes one log line for the developer and fires the script event
// on_item_use_refused(item, reason, message) on the inventory's object, so a game can show the same sentence to the
// player. Covers the three reasons a player can act on: a cooldown, a broken item, and an item nothing uses; an empty
// slot says nothing. (Written for the item cooldown and durability features, which added the "cooldown" and "broken"
// refusals; before this the only trace of a refusal was the return value.)
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/Consumables.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Inventory/Durability.hpp"
#include "okay/Inventory/InventoryOps.hpp"
#include "okay/Inventory/ItemDatabase.hpp"
#include "okay/Scripting/OkayScriptVM.hpp"
#include <cstdio>
#include <string>
#include <vector>

using namespace okay;

int main() {
    RUN_SUITE("inventory_refusals");
    Time::ResetForTesting();
    InventoryOps::ResetCooldowns();
    ItemDatabase::Get().Clear();
    {   ItemDefinition d; d.id = "potion"; d.name = "Healing Potion"; d.useAction = "Heal"; d.useAmount = 5.0f; d.custom = {{"cooldown", "2"}}; ItemDatabase::Get().Put(d); }
    {   ItemDefinition d; d.id = "sword"; d.name = "Iron Sword"; d.custom = {{"maxDurability", "3"}}; ItemDatabase::Get().Put(d); }
    {   ItemDefinition d; d.id = "plank"; d.name = "Plank"; ItemDatabase::Get().Put(d); }

    std::vector<std::string> lines;   // what the developer sees in the Console
    const Log::Sink oldSink = Log::sink;
    Log::sink = [&lines](Log::Level, const std::string& m) { lines.push_back(m); };
    auto inventoryLines = [&lines]() {
        int n = 0;
        for (const std::string& l : lines) if (l.rfind("inventory: ", 0) == 0) ++n;
        return n;
    };

    Scene s("refusals");
    GameObject* go = s.CreateGameObject("Hero");
    auto* inv = go->AddComponent<Inventory>();
    inv->slots.assign(5, Inventory::Slot{});
    inv->slots[0] = {"potion", 3, {}};
    inv->slots[1] = {"sword", 1, {}};
    inv->slots[2] = {"plank", 2, {}};
    auto* hp = go->AddComponent<HealthStat>();
    hp->publishPrefs = false;
    go->AddComponent<Consumables>();
    auto* sc = go->AddComponent<ScriptComponent>("okayscript");
    CHECK(sc->LoadSource("var n = 0; var item = \"\"; var reason = \"\"; var message = \"\";\n"
                         "function on_item_use_refused(i, r, m) { n = n + 1; item = i; reason = r; message = m; }\n"));
    s.Start();
    auto G = [&](const char* name) { return sc->VM()->GetGlobal(name); };

    // A use that works says nothing.
    CHECK(InventoryOps::Use(inv, 0, go).Ok());
    CHECK(G("n").AsFloat() == 0.0f);
    CHECK(inventoryLines() == 0);

    // The same potion again at once: refused, with the reason and a sentence that says how long to wait.
    const InvResult again = InventoryOps::Use(inv, 0, go);
    CHECK(!again.Ok() && again.reason == "cooldown");
    CHECK(G("n").AsFloat() == 1.0f);
    CHECK(G("item").AsString() == "potion");
    CHECK(G("reason").AsString() == "cooldown");
    CHECK(G("message").AsString() == "Healing Potion is not ready: wait 2.0 s.");
    CHECK(inventoryLines() == 1);
    CHECK(!lines.empty() && lines.back() == "inventory: Healing Potion is not ready: wait 2.0 s.");
    CHECK(InventoryOps::UseRefusalText(inv, "potion", "cooldown") == "Healing Potion is not ready: wait 2.0 s.");

    // Asking again at once: the script is told every time, the log line is not repeated within a second (a held key or a
    // networked client must not flood the log).
    CHECK(!InventoryOps::Use(inv, 0, go).Ok());
    CHECK(G("n").AsFloat() == 2.0f);
    CHECK(inventoryLines() == 1);
    Time::Step(0.5f);
    CHECK(InventoryOps::UseRefusalText(inv, "potion", "cooldown") == "Healing Potion is not ready: wait 1.5 s.");   // the live number
    CHECK(!InventoryOps::Use(inv, 0, go).Ok());
    CHECK(G("n").AsFloat() == 3.0f);
    CHECK(inventoryLines() == 1);
    Time::Step(0.6f);                                                     // a second after the first line: it speaks again
    CHECK(!InventoryOps::Use(inv, 0, go).Ok());
    CHECK(G("n").AsFloat() == 4.0f);
    CHECK(G("message").AsString() == "Healing Potion is not ready: wait 0.9 s.");
    CHECK(inventoryLines() == 2);

    // An item nothing uses.
    const InvResult plank = InventoryOps::Use(inv, 2, go);
    CHECK(!plank.Ok() && plank.reason == "nothing uses it");
    CHECK(G("n").AsFloat() == 5.0f);
    CHECK(G("message").AsString() == "Plank can't be used (nothing uses it).");
    CHECK(inventoryLines() == 3);

    // An empty slot: refused, but nobody is told (pressing the Use key on nothing is not an event).
    const InvResult empty = InventoryOps::Use(inv, 3, go);
    CHECK(!empty.Ok() && empty.reason == "nothing to use");
    CHECK(G("n").AsFloat() == 5.0f);
    CHECK(inventoryLines() == 3);

    // A broken item.
    CHECK(durability::Wear(inv, 1, 3.0f).Ok());
    CHECK(durability::Broken("sword", inv->slots[1].data));
    const InvResult broken = InventoryOps::Use(inv, 1, go);
    CHECK(!broken.Ok() && broken.reason == "broken");
    CHECK(G("n").AsFloat() == 6.0f);
    CHECK(G("item").AsString() == "sword");
    CHECK(G("message").AsString() == "Iron Sword is broken: repair it first.");
    CHECK(inventoryLines() == 4);

    // Once the cooldown has passed the potion works again, and still says nothing.
    Time::Step(2.0f);
    CHECK(InventoryOps::Use(inv, 0, go).Ok());
    CHECK(G("n").AsFloat() == 6.0f);
    CHECK(inventoryLines() == 4);
    std::printf("  %d log lines from refused uses, last: %s\n", inventoryLines(), lines.empty() ? "" : lines.back().c_str());

    // A bag with no object (no one to tell) still refuses and still logs; nothing crashes.
    {
        Inventory loose;
        loose.slots.assign(2, Inventory::Slot{});
        loose.slots[0] = {"plank", 1, {}};
        const InvResult r = InventoryOps::Use(&loose, 0, go);
        CHECK(!r.Ok());
    }

    Log::sink = oldSink;
    InventoryOps::ResetCooldowns();
    ItemDatabase::Get().Clear();
    TEST_MAIN_RESULT();
}
