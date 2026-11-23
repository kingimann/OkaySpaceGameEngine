#include "OkayScriptVMInternal.hpp"

namespace okay {

void OkayScriptVM::Impl::RegisterInventoryBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto charSelf = c.charSelf;
        auto modelSelf = c.modelSelf;
        auto smSelf = c.smSelf;
        // ---- Inventory ----
        // Every inv_* / grid_* call works on the host's Inventory (on it or its root), or
        // on the named object's when the optional last argument names one. Item names
        // map to item ids through the item database (ids, old names and display names).
        auto invHost = [this]() -> GameObject* { return rt.host ? rt.host->gameObject : nullptr; };
        auto invObj = [invHost](std::vector<Value>& a, std::size_t k) -> GameObject* {
            GameObject* host = invHost();
            if (a.size() > k && host && host->scene()) return ResolveObject(host->scene(), a[k]);
            return host;
        };
        auto invAt = [invObj](std::vector<Value>& a, std::size_t k) -> Inventory* {
            return ItemPickup::InventoryOf(invObj(a, k));
        };
        auto itemArg = [](std::vector<Value>& a, std::size_t k) -> std::string {
            return a.size() > k ? ItemDatabase::Get().Resolve(a[k].AsString()) : std::string();
        };
        auto intArg = [](std::vector<Value>& a, std::size_t k, int def) { return a.size() > k ? SafeInt(a[k].AsFloat()) : def; };
        b["inv_add"] = [invAt, itemArg, intArg](std::vector<Value>& a) {   // -> leftover that didn't fit
            Inventory* inv = invAt(a, 2);
            const int n = intArg(a, 1, 1);
            return Value{(float)(inv ? inv->AddPartial(itemArg(a, 0), n) : n)};
        };
        b["inv_remove"] = [invAt, itemArg, intArg](std::vector<Value>& a) {   // all or nothing
            Inventory* inv = invAt(a, 2);
            const std::string item = itemArg(a, 0);
            const int n = intArg(a, 1, 1);
            return Value{inv && n > 0 && inv->Count(item) >= n && inv->Remove(item, n) ? 1.0f : 0.0f};
        };
        b["inv_count"] = [invAt, itemArg](std::vector<Value>& a) {
            Inventory* inv = invAt(a, 1);
            return Value{(float)(inv ? inv->Count(itemArg(a, 0)) : 0)};
        };
        b["inv_has"] = [invAt, itemArg, intArg](std::vector<Value>& a) {
            Inventory* inv = invAt(a, 2);
            return Value{inv && inv->Has(itemArg(a, 0), intArg(a, 1, 1)) ? 1.0f : 0.0f};
        };
        b["inv_clear"] = [invAt](std::vector<Value>& a) { if (Inventory* inv = invAt(a, 0)) inv->Clear(); return Value{}; };
        b["inv_slot"] = [invAt, intArg](std::vector<Value>& a) {   // -> [item, count] ("" and 0 for a hole)
            Inventory* inv = invAt(a, 1);
            const int i = intArg(a, 0, -1);
            const bool ok = inv && i >= 0 && i < (int)inv->slots.size() && !Inventory::Empty(inv->slots[i]);
            Value v = Value::MakeArray();
            v.AsArray()->push_back(Value{ok ? inv->slots[i].item : std::string()});
            v.AsArray()->push_back(Value{ok ? (float)inv->slots[i].count : 0.0f});
            return v;
        };
        b["inv_set_slot"] = [invAt, itemArg, intArg](std::vector<Value>& a) {   // count <= 0 or "" empties it
            Inventory* inv = invAt(a, 3);
            const int i = intArg(a, 0, -1);
            if (!inv || i < 0 || (inv->capacity > 0 && i >= inv->capacity) || i > 4096) return Value{0.0f};
            const std::string item = itemArg(a, 1);
            const long long n = std::min<long long>(intArg(a, 2, 1), inv->StackCap(item));
            if ((int)inv->slots.size() <= i) inv->slots.resize(i + 1);
            inv->slots[i] = (item.empty() || n <= 0) ? Inventory::Slot{} : Inventory::Slot{item, (int)n, {}};
            inv->NotifyChanged();
            return Value{1.0f};
        };
        b["inv_capacity"] = [invAt](std::vector<Value>& a) { Inventory* inv = invAt(a, 0); return Value{(float)(inv ? inv->capacity : 0)}; };
        auto invUI = [invObj](std::vector<Value>& a, std::size_t k) -> InventoryUI* {
            GameObject* g = invObj(a, k);
            if (!g) return nullptr;
            if (auto* ui = g->GetComponent<InventoryUI>()) return ui;
            Scene* s = g->scene();   // the UI may sit on another object that points at this inventory
            Inventory* inv = ItemPickup::InventoryOf(g);
            if (s && inv) for (auto* ui : s->FindObjectsOfType<InventoryUI>()) if (ui->Inv() == inv) return ui;
            return nullptr;
        };
        b["inv_selected"] = [invUI](std::vector<Value>& a) { InventoryUI* ui = invUI(a, 0); return Value{ui ? ui->SelectedItem() : std::string()}; };
        b["inv_select"] = [invUI, intArg](std::vector<Value>& a) {
            InventoryUI* ui = invUI(a, 1);
            const int i = intArg(a, 0, 0);
            if (ui && i >= 0 && i < ui->hotbarSlots) ui->selected = i;
            return Value{};
        };
        b["inv_move"] = [invAt, intArg](std::vector<Value>& a) {   // (from, to, n = whole stack) -> bool
            Inventory* inv = invAt(a, 3);
            return Value{inv && InventoryOps::Move(inv, intArg(a, 0, -1), inv, intArg(a, 1, -1), intArg(a, 2, 0),
                                                   InventoryOps::UsableSlots(*inv)).Ok() ? 1.0f : 0.0f};
        };
        b["inv_transfer"] = [invAt, invHost, itemArg, intArg](std::vector<Value>& a) {   // (target, item, n) -> moved
            Inventory* src = invAt(a, 3);
            GameObject* tg = invHost() && invHost()->scene() && !a.empty() ? ResolveObject(invHost()->scene(), a[0]) : nullptr;
            Inventory* dst = ItemPickup::InventoryOf(tg);
            const std::string item = itemArg(a, 1);
            int left = intArg(a, 2, 1), moved = 0;
            if (!src || !dst || src == dst) return Value{0.0f};
            for (auto& s : src->slots) {   // stack by stack, keeping each stack's instance data
                if (left <= 0) break;
                if (Inventory::Empty(s) || s.item != item) continue;
                Inventory carried;
                carried.slots.push_back(Inventory::Slot{s.item, std::min(left, s.count), s.data});
                const int want = carried.slots[0].count;
                const InvResult r = InventoryOps::Transfer(&carried, 0, dst);
                s.count -= r.moved; moved += r.moved; left -= r.moved;
                if (s.count <= 0) s = Inventory::Slot{};
                if (r.moved < want) break;   // the target is full
            }
            if (moved > 0) src->NotifyChanged();
            return Value{(float)moved};
        };
        b["item_def"] = [itemArg](std::vector<Value>& a) -> Value {   // (id, field): a definition field or custom value
            const ItemDefinition* d = ItemDatabase::Get().Find(itemArg(a, 0));
            const std::string f = a.size() > 1 ? a[1].AsString() : std::string("name");
            if (!d) return Value{std::string()};
            if (f == "name") return Value{d->DisplayName()};
            if (f == "description") return Value{d->description};
            if (f == "icon") return Value{d->icon};
            if (f == "category") return Value{d->category};
            if (f == "weight") return Value{d->weight};
            if (f == "stack") return Value{(float)d->maxStack};
            if (f == "rarity") return Value{(float)d->rarity};
            if (f == "equip") return Value{std::string(EquipSlotName(d->equip))};
            if (f == "use") return Value{d->useAction};
            if (f == "prefab") return Value{d->worldPrefab};
            if (f == "width") return Value{(float)d->gridW};
            if (f == "height") return Value{(float)d->gridH};
            return Value{d->Custom(f)};
        };
        b["equip"] = [invHost, itemArg](std::vector<Value>& a) {   // (slot, item): from the host's bag
            GameObject* g = invHost();
            Equipment* eq = Equipment::Of(g);
            Inventory* inv = ItemPickup::InventoryOf(g);
            const int slot = eq && !a.empty() ? eq->Find(a[0].AsString()) : -1;
            const std::string item = itemArg(a, 1);
            if (!eq || !inv || slot < 0) return Value{0.0f};
            for (int i = 0; i < (int)inv->slots.size(); ++i)
                if (!Inventory::Empty(inv->slots[i]) && inv->slots[i].item == item)
                    return Value{eq->EquipFrom(*inv, i, slot).Ok() ? 1.0f : 0.0f};
            return Value{0.0f};
        };
        b["unequip"] = [invHost](std::vector<Value>& a) {   // (slot): back into the bag
            GameObject* g = invHost();
            Equipment* eq = Equipment::Of(g);
            Inventory* inv = ItemPickup::InventoryOf(g);
            const int slot = eq && !a.empty() ? eq->Find(a[0].AsString()) : -1;
            return Value{eq && inv && eq->Unequip(slot, *inv).Ok() ? 1.0f : 0.0f};
        };
        b["equipped"] = [invHost](std::vector<Value>& a) {
            Equipment* eq = Equipment::Of(invHost());
            return Value{eq && !a.empty() ? eq->ItemIn(eq->Find(a[0].AsString())) : std::string()};
        };
        b["craft"] = [invHost, itemArg, intArg](std::vector<Value>& a) {   // (recipe output, times = 1)
            GameObject* g = invHost();
            auto* c = g ? g->GetComponent<Crafting>() : nullptr;
            return Value{c && c->Craft(itemArg(a, 0), std::max(1, intArg(a, 1, 1))) ? 1.0f : 0.0f};
        };
        b["can_craft"] = [invHost, itemArg](std::vector<Value>& a) {
            GameObject* g = invHost();
            auto* c = g ? g->GetComponent<Crafting>() : nullptr;
            return Value{c && c->CanCraft(itemArg(a, 0)) ? 1.0f : 0.0f};
        };
        b["drop_item"] = [invHost, itemArg, intArg](std::vector<Value>& a) {   // (item, n = 1) -> how many dropped
            GameObject* g = invHost();
            Inventory* inv = ItemPickup::InventoryOf(g);
            Scene* sc = g ? g->scene() : nullptr;
            const std::string item = itemArg(a, 0);
            int left = intArg(a, 1, 1), dropped = 0;
            if (!inv || !sc || !g->transform) return Value{0.0f};
            const Vec3 pos = g->transform->Position() + g->transform->Forward() * 1.2f;
            for (int i = 0; i < (int)inv->slots.size() && left > 0; ++i) {
                if (Inventory::Empty(inv->slots[i]) || inv->slots[i].item != item) continue;
                const ItemInstanceData data = inv->slots[i].data;
                const int k = std::min(left, inv->slots[i].count);
                if (!InventoryOps::Drop(inv, i, k).Ok()) break;
                ItemPickup::Spawn(*sc, item, k, data, pos);
                dropped += k; left -= k;
            }
            return Value{(float)dropped};
        };
        b["spawn_pickup"] = [invHost, itemArg, intArg](std::vector<Value>& a) {   // (item, n, x, y, z) -> object name
            GameObject* g = invHost();
            Scene* sc = g ? g->scene() : nullptr;
            const Vec3 p{a.size() > 2 ? a[2].AsFloat() : 0.0f, a.size() > 3 ? a[3].AsFloat() : 0.0f, a.size() > 4 ? a[4].AsFloat() : 0.0f};
            GameObject* pk = sc ? ItemPickup::Spawn(*sc, itemArg(a, 0), std::max(1, intArg(a, 1, 1)), {}, p) : nullptr;
            return Value{pk ? pk->name : std::string()};
        };
        b["loot_roll"] = [invObj](std::vector<Value>& a) {   // (table, target = host) -> items rolled
            GameObject* t = invObj(a, 1);
            if (a.empty() || !t) return Value{0.0f};
            Inventory* inv = ItemPickup::InventoryOf(t);
            auto* grid = t->GetComponent<GridInventory>();
            int n = 0;
            for (const auto& s : LootTable::Cached(a[0].AsString()).Roll(Random::Shared())) {
                n += s.count;
                int left = s.count;
                if (inv) left = inv->AddPartial(s.item, left);
                else if (grid) for (; left > 0 && grid->AddDefined(s.item, 1) >= 0; --left) {}
                if (left > 0 && t->scene() && t->transform) ItemPickup::Spawn(*t->scene(), s.item, left, {}, t->transform->Position());
            }
            return Value{(float)n};
        };
        auto gridAt = [invObj](std::vector<Value>& a, std::size_t k) -> GridInventory* {
            GameObject* g = invObj(a, k);
            return g ? g->GetComponent<GridInventory>() : nullptr;
        };
        b["grid_add"] = [gridAt, itemArg, intArg](std::vector<Value>& a) {   // (item, n = 1) -> bool, all or nothing
            GridInventory* g = gridAt(a, 2);
            return Value{g && g->AddDefined(itemArg(a, 0), std::max(1, intArg(a, 1, 1))) >= 0 ? 1.0f : 0.0f};
        };
        b["grid_fits"] = [gridAt, itemArg](std::vector<Value>& a) {   // would one more fit (either way round)?
            GridInventory* g = gridAt(a, 1);
            const ItemDefinition* d = ItemDatabase::Get().Find(itemArg(a, 0));
            if (!g || !d) return Value{0.0f};
            int x = 0, y = 0;
            const bool room = g->FindSpot(d->gridW, d->gridH, x, y) || g->FindSpot(d->gridH, d->gridW, x, y);
            return Value{room && g->WeightAllows(d->weight) ? 1.0f : 0.0f};
        };
        b["grid_weight"] = [gridAt](std::vector<Value>& a) { GridInventory* g = gridAt(a, 0); return Value{g ? g->TotalWeight() : 0.0f}; };

        b["ik_set_pole"] = [this](std::vector<Value>& a) {
            Vec3 p{a.size() > 0 ? a[0].AsFloat() : 0.0f, a.size() > 1 ? a[1].AsFloat() : 0.0f, a.size() > 2 ? a[2].AsFloat() : 0.0f};
            if (GameObject* g = rt.host ? rt.host->gameObject : nullptr) {
                if (auto* l = g->GetComponent<LimbIK>())  { l->poleObject = nullptr; l->poleMode = LimbIK::PoleMode::World; l->pole = p - (l->upper ? l->upper->Position() : Vec3{0, 0, 0}); }
                if (auto* c = g->GetComponent<ChainIK>()) { c->poleObject = nullptr; c->poleMode = -1; c->usePole = true; c->pole = p; }
            }
            return Value{};
        };
        b["anim_prev_state"] = [smSelf](std::vector<Value>&) {
            AnimStateMachine* m = smSelf(); return Value{m ? m->Previous() : std::string{}};
        };
        b["anim_state_time"] = [smSelf](std::vector<Value>&) {
            AnimStateMachine* m = smSelf(); return Value{m ? m->StateTime() : 0.0f};
        };
        b["anim_normalized_time"] = [smSelf](std::vector<Value>&) {
            AnimStateMachine* m = smSelf(); return Value{m ? m->NormalizedTime() : 0.0f};
        };
        b["anim_goto"] = [smSelf](std::vector<Value>& a) {
            AnimStateMachine* m = smSelf();
            return Value{(m && !a.empty() && m->GoTo(a[0].AsString())) ? 1.0f : 0.0f};
        };
        b["anim_event"] = [charSelf, modelSelf](std::vector<Value>&) {
            if (Character* c = charSelf()) {
                std::string n = c->NextAnimEvent();
                if (!n.empty()) return Value{n};
            }
            ModelAnimator* m = modelSelf();       // imported-model clip events
            return Value{m ? m->NextAnimEvent() : std::string{}};
        };

        b["net_host"] = [this](std::vector<Value>& a) {
            NetworkManager* n = EnsureNet();
            std::uint16_t port = (std::uint16_t)(a.empty() ? 45000 : SafeInt(a[0].AsFloat()));
            return Value{(n && n->StartServer(port)) ? 1.0f : 0.0f};
        };
        b["net_join"] = [this](std::vector<Value>& a) {
            NetworkManager* n = EnsureNet();
            std::string host = a.empty() ? "127.0.0.1" : a[0].AsString();
            std::uint16_t port = (std::uint16_t)(a.size() < 2 ? 45000 : SafeInt(a[1].AsFloat()));
            return Value{(n && n->StartClient(host, port)) ? 1.0f : 0.0f};
        };
}

} // namespace okay
