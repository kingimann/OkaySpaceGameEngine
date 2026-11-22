// Module interaction: adapters that let the interaction contract (okay/Modules/Interact.hpp) use ItemPickup and
// the "open with a key" things (chests, crafting stations: InventoryInteractable). They only read the
// components' own settings and call their own code, so old scenes behave as before.
#include "okay/Components/Interactable.hpp"
#include "okay/Components/InventoryPlayer.hpp"
#include "okay/Components/ItemPickup.hpp"
#include "okay/Components/ProceduralObjectHolding.hpp"
#include "okay/Input/GameInput.hpp"
#include "okay/Modules/Interact.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Scene/ComponentSerializer.hpp"
#include "okay/Scene/PropertyBuilder.hpp"
#include "okay/Scene/RecordIO.hpp"
#include "okay/Scene/GameObject.hpp"
#include <iomanip>
#include <sstream>
#include "okay/Scene/Transform.hpp"

namespace okay {
namespace {

float DistanceTo(GameObject* a, GameObject* b) {
    if (!a || !b || !a->transform || !b->transform) return 0.0f;
    return (a->transform->Position() - b->transform->Position()).Magnitude();
}

class PickupInteractable : public IInteractable {
public:
    explicit PickupInteractable(ItemPickup* p) : p_(p) {}
    GameObject* Object() const override { return p_->gameObject; }
    float Range() const override { return p_->interactRange; }
    bool CanInteract(const InteractContext& c) const override {
        return p_->enabled && p_->interactKey != 0 && !p_->Held() && c.actor &&
               DistanceTo(c.actor, p_->gameObject) <= p_->interactRange;
    }
    std::string Prompt(const InteractContext&) const override {
        std::string n = p_->item.empty() ? p_->gameObject->name : p_->item;
        return UseKeyName(p_->interactKey) + " to pick up " + n;
    }
    bool Interact(const InteractContext& c) override {
        if (!CanInteract(c)) return false;
        auto* hands = p_->holdInHands ? c.actor->GetComponent<ProceduralObjectHolding>() : nullptr;
        if (hands && hands->Pickup(p_->gameObject)) return true;
        return p_->GiveTo(c.actor) > 0;
    }
    const char* Model() const override { return "ItemPickup"; }
private:
    ItemPickup* p_;
};

class OpenableInteractable : public IInteractable {
public:
    explicit OpenableInteractable(InventoryInteractable* c) : c_(c) {}
    GameObject* Object() const override { return c_->gameObject; }
    float Range() const override { return c_->range; }
    bool CanInteract(const InteractContext&) const override { return c_->enabled && c_->PlayerInRange(); }
    std::string Prompt(const InteractContext&) const override {
        return UseKeyName(c_->InteractKey()) + (c_->open ? " to close" : " to open");
    }
    bool Interact(const InteractContext&) override { c_->open = !c_->open; return true; }
    const char* Model() const override { return "InventoryInteractable"; }
private:
    InventoryInteractable* c_;
};

class UseInteractable : public IInteractable {
public:
    explicit UseInteractable(Interactable* c) : c_(c) {}
    GameObject* Object() const override { return c_->gameObject; }
    float Range() const override { return c_->range; }
    bool CanInteract(const InteractContext& c) const override {
        return c_->Available() && c.actor && DistanceTo(c.actor, c_->gameObject) <= c_->range;
    }
    std::string Prompt(const InteractContext&) const override { return UseKeyName(c_->key) + " to " + c_->prompt; }
    bool Interact(const InteractContext& c) override { return c_->Use(c.actor); }
    float HoldSeconds() const override { return c_->holdSeconds; }
    const char* Model() const override { return "Interactable"; }
private:
    Interactable* c_;
};

} // namespace

namespace modules {
void Register_interaction(ModuleContext&) {
    interact::AddFinder("ItemPickup", [](GameObject* g, std::vector<std::unique_ptr<IInteractable>>& out) {
        for (ItemPickup* p : g->GetComponents<ItemPickup>()) out.emplace_back(new PickupInteractable(p));
    });
    interact::AddFinder("Interactable", [](GameObject* g, std::vector<std::unique_ptr<IInteractable>>& out) {
        for (Interactable* c : g->GetComponents<Interactable>()) out.emplace_back(new UseInteractable(c));
    });
    // Records: key=value through the shared helpers (okay/Scene/RecordIO.hpp); the prompt is a quoted string.
    ComponentSerializerRegistry::Entry ia;
    ia.name = "interactable";
    ia.write = [](const GameObject& go, std::ostream& out) {
        for (auto* c : go.GetComponents<Interactable>()) {
            out << "  interactable";
            rec::KvOut{out}("range", c->range)("hold", c->holdSeconds).b("once", c->oneShot).key("key", c->key).str("prompt", c->prompt);
            out << "\n";
        }
    };
    ia.read = [](GameObject& go, std::istream& in) {
        auto* c = go.AddComponent<Interactable>();
        const rec::KvIn kv(in);
        kv.f("range", c->range); kv.f("hold", c->holdSeconds); kv.b("once", c->oneShot); kv.key("key", c->key); kv.str("prompt", c->prompt);
    };
    ComponentSerializerRegistry::Register(std::move(ia));
    ComponentSerializerRegistry::Entry is;
    is.name = "interactionsensor";
    is.write = [](const GameObject& go, std::ostream& out) {
        for (auto* c : go.GetComponents<InteractionSensor>()) { out << "  interactionsensor"; rec::KvOut{out}("range", c->maxRange); out << "\n"; }
    };
    is.read = [](GameObject& go, std::istream& in) {
        auto* c = go.AddComponent<InteractionSensor>();
        rec::KvIn(in).f("range", c->maxRange);
    };
    ComponentSerializerRegistry::Register(std::move(is));
    // Property tables (the Inspector, scripts, the save coverage gate). Saving stays with the records above.
    {
        PropertyBuilder<Interactable> b("Interactable", "interactable", "Gameplay");
        b.AddFloat("range", &Interactable::range).Range(0, 1000, 0.1).Tooltip("Metres from the user")
         .AddString("prompt", &Interactable::prompt).Tooltip("Shown as \"E to <prompt>\"")
         .AddFloat("holdSeconds", &Interactable::holdSeconds).Range(0, 60, 0.1).Tooltip("0 = a tap; more = hold the key this long")
         .AddBool("oneShot", &Interactable::oneShot).Tooltip("Can be used once")
         .AddKey("key", &Interactable::key).Tooltip("e = the Interact action; 0 = not usable")
         .AddInt("useCount", &Interactable::useCount).RuntimeOnly();
        b.Commit();
    }
    {
        PropertyBuilder<InteractionSensor> b("InteractionSensor", "interactionsensor", "Gameplay");
        b.AddFloat("maxRange", &InteractionSensor::maxRange).Range(0, 1000, 0.1).Tooltip("0 = each thing's own range");
        b.Commit();
    }
    interact::AddFinder("InventoryInteractable", [](GameObject* g, std::vector<std::unique_ptr<IInteractable>>& out) {
        for (InventoryInteractable* c : g->GetComponents<InventoryInteractable>()) out.emplace_back(new OpenableInteractable(c));
    });
}
} // namespace modules
} // namespace okay
