#pragma once
#include "okay/Components/SurvivalStats.hpp"
#include "okay/Components/SurvivalComponents.hpp"

namespace okay {

/// Damage the object's health source — the all-in-one SurvivalStats if present,
/// otherwise a standalone HealthStat. Used by afflictions to hurt the player.
inline void DamageHealthOn(GameObject* go, float amount) {
    if (!go || amount <= 0.0f) return;
    if (auto* s = go->GetComponent<SurvivalStats>()) { s->Damage(amount); return; }
    if (auto* h = go->GetComponent<HealthStat>())     { h->Damage(amount); return; }
}
/// Reduce the object's warmth source (SurvivalStats or TemperatureStat) — used by
/// Wetness to chill the player. Safe no-op if neither is present.
inline void ChillWarmthOn(GameObject* go, float amount) {
    if (!go || amount <= 0.0f) return;
    if (auto* s = go->GetComponent<SurvivalStats>())
        s->warmth = Mathf::Max(0.0f, s->warmth - amount);
    else if (auto* t = go->GetComponent<TemperatureStat>())
        t->warmth = Mathf::Max(0.0f, t->warmth - amount);
}
/// Heal the object's health source (SurvivalStats or HealthStat). For buffs/regen.
inline void HealOn(GameObject* go, float amount) {
    if (!go || amount <= 0.0f) return;
    if (auto* s = go->GetComponent<SurvivalStats>()) { s->Heal(amount); return; }
    if (auto* h = go->GetComponent<HealthStat>())     { h->Heal(amount); return; }
}
/// Drain the object's stamina source (SurvivalStats or StaminaStat) — used by
/// encumbrance. Safe no-op if neither is present.
inline void DrainStaminaOn(GameObject* go, float amount) {
    if (!go || amount <= 0.0f) return;
    if (auto* s = go->GetComponent<SurvivalStats>())
        s->stamina = Mathf::Max(0.0f, s->stamina - amount);
    else if (auto* st = go->GetComponent<StaminaStat>())
        st->stamina = Mathf::Max(0.0f, st->stamina - amount);
}

/// Afflictions — "danger meters" that fill toward harm (full bar = bad) and, while
/// active, damage the object's health source directly. They publish a saved value
/// and a same-named *Bar like the other stats, and broadcast once when they cross
/// into the danger band. Put them on the same object as SurvivalStats/HealthStat.

/// Radiation that builds up in irradiated zones and decays out of them; past
/// `sickThreshold` it poisons health. AntiRad meds flush it.
class RadiationStat : public StatComponent {
public:
    float maxRadiation = 100.0f, radiation = 0.0f;
    float gainPerSecond = 12.0f;   // absorbed while in a radiation zone
    float decayPerSecond = 2.0f;   // shed when out of it
    float sickThreshold = 50.0f;   // radiation level that starts hurting
    float damagePerSecond = 3.0f;  // HP/s lost above the threshold
    bool  inRadiation = false, sick = false;

    float Fraction() const { return Frac(radiation, maxRadiation); }   // 1 = lethal dose

    void Start() override { radiation = 0.0f; m_sick = false; Publish(); }
    void Update(float dt) override {
        if (dt <= 0.0f) return;
        if (inRadiation) radiation += gainPerSecond * dt;
        else             radiation -= decayPerSecond * dt;
        radiation = Mathf::Clamp(radiation, 0.0f, maxRadiation);
        sick = radiation >= sickThreshold;
        if (sick) DamageHealthOn(gameObject, damagePerSecond * dt);
        Signal(sick, m_sick, "irradiated");
        Publish();
    }
    void AddRadiation(float a)   { radiation = Mathf::Clamp(radiation + a, 0.0f, maxRadiation); }
    void TakeAntiRad(float a)    { radiation = Mathf::Max(0.0f, radiation - a); }
    void SetInRadiation(bool on) { inRadiation = on; }
private:
    void Publish() { SaveValue("radiation", radiation); SetBar("RadiationBar", Fraction()); }
    bool m_sick = false;
};

/// Bleeding: a wound level that drains health (scaled by how bad the bleed is) and
/// clots slowly on its own; a bandage stops it instantly.
class BleedingStat : public StatComponent {
public:
    float maxBleed = 100.0f, bleed = 0.0f;
    float damagePerSecond = 4.0f;  // HP/s at full bleed (scales with level)
    float clotPerSecond = 1.0f;    // natural clotting (0 = bleeds until bandaged)
    bool  bleeding = false;

    float Fraction() const { return Frac(bleed, maxBleed); }

    void Start() override { bleed = 0.0f; m_bleed = false; Publish(); }
    void Update(float dt) override {
        if (dt <= 0.0f) return;
        if (bleed > 0.0f) {
            DamageHealthOn(gameObject, damagePerSecond * Fraction() * dt);
            bleed = Mathf::Max(0.0f, bleed - clotPerSecond * dt);
        }
        bleeding = bleed > 0.0f;
        Signal(bleeding, m_bleed, "bleeding");
        Publish();
    }
    void Wound(float a)   { bleed = Mathf::Clamp(bleed + a, 0.0f, maxBleed); }
    void Bandage()        { bleed = 0.0f; Publish(); }
    void Heal(float a)    { bleed = Mathf::Max(0.0f, bleed - a); }   // partial dressing
private:
    void Publish() { SaveValue("bleed", bleed); SetBar("BleedBar", Fraction()); }
    bool m_bleed = false;
};

/// Poison / infection: a toxin level that damages health and fades over time; an
/// antidote clears it.
class PoisonStat : public StatComponent {
public:
    float maxPoison = 100.0f, poison = 0.0f;
    float damagePerSecond = 3.0f;  // HP/s at full dose (scales with level)
    float decayPerSecond = 1.5f;   // toxin metabolised over time
    bool  poisoned = false;

    float Fraction() const { return Frac(poison, maxPoison); }

    void Start() override { poison = 0.0f; m_pois = false; Publish(); }
    void Update(float dt) override {
        if (dt <= 0.0f) return;
        if (poison > 0.0f) {
            DamageHealthOn(gameObject, damagePerSecond * Fraction() * dt);
            poison = Mathf::Max(0.0f, poison - decayPerSecond * dt);
        }
        poisoned = poison > 0.0f;
        Signal(poisoned, m_pois, "poisoned");
        Publish();
    }
    void Poison(float a)  { poison = Mathf::Clamp(poison + a, 0.0f, maxPoison); }
    void Cure(float a)    { poison = Mathf::Max(0.0f, poison - a); }
    void CureAll()        { poison = 0.0f; Publish(); }
private:
    void Publish() { SaveValue("poison", poison); SetBar("PoisonBar", Fraction()); }
    bool m_pois = false;
};

/// Wetness: soaks up in water/rain and dries over time; while wet it chills the
/// object's warmth source (couple it with Temperature for hypothermia risk).
class WetnessStat : public StatComponent {
public:
    float maxWetness = 100.0f, wetness = 0.0f;
    float soakPerSecond = 25.0f;   // gained while in water / rain
    float dryPerSecond = 5.0f;     // dries off in the open
    float chillPerSecond = 0.0f;   // warmth drained while soaked (0 = off)
    float soakedThreshold = 60.0f;
    bool  inWater = false, soaked = false;

    float Fraction() const { return Frac(wetness, maxWetness); }

    void Start() override { wetness = 0.0f; m_soaked = false; Publish(); }
    void Update(float dt) override {
        if (dt <= 0.0f) return;
        if (inWater) wetness += soakPerSecond * dt;
        else         wetness -= dryPerSecond * dt;
        wetness = Mathf::Clamp(wetness, 0.0f, maxWetness);
        if (chillPerSecond > 0.0f && wetness > 0.0f)
            ChillWarmthOn(gameObject, chillPerSecond * Fraction() * dt);
        soaked = wetness >= soakedThreshold;
        Signal(soaked, m_soaked, "soaked");
        Publish();
    }
    void AddWetness(float a) { wetness = Mathf::Clamp(wetness + a, 0.0f, maxWetness); }
    void DryOff(float a)     { wetness = Mathf::Max(0.0f, wetness - a); }
    void SetInWater(bool on) { inWater = on; }
private:
    void Publish() { SaveValue("wetness", wetness); SetBar("WetnessBar", Fraction()); }
    bool m_soaked = false;
};

} // namespace okay
