#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Components/UIProgressBar.hpp"
#include "okay/Components/ActionList.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Math/Mathf.hpp"
#include <string>

namespace okay {

/// Individual native survival stats — the C++ siblings of the per-stat Survival Kit
/// scripts (Health, Hunger, Thirst, Stamina, Oxygen, Temperature, Sleep, Sanity).
/// Mix and match: drop just the ones a game needs (e.g. Health + Stamina for an
/// action game) instead of the all-in-one SurvivalStats. Each drains/regenerates in
/// native code, publishes a saved value (`{health}`, `{hunger}`, …) and fills a
/// same-named progress bar (`HealthBar`, `HungerBar`, …) each frame, and broadcasts a
/// message once when it bottoms out (`starving`, `dehydrated`, …) for an ActionList.
///
/// They don't cross-talk (a standalone Hunger won't drain Health) — use SurvivalStats
/// for the wired-together model where empty stats damage health directly.
class StatComponent : public Behaviour {
public:
    bool publishPrefs = true;      // write the saved value each frame
    bool publishBar   = true;      // fill the same-named *Bar widget
    bool sendMessages = true;      // broadcast the critical message once

protected:
    void SetBar(const char* name, float frac) {
        if (!publishBar) return;
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        if (!s) return;
        if (GameObject* g = s->Find(name))
            if (auto* pb = g->GetComponent<UIProgressBar>()) pb->SetValue(frac);
    }
    void SaveValue(const char* key, float v) { if (publishPrefs) Prefs::SetFloat(key, v); }
    void Broadcast(const std::string& msg) {
        if (!sendMessages) return;
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        if (!s) return;
        for (ActionList* al : s->FindObjectsOfType<ActionList>()) al->ReceiveMessage(msg);
    }
    // Fire `msg` once on the rising edge of `crit`; rearm when it clears.
    void Signal(bool crit, bool& latch, const char* msg) {
        if (crit) { if (!latch) { latch = true; Broadcast(msg); } }
        else latch = false;
    }
    static float Frac(float v, float mx) { return mx > 0.0f ? Mathf::Clamp01(v / mx) : 0.0f; }
};

/// HP with flat armor, optional delayed regen, low/death flags. On death it
/// broadcasts `died`, plays a sibling AudioSource, and deactivates the object.
class HealthStat : public StatComponent {
public:
    float maxHealth = 100.0f, health = 100.0f;
    float armor = 0.0f;
    float regenPerSecond = 0.0f;   // passive heal (0 = off)
    float regenDelay = 0.0f;       // delay after a hit before regen resumes
    float lowThreshold = 25.0f;
    bool  dead = false, lowHealth = false;

    float Fraction() const { return Frac(health, maxHealth); }
    bool  IsDead()   const { return dead; }

    void Start() override { health = maxHealth; dead = false; m_timer = 0.0f; Publish(); }
    void Update(float dt) override {
        if (dead || dt <= 0.0f) return;
        if (m_timer > 0.0f) m_timer -= dt;
        if (regenPerSecond > 0.0f && m_timer <= 0.0f)
            health = Mathf::Min(maxHealth, health + regenPerSecond * dt);
        lowHealth = health <= lowThreshold;
        Publish();
    }
    void Damage(float amount) {
        if (dead) return;
        health -= Mathf::Max(0.0f, amount - armor);
        m_timer = regenDelay;
        if (health <= 0.0f) {
            health = 0.0f; dead = true;
            Broadcast("died");
            if (gameObject) {
                if (auto* au = gameObject->GetComponent<AudioSource>()) au->Play();
                gameObject->active = false;
            }
        }
        Publish();
    }
    void Heal(float a)    { if (!dead) { health = Mathf::Min(maxHealth, health + a); Publish(); } }
    void AddArmor(float a){ armor += a; }
    void Revive()         { dead = false; health = maxHealth; m_timer = 0.0f; if (gameObject) gameObject->active = true; Publish(); }
private:
    void Publish() { SaveValue("health", health); SetBar("HealthBar", Fraction()); }
    float m_timer = 0.0f;
};

/// Hunger that drains over time (faster while sprinting); empty -> `starving`.
class HungerStat : public StatComponent {
public:
    float maxHunger = 100.0f, hunger = 100.0f;
    float drainPerSecond = 1.5f;
    float sprintMultiplier = 2.0f;
    float lowThreshold = 25.0f;
    bool  sprinting = false, starving = false, low = false;

    float Fraction() const { return Frac(hunger, maxHunger); }

    void Start() override { hunger = maxHunger; m_starv = false; Publish(); }
    void Update(float dt) override {
        if (dt <= 0.0f) return;
        float m = sprinting ? sprintMultiplier : 1.0f;
        hunger = Mathf::Clamp(hunger - drainPerSecond * m * dt, 0.0f, maxHunger);
        starving = hunger <= 0.0f; low = hunger <= lowThreshold;
        Signal(starving, m_starv, "starving");
        Publish();
    }
    void Eat(float a)          { hunger = Mathf::Min(maxHunger, hunger + a); Publish(); }
    void SetSprinting(bool on) { sprinting = on; }
private:
    void Publish() { SaveValue("hunger", hunger); SetBar("HungerBar", Fraction()); }
    bool m_starv = false;
};

/// Thirst that drains over time (faster while sprinting); empty -> `dehydrated`.
class ThirstStat : public StatComponent {
public:
    float maxThirst = 100.0f, thirst = 100.0f;
    float drainPerSecond = 2.0f;
    float sprintMultiplier = 2.0f;
    float lowThreshold = 25.0f;
    bool  sprinting = false, dehydrated = false, low = false;

    float Fraction() const { return Frac(thirst, maxThirst); }

    void Start() override { thirst = maxThirst; m_dehy = false; Publish(); }
    void Update(float dt) override {
        if (dt <= 0.0f) return;
        float m = sprinting ? sprintMultiplier : 1.0f;
        thirst = Mathf::Clamp(thirst - drainPerSecond * m * dt, 0.0f, maxThirst);
        dehydrated = thirst <= 0.0f; low = thirst <= lowThreshold;
        Signal(dehydrated, m_dehy, "dehydrated");
        Publish();
    }
    void Drink(float a)        { thirst = Mathf::Min(maxThirst, thirst + a); Publish(); }
    void SetSprinting(bool on) { sprinting = on; }
private:
    void Publish() { SaveValue("thirst", thirst); SetBar("ThirstBar", Fraction()); }
    bool m_dehy = false;
};

/// Stamina: sprint/jump cost, delayed regen, and an exhaustion lockout that clears
/// only once stamina recovers past `exhaustedUntil`.
class StaminaStat : public StatComponent {
public:
    float maxStamina = 100.0f, stamina = 100.0f;
    float regenPerSecond = 12.0f;
    float regenDelay = 1.0f;
    float sprintCost = 20.0f;
    float jumpCost = 15.0f;
    float exhaustedUntil = 20.0f;
    bool  sprinting = false, exhausted = false;

    float Fraction() const { return Frac(stamina, maxStamina); }
    bool  CanSprint() const { return stamina > 0.0f && !exhausted; }

    void Start() override { stamina = maxStamina; exhausted = false; m_timer = 0.0f; Publish(); }
    void Update(float dt) override {
        if (dt <= 0.0f) return;
        if (m_timer > 0.0f) m_timer -= dt;
        if (sprinting && stamina > 0.0f && !exhausted) {
            stamina -= sprintCost * dt; m_timer = regenDelay;
            if (stamina <= 0.0f) { stamina = 0.0f; exhausted = true; }
        } else if (m_timer <= 0.0f) {
            stamina = Mathf::Min(maxStamina, stamina + regenPerSecond * dt);
        }
        if (exhausted && stamina >= exhaustedUntil) exhausted = false;
        Publish();
    }
    bool TryJump() {
        if (stamina < jumpCost) return false;
        stamina -= jumpCost; m_timer = regenDelay; Publish();
        return true;
    }
    void SetSprinting(bool on) { sprinting = on; }
private:
    void Publish() { SaveValue("stamina", stamina); SetBar("StaminaBar", Fraction()); }
    float m_timer = 0.0f;
};

/// Oxygen that drains while submerged and refills breathing air; empty -> `drowning`.
class OxygenStat : public StatComponent {
public:
    float maxOxygen = 100.0f, oxygen = 100.0f;
    float drainPerSecond = 8.0f;
    float refillPerSecond = 25.0f;
    bool  submerged = false, outOfAir = false;

    float Fraction() const { return Frac(oxygen, maxOxygen); }

    void Start() override { oxygen = maxOxygen; m_drown = false; Publish(); }
    void Update(float dt) override {
        if (dt <= 0.0f) return;
        if (submerged) oxygen = Mathf::Max(0.0f, oxygen - drainPerSecond * dt);
        else           oxygen = Mathf::Min(maxOxygen, oxygen + refillPerSecond * dt);
        outOfAir = oxygen <= 0.0f;
        Signal(outOfAir, m_drown, "drowning");
        Publish();
    }
    void Breathe(float a)      { oxygen = Mathf::Min(maxOxygen, oxygen + a); Publish(); }
    void SetSubmerged(bool on) { submerged = on; }
private:
    void Publish() { SaveValue("oxygen", oxygen); SetBar("OxygenBar", Fraction()); }
    bool m_drown = false;
};

/// Warmth that drains when cold, recovers near a fire; empty -> `freezing`.
class TemperatureStat : public StatComponent {
public:
    float maxWarmth = 100.0f, warmth = 100.0f;
    float coldDrain = 3.0f;
    float warmRegen = 6.0f;
    bool  cold = false, nearFire = false, freezing = false;

    float Fraction() const { return Frac(warmth, maxWarmth); }

    void Start() override { warmth = maxWarmth; m_freeze = false; Publish(); }
    void Update(float dt) override {
        if (dt <= 0.0f) return;
        if (nearFire) warmth += warmRegen * dt;
        else if (cold) warmth -= coldDrain * dt;
        warmth = Mathf::Clamp(warmth, 0.0f, maxWarmth);
        freezing = warmth <= 0.0f;
        Signal(freezing, m_freeze, "freezing");
        Publish();
    }
    void Warm(float a)        { warmth = Mathf::Min(maxWarmth, warmth + a); Publish(); }
    void SetCold(bool on)     { cold = on; }
    void SetNearFire(bool on) { nearFire = on; }
private:
    void Publish() { SaveValue("warmth", warmth); SetBar("TemperatureBar", Fraction()); }
    bool m_freeze = false;
};

/// Energy that drains over time and recovers while resting; tired/exhausted flags.
class SleepStat : public StatComponent {
public:
    float maxEnergy = 100.0f, energy = 100.0f;
    float drainPerSecond = 0.5f;
    float restPerSecond = 20.0f;
    float tiredThreshold = 20.0f;
    bool  resting = false, exhausted = false, tired = false;

    float Fraction() const { return Frac(energy, maxEnergy); }

    void Start() override { energy = maxEnergy; m_exh = false; Publish(); }
    void Update(float dt) override {
        if (dt <= 0.0f) return;
        if (resting) energy += restPerSecond * dt;
        else         energy -= drainPerSecond * dt;
        energy = Mathf::Clamp(energy, 0.0f, maxEnergy);
        exhausted = energy <= 0.0f; tired = energy <= tiredThreshold;
        Signal(exhausted, m_exh, "exhausted");
        Publish();
    }
    void Rest(float a)       { energy = Mathf::Min(maxEnergy, energy + a); Publish(); }
    void SetResting(bool on) { resting = on; }
private:
    void Publish() { SaveValue("energy", energy); SetBar("EnergyBar", Fraction()); }
    bool m_exh = false;
};

/// Sanity that drains in danger/darkness and recovers when safe; empty -> `insane`.
class SanityStat : public StatComponent {
public:
    float maxSanity = 100.0f, sanity = 100.0f;
    float drainInDark = 4.0f;
    float regenInLight = 3.0f;
    float lowThreshold = 25.0f;
    bool  inDanger = false, insane = false, low = false;

    float Fraction() const { return Frac(sanity, maxSanity); }

    void Start() override { sanity = maxSanity; m_insane = false; Publish(); }
    void Update(float dt) override {
        if (dt <= 0.0f) return;
        if (inDanger) sanity -= drainInDark * dt;
        else          sanity += regenInLight * dt;
        sanity = Mathf::Clamp(sanity, 0.0f, maxSanity);
        insane = sanity <= 0.0f; low = sanity <= lowThreshold;
        Signal(insane, m_insane, "insane");
        Publish();
    }
    void Restore(float a)     { sanity = Mathf::Min(maxSanity, sanity + a); Publish(); }
    void SetInDanger(bool on) { inDanger = on; }
private:
    void Publish() { SaveValue("sanity", sanity); SetBar("SanityBar", Fraction()); }
    bool m_insane = false;
};

} // namespace okay
