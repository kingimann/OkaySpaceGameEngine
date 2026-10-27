#pragma once
// Cached lists of the inventory components a frame has to look at. Rebuilt when
// the scene's structure or component set changes, so a modal check does not call
// GetComponent on every object every frame.
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

struct InventoryModal {
    bool open = false;       ///< a bag, chest, bench, or fullscreen map is up
    bool dragging = false;   ///< a stack is on the cursor
};

inline InventoryModal QueryInventoryModal(const Scene& s) {
    struct Cache {
        const Scene* scene = nullptr;
        std::uint64_t structure = 0, components = 0;
        const void* bucket = nullptr;
        std::size_t count = 0;
        std::vector<InventoryUI*> uis;
        std::vector<GridInventory*> grids;
        std::vector<GridInventoryUI*> gridUis;
        std::vector<CraftingStation*> benches;
        std::vector<ChestInventory*> chests;
        std::vector<Minimap*> maps;
    };
    static Cache c;
    const std::uint64_t sv = s.StructureVersion(), cv = s.ComponentVersion();
    const void* bucket = s.Objects().data();
    const std::size_t count = s.Objects().size();
    if (c.scene != &s || c.structure != sv || c.components != cv || c.bucket != bucket || c.count != count) {
        c = {};
        c.scene = &s; c.structure = sv; c.components = cv; c.bucket = bucket; c.count = count;
        for (const auto& up : s.Objects()) {
            if (!up) continue;
            if (auto* p = up->GetComponent<InventoryUI>()) c.uis.push_back(p);
            if (auto* p = up->GetComponent<GridInventory>()) c.grids.push_back(p);
            if (auto* p = up->GetComponent<GridInventoryUI>()) c.gridUis.push_back(p);
            if (auto* p = up->GetComponent<CraftingStation>()) c.benches.push_back(p);
            if (auto* p = up->GetComponent<ChestInventory>()) c.chests.push_back(p);
            if (auto* p = up->GetComponent<Minimap>()) c.maps.push_back(p);
        }
    }
    InventoryModal m;
    for (InventoryUI* p : c.uis) {
        if (p->open && p->dragItems) m.open = true;
        if (p->dragIndex >= 0 || p->equipDrag >= 0) m.dragging = true;
    }
    for (GridInventory* p : c.grids) if (p->open) m.open = true;
    for (GridInventoryUI* p : c.gridUis) if (p->dragIndex >= 0) m.dragging = true;
    for (CraftingStation* p : c.benches) if (p->open) m.open = true;
    for (ChestInventory* p : c.chests) if (p->open) m.open = true;
    for (Minimap* p : c.maps) if (p->fullscreen) m.open = true;
    return m;
}

} // namespace okay
