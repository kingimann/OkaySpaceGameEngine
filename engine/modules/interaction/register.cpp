// Module interaction: adapters that let the interaction contract (okay/Modules/Interact.hpp) use ItemPickup and
// the "open with a key" things (chests, crafting stations: InventoryInteractable). They only read the
// components' own settings and call their own code, so old scenes behave as before.
#include "okay/Components/InventoryPlayer.hpp"
#include "okay/Components/ItemPickup.hpp"
#include "okay/Components/ProceduralObjectHolding.hpp"
#include "okay/Modules/Interact.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Scene/GameObject.hpp"
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
        return interact::KeyLabel(p_->interactKey) + " to pick up " + n;
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
        return interact::KeyLabel(c_->InteractKey()) + (c_->open ? " to close" : " to open");
    }
    bool Interact(const InteractContext&) override { c_->open = !c_->open; return true; }
    const char* Model() const override { return "InventoryInteractable"; }
private:
    InventoryInteractable* c_;
};

} // namespace

namespace modules {
void Register_interaction(ModuleContext&) {
    interact::AddFinder("ItemPickup", [](GameObject* g, std::vector<std::unique_ptr<IInteractable>>& out) {
        for (ItemPickup* p : g->GetComponents<ItemPickup>()) out.emplace_back(new PickupInteractable(p));
    });
    interact::AddFinder("InventoryInteractable", [](GameObject* g, std::vector<std::unique_ptr<IInteractable>>& out) {
        for (InventoryInteractable* c : g->GetComponents<InventoryInteractable>()) out.emplace_back(new OpenableInteractable(c));
    });
}
} // namespace modules
} // namespace okay
