// Inventory API for typed scripts (PR 7b): Inventory, Equipment, Crafting, GridInventory
// and Items in a `class : Behaviour` script, and the visual-script Engine API nodes
// generated from the same bindings (Inventory.Add, Inventory.Count, ...).
#include "test_framework.hpp"
#include <Okay.hpp>
#include <okay/Components/Equipment.hpp>
#include <okay/Components/ItemPickup.hpp>
#include <okay/Scripting/NextScriptVM.hpp>
#include <okay/VisualScript/VsApi.hpp>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

using namespace okay;

static float G(ScriptComponent* sc, const char* n) { return sc->VM()->GetGlobal(n).AsFloat(); }
static std::string GS(ScriptComponent* sc, const char* n) { return sc->VM()->GetGlobal(n).AsString(); }

int main() {
    RUN_SUITE("inventory_script_api");
    ItemDatabase& db = ItemDatabase::Get();
    db.Clear();
    db.LoadText("okayitems 1\n"
                "item \"key\"\n  name \"Gold Key\"\nend\n"
                "item \"wood\"\n  name \"Wood\"\n  weight 0.5\n  category \"Material\"\nend\n"
                "item \"plank\"\n  name \"Plank\"\nend\n"
                "item \"helmet\"\n  name \"Helmet\"\n  equip head\n  custom \"armor\" \"2\"\nend\n"
                "item \"crate\"\n  name \"Crate\"\n  size 2 2\nend\n");
    { std::ofstream("test_api_loot.okayloot") << "okayloot 1\nrolls 1 1\nentry \"wood\" 1 3 3 1\n"; }

    // --- A typed quest script gives 3 keys and checks them ---
    {
        Scene s("T");
        GameObject* hero = s.CreateGameObject("Hero");
        auto* inv = hero->AddComponent<Inventory>(); inv->capacity = 6;
        hero->AddComponent<Equipment>();
        auto* cr = hero->AddComponent<Crafting>();
        cr->recipes.push_back(Recipe({{"wood", 2}}, "plank", 1));
        GameObject* chest = s.CreateGameObject("Chest");
        auto* cinv = chest->AddComponent<Inventory>(); cinv->capacity = 4;
        auto* grid = chest->AddComponent<GridInventory>(); grid->cols = 2; grid->rows = 2;
        auto* sc = hero->AddComponent<ScriptComponent>(NextScriptVM::kLanguage);
        std::string err;
        CHECK(sc->LoadSource(
            "class Quest : Behaviour {\n"
            "    public int left; public bool ok; public bool four; public int keys;\n"
            "    public bool removedNo; public string slot0; public int slot0n; public bool set;\n"
            "    public int moved; public string name; public string cat; public bool equipped; public string worn;\n"
            "    public float armor; public bool crafted; public int planks; public int dropped; public int rolled;\n"
            "    public bool gridAdd; public bool gridFits; public string spawned; public int cap; public int total;\n"
            "    void Start() {\n"
            "        Inventory inv = GetComponent<Inventory>();\n"
            "        left = inv.Add(\"key\", 3);\n"
            "        ok = inv.Has(\"key\", 3);\n"
            "        four = inv.Has(\"Gold Key\", 4);\n"
            "        keys = inv.Count(\"key\");\n"
            "        removedNo = inv.Remove(\"key\", 9);\n"
            "        slot0 = inv.SlotItem(0); slot0n = inv.SlotCount(0);\n"
            "        set = inv.SetSlot(3, \"wood\", 4);\n"
            "        GameObject chest = GameObject.Find(\"Chest\");\n"
            "        moved = inv.TransferTo(chest, \"key\", 2);\n"
            "        name = Items.Name(\"wood\"); cat = Items.Field(\"wood\", \"category\");\n"
            "        inv.Add(\"helmet\", 1);\n"
            "        Equipment eq = GetComponent<Equipment>();\n"
            "        equipped = eq.Equip(\"Head\", \"helmet\"); worn = eq.Equipped(\"Head\"); armor = eq.armor;\n"
            "        Crafting cr = GetComponent<Crafting>();\n"
            "        crafted = cr.Craft(\"plank\", 1); planks = inv.Count(\"plank\");\n"
            "        dropped = inv.Drop(\"plank\", 1);\n"
            "        rolled = Items.RollLoot(\"test_api_loot.okayloot\", chest);\n"
            "        GridInventory grid = chest.GetComponent<GridInventory>();\n"
            "        gridAdd = grid.Add(\"crate\", 1); gridFits = grid.Fits(\"crate\");\n"
            "        GameObject pk = Items.SpawnPickup(\"key\", 1, new Vector3(4, 0, 0));\n"
            "        spawned = pk.name;\n"
            "        cap = inv.capacity; total = chest.GetComponent<Inventory>().Count(\"wood\");\n"
            "    }\n"
            "}\n", &err));
        if (!err.empty()) std::cerr << "  script error: " << err << "\n";
        s.Start();
        CHECK(G(sc, "left") == 0.0f && sc->VM()->GetGlobal("ok").AsBool() && !sc->VM()->GetGlobal("four").AsBool());
        CHECK(G(sc, "keys") == 3.0f && !sc->VM()->GetGlobal("removedNo").AsBool());
        CHECK(GS(sc, "slot0") == "key" && G(sc, "slot0n") == 3.0f && sc->VM()->GetGlobal("set").AsBool());
        CHECK(G(sc, "moved") == 2.0f && cinv->Count("key") == 2 && inv->Count("key") == 1);
        CHECK(GS(sc, "name") == "Wood" && GS(sc, "cat") == "Material");
        CHECK(sc->VM()->GetGlobal("equipped").AsBool() && GS(sc, "worn") == "helmet" && G(sc, "armor") == 2.0f);
        CHECK(sc->VM()->GetGlobal("crafted").AsBool() && G(sc, "planks") == 1.0f && G(sc, "dropped") == 1.0f);
        CHECK(G(sc, "rolled") == 3.0f && G(sc, "total") == 3.0f);
        CHECK(sc->VM()->GetGlobal("gridAdd").AsBool() && !sc->VM()->GetGlobal("gridFits").AsBool());
        CHECK(!GS(sc, "spawned").empty() && G(sc, "cap") == 6.0f);
        (void)grid;
    }

    // --- A wrong use is a clear error, not a crash ---
    {
        Scene s("T");
        GameObject* hero = s.CreateGameObject("Hero");
        auto* sc = hero->AddComponent<ScriptComponent>(NextScriptVM::kLanguage);
        std::string err;
        CHECK(sc->LoadSource("class Bad : Behaviour {\n    public int n = -1;\n"
                             "    void Start() { n = GetComponent<Inventory>().Count(\"key\"); }\n}\n", &err));
        s.Start();
        CHECK(G(sc, "n") == -1.0f);                                         // stopped at the missing Inventory
    }

    // --- Visual-script Engine API nodes: Add Item, then Count into a variable ---
    {
        Scene s("T");
        GameObject* hero = s.CreateGameObject("Hero");
        auto* inv = hero->AddComponent<Inventory>(); inv->capacity = 4;
        auto* vc = hero->AddComponent<VisualScriptComponent>();
        std::string err;
        CHECK(vc->LoadFromText(
            "node 0 OnStart\n"
            "node 1 Inventory.Add/2\n"
            "default 1 0 \"key\"\n"
            "default 1 1 3\n"
            "node 2 SetVar keys\n"
            "node 3 Inventory.Count/1\n"
            "default 3 0 \"key\"\n"
            "data 2 0 3 0\n"
            "exec 0 0 1\n"
            "exec 1 0 2\n"
            "entry OnStart 0\n", &err));
        if (!err.empty()) std::cerr << "  graph error: " << err << "\n";
        s.Start(); s.Update(0.016f);
        CHECK(inv->Count("key") == 3 && vc->GetVariable("keys").AsFloat() == 3.0f);
        const vs::VsApiEntry* add = vs::VsApiFind("Inventory.Add/2");
        const vs::VsApiEntry* count = vs::VsApiFind("Inventory.Count/1");
        CHECK(add && add->exec && count && !count->exec);                   // Add is a step, Count a value
    }

    std::remove("test_api_loot.okayloot");
    TEST_MAIN_RESULT();
}
