#pragma once
#include "okay/Modules/Damage.hpp"
#include <functional>
#include <memory>
#include <string>

namespace okay {

/// Health of one object, whichever component keeps it (HealthStat, SurvivalStats, Stats, NPCController).
/// The components stay as they are, so old scenes load unchanged; each module registers an adapter for its
/// own component (engine/modules/<name>/register.cpp) and everything else asks `health::`.
class IHealth : public IDamageable {
public:
    virtual float Current() const = 0;
    virtual float Max() const = 0;
    virtual float Armor() const = 0;
    virtual bool IsDead() const = 0;
    virtual bool IsInvincible() const = 0;
    /// Returns the health actually restored.
    virtual float Heal(float amount, GameObject* source = nullptr) = 0;
    /// Kill without a damage calculation. `from` (may be null) is where the blow came from.
    virtual void Kill(const Vec3* from = nullptr) = 0;
    /// Bring it back with full health. False when this kind of health cannot come back (an NPC is rebuilt
    /// by its own Start).
    virtual bool Revive() = 0;
    /// Name of the component behind this adapter ("HealthStat"), for logs and tests.
    virtual const char* Model() const = 0;
};

namespace health {

using Finder = std::function<std::unique_ptr<IHealth>(GameObject*)>;
/// Change a hit before the target sees it (hit zones scale it). Runs in registration order.
using Modifier = std::function<void(GameObject* target, DamageEvent&)>;

/// Register how to find health on an object. Lower `priority` is asked first: the object's own HealthStat
/// (0) wins over SurvivalStats (10), RPG Stats (20) and an NPC's own health (30). Same `name` replaces.
void AddFinder(const std::string& name, int priority, Finder f);
void AddModifier(const std::string& name, Modifier m);
/// Forget all finders and modifiers (tests).
void ClearAll();

/// Health on `go`, or null. With `parents`, climbs to the parents (a shot hits a child collider of a body).
std::unique_ptr<IHealth> Find(GameObject* go, bool parents = false);

/// Hurt whatever has health on `go` (or its parents). The one entry point for every damage source.
DamageResult Damage(GameObject* go, DamageEvent e, bool parents = false);
inline DamageResult Damage(GameObject* go, float amount, bool parents = false) {
    return Damage(go, DamageEvent::Of(amount), parents);
}
/// Heal whatever has health on `go`. Returns the health restored (0 when nothing has health).
float Heal(GameObject* go, float amount, GameObject* source = nullptr);

} // namespace health
} // namespace okay
