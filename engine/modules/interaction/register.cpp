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
    ComponentSerializerRegistry::Entry ia;
    ia.name = "interactable";
    ia.write = [](const GameObject& go, std::ostream& out) {
        for (auto* c : go.GetComponents<Interactable>())
            out << "  interactable range=" << c->range << " hold=" << c->holdSeconds << " once=" << (c->oneShot ? 1 : 0)
                << " key=" << (int)(unsigned char)c->key << " prompt=" << std::quoted(c->prompt) << "\n";
    };
    ia.read = [](GameObject& go, std::istream& in) {
        auto* c = go.AddComponent<Interactable>();
        std::string tok;
        while (in >> tok) {
            const size_t eq = tok.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = tok.substr(0, eq), v = tok.substr(eq + 1);
            if (k == "range") c->range = (float)std::atof(v.c_str());
            else if (k == "hold") c->holdSeconds = (float)std::atof(v.c_str());
            else if (k == "once") c->oneShot = std::atoi(v.c_str()) != 0;
            else if (k == "key") c->key = (char)std::atoi(v.c_str());
            else if (k == "prompt") {   // a quoted string, possibly with spaces
                std::string rest = v;
                if (!rest.empty() && rest[0] == '"' && (rest.size() < 2 || rest.back() != '"')) {
                    std::string more; std::getline(in, more); rest += more;
                }
                std::istringstream qs(rest);
                std::string unq;
                if (qs >> std::quoted(unq)) c->prompt = unq;
            }
        }
    };
    ComponentSerializerRegistry::Register(std::move(ia));
    ComponentSerializerRegistry::Entry is;
    is.name = "interactionsensor";
    is.write = [](const GameObject& go, std::ostream& out) {
        for (auto* c : go.GetComponents<InteractionSensor>()) out << "  interactionsensor range=" << c->maxRange << "\n";
    };
    is.read = [](GameObject& go, std::istream& in) {
        auto* c = go.AddComponent<InteractionSensor>();
        std::string tok;
        while (in >> tok)
            if (tok.compare(0, 6, "range=") == 0) c->maxRange = (float)std::atof(tok.c_str() + 6);
    };
    ComponentSerializerRegistry::Register(std::move(is));
    interact::AddFinder("InventoryInteractable", [](GameObject* g, std::vector<std::unique_ptr<IInteractable>>& out) {
        for (InventoryInteractable* c : g->GetComponents<InventoryInteractable>()) out.emplace_back(new OpenableInteractable(c));
    });
}
} // namespace modules
} // namespace okay
