#pragma once
// Which bags are open, without calling GetComponent on every object every frame.
// The component lists are rebuilt when the scene's structure or component set changes.
#include "okay/Scene/Scene.hpp"
#include "okay/Components/InventoryUI.hpp"
#include "okay/Components/GridInventory.hpp"
#include "okay/Components/GridInventoryUI.hpp"
#include "okay/Components/CraftingStation.hpp"
#include "okay/Components/ChestInventory.hpp"
#include "okay/Components/Minimap.hpp"
#include <cstdint>
#include <vector>

namespace okay {

struct InventoryModalState {
    bool open = false;       ///< a bag, chest, bench, or fullscreen map is up
    bool dragging = false;   ///< a stack is held, so the OS cursor should hide
};

inline InventoryModalState QueryInventoryModal(const Scene& scene) {
    struct Cache {
        const Scene* scene = nullptr;
        std::uint64_t ver = 0, comp = 0, n = 0;
        std::vector<InventoryUI*> ui;
        std::vector<GridInventory*> grid;
        std::vector<GridInventoryUI*> gui;
        std::vector<CraftingStation*> craft;
        std::vector<ChestInventory*> chest;
        std::vector<Minimap*> map;
    };
    static Cache c;
    if (c.scene != &scene || c.ver != scene.StructureVersion() || c.comp != scene.ComponentVersion()
        || c.n != scene.Objects().size()) {
        c.scene = &scene;
        c.ver = scene.StructureVersion();
        c.comp = scene.ComponentVersion();
        c.n = scene.Objects().size();
        c.ui.clear(); c.grid.clear(); c.gui.clear(); c.craft.clear(); c.chest.clear(); c.map.clear();
        for (const auto& up : scene.Objects()) {
            if (!up) continue;
            if (auto* x = up->GetComponent<InventoryUI>()) c.ui.push_back(x);
            if (auto* x = up->GetComponent<GridInventory>()) c.grid.push_back(x);
            if (auto* x = up->GetComponent<GridInventoryUI>()) c.gui.push_back(x);
            if (auto* x = up->GetComponent<CraftingStation>()) c.craft.push_back(x);
            if (auto* x = up->GetComponent<ChestInventory>()) c.chest.push_back(x);
            if (auto* x = up->GetComponent<Minimap>()) c.map.push_back(x);
        }
    }
    InventoryModalState q;
    for (auto* iu : c.ui) if (iu) {
        if (iu->open && iu->dragItems) q.open = true;
        if (iu->dragIndex >= 0) q.dragging = true;
    }
    for (auto* g : c.grid) if (g && g->open) q.open = true;
    for (auto* g : c.gui) if (g && g->dragIndex >= 0) q.dragging = true;
    for (auto* x : c.craft) if (x && x->open) q.open = true;
    for (auto* x : c.chest) if (x && x->open) q.open = true;
    for (auto* m : c.map) if (m && m->fullscreen) q.open = true;
    return q;
}

} // namespace okay
