// Module vehicles: the Seat and VehicleSeat adapters for the seat contract (okay/Modules/ISeat.hpp) and the
// interaction contract.
#include "okay/Animation/HumanoidActions.hpp"
#include "okay/Components/Seat.hpp"
#include "okay/Components/VehicleSeat.hpp"
#include "okay/Modules/ISeat.hpp"
#include "okay/Input/GameInput.hpp"
#include "okay/Modules/Interact.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"

namespace okay {
namespace {

float FlatDistance(GameObject* a, const Vec3& p) {
    if (!a || !a->transform) return 1e9f;
    Vec3 d = a->transform->Position() - p;
    d.y = 0.0f;
    return d.Magnitude();
}

class ChairSeat : public ISeat, public IInteractable {
public:
    explicit ChairSeat(Seat* s) : s_(s) {}
    // ISeat
    GameObject* Occupant() const override { return s_->Occupant(); }
    bool Enter(GameObject* who) override {
        if (!who || s_->occupied) return false;
        return HumanoidActions::RequestSit(who, s_->gameObject, s_->type);
    }
    void Leave() override { s_->Release(); }
    Vec3 SitPoint() const override { return s_->SitWorld(); }
    Vec3 ExitPoint() const override { return s_->ExitWorld(); }
    const char* Model() const override { return "Seat"; }
    // IInteractable
    GameObject* Object() const override { return s_->gameObject; }
    float Range() const override { return s_->useRadius; }
    bool CanInteract(const InteractContext& c) const override {
        return s_->enabled && !s_->occupied && c.actor && FlatDistance(c.actor, s_->SitWorld()) <= s_->useRadius;
    }
    std::string Prompt(const InteractContext&) const override {
        return UseKeyName('e') + " to " + (s_->prompt.empty() ? std::string("sit") : s_->prompt);
    }
    bool Interact(const InteractContext& c) override { return Enter(c.actor); }
private:
    Seat* s_;
};

class CarSeat : public ISeat, public IInteractable {
public:
    explicit CarSeat(VehicleSeat* s) : s_(s) {}
    GameObject* Occupant() const override { return s_->Occupant(); }
    bool IsDriver() const override { return s_->driver; }
    bool Enter(GameObject* who) override { return s_->Enter(who); }
    void Leave() override { s_->Exit(); }
    Vec3 SitPoint() const override { return s_->SitWorld(); }
    Vec3 ExitPoint() const override { return s_->ExitWorld(); }
    const char* Model() const override { return "VehicleSeat"; }
    GameObject* Object() const override { return s_->gameObject; }
    float Range() const override { return s_->useRadius; }
    bool CanInteract(const InteractContext& c) const override {
        return s_->enabled && !s_->occupied && c.actor && FlatDistance(c.actor, s_->SitWorld()) <= s_->useRadius;
    }
    std::string Prompt(const InteractContext&) const override {
        return UseKeyName(s_->enterKey) + " to " + (s_->prompt.empty() ? std::string("drive") : s_->prompt);
    }
    bool Interact(const InteractContext& c) override { return Enter(c.actor); }
private:
    VehicleSeat* s_;
};

} // namespace

namespace modules {
void Register_vehicles(ModuleContext&) {
    seat::AddFinder("Seat", [](GameObject* g) -> std::unique_ptr<ISeat> {
        if (auto* s = g->GetComponent<Seat>()) return std::unique_ptr<ISeat>(new ChairSeat(s));
        return nullptr;
    });
    seat::AddFinder("VehicleSeat", [](GameObject* g) -> std::unique_ptr<ISeat> {
        if (auto* s = g->GetComponent<VehicleSeat>()) return std::unique_ptr<ISeat>(new CarSeat(s));
        return nullptr;
    });
    interact::AddFinder("Seat", [](GameObject* g, std::vector<std::unique_ptr<IInteractable>>& out) {
        if (auto* s = g->GetComponent<Seat>()) out.emplace_back(new ChairSeat(s));
    });
    interact::AddFinder("VehicleSeat", [](GameObject* g, std::vector<std::unique_ptr<IInteractable>>& out) {
        if (auto* s = g->GetComponent<VehicleSeat>()) out.emplace_back(new CarSeat(s));
    });
}
} // namespace modules
} // namespace okay
