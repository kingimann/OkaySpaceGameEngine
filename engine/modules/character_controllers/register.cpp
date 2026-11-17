// Module character_controllers: the TopDownShooterController adapter for the weapon contract.
#include "okay/Components/TopDownShooterController.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Modules/Weapon.hpp"

namespace okay {
namespace {

class TopDownWeaponAdapter : public IWeapon {
public:
    explicit TopDownWeaponAdapter(TopDownShooterController* c) : c_(c) {}
    bool Fire() override {
        const int before = c_->ShotsFired();
        c_->Fire();
        return c_->ShotsFired() != before;
    }
    void Reload() override { c_->Reload(); }
    bool IsReloading() const override { return c_->IsReloading(); }
    int Rounds() const override { return c_->magazineSize > 0 ? c_->ammo : -1; }
    int MagazineSize() const override { return c_->magazineSize; }
    int Spare() const override { return -1; }   // this controller has no reserve: reloading is free
    const char* Model() const override { return "TopDownShooterController"; }
private:
    TopDownShooterController* c_;
};

} // namespace

namespace modules {
void Register_character_controllers(ModuleContext&) {
    weapon::AddFinder("TopDownShooterController", [](GameObject* g) -> std::unique_ptr<IWeapon> {
        if (auto* c = g->GetComponent<TopDownShooterController>()) return std::unique_ptr<IWeapon>(new TopDownWeaponAdapter(c));
        return nullptr;
    });
}
} // namespace modules
} // namespace okay
