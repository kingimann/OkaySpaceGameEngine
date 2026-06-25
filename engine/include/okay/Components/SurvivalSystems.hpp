#pragma once
#include "okay/Components/SurvivalAfflictions.hpp"   // pulls Stats + Components + helpers
#include <string>
#include <vector>

namespace okay {

/// Encumbrance: items add `load`; over `maxLoad` the object is encumbered — it drains
/// the stamina source and reports a `SpeedFactor()` a controller can multiply into
/// movement. Publishes `load` / `LoadBar` and broadcasts `encumbered` once.
class CarryWeightStat : public StatComponent {
public:
    float maxLoad = 50.0f, load = 0.0f;
    float overStaminaDrain = 5.0f;   // stamina/s drained while over the limit
    float minSpeedFactor = 0.4f;     // slowest movement multiplier when very overloaded
    bool  encumbered = false;

    float Fraction() const { return Frac(load, maxLoad); }
    bool  OverLimit() const { return load > maxLoad; }
    /// 1.0 under the limit, easing down to minSpeedFactor as overload grows.
    float SpeedFactor() const {
        if (load <= maxLoad || maxLoad <= 0.0f) return 1.0f;
        float over = (load - maxLoad) / maxLoad;          // 0..1+ past the limit
        return Mathf::Max(minSpeedFactor, 1.0f - Mathf::Clamp01(over));
    }

    void Start() override { m_enc = false; Publish(); }
    void Update(float dt) override {
        if (dt <= 0.0f) return;
        encumbered = OverLimit();
        if (encumbered && overStaminaDrain > 0.0f)
            DrainStaminaOn(gameObject, overStaminaDrain * dt);
        Signal(encumbered, m_enc, "encumbered");
        Publish();
    }
    void AddLoad(float a)    { load = Mathf::Max(0.0f, load + a); }
    void RemoveLoad(float a) { load = Mathf::Max(0.0f, load - a); }
    void SetLoad(float a)    { load = Mathf::Max(0.0f, a); }
private:
    void Publish() { SaveValue("load", load); SetBar("LoadBar", Fraction()); }
    bool m_enc = false;
};

/// Generic timed buff/debuff layer. Apply named effects with a duration and a
/// per-second health delta (negative = damage, positive = heal); they tick the
/// health source each frame and auto-expire. Driven from code or triggers (a fire
/// applies "burning", a potion applies "regen", …).
class StatusEffectStat : public Behaviour {
public:
    struct Effect { std::string name; float remaining = 0.0f; float hpPerSecond = 0.0f; };
    bool sendMessages = true;        // broadcast <name> on apply, <name>_expired on end

    /// Apply (or refresh) an effect. Re-applying the same name resets its timer.
    void Apply(const std::string& name, float duration, float hpPerSecond) {
        for (auto& e : m_effects)
            if (e.name == name) { e.remaining = duration; e.hpPerSecond = hpPerSecond; return; }
        m_effects.push_back({name, duration, hpPerSecond});
        Broadcast(name);
    }
    void Remove(const std::string& name) {
        for (size_t i = 0; i < m_effects.size(); ++i)
            if (m_effects[i].name == name) { m_effects.erase(m_effects.begin() + i); Broadcast(name + "_ended"); return; }
    }
    void Clear() { m_effects.clear(); }
    bool  Has(const std::string& name) const {
        for (const auto& e : m_effects) if (e.name == name) return true;
        return false;
    }
    float Remaining(const std::string& name) const {
        for (const auto& e : m_effects) if (e.name == name) return e.remaining;
        return 0.0f;
    }
    int ActiveCount() const { return (int)m_effects.size(); }

    void Update(float dt) override {
        if (dt <= 0.0f) return;
        for (size_t i = 0; i < m_effects.size(); ) {
            Effect& e = m_effects[i];
            if (e.hpPerSecond < 0.0f)      DamageHealthOn(gameObject, -e.hpPerSecond * dt);
            else if (e.hpPerSecond > 0.0f) HealOn(gameObject, e.hpPerSecond * dt);
            e.remaining -= dt;
            if (e.remaining <= 0.0f) {
                std::string n = e.name;
                m_effects.erase(m_effects.begin() + i);
                Broadcast(n + "_expired");
            } else ++i;
        }
    }
private:
    void Broadcast(const std::string& msg) {
        if (!sendMessages) return;
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        if (!s) return;
        for (ActionList* al : s->FindObjectsOfType<ActionList>()) al->ReceiveMessage(msg);
    }
    std::vector<Effect> m_effects;
};

/// Persists live stat values across sessions. On first frame (if `loadOnStart`) it
/// pulls saved current values into the sibling stat components; Save() writes them to
/// a `<saveKey>.okayprefs` file. Add it alongside the stat components on the Player.
class SurvivalSave : public Behaviour {
public:
    std::string saveKey = "survival";   // file = saveKey + ".okayprefs"
    bool loadOnStart = true;            // restore saved values on the first frame
    bool saveContinuously = false;      // mirror current values to Prefs every frame

    void Start() override { m_pendingLoad = loadOnStart; }
    void Update(float) override {
        if (m_pendingLoad) { Load(); m_pendingLoad = false; }
        if (saveContinuously) Capture();
    }
    /// Capture current values and write the save file.
    void Save() { Capture(); Prefs::Save(File()); }
    /// Load the save file and apply saved values to the sibling stats.
    bool Load() { if (!Prefs::Load(File())) return false; Apply(); return true; }

private:
    std::string File() const { return saveKey + ".okayprefs"; }

    // Mirror every present component's current value into Prefs.
    void Capture() {
        GameObject* g = gameObject; if (!g) return;
        if (auto* c = g->GetComponent<SurvivalStats>()) {
            P("health", c->health); P("hunger", c->hunger); P("thirst", c->thirst);
            P("stamina", c->stamina); P("oxygen", c->oxygen); P("warmth", c->warmth);
        }
        if (auto* c = g->GetComponent<HealthStat>())      P("health", c->health);
        if (auto* c = g->GetComponent<HungerStat>())      P("hunger", c->hunger);
        if (auto* c = g->GetComponent<ThirstStat>())      P("thirst", c->thirst);
        if (auto* c = g->GetComponent<StaminaStat>())     P("stamina", c->stamina);
        if (auto* c = g->GetComponent<OxygenStat>())      P("oxygen", c->oxygen);
        if (auto* c = g->GetComponent<TemperatureStat>()) P("warmth", c->warmth);
        if (auto* c = g->GetComponent<SleepStat>())       P("energy", c->energy);
        if (auto* c = g->GetComponent<SanityStat>())      P("sanity", c->sanity);
        if (auto* c = g->GetComponent<RadiationStat>())   P("radiation", c->radiation);
        if (auto* c = g->GetComponent<BleedingStat>())    P("bleed", c->bleed);
        if (auto* c = g->GetComponent<PoisonStat>())      P("poison", c->poison);
        if (auto* c = g->GetComponent<WetnessStat>())     P("wetness", c->wetness);
        if (auto* c = g->GetComponent<CarryWeightStat>()) P("load", c->load);
    }
    // Apply saved Prefs values back onto each present component (missing keys keep
    // the component's Start() value via the supplied default).
    void Apply() {
        GameObject* g = gameObject; if (!g) return;
        if (auto* c = g->GetComponent<SurvivalStats>()) {
            c->health = G("health", c->health); c->hunger = G("hunger", c->hunger);
            c->thirst = G("thirst", c->thirst); c->stamina = G("stamina", c->stamina);
            c->oxygen = G("oxygen", c->oxygen); c->warmth = G("warmth", c->warmth);
        }
        if (auto* c = g->GetComponent<HealthStat>())      c->health = G("health", c->health);
        if (auto* c = g->GetComponent<HungerStat>())      c->hunger = G("hunger", c->hunger);
        if (auto* c = g->GetComponent<ThirstStat>())      c->thirst = G("thirst", c->thirst);
        if (auto* c = g->GetComponent<StaminaStat>())     c->stamina = G("stamina", c->stamina);
        if (auto* c = g->GetComponent<OxygenStat>())      c->oxygen = G("oxygen", c->oxygen);
        if (auto* c = g->GetComponent<TemperatureStat>()) c->warmth = G("warmth", c->warmth);
        if (auto* c = g->GetComponent<SleepStat>())       c->energy = G("energy", c->energy);
        if (auto* c = g->GetComponent<SanityStat>())      c->sanity = G("sanity", c->sanity);
        if (auto* c = g->GetComponent<RadiationStat>())   c->radiation = G("radiation", c->radiation);
        if (auto* c = g->GetComponent<BleedingStat>())    c->bleed = G("bleed", c->bleed);
        if (auto* c = g->GetComponent<PoisonStat>())      c->poison = G("poison", c->poison);
        if (auto* c = g->GetComponent<WetnessStat>())     c->wetness = G("wetness", c->wetness);
        if (auto* c = g->GetComponent<CarryWeightStat>()) c->load = G("load", c->load);
    }
    static void  P(const char* k, float v) { Prefs::SetFloat(k, v); }
    static float G(const char* k, float def) { return Prefs::GetFloat(k, def); }
    bool m_pendingLoad = false;
};

} // namespace okay
