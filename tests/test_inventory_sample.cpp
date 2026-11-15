// The Inventory template (New Project > Inventory) and its shipped copy examples/inventory/: a grid bag, a Healing
// Potion with a cooldown and an Iron Sword with durability, played the way docs/try_it.md says. The example scene
// (examples/inventory/Assets/inventory_demo.okayscene) and the item definitions (examples/inventory/items.okayitems) are
// generated from Templates::Inventory and Templates::InventoryItems: run this test with OKAY_WRITE_EXAMPLE=1 to write
// them, never edit them by hand. The checks load the files from disk, feed the keys the steps name, and read what the
// player would see: the toasts, the status line, the hotbar counts and the grid bag.
// (It runs headless: nothing here draws the screen, and the editor's own windows are not touched.)
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/GridInventory.hpp"
#include "okay/Components/InventoryUI.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/UIToast.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Inventory/InventoryOps.hpp"
#include "okay/Inventory/ItemDatabase.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scene/SceneTemplates.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

namespace {
std::string ReadAll(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
void WriteAll(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
}
// Object guids are made up fresh every time an object is created, so a generated scene never repeats them: compare without.
std::string WithoutGuids(const std::string& text) {
    std::istringstream in(text);
    std::string line, out;
    while (std::getline(in, line)) {
        std::size_t i = 0;
        while (i < line.size() && line[i] == ' ') ++i;
        if (line.compare(i, 5, "guid ") == 0) continue;
        out += line + "\n";
    }
    return out;
}
} // namespace

int main() {
    RUN_SUITE("inventory_sample");
    const std::string root = std::string(OKAY_SOURCE_DIR) + "/examples/inventory/";
    const std::string scenePath = root + "Assets/inventory_demo.okayscene";
    const std::string itemsPath = root + "items.okayitems";
    constexpr float DT = 1.0f / 60.0f;

    // ---- the template, and (on request) the files that copy it ------------------------------------------------------------
    Time::ResetForTesting();
    InventoryOps::ResetCooldowns();
    ItemDatabase::Get().Clear();
    CHECK(ItemDatabase::Get().LoadText(Templates::InventoryItems()));
    std::string templateScene;
    {
        Scene t("template");
        t.physicsEnabled = false;
        Templates::Inventory(t);
        templateScene = SceneSerializer::Serialize(t);
    }
    if (std::getenv("OKAY_WRITE_EXAMPLE")) {
        std::error_code ec;
        fs::create_directories(root + "Assets", ec);
        WriteAll(itemsPath, Templates::InventoryItems());
        WriteAll(scenePath, templateScene);
        std::printf("  wrote examples/inventory/items.okayitems and Assets/inventory_demo.okayscene\n");
    }

    // ---- the shipped copy still is the template ---------------------------------------------------------------------------
    const std::string shippedScene = ReadAll(scenePath), shippedItems = ReadAll(itemsPath);
    CHECK(!shippedScene.empty() && !shippedItems.empty());
    CHECK(shippedItems == Templates::InventoryItems());
    const bool same = WithoutGuids(shippedScene) == WithoutGuids(templateScene);
    if (!same) std::printf("  examples/inventory/Assets/inventory_demo.okayscene differs from Templates::Inventory: run this test with OKAY_WRITE_EXAMPLE=1\n");
    CHECK(same);

    // ---- open the shipped scene the way the editor does (its items file is the project's) -------------------------------
    ItemDatabase::Get().Clear();
    CHECK(ItemDatabase::Get().Load(itemsPath));
    const ItemDefinition* potionDef = ItemDatabase::Get().Find("Potion");
    const ItemDefinition* swordDef = ItemDatabase::Get().Find("Sword");
    CHECK(potionDef && potionDef->Custom("cooldown") == "5" && potionDef->useAction == "Heal");
    CHECK(swordDef && swordDef->Custom("maxDurability") == "4" && swordDef->gridW == 1 && swordDef->gridH == 3);

    Scene s("sample");
    s.physicsEnabled = false;
    std::string err;
    const bool loaded = SceneSerializer::LoadFromFile(s, scenePath, &err);
    if (!loaded) std::printf("  could not load %s: %s\n", scenePath.c_str(), err.c_str());
    CHECK(loaded);
    GameObject* player = s.Find("Player");
    GameObject* hudObj = s.Find("InventoryUI");
    GameObject* toastObj = s.Find("Toasts");
    GameObject* statusObj = s.Find("Status");
    CHECK(player && hudObj && toastObj && statusObj);
    if (!player || !hudObj || !toastObj || !statusObj) TEST_MAIN_RESULT();
    auto* inv = player->GetComponent<Inventory>();
    auto* grid = player->GetComponent<GridInventory>();
    auto* hud = hudObj->GetComponent<InventoryUI>();
    auto* toast = toastObj->GetComponent<UIToast>();
    auto* status = statusObj->GetComponent<TextRenderer>();
    CHECK(inv && grid && hud && toast && status);
    if (!inv || !grid || !hud || !toast || !status) TEST_MAIN_RESULT();
    CHECK(hud->useKey == 'q');
    CHECK(grid->cols == 6 && grid->rows == 4 && grid->items.empty());     // the grid bag starts empty

    s.Start();
    auto frame = [&](std::vector<char> keys) { Input::FeedKeys(keys); Time::Step(DT); s.Update(DT); };
    auto idle = [&](float seconds) { for (int i = 0; i < (int)(seconds / DT + 0.5f); ++i) frame({}); };
    auto press = [&](char k) { frame({}); frame({k}); frame({}); };       // a tap: up, down (the press), up
    auto bodies = [&]() {                                                  // every toast message on screen
        std::vector<std::string> out;
        for (const auto& up : s.Objects())
            if (up && up->name == "ToastBody")
                if (auto* tr = up->GetComponent<TextRenderer>()) out.push_back(tr->text);
        return out;
    };
    auto showing = [&](const std::string& what) {
        for (const std::string& b : bodies()) if (b.find(what) != std::string::npos) return true;
        return false;
    };
    auto statusHas = [&](const std::string& what) { return status->text.find(what) != std::string::npos; };

    idle(0.1f);
    CHECK(inv->slots[0].item == "Potion" && inv->slots[0].count == 3);    // slot 1: the potions
    CHECK(inv->slots[1].item == "Sword" && inv->slots[1].count == 1);     // slot 2: the sword
    CHECK(statusHas("Potion: ready") && statusHas("Sword: 4 / 4"));

    // ---- a cooldown: use the potion, use it again at once ----------------------------------------------------------------
    press('q');                                                            // slot 1 is selected: use it
    CHECK(inv->Count("Potion") == 2);
    CHECK(InventoryOps::CooldownLeft(inv, "Potion") > 4.5f);
    CHECK(showing("Healing Potion"));                                      // "Used: Healing Potion"
    idle(0.1f);
    CHECK(statusHas("Potion: wait"));
    press('q');                                                            // too soon
    CHECK(inv->Count("Potion") == 2);                                      // nothing was taken
    CHECK(showing("Healing Potion is not ready: wait "));                  // the message the player reads
    std::printf("  refusal toast: %s\n", [&] { for (const std::string& b : bodies()) if (b.find("not ready") != std::string::npos) return b; return std::string("(none)"); }().c_str());
    idle(5.0f);                                                            // the cooldown runs out
    CHECK(statusHas("Potion: ready"));
    press('q');
    CHECK(inv->Count("Potion") == 1);                                      // and the potion works again

    // ---- durability: swing the sword until it breaks, see Use refuse it, repair it -----------------------------------------
    press('2');                                                            // select slot 2
    CHECK(hud->selected == 1);
    for (int swing = 1; swing <= 3; ++swing) {
        press('x');
        CHECK(inv->slots[1].data.durability == (float)(4 - swing));
    }
    idle(0.1f);
    CHECK(statusHas("Sword: 1 / 4"));
    press('x');                                                            // the fourth swing breaks it
    idle(0.1f);
    CHECK(inv->slots[1].data.durability == 0.0f);
    CHECK(statusHas("BROKEN"));
    CHECK(showing("Iron Sword broke"));
    press('q');                                                            // using a broken item
    CHECK(showing("Iron Sword is broken: repair it first."));
    press('x');                                                            // swinging a broken sword
    CHECK(showing("It is broken. Press V to repair it."));
    CHECK(inv->slots[1].data.durability == 0.0f);                          // it does not go below 0
    press('v');                                                            // repair
    idle(0.1f);
    CHECK(inv->slots[1].data.durability == 4.0f);
    CHECK(statusHas("Sword: 4 / 4"));
    press('x');                                                            // 3 / 4 again, to carry that into the grid bag

    // ---- the grid bag: T puts the selected stack in, Y takes the first grid item back -------------------------------------
    press('t');
    CHECK(inv->slots[1].item.empty());                                     // the sword left the hotbar
    CHECK(grid->items.size() == 1 && grid->items[0].name == "Sword");
    CHECK(grid->items[0].w == 1 && grid->items[0].h == 3);                 // its footprint comes from its definition
    CHECK(grid->items[0].x == 0 && grid->items[0].y == 0);
    CHECK(grid->items[0].data.durability == 3.0f);                         // the wear went with it
    CHECK(showing("In the grid at 0,0"));
    press('1');                                                            // the potion (one left)
    press('t');
    CHECK(grid->items.size() == 2 && grid->items[1].name == "Potion");
    CHECK(grid->items[1].x == 1 && grid->items[1].y == 0);                 // the next free cell (the sword is 1 wide)
    CHECK(grid->items[1].w == 1 && grid->items[1].h == 2);
    press('y');                                                            // the first grid item (the sword) comes back
    CHECK(grid->items.size() == 1 && grid->items[0].name == "Potion");
    int swordSlot = -1;
    for (int i = 0; i < (int)inv->slots.size(); ++i) if (inv->slots[i].item == "Sword") swordSlot = i;
    CHECK(swordSlot >= 0);
    CHECK(swordSlot >= 0 && inv->slots[swordSlot].data.durability == 3.0f);   // still worn by 1
    press('y');
    CHECK(grid->items.empty());
    press('y');                                                            // nothing left to take
    CHECK(showing("Nothing to take out"));

    ItemDatabase::Get().Clear();
    InventoryOps::ResetCooldowns();
    TEST_MAIN_RESULT();
}
