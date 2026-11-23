#pragma once
#include "okay/Components/Damage.hpp"
#include "okay/Components/SurvivalComponents.hpp"
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
/// Survival Kit's "Survival" script. Hunger, thirst, stamina, oxygen and
/// temperature are wired together and ticked each frame: hunger/thirst/oxygen and
/// warmth, when empty, damage health **directly** (no script VM, no message round-
/// trip), and health regenerates while fed + hydrated after the post-hit delay.
///
/// Health itself is the object's Health Stat (one health per object): adding Survival
/// Stats adds a Health Stat when there is none, and max health, armor, resistance, god
/// mode, i-frames, the regen delay, death and respawn are set there. Scenes saved before
/// format 4 kept those values here; the loader moves them into the Health Stat.
///
/// Drop it on the Player and tune in the Inspector. Each frame it publishes every
/// stat to a saved value (bind UI text with `{hunger}`, `{thirst}`, …; the Health Stat
/// publishes `{health}`) and fills a same-named progress bar if one exists
/// (`HungerBar`, `ThirstBar`, `StaminaBar`, `OxygenBar`, `TemperatureBar`; the Health
/// Stat fills `HealthBar`). On each stat hitting zero it broadcasts once (`starving`,
/// `dehydrated`, `drowning`, `freezing`) for an ActionList OnMessage to catch; the
/// Health Stat broadcasts `died`. Methods (Eat/Drink/…) are callable from a Button On
/// Click, an Action List `call`, or another component.
class SurvivalStats : public Behaviour {
public:
    float regenWhenFed = 1.0f;     // HP/s given to the Health Stat while fed + hydrated (after its Regen Delay)

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
    bool sendMessages = true;      // broadcast starving/dehydrated/… to ActionLists

    // 0..1 fractions for HUDs / logic.
    float HungerFraction()  const { return Frac(hunger,  maxHunger); }
    float ThirstFraction()  const { return Frac(thirst,  maxThirst); }
    float StaminaFraction() const { return Frac(stamina, maxStamina); }
    float OxygenFraction()  const { return Frac(oxygen,  maxOxygen); }
    float WarmthFraction()  const { return Frac(warmth,  maxWarmth); }
    bool  CanSprint() const { return stamina > 0.0f; }
    /// The object's one health (null only if someone removed it).
    HealthStat* Health() const { return gameObject ? gameObject->GetComponent<HealthStat>() : nullptr; }
    /// The Health Stat defaults a Survival Stats player had before health moved there: regen waits 5 s after
    /// a hit, and death switches the player off. Used when Survival Stats adds the Health Stat.
    static void SetUpHealth(HealthStat& h) { h.regenDelay = 5.0f; h.SetDeathAction(HealthStat::DeathAction::Disable); }

    void Start() override {
        hunger = maxHunger; thirst = maxThirst;
        stamina = maxStamina; oxygen = maxOxygen; warmth = maxWarmth;
        m_starv = m_dehy = m_drown = m_freeze = false;
        Publish();
    }

    void Update(float dt) override {
        HealthStat* hp = Health();
        if ((hp && hp->IsDead()) || dt <= 0.0f) return;
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
        tookEnv |= Drain(hunger,  starveDamage,    dt, m_starv,  "starving",    DamageType::Hunger);
        tookEnv |= Drain(thirst,  dehydrateDamage, dt, m_dehy,   "dehydrated",  DamageType::Thirst);
        tookEnv |= Drain(oxygen,  drownDamage,     dt, m_drown,  "drowning",    DamageType::Drowning);
        tookEnv |= Drain(warmth,  freezeDamage,    dt, m_freeze, "freezing",    DamageType::Cold);

        if (hp && !tookEnv && !hp->RegenPaused() && hunger > 0.0f && thirst > 0.0f && regenWhenFed > 0.0f)
            hp->Regenerate(regenWhenFed * dt);

        Publish();
    }

    // ---- Actions (callable from On Click / Action List / code) ----
    void Eat(float a)     { hunger = Mathf::Min(maxHunger, hunger + a); if (a > 0) m_starv = false; }
    void Drink(float a)   { thirst = Mathf::Min(maxThirst, thirst + a); if (a > 0) m_dehy = false; }
    void Breathe(float a) { oxygen = Mathf::Min(maxOxygen, oxygen + a); if (a > 0) m_drown = false; }
    void Warm(float a)    { warmth = Mathf::Min(maxWarmth, warmth + a); if (a > 0) m_freeze = false; }
    void SetSprinting(bool on) { sprinting = on; }
    void SetSubmerged(bool on) { submerged = on; }
    void SetCold(bool on)      { cold = on; }

private:
    static float Frac(float v, float mx) { return mx > 0.0f ? Mathf::Clamp01(v / mx) : 0.0f; }

    // If `stat` is empty, apply `dps` damage and fire `msg` once. Returns true if it
    // bit this frame (so health regen stays paused).
    bool Drain(float stat, float dps, float dt, bool& latch, const char* msg, DamageType type) {
        if (stat > 0.0f) { latch = false; return false; }
        DealDamage(gameObject, dps * dt, type);
        if (!latch) { latch = true; Broadcast(msg); }
        return true;
    }
    void Publish() {
        if (publishPrefs) {
            Prefs::SetFloat("hunger",  hunger);  Prefs::SetFloat("thirst",  thirst);
            Prefs::SetFloat("stamina", stamina); Prefs::SetFloat("oxygen",  oxygen);
            Prefs::SetFloat("warmth",  warmth);
        }
        if (publishBars) {
            SetBar("HungerBar", HungerFraction());
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
    bool  m_starv = false, m_dehy = false, m_drown = false, m_freeze = false;
};

namespace detail { inline const bool kSurvivalStatsHealthRule = RequireHealthStat<SurvivalStats, &SurvivalStats::SetUpHealth>(); }

} // namespace okay
