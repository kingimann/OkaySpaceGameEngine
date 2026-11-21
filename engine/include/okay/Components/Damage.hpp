#pragma once
// One damage vocabulary for the whole engine: what kind of damage, how much, who dealt it, who got it.
// Every health holder (Health Stat, Survival Stats, RPG Stats, NPC Controller) takes a DamageEvent and
// tells the same listeners, so melee, shots, traps, quests, scripts and survival all go through one path.
// Written from the engine's own needs; no outside code.
#include "okay/Math/Vec3.hpp"
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace okay {

class GameObject;

/// What kind of damage a hit is. A Health Stat can resist each kind separately.
enum class DamageType : int {
    Generic = 0, Physical, Fire, Cold, Electric, Poison, Explosion, Fall,
    Hunger, Thirst, Drowning, Exhaustion, Madness, Count
};
constexpr int kDamageTypeCount = (int)DamageType::Count;

inline const char* DamageTypeName(DamageType t) {
    static const char* n[] = {"Generic", "Physical", "Fire", "Cold", "Electric", "Poison", "Explosion",
                              "Fall", "Hunger", "Thirst", "Drowning", "Exhaustion", "Madness"};
    int i = (int)t; return (i >= 0 && i < kDamageTypeCount) ? n[i] : "Generic";
}
/// Case-insensitive name to type (for scripts and Visual Script text). False if unknown.
inline bool DamageTypeFromName(const std::string& s, DamageType& out) {
    for (int i = 0; i < kDamageTypeCount; ++i) {
        const char* n = DamageTypeName((DamageType)i);
        if (s.size() != std::strlen(n)) continue;
        bool same = true;
        for (size_t k = 0; k < s.size(); ++k)
            if (std::tolower((unsigned char)s[k]) != std::tolower((unsigned char)n[k])) { same = false; break; }
        if (same) { out = (DamageType)i; return true; }
    }
    return false;
}

/// One hit. `amount` is what was asked for; `dealt` is filled in with what really came off the
/// health (after armor, resistance and the like) when the listeners are told.
struct DamageEvent {
    float amount = 0.0f;
    DamageType type = DamageType::Generic;
    GameObject* source = nullptr;   ///< who dealt it (may be null: a trap, hunger, the world)
    GameObject* target = nullptr;   ///< who took it (filled in by the receiver)
    Vec3 hitPoint{0, 0, 0};         ///< where it came from or landed (only if hasHitPoint)
    bool hasHitPoint = false;
    float dealt = 0.0f;             ///< result: health actually lost
    bool killed = false;            ///< result: this hit killed the target

    static DamageEvent Make(float amount, DamageType type = DamageType::Generic, GameObject* source = nullptr) {
        DamageEvent e; e.amount = amount; e.type = type; e.source = source; return e;
    }
    static DamageEvent From(float amount, const Vec3& from, DamageType type = DamageType::Physical,
                            GameObject* source = nullptr) {
        DamageEvent e = Make(amount, type, source); e.hitPoint = from; e.hasHitPoint = true; return e;
    }
};

/// A damage amount that makes sense: NaN and zero or negative amounts give 0 (no damage);
/// an infinite amount is a huge but finite one (it kills, and does no further harm).
inline float SaneDamage(float a) {
    if (std::isnan(a) || !(a > 0.0f)) return 0.0f;
    return std::isinf(a) ? 3.0e38f : a;
}
/// The same for healing.
inline float SaneHeal(float a) { return SaneDamage(a); }

/// Listeners told about every hit, heal and death on any health holder. Add yours with the
/// matching Add function; they stay until ClearDamageListeners(). Order for one lethal hit:
/// damaged, then death.
struct DamageListeners {
    std::vector<std::function<void(const DamageEvent&)>> damaged;
    std::vector<std::function<void(GameObject*, float)>> healed;       ///< (who, amount really healed)
    std::vector<std::function<void(const DamageEvent&)>> death;
};
inline DamageListeners& Listeners() { static DamageListeners l; return l; }
inline void AddDamagedListener(std::function<void(const DamageEvent&)> f) { Listeners().damaged.push_back(std::move(f)); }
inline void AddHealedListener(std::function<void(GameObject*, float)> f)   { Listeners().healed.push_back(std::move(f)); }
inline void AddDeathListener(std::function<void(const DamageEvent&)> f)   { Listeners().death.push_back(std::move(f)); }
inline void ClearDamageListeners() { Listeners().damaged.clear(); Listeners().healed.clear(); Listeners().death.clear(); }
inline void NotifyDamaged(const DamageEvent& e) { for (auto& f : Listeners().damaged) if (f) f(e); }
inline void NotifyHealed(GameObject* who, float amount) { for (auto& f : Listeners().healed) if (f) f(who, amount); }
inline void NotifyDeath(const DamageEvent& e) { for (auto& f : Listeners().death) if (f) f(e); }

/// The one way to hurt something. Finds the object's health holder (Health Stat first, then
/// Survival Stats, RPG Stats, NPC Controller), applies the hit and tells the listeners.
/// Returns true if something took the hit (false for an already dead or immune target).
bool DealDamage(GameObject* target, DamageEvent ev);
/// Convenience: plain amount, type, source.
bool DealDamage(GameObject* target, float amount, DamageType type = DamageType::Generic, GameObject* source = nullptr);
/// Heal the object's health holder. Returns the health really gained.
float HealObject(GameObject* target, float amount);
/// Kill the object's health holder outright. True if something died.
bool KillObject(GameObject* target, GameObject* source = nullptr);
/// Current / max health of whichever holder the object has (false if none).
bool HealthOf(GameObject* target, float& health, float& maxHealth);
/// True if the object has a health holder and it is dead.
bool IsObjectDead(GameObject* target);

} // namespace okay
