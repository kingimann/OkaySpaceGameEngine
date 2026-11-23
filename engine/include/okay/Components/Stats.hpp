#pragma once
#include "okay/Components/Damage.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Math/Mathf.hpp"
#include <cmath>

namespace okay {

/// RPG character stats: mana, level/XP and a few attributes. Levelling raises max
/// health/mana and the attributes. Health is the object's Health Stat (one health per
/// object): adding Stats adds a Health Stat when there is none, every hit there counts
/// `defense` (plus worn defense) as armor and deals at least 1, and Stats fires the
/// sibling ScriptComponent's on_death() when that health hits 0 and on_level_up() each
/// level gained, so gameplay reacts without polling. Scenes saved before format 4 kept
/// health here; the loader moves it into the Health Stat.
class Stats : public Behaviour {
public:
    float mana   = 50.0f,  maxMana   = 50.0f;
    int   level = 1;
    int   xp = 0, xpToNext = 100;
    // Attributes (defense is armor on the Health Stat; the rest are for your own formulas).
    int   strength = 10, defense = 5, intelligence = 5, agility = 5;
    int   equipDefense = 0;   ///< runtime: added by worn Equipment (not saved)

    float ManaFraction()   const { return maxMana   > 0 ? Mathf::Clamp01(mana / maxMana)   : 0.0f; }
    /// The object's one health (null only if someone removed it).
    HealthStat* Health() const { return gameObject ? gameObject->GetComponent<HealthStat>() : nullptr; }
    /// The Health Stat defaults an RPG character had before health moved there: every hit deals at least
    /// 1, death changes nothing on the object (on_death is the script's to handle), and nothing is
    /// published as the player's {health}. Used when Stats adds the Health Stat.
    static void SetUpHealth(HealthStat& h) {
        h.minDamage = 1.0f;
        h.SetDeathAction(HealthStat::DeathAction::Nothing);
        h.publishPrefs = false; h.publishBar = false; h.sendMessages = false;
    }
    /// The Health Stat reached 0 (Damage.cpp HealthOwnersDied): fire the script's on_death.
    void OnHealthDied() { Event("on_death"); }

    void UseMana(float amount)   { mana = Mathf::Max(0.0f, mana - amount); }
    void RestoreMana(float amt)  { mana = Mathf::Clamp(mana + amt, 0.0f, maxMana); }
    bool HasMana(float amount) const { return mana >= amount; }
    /// Full heal + mana (e.g. on respawn / rest). A dead character is revived.
    void Restore() {
        mana = maxMana;
        if (HealthStat* h = Health()) { if (h->IsDead()) h->Revive(); else h->health = h->maxHealth; }
    }

    /// Grant experience; auto-levels while there's enough, raising caps + attributes.
    void AddXP(int amount) {
        if (amount <= 0) return;
        xp += amount;
        while (xp >= xpToNext) {
            xp -= xpToNext;
            ++level;
            xpToNext = (int)(xpToNext * 1.5f) + 25;
            maxMana += 10.0f;
            strength += 2; defense += 1; intelligence += 2; agility += 1;
            mana = maxMana;                          // refill on level up
            if (HealthStat* h = Health()) { h->maxHealth += 20.0f; if (!h->IsDead()) h->health = h->maxHealth; }
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

namespace detail { inline const bool kStatsHealthRule = RequireHealthStat<Stats, &Stats::SetUpHealth>(); }

} // namespace okay
