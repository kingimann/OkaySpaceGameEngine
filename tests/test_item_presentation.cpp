// DescribeItem builds the tooltip (name, stats, compare-to-equipped).
// InventoryKeyWarning reports two inventory screens bound to the same key.
#include "test_framework.hpp"
#include "okay/Inventory/ItemPresentation.hpp"
#include <cmath>

using namespace okay;

namespace {

bool HasLine(const ItemTip& tip, const std::string& text) {
    for (const TipLine& ln : tip.lines) if (ln.text == text) return true;
    return false;
}
const TipLine* Named(const ItemTip& tip) {
    for (const TipLine& ln : tip.lines) if (ln.name) return &ln;
    return nullptr;
}

ItemDefinition Def(const std::string& id, const std::string& name, EquipSlot slot, int damage) {
    ItemDefinition d;
    d.id = id;
    d.name = name;
    d.description = "A blade";
    d.category = "Weapon";
    d.equip = slot;
    d.rarity = 2;
    d.hasRarityColor = true;
    d.rarityColor = Color{0.2f, 0.4f, 0.9f, 1.0f};
    d.maxStack = 1;
    d.weight = 3.5f;
    d.useAction = "Swing";
    d.useAmount = 5.0f;
    d.custom = {{"damage", std::to_string(damage)}, {"equipPrefab", "hidden"}, {"speed", "4"}};
    return d;
}

} // namespace

int main() {
    RUN_SUITE("item_presentation");
    ItemDatabase::Get().Clear();

    CHECK(DescribeItem("", 1, nullptr, nullptr).lines.empty());
    {
        const ItemTip unknown = DescribeItem("mystery", 0, nullptr, nullptr);
        CHECK(unknown.lines.size() == 1);
        CHECK(unknown.lines[0].text == "mystery" && unknown.lines[0].name && !unknown.lines[0].useTint);
    }

    ItemDatabase::Get().Put(Def("iron_sword", "Iron Sword", EquipSlot::MainHand, 12));
    ItemDatabase::Get().Put(Def("wood_sword", "Wood Sword", EquipSlot::MainHand, 5));
    auto* wood = ItemDatabase::Get().FindMutable("wood_sword");
    CHECK(wood != nullptr);
    if (wood) {
        wood->custom = {{"damage", "5"}, {"speed", "1"}};
        wood->hasRarityColor = false;
    }

    {
        const ItemTip tip = DescribeItem("iron_sword", 1, nullptr, nullptr);
        const TipLine* name = Named(tip);
        CHECK(name && name->text == "Iron Sword" && name->useTint);
        CHECK_NEAR(name->tint.b, 0.9f, 1e-4f);
        CHECK(HasLine(tip, "Weapon  ·  mainhand  ·  rarity 2"));
        CHECK(HasLine(tip, "A blade"));
        CHECK(HasLine(tip, "Stack 1 / 1"));
        CHECK(HasLine(tip, "Weight 3.50 kg"));
        CHECK(HasLine(tip, "Use Swing  5"));
        CHECK(HasLine(tip, "damage: 12"));
        CHECK(HasLine(tip, "speed: 4"));
        CHECK(!HasLine(tip, "equipPrefab: hidden"));          // the prefab path is not a tooltip line
    }
    {
        ItemInstanceData data;
        data.durability = 40.0f;
        const ItemTip tip = DescribeItem("iron_sword", 3, &data, nullptr, 2.0f, 2, 3);
        CHECK(HasLine(tip, "Stack 3 / 1"));
        CHECK(HasLine(tip, "Weight 6.00 kg"));                 // the per-item override, times the count
        CHECK(HasLine(tip, "2 x 3"));
        CHECK(HasLine(tip, "Durability 40"));
    }

    {
        const ItemGlyph g = GlyphFor("iron_sword");
        CHECK(g.letters == "IS");
        CHECK_NEAR(g.bg.r, 0.2f, 1e-4f);
        CHECK_NEAR(g.bg.b, 0.9f, 1e-4f);
        const ItemGlyph oak = GlyphFor("oak_log");            // no definition: initials of the id, a hashed colour
        CHECK(oak.letters == "OL");
        CHECK(std::fabs(oak.bg.r - g.bg.r) > 0.05f || std::fabs(oak.bg.g - g.bg.g) > 0.05f);
        CHECK(GlyphFor("a").letters == "A");
        CHECK(GlyphFor("---").letters == "-");
    }

    // Compare against whatever is worn in the same slot. The same item says Equipped.
    {
        Scene s("equip");
        auto* eq = s.CreateGameObject("Hero")->AddComponent<Equipment>();
        eq->slots = {{"Hand", EquipSlot::MainHand, "", "wood_sword", {}, 0, ""}};
        const ItemTip better = DescribeItem("iron_sword", 1, nullptr, eq);
        CHECK(HasLine(better, "Compared to Wood Sword"));
        CHECK(HasLine(better, "damage  +7"));
        CHECK(HasLine(better, "speed  +3"));
        const ItemTip worse = DescribeItem("wood_sword", 1, nullptr, eq);
        CHECK(HasLine(worse, "Equipped"));
        CHECK(!HasLine(worse, "Compared to Wood Sword"));

        eq->slots[0].item = "iron_sword";
        const ItemTip hoverWood = DescribeItem("wood_sword", 1, nullptr, eq);
        CHECK(HasLine(hoverWood, "Compared to Iron Sword"));
        CHECK(HasLine(hoverWood, "damage  -7"));
    }

    // Shared keys, ignoring case. Unbound (0) and space are not keys. A null scene warns nothing.
    {
        CHECK(InventoryKeyWarning(nullptr).empty());
        Scene s("keys");
        auto* bag = s.CreateGameObject("Pack")->AddComponent<InventoryUI>();
        bag->toggleKey = 'E';
        bag->sortKey = 0;
        bag->dropKey = ' ';
        bag->useKey = 0;
        CHECK(InventoryKeyWarning(&s).empty());                // one screen, nothing to share

        auto* grid = s.CreateGameObject("Crate")->AddComponent<GridInventory>();
        grid->toggleKey = 'e';
        auto* chest = s.CreateGameObject("Box")->AddComponent<ChestInventory>();
        chest->openKey = 'e';
        auto* bench = s.CreateGameObject("Anvil")->AddComponent<CraftingStation>();
        bench->toggleKey = 'b';
        auto* menu = s.CreateGameObject("Book")->AddComponent<CraftingMenu>();
        menu->toggleKey = 'B';
        auto* rotate = s.CreateGameObject("GridUI")->AddComponent<GridInventoryUI>();
        rotate->rotateKey = 'r';                               // unique: not in the warning

        const std::string w = InventoryKeyWarning(&s);
        CHECK(w.find("Key 'e' is shared by Pack bag, Crate grid, Box chest") != std::string::npos);
        CHECK(w.find("Key 'b' is shared by Anvil bench, Book craft menu") != std::string::npos);
        CHECK(w.find("rotate") == std::string::npos);
        CHECK(w.find('\n') != std::string::npos);

        grid->toggleKey = 'i';
        chest->openKey = 'f';
        menu->toggleKey = 'k';
        bench->toggleKey = '1';
        bag->useKey = '1';
        const std::string digits = InventoryKeyWarning(&s);
        CHECK(digits == "Key '1' is shared by Pack use, Anvil bench");
    }

    ItemDatabase::Get().Clear();
    TEST_MAIN_RESULT();
}
