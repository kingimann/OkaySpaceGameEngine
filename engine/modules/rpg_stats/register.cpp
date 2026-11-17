// Module rpg_stats: the RPG Stats adapter for the health contract (defense lowers a hit, minimum 1).
#include "okay/Components/Stats.hpp"
#include "okay/Modules/Health.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include <algorithm>

namespace okay {
namespace {

class StatsHealthAdapter : public IHealth {
public:
    explicit StatsHealthAdapter(Stats* s) : s_(s) {}
    float Current() const override { return s_->health; }
    float Max() const override { return s_->maxHealth; }
    float Armor() const override { return (float)(s_->defense) + s_->equipDefense; }
    bool IsDead() const override { return s_->IsDead(); }
    bool IsInvincible() const override { return false; }
    const char* Model() const override { return "Stats"; }
    DamageResult ApplyDamage(const DamageEvent& e) override {
        DamageResult r;
        r.handled = true;
        if (s_->IsDead() || !(e.amount > 0.0f)) { r.blocked = true; return r; }
        const float before = s_->health;
        s_->TakeDamage(e.amount);
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
        if (!s_->IsDead()) s_->TakeDamage(s_->health + Armor());
    }
    bool Revive() override { s_->Restore(); return true; }
private:
    Stats* s_;
};

} // namespace

namespace modules {
void Register_rpg_stats(ModuleContext&) {
    health::AddFinder("Stats", 20, [](GameObject* g) -> std::unique_ptr<IHealth> {
        if (auto* s = g->GetComponent<Stats>()) return std::unique_ptr<IHealth>(new StatsHealthAdapter(s));
        return nullptr;
    });
}
} // namespace modules
} // namespace okay
