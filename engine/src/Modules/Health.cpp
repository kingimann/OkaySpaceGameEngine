#include "okay/Modules/Health.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include <algorithm>
#include <vector>

namespace okay {

namespace {
const char* const kTypeNames[kDamageTypeCount] = {"generic", "bullet", "explosion", "melee", "fall", "fire",
                                                  "cold", "poison", "starvation", "drowning", "collision", "environment"};
struct FinderEntry { std::string name; int priority; health::Finder fn; };
struct ModifierEntry { std::string name; health::Modifier fn; };
std::vector<FinderEntry>& Finders() { static std::vector<FinderEntry> v; return v; }
std::vector<ModifierEntry>& Modifiers() { static std::vector<ModifierEntry> v; return v; }
// The modules add their adapters in their register hooks, which run the first time the module list is read.
void EnsureModules() { (void)ModuleRegistry::Get(); }
} // namespace

const char* DamageTypeName(DamageType t) {
    int i = (int)t;
    return (i >= 0 && i < kDamageTypeCount) ? kTypeNames[i] : "generic";
}
bool DamageTypeFromName(const std::string& name, DamageType& out) {
    for (int i = 0; i < kDamageTypeCount; ++i)
        if (name == kTypeNames[i]) { out = (DamageType)i; return true; }
    return false;
}

namespace health {

void AddFinder(const std::string& name, int priority, Finder f) {
    auto& v = Finders();
    for (auto it = v.begin(); it != v.end(); ++it)
        if (it->name == name) { v.erase(it); break; }
    FinderEntry e{name, priority, std::move(f)};
    auto pos = std::upper_bound(v.begin(), v.end(), e,
                                [](const FinderEntry& a, const FinderEntry& b) { return a.priority < b.priority; });
    v.insert(pos, std::move(e));
}

void AddModifier(const std::string& name, Modifier m) {
    auto& v = Modifiers();
    for (auto& e : v) if (e.name == name) { e.fn = std::move(m); return; }
    v.push_back({name, std::move(m)});
}

void ClearAll() { Finders().clear(); Modifiers().clear(); }

std::unique_ptr<IHealth> Find(GameObject* go, bool parents) {
    EnsureModules();
    for (GameObject* g = go; g;) {
        for (auto& f : Finders())
            if (auto h = f.fn(g)) return h;
        if (!parents) break;
        Transform* p = g->transform ? g->transform->Parent() : nullptr;
        g = p ? p->gameObject : nullptr;
    }
    return nullptr;
}

DamageResult Damage(GameObject* go, DamageEvent e, bool parents) {
    DamageResult none;
    if (!go) return none;
    EnsureModules();
    // Find the object that has health first, so the modifiers (HitZones) are read from that object.
    GameObject* target = nullptr;
    std::unique_ptr<IHealth> h;
    for (GameObject* g = go; g && !h;) {
        for (auto& f : Finders())
            if ((h = f.fn(g))) { target = g; break; }
        if (h || !parents) break;
        Transform* p = g->transform ? g->transform->Parent() : nullptr;
        g = p ? p->gameObject : nullptr;
    }
    if (!h) return none;
    for (auto& m : Modifiers()) m.fn(target, e);
    DamageResult r = h->ApplyDamage(e);
    r.handled = true;
    return r;
}

float Heal(GameObject* go, float amount, GameObject* source) {
    if (amount <= 0.0f) return 0.0f;
    auto h = Find(go, false);
    return h ? h->Heal(amount, source) : 0.0f;
}

} // namespace health
} // namespace okay
