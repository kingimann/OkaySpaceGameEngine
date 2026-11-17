// Module ai_behavior: the NPCController adapter for the health contract. An NPC's own health is what
// MeleeAttacker and shots hit when it has no HealthStat.
#include "okay/Components/NPCController.hpp"
#include "okay/Modules/Health.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include <algorithm>

namespace okay {
namespace {

class NpcHealthAdapter : public IHealth {
public:
    explicit NpcHealthAdapter(NPCController* n) : n_(n) {}
    float Current() const override { return n_->health; }
    float Max() const override { return n_->maxHealth; }
    float Armor() const override { return 0.0f; }
    bool IsDead() const override { return n_->IsDead(); }
    bool IsInvincible() const override { return n_->invulnerable; }
    const char* Model() const override { return "NPCController"; }
    DamageResult ApplyDamage(const DamageEvent& e) override {
        DamageResult r;
        r.handled = true;
        if (n_->IsDead() || n_->invulnerable || !(e.amount > 0.0f)) { r.blocked = true; return r; }
        const float before = n_->health;   // Damage() may remove the object, so read nothing after the calls below
        n_->Damage(e.amount);
        r.applied = std::min(before, e.amount);
        r.killed = before - e.amount <= 0.0f;
        return r;
    }
    float Heal(float amount, GameObject*) override {
        const float before = n_->health;
        if (!n_->IsDead()) n_->health = std::min(n_->maxHealth, n_->health + amount);
        return std::max(0.0f, n_->health - before);
    }
    void Kill(const Vec3*) override { if (!n_->IsDead()) n_->Damage(n_->health + 1.0f); }
    bool Revive() override { return false; }   // an NPC is rebuilt by its own Start
private:
    NPCController* n_;
};

} // namespace

namespace modules {
void Register_ai_behavior(ModuleContext&) {
    health::AddFinder("NPCController", 30, [](GameObject* g) -> std::unique_ptr<IHealth> {
        if (auto* n = g->GetComponent<NPCController>()) return std::unique_ptr<IHealth>(new NpcHealthAdapter(n));
        return nullptr;
    });
}
} // namespace modules
} // namespace okay
