// Inventory scene wiring: only the nearest chest / bench in range opens, "the player"
// is the tagged player (not a minimap camera), the UI shows capacity slots and never
// writes past them, craft buttons aren't saved (so save/load can't double them), and
// a chest saved while open doesn't reopen on load.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <okay/Components/ChestInventory.hpp>
#include <okay/Components/CraftingMenu.hpp>
#include <okay/Components/CraftingStation.hpp>
#include <okay/Components/InventoryUI.hpp>
#include <string>

using namespace okay;

static void Press(Scene& s, char key) {
    Input::FeedKeys({key}); s.Update(0.016f);
    Input::FeedKeys({});    s.Update(0.016f);
}

static int CountNamed(const Scene& s, const std::string& prefix) {
    int n = 0;
    for (const auto& up : s.Objects()) if (up && up->name.rfind(prefix, 0) == 0) ++n;
    return n;
}

int main() {
    RUN_SUITE("inventory_wiring");

    // --- Two chests + two benches: only the nearest in range opens ---
    {
        Scene s("T");
        GameObject* cam = s.CreateGameObject("MainCamera");
        cam->AddComponent<Camera>();
        GameObject* mini = s.CreateGameObject("Minimap");        // far-away second camera
        auto* mc = mini->AddComponent<Camera>(); mc->main = false;
        mini->transform->localPosition = {100, 0, 0};
        GameObject* player = s.CreateGameObject("Hero");
        player->tag = "Player";
        player->AddComponent<Inventory>()->Add("Wood", 4);
        cam->transform->localPosition = {50, 0, 0};             // camera far from the player
        auto chest = [&](const char* name, float x) {
            GameObject* g = s.CreateGameObject(name);
            g->transform->localPosition = {x, 0, 0};
            g->AddComponent<Inventory>()->Add("Gold", 1);
            return g->AddComponent<ChestInventory>();
        };
        ChestInventory* nearC = chest("NearChest", 1.0f);
        ChestInventory* farC  = chest("FarChest", 2.0f);
        ChestInventory* outC  = chest("OutChest", 10.0f);
        auto bench = [&](const char* name, float x) {
            GameObject* g = s.CreateGameObject(name);
            g->transform->localPosition = {x, 0, 0};
            auto* cs = g->AddComponent<CraftingStation>();
            cs->recipes.push_back({{{"Wood", 2}}, "Plank", 4});
            return cs;
        };
        CraftingStation* nearB = bench("NearBench", -1.5f);
        CraftingStation* farB  = bench("FarBench", -2.2f);
        s.Start();
        CHECK(nearC->range > 0.0f && nearB->range > 0.0f);         // new components have a range
        CHECK(FindInventoryPlayer(&s) == player);

        Press(s, 'f');
        CHECK(nearC->open && !farC->open && !outC->open);
        CHECK(nearC->PlayerInv() == player->GetComponent<Inventory>());
        Press(s, 'f');                                              // toggles it shut
        CHECK(!nearC->open && !farC->open);

        player->transform->localPosition = {2.2f, 0, 0};           // now FarChest is nearest
        Press(s, 'f');
        CHECK(farC->open && !nearC->open);
        player->transform->localPosition = {6.0f, 0, 0};           // walk away: auto-close
        s.Update(0.016f);
        CHECK(!farC->open);

        player->transform->localPosition = {0, 0, 0};
        Press(s, 'c');
        CHECK(nearB->open && !farB->open);
        CHECK(nearB->Inv() == player->GetComponent<Inventory>()); // bench crafts from the player's bag
        CHECK(nearB->Craft(0) && player->GetComponent<Inventory>()->Count("Plank") == 4);

        // Saved while open: the chest comes back closed.
        nearC->open = true;
        Scene r("R");
        std::string err;
        CHECK(SceneSerializer::Deserialize(r, SceneSerializer::Serialize(s), &err));
        auto* rc = r.Find("NearChest") ? r.Find("NearChest")->GetComponent<ChestInventory>() : nullptr;
        auto* rb = r.Find("NearBench") ? r.Find("NearBench")->GetComponent<CraftingStation>() : nullptr;
        CHECK(rc && !rc->open && rc->range == nearC->range);
        CHECK(rb && !rb->open && rb->range == nearB->range);
    }

    // --- Older scenes: a bench has no range (as before) and a saved-open chest stays shut ---
    {
        const char* kOld =
            "okayscene 1\n"
            "gameobject 0 \"Box\"\n  transform 0 0 0 0 0 0 1 1 1 1\n"
            "  chestinventory 1 102 0 \"Chest\" 6 46 4 4 \"textures/items/\" 1"
            " 0 0 0 1 0 0 0 1 0 0 0 1 0 0 0 1 0 0 0 1 0 0 0 1\nend\n"
            "gameobject 1 \"Bench\"\n  transform 0 0 0 0 0 0 1 1 1 1\n"
            "  craftingstation 1 99 \"Crafting\" 0 26 240 0 0 4 \"textures/items/\""
            " 0 0 0 1 0 0 0 1 0 0 0 1 0 0 0 1 0 0 0 1 0 0 0 1 0 0 0 1 0\nend\n";
        Scene s("T");
        std::string err;
        CHECK(SceneSerializer::Deserialize(s, kOld, &err));
        auto* ch = s.Find("Box") ? s.Find("Box")->GetComponent<ChestInventory>() : nullptr;
        auto* cs = s.Find("Bench") ? s.Find("Bench")->GetComponent<CraftingStation>() : nullptr;
        CHECK(ch && !ch->open && ch->range == 0.0f);
        CHECK(cs && !cs->open && cs->range == 0.0f);
    }

    // --- Slots shown = capacity; mutators never go past it ---
    {
        Scene s("T");
        GameObject* p = s.CreateGameObject("P");
        auto* inv = p->AddComponent<Inventory>();
        inv->capacity = 20;
        auto* ui = p->AddComponent<InventoryUI>();
        CHECK(ui->SlotCount() == 20);                                // not 9 + 3*9 = 36
        CHECK(ui->BackpackRowCount() == 2);                          // 11 backpack slots → 2 rows
        inv->Add("Stone", 5);
        ui->MoveSlot(0, 30);                                         // a slot that isn't shown
        CHECK((int)inv->slots.size() <= 20 && inv->Count("Stone") == 5 && inv->slots[0].item == "Stone");
        ui->dragAmount = 2;
        ui->ApplyDrag(0, 25);
        CHECK((int)inv->slots.size() <= 20 && inv->slots[0].count == 5);
        ui->MoveSlot(0, 19);                                         // last real slot still works
        CHECK(inv->slots.size() == 20 && inv->slots[19].item == "Stone");
        inv->capacity = 40;                                          // bigger than the default 36
        CHECK(ui->SlotCount() == 40);                                // all of it visible
        inv->capacity = 0;
        CHECK(ui->SlotCount() == 36);                                // no capacity: old layout

        // A UI under a Canvas pointed at the player's Inventory.
        GameObject* hero = s.CreateGameObject("Hero");
        auto* heroInv = hero->AddComponent<Inventory>();
        GameObject* hud = s.CreateGameObject("HUD");
        auto* hu = hud->AddComponent<InventoryUI>();
        hu->inventoryTarget = "Hero";
        CHECK(hu->Inv() == heroInv);
    }

    // --- Craft buttons: one set, not saved, rebuilt on change, removed with the menu ---
    {
        Scene s("T");
        GameObject* p = s.CreateGameObject("Player");
        p->AddComponent<Inventory>()->Add("Wood", 10);
        auto* craft = p->AddComponent<Crafting>();
        craft->recipes.push_back({"Plank", 4, {{"Wood", 2}}});
        craft->recipes.push_back({"Stick", 2, {{"Wood", 1}}});
        auto* menu = p->AddComponent<CraftingMenu>();
        s.Start();
        s.Update(0.016f);
        CHECK(CountNamed(s, "CraftBtn:") == 2);
        const std::string text = SceneSerializer::Serialize(s);
        CHECK(text.find("CraftBtn:") == std::string::npos);
        Scene r("R");
        std::string err;
        CHECK(SceneSerializer::Deserialize(r, text, &err));
        r.Start(); r.Update(0.016f);
        CHECK(CountNamed(r, "CraftBtn:") == 2);                      // not 4
        // Clicking a button crafts that recipe on this menu's Crafting.
        if (menu->Buttons().size() == 2 && menu->Buttons()[1])
            if (auto* b = menu->Buttons()[1]->GetComponent<UIButton>()) { b->Press(); s.Update(0.016f); }
        CHECK(p->GetComponent<Inventory>()->Count("Stick") == 2);
        craft->recipes.push_back({"Torch", 1, {{"Wood", 1}}});
        s.Update(0.016f); s.Update(0.016f);
        CHECK(CountNamed(s, "CraftBtn:") == 3);
        p->RemoveComponent(menu);
        s.Update(0.016f);
        CHECK(CountNamed(s, "CraftBtn:") == 0);
    }

    TEST_MAIN_RESULT();
}
