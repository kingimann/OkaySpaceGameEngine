// The character a piece of UI belongs to, for `owner.` binding keys (see okay/Components/UIOwnerData.hpp).
#include "okay/Components/UIOwnerData.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/Damage.hpp"
#include "okay/Components/NPCController.hpp"
#include "okay/Components/Stats.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Components/SurvivalStats.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"

#include <cmath>
#include <cstdio>

namespace okay {
namespace uiowner {

namespace {
bool HasOwnerData(const GameObject* g) {
    return g->GetComponent<HealthStat>() || g->GetComponent<Stats>() || g->GetComponent<SurvivalStats>() ||
           g->GetComponent<NPCController>() || g->GetComponent<Character>();
}
// Health and its maximum: HealthOf (Damage.hpp), the holder DealDamage hurts, so the bar moves with the hits. (This
// used to check RPG Stats before Survival Stats, the other way round from the damage; #422 audit item 21.)
bool Health(const GameObject* o, float& hp, float& maxHp) { return HealthOf(const_cast<GameObject*>(o), hp, maxHp); }
} // namespace

GameObject* Find(const GameObject* ui) {
    if (!ui || !ui->transform) return nullptr;
    for (const Transform* t = ui->transform->Parent(); t; t = t->Parent()) {
        GameObject* g = t->gameObject;
        if (g && !g->IsDestroyed() && HasOwnerData(g)) return g;
    }
    return nullptr;
}

bool Lookup(const GameObject* ui, const std::string& key, std::string& text, float& number, bool& isNumber) {
    if (!IsOwnerKey(key)) return false;
    const GameObject* o = Find(ui);
    if (!o) return false;
    const std::string k = key.substr(6);
    isNumber = false;
    if (k == "name") { text = o->name; return true; }
    auto num = [&](float v) {
        if (!std::isfinite(v)) v = 0.0f;
        number = v; isNumber = true;
        char buf[32];
        if (std::fabs(v - std::round(v)) < 1e-4f && std::fabs(v) < 1e9f) std::snprintf(buf, sizeof buf, "%lld", (long long)std::llround(v));
        else std::snprintf(buf, sizeof buf, "%.1f", v);
        text = buf;
        return true;
    };
    if (k == "level") {
        if (auto* s = o->GetComponent<Stats>()) return num((float)s->level);
        return false;
    }
    float hp = 0.0f, maxHp = 0.0f;
    if (!Health(o, hp, maxHp)) return false;
    if (k == "health") return num(hp);
    if (k == "maxHealth") return num(maxHp);
    const float frac = maxHp > 0.0f ? Mathf::Clamp01(hp / maxHp) : 0.0f;
    if (k == "healthFrac") return num(frac);
    if (k == "healthPct") return num(std::round(frac * 100.0f));
    return false;
}

} // namespace uiowner
} // namespace okay
