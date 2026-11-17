// Module health_damage: the HealthStat adapter for the health contract, the HitZones damage modifier and
// the `hitzones` scene record. Linked only when the module is on (cmake/OkayModules.cmake).
#include "okay/Components/HitZones.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Modules/Health.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Scene/ComponentSerializer.hpp"
#include <algorithm>
#include <cstdlib>
#include <istream>
#include <ostream>
#include <sstream>

namespace okay {
namespace {

class HealthStatAdapter : public IHealth {
public:
    explicit HealthStatAdapter(HealthStat* h) : h_(h) {}
    float Current() const override { return h_->health; }
    float Max() const override { return h_->maxHealth; }
    float Armor() const override { return h_->armor + h_->equipArmor; }
    bool IsDead() const override { return h_->IsDead(); }
    bool IsInvincible() const override { return h_->IsInvincible(); }
    const char* Model() const override { return "HealthStat"; }
    DamageResult ApplyDamage(const DamageEvent& e) override {
        DamageResult r;
        r.handled = true;
        if (h_->IsDead() || h_->IsInvincible() || !(e.amount > 0.0f)) { r.blocked = true; return r; }
        const float before = h_->health;
        if (e.hasOrigin) h_->Damage(e.amount, e.origin); else h_->Damage(e.amount);
        r.applied = std::max(0.0f, before - h_->health);
        r.killed = h_->IsDead();
        r.blocked = r.applied <= 0.0f && !r.killed;
        return r;
    }
    float Heal(float amount, GameObject*) override {
        const float before = h_->health;
        h_->Heal(amount);
        return std::max(0.0f, h_->health - before);
    }
    void Kill(const Vec3* from) override { h_->KillFromAction(from != nullptr, from ? *from : Vec3{}); }
    bool Revive() override { h_->Revive(); return true; }
private:
    HealthStat* h_;
};

// Scene record: "  hitzones head=2 body=1 legs=0.75 hf=1.5 lt=0.5 t.fire=0.5". Zone names may not contain spaces.
void WriteHitZones(const GameObject& go, std::ostream& out) {
    for (auto* hz : go.GetComponents<HitZones>()) {
        out << "  hitzones hf=" << hz->headFromHeight << " lt=" << hz->legsToHeight;
        for (auto& z : hz->zones) {
            std::string n = z.name;
            std::replace(n.begin(), n.end(), ' ', '_');
            out << " z." << n << "=" << z.scale;
        }
        for (int i = 0; i < kDamageTypeCount; ++i)
            if (hz->typeScale[i] != 1.0f) out << " t." << DamageTypeName((DamageType)i) << "=" << hz->typeScale[i];
        out << "\n";
    }
}
void ReadHitZones(GameObject& go, std::istream& in) {
    auto* hz = go.AddComponent<HitZones>();
    hz->zones.clear();
    std::string tok;
    while (in >> tok) {
        size_t eq = tok.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = tok.substr(0, eq);
        const float v = (float)std::atof(tok.c_str() + eq + 1);
        if (k == "hf") hz->headFromHeight = v;
        else if (k == "lt") hz->legsToHeight = v;
        else if (k.compare(0, 2, "z.") == 0) hz->zones.push_back({k.substr(2), v});
        else if (k.compare(0, 2, "t.") == 0) {
            DamageType t;
            if (DamageTypeFromName(k.substr(2), t)) hz->typeScale[(int)t] = v;
        }
    }
}

} // namespace

namespace modules {
void Register_health_damage(ModuleContext&) {
    health::AddFinder("HealthStat", 0, [](GameObject* g) -> std::unique_ptr<IHealth> {
        if (auto* h = g->GetComponent<HealthStat>()) return std::unique_ptr<IHealth>(new HealthStatAdapter(h));
        return nullptr;
    });
    health::AddModifier("HitZones", [](GameObject* target, DamageEvent& e) {
        if (auto* hz = target->GetComponent<HitZones>()) hz->Scale(e);
    });
    ComponentSerializerRegistry::Entry hz;
    hz.name = "hitzones";
    hz.write = WriteHitZones;
    hz.read = ReadHitZones;
    ComponentSerializerRegistry::Register(std::move(hz));
}
} // namespace modules
} // namespace okay
