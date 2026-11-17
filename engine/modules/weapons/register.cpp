// Module weapons: the RangedWeapon adapter for the weapon contract (okay/Modules/Weapon.hpp).
#include "okay/Components/RangedWeapon.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Modules/Weapon.hpp"

namespace okay {
namespace {

class RangedWeaponAdapter : public IWeapon {
public:
    explicit RangedWeaponAdapter(RangedWeapon* w) : w_(w) {}
    bool Fire() override { return w_->Fire(); }
    void Reload() override { w_->Reload(); }
    bool IsReloading() const override { return w_->IsReloading(); }
    int Rounds() const override { return w_->MagazineSize() > 0 ? w_->Rounds() : -1; }
    int MagazineSize() const override { return w_->MagazineSize(); }
    int Spare() const override { return w_->Spare(); }
    const char* Model() const override { return "RangedWeapon"; }
private:
    RangedWeapon* w_;
};

} // namespace

namespace modules {
void Register_weapons(ModuleContext&) {
    weapon::AddFinder("RangedWeapon", [](GameObject* g) -> std::unique_ptr<IWeapon> {
        if (auto* w = g->GetComponent<RangedWeapon>()) return std::unique_ptr<IWeapon>(new RangedWeaponAdapter(w));
        return nullptr;
    });
}
} // namespace modules
} // namespace okay
