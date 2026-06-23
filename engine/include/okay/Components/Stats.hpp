#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Math/Mathf.hpp"

namespace okay {

/// RPG character stats: health, mana, level/XP and a few attributes, with the usual
/// combat verbs (TakeDamage / Heal / AddXP). Levelling raises max health/mana and the
/// attributes. Fires the sibling ScriptComponent's on_death() when health hits 0 and
/// on_level_up() each level gained — so gameplay reacts without polling.
class Stats : public Behaviour {
public:
    float health = 100.0f, maxHealth = 100.0f;
    float mana   = 50.0f,  maxMana   = 50.0f;
    int   level = 1;
    int   xp = 0, xpToNext = 100;
    // Attributes (used by TakeDamage and your own formulas).
    int   strength = 10, defense = 5, intelligence = 5, agility = 5;

    bool IsDead() const { return health <= 0.0f; }
    float HealthFraction() const { return maxHealth > 0 ? Mathf::Clamp01(health / maxHealth) : 0.0f; }
    float ManaFraction()   const { return maxMana   > 0 ? Mathf::Clamp01(mana / maxMana)   : 0.0f; }

    /// Apply incoming damage mitigated by defense (always at least 1). Fires on_death
    /// the moment health reaches 0.
    void TakeDamage(float amount) {
        if (IsDead()) return;
        float dmg = Mathf::Max(1.0f, amount - (float)defense);
        health = Mathf::Max(0.0f, health - dmg);
        if (health <= 0.0f) Event("on_death");
    }
    void Heal(float amount)      { health = Mathf::Clamp(health + amount, 0.0f, maxHealth); }
    void UseMana(float amount)   { mana = Mathf::Max(0.0f, mana - amount); }
    void RestoreMana(float amt)  { mana = Mathf::Clamp(mana + amt, 0.0f, maxMana); }
    bool HasMana(float amount) const { return mana >= amount; }
    /// Full heal + mana (e.g. on respawn / rest).
    void Restore() { health = maxHealth; mana = maxMana; }

    /// Grant experience; auto-levels while there's enough, raising caps + attributes.
    void AddXP(int amount) {
        if (amount <= 0) return;
        xp += amount;
        while (xp >= xpToNext) {
            xp -= xpToNext;
            ++level;
            xpToNext = (int)(xpToNext * 1.5f) + 25;
            maxHealth += 20.0f; maxMana += 10.0f;
            strength += 2; defense += 1; intelligence += 2; agility += 1;
            health = maxHealth; mana = maxMana;     // refill on level up
            Event("on_level_up");
        }
    }

private:
    void Event(const char* name) {
        if (gameObject)
            if (auto* sc = gameObject->GetComponent<ScriptComponent>())
                if (sc->VM()) sc->VM()->CallEvent(name);
    }
};

} // namespace okay
