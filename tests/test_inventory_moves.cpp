// Moving items between bags: hostile inputs (negative / huge / overflowing numbers, bad
// slots, same-slot moves), stack limits on the target bag, unique items never duplicating,
// enforced weight limits, slot bag <-> grid bag moves, and old saves still loading.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <okay/Inventory/InventoryOps.hpp>
#include <okay/Inventory/InventoryPersistence.hpp>
#include <climits>
#include <string>

using namespace okay;
using S = InvResult::Status;

static void Def(const char* id, int maxStack, float weight, int w = 1, int h = 1) {
    ItemDefinition d; d.id = id; d.name = id; d.maxStack = maxStack; d.weight = weight; d.gridW = w; d.gridH = h;
    ItemDatabase::Get().Put(d);
}
static int Total(const Inventory& a, const Inventory& b, const char* item) { return a.Count(item) + b.Count(item); }

int main() {
    RUN_SUITE("inventory_moves");
    ItemDatabase::Get().Clear();
    Def("wood", 10, 1.0f); Def("rock", 4, 5.0f); Def("sword", 1, 3.0f); Def("plank", 20, 0.5f, 1, 2); Def("totem", 0, 1.0f);

    // --- Hostile numbers on slot moves ---
    {
        Inventory a; a.capacity = 1000000; a.Add("wood", 5);   // a huge capacity: nothing else bounds the index
        Inventory b; b.capacity = 1000000;
        CHECK(InventoryOps::Move(&a, 0, &b, 0, -3).status == S::Rejected);       // negative amount: not "everything"
        CHECK(a.Count("wood") == 5 && b.slots.empty());
        CHECK(!InventoryOps::Move(&a, 0, &b, 100000).Ok());                      // would grow the bag to 100k slots
        CHECK(!InventoryOps::Move(&a, 0, &b, INT_MAX).Ok());
        CHECK(!InventoryOps::Move(&a, 0, &b, -1).Ok());
        CHECK(!InventoryOps::Move(&a, INT_MAX, &b, 0).Ok() && !InventoryOps::Move(&a, -1, &b, 0).Ok());
        CHECK(!InventoryOps::Move(nullptr, 0, &b, 0).Ok() && !InventoryOps::Move(&a, 0, nullptr, 0).Ok());
        CHECK(b.slots.size() < 5 && a.Count("wood") == 5);
        CHECK(!InventoryOps::Move(&a, 0, &a, 0).Ok());                           // same slot
        CHECK(InventoryOps::Move(&a, 0, &b, 3, INT_MAX).moved == 5);             // oversized amount = the stack
        CHECK(b.slots.size() == 4 && b.slots[3].count == 5 && a.slots.empty());
        CHECK(!InventoryOps::Move(&a, 0, &b, 0).Ok());                           // now an empty source
        Inventory c; c.capacity = 4; c.Add("wood", 4);
        CHECK(InventoryOps::Move(&c, 0, &c, 3, 1).Ok() && c.Count("wood") == 4); // inside one bag
        CHECK(!InventoryOps::Move(&c, 0, &c, 4).Ok());                           // past capacity
        CHECK(!InventoryOps::Split(&c, 0, -1).Ok() && !InventoryOps::Split(&c, 0, INT_MAX).Ok() && !InventoryOps::Split(&c, 99, 1).Ok());
        CHECK(!InventoryOps::Drop(&c, 0, -5).Ok() && !InventoryOps::Drop(&c, -1, 1).Ok() && c.Count("wood") == 4);
    }

    // --- The target bag's stack size applies to a move into an empty slot or a swap ---
    {
        Inventory a; a.capacity = 4; a.maxStack = 10; a.Add("wood", 10);
        Inventory b; b.capacity = 4; b.maxStack = 5;
        InvResult r = InventoryOps::Move(&a, 0, &b, 0);
        CHECK(r.status == S::Partial && r.moved == 5 && b.slots[0].count == 5 && a.slots[0].count == 5);
        CHECK(Total(a, b, "wood") == 10);
        Inventory c; c.capacity = 4; c.maxStack = 10; c.Add("wood", 8); c.Add("rock", 1);
        Inventory d; d.capacity = 4; d.maxStack = 5; d.Add("rock", 2);
        r = InventoryOps::Move(&c, 0, &d, 0);                                    // 8 wood onto d's rock: wood would be 8 > 5
        CHECK(!r.Ok() && c.slots[0].count == 8 && d.slots[0].item == "rock" && d.slots[0].count == 2);
    }

    // --- Unique items never duplicate ---
    {
        Inventory a; a.capacity = 4;
        a.slots.push_back({"totem", 2, ItemInstanceData{}}); a.slots[0].data.uid = 77;   // a bad 2-stack of one unique item
        Inventory b; b.capacity = 4;
        CHECK(!InventoryOps::Move(&a, 0, &b, 0, 1).Ok());
        CHECK(!InventoryOps::Split(&a, 0, 1).Ok());
        CHECK(b.slots.empty() && a.slots.size() == 1 && a.slots[0].count == 2);
        CHECK(InventoryOps::Move(&a, 0, &b, 1).Ok() && b.slots[1].data.uid == 77 && a.slots.empty());   // whole moves fine
    }

    // --- Enforced weight limit on slot bags ---
    {
        Inventory a; a.capacity = 8; a.weightLimit = 12; a.enforceWeight = true;
        CHECK(a.Add("rock", 2));                                                  // 10
        CHECK(!a.Add("rock", 1) && a.Count("rock") == 2);                         // 15 > 12: refused, nothing added
        CHECK(a.AddPartial("wood", 5) == 3 && a.Count("wood") == 2);              // 2 more kg fit
        CHECK(a.CanAdd("wood", 9) == 0);
        Inventory off; off.capacity = 8; off.weightLimit = 12;                    // default: not enforced
        CHECK(off.Add("rock", 4) && off.OverWeight());
        // Crafting swaps weight: removing first makes room.
        Inventory c; c.capacity = 8; c.weightLimit = 10; c.enforceWeight = true; c.Add("rock", 2);
        CHECK(InventoryOps::Craft(&c, {{"rock", 2}}, {{"wood", 10}}).Ok() && c.Count("wood") == 10);
        CHECK(!InventoryOps::Craft(&c, {{"wood", 1}}, {{"rock", 2}}).Ok() && c.Count("wood") == 10);
        // Moves into a limited bag are trimmed or refused.
        Inventory src; src.capacity = 8; src.Add("rock", 4);
        Inventory dst; dst.capacity = 8; dst.weightLimit = 11; dst.enforceWeight = true;
        InvResult r = InventoryOps::Move(&src, 0, &dst, 0);
        CHECK(r.status == S::Partial && r.moved == 2 && dst.Count("rock") == 2 && src.Count("rock") == 2 && r.reason == "too heavy");
        CHECK(!InventoryOps::Move(&src, 0, &dst, 1).Ok() && dst.Count("rock") == 2);   // 1 more rock would be 15
        Inventory sw; sw.capacity = 2; sw.Add("rock", 3); sw.slots[0].count = 3;         // swap: 15 kg in, 4 out
        Inventory lim; lim.capacity = 2; lim.weightLimit = 6; lim.enforceWeight = true; lim.Add("wood", 4);
        CHECK(!InventoryOps::Move(&sw, 0, &lim, 0).Ok() && lim.Count("wood") == 4 && sw.Count("rock") == 3);
        CHECK(Total(sw, lim, "rock") == 3);
        Inventory ch; ch.capacity = 4; ch.Add("wood", 5);                         // Transfer respects it too
        Inventory tight; tight.capacity = 4; tight.weightLimit = 3; tight.enforceWeight = true;
        CHECK(InventoryOps::Transfer(&ch, 0, &tight).moved == 3 && ch.Count("wood") == 2);
        Inventory un; un.capacity = 4; un.slots.push_back({"sword", 1, ItemInstanceData{}}); un.slots[0].data.uid = 5;
        Inventory tiny; tiny.capacity = 4; tiny.weightLimit = 1; tiny.enforceWeight = true;
        CHECK(!InventoryOps::Transfer(&un, 0, &tiny).Ok() && un.slots.size() == 1 && tiny.slots.empty());
    }

    // --- Grid bounds with huge coordinates ---
    {
        GridInventory g; g.cols = 4; g.rows = 4;
        CHECK(!g.CanPlace(INT_MAX, 0, 1, 1) && !g.CanPlace(0, INT_MAX, 2, 2) && !g.CanPlace(INT_MAX - 1, INT_MAX - 1, 5, 5));
        g.AddItem("rock", 1, 1, 1);
        CHECK(!g.MoveTo(0, g, INT_MAX, INT_MAX) && g.items[0].x == 0);
    }

    // --- Slot bag <-> grid bag ---
    {
        Inventory hot; hot.capacity = 6; hot.Add("wood", 8); hot.Add("plank", 6);
        GridInventory g; g.cols = 3; g.rows = 3; g.maxStack = 10;
        InvResult r = InventoryOps::ToGrid(&hot, 0, &g, 1, 1);
        CHECK(r.status == S::Ok && r.moved == 8 && hot.Count("wood") == 0 && g.items.size() == 1 && g.items[0].x == 1 && g.items[0].count == 8);
        CHECK(g.items[0].weight == 1.0f);
        // plank is 1x2: at (0,2) only one row is left, so it only fits turned (2x1).
        CHECK(!InventoryOps::ToGrid(&hot, 1, &g, 0, 2).Ok() && hot.Count("plank") == 6);          // 1x2 runs off the bottom
        CHECK(InventoryOps::ToGrid(&hot, 1, &g, 1, 2, true, 4).moved == 4);                         // turned: 2x1 at (1,2)
        CHECK(g.items.size() == 2 && g.items[1].w == 2 && g.items[1].h == 1 && hot.Count("plank") == 2);
        CHECK(!InventoryOps::ToGrid(&hot, 1, &g, 1, 1).Ok());                                      // cell taken by wood (not a stack)
        CHECK(!InventoryOps::ToGrid(&hot, 1, &g, 5, 5).Ok() && !InventoryOps::ToGrid(&hot, 1, &g, INT_MAX, INT_MIN).Ok());
        CHECK(!InventoryOps::ToGrid(&hot, 1, &g, 0, 0, false, -2).Ok() && !InventoryOps::ToGrid(&hot, 9, &g, 0, 0).Ok());
        CHECK(!InventoryOps::ToGrid(nullptr, 0, &g, 0, 0).Ok() && !InventoryOps::ToGrid(&hot, 1, nullptr, 0, 0).Ok());
        CHECK(hot.Count("plank") == 2 && g.items.size() == 2);                                    // every reject left both alone
        // merge + stack limit: wood 8 in the grid, 5 more in the hotbar -> 2 merge, 3 stay
        hot.Add("wood", 5);
        int wi = -1; for (int i = 0; i < (int)hot.slots.size(); ++i) if (hot.slots[i].item == "wood") wi = i;
        r = InventoryOps::ToGrid(&hot, wi, &g, 1, 1);
        CHECK(r.status == S::Partial && r.moved == 2 && g.items[0].count == 10 && hot.Count("wood") == 3);
        // back to the slot bag
        r = InventoryOps::FromGrid(&g, 0, &hot, 4, 4);
        CHECK(r.moved == 4 && g.items[0].count == 6 && hot.slots[4].item == "wood" && hot.slots[4].count == 4);
        CHECK(!InventoryOps::FromGrid(&g, 0, &hot, 99).Ok() && !InventoryOps::FromGrid(&g, 7, &hot, 0).Ok() &&
              !InventoryOps::FromGrid(&g, 0, &hot, 0, -1).Ok() && !InventoryOps::FromGrid(&g, 0, nullptr, 0).Ok());
        int pi = -1; for (int i = 0; i < (int)hot.slots.size(); ++i) if (hot.slots[i].item == "plank") pi = i;
        CHECK(!InventoryOps::FromGrid(&g, 0, &hot, pi).Ok() && g.items[0].count == 6);            // different item there: no swap
        CHECK(InventoryOps::FromGrid(&g, 0, &hot, 5).moved == 6 && g.items.size() == 1 && g.items[0].name == "plank");
        // instance data travels, and unique items do not split
        Inventory h2; h2.capacity = 4; h2.slots.push_back({"sword", 1, ItemInstanceData{}});
        h2.slots[0].data.durability = 0.4f; h2.slots[0].data.uid = 9;
        GridInventory g2; g2.cols = 2; g2.rows = 2;
        CHECK(InventoryOps::ToGrid(&h2, 0, &g2, 0, 0).Ok() && g2.items[0].data.uid == 9 && g2.items[0].data.durability == 0.4f && h2.slots.empty());
        CHECK(InventoryOps::FromGrid(&g2, 0, &h2, 2).Ok() && h2.slots[2].data.uid == 9 && h2.slots[2].data.durability == 0.4f && g2.items.empty());
        h2.slots[2].count = 2;
        CHECK(!InventoryOps::ToGrid(&h2, 2, &g2, 0, 0, false, 1).Ok() && h2.slots[2].count == 2 && g2.items.empty());
        // weight limits on both sides
        Inventory h3; h3.capacity = 4; h3.Add("rock", 4);
        GridInventory g3; g3.cols = 4; g3.rows = 4; g3.weightLimit = 11; g3.enforceWeight = true; g3.maxStack = 4;
        r = InventoryOps::ToGrid(&h3, 0, &g3, 0, 0);
        CHECK(!r.Ok() && h3.Count("rock") == 4 && g3.items.empty());                               // 20 kg whole stack > 11
        CHECK(InventoryOps::ToGrid(&h3, 0, &g3, 0, 0, false, 2).Ok() && g3.items[0].count == 2);
        Inventory h4; h4.capacity = 4; h4.weightLimit = 6; h4.enforceWeight = true;
        r = InventoryOps::FromGrid(&g3, 0, &h4, 0);
        CHECK(r.status == S::Partial && r.moved == 1 && g3.items[0].count == 1 && h4.Count("rock") == 1);
    }

    // --- Old saves still load ---
    {
        // A scene written before enforceWeight existed: the inventory line ends at keepOnSceneLoad.
        Scene s("old");
        GameObject* p = s.CreateGameObject("Player");
        auto* inv = p->AddComponent<Inventory>();
        inv->capacity = 8; inv->maxStack = 10; inv->weightLimit = 30; inv->keepOnSceneLoad = true; inv->enforceWeight = true;
        inv->Add("wood", 12);
        std::string text = SceneSerializer::Serialize(s);
        { Scene back("b"); std::string err;
          CHECK(SceneSerializer::Deserialize(back, text, &err));
          auto* bi = back.Find("Player") ? back.Find("Player")->GetComponent<Inventory>() : nullptr;
          CHECK(bi && bi->enforceWeight && bi->Count("wood") == 12 && bi->keepOnSceneLoad); }
        const std::size_t at = text.find("  inventory ");
        const std::size_t eol = text.find('\n', at);
        std::string line = text.substr(at, eol - at);
        const std::size_t lastSp = line.rfind(' ');
        CHECK(line.substr(lastSp) == " 1");
        text.replace(at, eol - at, line.substr(0, lastSp));                                          // the old format
        { Scene back("b"); std::string err;
          CHECK(SceneSerializer::Deserialize(back, text, &err));
          auto* bi = back.Find("Player") ? back.Find("Player")->GetComponent<Inventory>() : nullptr;
          CHECK(bi && !bi->enforceWeight && bi->Count("wood") == 12 && bi->keepOnSceneLoad && bi->weightLimit == 30.0f && bi->maxStack == 10); }
        // The player-bag file format (okayinv 1), written by hand exactly as saved today.
        Scene s2("p");
        GameObject* hero = s2.CreateGameObject("Hero");
        auto* hi = hero->AddComponent<Inventory>(); hi->capacity = 6;
        auto* hg = hero->AddComponent<GridInventory>();
        const std::string file =
            "okayinv 1\ninventory 3\nslot \"wood\" 7 -1 0 0\nslot \"\" 0 -1 0 0\n"
            "slot \"sword\" 1 0.5 12 1 \"note\" \"mine\"\n"
            "grid\nitem \"rock\" 1 1 2 3 4 5 -1 0 0\noverflow \"plank\" 1 2 0 0 3 0.5 -1 0 0\n";
        CHECK(InventoryPersistence::LoadObjectText(*hero, file));
        CHECK(hi->slots.size() == 3 && hi->slots[0].item == "wood" && hi->slots[0].count == 7 && Inventory::Empty(hi->slots[1]));
        CHECK(hi->slots[2].data.durability == 0.5f && hi->slots[2].data.uid == 12 && hi->slots[2].data.Get("note") == "mine");
        CHECK(hg->items.size() == 2 && hg->overflow.empty() && hg->items[0].x == 2 && hg->items[0].y == 3 && hg->items[0].count == 4);
        // and what we save now round-trips through the moves above
        Inventory spare; spare.capacity = 6;
        CHECK(InventoryOps::Move(hi, 0, &spare, 2, 3).Ok() && hi->Count("wood") == 4 && spare.slots[2].count == 3);
        const std::string again = InventoryPersistence::SerializeObject(*hero);
        CHECK(InventoryPersistence::LoadObjectText(*hero, again) && hi->Count("wood") == 4 && hi->slots[2].data.uid == 12);
    }

    TEST_MAIN_RESULT();
}
