// Where the inventory nodes are in the Add Node menu. The text-script functions each have a node (Fn.<name>); the
// inventory ones (inv_add, inv_to_grid, inv_cooldown, inv_wear ...) belong in the topic Inventory & Items, subcategory
// Inventory Slots, with the other item nodes (rules: editor/VisualScriptPlaceRules.hpp), are listed (not hidden as a copy of
// another row), and say what they do in their title. The typed-script Inventory / Equipment / Crafting / GridInventory /
// Items API (ApiInventory.cpp) makes Engine API nodes too: they sit in the same topic, and this prints which of them the
// menu lists and which it hides as the same job as a hand-written node, so the owner knows where to look (docs/try_it.md
// names the Fn nodes). It also checks that the typed nodes did not push four unrelated text-script functions out of the menu.
#include "test_framework.hpp"
#include "VisualScriptEditor.hpp"
#include "okay/VisualScript/VsApi.hpp"
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace okay::editor::vsed;

int main() {
    RUN_SUITE("vs_inventory_palette");

    // The nodes the new inventory features added, and the older inventory functions beside them.
    const char* fns[] = {"inv_to_grid", "inv_from_grid", "inv_cooldown", "inv_durability", "inv_broken", "inv_wear",
                         "inv_repair", "inv_add", "inv_remove", "inv_count", "inv_has", "inv_move", "inv_transfer",
                         "inv_set_slot", "inv_slot", "inv_selected", "item_def"};
    int listed = 0;
    for (const char* name : fns) {
        const std::string type = std::string("Fn.") + name;
        const VsDef* def = nullptr;
        for (const VsDef& d : Registry()) if (type == d.type) { def = &d; break; }
        CHECK(def != nullptr);
        if (!def) { std::printf("  no node %s\n", type.c_str()); continue; }
        const VsPlace p = VsPlaceForNode(def->type);
        const bool inItems = p.category && std::strcmp(p.category, "Inventory & Items") == 0 &&
                             std::strcmp(p.subcategory, "Inventory Slots") == 0;
        const bool hidden = VsPaletteHidden(*def);
        CHECK(inItems);
        CHECK(!hidden);
        if (!inItems) std::printf("  %s is in %s / %s, not Inventory & Items / Inventory Slots\n", type.c_str(), p.category ? p.category : "?", p.subcategory);
        if (hidden) std::printf("  %s is hidden in the menu\n", type.c_str());
        if (inItems && !hidden) {
            ++listed;
            std::printf("  Add Node > Inventory & Items > Inventory Slots > \"%s\"  (%s)\n", def->title, type.c_str());
        }
    }
    CHECK(listed == (int)(sizeof(fns) / sizeof(fns[0])));

    // The typed-script API nodes: every one has a topic (checked for the whole registry by vs_palette_categories); here the
    // inventory ones are counted, and the hidden ones named.
    const std::set<std::string> types = {"Inventory", "Equipment", "Crafting", "GridInventory", "Items"};
    int api = 0, apiHidden = 0;
    std::string hiddenNames;
    for (const okay::vs::VsApiEntry& e : okay::vs::VsApiEntries()) {
        if (!types.count(e.apiType)) continue;
        ++api;
        const VsDef* def = nullptr;
        for (const VsDef& d : Registry()) if (e.type == d.type) { def = &d; break; }
        CHECK(def != nullptr);
        const VsPlace where = VsPlaceForApi(e.apiType.c_str(), e.member.c_str());
        CHECK(where.category && std::strcmp(where.category, "Inventory & Items") == 0);   // every one is in the items topic
        if (def && VsPaletteHidden(*def)) { ++apiHidden; hiddenNames += " " + e.apiType + "." + e.member; }
    }
    CHECK(api >= 20);

    // The typed-script nodes must not take the title of an unrelated text-script function: the palette hides a function whose
    // title an earlier row has. With the plain titles Move, Add, Set Slot and Name (Inventory.Move, Inventory.Add,
    // Inventory.SetSlot, Items.Name) these four functions were missing from the menu until the typed nodes got titles of their own.
    for (const char* type : {"Fn.move", "Fn.add_xp", "Fn.set_slot", "Fn.mm_name"}) {
        const VsDef* def = nullptr;
        for (const VsDef& d : Registry()) if (std::strcmp(type, d.type) == 0) { def = &d; break; }
        CHECK(def != nullptr);
        if (!def) continue;
        CHECK(!VsPaletteHidden(*def));
        if (VsPaletteHidden(*def)) std::printf("  %s (\"%s\") is hidden by a typed-script node with the same title\n", type, def->title);
    }
    std::printf("  typed-script inventory API: %d Engine API nodes, %d hidden as a copy of a hand-written node:%s\n",
                api, apiHidden, hiddenNames.empty() ? " none" : hiddenNames.c_str());
    TEST_MAIN_RESULT();
}
