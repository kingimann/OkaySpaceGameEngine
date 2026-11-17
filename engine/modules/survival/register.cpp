// Module survival: the SurvivalStats adapter for the health contract. Hunger, thirst and the rest keep
// calling SurvivalStats::Damage themselves; this lets shots, afflictions and scripts reach its health too.
#include "okay/Components/SurvivalStats.hpp"
#include "okay/Modules/Health.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include <algorithm>

namespace okay {
namespace {

class SurvivalHealthAdapter : public IHealth {
public:
    explicit SurvivalHealthAdapter(SurvivalStats* s) : s_(s) {}
    float Current() const override { return s_->health; }
    float Max() const override { return s_->maxHealth; }
    float Armor() const override { return s_->armor + s_->equipArmor; }
    bool IsDead() const override { return s_->IsDead(); }
    bool IsInvincible() const override { return s_->IsInvincible(); }
    const char* Model() const override { return "SurvivalStats"; }
    DamageResult ApplyDamage(const DamageEvent& e) override {
        DamageResult r;
        r.handled = true;
        if (s_->IsDead() || s_->IsInvincible() || !(e.amount > 0.0f)) { r.blocked = true; return r; }
        const float before = s_->health;
        s_->Damage(e.amount);
        r.applied = std::max(0.0f, before - s_->health);
        r.killed = s_->IsDead();
        r.blocked = r.applied <= 0.0f && !r.killed;
        return r;
    }
    float Heal(float amount, GameObject*) override {
        const float before = s_->health;
        s_->Heal(amount);
        return std::max(0.0f, s_->health - before);
    }
    void Kill(const Vec3*) override {
        if (s_->IsDead()) return;
        // Armor and resistance do not stop a kill: lift them for this one blow.
        const float a = s_->armor, ea = s_->equipArmor, res = s_->resistance;
        s_->armor = 0.0f; s_->equipArmor = 0.0f; s_->resistance = 0.0f;
        s_->Damage(s_->health + 1.0f);
        s_->armor = a; s_->equipArmor = ea; s_->resistance = res;
    }
    bool Revive() override { s_->Revive(); return true; }
private:
    SurvivalStats* s_;
};

} // namespace

namespace modules {
void Register_survival(ModuleContext&) {
    health::AddFinder("SurvivalStats", 10, [](GameObject* g) -> std::unique_ptr<IHealth> {
        if (auto* s = g->GetComponent<SurvivalStats>()) return std::unique_ptr<IHealth>(new SurvivalHealthAdapter(s));
        return nullptr;
    });
}
} // namespace modules
} // namespace okay
