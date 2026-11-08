#pragma once
// ---------------------------------------------------------------------------
// Visual-script nodes for the inventory: add / remove / check items, equip, craft,
// roll loot, drop a pickup into the world. They work on this object's Inventory
// (or its root's), exactly like the inv_* / equip / craft / loot_roll script calls.
// The events (On Item Added, On Equip, On Craft ...) are plain EventNodes.
// ---------------------------------------------------------------------------
#include "okay/VisualScript/NodeGraph.hpp"
#include "okay/Components/Crafting.hpp"
#include "okay/Components/Equipment.hpp"
#include "okay/Components/GridInventory.hpp"
#include "okay/Components/ItemPickup.hpp"
#include "okay/Inventory/LootTable.hpp"
#include "okay/Core/Random.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include <algorithm>
#include <string>

namespace okay::vs {

inline Inventory* VsInventory(VsContext& ctx) { return ItemPickup::InventoryOf(ctx.gameObject); }

// Add Item: puts the node's item in the bag; Left is how many didn't fit (in0 = count, 1).
struct InvAddNode : VsNode {
    std::string item;
    float left = 0.0f;
    explicit InvAddNode(std::string i) : item(std::move(i)) {}
    int ExecOutCount() const override { return 1; }
    VsValue Eval(int, NodeGraph&, VsContext&) override { return left; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        const int n = (int)In(0, g, ctx, 1.0f).AsFloat();
        Inventory* inv = VsInventory(ctx);
        left = (float)(inv ? inv->AddPartial(ItemDatabase::Get().Resolve(item), n) : n);
        return 0;
    }
};

// Remove Item: all or nothing; Removed says whether it worked (in0 = count, 1).
struct InvRemoveNode : VsNode {
    std::string item;
    bool ok = false;
    explicit InvRemoveNode(std::string i) : item(std::move(i)) {}
    int ExecOutCount() const override { return 1; }
    VsValue Eval(int, NodeGraph&, VsContext&) override { return ok; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        const int n = (int)In(0, g, ctx, 1.0f).AsFloat();
        const std::string id = ItemDatabase::Get().Resolve(item);
        Inventory* inv = VsInventory(ctx);
        ok = inv && n > 0 && inv->Count(id) >= n && inv->Remove(id, n);
        return 0;
    }
};

// Has Item?: true when the bag holds at least in0 (1) of the node's item.
struct InvHasNode : VsNode {
    std::string item;
    explicit InvHasNode(std::string i) : item(std::move(i)) {}
    VsValue Eval(int, NodeGraph& g, VsContext& ctx) override {
        Inventory* inv = VsInventory(ctx);
        return VsValue(inv && inv->Has(ItemDatabase::Get().Resolve(item), (int)In(0, g, ctx, 1.0f).AsFloat()));
    }
};

// Item Count: how many of the node's item the bag holds.
struct InvCountNode : VsNode {
    std::string item;
    explicit InvCountNode(std::string i) : item(std::move(i)) {}
    VsValue Eval(int, NodeGraph&, VsContext& ctx) override {
        Inventory* inv = VsInventory(ctx);
        return (float)(inv ? inv->Count(ItemDatabase::Get().Resolve(item)) : 0);
    }
};

// Equip: moves the node's item from the bag onto the slot named in0 ("head", ...).
struct InvEquipNode : VsNode {
    std::string item;
    bool ok = false;
    explicit InvEquipNode(std::string i) : item(std::move(i)) {}
    int ExecOutCount() const override { return 1; }
    VsValue Eval(int, NodeGraph&, VsContext&) override { return ok; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        ok = false;
        Equipment* eq = Equipment::Of(ctx.gameObject);
        Inventory* inv = VsInventory(ctx);
        const int slot = eq ? eq->Find(In(0, g, ctx).AsString()) : -1;
        const std::string id = ItemDatabase::Get().Resolve(item);
        if (!eq || !inv || slot < 0) return 0;
        for (int i = 0; i < (int)inv->slots.size(); ++i)
            if (!Inventory::Empty(inv->slots[i]) && inv->slots[i].item == id) { ok = eq->EquipFrom(*inv, i, slot).Ok(); break; }
        return 0;
    }
};

// Craft: runs the recipe that makes the node's item, in0 times (1); needs a Crafting component.
struct InvCraftNode : VsNode {
    std::string item;
    bool ok = false;
    explicit InvCraftNode(std::string i) : item(std::move(i)) {}
    int ExecOutCount() const override { return 1; }
    VsValue Eval(int, NodeGraph&, VsContext&) override { return ok; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        auto* c = ctx.gameObject ? ctx.gameObject->GetComponent<Crafting>() : nullptr;
        ok = c && c->Craft(ItemDatabase::Get().Resolve(item), std::max(1, (int)In(0, g, ctx, 1.0f).AsFloat()));
        return 0;
    }
};

// Loot Roll: rolls the node's loot table into the bag; what doesn't fit drops next to the
// object. Rolled is how many items came up.
struct InvLootRollNode : VsNode {
    std::string table;
    float rolled = 0.0f;
    explicit InvLootRollNode(std::string t) : table(std::move(t)) {}
    int ExecOutCount() const override { return 1; }
    VsValue Eval(int, NodeGraph&, VsContext&) override { return rolled; }
    int Exec(NodeGraph&, VsContext& ctx) override {
        rolled = 0.0f;
        GameObject* t = ctx.gameObject;
        if (!t) return 0;
        Inventory* inv = ItemPickup::InventoryOf(t);
        auto* grid = t->GetComponent<GridInventory>();
        for (const auto& s : LootTable::Cached(table).Roll(Random::Shared())) {
            rolled += (float)s.count;
            int left = s.count;
            if (inv) left = inv->AddPartial(s.item, left);
            else if (grid) for (; left > 0 && grid->AddDefined(s.item, 1) >= 0; --left) {}
            if (left > 0 && t->scene() && t->transform) ItemPickup::Spawn(*t->scene(), s.item, left, {}, t->transform->Position());
        }
        return 0;
    }
};

// Spawn Pickup: drops the node's item into the world at (in1, in2, in3), in0 of them (1).
// Name is the new pickup object's name.
struct InvSpawnPickupNode : VsNode {
    std::string item;
    std::string name;
    explicit InvSpawnPickupNode(std::string i) : item(std::move(i)) {}
    int ExecOutCount() const override { return 1; }
    VsValue Eval(int, NodeGraph&, VsContext&) override { return name; }
    int Exec(NodeGraph& g, VsContext& ctx) override {
        Scene* sc = ctx.gameObject ? ctx.gameObject->scene() : nullptr;
        const Vec3 p{In(1, g, ctx, 0.0f).AsFloat(), In(2, g, ctx, 0.0f).AsFloat(), In(3, g, ctx, 0.0f).AsFloat()};
        const int n = std::max(1, (int)In(0, g, ctx, 1.0f).AsFloat());
        GameObject* pk = sc ? ItemPickup::Spawn(*sc, ItemDatabase::Get().Resolve(item), n, {}, p) : nullptr;
        name = pk ? pk->name : std::string();
        return 0;
    }
};

} // namespace okay::vs
