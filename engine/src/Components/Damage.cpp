#include "okay/Components/Damage.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Components/SurvivalStats.hpp"
#include "okay/Components/Stats.hpp"
#include "okay/Components/NPCController.hpp"
#include "okay/Scene/GameObject.hpp"

namespace okay {

// The order matters and matches the old combat code: a Health Stat wins, then Survival Stats,
// then RPG Stats, then an NPC Controller.
bool DealDamage(GameObject* target, DamageEvent ev) {
    if (!target) return false;
    ev.amount = SaneDamage(ev.amount);
    if (!(ev.amount > 0.0f)) return false;
    ev.target = target;
    if (auto* hp = target->GetComponent<HealthStat>())  return !hp->IsDead() && hp->Damage(ev);
    if (auto* sv = target->GetComponent<SurvivalStats>()) return !sv->dead && sv->Damage(ev);
    if (auto* st = target->GetComponent<Stats>())        return !st->IsDead() && st->TakeDamage(ev);
    if (auto* npc = target->GetComponent<NPCController>()) return !npc->IsDead() && npc->Damage(ev);
    return false;
}

bool DealDamage(GameObject* target, float amount, DamageType type, GameObject* source) {
    return DealDamage(target, DamageEvent::Make(amount, type, source));
}

float HealObject(GameObject* target, float amount) {
    float before = 0.0f, max = 0.0f;
    if (!target || !HealthOf(target, before, max)) return 0.0f;
    if (auto* hp = target->GetComponent<HealthStat>())       hp->Heal(amount);
    else if (auto* sv = target->GetComponent<SurvivalStats>()) sv->Heal(amount);
    else if (auto* st = target->GetComponent<Stats>())        st->Heal(amount);
    else if (auto* npc = target->GetComponent<NPCController>()) {
        const float a = SaneHeal(amount);
        if (!npc->IsDead() && a > 0.0f) {
            npc->health = Mathf::Min(npc->maxHealth, npc->health + a);
            if (npc->health > before) NotifyHealed(target, npc->health - before);
        }
    }
    float after = 0.0f;
    HealthOf(target, after, max);
    return after > before ? after - before : 0.0f;
}

bool KillObject(GameObject* target, GameObject* source) {
    if (!target) return false;
    bool killed = false;
    if (auto* hp = target->GetComponent<HealthStat>())       if (!hp->IsDead())  { hp->Kill(source);  killed = true; }
    if (auto* sv = target->GetComponent<SurvivalStats>())    if (!sv->dead)      { sv->Kill(source);  killed = true; }
    if (auto* st = target->GetComponent<Stats>())            if (!st->IsDead())  { st->Kill(source);  killed = true; }
    if (auto* npc = target->GetComponent<NPCController>())   if (!npc->IsDead()) { npc->Kill(source); killed = true; }
    return killed && IsObjectDead(target);
}

bool HealthOf(GameObject* target, float& health, float& maxHealth) {
    if (!target) return false;
    if (auto* hp = target->GetComponent<HealthStat>())       { health = hp->health; maxHealth = hp->maxHealth; return true; }
    if (auto* sv = target->GetComponent<SurvivalStats>())    { health = sv->health; maxHealth = sv->maxHealth; return true; }
    if (auto* st = target->GetComponent<Stats>())            { health = st->health; maxHealth = st->maxHealth; return true; }
    if (auto* npc = target->GetComponent<NPCController>())   { health = npc->health; maxHealth = npc->maxHealth; return true; }
    return false;
}

bool IsObjectDead(GameObject* target) {
    if (!target) return false;
    if (auto* hp = target->GetComponent<HealthStat>())       return hp->IsDead();
    if (auto* sv = target->GetComponent<SurvivalStats>())    return sv->dead;
    if (auto* st = target->GetComponent<Stats>())            return st->IsDead();
    if (auto* npc = target->GetComponent<NPCController>())   return npc->IsDead();
    return false;
}

} // namespace okay
