#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Components/UIProgressBar.hpp"
#include "okay/Components/ActionList.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Math/Mathf.hpp"

namespace okay {

/// All-in-one survival stats as a native component — the C++ sibling of the
/// Survival Kit's "Survival" script. Health, hunger, thirst, stamina, oxygen and
/// temperature are wired together and ticked each frame: hunger/thirst/oxygen and
/// warmth, when empty, call Damage() **directly** (no script VM, no message round-
/// trip), armor soaks incoming damage, and health regenerates while fed + hydrated
/// after a post-hit delay.
///
/// Drop it on the Player and tune in the Inspector. Each frame it publishes every
/// stat to a saved value (bind UI text with `{health}`, `{hunger}`, …) and fills a
/// same-named progress bar if one exists (`HealthBar`, `HungerBar`, `ThirstBar`,
/// `StaminaBar`, `OxygenBar`, `TemperatureBar`). On death it broadcasts `died`,
/// plays a sibling AudioSource, and deactivates the object; on each stat hitting
/// zero it broadcasts once (`starving`, `dehydrated`, `drowning`, `freezing`) for an
/// ActionList OnMessage to catch. Methods (Eat/Drink/Heal/…) are callable from a
/// Button On Click, an Action List `call`, or another component.
class SurvivalStats : public Behaviour {
public:
    // ---- Health ----
    float maxHealth = 100.0f, health = 100.0f;
    float armor = 0.0f;
    float resistance = 0.0f;       // 0..1 fraction of post-armor damage ignored
    bool  godMode = false;         // ignore all damage
    float invincibleTime = 0.0f;   // i-frames after a hit (seconds, 0 = off)
    float regenWhenFed = 1.0f;     // HP/s regained while fed + hydrated
    float regenDelay   = 5.0f;     // pause regen this long after a hit
    bool  dead = false;

    // ---- Hunger ----
    float maxHunger = 100.0f, hunger = 100.0f;
    float hungerDrain  = 1.5f;
    float starveDamage = 2.0f;     // HP/s lost while starving

    // ---- Thirst ----
    float maxThirst = 100.0f, thirst = 100.0f;
    float thirstDrain    = 2.0f;
    float dehydrateDamage = 3.0f;

    // ---- Stamina ----
    float maxStamina = 100.0f, stamina = 100.0f;
    float staminaRegen   = 12.0f;
    float sprintCost     = 20.0f;
    float sprintDrainMult = 2.0f;  // hunger/thirst drain faster while sprinting
    bool  sprinting = false;

    // ---- Oxygen ----
    float maxOxygen = 100.0f, oxygen = 100.0f;
    float oxygenDrain  = 8.0f;
    float oxygenRefill = 25.0f;
    float drownDamage  = 5.0f;
    bool  submerged = false;

    // ---- Temperature ----
    float maxWarmth = 100.0f, warmth = 100.0f;
    float coldDrain   = 3.0f;
    float warmRegen   = 6.0f;
    float freezeDamage = 2.0f;
    bool  cold = false;

    // ---- Output ----
    bool publishPrefs = true;      // write saved values each frame
    bool publishBars  = true;      // fill same-named *Bar progress widgets
    bool sendMessages = true;      // broadcast died/starving/… to ActionLists

    // 0..1 fractions for HUDs / logic.
    float Fraction()        const { return Frac(health,  maxHealth); }
    float HungerFraction()  const { return Frac(hunger,  maxHunger); }
    float ThirstFraction()  const { return Frac(thirst,  maxThirst); }
    float StaminaFraction() const { return Frac(stamina, maxStamina); }
    float OxygenFraction()  const { return Frac(oxygen,  maxOxygen); }
    float WarmthFraction()  const { return Frac(warmth,  maxWarmth); }
    bool  IsDead()    const { return dead; }
    bool  CanSprint() const { return stamina > 0.0f; }

    void Start() override {
        health = maxHealth; hunger = maxHunger; thirst = maxThirst;
        stamina = maxStamina; oxygen = maxOxygen; warmth = maxWarmth;
        dead = false; m_regenTimer = 0.0f; m_iframes = 0.0f;
        m_starv = m_dehy = m_drown = m_freeze = false;
        Publish();
    }

    void Update(float dt) override {
        if (dead || dt <= 0.0f) return;
        if (m_iframes > 0.0f) m_iframes -= dt;
        float drainMult = (sprinting && stamina > 0.0f) ? sprintDrainMult : 1.0f;

        hunger = Mathf::Clamp(hunger - hungerDrain * drainMult * dt, 0.0f, maxHunger);
        thirst = Mathf::Clamp(thirst - thirstDrain * drainMult * dt, 0.0f, maxThirst);

        if (sprinting && stamina > 0.0f)
            stamina = Mathf::Max(0.0f, stamina - sprintCost * dt);
        else
            stamina = Mathf::Min(maxStamina, stamina + staminaRegen * dt);

        if (submerged) oxygen = Mathf::Max(0.0f, oxygen - oxygenDrain * dt);
        else           oxygen = Mathf::Min(maxOxygen, oxygen + oxygenRefill * dt);

        if (cold) warmth = Mathf::Max(0.0f, warmth - coldDrain * dt);
        else      warmth = Mathf::Min(maxWarmth, warmth + warmRegen * dt);

        // Empty stats damage health directly (latched messages, fired once).
        bool tookEnv = false;
        tookEnv |= Drain(hunger,  starveDamage,    dt, m_starv,  "starving");
        tookEnv |= Drain(thirst,  dehydrateDamage, dt, m_dehy,   "dehydrated");
        tookEnv |= Drain(oxygen,  drownDamage,     dt, m_drown,  "drowning");
        tookEnv |= Drain(warmth,  freezeDamage,    dt, m_freeze, "freezing");

        if (m_regenTimer > 0.0f) m_regenTimer -= dt;
        if (!tookEnv && m_regenTimer <= 0.0f && hunger > 0.0f && thirst > 0.0f && regenWhenFed > 0.0f)
            health = Mathf::Min(maxHealth, health + regenWhenFed * dt);

        Publish();
    }

    // ---- Actions (callable from On Click / Action List / code) ----
    void Damage(float amount) {
        if (dead || godMode || m_iframes > 0.0f || amount <= 0.0f) return;
        float dmg = Mathf::Max(0.0f, amount - armor) * (1.0f - Mathf::Clamp01(resistance));
        health -= dmg;
        m_regenTimer = regenDelay;
        m_iframes = invincibleTime;
        if (health <= 0.0f) { health = 0.0f; dead = true; OnDeath(); }
    }
    void SetGodMode(bool on) { godMode = on; }
    bool IsInvincible() const { return godMode || m_iframes > 0.0f; }
    void Heal(float a)    { if (!dead) health = Mathf::Min(maxHealth, health + a); }
    void Eat(float a)     { hunger = Mathf::Min(maxHunger, hunger + a); if (a > 0) m_starv = false; }
    void Drink(float a)   { thirst = Mathf::Min(maxThirst, thirst + a); if (a > 0) m_dehy = false; }
    void Breathe(float a) { oxygen = Mathf::Min(maxOxygen, oxygen + a); if (a > 0) m_drown = false; }
    void Warm(float a)    { warmth = Mathf::Min(maxWarmth, warmth + a); if (a > 0) m_freeze = false; }
    void AddArmor(float a){ armor += a; }
    void Revive() {
        dead = false; health = maxHealth; m_regenTimer = 0.0f;
        if (gameObject) gameObject->active = true;
    }
    void SetSprinting(bool on) { sprinting = on; }
    void SetSubmerged(bool on) { submerged = on; }
    void SetCold(bool on)      { cold = on; }

private:
    static float Frac(float v, float mx) { return mx > 0.0f ? Mathf::Clamp01(v / mx) : 0.0f; }

    // If `stat` is empty, apply `dps` damage and fire `msg` once. Returns true if it
    // bit this frame (so health regen stays paused).
    bool Drain(float stat, float dps, float dt, bool& latch, const char* msg) {
        if (stat > 0.0f) { latch = false; return false; }
        Damage(dps * dt);
        if (!latch) { latch = true; Broadcast(msg); }
        return true;
    }
    void Publish() {
        if (publishPrefs) {
            Prefs::SetFloat("health",  health);  Prefs::SetFloat("hunger",  hunger);
            Prefs::SetFloat("thirst",  thirst);  Prefs::SetFloat("stamina", stamina);
            Prefs::SetFloat("oxygen",  oxygen);  Prefs::SetFloat("warmth",  warmth);
        }
        if (publishBars) {
            SetBar("HealthBar", Fraction());        SetBar("HungerBar", HungerFraction());
            SetBar("ThirstBar", ThirstFraction());  SetBar("StaminaBar", StaminaFraction());
            SetBar("OxygenBar", OxygenFraction());   SetBar("TemperatureBar", WarmthFraction());
        }
    }
    void SetBar(const char* name, float frac) {
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        if (!s) return;
        if (GameObject* g = s->Find(name))
            if (auto* pb = g->GetComponent<UIProgressBar>()) pb->SetValue(frac);
    }
    void Broadcast(const std::string& msg) {
        if (!sendMessages) return;
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        if (!s) return;
        for (ActionList* al : s->FindObjectsOfType<ActionList>()) al->ReceiveMessage(msg);
    }
    void OnDeath() {
        Broadcast("died");
        if (gameObject) {
            if (auto* au = gameObject->GetComponent<AudioSource>()) au->Play();
            gameObject->active = false;
        }
    }

    float m_regenTimer = 0.0f;
    float m_iframes = 0.0f;
    bool  m_starv = false, m_dehy = false, m_drown = false, m_freeze = false;
};

} // namespace okay
