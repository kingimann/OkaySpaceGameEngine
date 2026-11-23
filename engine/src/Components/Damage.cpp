#include "okay/Components/Damage.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Components/Stats.hpp"
#include "okay/Components/NPCController.hpp"
#include "okay/Scene/GameObject.hpp"

namespace okay {

// One health per object: its Health Stat. Survival Stats, RPG Stats and an NPC Controller have none of
// their own any more (scene format 4); they use this one and react through the HealthOwners* below.
bool DealDamage(GameObject* target, DamageEvent ev) {
    if (!target) return false;
    ev.amount = SaneDamage(ev.amount);
    if (!(ev.amount > 0.0f)) return false;
    ev.target = target;
    if (auto* hp = target->GetComponent<HealthStat>()) return !hp->IsDead() && hp->Damage(ev);
    return false;
}

bool DealDamage(GameObject* target, float amount, DamageType type, GameObject* source) {
    return DealDamage(target, DamageEvent::Make(amount, type, source));
}

float HealObject(GameObject* target, float amount) {
    auto* hp = target ? target->GetComponent<HealthStat>() : nullptr;
    if (!hp) return 0.0f;
    const float before = hp->health;
    hp->Heal(amount);
    return hp->health > before ? hp->health - before : 0.0f;
}

bool KillObject(GameObject* target, GameObject* source) {
    auto* hp = target ? target->GetComponent<HealthStat>() : nullptr;
    if (!hp || hp->IsDead()) return false;
    hp->Kill(source);
    return hp->IsDead();
}

bool HealthOf(GameObject* target, float& health, float& maxHealth) {
    auto* hp = target ? target->GetComponent<HealthStat>() : nullptr;
    if (!hp) return false;
    health = hp->health; maxHealth = hp->maxHealth;
    return true;
}

bool IsObjectDead(GameObject* target) {
    auto* hp = target ? target->GetComponent<HealthStat>() : nullptr;
    return hp && hp->IsDead();
}

float HealthOwnerArmor(GameObject* go) {
    auto* st = go ? go->GetComponent<Stats>() : nullptr;
    return st ? (float)(st->defense + st->equipDefense) : 0.0f;
}

void HealthOwnersHit(const DamageEvent& ev) {
    if (!ev.target) return;
    if (auto* npc = ev.target->GetComponent<NPCController>()) npc->OnHealthHit();
}

void HealthOwnersDied(const DamageEvent& ev) {
    if (!ev.target) return;
    if (auto* st = ev.target->GetComponent<Stats>()) st->OnHealthDied();
    if (auto* npc = ev.target->GetComponent<NPCController>()) npc->OnHealthDied();
}

} // namespace okay
