// Item definitions: the .okayitems database (load / save / legacy names / loose items),
// definition-driven stack size, icon, rarity, grid size and use action, per-instance
// item data (durability / uid / values) that stops stacking and survives save/load,
// and old scenes loading unchanged.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <okay/Inventory/InventoryOps.hpp>
#include <okay/Inventory/ItemDatabase.hpp>
#include <string>

using namespace okay;

static const char* kItems =
    "okayitems 1\n"
    "item \"wood\"\n"
    "  name \"Wood\"\n"
    "  description \"From trees.\"\n"
    "  icon \"icons/wood.png\"\n"
    "  stack 64\n"
    "  weight 0.5\n"
    "  category \"Material\"\n"
    "  tags \"fuel\" \"building\"\n"
    "  legacy \"Log\"\n"
    "end\n"
    "item \"apple\"\n"
    "  name \"Apple\"\n"
    "  stack 16\n"
    "  use \"Eat\" 20\n"
    "  rarity 1 0.2 0.8 0.3 1\n"
    "end\n"
    "item \"rifle\"\n"
    "  name \"Rifle\"\n"
    "  size 2 5\n"
    "  weight 4\n"
    "  equip mainhand\n"
    "  custom \"damage\" \"35\"\n"
    "  someFutureKey 1 2 3\n"
    "end\n";

int main() {
    RUN_SUITE("item_database");
    ItemDatabase& db = ItemDatabase::Get();

    // --- Load / fields / save round trip ---
    {
        db.Clear();
        std::string err;
        CHECK(db.LoadText(kItems, &err));
        CHECK(db.Items().size() == 3);
        const ItemDefinition* w = db.Find("wood");
        CHECK(w && w->DisplayName() == "Wood" && w->maxStack == 64 && w->weight == 0.5f && w->HasTag("fuel"));
        const ItemDefinition* r = db.Find("rifle");
        CHECK(r && r->gridW == 2 && r->gridH == 5 && r->equip == EquipSlot::MainHand && r->Custom("damage") == "35");
        const ItemDefinition* a = db.Find("apple");
        CHECK(a && a->useAction == "Eat" && a->useAmount == 20.0f && a->hasRarityColor && a->rarity == 1);
        CHECK(db.DuplicateIds().empty());
        const std::string text = db.Serialize();
        ItemDatabase copy;
        CHECK(copy.LoadText(text, &err) && copy.Serialize() == text);
        CHECK(!db.LoadText("not items", &err) && !err.empty());
        CHECK(db.Items().size() == 3);                                 // a bad file leaves it as it was
    }

    // --- Names → ids: id as-is, legacy name, display name, unknown = loose ---
    {
        CHECK(db.Resolve("wood") == "wood");
        CHECK(db.Resolve("Log") == "wood");
        CHECK(db.Resolve("Apple") == "apple");
        CHECK(db.Resolve("Mystery") == "Mystery");
        const ItemDefinition* m = db.Find("Mystery");
        CHECK(m && m->loose);
        CHECK(db.Serialize().find("Mystery") == std::string::npos);  // loose items aren't saved
        const ItemDefinition* before = db.Find("wood");
        db.Resolve("Another"); db.Resolve("YetAnother");
        CHECK(db.Find("wood") == before);                               // pointers stay valid
    }

    // --- Stack size from the definition; the container cap still applies ---
    {
        Inventory inv; inv.capacity = 4;
        CHECK(inv.Add("apple", 20));                                   // apple stacks to 16
        CHECK(inv.slots.size() == 2 && inv.slots[0].count == 16 && inv.slots[1].count == 4);
        Inventory small; small.capacity = 4; small.maxStack = 10;
        CHECK(small.Add("apple", 12) && small.slots[0].count == 10);   // the smaller limit wins
        Inventory loose; loose.capacity = 1;
        CHECK(loose.Add("Mystery", 500) && loose.slots[0].count == 500);   // no limit anywhere
    }

    // --- Instance data: one-off items don't stack, and move whole ---
    {
        Inventory inv; inv.capacity = 4;
        inv.Add("wood", 5);
        Inventory::Slot sword; sword.item = "rifle"; sword.count = 1; sword.data.durability = 0.5f; sword.data.uid = 77;
        inv.slots.push_back(sword);
        Inventory::Slot sword2 = sword; sword2.data.uid = 78;
        inv.slots.push_back(sword2);
        CHECK(InventoryOps::Move(&inv, 2, &inv, 1).Ok());            // two rifles with uids: swapped, never merged
        CHECK(inv.slots[1].count == 1 && inv.slots[1].data.uid == 78 && inv.slots[2].data.uid == 77);
        CHECK(inv.Count("rifle") == 2);
        Inventory::Slot mag; mag.item = "wood"; mag.count = 3; mag.data.Set("note", "wet");
        inv.slots.push_back(mag);
        CHECK(InventoryOps::Move(&inv, 3, &inv, 0).Ok());            // wet wood onto plain wood: swapped
        CHECK(!inv.slots[0].data.Empty() && inv.slots[0].count == 3);
        CHECK(inv.Count("wood") == 8);
        int plain = 0, wet = 0;
        for (const auto& s : inv.slots) if (s.item == "wood") (s.data.Empty() ? plain : wet) += s.count;
        CHECK(plain == 5 && wet == 3);                                  // the wet wood never merged into plain wood
        Inventory chest; chest.capacity = 4;
        int ri = -1; for (int i = 0; i < (int)inv.slots.size(); ++i) if (inv.slots[i].data.uid == 77) ri = i;
        CHECK(ri >= 0 && InventoryOps::Transfer(&inv, ri, &chest).Ok());
        CHECK(chest.slots.size() == 1 && chest.slots[0].data.uid == 77 && chest.slots[0].data.durability == 0.5f);
        inv.Add("wood", 2);                                             // plain adds top up the plain stack only
        plain = 0; for (const auto& s : inv.slots) if (s.item == "wood" && s.data.Empty()) plain += s.count;
        CHECK(plain == 7);
    }

    // --- Icon, rarity and use come from the definition ---
    {
        CHECK(ItemIconPath("textures/items/", "wood") == "icons/wood.png");
        CHECK(ItemIconPath("textures/items/", "Mystery") == "textures/items/Mystery.png");
        CHECK(ItemIconPath("", "Mystery").empty());
        InventoryUI ui;
        const Color* c = ui.RarityOf("apple");
        CHECK(c && c->g == 0.8f);
        ui.rarities.push_back({"apple", Color::FromBytes(255, 0, 0, 255)});
        c = ui.RarityOf("apple");
        CHECK(c && c->r == 1.0f && c->g == 0.0f);                       // the UI rule overrides
        CHECK(ui.RarityIndex("apple") == 0 && ui.RarityIndex("wood") > ui.RarityIndex("apple"));

        Scene s("T");
        GameObject* p = s.CreateGameObject("P");
        auto* inv = p->AddComponent<Inventory>();
        inv->Add("apple", 2);
        auto* cons = p->AddComponent<Consumables>();                    // no recipes of its own
        auto* hunger = p->AddComponent<HungerStat>();
        hunger->hunger = 10.0f;
        CHECK(cons->Use("apple"));
        CHECK(inv->Count("apple") == 1 && hunger->hunger > 10.0f);

        GridInventory bag; bag.cols = 6; bag.rows = 6;
        int k = bag.AddDefined("rifle");
        CHECK(k >= 0 && bag.items[k].w == 2 && bag.items[k].h == 5 && bag.items[k].weight == 4.0f);
    }

    // --- Scenes: old names map to ids, instance data round-trips ---
    {
        const char* kOld =
            "okayscene 1\n"
            "gameobject 0 \"Bag\"\n  transform 0 0 0 0 0 0 1 1 1 1\n"
            "  inventory 10 2 \"Log\" 4 \"Apple\" 2 0\nend\n";
        Scene s("T");
        std::string err;
        CHECK(SceneSerializer::Deserialize(s, kOld, &err));
        auto* inv = s.Find("Bag") ? s.Find("Bag")->GetComponent<Inventory>() : nullptr;
        CHECK(inv && inv->slots.size() == 2 && inv->slots[0].item == "wood" && inv->slots[1].item == "apple");
        if (inv) {
            inv->slots[1].data.durability = 0.25f;
            inv->slots[1].data.Set("custom name", "Golden \"Apple\"");
            const std::string text = SceneSerializer::Serialize(s);
            Scene r("R");
            CHECK(SceneSerializer::Deserialize(r, text, &err));
            auto* back = r.Find("Bag") ? r.Find("Bag")->GetComponent<Inventory>() : nullptr;
            CHECK(back && back->slots.size() == 2 && back->slots[1].data.durability == 0.25f &&
                  back->slots[1].data.Get("custom name") == "Golden \"Apple\"" && back->slots[0].data.Empty());
        }
        // With no database at all, names load exactly as written.
        db.Clear();
        Scene t("T2");
        CHECK(SceneSerializer::Deserialize(t, kOld, &err));
        auto* raw = t.Find("Bag") ? t.Find("Bag")->GetComponent<Inventory>() : nullptr;
        CHECK(raw && raw->slots[0].item == "Log" && raw->slots[1].item == "Apple" && raw->slots[0].count == 4);
    }

    TEST_MAIN_RESULT();
}
