// Module health_damage: the `hitzones` scene record. The damage path itself is main's one path
// (okay/Components/Damage.hpp: DealDamage, HealthOf), which applies HitZones. Linked only when the module is on
// (cmake/OkayModules.cmake).
#include "okay/Components/HitZones.hpp"
#include "okay/Components/Damage.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Scene/ComponentSerializer.hpp"
#include "okay/Scene/PropertyBuilder.hpp"
#include <algorithm>
#include <cstdlib>
#include <istream>
#include <ostream>
#include <sstream>

namespace okay {
namespace {

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
            if (hz->typeScale[i] != 1.0f) out << " t." << DamageTypeName((DamageType)i) << "=" << hz->typeScale[i];   // DamageTypeFromName ignores case
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
    ComponentSerializerRegistry::Entry hz;
    hz.name = "hitzones";
    hz.write = WriteHitZones;
    hz.read = ReadHitZones;
    ComponentSerializerRegistry::Register(std::move(hz));
    // Property table (the Inspector, scripts, the save coverage gate); the zone list and the per-type scales are saved by
    // the record above only.
    PropertyBuilder<HitZones> b("HitZones", "hitzones", "Gameplay");
    b.AddFloat("headFromHeight", &HitZones::headFromHeight).Range(0, 50, 0.05).Tooltip("Height above the feet where the head starts (m)")
     .AddFloat("legsToHeight", &HitZones::legsToHeight).Range(0, 50, 0.05).Tooltip("Height above the feet where the legs end (m)");
    b.Commit();
}
} // namespace modules
} // namespace okay
