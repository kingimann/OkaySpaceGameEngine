// Inventory scripting (PR 7a): the inv_* OkayScript calls, inventory events to scripts,
// visual scripts and C++ callbacks (from any route: calls, UI drags, chest moves,
// pickups, crafting, equipping), and the ActionList give / take / has_item ops.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <okay/Components/Equipment.hpp>
#include <okay/Components/ItemPickup.hpp>
#include <okay/Inventory/InventoryInput.hpp>
#include <cstdio>
#include <fstream>
#include <string>

using namespace okay;

static ActionList::Item I(const std::string& op, std::vector<std::string> a = {}) { return {op, std::move(a)}; }
static float G(ScriptComponent* sc, const char* n) { return sc->VM()->GetGlobal(n).AsFloat(); }
static std::string GS(ScriptComponent* sc, const char* n) { return sc->VM()->GetGlobal(n).AsString(); }

int main() {
    RUN_SUITE("inventory_script");
    ItemDatabase& db = ItemDatabase::Get();
    db.Clear();
    db.LoadText("okayitems 1\n"
                "item \"key\"\n  name \"Gold Key\"\n  stack 10\nend\n"
                "item \"wood\"\n  name \"Wood\"\n  weight 0.5\n  category \"Material\"\n  custom \"value\" \"3\"\nend\n"
                "item \"plank\"\n  name \"Plank\"\nend\n"
                "item \"apple\"\n  name \"Apple\"\n  use \"Eat\" 10\nend\n"
                "item \"helmet\"\n  name \"Helmet\"\n  equip head\nend\n"
                "item \"crate\"\n  name \"Crate\"\n  size 2 2\n  weight 4\nend\n");
    { std::ofstream("test_script_loot.okayloot") << "okayloot 1\nrolls 1 1\nentry \"wood\" 1 2 2 1\n"; }

    // --- A quest script gives 3 keys and checks them ---
    {
        Scene s("T");
        GameObject* hero = s.CreateGameObject("Hero");
        auto* inv = hero->AddComponent<Inventory>(); inv->capacity = 4;
        auto* sc = hero->AddComponent<ScriptComponent>("okayscript");
        std::string err;
        CHECK(sc->LoadSource(
            "var left = -1; var ok = -1; var four = -1; var n = -1;\n"
            "function start() {\n"
            "  left = inv_add(\"key\", 3);\n"
            "  ok = inv_has(\"key\", 3);\n"
            "  four = inv_has(\"Gold Key\", 4);\n"              // display names work too
            "  n = inv_count(\"key\");\n"
            "}\n", &err));
        s.Start();
        CHECK(G(sc, "left") == 0.0f && G(sc, "ok") == 1.0f && G(sc, "four") == 0.0f && G(sc, "n") == 3.0f);
        CHECK(inv->Count("key") == 3);
    }

    // --- The other inv_* calls ---
    {
        Scene s("T");
        GameObject* hero = s.CreateGameObject("Hero");
        auto* inv = hero->AddComponent<Inventory>(); inv->capacity = 4;
        auto* ui = hero->AddComponent<InventoryUI>(); ui->hotbarSlots = 4;
        auto* eq = hero->AddComponent<Equipment>();
        auto* cr = hero->AddComponent<Crafting>();
        cr->recipes.push_back(Recipe({{"wood", 2}}, "plank", 1));
        hero->AddComponent<Consumables>();
        GameObject* chest = s.CreateGameObject("Chest");
        auto* cinv = chest->AddComponent<Inventory>(); cinv->capacity = 2;
        auto* grid = chest->AddComponent<GridInventory>(); grid->cols = 3; grid->rows = 2; grid->weightLimit = 5.0f;
        auto* sc = hero->AddComponent<ScriptComponent>("okayscript");
        std::string err;
        CHECK(sc->LoadSource(
            "var a = 0; var rem_no = 0; var rem_yes = 0; var slot = 0; var s_item = 0; var s_count = 0; var set = 0; var set_bad = 0;\n"
            "var cap = 0; var moved = 0; var given = 0; var chest_wood = 0; var chest_add = 0; var d_name = 0; var d_w = 0; var d_cat = 0;\n"
            "var d_val = 0; var d_eq = 0; var eq_ok = 0; var worn = 0; var uneq = 0; var worn_after = 0; var can = 0; var crafted = 0;\n"
            "var planks = 0; var sel = 0; var dropped = 0; var pk = 0; var rolled = 0; var g_add = 0; var g_fits = 0; var g_w = 0;\n"
            "function start() {\n"
            "  a = inv_add(\"wood\", 5);\n"
            "  rem_no = inv_remove(\"wood\", 9);\n"
            "  rem_yes = inv_remove(\"wood\", 1);\n"
            "  slot = inv_slot(0);\n"
            "  s_item = slot[0]; s_count = slot[1];\n"
            "  set = inv_set_slot(3, \"key\", 2);\n"
            "  set_bad = inv_set_slot(9, \"key\", 2);\n"
            "  cap = inv_capacity();\n"
            "  moved = inv_move(3, 2);\n"
            "  given = inv_transfer(\"Chest\", \"wood\", 3);\n"
            "  chest_wood = inv_count(\"wood\", \"Chest\");\n"
            "  chest_add = inv_add(\"key\", 1, \"Chest\");\n"
            "  d_name = item_def(\"wood\", \"name\"); d_w = item_def(\"wood\", \"weight\");\n"
            "  d_cat = item_def(\"wood\", \"category\"); d_val = item_def(\"wood\", \"value\");\n"
            "  d_eq = item_def(\"helmet\", \"equip\");\n"
            "  inv_add(\"helmet\", 1);\n"
            "  eq_ok = equip(\"Head\", \"helmet\");\n"
            "  worn = equipped(\"Head\");\n"
            "  uneq = unequip(\"Head\");\n"
            "  worn_after = equipped(\"Head\");\n"
            "  inv_add(\"wood\", 2);\n"
            "  can = can_craft(\"plank\");\n"
            "  crafted = craft(\"Plank\");\n"
            "  planks = inv_count(\"plank\");\n"
            "  inv_select(2); sel = inv_selected();\n"
            "  dropped = drop_item(\"plank\", 1);\n"
            "  pk = spawn_pickup(\"apple\", 2, 5, 0, 0);\n"
            "  rolled = loot_roll(\"test_script_loot.okayloot\", \"Chest\");\n"
            "  g_add = grid_add(\"crate\", 1, \"Chest\");\n"
            "  g_fits = grid_fits(\"crate\", \"Chest\");\n"
            "  g_w = grid_weight(\"Chest\");\n"
            "}\n", &err));
        s.Start();
        CHECK(err.empty());
        CHECK(G(sc, "a") == 0.0f && G(sc, "rem_no") == 0.0f && G(sc, "rem_yes") == 1.0f);
        CHECK(GS(sc, "s_item") == "wood" && G(sc, "s_count") == 4.0f);
        CHECK(G(sc, "set") == 1.0f && G(sc, "set_bad") == 0.0f && G(sc, "cap") == 4.0f);
        CHECK(G(sc, "moved") == 1.0f);
        CHECK(G(sc, "given") == 3.0f && G(sc, "chest_wood") == 3.0f);
        CHECK(G(sc, "chest_add") == 0.0f);
        CHECK(GS(sc, "d_name") == "Wood" && G(sc, "d_w") == 0.5f && GS(sc, "d_cat") == "Material" && GS(sc, "d_val") == "3");
        CHECK(GS(sc, "d_eq") == "head");
        CHECK(G(sc, "eq_ok") == 1.0f && GS(sc, "worn") == "helmet" && G(sc, "uneq") == 1.0f && GS(sc, "worn_after").empty());
        CHECK(eq->ItemIn(eq->Find("Head")).empty() && inv->Count("helmet") == 1);
        CHECK(G(sc, "can") == 1.0f && G(sc, "crafted") == 1.0f && G(sc, "planks") == 1.0f);
        CHECK(G(sc, "dropped") == 1.0f && inv->Count("plank") == 0);
        CHECK(!GS(sc, "pk").empty());
        int ground = 0;
        for (auto* p : s.FindObjectsOfType<ItemPickup>()) ground += p->count;
        CHECK(ground == 1 + 2);
        CHECK(G(sc, "rolled") == 2.0f && cinv->Count("wood") == 5);
        CHECK(G(sc, "g_add") == 1.0f && G(sc, "g_fits") == 0.0f && G(sc, "g_w") == 4.0f);
        (void)ui;
    }

    // --- Events reach scripts, visual scripts and C++ hooks, by every route ---
    {
        Scene s("T");
        GameObject* hero = s.CreateGameObject("Hero");
        hero->tag = "Player";
        auto* inv = hero->AddComponent<Inventory>(); inv->capacity = 3;
        auto* ui = hero->AddComponent<InventoryUI>(); ui->hotbarSlots = 3;
        auto* eq = hero->AddComponent<Equipment>();
        hero->AddComponent<Consumables>();
        hero->AddComponent<SurvivalStats>();                         // eating needs something to feed
        auto* cr = hero->AddComponent<Crafting>();
        cr->recipes.push_back(Recipe({{"wood", 2}}, "plank", 1));
        cr->recipes.push_back(Recipe({{"key", 5}}, "apple", 1));
        auto* sc = hero->AddComponent<ScriptComponent>("okayscript");
        std::string err;
        CHECK(sc->LoadSource(
            "var log = \"\";\n"
            "function on_item_added(item, n) { log = log + \"+\" + item + n; }\n"
            "function on_item_removed(item, n) { log = log + \"-\" + item + n; }\n"
            "function OnInventoryFull(item, n) { log = log + \"!\" + item + n; }\n"   // C#-style name works too
            "function on_item_used(item) { log = log + \"u\" + item; }\n"
            "function on_item_picked_up(item, n) { log = log + \"p\" + item + n; }\n"
            "function on_equip(slot, item) { log = log + \"e\" + slot; }\n"
            "function on_craft(r) { log = log + \"c\" + r; }\n"
            "function on_craft_failed(r, why) { log = log + \"x\" + r + \"(\" + why + \")\"; }\n"
            "function on_chest_opened(name) { log = log + \"o\" + name; }\n"
            "function on_chest_closed(name) { log = log + \"/o\"; }\n", &err));
        auto* vc = hero->AddComponent<VisualScriptComponent>();
        CHECK(vc->LoadFromText(
            "node 1 OnItemAdded\nentry OnItemAdded 1\nnode 2 AddVar added\nnode 3 EventValue\nexec 1 0 2\ndata 2 0 3 0\n"
            "node 4 SetVar last\nnode 5 EventName\nexec 2 0 4\ndata 4 0 5 0\n"
            "node 6 OnItemRemoved\nentry OnItemRemoved 6\nnode 7 AddVar removed\ndefault 7 0 1\nexec 6 0 7\n"
            "node 8 OnInventoryFull\nentry OnInventoryFull 8\nnode 9 AddVar full\ndefault 9 0 1\nexec 8 0 9\n"
            "node 10 OnItemUsed\nentry OnItemUsed 10\nnode 11 AddVar used\ndefault 11 0 1\nexec 10 0 11\n"
            "node 12 OnEquip\nentry OnEquip 12\nnode 13 SetVar slot\nnode 14 EventName\nexec 12 0 13\ndata 13 0 14 0\n"
            "node 15 OnCraft\nentry OnCraft 15\nnode 16 AddVar crafts\ndefault 16 0 1\nexec 15 0 16\n", &err));
        int cppAdded = 0, cppRemoved = 0, cppFull = 0;
        inv->onItemAdded = [&](const std::string&, int n) { cppAdded += n; };
        inv->onItemRemoved = [&](const std::string&, int n) { cppRemoved += n; };
        inv->onInventoryFull = [&](const std::string&, int n) { cppFull += n; };
        GameObject* chestObj = s.CreateGameObject("Box");
        auto* chest = chestObj->AddComponent<ChestInventory>(); chest->range = 0.0f;
        auto* cinv = chestObj->AddComponent<Inventory>(); cinv->capacity = 4;
        cinv->Add("key", 4);
        s.Start();

        inv->Add("wood", 4);                                         // a call
        CHECK(GS(sc, "log") == "+wood4" && cppAdded == 4);
        CHECK(vc->GetVariable("added").AsFloat() == 4.0f && vc->GetVariable("last").AsString() == "wood");
        // A UI drag inside the bag moves, it doesn't add or remove.
        ui->open = true;
        InventoryInput::HotbarLayout L = InventoryInput::LayoutHotbar(*ui, 1280, 720);
        float x0, y0, x2, y2; L.SlotXY(0, x0, y0); L.SlotXY(2, x2, y2);
        Input::FeedMouse({x0 + 4, y0 + 4}, 1); InventoryInput::HandleHotbar(*ui, L, Input::MousePosition());
        Input::FeedMouse({x2 + 4, y2 + 4}, 0); InventoryInput::HandleHotbar(*ui, L, Input::MousePosition());
        CHECK(inv->slots.size() == 3 && inv->slots[2].item == "wood" && GS(sc, "log") == "+wood4");
        ui->open = false;
        // A chest move: removed from the chest, added to the player.
        CHECK(InventoryOps::Transfer(cinv, 0, inv).Ok());
        CHECK(GS(sc, "log") == "+wood4+key4" && cinv->Count("key") == 0);
        // Full: an add that doesn't fit.
        inv->Add("apple", 1);
        CHECK(!inv->Add("plank", 1) && cppFull == 1 && vc->GetVariable("full").AsFloat() == 1.0f);
        CHECK(GS(sc, "log") == "+wood4+key4+apple1!plank1");
        // Using: eat the apple.
        int apple = -1;
        for (int i = 0; i < (int)inv->slots.size(); ++i) if (inv->slots[i].item == "apple") apple = i;
        CHECK(InventoryOps::Use(inv, apple, hero).Ok());
        CHECK(GS(sc, "log") == "+wood4+key4+apple1!plank1-apple1uapple" && vc->GetVariable("used").AsFloat() == 1.0f);
        // Crafting, and a craft that can't happen.
        CHECK(cr->Craft("plank") && vc->GetVariable("crafts").AsFloat() == 1.0f);
        CHECK(!cr->Craft("apple"));
        const std::string afterCraft = GS(sc, "log");
        CHECK(afterCraft.find("cplank") != std::string::npos && afterCraft.find("xapple(missing ingredients)") != std::string::npos);
        CHECK(afterCraft.find("-wood2") != std::string::npos && afterCraft.find("+plank1") != std::string::npos);
        // A pickup.
        inv->Clear();
        GameObject* pkObj = ItemPickup::Spawn(s, "key", 2, {}, Vec3{3, 0, 0});
        CHECK(pkObj && pkObj->GetComponent<ItemPickup>()->GiveTo(hero) == 2);
        CHECK(GS(sc, "log").find("pkey2") != std::string::npos);
        // Equipping.
        inv->Add("helmet", 1);
        CHECK(eq->EquipFrom(*inv, 1, eq->Find("Head")).Ok() || eq->EquipFrom(*inv, 0, eq->Find("Head")).Ok());
        CHECK(GS(sc, "log").find("eHead") != std::string::npos && vc->GetVariable("slot").AsString() == "Head");
        // Opening and closing the chest.
        const char key = chest->InteractKey();
        Input::FeedKeys({key}); s.Update(0.016f); Input::FeedKeys({}); s.Update(0.016f);
        CHECK(chest->open && GS(sc, "log").find("oBox") != std::string::npos);
        Input::FeedKeys({key}); s.Update(0.016f); Input::FeedKeys({}); s.Update(0.016f);
        CHECK(!chest->open && GS(sc, "log").find("/o") != std::string::npos);
        CHECK(cppRemoved > 0 && vc->GetVariable("removed").AsFloat() > 0.0f);
    }

    // --- ActionList: give, take, has_item ---
    {
        Scene s("T");
        GameObject* npc = s.CreateGameObject("Npc");
        GameObject* hero = s.CreateGameObject("Hero");
        auto* inv = hero->AddComponent<Inventory>(); inv->capacity = 4;
        auto* al = npc->AddComponent<ActionList>();
        al->instructions = { I("give", {"Gold Key", "3", "Hero"}), I("take", {"key", "5", "Hero"}), I("take", {"key", "1", "Hero"}),
                             I("set_var", {"done", "1"}) };
        auto* gate = npc->AddComponent<ActionList>();
        gate->trigger = ActionList::Trigger::OnUpdate;
        gate->conditions = { I("has_item", {"key", "2", "Hero"}) };
        gate->instructions = { I("set_var", {"gate", "1"}) };
        s.Start(); s.Update(0.016f); s.Update(0.016f);
        CHECK(inv->Count("key") == 2);                       // +3, the take of 5 did nothing, -1
        CHECK(ActionList::GetVar("done") == 1.0f);
        CHECK(ActionList::GetVar("gate") == 1.0f);
    }

    std::remove("test_script_loot.okayloot");
    TEST_MAIN_RESULT();
}
