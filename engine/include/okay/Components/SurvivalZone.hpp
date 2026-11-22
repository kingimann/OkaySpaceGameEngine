#pragma once
#include "okay/Components/SurvivalSystems.hpp"   // pulls Stats + Components + afflictions + helpers
#include "okay/Physics/Collider2D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include <string>

namespace okay {

/// A trigger volume that drives survival state on whatever enters it — no scripting.
/// Put it on an object with a trigger collider (2D or 3D); when a body carrying the
/// matching survival component enters, the zone applies its effect, and for the
/// "while inside" effects it clears the state again on exit.
///
/// Toggle effects (held while inside): Radiation, Water (wet + submerged), Cold, Fire
/// (warms), Danger (sanity), Submerged (oxygen). One-shot effects (fired on enter):
/// Poison (adds toxin), Status (applies a named timed effect), Damage, Heal.
class SurvivalZone : public Behaviour {
public:
    enum class Effect { Radiation, Water, Cold, Fire, Danger, Submerged, Poison, Status, Damage, Heal, Eat, Drink };
    int   effect = (int)Effect::Radiation;
    float amount = 25.0f;            // poison/damage/heal amount, or status hpPerSecond
    float duration = 5.0f;           // status effect duration (Status only)
    std::string effectName = "buff"; // status effect name (Status only)

    void OnTriggerEnter3D(Collider3D* o) override { if (o) Enter(o->gameObject); }
    void OnTriggerExit3D(Collider3D* o)  override { if (o) Exit(o->gameObject); }
    void OnTriggerEnter2D(Collider2D* o) override { if (o) Enter(o->gameObject); }
    void OnTriggerExit2D(Collider2D* o)  override { if (o) Exit(o->gameObject); }

    // Exposed so triggers can be simulated/tested and driven from code.
    void Enter(GameObject* g) { ApplyState(g, true); }
    void Exit(GameObject* g)  { ApplyState(g, false); }

private:
    void ApplyState(GameObject* g, bool inside) {
        if (!g) return;
        switch ((Effect)effect) {
        case Effect::Radiation:
            if (auto* c = g->GetComponent<RadiationStat>()) c->SetInRadiation(inside);
            break;
        case Effect::Water:
            if (auto* c = g->GetComponent<WetnessStat>()) c->SetInWater(inside);
            SetSubmerged(g, inside);
            break;
        case Effect::Cold:
            if (auto* s = g->GetComponent<SurvivalStats>())    s->SetCold(inside);
            if (auto* t = g->GetComponent<TemperatureStat>())  t->SetCold(inside);
            break;
        case Effect::Fire:   // a campfire: warm up while inside (TemperatureStat near-fire)
            if (auto* t = g->GetComponent<TemperatureStat>())  t->SetNearFire(inside);
            break;
        case Effect::Danger:
            if (auto* c = g->GetComponent<SanityStat>()) c->SetInDanger(inside);
            break;
        case Effect::Submerged:
            SetSubmerged(g, inside);
            break;
        case Effect::Poison:
            if (inside) if (auto* c = g->GetComponent<PoisonStat>()) c->Poison(amount);
            break;
        case Effect::Status:
            if (inside) if (auto* c = g->GetComponent<StatusEffectStat>())
                c->Apply(effectName, duration, amount);
            break;
        case Effect::Damage: if (inside) DamageHealthOn(g, amount); break;
        case Effect::Heal:   if (inside) HealOn(g, amount); break;
        case Effect::Eat:    // a berry bush / food source restores hunger
            if (inside) {
                if (auto* s = g->GetComponent<SurvivalStats>()) s->Eat(amount);
                else if (auto* h = g->GetComponent<HungerStat>()) h->Eat(amount);
            }
            break;
        case Effect::Drink:  // a well / water source restores thirst
            if (inside) {
                if (auto* s = g->GetComponent<SurvivalStats>()) s->Drink(amount);
                else if (auto* t = g->GetComponent<ThirstStat>()) t->Drink(amount);
            }
            break;
        }
    }
    static void SetSubmerged(GameObject* g, bool on) {
        if (auto* s = g->GetComponent<SurvivalStats>()) s->SetSubmerged(on);
        if (auto* o = g->GetComponent<OxygenStat>())    o->SetSubmerged(on);
    }
};

} // namespace okay
