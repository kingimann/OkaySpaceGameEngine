#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Components/UIProgressBar.hpp"
#include "okay/Components/ActionList.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Vec3.hpp"
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
    // ---- Pools ----
    float maxHealth = 100.0f, health = 100.0f;
    float lowThreshold = 25.0f;

    // ---- Damage mitigation ----
    float armor = 0.0f;            // flat reduction subtracted first
    float resistance = 0.0f;       // 0..1 fraction of the remaining damage ignored
    float minDamage = 0.0f;        // a hit always deals at least this (caps mitigation)
    bool  godMode = false;         // ignore all damage
    float invincibleTime = 0.0f;   // i-frames after a hit (seconds, 0 = off)

    // ---- Regeneration ----
    float regenPerSecond = 0.0f;   // passive heal (0 = off)
    float regenDelay = 0.0f;       // delay after a hit before regen resumes

    // ---- Overheal (Heal past max, decays back) ----
    float overhealMax = 0.0f;      // extra HP Heal may stack above maxHealth
    float overhealDecay = 0.0f;    // HP/s the part above maxHealth bleeds off

    // ---- Death / respawn ----
    bool  destroyOnDeath = false;  // remove the object instead of just deactivating
    bool  respawn = false;         // come back after respawnDelay
    float respawnDelay = 3.0f;
    int   lives = 0;               // respawns allowed (0 = unlimited)

    bool  dead = false, lowHealth = false;

    float Fraction() const { return Frac(health, maxHealth); }
    bool  IsDead()   const { return dead; }
    bool  IsInvincible() const { return godMode || m_iframes > 0.0f; }

    void Start() override {
        health = maxHealth; dead = false; m_timer = 0.0f; m_iframes = 0.0f;
        m_respawnTimer = 0.0f; m_livesLeft = lives;
        if (gameObject && gameObject->transform) m_start = gameObject->transform->Position();
        Publish();
    }
    void Update(float dt) override {
        if (dt <= 0.0f) return;
        if (m_iframes > 0.0f) m_iframes -= dt;
        if (dead) {
            if (respawn && m_respawnTimer > 0.0f) {
                m_respawnTimer -= dt;
                if (m_respawnTimer <= 0.0f) Respawn();
            }
            return;
        }
        if (m_timer > 0.0f) m_timer -= dt;
        if (regenPerSecond > 0.0f && m_timer <= 0.0f && health < maxHealth)
            health = Mathf::Min(maxHealth, health + regenPerSecond * dt);
        if (health > maxHealth && overhealDecay > 0.0f)
            health = Mathf::Max(maxHealth, health - overhealDecay * dt);
        lowHealth = health <= lowThreshold;
        Publish();
    }
    void Damage(float amount) {
        if (dead || godMode || m_iframes > 0.0f || amount <= 0.0f) return;
        float dmg = Mathf::Max(0.0f, amount - armor);
        dmg *= (1.0f - Mathf::Clamp01(resistance));
        if (dmg < minDamage) dmg = Mathf::Min(minDamage, amount);   // mitigation never fully nullifies a hit
        health -= dmg;
        m_timer = regenDelay;
        m_iframes = invincibleTime;
        if (health <= 0.0f) OnDeath();
        Publish();
    }
    void Heal(float a) {
        if (dead || a <= 0.0f) return;
        health = Mathf::Min(maxHealth + Mathf::Max(0.0f, overhealMax), health + a);
        Publish();
    }
    void AddArmor(float a) { armor += a; }
    void Revive()          { Respawn(); }
    void SetGodMode(bool on) { godMode = on; }

private:
    void OnDeath() {
        health = 0.0f; dead = true;
        Broadcast("died");
        if (gameObject) { if (auto* au = gameObject->GetComponent<AudioSource>()) au->Play(); }
        bool canRespawn = respawn && (lives <= 0 || m_livesLeft > 0);
        if (canRespawn) {
            if (lives > 0) --m_livesLeft;
            m_respawnTimer = respawnDelay;
            // Stay active (inert) so Update keeps ticking the respawn timer; a
            // deactivated object gets no Update. Hide visuals via the `died` message.
        } else if (destroyOnDeath && gameObject && gameObject->scene()) {
            gameObject->scene()->Destroy(gameObject);
        } else if (gameObject) {
            gameObject->active = false;
        }
    }
    void Respawn() {
        dead = false; health = maxHealth; m_timer = 0.0f; m_iframes = 0.0f; m_respawnTimer = 0.0f;
        if (gameObject) {
            gameObject->active = true;
            if (gameObject->transform && respawn) gameObject->transform->SetPosition(m_start);
        }
        Broadcast("respawned");
        Publish();
    }
    void Publish() { SaveValue("health", health); SetBar("HealthBar", Fraction()); }
    float m_timer = 0.0f, m_iframes = 0.0f, m_respawnTimer = 0.0f;
    int   m_livesLeft = 0;
    Vec3  m_start{0, 0, 0};
};

/// Hunger that drains over time (faster while sprinting); empty -> `starving`. When
/// empty it can damage a sibling HealthStat (`starveDamage`), slow the player
/// (`slowWhenStarving` + `SpeedFactor()`), and you can let it `overeatMax` past full.
/// `SetDrainScale` lets the world speed it up (cold, activity).
class HungerStat : public StatComponent {
public:
    float maxHunger = 100.0f, hunger = 100.0f;
    float drainPerSecond = 1.5f;
    float sprintMultiplier = 2.0f;
    float lowThreshold = 25.0f;
    float starveDamage = 0.0f;     // HP/s to a sibling HealthStat while empty (0 = off)
    float overeatMax = 0.0f;       // Eat may stack this far above maxHunger
    bool  slowWhenStarving = false;
    float minSpeedFactor = 0.5f;   // SpeedFactor() floor when fully starving
    bool  sprinting = false, starving = false, low = false;

    float Fraction() const { return Frac(hunger, maxHunger); }
    /// Movement multiplier for a controller to apply (1 when fed, → minSpeedFactor
    /// as hunger empties; always 1 unless `slowWhenStarving`).
    float SpeedFactor() const {
        return slowWhenStarving ? Mathf::Lerp(minSpeedFactor, 1.0f, Fraction()) : 1.0f;
    }
    /// Runtime drain multiplier (e.g. ×2 in the cold). Resets to 1 each frame's base.
    void SetDrainScale(float s) { drainScale = s; }

    void Start() override { hunger = maxHunger; m_starv = false; drainScale = 1.0f; Publish(); }
    void Update(float dt) override {
        if (dt <= 0.0f) return;
        float m = (sprinting ? sprintMultiplier : 1.0f) * drainScale;
        hunger = Mathf::Clamp(hunger - drainPerSecond * m * dt, 0.0f, maxHunger + Mathf::Max(0.0f, overeatMax));
        if (hunger > maxHunger) hunger = Mathf::Max(maxHunger, hunger - drainPerSecond * dt); // bleed off the overeat buffer
        starving = hunger <= 0.0f; low = hunger <= lowThreshold;
        if (starving && starveDamage > 0.0f && gameObject)
            if (auto* hp = gameObject->GetComponent<HealthStat>()) hp->Damage(starveDamage * dt);
        Signal(starving, m_starv, "starving");
        Publish();
    }
    void Eat(float a)          { hunger = Mathf::Min(maxHunger + Mathf::Max(0.0f, overeatMax), hunger + a); Publish(); }
    void SetSprinting(bool on) { sprinting = on; }
private:
    void Publish() { SaveValue("hunger", hunger); SetBar("HungerBar", Fraction()); }
    bool  m_starv = false;
    float drainScale = 1.0f;
};

/// Thirst that drains over time (faster while sprinting); empty -> `dehydrated`. Can
/// damage a sibling Health when empty, drink past full (`overdrinkMax`), slow the
/// player (`slowWhenDehydrated`), and be sped up by the world (`SetDrainScale`).
class ThirstStat : public StatComponent {
public:
    float maxThirst = 100.0f, thirst = 100.0f;
    float drainPerSecond = 2.0f;
    float sprintMultiplier = 2.0f;
    float lowThreshold = 25.0f;
    float dehydrateDamage = 0.0f;   // HP/s to a sibling HealthStat while empty
    float overdrinkMax = 0.0f;      // Drink may stack this far past full
    bool  slowWhenDehydrated = false;
    float minSpeedFactor = 0.5f;
    bool  sprinting = false, dehydrated = false, low = false;

    float Fraction() const { return Frac(thirst, maxThirst); }
    float SpeedFactor() const { return slowWhenDehydrated ? Mathf::Lerp(minSpeedFactor, 1.0f, Fraction()) : 1.0f; }
    void  SetDrainScale(float s) { drainScale = s; }

    void Start() override { thirst = maxThirst; m_dehy = false; drainScale = 1.0f; Publish(); }
    void Update(float dt) override {
        if (dt <= 0.0f) return;
        float m = (sprinting ? sprintMultiplier : 1.0f) * drainScale;
        thirst = Mathf::Clamp(thirst - drainPerSecond * m * dt, 0.0f, maxThirst + Mathf::Max(0.0f, overdrinkMax));
        if (thirst > maxThirst) thirst = Mathf::Max(maxThirst, thirst - drainPerSecond * dt);
        dehydrated = thirst <= 0.0f; low = thirst <= lowThreshold;
        if (dehydrated && dehydrateDamage > 0.0f && gameObject)
            if (auto* hp = gameObject->GetComponent<HealthStat>()) hp->Damage(dehydrateDamage * dt);
        Signal(dehydrated, m_dehy, "dehydrated");
        Publish();
    }
    void Drink(float a)        { thirst = Mathf::Min(maxThirst + Mathf::Max(0.0f, overdrinkMax), thirst + a); Publish(); }
    void SetSprinting(bool on) { sprinting = on; }
private:
    void Publish() { SaveValue("thirst", thirst); SetBar("ThirstBar", Fraction()); }
    bool  m_dehy = false;
    float drainScale = 1.0f;
};

/// Stamina: sprint/jump cost, delayed regen, and an exhaustion lockout that clears
/// only once stamina recovers past `exhaustedUntil`.
class StaminaStat : public StatComponent {
public:
    float maxStamina = 100.0f, stamina = 100.0f;
    float regenPerSecond = 12.0f;
    float regenScale = 1.0f;        // multiplier on regen (e.g. faster while resting)
    float regenDelay = 1.0f;
    float sprintCost = 20.0f;
    float jumpCost = 15.0f;
    float exhaustedUntil = 20.0f;
    float lowThreshold = 20.0f;
    bool  slowWhenExhausted = false;
    float minSpeedFactor = 0.5f;
    bool  sprinting = false, exhausted = false, low = false;

    float Fraction() const { return Frac(stamina, maxStamina); }
    bool  CanSprint() const { return stamina > 0.0f && !exhausted; }
    /// Movement multiplier (1 normally; eases to minSpeedFactor as stamina empties
    /// when `slowWhenExhausted`).
    float SpeedFactor() const { return slowWhenExhausted ? Mathf::Lerp(minSpeedFactor, 1.0f, Fraction()) : 1.0f; }
    void  SetDrainScale(float s) { drainScale = s; }

    void Start() override { stamina = maxStamina; exhausted = false; m_timer = 0.0f; drainScale = 1.0f; Publish(); }
    void Update(float dt) override {
        if (dt <= 0.0f) return;
        if (m_timer > 0.0f) m_timer -= dt;
        if (sprinting && stamina > 0.0f && !exhausted) {
            stamina -= sprintCost * drainScale * dt; m_timer = regenDelay;
            if (stamina <= 0.0f) { stamina = 0.0f; exhausted = true; }
        } else if (m_timer <= 0.0f) {
            stamina = Mathf::Min(maxStamina, stamina + regenPerSecond * regenScale * dt);
        }
        if (exhausted && stamina >= exhaustedUntil) exhausted = false;
        low = stamina <= lowThreshold;
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
    float drainScale = 1.0f;
};

/// Oxygen that drains while submerged and refills breathing air; empty -> `drowning`.
class OxygenStat : public StatComponent {
public:
    float maxOxygen = 100.0f, oxygen = 100.0f;
    float drainPerSecond = 8.0f;
    float refillPerSecond = 25.0f;
    float lowThreshold = 25.0f;
    float drownDamage = 0.0f;       // HP/s to a sibling HealthStat while out of air
    bool  submerged = false, outOfAir = false, low = false;

    float Fraction() const { return Frac(oxygen, maxOxygen); }
    void  SetDrainScale(float s) { drainScale = s; }

    void Start() override { oxygen = maxOxygen; m_drown = false; drainScale = 1.0f; Publish(); }
    void Update(float dt) override {
        if (dt <= 0.0f) return;
        if (submerged) oxygen = Mathf::Max(0.0f, oxygen - drainPerSecond * drainScale * dt);
        else           oxygen = Mathf::Min(maxOxygen, oxygen + refillPerSecond * dt);
        outOfAir = oxygen <= 0.0f; low = oxygen <= lowThreshold;
        if (outOfAir && drownDamage > 0.0f && gameObject)
            if (auto* hp = gameObject->GetComponent<HealthStat>()) hp->Damage(drownDamage * dt);
        Signal(outOfAir, m_drown, "drowning");
        Publish();
    }
    void Breathe(float a)      { oxygen = Mathf::Min(maxOxygen, oxygen + a); Publish(); }
    void SetSubmerged(bool on) { submerged = on; }
private:
    void Publish() { SaveValue("oxygen", oxygen); SetBar("OxygenBar", Fraction()); }
    bool  m_drown = false;
    float drainScale = 1.0f;
};

/// Warmth that drains when cold, recovers near a fire; empty -> `freezing`.
class TemperatureStat : public StatComponent {
public:
    float maxWarmth = 100.0f, warmth = 100.0f;
    float coldDrain = 3.0f;
    float warmRegen = 6.0f;
    float lowThreshold = 25.0f;
    float freezeDamage = 0.0f;      // HP/s to a sibling HealthStat while frozen
    bool  cold = false, nearFire = false, freezing = false, low = false;

    float Fraction() const { return Frac(warmth, maxWarmth); }
    void  SetDrainScale(float s) { drainScale = s; }

    void Start() override { warmth = maxWarmth; m_freeze = false; drainScale = 1.0f; Publish(); }
    void Update(float dt) override {
        if (dt <= 0.0f) return;
        if (nearFire) warmth += warmRegen * dt;
        else if (cold) warmth -= coldDrain * drainScale * dt;
        warmth = Mathf::Clamp(warmth, 0.0f, maxWarmth);
        freezing = warmth <= 0.0f; low = warmth <= lowThreshold;
        if (freezing && freezeDamage > 0.0f && gameObject)
            if (auto* hp = gameObject->GetComponent<HealthStat>()) hp->Damage(freezeDamage * dt);
        Signal(freezing, m_freeze, "freezing");
        Publish();
    }
    void Warm(float a)        { warmth = Mathf::Min(maxWarmth, warmth + a); Publish(); }
    void SetCold(bool on)     { cold = on; }
    void SetNearFire(bool on) { nearFire = on; }
private:
    void Publish() { SaveValue("warmth", warmth); SetBar("TemperatureBar", Fraction()); }
    bool  m_freeze = false;
    float drainScale = 1.0f;
};

/// Energy that drains over time and recovers while resting; tired/exhausted flags.
class SleepStat : public StatComponent {
public:
    float maxEnergy = 100.0f, energy = 100.0f;
    float drainPerSecond = 0.5f;
    float restPerSecond = 20.0f;
    float tiredThreshold = 20.0f;
    float exhaustDamage = 0.0f;     // HP/s to a sibling HealthStat while at 0 energy
    bool  slowWhenTired = false;
    float minSpeedFactor = 0.5f;
    bool  resting = false, exhausted = false, tired = false;

    float Fraction() const { return Frac(energy, maxEnergy); }
    float SpeedFactor() const { return slowWhenTired ? Mathf::Lerp(minSpeedFactor, 1.0f, Fraction()) : 1.0f; }
    void  SetDrainScale(float s) { drainScale = s; }

    void Start() override { energy = maxEnergy; m_exh = false; drainScale = 1.0f; Publish(); }
    void Update(float dt) override {
        if (dt <= 0.0f) return;
        if (resting) energy += restPerSecond * dt;
        else         energy -= drainPerSecond * drainScale * dt;
        energy = Mathf::Clamp(energy, 0.0f, maxEnergy);
        exhausted = energy <= 0.0f; tired = energy <= tiredThreshold;
        if (exhausted && exhaustDamage > 0.0f && gameObject)
            if (auto* hp = gameObject->GetComponent<HealthStat>()) hp->Damage(exhaustDamage * dt);
        Signal(exhausted, m_exh, "exhausted");
        Publish();
    }
    void Rest(float a)       { energy = Mathf::Min(maxEnergy, energy + a); Publish(); }
    void SetResting(bool on) { resting = on; }
private:
    void Publish() { SaveValue("energy", energy); SetBar("EnergyBar", Fraction()); }
    bool  m_exh = false;
    float drainScale = 1.0f;
};

/// Sanity that drains in danger/darkness and recovers when safe; empty -> `insane`.
class SanityStat : public StatComponent {
public:
    float maxSanity = 100.0f, sanity = 100.0f;
    float drainInDark = 4.0f;
    float regenInLight = 3.0f;
    float lowThreshold = 25.0f;
    float insaneDamage = 0.0f;      // HP/s to a sibling HealthStat while insane
    bool  inDanger = false, insane = false, low = false;

    float Fraction() const { return Frac(sanity, maxSanity); }
    void  SetDrainScale(float s) { drainScale = s; }

    void Start() override { sanity = maxSanity; m_insane = false; drainScale = 1.0f; Publish(); }
    void Update(float dt) override {
        if (dt <= 0.0f) return;
        if (inDanger) sanity -= drainInDark * drainScale * dt;
        else          sanity += regenInLight * dt;
        sanity = Mathf::Clamp(sanity, 0.0f, maxSanity);
        insane = sanity <= 0.0f; low = sanity <= lowThreshold;
        if (insane && insaneDamage > 0.0f && gameObject)
            if (auto* hp = gameObject->GetComponent<HealthStat>()) hp->Damage(insaneDamage * dt);
        Signal(insane, m_insane, "insane");
        Publish();
    }
    void Restore(float a)     { sanity = Mathf::Min(maxSanity, sanity + a); Publish(); }
    void SetInDanger(bool on) { inDanger = on; }
private:
    void Publish() { SaveValue("sanity", sanity); SetBar("SanityBar", Fraction()); }
    bool  m_insane = false;
    float drainScale = 1.0f;
};

} // namespace okay
