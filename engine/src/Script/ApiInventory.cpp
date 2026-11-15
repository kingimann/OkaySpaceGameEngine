// Inventory bindings for typed scripts (okayscript-next, `class Name : Behaviour`):
// the Inventory, Equipment, Crafting and GridInventory components and the static
// Items helpers. The visual-script Engine API nodes (Inventory.Add, ...) are
// generated from these too. They call the same engine code as the OkayScript
// inv_* functions (InventoryOps), so both behave the same.
#include "ScriptInternal.hpp"
#include "okay/Components/Crafting.hpp"
#include "okay/Components/Equipment.hpp"
#include "okay/Components/GridInventory.hpp"
#include "okay/Components/Inventory.hpp"
#include "okay/Components/ItemPickup.hpp"
#include "okay/Inventory/InventoryOps.hpp"
#include "okay/Inventory/ItemDatabase.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"

namespace okay::script {

namespace {

std::int32_t Int(const Value& v) {
    if (auto* i = std::get_if<std::int32_t>(&v)) return *i;
    if (auto* f = std::get_if<float>(&v)) return (std::int32_t)*f;
    return 0;
}
const std::string& Str(const Value& v) { static const std::string e; auto* s = std::get_if<std::string>(&v); return s ? *s : e; }
std::string Item(const Value& v) { return ItemDatabase::Get().Resolve(Str(v)); }

template <class T>
T* Comp(const Value& self, CallContext& c, const char* what) {
    GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
    auto* t = g->GetComponent<T>();
    if (!t) throw RuntimeError("'" + g->name + "' has no " + what + " (add one in the Inspector)", c.loc);
    return t;
}
Inventory* Inv(const Value& s, CallContext& c) { return Comp<Inventory>(s, c, "Inventory"); }

ApiProperty Prop(std::string n, Type t, std::function<Value(const Value&, CallContext&)> get,
                 std::function<void(const Value&, const Value&, CallContext&)> set, std::string doc) {
    ApiProperty p; p.name = std::move(n); p.type = t; p.get = std::move(get); p.set = std::move(set); p.doc = std::move(doc);
    return p;
}
ApiMethod Method(std::string n, std::vector<Type> params, std::vector<std::string> names, Type ret,
                 std::function<Value(const Value&, std::vector<Value>&, CallContext&)> fn, std::string doc, bool pure = false) {
    ApiMethod m; m.name = std::move(n); m.params = std::move(params); m.paramNames = std::move(names);
    m.ret = ret; m.fn = std::move(fn); m.doc = std::move(doc); m.pure = pure;
    return m;
}

} // namespace

void RegisterInventoryApi(EngineApi& api) {
    const Type tFloat = Type::Of(TK::Float), tInt = Type::Of(TK::Int), tBool = Type::Of(TK::Bool),
               tString = Type::Of(TK::String), tVec = Type::Of(TK::Vector3), tVoid = Type::Of(TK::Void),
               tGO = Type::Obj(api.gameObject);
    const int goType = api.gameObject;

    // ---- Inventory ----
    {
        ApiType t; t.name = "Inventory"; t.isComponent = true;
        t.doc = "A bag of item stacks (hotbar, backpack, chest). Items are item ids; names and old names work too.";
        t.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<Inventory>() : nullptr; };
        t.props.push_back(Prop("capacity", tInt,
            [](const Value& s, CallContext& c) { return Value{(std::int32_t)Inv(s, c)->capacity}; },
            [](const Value& s, const Value& v, CallContext& c) {
                if (Int(v) < 0) throw RuntimeError("capacity can't be negative", c.loc);
                Inv(s, c)->capacity = Int(v);
            }, "How many stacks fit."));
        t.props.push_back(Prop("totalWeight", tFloat, [](const Value& s, CallContext& c) { return Value{Inv(s, c)->TotalWeight()}; },
            {}, "Weight carried (from the item definitions), worn items included."));
        t.props.push_back(Prop("isFull", tBool, [](const Value& s, CallContext& c) { return Value{Inv(s, c)->IsFull()}; },
            {}, "Every slot holds something."));
        t.methods.push_back(Method("Add", {tString, tInt}, {"item", "count"}, tInt,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                return Value{(std::int32_t)Inv(s, c)->AddPartial(Item(a[0]), Int(a[1]))};
            }, "Add as many as fit; gives back how many didn't."));
        t.methods.push_back(Method("Remove", {tString, tInt}, {"item", "count"}, tBool,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                Inventory* inv = Inv(s, c);
                const std::string item = Item(a[0]);
                const int n = Int(a[1]);
                return Value{n > 0 && inv->Has(item, n) && inv->Remove(item, n)};
            }, "Take items out: all of them, or none (false) if there aren't enough."));
        t.methods.push_back(Method("Count", {tString}, {"item"}, tInt,
            [](const Value& s, std::vector<Value>& a, CallContext& c) { return Value{(std::int32_t)Inv(s, c)->Count(Item(a[0]))}; },
            "How many of the item it holds.", true));
        t.methods.push_back(Method("Has", {tString, tInt}, {"item", "count"}, tBool,
            [](const Value& s, std::vector<Value>& a, CallContext& c) { return Value{Inv(s, c)->Has(Item(a[0]), Int(a[1]))}; },
            "It holds at least that many.", true));
        t.methods.push_back(Method("Clear", {}, {}, tVoid,
            [](const Value& s, std::vector<Value>&, CallContext& c) { Inv(s, c)->Clear(); return Value{}; }, "Empty it."));
        t.methods.push_back(Method("SlotItem", {tInt}, {"slot"}, tString,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                Inventory* inv = Inv(s, c);
                const int i = Int(a[0]);
                return Value{i >= 0 && i < (int)inv->slots.size() && !Inventory::Empty(inv->slots[i]) ? inv->slots[i].item : std::string()};
            }, "The item in a slot (\"\" when it's empty).", true));
        t.methods.push_back(Method("SlotCount", {tInt}, {"slot"}, tInt,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                Inventory* inv = Inv(s, c);
                const int i = Int(a[0]);
                return Value{(std::int32_t)(i >= 0 && i < (int)inv->slots.size() && !Inventory::Empty(inv->slots[i]) ? inv->slots[i].count : 0)};
            }, "How many are in a slot.", true));
        t.methods.push_back(Method("SetSlot", {tInt, tString, tInt}, {"slot", "item", "count"}, tBool,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                return Value{InventoryOps::SetSlot(Inv(s, c), Int(a[0]), Item(a[1]), Int(a[2]))};
            }, "Put items straight into a slot (count 0 empties it). False past the capacity."));
        t.methods.push_back(Method("Move", {tInt, tInt, tInt}, {"from", "to", "count"}, tBool,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                Inventory* inv = Inv(s, c);
                return Value{InventoryOps::Move(inv, Int(a[0]), inv, Int(a[1]), Int(a[2]), InventoryOps::UsableSlots(*inv)).Ok()};
            }, "Move a stack between slots (count 0 = the whole stack): merge, swap or place."));
        t.methods.push_back(Method("TransferTo", {tGO, tString, tInt}, {"target", "item", "count"}, tInt,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                GameObject* tg = Resolve(std::get<ObjRef>(a[0]), c.loc);
                Inventory* dst = ItemPickup::InventoryOf(tg);
                if (!dst) throw RuntimeError("'" + tg->name + "' has no Inventory", c.loc);
                return Value{(std::int32_t)InventoryOps::TransferItem(Inv(s, c), Item(a[1]), Int(a[2]), dst)};
            }, "Move items into another object's Inventory (a chest, a trade); gives back how many moved."));
        t.methods.push_back(Method("Drop", {tString, tInt}, {"item", "count"}, tInt,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                GameObject* g = Resolve(std::get<ObjRef>(s), c.loc);
                if (!g->scene() || !g->transform) return Value{(std::int32_t)0};
                const Vec3 pos = g->transform->Position() + g->transform->Forward() * 1.2f;
                return Value{(std::int32_t)InventoryOps::DropItem(Inv(s, c), Item(a[0]), Int(a[1]), *g->scene(), pos)};
            }, "Drop items in front of the object as pickups; gives back how many dropped."));
        t.methods.push_back(Method("Use", {tInt}, {"slot"}, tBool,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                GameObject* g = Resolve(std::get<ObjRef>(s), c.loc);
                return Value{InventoryOps::Use(Inv(s, c), Int(a[0]), g).Ok()};
            }, "Use the item in a slot: eat or drink it, pick its build piece, or equip it."));
        api.types.push_back(std::move(t));
    }
    // ---- Equipment ----
    {
        ApiType t; t.name = "Equipment"; t.isComponent = true;
        t.doc = "What a character wears and holds: slots like Head, Chest, Main Hand.";
        t.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<Equipment>() : nullptr; };
        t.props.push_back(Prop("armor", tFloat,
            [](const Value& s, CallContext& c) { return Value{Comp<Equipment>(s, c, "Equipment")->ArmorBonus()}; },
            {}, "Armor added by what's worn."));
        t.methods.push_back(Method("Equip", {tString, tString}, {"slot", "item"}, tBool,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                Comp<Equipment>(s, c, "Equipment");
                return Value{InventoryOps::EquipItem(Resolve(std::get<ObjRef>(s), c.loc), Str(a[0]), Item(a[1]))};
            }, "Put an item from the bag on, in the named slot."));
        t.methods.push_back(Method("Unequip", {tString}, {"slot"}, tBool,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                Comp<Equipment>(s, c, "Equipment");
                return Value{InventoryOps::UnequipSlot(Resolve(std::get<ObjRef>(s), c.loc), Str(a[0]))};
            }, "Take off what's in the named slot, back into the bag."));
        t.methods.push_back(Method("Equipped", {tString}, {"slot"}, tString,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                Equipment* e = Comp<Equipment>(s, c, "Equipment");
                return Value{e->ItemIn(e->Find(Str(a[0])))};
            }, "The item worn in the named slot (\"\" if none).", true));
        api.types.push_back(std::move(t));
    }
    // ---- Crafting ----
    {
        ApiType t; t.name = "Crafting"; t.isComponent = true;
        t.doc = "Makes items from recipes, out of this object's Inventory.";
        t.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<Crafting>() : nullptr; };
        t.methods.push_back(Method("Craft", {tString, tInt}, {"recipe", "times"}, tBool,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                return Value{Comp<Crafting>(s, c, "Crafting")->Craft(Item(a[0]), std::max(1, (int)Int(a[1])))};
            }, "Craft a recipe (by its output item), or queue it when it takes time."));
        t.methods.push_back(Method("CanCraft", {tString}, {"recipe"}, tBool,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                return Value{Comp<Crafting>(s, c, "Crafting")->CanCraft(Item(a[0]))};
            }, "Known, at the right bench, and the ingredients are there.", true));
        api.types.push_back(std::move(t));
    }
    // ---- GridInventory ----
    {
        ApiType t; t.name = "GridInventory"; t.isComponent = true;
        t.doc = "A grid bag where items take up cells (and can turn to fit).";
        t.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<GridInventory>() : nullptr; };
        t.props.push_back(Prop("totalWeight", tFloat,
            [](const Value& s, CallContext& c) { return Value{Comp<GridInventory>(s, c, "Grid Inventory")->TotalWeight()}; },
            {}, "Weight of everything in it."));
        t.methods.push_back(Method("Add", {tString, tInt}, {"item", "count"}, tBool,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                return Value{Comp<GridInventory>(s, c, "Grid Inventory")->AddDefined(Item(a[0]), std::max(1, (int)Int(a[1]))) >= 0};
            }, "Add items (size and weight from their definitions): all of them, or none."));
        t.methods.push_back(Method("Fits", {tString}, {"item"}, tBool,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                GridInventory* g = Comp<GridInventory>(s, c, "Grid Inventory");
                const ItemDefinition* d = ItemDatabase::Get().Find(Item(a[0]));
                int x = 0, y = 0;
                return Value{d && (g->FindSpot(d->gridW, d->gridH, x, y) || g->FindSpot(d->gridH, d->gridW, x, y)) && g->WeightAllows(d->weight)};
            }, "One more would fit (either way round, within the weight limit).", true));
        api.types.push_back(std::move(t));
    }
    // ---- Items ----
    {
        ApiType t; t.name = "Items"; t.isStatic = true;
        t.doc = "The project's item definitions, world pickups and loot tables.";
        t.staticMethods.push_back(Method("Name", {tString}, {"item"}, tString,
            [](const Value&, std::vector<Value>& a, CallContext&) {
                const ItemDefinition* d = ItemDatabase::Get().Find(Item(a[0]));
                return Value{d ? d->DisplayName() : Str(a[0])};
            }, "The item's display name.", true));
        t.staticMethods.push_back(Method("Field", {tString, tString}, {"item", "field"}, tString,
            [](const Value&, std::vector<Value>& a, CallContext&) {
                const ItemDefinition* d = ItemDatabase::Get().Find(Item(a[0]));
                return Value{d ? d->FieldText(Str(a[1])) : std::string()};
            }, "A field of the item's definition as text: name, description, category, weight, stack, rarity, equip, use, or a custom field.", true));
        t.staticMethods.push_back(Method("SpawnPickup", {tString, tInt, tVec}, {"item", "count", "position"}, tGO,
            [goType](const Value&, std::vector<Value>& a, CallContext& c) {
                Scene* sc = c.self ? c.self->scene() : nullptr;
                const Vec3* p = std::get_if<Vec3>(&a[2]);
                GameObject* pk = sc ? ItemPickup::Spawn(*sc, Item(a[0]), std::max(1, (int)Int(a[1])), {}, p ? *p : Vec3::Zero) : nullptr;
                return MakeRef(pk, goType);
            }, "Put items in the world as a pickup; gives back the new object."));
        t.staticMethods.push_back(Method("RollLoot", {tString, tGO}, {"table", "target"}, tInt,
            [](const Value&, std::vector<Value>& a, CallContext& c) {
                return Value{(std::int32_t)InventoryOps::RollLoot(Str(a[0]), Resolve(std::get<ObjRef>(a[1]), c.loc))};
            }, "Roll an .okayloot table into the target's Inventory (extra drops beside it); gives back how many items."));
        api.types.push_back(std::move(t));
    }
}

} // namespace okay::script
